#include <SimpleHart.hpp>

SimpleHart::SimpleHart(CASK::IOTarget* bus) : translator(bus), mmu(bus, &translator) {
    state.mmu = &mmu;
    state.currentFetch = &fetch;
    translator.Configure(&state);
}

void SimpleHart::BeforeFirstTick() {
    Reset();
    switch (state.GetXLEN()) {
    case RISCV::XlenMode::XL32:
        Fetch<__uint32_t>();
        break;
    case RISCV::XlenMode::XL64:
        Fetch<__uint64_t>();
        break;
    case RISCV::XlenMode::XL128:
        Fetch<__uint128_t>();
        break;
    default:
        break; // TODO nonsense / fatal
    }
}

void SimpleHart::Tick() {
    fetch.instruction.execute(fetch.operands, &state);
    switch (state.GetXLEN()) {
    case RISCV::XlenMode::XL32:
        Fetch<__uint32_t>();
        break;
    case RISCV::XlenMode::XL64:
        Fetch<__uint64_t>();
        break;
    case RISCV::XlenMode::XL128:
        Fetch<__uint128_t>();
        break;
    default:
        break; // TODO nonsense / fatal
    }
}

void SimpleHart::Reset() {
    state.Reset(&spec);
}
