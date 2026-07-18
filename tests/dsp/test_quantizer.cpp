#include "../harness.hpp"
#include "common/dsp/utility/Quantizer.hpp"

using namespace kastle2;

TEST(Quantizer_DisabledByDefaultAfterInit_PassesFrequencyThrough)
{
    Quantizer q;
    q.Init();
    ASSERT_FALSE(q.IsEnabled());
    ASSERT_EQ(q.Process(277.18f), 277.18f);
}

TEST(Quantizer_MajorScaleSnapsOutOfScaleNoteUpwardOnTie)
{
    // C major = {C, D, E, F, G, A, B} = semitone bits {0,2,4,5,7,9,11}.
    // C#4 (277.18 Hz, MIDI 61) isn't in the scale. Naively "nearest scale
    // tone" would pick C4 (261.63, 15.55 Hz away) over D4 (293.66, 16.48 Hz
    // away) — but the search in Quantizer::Process() checks upward before
    // downward at each radius, so it returns D4 instead. This is current,
    // real behavior worth locking in before it's reused in new firmware.
    Quantizer q;
    q.Init();
    q.SetEnabled(true);
    q.SetScale(Quantizer::DefaultScale::MAJOR_DIATONIC);

    float result = q.Process(277.18f);
    ASSERT_NEAR(result, 293.66f, 0.01);
}

TEST(Quantizer_MajorScaleLeavesInScaleNoteUnchanged)
{
    Quantizer q;
    q.Init();
    q.SetEnabled(true);
    q.SetScale(Quantizer::DefaultScale::MAJOR_DIATONIC);

    // C4 (261.63 Hz, MIDI 60) is in C major already.
    float result = q.Process(261.63f);
    ASSERT_NEAR(result, 261.63f, 0.01);
}

TEST(Quantizer_SetRootHasNoEffectOnProcess)
{
    // Quantizer::Process(float) computes its scale-membership mask as
    // `1 << (closest_index % 12)` — it never reads scale_root_offset_.
    // SetRoot() only affects ProcessMultiplier(). The header comment says
    // as much ("doesn't transpose the output frequency") but doesn't
    // mention Process() ignores it outright — worth pinning down.
    Quantizer q;
    q.Init();
    q.SetEnabled(true);
    q.SetScale(Quantizer::DefaultScale::MAJOR_DIATONIC);

    float without_root = q.Process(277.18f);

    Quantizer q2;
    q2.Init();
    q2.SetEnabled(true);
    q2.SetScale(Quantizer::DefaultScale::MAJOR_DIATONIC);
    q2.SetRoot(Quantizer::ScaleRoot::D);
    float with_root = q2.Process(277.18f);

    ASSERT_NEAR(without_root, with_root, 0.001);
}

TEST(Quantizer_SetRootShiftsProcessMultiplierScale)
{
    // Unlike Process(), ProcessMultiplier() DOES apply scale_root_offset_.
    // MAJOR_CHORD = {C, E, G} (bits 0,4,7). With root=D that's transposed to
    // {D, F#, A} (bits 2,6,9). Multiplier 1.0 == C (kMultiplierTable[0]),
    // not in the D-major-chord scale, so it should resolve to the nearest
    // in-scale multiplier — D, kMultiplierTable[2] (1.12246).
    Quantizer q;
    q.Init();
    q.SetEnabled(true);
    q.SetScale(Quantizer::DefaultScale::MAJOR_CHORD);
    q.SetRoot(Quantizer::ScaleRoot::D);

    float result = q.ProcessMultiplier(1.0f);
    ASSERT_NEAR(result, 1.12246f, 0.0001);
}

TEST(Quantizer_DisabledProcessMultiplierPassesThrough)
{
    Quantizer q;
    q.Init();
    ASSERT_EQ(q.ProcessMultiplier(1.5f), 1.5f);
}

TEST(Quantizer_ScaleSelectionIgnoresOutOfRangeIndex)
{
    Quantizer q;
    q.Init();
    size_t original = q.GetScale();
    q.SetScale(q.GetScaleTableSize() + 100); // out of range — should be a no-op
    ASSERT_EQ(q.GetScale(), original);
}
