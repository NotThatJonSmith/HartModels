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
    
    static constexpr unsigned int cachedBasicBlocks = 1 << bbCacheSizePoT; // Tied to the hash alg
    BasicBlock* RootBBCache[cachedBasicBlocks];
    BasicBlock* currentBasicBlock = nullptr;
    bool bbCacheIsWritingBlock = false;
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
        Reset();
    };

    virtual inline unsigned int Tick() override {

        trapTakenDuringInstruction = false;

        if (!bbCacheIsWritingBlock) [[likely]] {

            if (currentBasicBlock != nullptr) {
                // Search in the next-block cache. This is the fastest path.
                BasicBlock* nextBlock = currentBasicBlock->GetNextBlock(this->state.pc);
                if (nextBlock != nullptr) {
                    currentBasicBlock = nextBlock;
                    ExecuteBasicBlock(currentBasicBlock);
                    return currentBasicBlock->length;
                }
            }

            unsigned int rootCacheIndex = (this->state.pc >> 1) & ((1 << bbCacheSizePoT) - 1);
            if (RootBBCache[rootCacheIndex] != nullptr && RootBBCache[rootCacheIndex]->pc == this->state.pc) {
                if (currentBasicBlock != nullptr) {
                    for (unsigned int i = 0; i < numNextBlocks; i++) {
                        if (currentBasicBlock->possibleNextBlocks[i] == nullptr) {
                            currentBasicBlock->possibleNextBlocks[i] = RootBBCache[rootCacheIndex];
                            break;
                        }
                    }
                    // TODO what if we get here and can't have connected the block?
                    // Oh well, I guess!
                }
                currentBasicBlock = RootBBCache[rootCacheIndex];
                ExecuteBasicBlock(currentBasicBlock);
                return currentBasicBlock->length;
            }
            
            if (RootBBCache[rootCacheIndex] != nullptr) {
                // Uh oh. Not only erase the block but all references to it in OTHER blocks...
                // If this starts happening a lot the fix is to add sub-caches under the index to resolve collisions
                // Another thing to do could be to speed up eviction by maintaining a bit matrix to find referrers faster
                for (unsigned int i = 0; i < cachedBasicBlocks; i++) {
                    if (RootBBCache[i] == nullptr) {
                        continue;
                    }
                    for (unsigned int j = 0; j < numNextBlocks; j++) {
                        if (RootBBCache[i]->possibleNextBlocks[j] == RootBBCache[rootCacheIndex]) {
                            RootBBCache[i]->possibleNextBlocks[j] = nullptr;
                        }
                    }
                }
                delete RootBBCache[rootCacheIndex];
                RootBBCache[rootCacheIndex] = nullptr;
            }

            RootBBCache[rootCacheIndex] = new BasicBlock;

            if (currentBasicBlock != nullptr) {
                for (unsigned int i = 0; i < numNextBlocks; i++) {
                    if (currentBasicBlock->possibleNextBlocks[i] == nullptr) {
                        currentBasicBlock->possibleNextBlocks[i] = RootBBCache[rootCacheIndex];
                        break;
                    }
                }
                // TODO what if we get here and can't have connected the block?
                // Oh well, I guess!
            }

            currentBasicBlock = RootBBCache[rootCacheIndex];
            currentBasicBlock->length = 0;
            currentBasicBlock->pc = this->state.pc;
            bbCacheIsWritingBlock = true;
        }

        __uint32_t encoding;
        Transaction<XLEN_t> transaction;
        if constexpr (SkipBusForFetches) {
            transaction = memVATransactor.Fetch(this->state.pc, sizeof(encoding), (char*)&encoding);
        } else {
            transaction = busVATransactor.Fetch(this->state.pc, sizeof(encoding), (char*)&encoding);
        }

        if (transaction.trapCause == RISCV::TrapCause::NONE) [[likely]] {
            DecodedInstruction<XLEN_t> decoded = decoder.Decode(encoding);
            currentBasicBlock->instructions[currentBasicBlock->length++] = { encoding, decoded };
            bbCacheIsWritingBlock = currentBasicBlock->length < maxBasicBlockLength && !instructionCanBranch(decoded);
            decoded(encoding, &this->state, &busVATransactor);
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

    virtual inline Transactor<XLEN_t>* getVATransactor() override {
        return &busVATransactor;
    }

private:

    inline void ExecuteBasicBlock(BasicBlock* block) {
        // TODO maybe pick up speed by modeling trapTakenDuringInstruction with C++ try/catch instead of a flag to check
        for (unsigned int i = 0; i < block->length && !trapTakenDuringInstruction; i++) {
            block->instructions[i].instruction(block->instructions[i].encoding, &this->state, &busVATransactor);
        }
    }

    static inline bool instructionCanBranch(DecodedInstruction<XLEN_t> instruction) {
        // TODO, maybe some of these aren't 100% necessarily basic-block-breakers
        // TODO perf: this check can be abolished if there is a hart callback for "I might branch" or some other slickness
        return (
            instruction == ex_jal<XLEN_t>     || instruction == ex_jalr<XLEN_t>      ||
            instruction == ex_beq<XLEN_t>     || instruction == ex_bne<XLEN_t>       ||
            instruction == ex_blt<XLEN_t>     || instruction == ex_bge<XLEN_t>       ||
            instruction == ex_bltu<XLEN_t>    || instruction == ex_bgeu<XLEN_t>      ||
            instruction == ex_fence<XLEN_t>   || instruction == ex_fencei<XLEN_t>    ||
            instruction == ex_uret<XLEN_t>    || instruction == ex_sret<XLEN_t>      ||
            instruction == ex_mret<XLEN_t>    || instruction == ex_sfencevma<XLEN_t> ||
            instruction == ex_cjal<XLEN_t>    || instruction == ex_cj<XLEN_t>        ||
            instruction == ex_cbeqz<XLEN_t>   || instruction == ex_cbnez<XLEN_t>     ||
            instruction == ex_cjalr<XLEN_t>   || instruction == ex_cjr<XLEN_t>
        );
    }

    inline void ClearBlockCache() {
        bbCacheIsWritingBlock = false;
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

        // TODO be strict about these

        if (arg == HartCallbackArgument::RequestedVMfence)
            cachedTranslator.Clear();

        if (arg == HartCallbackArgument::RequestedIfence || arg == HartCallbackArgument::RequestedVMfence)
            ClearBlockCache();

        // TODO Operating XLEN changing also changes this.
        // TODO cut out work by giving me old & new MISA values etc?
        if (arg == HartCallbackArgument::ChangedMISA)
            decoder.Configure(&this->state);
            
        return;
    }

};
