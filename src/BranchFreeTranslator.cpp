#include <BranchFreeTranslator.hpp>

BranchFreeTranslator::BranchFreeTranslator(CASK::IOTarget *systemBus) :
    bus(systemBus) {
}

void BranchFreeTranslator::Configure(HartState *state) {
    ppnRegister = &state->ppn;
    unsigned int translatorHash = hashTranslatorParameters(
        CASK::AccessType::R,
        state->pagingMode,
        state->privilegeMode,
        state->makeExecutableReadable,
        state->supervisorUserMemoryAccess);
    translateForRead32 = TranslatorsXL32[translatorHash];
    translateForRead64 = TranslatorsXL64[translatorHash];
    translateForRead128 = TranslatorsXL128[translatorHash];
    translatorHash = hashTranslatorParameters(
        CASK::AccessType::W,
        state->pagingMode,
        state->privilegeMode,
        state->makeExecutableReadable,
        state->supervisorUserMemoryAccess);
    translateForWrite32 = TranslatorsXL32[translatorHash];
    translateForWrite64 = TranslatorsXL64[translatorHash];
    translateForWrite128 = TranslatorsXL128[translatorHash];
    translatorHash = hashTranslatorParameters(
        CASK::AccessType::X,
        state->pagingMode,
        state->privilegeMode,
        state->makeExecutableReadable,
        state->supervisorUserMemoryAccess);
    translateForFetch32 = TranslatorsXL32[translatorHash];
    translateForFetch64 = TranslatorsXL64[translatorHash];
    translateForFetch128 = TranslatorsXL128[translatorHash];
}

Translation<__uint32_t> BranchFreeTranslator::TranslateRead32(__uint32_t address) {
    return translateForRead32(address, bus, ppnRegister->Read<__uint32_t>());
}

Translation<__uint64_t> BranchFreeTranslator::TranslateRead64(__uint64_t address) {
    return translateForRead64(address, bus, ppnRegister->Read<__uint64_t>());
}

Translation<__uint128_t> BranchFreeTranslator::TranslateRead128(__uint128_t address) {
    return translateForRead128(address, bus, ppnRegister->Read<__uint128_t>());
}

Translation<__uint32_t> BranchFreeTranslator::TranslateWrite32(__uint32_t address) {
    return translateForWrite32(address, bus, ppnRegister->Read<__uint32_t>());
}

Translation<__uint64_t> BranchFreeTranslator::TranslateWrite64(__uint64_t address) {
    return translateForWrite64(address, bus, ppnRegister->Read<__uint64_t>());
}

Translation<__uint128_t> BranchFreeTranslator::TranslateWrite128(__uint128_t address) {
    return translateForWrite128(address, bus, ppnRegister->Read<__uint128_t>());
}

Translation<__uint32_t> BranchFreeTranslator::TranslateFetch32(__uint32_t address) {
    return translateForFetch32(address, bus, ppnRegister->Read<__uint32_t>());
}

Translation<__uint64_t> BranchFreeTranslator::TranslateFetch64(__uint64_t address) {
    return translateForFetch64(address, bus, ppnRegister->Read<__uint64_t>());
}

Translation<__uint128_t> BranchFreeTranslator::TranslateFetch128(__uint128_t address) {
    return translateForFetch128(address, bus, ppnRegister->Read<__uint128_t>());
}