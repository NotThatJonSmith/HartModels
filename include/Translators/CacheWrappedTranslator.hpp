#pragma once

#include <Translator.hpp>

template<typename XLEN_t, unsigned int cacheBits>
class CacheWrappedTranslator : public Translator<XLEN_t> {

private:

    struct CacheEntry {
        Translation<XLEN_t> translation;
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
            cacheR[i].translation.untranslated = ~(XLEN_t)0;
            cacheW[i].translation.untranslated = ~(XLEN_t)0;
            cacheX[i].translation.untranslated = ~(XLEN_t)0;
        }
    }

private:

    template<IOVerb verb>
    inline Translation<XLEN_t> TranslateInternal(XLEN_t address) {

        if constexpr (cacheBits == 0) {
            return translator->template Translate<verb>(address);
        }

        if (address >> 12 == ~(XLEN_t)0 >> 12) {
            return translator->template Translate<verb>(address);
        }

        CacheEntry* cache = cacheX;
        if constexpr (verb == IOVerb::Read) {
            cache = cacheR;
        } else if constexpr (verb == IOVerb::Write) {
            cache = cacheW;
        }

        constexpr unsigned int cacheSize = (1 << cacheBits);
        static unsigned int cacheWriteIndex = 0;
        unsigned int cacheReadIndex = cacheWriteIndex;
        do {
            if (cache[cacheReadIndex].translation.untranslated >> 12 == address >> 12)
                return cache[cacheReadIndex].translation;
            cacheReadIndex = ((int)cacheReadIndex-1) % cacheSize;
        } while (cacheReadIndex != cacheWriteIndex);

        cacheWriteIndex = (cacheWriteIndex + 1) % (1 << cacheBits);
        cache[cacheWriteIndex].translation = translator->template Translate<verb>(address);
        return cache[cacheWriteIndex].translation;
    }
};
