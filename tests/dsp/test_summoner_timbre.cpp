#include "../harness.hpp"
#include "apps/Summoner/SummonerTimbre.hpp"

using namespace kastle2;

// --- EnvAmountFromPot: bipolar, center off -----------------------------------

TEST(SummonerTimbre_AmountCenterIsZero)
{
    // The FancyPot deadzone plateaus at exactly pot(0.5f) — that must map to 0
    ASSERT_TRUE(SummonerTimbre::EnvAmountFromPot(pot(0.5f)) == 0.0f);
}

TEST(SummonerTimbre_AmountEndsAreFullScale)
{
    ASSERT_NEAR(SummonerTimbre::EnvAmountFromPot(0), -1.0f, 1e-6f);
    // POT_MAX lands within integer rounding of +1 and never above it
    const float top = SummonerTimbre::EnvAmountFromPot(POT_MAX);
    ASSERT_TRUE(top > 0.99f && top <= 1.0f);
}

TEST(SummonerTimbre_AmountClampsOverRange)
{
    // Pot + CV style over-range inputs stay in [-1, 1]
    ASSERT_NEAR(SummonerTimbre::EnvAmountFromPot(-500), -1.0f, 1e-6f);
    ASSERT_NEAR(SummonerTimbre::EnvAmountFromPot(POT_MAX + 500), 1.0f, 1e-6f);
}

// --- ModulatedCutoffHz -------------------------------------------------------

TEST(SummonerTimbre_CenterAmountLeavesBase)
{
    ASSERT_NEAR(SummonerTimbre::ModulatedCutoffHz(1000.0f, 0.0f, Q15_MAX), 1000.0f, 1e-3f);
}

TEST(SummonerTimbre_ZeroEnvLeavesBase)
{
    ASSERT_NEAR(SummonerTimbre::ModulatedCutoffHz(1000.0f, 1.0f, 0), 1000.0f, 1e-3f);
    ASSERT_NEAR(SummonerTimbre::ModulatedCutoffHz(1000.0f, -1.0f, 0), 1000.0f, 1e-3f);
}

TEST(SummonerTimbre_PositiveAmountOpensWithEnv)
{
    // Rising envelope opens the filter monotonically at positive amount
    float prev = 0.0f;
    for (int32_t env = 0; env <= Q15_MAX; env += Q15_MAX / 8)
    {
        const float cutoff = SummonerTimbre::ModulatedCutoffHz(200.0f, 1.0f, static_cast<q15_t>(env));
        ASSERT_TRUE(cutoff >= prev);
        prev = cutoff;
    }
    ASSERT_TRUE(prev > 200.0f);
}

TEST(SummonerTimbre_NegativeAmountDarkensWithEnv)
{
    const float hit = SummonerTimbre::ModulatedCutoffHz(2000.0f, -1.0f, Q15_MAX / 4);
    ASSERT_TRUE(hit < 2000.0f);
    // Tail (envelope near zero) brightens back toward the base cutoff
    const float tail = SummonerTimbre::ModulatedCutoffHz(2000.0f, -1.0f, Q15_MAX / 64);
    ASSERT_TRUE(tail > hit);
}

TEST(SummonerTimbre_FullSwingIsSixOctavesBeforeClamp)
{
    // Base low enough that +6 octaves stays under the ceiling: 100Hz -> 6400Hz
    ASSERT_NEAR(SummonerTimbre::ModulatedCutoffHz(100.0f, 1.0f, Q15_MAX), 6400.0f, 20.0f);
}

TEST(SummonerTimbre_ClampsToStableRange)
{
    // Full positive swing from an open filter clamps at the ceiling (Svf needs < rate/3)
    ASSERT_NEAR(SummonerTimbre::ModulatedCutoffHz(12000.0f, 1.0f, Q15_MAX),
                SummonerTimbre::kMaxCutoffHz, 1e-3f);
    // Full negative swing clamps at the stable floor
    ASSERT_NEAR(SummonerTimbre::ModulatedCutoffHz(50.0f, -1.0f, Q15_MAX),
                SummonerTimbre::kMinCutoffHz, 1e-3f);
}
