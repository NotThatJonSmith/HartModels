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

public:

    SimpleHart(CASK::IOTarget* bus, __uint32_t maximalExtensions) :
        Hart<XLEN_t>(maximalExtensions),
        paTransactor(bus),
        translator(&this->state, &paTransactor),
        vaTransactor(&translator, &paTransactor),
        decoder(&this->state) {
        // TODO callback for changing XLEN!
    };

    virtual inline void BeforeFirstTick() override {
        Reset();
        DoFetch();
    }

    virtual inline void Tick() override {
        decoder.Decode(this->state.inst)(&this->state, &vaTransactor);
        DoFetch();
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
    };

private:

    inline void DoFetch() {
        // TODO double-fault guard?
        while (true) {
            this->state.pc = this->state.nextPC;
            Transaction<XLEN_t> transaction = vaTransactor.Fetch(this->state.nextPC, sizeof(this->state.inst), (char*)&this->state.inst);
            if (transaction.trapCause != RISCV::TrapCause::NONE) {
                this->state.RaiseException(transaction.trapCause, this->state.pc);
                continue;
            }
            break;
            // if (transaction.size != sizeof(this->state.inst)) // TODO what if?
        }
        this->state.nextPC += RISCV::instructionLength(this->state.inst);
    }
};
