#pragma once

#include <cstdint>
#include <unordered_map>

#include <Decoder.hpp>

#include <RiscV.hpp>

template<typename XLEN_t>
class PrecomputedDecoder final : public Decoder<XLEN_t> {

private:

    struct LUT {
        std::array<DecodedInstruction<XLEN_t>, 1 << 20> uncompressed;
        std::array<DecodedInstruction<XLEN_t>, 1 << 16> compressed;
    };

    std::unordered_map<__uint32_t,
        std::unordered_map<RISCV::XlenMode,
            std::unordered_map<RISCV::XlenMode,
                LUT*>>> cache;
    LUT *currentLookupTable = nullptr;

public:

    void Configure(HartState<XLEN_t>* state) override {

        if (cache.find(state->extensions) == cache.end()) {
            cache.emplace(state->extensions, std::unordered_map<RISCV::XlenMode, std::unordered_map<RISCV::XlenMode, LUT*>>());
        }
        
        if (cache[state->extensions].find(state->mxlen) == cache[state->extensions].end()) {
            cache[state->extensions].emplace(state->mxlen, std::unordered_map<RISCV::XlenMode, LUT*>());
        }

        RISCV::XlenMode xlen = state->GetXLEN();
        if (cache[state->extensions][state->mxlen].find(xlen) == cache[state->extensions][state->mxlen].end()) {

            cache[state->extensions][state->mxlen][xlen] = new LUT;

            for (__uint32_t packed = 0; packed < (1<<20); packed++) {
                __uint32_t encoded = Unpack(packed);
                DecodedInstruction<XLEN_t> decoded = decode_full(encoded, state->extensions, state->mxlen, xlen);
                cache[state->extensions][state->mxlen][xlen]->uncompressed[packed] = decoded;
            }

            for (__uint32_t encoded = 0; encoded < 1<<16; encoded++) {
                if ((encoded & 0b11) == 0b11) {
                    continue;
                }
                DecodedInstruction<XLEN_t> decoded = decode_full(encoded, state->extensions, state->mxlen, xlen);
                cache[state->extensions][state->mxlen][xlen]->compressed[encoded] = decoded;
            }

        }

        currentLookupTable = cache[state->extensions][state->mxlen][xlen];
    }

    DecodedInstruction<XLEN_t> Decode(__uint32_t encoded) override {
        if (RISCV::isCompressed(encoded)) {
            return currentLookupTable->compressed[encoded & 0x0000ffff];
        }
        return currentLookupTable->uncompressed[Pack(encoded)];
    }

private:

    __uint32_t Pack(__uint32_t encoded) {
        return swizzle<__uint32_t, ExtendBits::Zero, 31, 20, 14, 12, 6, 2>(encoded);
    }

    __uint32_t Unpack(__uint32_t packed) {
        return 0b11 |
            ((0b00000000000000011111 & packed) << 2) |
            ((0b00000000000011100000 & packed) << 7) |
            ((0b11111111111100000000 & packed) << 12);
    }


};
