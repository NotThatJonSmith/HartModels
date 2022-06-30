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
    bool SkipBusForFetches
    // TODO optional (always on right now):  bool SkipBusForPageTables
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
    FetchedInstruction<XLEN_t> fetch;

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
        this->state.currentFetch = &fetch;
        // TODO callback for changing XLENs
    };

    virtual inline void BeforeFirstTick() override {
        Reset();
        DoFetch();
    };

    virtual inline void Tick() override {
        // TODO HartState function?
        fetch.instruction.execute(fetch.operands, &this->state, &busVATransactor);
        DoFetch();
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
        cachedTranslator.Clear();
        decoder.Configure(&this->state);
    };

private:

    inline void DoFetch() {

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

    inline void Callback(HartCallbackArgument arg) {

        // todo only sometimes
        cachedTranslator.Clear();

        // TODO Operating XLEN changing also changes this.
        // TODO cut out work by giving me old & new MISA values etc?
        if (arg == HartCallbackArgument::ChangedMISA)
            decoder.Configure(&this->state);

        return;
    }

};
