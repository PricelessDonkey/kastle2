#pragma once
// Host-build stub for lib/I2S.hpp. common/config.hpp only reads
// I2S::kAudioBufferSize to define AUDIO_BUFFER_SIZE — mirror that one
// constant instead of pulling in the real PIO/DMA-based driver.

#include <cstddef>

class I2S
{
public:
    static constexpr size_t kAudioBufferSize = 48;
};
