// #include <PrefetchableProgramCounter.hpp>

// #include <OptimizedHart.hpp>

// PrefetchableProgramCounter::PrefetchableProgramCounter(OptimizedHart *owner)
//     : hart(owner) {
// }

// __uint32_t PrefetchableProgramCounter::Read32() {
//     __uint32_t value = hart->state.currentFetch->virtualPC.Read<__uint32_t>();
//     CommitReadSideEffects();
//     return value;
// }

// __uint64_t PrefetchableProgramCounter::Read64() {
//     __uint64_t value = hart->state.currentFetch->virtualPC.Read<__uint64_t>();
//     CommitReadSideEffects();
//     return value;
// }

// __uint128_t PrefetchableProgramCounter::Read128() {
//     __uint128_t value = hart->state.currentFetch->virtualPC.Read<__uint128_t>();
//     CommitReadSideEffects();
//     return value;
// }

// void PrefetchableProgramCounter::Write32(__uint32_t value) {
//     hart->fetchService.Pause();
//     Set32(value);
//     hart->fetchService.Run();
//     CommitWriteSideEffects();
// }

// void PrefetchableProgramCounter::Write64(__uint64_t value) {
//     hart->fetchService.Pause();
//     Set64(value);
//     hart->fetchService.Run();
//     CommitWriteSideEffects();
// }

// void PrefetchableProgramCounter::Write128(__uint128_t value) {
//     hart->fetchService.Pause();
//     Set128(value);
//     hart->fetchService.Run();
//     CommitWriteSideEffects();
// }
