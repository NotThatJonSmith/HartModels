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
    unsigned int maxBasicBlockLength,
    unsigned int numNextBlocks
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

    struct BasicBlock {

        struct CachedInstruction {
            __uint32_t encoding = 0;
            DecodedInstruction<XLEN_t> instruction = nullptr;
        };

        XLEN_t pc = 0;
        unsigned int length = 0;
        CachedInstruction instructions[maxBasicBlockLength];
        BasicBlock* possibleNextBlocks[numNextBlocks] = { nullptr };
        
        BasicBlock* GetNextBlock(XLEN_t blockStartPC) {
            for (unsigned int i = 0; i < numNextBlocks; i++) {
                if (possibleNextBlocks[i] != nullptr &&
                    possibleNextBlocks[i]->pc == blockStartPC) {
                    return possibleNextBlocks[i];
                }
            }
            return nullptr;
        }

    };
    
    BasicBlock* RootBBCache[cachedBasicBlocks];
    BasicBlock* currentBasicBlock = nullptr;
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
        // memVATransactor.Fetch(this->state.pc, sizeof(this->state.encoded_instruction), (char*)&this->state.encoded_instruction);
        // if (transaction.trapCause != RISCV::TrapCause::NONE) {
            // TODO what if? This is definitely fatal - a trap on the first fetch ever
        // }
    }

    virtual inline unsigned int Tick() override {

        trapTakenDuringInstruction = false;

        if (!bbCacheIsWritingBlock) {

            if (currentBasicBlock != nullptr) {
                // Search in the next-block cache. This is the fastest path.
                BasicBlock* nextBlock = currentBasicBlock->GetNextBlock(this->state.pc);
                if (nextBlock != nullptr) {
                    currentBasicBlock = nextBlock;
                    ExecuteBasicBlock(currentBasicBlock);
                    return currentBasicBlock->length;
                }
            } // TODO simplify this mess

            // Search every block we know about. This is slow! TODO do we even need to do this?
            // What if we go to a block that isn't in the next-block cache but *is* somewhere else in the tree?
            // Does this happen often? Often enough to matter? Maybe duplication isn't a real problem. Or maybe a better
            // search and evict algorithm set would solve it really well.
            for (unsigned int i = 0; i < cachedBasicBlocks; i++) {
                if (RootBBCache[i] != nullptr && RootBBCache[i]->pc == this->state.pc) {
                    currentBasicBlock = RootBBCache[i];
                    ExecuteBasicBlock(currentBasicBlock);
                    return currentBasicBlock->length;
                }
            }

            // Now we need to open up a new block for writing and put ourselves in write-mode
            bbCacheWriteIndex = (bbCacheWriteIndex + 1) % cachedBasicBlocks;
            
            if (RootBBCache[bbCacheWriteIndex] != nullptr) {
                // Uh oh. Not only erase the block but all references to it in OTHER blocks...
                for (unsigned int i = 0; i < cachedBasicBlocks; i++) {
                    if (RootBBCache[i] == nullptr) {
                        continue;
                    }
                    for (unsigned int j = 0; j < numNextBlocks; j++) {
                        if (RootBBCache[i]->possibleNextBlocks[j] == RootBBCache[bbCacheWriteIndex]) {
                            RootBBCache[i]->possibleNextBlocks[j] = nullptr;
                        }
                    }
                }
                delete RootBBCache[bbCacheWriteIndex];
                RootBBCache[bbCacheWriteIndex] = nullptr;
            }

            RootBBCache[bbCacheWriteIndex] = new BasicBlock;

            if (currentBasicBlock != nullptr) {
                for (unsigned int i = 0; i < numNextBlocks; i++) {
                    if (currentBasicBlock->possibleNextBlocks[i] == nullptr) {
                        currentBasicBlock->possibleNextBlocks[i] = RootBBCache[bbCacheWriteIndex];
                        break;
                    }
                }
                // TODO what if we get here and can't have connected the block?
                // Oh well, I guess!
            }

            currentBasicBlock = RootBBCache[bbCacheWriteIndex];
            currentBasicBlock->length = 0;
            currentBasicBlock->pc = this->state.pc;
            bbCacheIsWritingBlock = true;
            bbCacheBlockWriteCursor = 0;
        }

        __uint32_t encoding;
        Transaction<XLEN_t> transaction;
        if constexpr (SkipBusForFetches) {
            transaction = memVATransactor.Fetch(this->state.pc, sizeof(encoding), (char*)&encoding);
        } else {
            transaction = busVATransactor.Fetch(this->state.pc, sizeof(encoding), (char*)&encoding);
        }

        if (transaction.trapCause == RISCV::TrapCause::NONE) {
            DecodedInstruction<XLEN_t> instr = decoder.Decode(encoding);
            currentBasicBlock->instructions[bbCacheBlockWriteCursor++] = { encoding, instr };
            currentBasicBlock->length++;
            bbCacheIsWritingBlock = bbCacheBlockWriteCursor < maxBasicBlockLength && !instructionCanBranch(instr);
            instr(encoding, &this->state, &busVATransactor);
            return 1;
        } 

        this->state.RaiseException(transaction.trapCause, this->state.pc);
        return 1;
    };

    virtual inline void Reset() override {
        this->state.Reset(this->resetVector);
        cachedTranslator.Clear();
        decoder.Configure(&this->state);
        ClearBlockCache();
    };

private:

    inline void ExecuteBasicBlock(BasicBlock* block) {
        for (unsigned int i = 0; i < block->length; i++) {
            block->instructions[i].instruction(block->instructions[i].encoding, &this->state, &busVATransactor);
            if (trapTakenDuringInstruction) { 
                break;
            }
        }
    }

    static inline bool instructionCanBranch(DecodedInstruction<XLEN_t> instruction) {
        // TODO, maybe some of these aren't 100% necessarily basic-block-breakers
        return (
            instruction == ex_jal<XLEN_t>     || instruction == ex_jalr<XLEN_t>      ||
            instruction == ex_beq<XLEN_t>     || instruction == ex_bne<XLEN_t>       ||
            instruction == ex_blt<XLEN_t>     || instruction == ex_bge<XLEN_t>       ||
            instruction == ex_bltu<XLEN_t>    || instruction == ex_bgeu<XLEN_t>      ||
            instruction == ex_fence<XLEN_t>   || instruction == ex_fencei<XLEN_t>    ||
            instruction == ex_ecall<XLEN_t>   || instruction == ex_ebreak<XLEN_t>    ||
            instruction == ex_uret<XLEN_t>    || instruction == ex_sret<XLEN_t>      ||
            instruction == ex_mret<XLEN_t>    || instruction == ex_sfencevma<XLEN_t> ||
            instruction == ex_cjal<XLEN_t>    || instruction == ex_cj<XLEN_t>        ||
            instruction == ex_cbeqz<XLEN_t>   || instruction == ex_cbnez<XLEN_t>     ||
            instruction == ex_cjalr<XLEN_t>   || instruction == ex_cjr<XLEN_t>       ||
            instruction == ex_cebreak<XLEN_t>
        );
    }

    inline void ClearBlockCache() {
        bbCacheIsWritingBlock = false;
        bbCacheBlockWriteCursor = 0;
        bbCacheWriteIndex = 0;
        for (unsigned int i = 0; i < cachedBasicBlocks; i++) {
            if (RootBBCache[i] == nullptr) {
                continue;
            }
            delete RootBBCache[i];
            RootBBCache[i] = nullptr;
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
