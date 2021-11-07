#pragma once

#include <cstdint>

#include <unordered_map>

#include <RiscV.hpp>

#include <Instruction.hpp>
#include <Operands.hpp>


class PrecomputedDecoder {

private:

    struct LUT {
        std::array<Instruction, 1 << 20> uncompressed;
        std::array<Instruction, 1 << 16> compressed;
    };

    std::unordered_map<__uint32_t,
        std::unordered_map<RISCV::XlenMode,
            std::unordered_map<RISCV::XlenMode,
                LUT*>>> cache;
    LUT *currentLookupTable = nullptr;

public:

    void Configure(HartState* state);
    Instruction Decode(__uint32_t encoded);

private:

    __uint32_t Pack(__uint32_t encoded);
    __uint32_t Unpack(__uint32_t packed);

};
