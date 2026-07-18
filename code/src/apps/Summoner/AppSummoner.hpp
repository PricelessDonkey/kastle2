#pragma once

#include <cstddef>
#include <cstdint>
#include "common/core/App.hpp"
#include "common/core/Kastle2.hpp"
#include "common/dsp/math/qmath.hpp"

namespace kastle2
{

/**
 * @class AppSummoner
 * @ingroup apps
 * @brief App description
 * @author sam
 * @date 2026-07-17
 */
class AppSummoner : public virtual App
{
public:
    /**
     * @brief Initializes all the parameters, memory, etc.
     */
    void Init();

    /**
     * @brief Deinitializes the app, stops all effects, etc.
     */
    void DeInit();

    /**
     * @brief Called each interrupt loop. Implements all the audio processing.
     * @param input Input buffer.
     * @param output Output buffer.
     * @param size Number of sample pairs in the buffer (real size of the buffer is 2*size).
     */
    FASTCODE void AudioLoop(q15_t *input, q15_t *output, size_t size);

    /**
     * @brief Called each time AudioLoop isn't busy.
     */
    void UiLoop();

    /**
     * @brief Called when the app is first loaded - initializes the memory values.
     */
    void MemoryInitialization() {}

    /**
     * @brief Returns the app ID.
     * @return The app ID.
     */
    uint8_t GetId()
    {
        return Kastle2::kDefaultAppId;
    }

private:
    // Put private variables here
};
}
