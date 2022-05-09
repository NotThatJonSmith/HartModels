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

public:

    DirectTransactor<XLEN_t> paTransactor;
    DirectTranslator<XLEN_t> translator;
    TranslatingTransactor<XLEN_t, true> vaTransactor;
    DirectDecoder<XLEN_t> decoder;
    typename HartState<XLEN_t>::Fetch fetch;
    HartState<XLEN_t> state;

public:

    SimpleHart(CASK::IOTarget* bus, __uint32_t maximalExtensions) :
        paTransactor(bus),
        translator(&state, &paTransactor),
        vaTransactor(&translator, &paTransactor),
        state(maximalExtensions) {
        state.currentFetch = &fetch;
    };

    virtual inline void BeforeFirstTick() override {
        Reset();
        FetchInto(&fetch, state.nextFetchVirtualPC);
    };

    virtual inline void Tick() override {
        fetch.instruction.execute(fetch.operands, &state);
        FetchInto(&fetch, state.nextFetchVirtualPC);
    };

    virtual inline void Reset() override {
        state.Reset(resetVector);
    };

private:

    inline void FetchInto(typename HartState<XLEN_t>::Fetch* fetch_into, XLEN_t pc) {
        bool need_fetch = true;
        while (need_fetch) {
            need_fetch = false;
            fetch_into->virtualPC = pc;
            Transaction<XLEN_t> transaction = vaTransactor.Fetch(pc, sizeof(fetch_into->encoding), (char*)&fetch_into->encoding);
            if (transaction.trapCause != RISCV::TrapCause::NONE) {
                state.RaiseException(transaction.trapCause, fetch_into->virtualPC);
                need_fetch = true;
            }
            // if (transaction.size != sizeof(fetch_into->encoding)) // TODO what if?
        }
        fetch_into->instruction = decoder.Decode(fetch_into->encoding);
        fetch_into->encoding &= fetch_into->instruction.width == 2 ? 0x0000ffff : 0xffffffff; // TODO strictly necessary?
        fetch_into->operands = fetch_into->instruction.getOperands(fetch_into->encoding);
        state.nextFetchVirtualPC += fetch_into->instruction.width;
    }
};
