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
 *
 * Direction is a 6-zone continuous control (POT_3 + PARAM_2 CV since
 * 2026-08-12): DirectionFromQ15() selects the zone, SkipChanceFromQ15() adds
 * the broken-chord fray that ramps in only at the two hard ends of the range
 * (deep Up / deep Random); the root voice is never skipped.
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

    /** @brief Peak per-voice skip chance at the very ends of the direction range. */
    static constexpr float kMaxSkipChance = 0.4f;

    /**
     * @brief Strum directions (POT_3 + PARAM_2, 6 zones). Voice index 0 is the lowest note.
     */
    enum class Direction
    {
        UP,        ///< Root first, ascending (default)
        DOWN,      ///< Top note first, cascades down
        OUT_TO_IN, ///< Outermost voices first, then middle (0, 3, 1, 2)
        IN_TO_OUT, ///< Innermost voices first, diverges outward (1, 2, 0, 3)
        UP_DOWN,   ///< Ping-pong: alternates Up/Down order per chord fire
        RANDOM,    ///< Fresh random permutation, re-rolled every chord fire
        COUNT
    };

    /**
     * @brief Maps the summed direction knob+CV to a zone (6 equal zones over the range).
     * @param value Direction control 0..Q15_MAX.
     * @return Direction zone.
     */
    static Direction DirectionFromQ15(const q15_t value)
    {
        int32_t zone = (static_cast<int32_t>(value) * static_cast<int32_t>(Direction::COUNT)) / (Q15_MAX + 1);
        if (zone < 0)
        {
            zone = 0;
        }
        if (zone >= static_cast<int32_t>(Direction::COUNT))
        {
            zone = static_cast<int32_t>(Direction::COUNT) - 1;
        }
        return static_cast<Direction>(zone);
    }

    /**
     * @brief Broken-chord skip chance at the direction range's hard ends.
     *
     * Ramps kMaxSkipChance -> 0 across the bottom 3% (deep Up) and
     * 0 -> kMaxSkipChance across the top 3% (deep Random); zero everywhere
     * else, so the four middle zones always play full chords.
     *
     * @param value Direction control 0..Q15_MAX.
     * @return Per-voice skip probability 0..kMaxSkipChance.
     */
    static float SkipChanceFromQ15(const q15_t value)
    {
        constexpr float kEdge = 0.03f;
        const float t = static_cast<float>(value) / static_cast<float>(Q15_MAX);
        if (t < kEdge)
        {
            return kMaxSkipChance * (1.0f - t / kEdge);
        }
        if (t > 1.0f - kEdge)
        {
            return kMaxSkipChance * (t - (1.0f - kEdge)) / kEdge;
        }
        return 0.0f;
    }

    /**
     * @brief Initializes the scheduler.
     * @param seed Seed for the jitter/shuffle/skip noise source (deterministic in tests).
     */
    void Init(const uint32_t seed)
    {
        noise_.Seed(seed);
        fire_count_ = 0;
        Reset();
    }

    /**
     * @brief Schedules a chord fire.
     * @param strum_frames Frames between adjacent voices in the strum order (0 = block chord).
     * @param direction Strum direction.
     * @param humanize Jitter amount 0.0..1.0 (scales +-kMaxHumanizeFrames, re-rolled per voice per fire).
     * @param skip_chance Per-voice broken-chord skip probability 0.0..1.0, re-rolled per voice
     *        per fire; voice 0 (the root) is never skipped.
     */
    void Fire(int32_t strum_frames, const Direction direction, const float humanize,
              const float skip_chance = 0.0f)
    {
        if (strum_frames < 0)
        {
            strum_frames = 0;
        }
        if (strum_frames > kMaxStrumFrames)
        {
            strum_frames = kMaxStrumFrames;
        }

        std::array<size_t, kNumVoices> position;
        FillPositions(direction, position);
        fire_count_++;

        for (size_t v = 0; v < kNumVoices; v++)
        {
            if (v != 0 && skip_chance > 0.0f && RollUnit() < skip_chance)
            {
                countdown_[v] = -1;
                continue;
            }
            int32_t frames = static_cast<int32_t>(position[v]) * strum_frames;
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
     * @brief Cancels all pending voice triggers and restores the ping-pong
     *        alternation to Up (PATTERN R strum-to-0 behavior).
     */
    void Reset()
    {
        countdown_.fill(-1);
        fire_count_ = 0;
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
     * @brief Fills each voice's position (0 = first) in the strum order for a direction.
     *
     * UP_DOWN reads the fire counter's parity (first fire = Up); RANDOM draws a
     * fresh Fisher-Yates permutation from the noise source on every call.
     */
    void FillPositions(const Direction direction, std::array<size_t, kNumVoices> &position)
    {
        switch (direction)
        {
        case Direction::DOWN:
            for (size_t v = 0; v < kNumVoices; v++)
            {
                position[v] = kNumVoices - 1 - v;
            }
            break;
        case Direction::OUT_TO_IN:
            // Fire order 0, 3, 1, 2 -> positions per voice
            position = {0, 2, 3, 1};
            break;
        case Direction::IN_TO_OUT:
            // Fire order 1, 2, 0, 3 -> positions per voice
            position = {2, 0, 1, 3};
            break;
        case Direction::UP_DOWN:
            for (size_t v = 0; v < kNumVoices; v++)
            {
                position[v] = ((fire_count_ & 1u) == 0u) ? v : kNumVoices - 1 - v;
            }
            break;
        case Direction::RANDOM:
        {
            std::array<size_t, kNumVoices> order = {0, 1, 2, 3};
            for (size_t i = kNumVoices - 1; i > 0; i--)
            {
                const size_t j = RollIndex(i + 1);
                const size_t tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
            for (size_t p = 0; p < kNumVoices; p++)
            {
                position[order[p]] = p;
            }
            break;
        }
        case Direction::UP:
        default:
            for (size_t v = 0; v < kNumVoices; v++)
            {
                position[v] = v;
            }
            break;
        }
    }

    /** @brief Uniform draw in [0, 1) from the noise source. */
    float RollUnit()
    {
        const uint32_t bits = static_cast<uint32_t>(noise_.Process()) & 0x7FFFu;
        return static_cast<float>(bits) / 32768.0f;
    }

    /** @brief Uniform draw in [0, n) from the noise source. */
    size_t RollIndex(const size_t n)
    {
        return (static_cast<uint32_t>(noise_.Process()) & 0x7FFFu) % n;
    }

    std::array<int32_t, kNumVoices> countdown_{};
    uint32_t fire_count_ = 0;
    WhiteNoise noise_;
};

}
