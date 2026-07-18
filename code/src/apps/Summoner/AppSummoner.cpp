#include "AppSummoner.hpp"
#include "common/core/Kastle2.hpp"
#include "common/utils.hpp"

using namespace kastle2;

void AppSummoner::Init()
{
    // App owns ENV/CV/GATE outputs; Base keeps LFO, SYNC, clock and audio chain
    Kastle2::base.SetFeatureEnabled(Base::Feature::ENV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::CV_OUT, false);
    Kastle2::base.SetFeatureEnabled(Base::Feature::GATE_OUT, false);
}

void AppSummoner::DeInit()
{
}

FASTCODE void AppSummoner::AudioLoop(q15_t *input, q15_t *output, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        // read
        q15_t left = input[2 * i];
        q15_t right = input[2 * i + 1];

        // code that runs each sample

        // output
        output[2 * i] = left;
        output[2 * i + 1] = right;
    }
}

void AppSummoner::UiLoop()
{
    Kastle2::hw.SetLed(Hardware::Led::LED_1, WS2812::GREEN);
    Kastle2::hw.SetLed(Hardware::Led::LED_2, WS2812::BLUE);
}
