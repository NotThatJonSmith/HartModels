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

    typename HartState<XLEN_t>::Fetch fetch;

    // Ugh, this is gross:
    struct FetchFrame {
        typename HartState<XLEN_t>::Fetch fetch;
        RISCV::TrapCause deferredTrap;
    };
    XLEN_t fetchAheadVPC;
    FetchFrame* currentFetchFrame;
    Spigot<FetchFrame, FetchThreadDepth> fetchService;

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
            this->state.currentFetch = &fetch;
        }
        // TODO callback for changing XLENs
    };

    virtual inline void BeforeFirstTick() override {
        Reset();
        DoFetch();
    };

    virtual inline void Tick() override {

        // ptr-chasing is less performant but needed for thread; makes sense...
        if constexpr (FetchThreadDepth > 1) {
            fetch.instruction.execute(fetch.operands, &this->state, &busVATransactor);
        } else  {
            this->state.currentFetch->instruction.execute(this->state.currentFetch->operands, &this->state, &busVATransactor);
        }
        DoFetch();
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
        cachedTranslator.Clear();
        decoder.Configure(&this->state);
    };

private:

    inline void FetchThread(FetchFrame* frame) {
        frame->fetch.virtualPC = fetchAheadVPC;
        Transaction<XLEN_t> transaction;
        if constexpr (SkipBusForFetches) {
            transaction = memVATransactor.Fetch(
                fetchAheadVPC, 
                sizeof(frame->fetch.encoding),
                (char*)&frame->fetch.encoding);
        } else {
            transaction = busVATransactor.Fetch(
                fetchAheadVPC,
                sizeof(frame->fetch.encoding),
                (char*)&frame->fetch.encoding);
        }
        frame->deferredTrap = transaction.trapCause;
        // if (transaction.size != sizeof(fetch.encoding)) // TODO what if?
        if (frame->deferredTrap != RISCV::TrapCause::NONE) {
            return;
        }
        frame->fetch.instruction = decoder.Decode(frame->fetch.encoding);
        frame->fetch.operands = frame->fetch.instruction.getOperands(fetch.encoding);
        fetchAheadVPC += frame->fetch.instruction.width;
    };

    inline void DoFetch() {

        if constexpr (FetchThreadDepth > 1) {
            while (true) {

                currentFetchFrame = fetchService.Next();
                this->state.currentFetch = &currentFetchFrame->fetch;

                if (this->state.currentFetch->virtualPC != this->state.nextFetchVirtualPC) {
                    fetchService.Pause();
                    fetchAheadVPC = this->state.nextFetchVirtualPC;
                    fetchService.Run();
                    continue;
                }

                if (currentFetchFrame->deferredTrap != RISCV::TrapCause::NONE) {
                    this->state.RaiseException(currentFetchFrame->deferredTrap, currentFetchFrame->fetch.virtualPC);
                    continue;
                }

                this->state.nextFetchVirtualPC += this->state.currentFetch->instruction.width;
                break;
            }
        } else {
            while (true) {
                fetch.virtualPC = this->state.nextFetchVirtualPC;
                Transaction<XLEN_t> transaction;
                if constexpr (SkipBusForFetches) {
                    transaction = memVATransactor.Fetch(
                        this->state.nextFetchVirtualPC, 
                        sizeof(fetch.encoding),
                        (char*)&fetch.encoding);
                } else {
                    transaction = busVATransactor.Fetch(
                        this->state.nextFetchVirtualPC,
                        sizeof(fetch.encoding),
                        (char*)&fetch.encoding);
                }
                if (transaction.trapCause != RISCV::TrapCause::NONE) {
                    this->state.RaiseException(transaction.trapCause, fetch.virtualPC);
                    continue;
                }
                break;
                // if (transaction.size != sizeof(fetch.encoding)) // TODO what if?
            }
            fetch.instruction = decoder.Decode(fetch.encoding);
            fetch.operands = fetch.instruction.getOperands(fetch.encoding);
            this->state.nextFetchVirtualPC += fetch.instruction.width;
        }
    }

    inline void Callback(HartCallbackArgument arg) {
        cachedTranslator.Clear();

        // TODO Operating XLEN changing also changes this.
        // TODO cut out work by giving me old & new MISA values etc?
        if (arg == HartCallbackArgument::ChangedMISA)
            decoder.Configure(&this->state);
        return;
    }

};
