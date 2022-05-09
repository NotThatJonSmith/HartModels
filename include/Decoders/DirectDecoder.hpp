#pragma once

#include <Decoder.hpp>
#include <CodePoint.hpp>

#include <RiscVDecoder.hpp>

template<typename XLEN_t>
class DirectDecoder final : public Decoder<XLEN_t> {

private:

    HartState<XLEN_t>* state;

public:

    void Configure(HartState<XLEN_t>* hartState) override {
        state = hartState;
    }

    DecodedInstruction<XLEN_t> Decode(__uint32_t encoded) override {
        CodePoint codePoint = decode_instruction(encoded, state->extensions, state->mxlen);
        DecodedInstruction<XLEN_t> decoded = {};
        decoded.disassemble = codePoint.disassemble;
        decoded.getOperands = codePoint.getOperands;
        decoded.width = codePoint.width;
        if constexpr (std::is_same<XLEN_t, __uint32_t>()) {
            decoded.execute = codePoint.execute32;
        } else if constexpr (std::is_same<XLEN_t, __uint64_t>()) {
            decoded.execute = codePoint.execute64;
        } else /*if constexpr (std::is_same<XLEN_t, __uint128_t>())*/ {
            decoded.execute = codePoint.execute128;
        }
        return decoded;
    }

};
