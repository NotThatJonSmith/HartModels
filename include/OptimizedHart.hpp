#pragma once

#include <type_traits>
#include <cstdint>

#include <Hart.hpp>
#include <Spigot.hpp>
#include <RiscV.hpp>

#include <PrecomputedDecoder.hpp>

#include <NaiveTranslator.hpp>
#include <BranchFreeTranslator.hpp>
#include <CacheWrappedTranslator.hpp>

template<
    bool UseFlattenedDecoder,
    bool UseBranchFreeTranslator,
    bool SkipXLENCheck,
    unsigned int TranslationCacheSizePoT,
    unsigned int CachedDecodedBasicBlocks,
    unsigned int Threads
>
class OptimizedHart final : public Hart {

public:

    OptimizedHart(CASK::IOTarget* bus) {

        Translator* uncachedTranslator = nullptr;
        if constexpr (UseBranchFreeTranslator) {
            uncachedTranslator = new BranchFreeTranslator(bus);
        } else {
            uncachedTranslator = new NaiveTranslator(bus);
        }

        if constexpr (TranslationCacheSizePoT != 0) {
            translator = new CacheWrappedTranslator<TranslationCacheSizePoT>(uncachedTranslator);
        } else {
            translator = uncachedTranslator;
        }

        translator->Configure(&state);

        state.mmu = new MMU(bus, translator);

        if constexpr (UseFlattenedDecoder) {
            decoder.Configure(&state);
        }

        // TODO this gets a prefetching optimization
        state.currentFetch = &fetch;

        state.notifyPrivilegeChanged = std::bind(&OptimizedHart::PrivilegeChanged, this);
        state.notifySoftwareChangedMISA = std::bind(&OptimizedHart::SoftwareChangedMISA, this);
        state.notifySoftwareChangedMSTATUS = std::bind(&OptimizedHart::SoftwareChangedMSTATUS, this);
        state.notifySoftwareChangedSATP = std::bind(&OptimizedHart::SoftwareChangedSATP, this);
        state.notifyInstructionFenceRequested = std::bind(&OptimizedHart::InstructionFenceRequested, this);
        state.notifyVMFenceRequested = std::bind(&OptimizedHart::VMFenceRequested, this);
    }

    virtual void BeforeFirstTick() override {
        Reset();
        switch (state.GetXLEN()) {
        case RISCV::XlenMode::XL32:
            Fetch<__uint32_t>();
            break;
        case RISCV::XlenMode::XL64:
            Fetch<__uint64_t>();
            break;
        case RISCV::XlenMode::XL128:
            Fetch<__uint128_t>();
            break;
        default:
            break; // TODO nonsense / fatal
        }
    }

    inline virtual void Tick() override {
        fetch.instruction.execute(fetch.operands, &state);
        if constexpr (SkipXLENCheck) {
            CurrentXLENFetch();
        } else {
            switch (state.GetXLEN()) {
            case RISCV::XlenMode::XL32:
                Fetch<__uint32_t>();
                break;
            case RISCV::XlenMode::XL64:
                Fetch<__uint64_t>();
                break;
            case RISCV::XlenMode::XL128:
                Fetch<__uint128_t>();
                break;
            default:
                break; // TODO nonsense / fatal
            }
        }
    }

    virtual void Reset() override {
        state.Reset(&spec);
        PrivilegeChanged();
        SoftwareChangedMISA();
        SoftwareChangedMSTATUS();
        SoftwareChangedSATP();
        InstructionFenceRequested();
        VMFenceRequested();
    }

private:

    HartState::Fetch fetch;
    PrecomputedDecoder decoder;
    Translator* translator;

    std::function<void(void)> CurrentXLENFetch;

    // TODO if >1 thread, fetch from ring buffer instead
    template<typename XLEN_t>
    inline void Fetch() {
        
        XLEN_t vpc;
        bool need_fetch = true;
        while (need_fetch) {

            need_fetch = false;

            // Get the next virtual program counter to fetch from
            vpc = state.nextFetchVirtualPC->Read<XLEN_t>();

            // Fill out the virtual PC
            fetch.virtualPC.Write<XLEN_t>(vpc);

            // Fetch the instruction from the MMU
            TransactionResult transactionResult = state.mmu->Fetch<XLEN_t>(vpc, sizeof(fetch.encoding), (char*)&fetch.encoding);
            
            // Raise an exception if the transaction failed - TODO size mismatch / device failure on fetch
            if (transactionResult.trapCause != RISCV::TrapCause::NONE) {
                state.RaiseException<XLEN_t>(transactionResult.trapCause, fetch.virtualPC.Read<XLEN_t>());
                need_fetch = true;
            }
        }

        // Set the default value of the next virtual PC we will fetch if the instruction doesn't change it
        state.nextFetchVirtualPC->Write<XLEN_t>(vpc + RISCV::instructionLength(fetch.encoding));

        // Mask off the higher bits of compressed instructions
        if (RISCV::isCompressed(fetch.encoding)) {
            fetch.encoding &= 0x0000ffff;
        }

        // Decode the instruction
        if constexpr (UseFlattenedDecoder) {
            fetch.instruction = decoder.Decode(fetch.encoding);
        } else {
            fetch.instruction = decode_full(fetch.encoding, state.extensions, state.mxlen, state.GetXLEN());
        }

        // Decode the operands
        fetch.operands = fetch.instruction.getOperands(fetch.encoding);

    }

    void SetFetchFunctionPointer() {
        switch (state.GetXLEN()) {
        case RISCV::XlenMode::XL32:
            CurrentXLENFetch = std::bind(&OptimizedHart::Fetch<__uint32_t>, this);
            break;
        case RISCV::XlenMode::XL64:
            CurrentXLENFetch = std::bind(&OptimizedHart::Fetch<__uint64_t>, this);
            break;
        case RISCV::XlenMode::XL128:
            CurrentXLENFetch = std::bind(&OptimizedHart::Fetch<__uint128_t>, this);
            break;
        default:
            break; // TODO nonsense / fatal
        }
    }

    void PrivilegeChanged() {
        if constexpr (UseBranchFreeTranslator || TranslationCacheSizePoT > 0) {
            translator->Configure(&state);
        }
        if constexpr (UseFlattenedDecoder) {
            decoder.Configure(&state);
        }
        if constexpr (SkipXLENCheck) {
            SetFetchFunctionPointer();
        }
    }

    void SoftwareChangedMISA() {
        if constexpr (UseBranchFreeTranslator || TranslationCacheSizePoT > 0) {
            translator->Configure(&state);
        }
        if constexpr (UseFlattenedDecoder) {
            decoder.Configure(&state);
        }
        if constexpr (SkipXLENCheck) {
            SetFetchFunctionPointer();
        }
    }

    void SoftwareChangedMSTATUS() {
        if constexpr (UseBranchFreeTranslator || TranslationCacheSizePoT > 0) {
            translator->Configure(&state);
        }
        if constexpr (UseFlattenedDecoder) {
            decoder.Configure(&state);
        }
        if constexpr (SkipXLENCheck) {
            SetFetchFunctionPointer();
        }
    }

    void SoftwareChangedSATP() {
        // TODO - I think this is actually useless because of VMFenceRequested!
        // if constexpr (UseBranchFreeTranslator || TranslationCacheSizePoT > 0) {
        //     translator->Configure(&state);
        // }
    }

    void InstructionFenceRequested() {
        // TODO - things like dumping the prefetch buffer and clearing bb caches
    }

    void VMFenceRequested() {
        if constexpr (UseBranchFreeTranslator || TranslationCacheSizePoT > 0) {
            translator->Configure(&state);
        }
    }

};

/*

void OptimizedHart::BeforeFirstTick() {
    Reset();
    state.currentFetch = fetchService.Current();
}

void OptimizedHart::Tick() {
    state.currentFetch->instruction.execute(state.currentFetch->operands, &state);
    state.currentFetch = fetchService.Next();
}

void OptimizedHart::Reset() {
    state.Reset(&spec);
    Reconfigure();
}

void OptimizedHart::FlushPrefetches() {
    unsigned int offset = RISCV::instructionLength(state.currentFetch->encoding);
    fetchService.Pause();
    state.nextFetchVirtualPC->Set<__uint32_t>(state.currentFetch->virtualPC.Read<__uint32_t>() + offset);
    state.nextFetchVirtualPC->Set<__uint64_t>(state.currentFetch->virtualPC.Read<__uint64_t>() + offset);
    state.nextFetchVirtualPC->Set<__uint128_t>(state.currentFetch->virtualPC.Read<__uint128_t>() + offset);
    fetchService.Run();
}

void OptimizedHart::Reconfigure() {

    fetchService.Pause();

    for (unsigned int i = 0; i < prefetchCacheSize; i++) {
        prefetchCache[i].valid = false;
    }

    RISCV::PrivilegeMode translationPrivilege = state.modifyMemoryPrivilege ? state.machinePreviousPrivilege : state.privilegeMode;
    RISCV::XlenMode xlen = state.GetXLEN();

    // TODO move the prefetch service out of the Hart class and give it a Configure function, too
    if (xlen == RISCV::XlenMode::XL32) {
        fetchService.SetProducer(std::bind(&OptimizedHart::ServePrefetches<__uint32_t>, this, std::placeholders::_1));
    } else if (xlen == RISCV::XlenMode::XL64) {
        fetchService.SetProducer(std::bind(&OptimizedHart::ServePrefetches<__uint64_t>, this, std::placeholders::_1));
    } else if (xlen == RISCV::XlenMode::XL128) {
        fetchService.SetProducer(std::bind(&OptimizedHart::ServePrefetches<__uint128_t>, this, std::placeholders::_1));
    } else {
        // TODO fatal("Starting to fetch code in XLEN mode 'None'!");
    }

    fetchService.Run();
}

*/