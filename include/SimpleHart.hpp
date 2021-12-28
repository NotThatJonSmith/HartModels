#pragma once

#include <type_traits>
#include <cstdint>

#include <Hart.hpp>

#include <NaiveTranslator.hpp>
#include <MMU.hpp>
#include <RiscVDecoder.hpp>

class SimpleHart final : public Hart {

public:

    NaiveTranslator translator;
    MMU mmu;
    HartState::Fetch fetch;

public:

    SimpleHart(CASK::IOTarget* bus);

    virtual void BeforeFirstTick() override;
    virtual void Tick() override;
    virtual void Reset() override;

private:

    template<typename XLEN_t>
    void Fetch() {
        
        XLEN_t vpc;
        bool need_fetch = true;
        while (need_fetch) {

            need_fetch = false;

            // Get the next virtual program counter to fetch from
            vpc = state.nextFetchVirtualPC->Read<XLEN_t>();

            // Fill out the virtual PC
            fetch.virtualPC.Write<XLEN_t>(vpc);

            // Fetch the instruction from the MMU
            TransactionResult transactionResult = state.mmu->Fetch<XLEN_t>(vpc, sizeof(fetch.encoding), (char*)&fetch.encoding);
            
            // Raise an exception if the transaction failed - TODO size mismatch / device failure on fetch
            if (transactionResult.trapCause != RISCV::TrapCause::NONE) {
                state.RaiseException<XLEN_t>(transactionResult.trapCause, fetch.virtualPC.Read<XLEN_t>());
                need_fetch = true;
            }
        }

        // Set the default value of the next virtual PC we will fetch if the instruction doesn't change it
        state.nextFetchVirtualPC->Write<XLEN_t>(vpc + RISCV::instructionLength(fetch.encoding));

        // Mask off the higher bits of compressed instructions
        if (RISCV::isCompressed(fetch.encoding)) {
            fetch.encoding &= 0x0000ffff;
        }

        // Decode the instruction
        fetch.instruction = decode_full(fetch.encoding, state.extensions, state.mxlen, state.GetXLEN());

        // Decode the operands
        fetch.operands = fetch.instruction.getOperands(fetch.encoding);

    }
};
