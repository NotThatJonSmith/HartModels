#pragma once

#include <type_traits>
#include <cstdint>

#include <Hart.hpp>

#include <Translators/DirectTranslator.hpp>
#include <Transactors/DirectTransactor.hpp>
#include <Transactors/TranslatingTransactor.hpp>
#include <Decoders/PrecomputedDecoder.hpp>
#include <Translators/CacheWrappedTranslator.hpp>

#include <Spigot.hpp>


// TODO accelerated WFI states?
template<
    typename XLEN_t,
    unsigned int TranslationCacheSizePoT,
    bool SkipBusForFetches,
    // TODO optional (always on right now):  bool SkipBusForPageTables
    unsigned int FetchThreadDepth
    // bool UseBranchFreeTranslator, // Dubious!
    // unsigned int CachedDecodedBasicBlocks, // not real yet...
>
class ThreadedHart final : public Hart<XLEN_t> {

private:

    DirectTransactor<XLEN_t> busPATransactor;
    DirectTransactor<XLEN_t> memPATransactor;
    DirectTranslator<XLEN_t> translator;
    CacheWrappedTranslator<XLEN_t, TranslationCacheSizePoT> cachedTranslator;
    TranslatingTransactor<XLEN_t, true> busVATransactor;
    TranslatingTransactor<XLEN_t, true> memVATransactor;
    PrecomputedDecoder<XLEN_t> decoder;

    struct FetchData {
        // TODO
    } fetchData;

    XLEN_t fetchAheadVPC;
    Spigot<FetchedInstruction<XLEN_t>, FetchData, FetchThreadDepth> fetchService;


public:

    ThreadedHart(CASK::IOTarget* bus, CASK::IOTarget* mem, __uint32_t maximalExtensions) :
        Hart<XLEN_t>(maximalExtensions),
        busPATransactor(bus),
        memPATransactor(mem),
        translator(&this->state, &memPATransactor), // TODO make optional busPAT
        cachedTranslator(&translator),
        busVATransactor(&cachedTranslator, &busPATransactor),
        memVATransactor(&cachedTranslator, &memPATransactor),
        decoder(&this->state),
        fetchService(std::bind(&ThreadedHart::FetchThread, this, std::placeholders::_1)) {
        this->state.implCallback = std::bind(&ThreadedHart::Callback, this, std::placeholders::_1);
        fetchService.Run();
        // TODO callback for changing XLENs
    };

    virtual inline void BeforeFirstTick() override {
        Reset();
        DoFetch();
    };

    virtual inline void Tick() override {
        // TODO HartState function?
        this->state.currentFetch->instruction.execute(
            this->state.currentFetch->operands,
            &this->state,
            &busVATransactor);
        DoFetch();
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
        cachedTranslator.Clear();
        decoder.Configure(&this->state);
    };

private:

    inline void FetchThread(FetchedInstruction<XLEN_t>* prefetch) {
        prefetch->virtualPC = fetchAheadVPC;
        Transaction<XLEN_t> transaction;
        if constexpr (SkipBusForFetches) {
            transaction = memVATransactor.Fetch(
                fetchAheadVPC, 
                sizeof(prefetch->encoding),
                (char*)&prefetch->encoding);
            // if (transaction.size != sizeof(this->state.currentFetch->encoding)) // TODO what if?
        } else {
            transaction = busVATransactor.Fetch(
                fetchAheadVPC,
                sizeof(prefetch->encoding),
                (char*)&prefetch->encoding);
            // if (transaction.size != sizeof(this->state.currentFetch->encoding)) // TODO what if?
        }
        fetchAheadVPC += RISCV::isCompressed(prefetch->encoding) ? 2 : 4;
        prefetch->deferredTrap = transaction.trapCause;
        if (prefetch->deferredTrap != RISCV::TrapCause::NONE) {
            return;
        }
    };

    inline void DoFetch() {

        while (true) {

            fetchService.Advance();
            this->state.currentFetch = fetchService.Current();

            if (this->state.currentFetch->virtualPC != this->state.nextFetchVirtualPC) {
                fetchService.Pause();
                fetchAheadVPC = this->state.nextFetchVirtualPC;
                fetchService.Run();
                continue;
            }

            this->state.currentFetch->virtualPC = this->state.nextFetchVirtualPC;
            this->state.currentFetch->encoding = this->state.currentFetch->encoding;

            if (this->state.currentFetch->deferredTrap != RISCV::TrapCause::NONE) {
                this->state.RaiseException(this->state.currentFetch->deferredTrap, this->state.currentFetch->virtualPC);
                fetchService.Pause();
                fetchAheadVPC = this->state.nextFetchVirtualPC;
                fetchService.Run();
                continue;
            }

            break;
        }
        
        this->state.currentFetch->instruction = decoder.Decode(this->state.currentFetch->encoding);
        this->state.currentFetch->operands = this->state.currentFetch->instruction.getOperands(this->state.currentFetch->encoding);
        this->state.nextFetchVirtualPC += this->state.currentFetch->instruction.width;
    }

    inline void Callback(HartCallbackArgument arg) {
        cachedTranslator.Clear();

        // TODO Operating XLEN changing also changes this.
        // TODO cut out work by giving me old & new MISA values etc?
        if (arg == HartCallbackArgument::ChangedMISA)
            decoder.Configure(&this->state);

        if (arg == HartCallbackArgument::RequestedIfence) { // TODO vmfence?
            fetchService.Pause();
            fetchAheadVPC = this->state.nextFetchVirtualPC;
            fetchService.Run();
        }
        return;
    }

};
