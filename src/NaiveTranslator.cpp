#include <NaiveTranslator.hpp>

NaiveTranslator::NaiveTranslator(CASK::IOTarget *systemBus) : bus(systemBus) {
}

void NaiveTranslator::Configure(HartState* hartState) {
    state = hartState;
}

Translation<__uint32_t> NaiveTranslator::TranslateRead32(__uint32_t address) {
    return TranslateInternal<__uint32_t, CASK::AccessType::R>(address);
}

Translation<__uint64_t> NaiveTranslator::TranslateRead64(__uint64_t address) {
    return TranslateInternal<__uint64_t, CASK::AccessType::R>(address);
}

Translation<__uint128_t> NaiveTranslator::TranslateRead128(__uint128_t address) {
    return TranslateInternal<__uint128_t, CASK::AccessType::R>(address);
}

Translation<__uint32_t> NaiveTranslator::TranslateWrite32(__uint32_t address) {
    return TranslateInternal<__uint32_t, CASK::AccessType::W>(address);
}

Translation<__uint64_t> NaiveTranslator::TranslateWrite64(__uint64_t address) {
    return TranslateInternal<__uint64_t, CASK::AccessType::W>(address);
}

Translation<__uint128_t> NaiveTranslator::TranslateWrite128(__uint128_t address) {
    return TranslateInternal<__uint128_t, CASK::AccessType::W>(address);
}

Translation<__uint32_t> NaiveTranslator::TranslateFetch32(__uint32_t address) {
    return TranslateInternal<__uint32_t, CASK::AccessType::X>(address);
}

Translation<__uint64_t> NaiveTranslator::TranslateFetch64(__uint64_t address) {
    return TranslateInternal<__uint64_t, CASK::AccessType::X>(address);
}

Translation<__uint128_t> NaiveTranslator::TranslateFetch128(__uint128_t address) {
    return TranslateInternal<__uint128_t, CASK::AccessType::X>(address);
}
