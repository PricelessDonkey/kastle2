#include <set>
#include <vector>
#include "../harness.hpp"
#include "apps/Summoner/SummonerScales.hpp"

using namespace kastle2;

namespace
{
std::vector<int> Semitones(const SummonerScales::Scale s)
{
    std::vector<int> out;
    const auto mask = SummonerScales::Mask(s);
    for (int i = 0; i < 12; i++)
    {
        if (mask & (1u << i))
        {
            out.push_back(i);
        }
    }
    return out;
}
}

TEST(SummonerScales_TableMatchesDocumentedIntervals)
{
    using S = SummonerScales::Scale;
    ASSERT_TRUE(Semitones(S::MINOR_CHORD) == (std::vector<int>{0, 3, 7}));
    ASSERT_TRUE(Semitones(S::MINOR_PENTATONIC) == (std::vector<int>{0, 3, 5, 7, 10}));
    ASSERT_TRUE(Semitones(S::MINOR_DIATONIC) == (std::vector<int>{0, 2, 3, 5, 7, 8, 10}));
    ASSERT_TRUE(Semitones(S::HARMONIC_MINOR) == (std::vector<int>{0, 2, 3, 5, 7, 8, 11}));
    ASSERT_TRUE(Semitones(S::TIZITA_MAJOR) == (std::vector<int>{0, 2, 4, 7, 9}));
    ASSERT_TRUE(Semitones(S::TIZITA_MINOR) == (std::vector<int>{0, 2, 3, 7, 8}));
    ASSERT_TRUE(Semitones(S::AMBASSEL) == (std::vector<int>{0, 1, 5, 7, 8}));
    ASSERT_TRUE(Semitones(S::BATI_MAJOR) == (std::vector<int>{0, 4, 5, 7, 11}));
    ASSERT_TRUE(Semitones(S::ANCHIHOYE) == (std::vector<int>{0, 1, 5, 6, 8}));
}

TEST(SummonerScales_NoChromaticNoMajorDiatonic_AndAllDistinct)
{
    std::set<Quantizer::Scale> seen;
    for (const auto mask : SummonerScales::kTable)
    {
        ASSERT_TRUE(mask & 1u);                  // every scale contains the root
        ASSERT_TRUE(mask != 0b111111111111u);    // no chromatic
        ASSERT_TRUE(mask != 0b101010110101u);    // no major diatonic
        ASSERT_TRUE(seen.insert(mask).second);   // no duplicate slots
    }
    ASSERT_EQ(static_cast<int>(SummonerScales::kTable.size()),
              static_cast<int>(SummonerScales::Scale::COUNT));
}

TEST(SummonerScales_QuantizerUsesTable_AndPotHalfLandsOnTizitaMajor)
{
    // The SCALE pot maps POT_HALF over map_size = table size; index 4 is the default.
    const size_t idx = (2048u * SummonerScales::kTable.size()) / 4096u;
    ASSERT_EQ(static_cast<int>(idx), static_cast<int>(SummonerScales::kDefaultIndex));

    Quantizer q;
    q.Init();
    q.SetScaleTable(SummonerScales::kTable);
    ASSERT_EQ(static_cast<int>(q.GetScaleTableSize()),
              static_cast<int>(SummonerScales::kTable.size()));
    q.SetEnabled(true);
    q.SetScale(SummonerScales::kDefaultIndex);

    // Tizita major on C: C4 (261.63) stays, C#4 (277.18) snaps to D4 (293.66),
    // F4 (349.23) is out of scale and must land on a scale tone (E4 or G4).
    ASSERT_NEAR(q.Process(261.63f), 261.63f, 0.5f);
    ASSERT_NEAR(q.Process(277.18f), 293.66f, 0.5f);
    const float f = q.Process(349.23f);
    ASSERT_TRUE(std::abs(f - 329.63f) < 0.5f || std::abs(f - 392.00f) < 0.5f);
}
