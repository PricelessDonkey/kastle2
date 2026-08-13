#pragma once

#include <cstddef>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/synthesis/WhiteNoise.hpp"

namespace kastle2
{

/**
 * @class SummonerGroove
 * @ingroup apps
 * @brief Groove control for Summoner's euclidean hits: humanize / swing / skip zones.
 * @author sam
 * @date 2026-08-12
 *
 * Pure logic, no hardware dependency (host-testable). SHIFT+BANK+POT_3 spans
 * three character zones (CHORD-GEN.md Groove table): 0-33% humanize (per-voice
 * strum jitter — computed here, applied by SummonerStrum), 34-66% swing
 * (odd euclidean steps fire late by up to kMaxSwing of the step period),
 * 67-100% skip (per-hit Bernoulli roll cancels a scheduled hit). Only
 * internally-generated euclidean hits pass through ProcessHit(); TRIG_IN's
 * additive fires must bypass this class entirely.
 *
 * The step period is measured by counting Tick() calls (one per audio frame)
 * between OnClockTick() calls, so swing adapts to the live tempo.
 */
class SummonerGroove
{
public:
    /** @brief Maximum swing delay as a fraction of the step period. */
    static constexpr float kMaxSwing = 0.66f;

    /** @brief Maximum per-hit skip probability at the top of the knob. */
    static constexpr float kMaxSkipChance = 0.6f;

    /** @brief What to do with a scheduled euclidean hit. */
    enum class HitAction
    {
        FIRE,     ///< Fire the chord now
        DEFERRED, ///< Swing: the fire matures via Tick() after the delay
        SKIPPED   ///< Skip: the hit is cancelled outright
    };

    /**
     * @brief Humanize amount from the groove control (bottom third of the range).
     * @param value Groove control 0..Q15_MAX.
     * @return Jitter amount 0..1 (0 outside the humanize zone).
     */
    static float HumanizeFromQ15(const q15_t value)
    {
        const float t = ZonePosition(value, 0);
        return (t > 0.0f) ? t : 0.0f;
    }

    /**
     * @brief Swing amount from the groove control (middle third of the range).
     * @param value Groove control 0..Q15_MAX.
     * @return Odd-step delay as a fraction of the step period, 0..kMaxSwing.
     */
    static float SwingFromQ15(const q15_t value)
    {
        const float t = ZonePosition(value, 1);
        return (t > 0.0f) ? t * kMaxSwing : 0.0f;
    }

    /**
     * @brief Skip chance from the groove control (top third of the range).
     * @param value Groove control 0..Q15_MAX.
     * @return Per-hit cancel probability, 0..kMaxSkipChance.
     */
    static float SkipChanceFromQ15(const q15_t value)
    {
        const float t = ZonePosition(value, 2);
        return (t > 0.0f) ? t * kMaxSkipChance : 0.0f;
    }

    /**
     * @brief Initializes the groove state.
     * @param seed Seed for the skip-roll noise source (deterministic in tests).
     */
    void Init(const uint32_t seed)
    {
        noise_.Seed(seed);
        period_frames_ = 0;
        frames_since_tick_ = 0;
        Reset();
    }

    /**
     * @brief Registers a clock tick; measures the step period from the Tick() count.
     */
    void OnClockTick()
    {
        if (frames_since_tick_ > 0)
        {
            period_frames_ = frames_since_tick_;
        }
        frames_since_tick_ = 0;
    }

    /**
     * @brief Decides what happens to a scheduled euclidean hit.
     * @param step Euclidean step index (parity selects which steps swing).
     * @param groove Groove control value 0..Q15_MAX.
     * @return FIRE, DEFERRED (poll Tick() for maturity) or SKIPPED. A new
     *         deferral overwrites any pending one.
     */
    HitAction ProcessHit(const size_t step, const q15_t groove)
    {
        const float skip_chance = SkipChanceFromQ15(groove);
        if (skip_chance > 0.0f && RollUnit() < skip_chance)
        {
            return HitAction::SKIPPED;
        }

        const float swing = SwingFromQ15(groove);
        if (swing > 0.0f && (step & 1u) != 0u && period_frames_ > 0)
        {
            const int32_t delay = static_cast<int32_t>(swing * static_cast<float>(period_frames_));
            if (delay > 0)
            {
                pending_ = delay;
                return HitAction::DEFERRED;
            }
        }
        return HitAction::FIRE;
    }

    /**
     * @brief Advances by one audio frame; counts the step period.
     * @return true when a deferred (swung) fire matures on this frame.
     */
    bool Tick()
    {
        frames_since_tick_++;
        if (pending_ < 0)
        {
            return false;
        }
        pending_--;
        return pending_ < 0;
    }

    /**
     * @brief Cancels any pending deferred fire (PATTERN R reset behavior).
     */
    void Reset()
    {
        pending_ = -1;
    }

private:
    /**
     * @brief Position 0..1 within one of the three equal zones; <= 0 outside it.
     */
    static float ZonePosition(const q15_t value, const int zone)
    {
        constexpr float kThird = 1.0f / 3.0f;
        const float t = static_cast<float>(value) / static_cast<float>(Q15_MAX);
        const float lo = static_cast<float>(zone) * kThird;
        if (t > lo + kThird)
        {
            return 0.0f;
        }
        return (t - lo) / kThird;
    }

    /** @brief Uniform draw in [0, 1) from the noise source. */
    float RollUnit()
    {
        const uint32_t bits = static_cast<uint32_t>(noise_.Process()) & 0x7FFFu;
        return static_cast<float>(bits) / 32768.0f;
    }

    int32_t period_frames_ = 0;
    int32_t frames_since_tick_ = 0;
    int32_t pending_ = -1;
    WhiteNoise noise_;
};

}
