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
    unsigned int cachedBasicBlocks,
    unsigned int maxBasicBlockLength
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

    struct CachedInstruction {
        __uint32_t encoding;
        DecodedInstruction<XLEN_t> instruction;
    };

    struct CachedBasicBlock {
        XLEN_t pc;
        unsigned int length;
        CachedInstruction instructions[maxBasicBlockLength];
    };

    CachedBasicBlock BBCache[cachedBasicBlocks];
    bool bbCacheIsWritingBlock = false;
    unsigned int bbCacheBlockWriteCursor = 0;
    unsigned int bbCacheWriteIndex = 0;
    bool trapTakenDuringInstruction = false;

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
    };

    virtual inline void BeforeFirstTick() override {
        Reset();

        // Really just here to pre-warm the disassembly if we're doing that
        memVATransactor.Fetch(this->state.pc, sizeof(this->state.inst), (char*)&this->state.inst);
        // if (transaction.trapCause != RISCV::TrapCause::NONE) {
            // TODO what if? This is definitely fatal - a trap on the first fetch ever
        // }
    }


    virtual inline void Tick() override {

        trapTakenDuringInstruction = false;

        // If I'm not writing a basic block right now
        if (!bbCacheIsWritingBlock) {

            // Read through the whole basic block cache
            unsigned int bbCacheReadIndex = bbCacheWriteIndex;
            do {

                // If I can find a matching PC, just execute the whole BB right now and bail!
                if (BBCache[bbCacheReadIndex].pc == this->state.pc && BBCache[bbCacheReadIndex].length != 0) {

                    for (unsigned int i = 0; i < BBCache[bbCacheReadIndex].length; i++) {

                        this->state.inst = BBCache[bbCacheReadIndex].instructions[i].encoding;
                        BBCache[bbCacheReadIndex].instructions[i].instruction(&this->state, &busVATransactor);
                        if (trapTakenDuringInstruction) {
                            break;
                        }
                    }
                    return;
                }

                // Reading backward from a ring buffer that we write forward into is a cheap pseudo LRU
                bbCacheReadIndex = ((int)bbCacheReadIndex-1) % cachedBasicBlocks;

            } while (bbCacheReadIndex != bbCacheWriteIndex);
        }

        // If we're not writing a basic block right now, but we don't have this PC in the cache, we open a block:
        if (!bbCacheIsWritingBlock) {
            bbCacheWriteIndex = (bbCacheWriteIndex + 1) % cachedBasicBlocks;
            bbCacheIsWritingBlock = true;
            bbCacheBlockWriteCursor = 0;
            BBCache[bbCacheWriteIndex].length = 0;
            BBCache[bbCacheWriteIndex].pc = this->state.pc;
        }

        // Now we are definitely executing one-by-one and writing down what we do in the BB cache!

        Transaction<XLEN_t> transaction;
        if constexpr (SkipBusForFetches) {
            transaction = memVATransactor.Fetch(this->state.pc, sizeof(this->state.inst), (char*)&this->state.inst);
        } else {
            transaction = busVATransactor.Fetch(this->state.pc, sizeof(this->state.inst), (char*)&this->state.inst);
        }

        if (transaction.trapCause == RISCV::TrapCause::NONE) {
            DecodedInstruction<XLEN_t> instr = decoder.Decode(this->state.inst);
            BBCache[bbCacheWriteIndex].instructions[bbCacheBlockWriteCursor].encoding = this->state.inst;
            BBCache[bbCacheWriteIndex].instructions[bbCacheBlockWriteCursor].instruction = instr;
            BBCache[bbCacheWriteIndex].length++;
            bbCacheBlockWriteCursor++;
            if (bbCacheBlockWriteCursor == maxBasicBlockLength || instructionCanBranch(instr)) {
                bbCacheIsWritingBlock = false;
            }
            instr(&this->state, &busVATransactor);
        } else {
            this->state.RaiseException(transaction.trapCause, this->state.pc);
        }
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
        cachedTranslator.Clear();
        decoder.Configure(&this->state);
        ClearBlockCache();
    };

private:

    static inline bool instructionCanBranch(DecodedInstruction<XLEN_t> instruction) {
        // TODO, maybe some of these aren't 100% necessary
        return (
            instruction == ex_jal<XLEN_t> ||
            instruction == ex_jalr<XLEN_t> ||
            instruction == ex_beq<XLEN_t> ||
            instruction == ex_bne<XLEN_t> ||
            instruction == ex_blt<XLEN_t> ||
            instruction == ex_bge<XLEN_t> ||
            instruction == ex_bltu<XLEN_t> ||
            instruction == ex_bgeu<XLEN_t> ||
            instruction == ex_fence<XLEN_t> ||
            instruction == ex_fencei<XLEN_t> ||
            instruction == ex_ecall<XLEN_t> ||
            instruction == ex_ebreak<XLEN_t> ||
            instruction == ex_uret<XLEN_t> ||
            instruction == ex_sret<XLEN_t> ||
            instruction == ex_mret<XLEN_t> ||
            instruction == ex_sfencevma<XLEN_t> ||
            instruction == ex_cjal<XLEN_t> ||
            instruction == ex_cj<XLEN_t> ||
            instruction == ex_cbeqz<XLEN_t> ||
            instruction == ex_cbnez<XLEN_t> ||
            instruction == ex_cjalr<XLEN_t> ||
            instruction == ex_cjr<XLEN_t> ||
            instruction == ex_cebreak<XLEN_t>
        );
    }

    inline void ClearBlockCache() {
        bbCacheIsWritingBlock = false;
        bbCacheBlockWriteCursor = 0;
        bbCacheWriteIndex = 0;
        for (unsigned int i = 0; i < cachedBasicBlocks; i++) {
            BBCache[i].pc = 0;
            BBCache[i].length = 0;
            for (unsigned int j = 0; j < maxBasicBlockLength; j++) {
                BBCache[i].instructions[j].encoding = 0;
                BBCache[i].instructions[j].instruction = nullptr;
            }
        }
    }

    inline void Callback(HartCallbackArgument arg) {

        if (arg == HartCallbackArgument::TookTrap) {
            trapTakenDuringInstruction = true;
            return;
        }

        // todo only sometimes
        cachedTranslator.Clear();
        ClearBlockCache();

        // TODO Operating XLEN changing also changes this.
        // TODO cut out work by giving me old & new MISA values etc?
        if (arg == HartCallbackArgument::ChangedMISA)
            decoder.Configure(&this->state);
        return;
    }

};
