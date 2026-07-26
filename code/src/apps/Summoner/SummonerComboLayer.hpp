#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "common/config.hpp"

namespace kastle2
{

/**
 * @class SummonerComboLayer
 * @ingroup apps
 * @brief SHIFT+BANK fourth knob layer for Summoner: per-slot pickup state machine.
 * @author sam
 * @date 2026-07-25
 *
 * Hardware::Layer has no combined SHIFT+MODE state, so this layer is app-side.
 * Pure logic, no hardware dependency (host-testable): the app feeds it the
 * combo-button state and raw pot values each UI pass. While the combo is held,
 * a slot "engages" only after its pot moves past a pickup threshold from where
 * it sat when the hold began (same idea as Hardware's pot freezing) — so
 * holding both buttons never jumps a value, and a hold with no pot movement
 * leaves every slot untouched (preserving tap tempo and the settings timers
 * per the CHORD-GEN.md button-gesture rules; the app uses AnyEngaged() to
 * cancel those on movement). Values persist across holds.
 */
class SummonerComboLayer
{
public:
    /** @brief One slot per knob (POT_1..POT_7 order per the CHORD-GEN.md SHIFT+BANK column). */
    static constexpr size_t kNumSlots = 7;

    /** @brief Pot movement (raw 0-4095) needed to engage a slot — matches Hardware's unfreeze threshold. */
    static constexpr int32_t kEngageThreshold = POT_MOVE_THRESHOLD;

    /**
     * @brief Resets the state machine. Slot values default to 0; load persisted
     *        values with SetSlotValue() after calling this.
     */
    void Init()
    {
        active_ = false;
        just_ended_ = false;
        any_engaged_ = false;
        engaged_.fill(false);
        changed_.fill(false);
        refs_.fill(0);
        values_.fill(0);
    }

    /**
     * @brief Sets a slot's value directly (power-on defaults / persisted values).
     * @param slot Slot index (0-based, = pot number - 1).
     * @param value Value in raw pot range 0-4095.
     */
    void SetSlotValue(const size_t slot, const int32_t value)
    {
        values_[slot] = value;
    }

    /**
     * @brief Advances the state machine; call once per UI pass.
     * @param combo_pressed True while both SHIFT and BANK are held.
     * @param raw_values Raw pot values (0-4095) for the seven slots.
     */
    void Process(const bool combo_pressed, const std::array<int32_t, kNumSlots> &raw_values)
    {
        just_ended_ = false;
        changed_.fill(false);

        if (combo_pressed && !active_)
        {
            // Hold begins: snapshot references, nothing engaged yet
            active_ = true;
            any_engaged_ = false;
            engaged_.fill(false);
            refs_ = raw_values;
        }
        else if (!combo_pressed && active_)
        {
            active_ = false;
            just_ended_ = true;
        }

        if (!active_)
        {
            return;
        }

        for (size_t s = 0; s < kNumSlots; s++)
        {
            if (!engaged_[s])
            {
                const int32_t delta = raw_values[s] - refs_[s];
                if (delta > kEngageThreshold || delta < -kEngageThreshold)
                {
                    engaged_[s] = true;
                    any_engaged_ = true;
                }
            }
            if (engaged_[s] && values_[s] != raw_values[s])
            {
                values_[s] = raw_values[s];
                changed_[s] = true;
            }
        }
    }

    /** @brief True while the combo hold is active. */
    bool IsActive() const
    {
        return active_;
    }

    /** @brief True for the one Process() call after the combo is released. */
    bool JustEnded() const
    {
        return just_ended_;
    }

    /** @brief True once any slot engaged during the current (or just-ended) hold. */
    bool AnyEngaged() const
    {
        return any_engaged_;
    }

    /**
     * @brief Whether a slot is picked up (pot moved past the threshold) in the current hold.
     * @param slot Slot index.
     */
    bool IsEngaged(const size_t slot) const
    {
        return engaged_[slot];
    }

    /**
     * @brief Whether a slot's value changed on the last Process() call.
     * @param slot Slot index.
     */
    bool HasChanged(const size_t slot) const
    {
        return changed_[slot];
    }

    /**
     * @brief Returns a slot's current value (raw pot range 0-4095).
     * @param slot Slot index.
     */
    int32_t GetValue(const size_t slot) const
    {
        return values_[slot];
    }

private:
    bool active_ = false;
    bool just_ended_ = false;
    bool any_engaged_ = false;
    std::array<bool, kNumSlots> engaged_{};
    std::array<bool, kNumSlots> changed_{};
    std::array<int32_t, kNumSlots> refs_{};
    std::array<int32_t, kNumSlots> values_{};
};

}
