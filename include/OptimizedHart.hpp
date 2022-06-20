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
class OptimizedHart final : public Hart<XLEN_t> {

private:

    DirectTransactor<XLEN_t> busPATransactor;
    DirectTransactor<XLEN_t> memPATransactor;
    DirectTranslator<XLEN_t> translator;
    CacheWrappedTranslator<XLEN_t, TranslationCacheSizePoT> cachedTranslator;
    TranslatingTransactor<XLEN_t, true> busVATransactor;
    TranslatingTransactor<XLEN_t, true> memVATransactor;
    PrecomputedDecoder<XLEN_t> decoder;

    FetchedInstruction<XLEN_t> singleFetch;
    XLEN_t fetchAheadVPC;
    Spigot<FetchedInstruction<XLEN_t>, FetchThreadDepth> fetchService;

public:

    OptimizedHart(CASK::IOTarget* bus, CASK::IOTarget* mem, __uint32_t maximalExtensions) :
        Hart<XLEN_t>(maximalExtensions),
        busPATransactor(bus),
        memPATransactor(mem),
        translator(&this->state, &memPATransactor), // TODO make optional busPAT
        cachedTranslator(&translator),
        busVATransactor(&cachedTranslator, &busPATransactor),
        memVATransactor(&cachedTranslator, &memPATransactor),
        decoder(&this->state) {
        this->state.implCallback = std::bind(&OptimizedHart::Callback, this, std::placeholders::_1);
        if constexpr (FetchThreadDepth > 1) {
            fetchService.SetProducer(std::bind(&OptimizedHart::FetchThread, this, std::placeholders::_1));
            fetchService.Run();
        } else {
            this->state.currentFetch = &singleFetch;
        }
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

        if constexpr (FetchThreadDepth > 1) {

            while (true) {

                this->state.currentFetch = fetchService.Next();

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
        } else {
            while (true) {
                singleFetch.virtualPC = this->state.nextFetchVirtualPC;
                Transaction<XLEN_t> transaction;
                if constexpr (SkipBusForFetches) {
                    transaction = memVATransactor.Fetch(
                        this->state.nextFetchVirtualPC, 
                        sizeof(singleFetch.encoding),
                        (char*)&singleFetch.encoding);
                } else {
                    transaction = busVATransactor.Fetch(
                        this->state.nextFetchVirtualPC,
                        sizeof(singleFetch.encoding),
                        (char*)&singleFetch.encoding);
                }
                if (transaction.trapCause != RISCV::TrapCause::NONE) {
                    this->state.RaiseException(transaction.trapCause, singleFetch.virtualPC);
                    continue;
                }
                break;
                // if (transaction.size != sizeof(singleFetch.encoding)) // TODO what if?
            }
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

        if constexpr (FetchThreadDepth > 1) {
            if (arg == HartCallbackArgument::RequestedIfence) {
                fetchService.Pause();
                fetchAheadVPC = this->state.nextFetchVirtualPC;
                fetchService.Run();
            }
        }
        return;
    }

};
