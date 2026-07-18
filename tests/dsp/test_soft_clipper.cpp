#include "../harness.hpp"
#include "common/dsp/effects/SoftClipper.hpp"
#include <algorithm>
#include <cmath>

using namespace kastle2;

TEST(SoftClipper_SilenceInSilenceOut)
{
    SoftClipper clipper;
    clipper.Init(44000.0f);
    ASSERT_EQ(clipper.Process(0), 0);
}

TEST(SoftClipper_IsApproximatelyOddSymmetric)
{
    // Process() computes a sign flag from the input and reapplies it after
    // the (always-positive) tanh lookup, so output should roughly mirror
    // the input's sign. It's not bit-exact, though: the drive stage
    // (`val + ((val * drive_) >> 9)`) right-shifts a signed intermediate,
    // which rounds toward -infinity — so positive and negative inputs pick
    // up systematically different rounding, growing with drive and
    // magnitude. Use a relative tolerance rather than a fixed LSB budget.
    SoftClipper clipper;
    clipper.Init(44000.0f);
    for (q15_t drive : {q15_t(0), q15_t(Q15_MAX / 2), q15_t(Q15_MAX)})
    {
        clipper.SetDrive(drive);
        for (q15_t input : {q15_t(1000), q15_t(10000), q15_t(Q15_MAX / 2), q15_t(Q15_MAX)})
        {
            q15_t pos = clipper.Process(input);
            q15_t neg = clipper.Process(-input);
            ASSERT_TRUE((pos > 0) == (neg < 0) || pos == 0);
            double eps = std::max(4.0, std::abs(static_cast<double>(pos)) * 0.01);
            ASSERT_NEAR(neg, -pos, eps);
        }
    }
}

TEST(SoftClipper_OutputNeverExceedsQ15Range)
{
    SoftClipper clipper;
    clipper.Init(44000.0f);
    for (q15_t drive : {q15_t(0), q15_t(Q15_MAX / 2), q15_t(Q15_MAX)})
    {
        clipper.SetDrive(drive);
        for (q15_t input : {Q15_MIN, q15_t(-1), q15_t(0), q15_t(1), Q15_MAX})
        {
            q15_t out = clipper.Process(input);
            ASSERT_TRUE(out >= Q15_MIN && out <= Q15_MAX);
        }
    }
}

TEST(SoftClipper_VolumeCompensationCanBeDisabled)
{
    SoftClipper clipper;
    clipper.Init(44000.0f);
    clipper.SetDrive(Q15_MAX);
    clipper.DisableVolumeCompensation(true);
    q15_t uncompensated = clipper.Process(10000);

    SoftClipper clipper2;
    clipper2.Init(44000.0f);
    clipper2.SetDrive(Q15_MAX);
    // compensation is on by default after Init()
    q15_t compensated = clipper2.Process(10000);

    // At max drive, volume compensation roughly halves the output — the two
    // should differ noticeably, not just by rounding.
    ASSERT_TRUE(uncompensated > compensated);
}

TEST(SoftClipper_DriveGetterMatchesSetter)
{
    SoftClipper clipper;
    clipper.Init(44000.0f);
    clipper.SetDrive(12345);
    ASSERT_EQ(clipper.GetDrive(), 12345);
}
