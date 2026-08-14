/*
MIT License

Copyright (c) 2024 Vaclav Mach (Bastl Instruments)

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

#include "common/dsp/effects/BitCrusher.hpp"

namespace kastle2
{

// Defined out of line (Summoner Phase 8, 2026-08-13): the FASTCODE section
// attribute on an in-class inline definition produces a COMDAT .fastcode
// section that conflicts with a FASTCODE app function (e.g. AudioLoop) in the
// same translation unit — GCC "section type conflict". Every other effect
// (SoftClipper, HardClipper, StereoDelay) already declares FASTCODE in the
// header and defines it here for this reason; BitCrusher was the lone inline
// exception and could not be used by any FASTCODE app until now.
FASTCODE q15_t BitCrusher::Process(q15_t sample)
{
    counter_ += sample_rate_;
    if (counter_ >= Q15_MAX)
    {
        counter_ -= Q15_MAX;

        int32_t shift_up = shift_;

        // Loudness compensation, so it doesn't blow up the volume too much when in lowest bit depths
        if (shift_ > 12)
        {
            shift_up -= 1;
        }
        if (shift_ > 13)
        {
            shift_up -= 1;
        }

        last_sample_ = next_sample_;
        next_sample_ = (sample >> shift_) << shift_up;
    }

    // Linear interpolation between last_sample_ and next_sample_
    return q15_add(last_sample_, q15_mult((next_sample_ - last_sample_), counter_));
}

} // namespace kastle2
