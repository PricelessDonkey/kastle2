#include "../harness.hpp"
#include "apps/Summoner/SummonerChords.hpp"

using namespace kastle2;

namespace
{
constexpr float kC4 = 261.63f;

// Frequency -> semitone index relative to A440 grid, for in-scale checks.
int SemitoneOf(float freq)
{
    const float midi = 69.0f + 12.0f * std::log2(freq / 440.0f);
    const int rounded = static_cast<int>(std::lround(midi));
    return ((rounded % 12) + 12) % 12;
}
}

TEST(SummonerChords_QualityTable_CloseVoicingOffsets)
{
    // Tone 3 is the doubled root (+12) for triads, the 7th for seventh chords.
    std::array<float, SummonerChords::kNumVoices> o;

    SummonerChords::GetOffsets(SummonerChords::Quality::MAJOR, 0.0f, o);
    ASSERT_EQ(o[0], 0.0f);
    ASSERT_EQ(o[1], 4.0f);
    ASSERT_EQ(o[2], 7.0f);
    ASSERT_EQ(o[3], 12.0f);

    SummonerChords::GetOffsets(SummonerChords::Quality::MIN7, 0.0f, o);
    ASSERT_EQ(o[0], 0.0f);
    ASSERT_EQ(o[1], 3.0f);
    ASSERT_EQ(o[2], 7.0f);
    ASSERT_EQ(o[3], 10.0f);

    SummonerChords::GetOffsets(SummonerChords::Quality::DIM, 0.0f, o);
    ASSERT_EQ(o[1], 3.0f);
    ASSERT_EQ(o[2], 6.0f);

    SummonerChords::GetOffsets(SummonerChords::Quality::SUS4, 0.0f, o);
    ASSERT_EQ(o[1], 5.0f);
}

TEST(SummonerChords_QualityFromQ15_ZoneCenters)
{
    // Exact zone centers from the CHORD-GEN.md table map to their quality.
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(0)),
              static_cast<int>(SummonerChords::Quality::MAJOR));
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(static_cast<q15_t>(Q15_MAX * 0.15f))),
              static_cast<int>(SummonerChords::Quality::MINOR));
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(static_cast<q15_t>(Q15_MAX * 0.45f))),
              static_cast<int>(SummonerChords::Quality::MIN7));
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(static_cast<q15_t>(Q15_MAX * 0.85f))),
              static_cast<int>(SummonerChords::Quality::SUS4));
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(Q15_MAX)),
              static_cast<int>(SummonerChords::Quality::DIM));
}

TEST(SummonerChords_QualityFromQ15_NearestCenterWins)
{
    // 10% is nearer the 15% (MINOR) center than the 0% (MAJOR) one.
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(static_cast<q15_t>(Q15_MAX * 0.10f))),
              static_cast<int>(SummonerChords::Quality::MINOR));
    // 93% sits between SUS4 (85%) and DIM (100%), nearer DIM.
    ASSERT_EQ(static_cast<int>(SummonerChords::QualityFromQ15(static_cast<q15_t>(Q15_MAX * 0.93f))),
              static_cast<int>(SummonerChords::Quality::DIM));
}

TEST(SummonerChords_VoicingMidpoint_InterpolatesLinearly)
{
    // 12.5% = midway between close {0,4,7,12} and 1st inversion {4,7,12,16}.
    std::array<float, SummonerChords::kNumVoices> o;
    SummonerChords::GetOffsets(SummonerChords::Quality::MAJOR, 0.125f, o);
    ASSERT_NEAR(o[0], 2.0f, 0.001);
    ASSERT_NEAR(o[1], 5.5f, 0.001);
    ASSERT_NEAR(o[2], 9.5f, 0.001);
    ASSERT_NEAR(o[3], 14.0f, 0.001);
}

TEST(SummonerChords_VoicingAnchors_MatchDesignTable)
{
    std::array<float, SummonerChords::kNumVoices> o;

    // 25% first inversion: E4 G4 C5 E5 (major, semitones from root)
    SummonerChords::GetOffsets(SummonerChords::Quality::MAJOR, 0.25f, o);
    ASSERT_NEAR(o[0], 4.0f, 0.001);
    ASSERT_NEAR(o[1], 7.0f, 0.001);
    ASSERT_NEAR(o[2], 12.0f, 0.001);
    ASSERT_NEAR(o[3], 16.0f, 0.001);

    // 75% spread + 7th with DOM7: C4 G4 Bb5 E5 — the doc's example row.
    SummonerChords::GetOffsets(SummonerChords::Quality::DOM7, 0.75f, o);
    ASSERT_NEAR(o[0], 0.0f, 0.001);
    ASSERT_NEAR(o[1], 7.0f, 0.001);
    ASSERT_NEAR(o[2], 22.0f, 0.001);
    ASSERT_NEAR(o[3], 16.0f, 0.001);

    // 100% wide + extensions: C3 G4 D5 E5.
    SummonerChords::GetOffsets(SummonerChords::Quality::MAJOR, 1.0f, o);
    ASSERT_NEAR(o[0], -12.0f, 0.001);
    ASSERT_NEAR(o[1], 7.0f, 0.001);
    ASSERT_NEAR(o[2], 14.0f, 0.001);
    ASSERT_NEAR(o[3], 16.0f, 0.001);
}

TEST(SummonerChords_CMajorCloseVoicing_IsC4E4G4PlusDoubledRoot)
{
    Quantizer q;
    q.Init();
    q.SetEnabled(true);
    q.SetScale(Quantizer::DefaultScale::MAJOR_DIATONIC);

    std::array<float, SummonerChords::kNumVoices> f;
    SummonerChords::ComputeChord(kC4, SummonerChords::Quality::MAJOR, 0.0f, q, f);

    ASSERT_NEAR(f[0], 261.63f, 0.5); // C4
    ASSERT_NEAR(f[1], 329.63f, 0.5); // E4
    ASSERT_NEAR(f[2], 392.00f, 0.5); // G4
    ASSERT_NEAR(f[3], 523.25f, 1.0); // C5 (doubled root)
}

TEST(SummonerChords_AllQualitiesAndVoicings_StayInScale)
{
    // The design promise: quality/voicing sweeps never leave the key.
    Quantizer q;
    q.Init();
    q.SetEnabled(true);
    q.SetScale(Quantizer::DefaultScale::MAJOR_DIATONIC);

    constexpr std::array<int, 7> kCMajor = {0, 2, 4, 5, 7, 9, 11};

    for (size_t quality = 0; quality < SummonerChords::kNumQualities; quality++)
    {
        for (int step = 0; step <= 20; step++)
        {
            const float voicing = static_cast<float>(step) / 20.0f;
            std::array<float, SummonerChords::kNumVoices> f;
            SummonerChords::ComputeChord(kC4, static_cast<SummonerChords::Quality>(quality),
                                         voicing, q, f);
            for (size_t v = 0; v < SummonerChords::kNumVoices; v++)
            {
                const int semitone = SemitoneOf(f[v]);
                bool in_scale = false;
                for (int s : kCMajor)
                {
                    if (s == semitone)
                    {
                        in_scale = true;
                    }
                }
                ASSERT_TRUE(in_scale);
            }
        }
    }
}
