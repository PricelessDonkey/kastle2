#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/utility/Quantizer.hpp"

namespace kastle2
{

/**
 * @class SummonerChords
 * @ingroup apps
 * @brief Chord engine for Summoner: quality table, continuous voicing interpolation, scale snap.
 * @author sam
 * @date 2026-07-17
 *
 * Pure math, no hardware dependency (host-testable). Pipeline per CHORD-GEN.md:
 * root frequency + quality zone (POT_6) + voicing position (PARAM_1, 0..1 across
 * 5 anchors, fully interpolated) -> 4 voice frequencies, each snapped to the
 * active scale by the app's Quantizer so quality/voicing sweeps never leave the key.
 */
class SummonerChords
{
public:
    /** @brief Number of chord voices. */
    static constexpr size_t kNumVoices = 4;

    /** @brief Number of voicing anchor states (0%, 25%, 50%, 75%, 100%). */
    static constexpr size_t kNumAnchors = 5;

    /**
     * @brief Chord qualities swept by POT_6, in zone order.
     */
    enum class Quality
    {
        MAJOR,
        MINOR,
        DOM7,
        MIN7,
        MAJ7,
        SUS2,
        SUS4,
        DIM,
        COUNT
    };

    static constexpr size_t kNumQualities = static_cast<size_t>(Quality::COUNT);

    /**
     * @brief Maps a normalized q15 control value (pot + CV sum) to a quality zone.
     * @param value Normalized control value, 0..Q15_MAX.
     * @return The quality whose zone center is nearest to the value.
     * @note Zone centers follow the CHORD-GEN.md table: 0/15/30/45/60/75/85/100%.
     */
    static Quality QualityFromQ15(q15_t value)
    {
        size_t best = 0;
        int32_t best_dist = INT32_MAX;
        for (size_t i = 0; i < kNumQualities; i++)
        {
            int32_t dist = static_cast<int32_t>(value) - kZoneCenters[i];
            if (dist < 0)
            {
                dist = -dist;
            }
            if (dist < best_dist)
            {
                best_dist = dist;
                best = i;
            }
        }
        return static_cast<Quality>(best);
    }

    /**
     * @brief Computes the 4 voice offsets in semitones above the root, unsnapped.
     * @param quality Chord quality.
     * @param voicing Voicing position 0.0..1.0 (0 = close, 1 = wide + extensions).
     * @param offsets Output: semitone offsets per voice (fractional between anchors).
     */
    static void GetOffsets(Quality quality, float voicing, std::array<float, kNumVoices> &offsets)
    {
        if (voicing < 0.0f)
        {
            voicing = 0.0f;
        }
        if (voicing > 1.0f)
        {
            voicing = 1.0f;
        }

        // Segment between adjacent anchors + interpolation fraction
        const float position = voicing * static_cast<float>(kNumAnchors - 1);
        size_t segment = static_cast<size_t>(position);
        if (segment >= kNumAnchors - 1)
        {
            segment = kNumAnchors - 2;
        }
        const float t = position - static_cast<float>(segment);

        for (size_t v = 0; v < kNumVoices; v++)
        {
            const float a = AnchorSemitones(quality, segment, v);
            const float b = AnchorSemitones(quality, segment + 1, v);
            offsets[v] = a + (b - a) * t;
        }
    }

    /**
     * @brief Computes the 4 voice frequencies for a chord, snapped to the active scale.
     * @param root_frequency Root pitch in Hz.
     * @param quality Chord quality.
     * @param voicing Voicing position 0.0..1.0.
     * @param quantizer The app's quantizer (scale already selected); snaps each voice when enabled.
     * @param frequencies Output: per-voice frequencies in Hz.
     */
    static void ComputeChord(float root_frequency, Quality quality, float voicing,
                             Quantizer &quantizer, std::array<float, kNumVoices> &frequencies)
    {
        std::array<float, kNumVoices> offsets;
        GetOffsets(quality, voicing, offsets);
        for (size_t v = 0; v < kNumVoices; v++)
        {
            const float freq = root_frequency * std::pow(2.0f, offsets[v] / 12.0f);
            frequencies[v] = quantizer.Process(freq);
        }
    }

private:
    /** @brief Quality-zone centers on the q15 control range (0/15/30/45/60/75/85/100%). */
    static constexpr std::array<int32_t, kNumQualities> kZoneCenters = {
        0,
        static_cast<int32_t>(Q15_MAX * 0.15f),
        static_cast<int32_t>(Q15_MAX * 0.30f),
        static_cast<int32_t>(Q15_MAX * 0.45f),
        static_cast<int32_t>(Q15_MAX * 0.60f),
        static_cast<int32_t>(Q15_MAX * 0.75f),
        static_cast<int32_t>(Q15_MAX * 0.85f),
        Q15_MAX,
    };

    /**
     * @brief Chord tones per quality, semitones above root. Tone 3 is the 7th
     *        for seventh chords and the doubled root (+12) for triads.
     */
    static constexpr std::array<std::array<int8_t, 4>, kNumQualities> kQualityTones = {{
        {0, 4, 7, 12},  // MAJOR
        {0, 3, 7, 12},  // MINOR
        {0, 4, 7, 10},  // DOM7
        {0, 3, 7, 10},  // MIN7
        {0, 4, 7, 11},  // MAJ7
        {0, 2, 7, 12},  // SUS2
        {0, 5, 7, 12},  // SUS4
        {0, 3, 6, 12},  // DIM
    }};

    /** @brief Sentinel tone index meaning "the 9th" (root + 14), used by the wide anchor. */
    static constexpr int8_t kNinth = 4;

    /**
     * @brief Voicing anchors: per voice a chord-tone index (or kNinth) and an octave shift.
     * Anchor rows (CHORD-GEN.md, C major illustration; tone 3 = doubled root for
     * triads, the 7th for seventh chords, so seventh qualities stay audible at
     * every voicing, not just the "spread + 7th" anchor):
     * close (C4 E4 G4 C5), 1st inversion (E4 G4 C5 E5), open (C4 G4 C5 E5),
     * spread + 7th (C4 G4 C6 E5; dom7 gives the doc's C4 G4 Bb5 E5),
     * wide + extensions (C3 G4 D5 E5).
     */
    struct AnchorNote
    {
        int8_t tone;
        int8_t octave;
    };

    static constexpr std::array<std::array<AnchorNote, kNumVoices>, kNumAnchors> kAnchors = {{
        {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}}, // 0%   close
        {{{1, 0}, {2, 0}, {3, 0}, {1, 1}}}, // 25%  first inversion
        {{{0, 0}, {2, 0}, {3, 0}, {1, 1}}}, // 50%  open
        {{{0, 0}, {2, 0}, {3, 1}, {1, 1}}}, // 75%  spread + 7th
        {{{0, -1}, {2, 0}, {kNinth, 0}, {1, 1}}}, // 100% wide + extensions
    }};

    /**
     * @brief Resolves one anchor slot to semitones above root for a quality.
     */
    static float AnchorSemitones(Quality quality, size_t anchor, size_t voice)
    {
        const AnchorNote &note = kAnchors[anchor][voice];
        const int32_t base = (note.tone == kNinth)
                                 ? 14
                                 : kQualityTones[static_cast<size_t>(quality)][note.tone];
        return static_cast<float>(base + 12 * note.octave);
    }
};

}
