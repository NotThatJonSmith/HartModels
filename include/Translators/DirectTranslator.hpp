#pragma once

#include <Translator.hpp>
#include <RiscVTranslationAlgorithm.hpp>

template<typename XLEN_t>
class DirectTranslator final : public Translator<XLEN_t> {

private:

    const HartState<XLEN_t>* state;
    const Transactor<XLEN_t>* transactor;

public:

    DirectTranslator(HartState<XLEN_t>* hartState, Transactor<XLEN_t>* sourceTransactor)
        : state(hartState), transactor(sourceTransactor) {
    };

    virtual inline Translation<XLEN_t> TranslateRead(XLEN_t address) override {
        return TranslateInternal<IOVerb::Read>(address);
    }

    virtual inline Translation<XLEN_t> TranslateWrite(XLEN_t address) override {
        return TranslateInternal<IOVerb::Write>(address);
    }

    virtual inline Translation<XLEN_t> TranslateFetch(XLEN_t address) override {
        return TranslateInternal<IOVerb::Fetch>(address);
    }

private:

    // The MPRV (Modify PRiVilege) bit modifies the privilege level at which
    // loads and stores execute in all privilege modes. When MPRV=0, loads and
    // stores behave as normal, using the translation and protection mechanisms
    // of the current privilege mode. When MPRV=1, load and store memory
    // addresses are translated and protected as though the current privilege
    // mode were set to MPP. Instruction address-translation and protection are
    // unaffected by the setting of MPRV. MPRV is hardwired to 0 if U-mode is
    // not supported.
    template<IOVerb verb>
    inline Translation<XLEN_t> TranslateInternal(XLEN_t address) {
    
        return TranslationAlgorithm<XLEN_t, verb>(
            address,
            transactor,
            state->ppn,
            state->pagingMode,
            state->modifyMemoryPrivilege ? state->machinePreviousPrivilege
                                         : state->privilegeMode,
            state->makeExecutableReadable,
            state->supervisorUserMemoryAccess);
    }

};
