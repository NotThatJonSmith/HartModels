#pragma once

#include <Translator.hpp>

template<typename XLEN_t, unsigned int cacheBits>
class CacheWrappedTranslator : public Translator<XLEN_t> {

private:

    struct CacheEntry {
        Translation<XLEN_t> translation;
        bool valid;
    };

    Translator<XLEN_t>* translator;

    CacheEntry cacheR[1 << cacheBits];
    CacheEntry cacheW[1 << cacheBits];
    CacheEntry cacheX[1 << cacheBits];

public:

    CacheWrappedTranslator(Translator<XLEN_t>* targetTranslator) : translator(targetTranslator) {
        Clear();
    }

    virtual inline Translation<XLEN_t> TranslateRead(XLEN_t address) override {
        return TranslateInternal<IOVerb::Read>(address);
    }

    virtual inline Translation<XLEN_t> TranslateWrite(XLEN_t address) override {
        return TranslateInternal<IOVerb::Write>(address);
    }

    virtual inline Translation<XLEN_t> TranslateFetch(XLEN_t address) override {
        return TranslateInternal<IOVerb::Fetch>(address);
    }

    void Clear() {
        for (unsigned int i = 0; i < (1 << cacheBits); i++) {
            cacheR[i].valid = false;
            cacheW[i].valid = false;
            cacheX[i].valid = false;
        }
    }

private:

    template<IOVerb verb>
    inline Translation<XLEN_t> TranslateInternal(XLEN_t address) {

        if constexpr (cacheBits == 0) {
            return translator->template Translate<verb>(address);
        }

        CacheEntry* cache = cacheX;
        if constexpr (verb == IOVerb::Read) {
            cache = cacheR;
        } else if constexpr (verb == IOVerb::Write) {
            cache = cacheW;
        }


        // XLEN_t cacheIndex = (address >> 12) & ((1 << cacheBits) - 1);
        // XLEN_t cacheTag = address >> (12 + cacheBits);
        // XLEN_t residentAddress = cache[cacheIndex].translation.untranslated;
        // XLEN_t residentTag = residentAddress >> (12 + cacheBits);

        // if (!cache[cacheIndex].valid || cacheTag != residentTag) {
        //     cache[cacheIndex].translation = translator->template Translate<verb>(address);
        //     cache[cacheIndex].valid = true;
        // }

        for (unsigned int cacheReadIndex = 0; cacheReadIndex < (1 << cacheBits); cacheReadIndex++)
            if (cache[cacheReadIndex].valid &&
                cache[cacheReadIndex].translation.untranslated <= address &&
                cache[cacheReadIndex].translation.validThrough > address)
                return cache[cacheReadIndex].translation;
        static unsigned int cacheWriteIndex = 0;
        unsigned int cacheReadIndex = cacheWriteIndex;
        cache[cacheWriteIndex].translation = translator->template Translate<verb>(address);
        cache[cacheWriteIndex].valid = true;
        cacheWriteIndex = (cacheWriteIndex + 1) % (1 << cacheBits);
        return cache[cacheReadIndex].translation;
    }
};
