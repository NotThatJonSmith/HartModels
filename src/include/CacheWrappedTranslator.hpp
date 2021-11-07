#pragma once

#include <cstdint>

#include <Translator.hpp>

template<unsigned int cacheBits>
class CacheWrappedTranslator : public Translator {

private:

    template<typename XLEN_t>
    struct CacheEntry {
        Translation<XLEN_t> translation;
        bool valid;
    };

    Translator* translator;

    CacheEntry<__uint32_t> cacheR32[1 << cacheBits];
    CacheEntry<__uint32_t> cacheW32[1 << cacheBits];
    CacheEntry<__uint32_t> cacheX32[1 << cacheBits];
    CacheEntry<__uint64_t> cacheR64[1 << cacheBits];
    CacheEntry<__uint64_t> cacheW64[1 << cacheBits];
    CacheEntry<__uint64_t> cacheX64[1 << cacheBits];
    CacheEntry<__uint128_t> cacheR128[1 << cacheBits];
    CacheEntry<__uint128_t> cacheW128[1 << cacheBits];
    CacheEntry<__uint128_t> cacheX128[1 << cacheBits];

public:

    CacheWrappedTranslator(Translator* targetTranslator) : translator(targetTranslator) {
        ClearCaches();
    }

    virtual void Configure(HartState* hartState) override {
        ClearCaches();
        translator->Configure(hartState);
    }

    virtual Translation<__uint32_t> TranslateRead32(__uint32_t address) override {
        return TranslateInternal<__uint32_t, CASK::AccessType::R>(address);
    }

    virtual Translation<__uint64_t> TranslateRead64(__uint64_t address) override {
        return TranslateInternal<__uint64_t, CASK::AccessType::R>(address);
    }

    virtual Translation<__uint128_t> TranslateRead128(__uint128_t address) override {
        return TranslateInternal<__uint128_t, CASK::AccessType::R>(address);
    }

    virtual Translation<__uint32_t> TranslateWrite32(__uint32_t address) override {
        return TranslateInternal<__uint32_t, CASK::AccessType::W>(address);
    }

    virtual Translation<__uint64_t> TranslateWrite64(__uint64_t address) override {
        return TranslateInternal<__uint64_t, CASK::AccessType::W>(address);
    }

    virtual Translation<__uint128_t> TranslateWrite128(__uint128_t address) override {
        return TranslateInternal<__uint128_t, CASK::AccessType::W>(address);
    }

    virtual Translation<__uint32_t> TranslateFetch32(__uint32_t address) override {
        return TranslateInternal<__uint32_t, CASK::AccessType::X>(address);
    }

    virtual Translation<__uint64_t> TranslateFetch64(__uint64_t address) override {
        return TranslateInternal<__uint64_t, CASK::AccessType::X>(address);
    }

    virtual Translation<__uint128_t> TranslateFetch128(__uint128_t address) override {
        return TranslateInternal<__uint128_t, CASK::AccessType::X>(address);
    }

private:

    void ClearCaches() {
        for (unsigned int i = 0; i < (1 << cacheBits); i++) {
            cacheR32[i].valid = false;
            cacheW32[i].valid = false;
            cacheX32[i].valid = false;
            cacheR64[i].valid = false;
            cacheW64[i].valid = false;
            cacheX64[i].valid = false;
            cacheR128[i].valid = false;
            cacheW128[i].valid = false;
            cacheX128[i].valid = false;
        }
    }

    template<typename XLEN_t, CASK::AccessType accessType>
    CacheEntry<XLEN_t>* GetCache() {
        if constexpr (std::is_same<XLEN_t, __uint32_t>()) {
            if constexpr (accessType == CASK::AccessType::R) {
                return cacheR32;
            } else if constexpr (accessType == CASK::AccessType::W) {
                return cacheW32;
            } else { // CASK::AccessType::X
                return cacheX32;
            }
        } else if constexpr (std::is_same<XLEN_t, __uint64_t>()) {
            if constexpr (accessType == CASK::AccessType::R) {
                return cacheR64;
            } else if constexpr (accessType == CASK::AccessType::W) {
                return cacheW64;
            } else { // CASK::AccessType::X
                return cacheX64;
            }
        } else {
            if constexpr (accessType == CASK::AccessType::R) {
                return cacheR128;
            } else if constexpr (accessType == CASK::AccessType::W) {
                return cacheW128;
            } else { // CASK::AccessType::X
                return cacheX128;
            }
        }
    }

    template<typename XLEN_t, CASK::AccessType accessType>
    inline Translation<XLEN_t> TranslateInternal(XLEN_t address) {

        CacheEntry<XLEN_t>* cache = GetCache<XLEN_t, accessType>();
        XLEN_t cacheIndex = (address >> 12) & ((1 << cacheBits) - 1);
        XLEN_t cacheTag = address >> (12 + cacheBits);
        XLEN_t residentAddress = cache[cacheIndex].translation.untranslated;
        XLEN_t residentTag = residentAddress >> (12 + cacheBits);

        if (!cache[cacheIndex].valid || cacheTag != residentTag) {
            cache[cacheIndex].translation = translator->Translate<XLEN_t, accessType>(address);
            cache[cacheIndex].valid = true;
        }
        
        return cache[cacheIndex].translation;
    }
};
