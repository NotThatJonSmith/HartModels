#pragma once

#include <type_traits>
#include <cstdint>

#include <Hart.hpp>
#include <Spigot.hpp>
#include <RiscV.hpp>

#include <PrecomputedDecoder.hpp>

#include <DirectTranslator.hpp>
#include <BranchFreeTranslator.hpp>
#include <CacheWrappedTranslator.hpp>

template<
    bool UseFlattenedDecoder,
    bool UseBranchFreeTranslator,
    bool SkipXLENCheck,
    bool SkipBusForPageTables,
    unsigned int TranslationCacheSizePoT,
    unsigned int CachedDecodedBasicBlocks,
    unsigned int FetchThreadDepth
>
class OptimizedHart final : public Hart {

public:

    OptimizedHart(CASK::IOTarget* bus, CASK::IOTarget* directMem=nullptr) {

        CASK::IOTarget* translatorBus = bus;
        if constexpr (SkipBusForPageTables) {
            translatorBus = directMem;
        }

        Translator* uncachedTranslator = nullptr;
        if constexpr (UseBranchFreeTranslator) {
            uncachedTranslator = new BranchFreeTranslator(translatorBus);
        } else {
            uncachedTranslator = new NaiveTranslator(translatorBus);
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

        if constexpr (FetchThreadDepth > 1) {
            // TODO configure prefetch service
            // TODO set currentFetch
        } else {
            state.currentFetch = &fetch;
        }

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
            ServeFetch<__uint32_t>(state.currentFetch);
            break;
        case RISCV::XlenMode::XL64:
            ServeFetch<__uint64_t>(state.currentFetch);
            break;
        case RISCV::XlenMode::XL128:
            ServeFetch<__uint128_t>(state.currentFetch);
            break;
        default:
            break; // TODO nonsense / fatal
        }
    }

    inline virtual void Tick() override {
        
        state.currentFetch->instruction.execute(state.currentFetch->operands, &state);

        if constexpr (FetchThreadDepth > 1) {
            state.currentFetch = fetchService.Next();
        } else if constexpr (SkipXLENCheck) {
            CurrentXLENFetch(state.currentFetch);
        } else {
            switch (state.GetXLEN()) {
            case RISCV::XlenMode::XL32:
                ServeFetch<__uint32_t>(state.currentFetch);
                break;
            case RISCV::XlenMode::XL64:
                ServeFetch<__uint64_t>(state.currentFetch);
                break;
            case RISCV::XlenMode::XL128:
                ServeFetch<__uint128_t>(state.currentFetch);
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
    Spigot<HartState::Fetch, FetchThreadDepth> fetchService;

    std::function<void(HartState::Fetch*)> CurrentXLENFetch;

    template<typename XLEN_t>
    inline void ServeFetch(HartState::Fetch* fetch_into) {
        XLEN_t vpc;
        bool need_fetch = true;
        while (need_fetch) {
            need_fetch = false;
            vpc = state.nextFetchVirtualPC.Read<XLEN_t>();
            fetch_into->virtualPC.Write<XLEN_t>(vpc);
            TransactionResult transactionResult = 
                state.mmu->Fetch<XLEN_t>(vpc, sizeof(fetch_into->encoding), (char*)&fetch_into->encoding);
            if (transactionResult.trapCause != RISCV::TrapCause::NONE) {
                state.RaiseException<XLEN_t>(transactionResult.trapCause, fetch_into->virtualPC.Read<XLEN_t>());
                need_fetch = true;
            }
        }
        state.nextFetchVirtualPC.Write<XLEN_t>(vpc + RISCV::instructionLength(fetch_into->encoding));
        if constexpr (UseFlattenedDecoder) {
            fetch_into->instruction = decoder.Decode(fetch_into->encoding);
        } else {
            fetch_into->instruction = decode_full(fetch_into->encoding, state.extensions, state.mxlen, state.GetXLEN());
        }
        fetch_into->operands = fetch_into->instruction.getOperands(fetch_into->encoding);
    }

    // TODO move the prefetch service out of the Hart class and give it a Configure function, too
    void ConfigureFetchService() {

        fetchService.Pause();

        // TODO one day, another optimization flag for the cached fetches
        // for (unsigned int i = 0; i < prefetchCacheSize; i++) {
        //     prefetchCache[i].valid = false;
        // }

        RISCV::XlenMode xlen = state.GetXLEN();
        if (xlen == RISCV::XlenMode::XL32) {
            fetchService.SetProducer(std::bind(&OptimizedHart::ServeFetch<__uint32_t>, this, std::placeholders::_1));
        } else if (xlen == RISCV::XlenMode::XL64) {
            fetchService.SetProducer(std::bind(&OptimizedHart::ServeFetch<__uint64_t>, this, std::placeholders::_1));
        } else if (xlen == RISCV::XlenMode::XL128) {
            fetchService.SetProducer(std::bind(&OptimizedHart::ServeFetch<__uint128_t>, this, std::placeholders::_1));
        } else {
            // TODO fatal("Starting to fetch code in XLEN mode 'None'!");
        }

        fetchService.Run();
    }

    // TODO move the prefetch service out of the Hart class and give it a Flush function, too
    void FlushPrefetches() {
        unsigned int offset = RISCV::instructionLength(state.currentFetch->encoding);
        fetchService.Pause();
        state.nextFetchVirtualPC.Set<__uint32_t>(state.currentFetch->virtualPC.Read<__uint32_t>() + offset);
        state.nextFetchVirtualPC.Set<__uint64_t>(state.currentFetch->virtualPC.Read<__uint64_t>() + offset);
        state.nextFetchVirtualPC.Set<__uint128_t>(state.currentFetch->virtualPC.Read<__uint128_t>() + offset);
        fetchService.Run();
    }

    void SetFetchFunctionPointer() {
        switch (state.GetXLEN()) {
        case RISCV::XlenMode::XL32:
            CurrentXLENFetch = std::bind(&OptimizedHart::ServeFetch<__uint32_t>, this, std::placeholders::_1);
            break;
        case RISCV::XlenMode::XL64:
            CurrentXLENFetch = std::bind(&OptimizedHart::ServeFetch<__uint64_t>, this, std::placeholders::_1);
            break;
        case RISCV::XlenMode::XL128:
            CurrentXLENFetch = std::bind(&OptimizedHart::ServeFetch<__uint128_t>, this, std::placeholders::_1);
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



*/