#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"

namespace kastle2
{

/**
 * @class SummonerStrum
 * @ingroup apps
 * @brief Strum scheduler for Summoner: per-voice frame countdowns with direction and humanize.
 * @author sam
 * @date 2026-07-17
 *
 * Pure logic, no hardware dependency (host-testable). On each chord fire the
 * voices get frame countdowns of position * strum_frames per the strum
 * direction (CHORD-GEN.md), plus optional humanize jitter re-rolled from a
 * seeded WhiteNoise on every fire. Tick() is called once per audio frame and
 * reports which voices trigger on that frame.
 */
class SummonerStrum
{
public:
    /** @brief Number of chord voices. */
    static constexpr size_t kNumVoices = 4;

    /** @brief Maximum strum delay between adjacent voices: 300ms at 44kHz. */
    static constexpr int32_t kMaxStrumFrames = 13200;

    /** @brief Maximum humanize jitter: +-25ms at 44kHz. */
    static constexpr int32_t kMaxHumanizeFrames = 1100;

    /**
     * @brief Strum directions (SHIFT + POT_3). Voice index 0 is the lowest note.
     */
    enum class Direction
    {
        LOW_TO_HIGH, ///< Root first, upward (default)
        HIGH_TO_LOW, ///< Top note first, cascades down
        OUT_TO_IN,   ///< Outermost voices first, then middle (0, 3, 1, 2)
        COUNT
    };

    /**
     * @brief Initializes the scheduler.
     * @param seed Seed for the humanize jitter noise source (deterministic in tests).
     */
    void Init(const uint32_t seed)
    {
        noise_.Seed(seed);
        Reset();
    }

    /**
     * @brief Schedules a chord fire.
     * @param strum_frames Frames between adjacent voices in the strum order (0 = block chord).
     * @param direction Strum direction.
     * @param humanize Jitter amount 0.0..1.0 (scales +-kMaxHumanizeFrames, re-rolled per voice per fire).
     */
    void Fire(int32_t strum_frames, const Direction direction, const float humanize)
    {
        if (strum_frames < 0)
        {
            strum_frames = 0;
        }
        if (strum_frames > kMaxStrumFrames)
        {
            strum_frames = kMaxStrumFrames;
        }

        for (size_t v = 0; v < kNumVoices; v++)
        {
            int32_t frames = static_cast<int32_t>(Position(direction, v)) * strum_frames;
            if (humanize > 0.0f)
            {
                // noise in [-1, 1) scaled by the humanize amount
                const float jitter = static_cast<float>(noise_.Process()) / static_cast<float>(Q15_MAX);
                frames += static_cast<int32_t>(jitter * humanize * static_cast<float>(kMaxHumanizeFrames));
            }
            if (frames < 0)
            {
                frames = 0;
            }
            countdown_[v] = frames;
        }
    }

    /**
     * @brief Advances the scheduler by one audio frame.
     * @return Bitmask of voices whose trigger fires on this frame (bit v = voice v).
     */
    uint32_t Tick()
    {
        uint32_t fired = 0;
        for (size_t v = 0; v < kNumVoices; v++)
        {
            if (countdown_[v] < 0)
            {
                continue;
            }
            if (countdown_[v] == 0)
            {
                fired |= (1u << v);
            }
            countdown_[v]--;
        }
        return fired;
    }

    /**
     * @brief Cancels all pending voice triggers (PATTERN R strum-to-0 behavior).
     */
    void Reset()
    {
        countdown_.fill(-1);
    }

    /**
     * @brief Returns a voice's remaining countdown in frames.
     * @param voice Voice index.
     * @return Frames until the voice fires; negative if nothing is pending.
     */
    int32_t GetCountdown(const size_t voice) const
    {
        return countdown_[voice];
    }

private:
    /**
     * @brief Returns a voice's position (0 = first) in the strum order for a direction.
     */
    static size_t Position(const Direction direction, const size_t voice)
    {
        switch (direction)
        {
        case Direction::HIGH_TO_LOW:
            return kNumVoices - 1 - voice;
        case Direction::OUT_TO_IN:
        {
            // Fire order 0, 3, 1, 2 -> positions per voice
            constexpr std::array<size_t, kNumVoices> kPositions = {0, 2, 3, 1};
            return kPositions[voice];
        }
        case Direction::LOW_TO_HIGH:
        default:
            return voice;
        }
    }

    std::array<int32_t, kNumVoices> countdown_{};
    WhiteNoise noise_;
};

}
