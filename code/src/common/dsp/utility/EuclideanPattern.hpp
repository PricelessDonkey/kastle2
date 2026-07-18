/*
MIT License

Copyright (c) 2026 Sam Drilias

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kastle2
{

/**
 * @class EuclideanPattern
 * @ingroup dsp_utility
 * @brief Stateful euclidean rhythm generator (Bjorklund algorithm) with a step counter.
 * @author Sam Drilias
 * @date 2026-07-17
 *
 * Computes the canonical Bjorklund pattern (rotated so step 0 is always a hit
 * when hits > 0), then steps through it one clock tick at a time. Unlike
 * TriggerGenerator::EuclidianTriggerGenerator this supports zero hits (silence),
 * exact hit counts rather than a q15 input mapping, and keeps the step position
 * stable across density/length changes so live tweaks don't re-phase the groove.
 */
class EuclideanPattern
{
public:
    /** @brief Maximum supported pattern length in steps. */
    static constexpr size_t kMaxSteps = 16;

    /**
     * @brief Initializes the generator to the power-on default: all steps hit.
     */
    void Init()
    {
        step_ = 0;
        hits_ = kMaxSteps;
        length_ = kMaxSteps;
        pattern_ = Compute(hits_, length_);
    }

    /**
     * @brief Sets the pattern parameters and recomputes the pattern.
     * @param hits Number of hits per cycle (clamped to 0..length).
     * @param length Cycle length in steps (clamped to 1..kMaxSteps).
     * @note The step position is preserved (modulo the new length), so
     *       changing density mid-cycle doesn't restart the pattern.
     */
    void SetPattern(size_t hits, size_t length)
    {
        if (length < 1)
        {
            length = 1;
        }
        if (length > kMaxSteps)
        {
            length = kMaxSteps;
        }
        if (hits > length)
        {
            hits = length;
        }
        if (hits == hits_ && length == length_)
        {
            return;
        }
        hits_ = hits;
        length_ = length;
        pattern_ = Compute(hits_, length_);
        step_ %= length_;
    }

    /**
     * @brief Advances the pattern by one clock tick.
     * @return True if the step just consumed is a hit.
     */
    bool Step()
    {
        const bool hit = GetStepValue(step_);
        step_ = (step_ + 1) % length_;
        return hit;
    }

    /**
     * @brief Resets the step counter to step 0 (the next Step() consumes step 0).
     */
    void Reset()
    {
        step_ = 0;
    }

    /**
     * @brief Returns the hit/rest value of an arbitrary step.
     * @param index Step index (0-based; indices >= length are rests).
     * @return True if the step is a hit.
     */
    bool GetStepValue(size_t index) const
    {
        return ((pattern_ >> index) & 1u) != 0;
    }

    /**
     * @brief Returns the step the next Step() call will consume.
     * @return The current step index (0-based).
     */
    size_t GetStep() const
    {
        return step_;
    }

    /** @brief Returns the current (clamped) hit count. */
    size_t GetHits() const
    {
        return hits_;
    }

    /** @brief Returns the current (clamped) cycle length. */
    size_t GetLength() const
    {
        return length_;
    }

private:
    /**
     * @brief Computes the canonical Bjorklund pattern as a bitmask (bit i = step i).
     */
    static uint16_t Compute(size_t hits, size_t length)
    {
        if (hits == 0)
        {
            return 0;
        }
        const uint16_t full_mask = static_cast<uint16_t>((1u << length) - 1u);
        if (hits >= length)
        {
            return full_mask;
        }

        // Bjorklund's algorithm: repeated euclidean-style pairing of hit and
        // rest groups, expressed as the standard counts/remainders recursion.
        std::array<size_t, kMaxSteps + 2> counts{};
        std::array<size_t, kMaxSteps + 2> remainders{};
        size_t divisor = length - hits;
        remainders[0] = hits;
        size_t level = 0;
        while (true)
        {
            counts[level] = divisor / remainders[level];
            remainders[level + 1] = divisor % remainders[level];
            divisor = remainders[level];
            level++;
            if (remainders[level] <= 1)
            {
                break;
            }
        }
        counts[level] = divisor;

        uint16_t pattern = 0;
        size_t pos = 0;
        auto build = [&](auto &&self, int lvl) -> void
        {
            if (lvl == -1)
            {
                pos++; // rest
            }
            else if (lvl == -2)
            {
                pattern |= static_cast<uint16_t>(1u << pos); // hit
                pos++;
            }
            else
            {
                for (size_t i = 0; i < counts[lvl]; i++)
                {
                    self(self, lvl - 1);
                }
                if (remainders[lvl] != 0)
                {
                    self(self, lvl - 2);
                }
            }
        };
        build(build, static_cast<int>(level));

        // Rotate so the first hit lands on step 0 (canonical form).
        size_t first_hit = 0;
        while (((pattern >> first_hit) & 1u) == 0 && first_hit < length)
        {
            first_hit++;
        }
        if (first_hit > 0 && first_hit < length)
        {
            pattern = static_cast<uint16_t>((pattern >> first_hit) | (pattern << (length - first_hit)));
            pattern &= full_mask;
        }
        return pattern;
    }

    uint16_t pattern_ = 0;
    size_t step_ = 0;
    size_t hits_ = 0;
    size_t length_ = kMaxSteps;
};

}
