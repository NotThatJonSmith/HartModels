#pragma once

#include <type_traits>
#include <cstdint>

#include <Hart.hpp>

#include <Translators/DirectTranslator.hpp>
#include <Transactors/DirectTransactor.hpp>
#include <Transactors/TranslatingTransactor.hpp>
#include <Decoders/DirectDecoder.hpp>


template<typename XLEN_t>
class SimpleHart final : public Hart<XLEN_t> {

private:

    DirectTransactor<XLEN_t> paTransactor;
    DirectTranslator<XLEN_t> translator;
    TranslatingTransactor<XLEN_t, true> vaTransactor;
    DirectDecoder<XLEN_t> decoder;
    FetchedInstruction<XLEN_t> fetch;

public:

    SimpleHart(CASK::IOTarget* bus, __uint32_t maximalExtensions) :
        Hart<XLEN_t>(maximalExtensions),
        paTransactor(bus),
        translator(&this->state, &paTransactor),
        vaTransactor(&translator, &paTransactor),
        decoder(&this->state) {
        // TODO callback for changing XLEN!

        this->state.currentFetch = &fetch;

    };

    virtual inline void BeforeFirstTick() override {
        Reset();
        DoFetch();
    };

    virtual inline void Tick() override {
        fetch.instruction.execute(
            fetch.operands,
            &this->state,
            &vaTransactor);
        DoFetch();
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
    };

private:

    inline void DoFetch() {
        // TODO double-fault guard?
        while (true) {
            fetch.virtualPC = this->state.nextFetchVirtualPC;
            Transaction<XLEN_t> transaction = vaTransactor.Fetch(this->state.nextFetchVirtualPC, sizeof(fetch.encoding), (char*)&fetch.encoding);
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
};
