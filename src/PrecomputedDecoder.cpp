#include <PrecomputedDecoder.hpp>

#include <RiscVDecoder.hpp>

void PrecomputedDecoder::Configure(HartState* state) {

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
            Instruction decoded = decode_full(encoded, state->extensions, state->mxlen, xlen);
            cache[state->extensions][state->mxlen][xlen]->uncompressed[packed] = decoded;
        }

        for (__uint32_t encoded = 0; encoded < 1<<16; encoded++) {
            if ((encoded & 0b11) == 0b11) {
                continue;
            }
            Instruction decoded = decode_full(encoded, state->extensions, state->mxlen, xlen);
            cache[state->extensions][state->mxlen][xlen]->compressed[encoded] = decoded;
        }

    }

    currentLookupTable = cache[state->extensions][state->mxlen][xlen];
}

Instruction PrecomputedDecoder::Decode(__uint32_t encoded) {
    if (RISCV::isCompressed(encoded)) {
        return currentLookupTable->compressed[encoded & 0x0000ffff];
    }
    return currentLookupTable->uncompressed[Pack(encoded)];
}

__uint32_t PrecomputedDecoder::Pack(__uint32_t encoded) {
    return swizzle<__uint32_t, ExtendBits::Zero, 31, 20, 14, 12, 6, 2>(encoded);
}

__uint32_t PrecomputedDecoder::Unpack(__uint32_t packed) {
    return 0b11 |
        ((0b00000000000000011111 & packed) << 2) |
        ((0b00000000000011100000 & packed) << 7) |
        ((0b11111111111100000000 & packed) << 12);
}
