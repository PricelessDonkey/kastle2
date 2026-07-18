# DSP test suite

Host-native unit tests for `code/src/common/dsp/` — the DSP module library
shared by every Kastle 2 firmware (Wave Bard included, and every custom
firmware built on top of it). No RP2040, no flashing, no pico-sdk: this
compiles with your Mac's system compiler and runs in well under a second.

v1 covers the modules Wave Bard actually uses today, as a known-good
baseline to diff future changes against: `AdsrEnv`, `Quantizer`,
`SoftClipper`, `Slewer`, `Oscillator`. Since then, `OscillatorQ15` and
`WhiteNoise` have been added (via the noise-blend prototype below).

## Running

```
./run.sh              # zero dependencies beyond a C++23 compiler
```

or, if you have `cmake` installed (needed anyway for the real RP2040 build,
see ../TOOLCHAIN_INSTALL.md):

```
cmake -S . -B build && cmake --build build && ./build/dsp_tests
```

Both build the same sources the same way — `run.sh` is just the fallback
for when cmake isn't set up yet.

## Adding a test for a new DSP module

1. Check whether the module is host-testable at all. Grep its `.hpp`/`.cpp`
   for `#include`s outside `<...>` standard headers and `common/dsp/...` —
   anything reaching into `pico/`, `hardware/`, or `lib/I2S.hpp` needs a
   shim (see below) or isn't worth testing this way.
2. Add the module's `.cpp` to `DSP_SOURCES` in `CMakeLists.txt` and to the
   source list in `run.sh`.
3. Write `dsp/test_<module>.cpp` using the macros in `harness.hpp`:
   `TEST(Name) { ... }`, `ASSERT_TRUE`, `ASSERT_FALSE`, `ASSERT_EQ`,
   `ASSERT_NEAR(a, b, eps)`.
4. Add the new file to `TEST_SOURCES` in `CMakeLists.txt` (`run.sh` picks up
   `dsp/*.cpp` automatically).

Fixed-point math (`q15_t`/`q31_t`) rarely round-trips exactly — see
`SoftClipper_IsApproximatelyOddSymmetric` in `dsp/test_soft_clipper.cpp` for
the pattern of asserting a property (sign matches, bounded, roughly
symmetric) with a tolerance instead of an exact value, and for how to derive
an expected value by running a second instance of the module rather than
hand-computing fixed-point arithmetic.

## Prototypes (`dsp/prototypes/`)

Not part of the shared DSP library — sketches used to answer an open design
question with measured behavior before committing an approach. Currently:

- **`NoiseBlendCrossfade.hpp`** — CHORD-GEN.md's "Noise blend parameter"
  section left the blend law (linear vs. equal-power crossfade) undecided.
  `dsp/test_noise_blend_crossfade.cpp` blends real `OscillatorQ15` +
  `WhiteNoise` output under both laws and measures RMS across a sweep:
  confirms the linear law's ~3dB power dip at the midpoint is real (not
  just a coefficient-math artifact) and that equal-power avoids it. If
  Sam picks a law, promote the chosen function into
  `common/dsp/synthesis/` for real and delete the other; don't wire this
  header into an app directly.

## Known gaps (not yet covered)

- **`Svf`, `HardClipper`, `DjFilter`** — all reachable from a host build
  (only `Svf.cpp` needs a shim, already provided at
  `shims/hardware/sync.h`), just not tested yet. Good next addition.
- **Anything touching `Hardware.hpp`, ADC reads, or the app's `AudioLoop()`
  directly** — those need the CV/ADC layer mocked out, a bigger lift than
  testing DSP modules in isolation. Out of scope for v1.
- **On-device behavior** (actual codec output, timing under the real ISR)
  — this suite can't see that at all. It only proves the math matches
  expectations; use `code/src/common/testmode/` or real hardware for
  anything below that.

## Shims (`shims/`)

The DSP library is almost entirely platform-independent, but a few files
pull in RP2040-only headers transitively:

| Real header | Why it's needed | What the shim provides |
|---|---|---|
| `common/fastcode.hpp` | `FASTCODE` macro places functions in a RAM-resident linker section — not valid syntax on host object formats | `#define FASTCODE` (no-op) |
| `pico/stdlib.h` | dragged in by `common/config.hpp`, nothing in it is actually used | empty file |
| `I2S.hpp` | `common/config.hpp` reads `I2S::kAudioBufferSize` to define `AUDIO_BUFFER_SIZE` | just that one constant (`48`), matching the real value |
| `hardware/sync.h` | `Svf.cpp` uses `save_and_disable_interrupts()`/`restore_interrupts()` to guard a coefficient update against the audio ISR | no-op stand-ins (host tests are single-threaded) |

`shims/` is listed first in the include path, so these resolve before (and
instead of) anything real. If a new module needs another pico-sdk header,
add a matching stub here rather than pulling in the actual SDK.

## A known upstream quirk this suite surfaced

`Quantizer.cpp` calls `std::max`/`std::min` without including `<algorithm>`.
`arm-none-eabi-g++` tolerates it (something else in that toolchain's
headers pulls it in transitively); host libc++/libstdc++ don't. Both build
scripts force `-include algorithm` to work around it rather than patching
the vendored source — worth a small upstream PR at some point.
