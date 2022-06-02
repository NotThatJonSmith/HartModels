#pragma once

#include <HartState.hpp>
#include <Translator.hpp>
#include <Transactor.hpp>

#include <Tickable.hpp>

template<typename XLEN_t>
class Hart : public CASK::Tickable {

public:

    Hart(__uint32_t maximalExtensions) : state(maximalExtensions) { }
    
    virtual inline void Tick() override = 0;
    virtual inline void BeforeFirstTick() override { }
    virtual inline void Reset() override { }

    HartState<XLEN_t> state;
    XLEN_t resetVector;

};
