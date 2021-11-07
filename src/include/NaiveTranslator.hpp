#pragma once

#include <cstdint>

#include <TranslationAlgorithm.hpp>

class HartState;

class NaiveTranslator : public Translator {

private:

    HartState *state;
    CASK::IOTarget *bus;

public:

    NaiveTranslator(CASK::IOTarget *systemBus);

    virtual void Configure(HartState *hartState) override;
    virtual Translation<__uint32_t> TranslateRead32(__uint32_t address) override;
    virtual Translation<__uint64_t> TranslateRead64(__uint64_t address) override;
    virtual Translation<__uint128_t> TranslateRead128(__uint128_t address) override;
    virtual Translation<__uint32_t> TranslateWrite32(__uint32_t address) override;
    virtual Translation<__uint64_t> TranslateWrite64(__uint64_t address) override;
    virtual Translation<__uint128_t> TranslateWrite128(__uint128_t address) override;
    virtual Translation<__uint32_t> TranslateFetch32(__uint32_t address) override;
    virtual Translation<__uint64_t> TranslateFetch64(__uint64_t address) override;
    virtual Translation<__uint128_t> TranslateFetch128(__uint128_t address) override;

private:

    template<typename XLEN_t, CASK::AccessType accessType>
    inline Translation<XLEN_t> TranslateInternal(XLEN_t address) {
        return TranslationFunction<XLEN_t, accessType>(address, bus, state);
    }

};
