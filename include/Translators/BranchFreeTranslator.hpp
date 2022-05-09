#pragma once

#include <cstdint>
#include <type_traits>
#include <queue>

#include <RiscV.hpp>
#include <TranslationAlgorithm.hpp>
#include <Register.hpp>
#include <MMU.hpp>
#include <Translator.hpp>

// The heavily-templated version where the algorithm should get inlined and optimized
template<typename XLEN_t, CASK::AccessType accessType, RISCV::PagingMode currentPagingMode,
         RISCV::PrivilegeMode translationPrivilege, bool mxrBit, bool sumBit>
inline Translation<XLEN_t> TranslationTemplate(XLEN_t virt_addr, CASK::IOTarget* bus, XLEN_t root_ppn) {
    return TranslationAlgorithm<XLEN_t, accessType>(virt_addr, bus, root_ppn, currentPagingMode, translationPrivilege, mxrBit, sumBit);
}

// There is a lot of C++-fu here, but it's not that complex really. The idea is
// to remove as much of the branchy code as possible from the RISC-V paged
// virtual memory address translation algorithm.
//
// To do this, the translation algorithm described by the spec is transcribed
// essentially 1:1 into C++, but with one key difference: The parameters that
// change infrequently are all made into non-type template parameters, baked in
// at compile time.
//
// How, then, do we deal with the fact that these parameters will change at
// runtime sometimes? The answer is that, at compile time, we instantiate the
// template for every possible combination of the template parameters.
//
// To accomplish this we need both compile-time and runtime code to have access
// to a collision-free hash function combines the template parameters into an
// array index, so runtime code can cache a pointer to the right version when
// the parameters of paged virtual memory do change.
//
// At compile time, we know that there are 216 variations of the chosen template
// parameters. So, we create a constant array of 216 function pointers to the
// instantiated function templates. We create a separate table for each of the
// three possible XLEN modes.
//
// At runtime, we detect any changes to MSTATUS or SATP, and respond by encoding
// the parameter values into the new hashes for the nine translators we need
// access to (three XLEN modes by the three access types, read, write, fetch).
// Then whenever we need to make a translation for a memory access, we call the
// cached function pointer, and execute a nearly branch-free version of the
// algorithm.
//
// Note that although the PPN doesn't change often, making the PPN values part
// of the hash would explode the number of required templates, so it's still a
// normal runtime parameter.


// Take all the infrequently-changing arguments to the RISC-V virtual memory
// translation algorithm, and create a collision-free hash into unsigned ints.
constexpr unsigned int hashTranslatorParameters(
        CASK::AccessType accessType,
        RISCV::PagingMode pagingMode,
        RISCV::PrivilegeMode privilege,
        bool mxrBit,
        bool sumBit
    ) {
    
    unsigned int accessIndex =
        accessType == CASK::AccessType::R ? 0 :
                      CASK::AccessType::W ? 1 :
                                            2;
                      
    unsigned int pmIndex =
        pagingMode == RISCV::PagingMode::Bare ? 0 :
        pagingMode == RISCV::PagingMode::Sv32 ? 1 :
        pagingMode == RISCV::PagingMode::Sv39 ? 2 :
        pagingMode == RISCV::PagingMode::Sv48 ? 3 :
        pagingMode == RISCV::PagingMode::Sv57 ? 4 :
                                                5;
    
    unsigned int privIndex =
        privilege == RISCV::PrivilegeMode::Machine    ? 0 :
        privilege == RISCV::PrivilegeMode::Supervisor ? 1 :
                                                        2;

    unsigned int mxrIndex =
        mxrBit ? 1 :
                 0;

    unsigned int sumIndex =
        sumBit ? 1 :
                 0;

    return accessIndex * 6 * 3 * 2 * 2 +
               pmIndex     * 3 * 2 * 2 +
             privIndex         * 2 * 2 +
              mxrIndex             * 2 +
             sumIndex;
}

// Define the InstantiatedTranslationTemplate function pointer type, templated over XLEN mode types.
template <typename XLEN_t>
using InstantiatedTranslationTemplate = Translation<XLEN_t> (*)(XLEN_t, CASK::IOTarget*, XLEN_t);

// Reverse the translator template parameter hash and return a pointer to the
// corresponding template specialization.
template <typename XLEN_t, unsigned int translatorHash>
constexpr InstantiatedTranslationTemplate<XLEN_t> generateTranslatorFromHash() {

    constexpr unsigned int accessIndex =  translatorHash / (6 * 3 * 2 * 2);
    constexpr unsigned int translatorHash2 = translatorHash - (accessIndex * 6 * 3 * 2 * 2);
    constexpr CASK::AccessType accessType =
        accessIndex == 0 ? CASK::AccessType::R :
        accessIndex == 1 ? CASK::AccessType::W :
                           CASK::AccessType::X;

    constexpr unsigned int pmIndex =  translatorHash2 / (3 * 2 * 2);
    constexpr unsigned int translatorHash3 = translatorHash2 - (pmIndex * 3 * 2 * 2);
    constexpr RISCV::PagingMode pagingMode =
        pmIndex == 0 ? RISCV::PagingMode::Bare :
        pmIndex == 1 ? RISCV::PagingMode::Sv32 :
        pmIndex == 2 ? RISCV::PagingMode::Sv39 :
        pmIndex == 3 ? RISCV::PagingMode::Sv48 :
        pmIndex == 4 ? RISCV::PagingMode::Sv57 :
                       RISCV::PagingMode::Sv64;
    
    constexpr unsigned int privIndex =  translatorHash3 / (2 * 2);
    constexpr unsigned int translatorHash4 = translatorHash3 - (privIndex * 2 * 2);
    constexpr RISCV::PrivilegeMode privilege =
        privIndex == 0 ? RISCV::PrivilegeMode::Machine :
        privIndex == 1 ? RISCV::PrivilegeMode::Supervisor :
                         RISCV::PrivilegeMode::User;
    
    constexpr unsigned int mxrIndex = privIndex / 2;
    constexpr unsigned int translatorHash5 = translatorHash4 - mxrIndex * 2;
    constexpr bool mxr = mxrIndex == 1;

    constexpr unsigned int sumIndex = translatorHash5;
    constexpr bool sum = sumIndex == 1;

    constexpr InstantiatedTranslationTemplate<XLEN_t> translator = &TranslationTemplate<XLEN_t, accessType, pagingMode, privilege, mxr, sum>;

    return translator;
}

// (Ab)use recursive template instantiation to fill out an array of specialized
// translation functions for every hash
template <typename XLEN_t, unsigned int currentHash, unsigned int numHashes>
constexpr std::array<InstantiatedTranslationTemplate<XLEN_t>, numHashes> generateTranslatorsForWidthRecursive(std::array<InstantiatedTranslationTemplate<XLEN_t>, numHashes> result) {
    
    if constexpr (currentHash == numHashes) {
        return result;
    } else {
        result = generateTranslatorsForWidthRecursive<XLEN_t, currentHash + 1, numHashes>(result);
        result[currentHash] = generateTranslatorFromHash<XLEN_t, currentHash>();
        return result;
    }

}

// Wrap the above recursive template abuse into a function with no arguments.
template <typename XLEN_t, unsigned int currentHash, unsigned int numHashes>
constexpr std::array<InstantiatedTranslationTemplate<XLEN_t>, numHashes> generateTranslatorsForWidth() {
    std::array<InstantiatedTranslationTemplate<XLEN_t>, numHashes> result = {0};
    return generateTranslatorsForWidthRecursive<XLEN_t, currentHash, numHashes>(result);
}

constexpr std::array<InstantiatedTranslationTemplate<__uint32_t>, 216> TranslatorsXL32 = generateTranslatorsForWidth<__uint32_t, 0, 216>();
constexpr std::array<InstantiatedTranslationTemplate<__uint64_t>, 216> TranslatorsXL64 = generateTranslatorsForWidth<__uint64_t, 0, 216>();
constexpr std::array<InstantiatedTranslationTemplate<__uint128_t>, 216> TranslatorsXL128 = generateTranslatorsForWidth<__uint128_t, 0, 216>();

// With all that out of the way we can implement the MMU API with an added Configure() function
template <typename XLEN_t>
class BranchFreeTranslator final : public Translator<XLEN_t> {

private:

    Register *ppnRegister;
    CASK::IOTarget *bus;

    InstantiatedTranslationTemplate<__uint32_t> translateForRead32;
    InstantiatedTranslationTemplate<__uint32_t> translateForWrite32;
    InstantiatedTranslationTemplate<__uint32_t> translateForFetch32;
    InstantiatedTranslationTemplate<__uint64_t> translateForRead64;
    InstantiatedTranslationTemplate<__uint64_t> translateForWrite64;
    InstantiatedTranslationTemplate<__uint64_t> translateForFetch64;
    InstantiatedTranslationTemplate<__uint128_t> translateForRead128;
    InstantiatedTranslationTemplate<__uint128_t> translateForWrite128;
    InstantiatedTranslationTemplate<__uint128_t> translateForFetch128;

public:

    BranchFreeTranslator(CASK::IOTarget *systemBus) {
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
    virtual void Configure(HartState *state) override;
    virtual Translation<__uint32_t> TranslateRead32(__uint32_t address) override;
    virtual Translation<__uint64_t> TranslateRead64(__uint64_t address) override;
    virtual Translation<__uint128_t> TranslateRead128(__uint128_t address) override;
    virtual Translation<__uint32_t> TranslateWrite32(__uint32_t address) override;
    virtual Translation<__uint64_t> TranslateWrite64(__uint64_t address) override;
    virtual Translation<__uint128_t> TranslateWrite128(__uint128_t address) override;
    virtual Translation<__uint32_t> TranslateFetch32(__uint32_t address) override;
    virtual Translation<__uint64_t> TranslateFetch64(__uint64_t address) override;
    virtual Translation<__uint128_t> TranslateFetch128(__uint128_t address) override;

};
