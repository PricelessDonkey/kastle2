#pragma once

#include <cstddef>
#include <cstdint>
#include "common/config.hpp"

namespace kastle2
{

/**
 * @class SummonerSequencer
 * @ingroup apps
 * @brief Pure mapping helpers for Summoner's euclidean sequencer controls.
 * @author sam
 * @date 2026-07-25
 *
 * Density = BANK+POT_3 summed with the PATTERN C (FEED_3) CV, cycle length =
 * BANK+POT_4 stepped 2..16 (CHORD-GEN.md CV Input Mapping / Euclidean
 * sequencer). Kept hardware-free so the FEED_3 dead-band handling is
 * host-testable.
 */
class SummonerSequencer
{
public:
    /** @brief Stepped cycle-length range (BANK+POT_4): 2..16 steps. */
    static constexpr size_t kMinLength = 2;
    static constexpr size_t kMaxLength = 16;
    static constexpr size_t kLengthSteps = kMaxLength - kMinLength + 1;

    /**
     * @brief FEED_3 readings at or below this contribute no density.
     *
     * The FEED jacks are pulled up for tri-state reads: unconnected rests at
     * ~1620 (Hardware::kFeedCenterApprox, private), indistinguishable from a
     * genuine mid-range CV. Density contribution therefore starts above the
     * tri-state HIGH threshold (mirrors the private Hardware::kFeedHighThreshold)
     * so both "unconnected" and "cable at 0V" read as zero — the pot alone
     * sets the floor, and a +5V gate into PATTERN C adds full density.
     */
    static constexpr int32_t kCvActiveThreshold = 2200;

    /**
     * @brief Maps a raw FEED_3 analog reading to a density contribution in pot units.
     * @param analog Raw analog value (0-4095; unconnected rests mid-range).
     * @return Density contribution 0..POT_MAX (0 at/below the dead band, full scale at 4095).
     */
    static constexpr int32_t DensityCvToPot(int32_t analog)
    {
        if (analog <= kCvActiveThreshold)
        {
            return 0;
        }
        if (analog > POT_MAX)
        {
            analog = POT_MAX;
        }
        return ((analog - kCvActiveThreshold) * POT_MAX) / (POT_MAX - kCvActiveThreshold);
    }

    /**
     * @brief Maps a summed density value to a euclidean hit count.
     * @param density Summed pot + CV density (clamped to 0..POT_MAX here).
     * @param length Current cycle length in steps.
     * @return Hits 0..length; 0 only at the bottom zone (the knob normal-breaker),
     *         length at full scale (the power-on default).
     */
    static constexpr size_t DensityToHits(int32_t density, const size_t length)
    {
        if (density < POT_MIN)
        {
            density = POT_MIN;
        }
        if (density > POT_MAX)
        {
            density = POT_MAX;
        }
        size_t hits = (static_cast<size_t>(density) * (length + 1)) / static_cast<size_t>(POT_RANGE);
        if (hits > length)
        {
            hits = length;
        }
        return hits;
    }

    /**
     * @brief Maps a FancyPot mapped value (map_size = kLengthSteps) to a cycle length.
     * @param mapped Mapped pot value 0..kLengthSteps-1 (negative = not read yet).
     * @return Cycle length kMinLength..kMaxLength.
     */
    static constexpr size_t LengthFromMapped(const int32_t mapped)
    {
        if (mapped < 0)
        {
            return kMaxLength;
        }
        size_t length = kMinLength + static_cast<size_t>(mapped);
        if (length > kMaxLength)
        {
            length = kMaxLength;
        }
        return length;
    }
};

}
