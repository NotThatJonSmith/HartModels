#pragma once

#include <type_traits>
#include <cstdint>

#include <Hart.hpp>

#include <Translators/DirectTranslator.hpp>
#include <Transactors/DirectTransactor.hpp>
#include <Transactors/TranslatingTransactor.hpp>
#include <Decoders/PrecomputedDecoder.hpp>
#include <Translators/CacheWrappedTranslator.hpp>

// TODO accelerated WFI states?
template<
    typename XLEN_t,
    unsigned int TranslationCacheSizePoT,
    bool SkipBusForFetches,
    unsigned int maxBasicBlockLength,
    unsigned int numNextBlocks,
    unsigned int bbCacheSizePoT
    // TODO optional (always on right now):  bool SkipBusForPageTables
>
class OptimizedHart final : public Hart<XLEN_t> {

private:

    DirectTransactor<XLEN_t> busPATransactor;
    DirectTransactor<XLEN_t> memPATransactor;
    DirectTranslator<XLEN_t> memTranslator;
    DirectTranslator<XLEN_t> busTranslator;
    CacheWrappedTranslator<XLEN_t, TranslationCacheSizePoT> cachedTranslator;
    TranslatingTransactor<XLEN_t, false> busVATransactor;
    TranslatingTransactor<XLEN_t, false> memVATransactor;
    PrecomputedDecoder<XLEN_t> decoder;

    struct SimplyCachedInstruction {
        XLEN_t full_pc;
        __uint32_t encoding = 0;
        DecodedInstruction<XLEN_t> instruction = nullptr;
    };
    SimplyCachedInstruction icache[0x10000];

public:

    OptimizedHart(CASK::IOTarget* bus, CASK::IOTarget* mem, __uint32_t maximalExtensions) :
        Hart<XLEN_t>(maximalExtensions),
        busPATransactor(bus),
        memPATransactor(mem),
        memTranslator(&this->state, &memPATransactor),
        busTranslator(&this->state, &busPATransactor),
        cachedTranslator(&memTranslator),
        busVATransactor(&cachedTranslator, &busPATransactor),
        memVATransactor(&cachedTranslator, &memPATransactor),
        decoder(&this->state) {
        this->state.implCallback = std::bind(&OptimizedHart::Callback, this, std::placeholders::_1);
        // TODO callback for changing XLENs
        Reset();
    };

    virtual inline unsigned int Tick() override {
        for (unsigned int i = 0; i < 10000; i++) {
            SimplyCachedInstruction inst = icache[(this->state.pc >> 1) & 0xffff];
            if (inst.full_pc == this->state.pc) [[ likely ]] {
                inst.instruction(inst.encoding, &this->state, &busVATransactor);
            } else {
                __uint32_t encoding;
                Transaction<XLEN_t> transaction;
                if constexpr (SkipBusForFetches) {
                    transaction = memVATransactor.Fetch(this->state.pc, sizeof(encoding), (char*)&encoding);
                } else {
                    transaction = busVATransactor.Fetch(this->state.pc, sizeof(encoding), (char*)&encoding);
                }
                if (transaction.trapCause == RISCV::TrapCause::NONE) {
                    DecodedInstruction<XLEN_t> decoded = decoder.Decode(encoding);
                    icache[(this->state.pc >> 1) & 0xffff] = { this->state.pc, encoding, decoded };
                    decoded(encoding, &this->state, &busVATransactor);
                } else {
                    this->state.RaiseException(transaction.trapCause, this->state.pc);
                }
            }
        }
        return 10000;
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
        cachedTranslator.Clear();
        decoder.Configure(&this->state);
        memset(icache, 0, sizeof(icache));
    };

    virtual inline Transactor<XLEN_t>* getVATransactor() override {
        return &busVATransactor;
    }

private:

    static inline bool instructionCanBranch(DecodedInstruction<XLEN_t> instruction) {
        // TODO, maybe some of these aren't 100% necessarily basic-block-breakers
        // TODO perf: this check can be abolished if there is a hart callback for "I might branch" or some other slickness
        return (
            instruction == ex_jal<XLEN_t>     || instruction == ex_jalr<XLEN_t>         ||
            instruction == ex_fence<XLEN_t>   || instruction == ex_fencei<XLEN_t>       ||
            instruction == ex_sfencevma<XLEN_t> ||
            instruction == ex_branch_generic<XLEN_t, std::equal_to<XLEN_t>>      ||
            instruction == ex_branch_generic<XLEN_t, std::not_equal_to<XLEN_t>>  ||
            instruction == ex_branch_generic<XLEN_t, std::less<XLEN_t>>           ||
            instruction == ex_branch_generic<XLEN_t, std::greater_equal<XLEN_t>>  ||
            instruction == ex_branch_generic<XLEN_t, std::less<std::make_signed_t<XLEN_t>>>          ||
            instruction == ex_branch_generic<XLEN_t, std::greater_equal<std::make_signed_t<XLEN_t>>> ||
            instruction == ex_trap_return<XLEN_t, RISCV::PrivilegeMode::Machine> ||
            instruction == ex_trap_return<XLEN_t, RISCV::PrivilegeMode::Supervisor> ||
            instruction == ex_trap_return<XLEN_t, RISCV::PrivilegeMode::User> ||
            instruction == ex_cjal<XLEN_t>    || instruction == ex_cj<XLEN_t>        ||
            instruction == ex_cbeqz<XLEN_t>   || instruction == ex_cbnez<XLEN_t>     ||
            instruction == ex_cjalr<XLEN_t>   || instruction == ex_cjr<XLEN_t>
        );
    }

    inline void Callback(HartCallbackArgument arg) {
        if (arg == HartCallbackArgument::RequestedVMfence)
            cachedTranslator.Clear();
        if (arg == HartCallbackArgument::RequestedIfence || arg == HartCallbackArgument::RequestedVMfence)
            memset(icache, 0, sizeof(icache));
        if (arg == HartCallbackArgument::ChangedMISA) {
            memset(icache, 0, sizeof(icache));
            decoder.Configure(&this->state);
        }
        return;
    }

};
