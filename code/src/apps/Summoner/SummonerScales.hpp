#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "common/dsp/utility/Quantizer.hpp"

namespace kastle2
{

/**
 * @namespace SummonerScales
 * @ingroup apps
 * @brief Summoner's own quantizer scale table (BANK + POT_1), replacing the stock defaults.
 * @author sam
 * @date 2026-08-18
 *
 * The stock Quantizer::DefaultScale table is chromatic/major-heavy and its
 * MINOR_PENTATONIC entry is actually the major diatonic mask (a bug in the
 * shared library, left untouched here). Summoner installs this table instead
 * via Quantizer::SetScaleTable(): minor scales, pentatonics, and the Ethiopian
 * qignit modes. Chromatic and major diatonic are deliberately absent — the
 * quantizer should always be doing musical work.
 *
 * Masks are LSB-first 12-bit semitone bitmasks (bit 0 = root), matching
 * Quantizer::Scale. A later "scale editor" support app can push user tables
 * through the same SetScaleTable() entry point; this array is just the
 * built-in default set.
 *
 * Qignit intervals follow the common Ethiopian pentatonic descriptions
 * (Astatke/Mulatu-era usage):
 *   tizita major   0 2 4 7 9   — the nostalgic one, = major pentatonic
 *   tizita minor   0 2 3 7 8   — same contour, flat 3rd/6th
 *   ambassel       0 1 5 7 8   — semitone above the root, stark
 *   bati major     0 4 5 7 11  — major 3rd + leading tone, bright/exotic
 *   anchihoye      0 1 5 6 8   — tritone qignit, the most unstable
 */
namespace SummonerScales
{

/// Scale slots in knob order (BANK + POT_1 sweeps left to right).
enum class Scale : size_t
{
    MINOR_CHORD,      ///< 0 3 7
    MINOR_PENTATONIC, ///< 0 3 5 7 10
    MINOR_DIATONIC,   ///< 0 2 3 5 7 8 10 (aeolian)
    HARMONIC_MINOR,   ///< 0 2 3 5 7 8 11
    TIZITA_MAJOR,     ///< 0 2 4 7 9   (Ethiopian qignit; = major pentatonic)
    TIZITA_MINOR,     ///< 0 2 3 7 8   (Ethiopian qignit)
    AMBASSEL,         ///< 0 1 5 7 8   (Ethiopian qignit)
    BATI_MAJOR,       ///< 0 4 5 7 11  (Ethiopian qignit)
    ANCHIHOYE,        ///< 0 1 5 6 8   (Ethiopian qignit)
    COUNT
};

/// The table itself, indexed by Scale. Bit i set = semitone i is in the scale.
inline constexpr std::array<Quantizer::Scale, static_cast<size_t>(Scale::COUNT)> kTable = {
    0b000010001001u, ///< MINOR_CHORD      0 3 7
    0b010010101001u, ///< MINOR_PENTATONIC 0 3 5 7 10
    0b010110101101u, ///< MINOR_DIATONIC   0 2 3 5 7 8 10
    0b100110101101u, ///< HARMONIC_MINOR   0 2 3 5 7 8 11
    0b001010010101u, ///< TIZITA_MAJOR     0 2 4 7 9
    0b000110001101u, ///< TIZITA_MINOR     0 2 3 7 8
    0b000110100011u, ///< AMBASSEL         0 1 5 7 8
    0b100010110001u, ///< BATI_MAJOR       0 4 5 7 11
    0b000101100011u, ///< ANCHIHOYE        0 1 5 6 8
};

/// Index the SCALE pot's POT_HALF default lands on (see AppSummoner::MemoryInitialization).
inline constexpr size_t kDefaultIndex = static_cast<size_t>(Scale::TIZITA_MAJOR);

/// Convenience for tests/readers: the semitone set of a slot as a bitmask.
inline constexpr Quantizer::Scale Mask(const Scale scale)
{
    return kTable[static_cast<size_t>(scale)];
}

} // namespace SummonerScales
} // namespace kastle2
