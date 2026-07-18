#pragma once
// Host-build stub for the RP2040 SDK's hardware/sync.h. Svf.cpp uses these
// two functions to guard a coefficient update against the audio ISR on the
// real device. Single-threaded host tests don't need the protection.

#include <cstdint>

inline uint32_t save_and_disable_interrupts()
{
    return 0;
}

inline void restore_interrupts(uint32_t)
{
}
