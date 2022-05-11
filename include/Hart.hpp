#pragma once

#include <HartState.hpp>
#include <Translator.hpp>
#include <Transactor.hpp>

#include <Tickable.hpp>

template<typename XLEN_t>
class Hart : public CASK::Tickable {

public:

    Hart()
    : state(RISCV::stringToExtensions("imacsu")) // TODO this is a hack to get it to compile
    { }
    
    virtual inline void Tick() override = 0;
    virtual inline void BeforeFirstTick() override { }
    virtual inline void Reset() override { }

    HartState<XLEN_t> state;
    Transactor<XLEN_t>* transactor;
    XLEN_t resetVector;

};
