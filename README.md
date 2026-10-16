# PTZR.FORGE — Tape Saturator (PTZR.PLUGINS)

JUCE 8 / C++17 / CMake. Formats: VST3, AU (macOS), Standalone.

## Build
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release      # or -G Xcode on macOS
cmake --build build --target PTZRForge_VST3 -j8
cmake --build build --target PTZRForge_AU -j8                 # macOS only
```
Add `-DPTZR_COPY_AFTER_BUILD=ON` to copy to `~/Library/Audio/Plug-Ins/{VST3,Components}`.
First configure downloads JUCE 8.0.6 (needs network). On macOS the default build is universal (arm64 + x86_64).
`-DPTZR_ENABLE_MIDI=OFF` removes MIDI input (AU then becomes a plain effect instead of `aumf`).
The bundle file is named `PTZR FORGE` (a dot in the file name breaks JUCE's VST3 manifest step); hosts show `PTZR.FORGE`.

DSP unit tests (no JUCE): `g++ -O2 -std=c++17 -I Source/DSP tests/dsp_test.cpp -o dsp_test && ./dsp_test`

Assets are regenerated from the reference screenshot with `python3 tools/make_assets.py <reference.png>` (needs Pillow, numpy).

## Signal flow (per channel)
input -> HF pre-emphasis (shelf, depth follows DRIVE) -> pre-tone tilt (35 % of TONE) -> **4x/2x oversampled nonlinear stage**
-> de-emphasis -> post-tone tilt (65 % of TONE) -> head-bump + HF roll-off (both follow DRIVE) -> DC blocker
-> wow/flutter modulated delay -> auto-gain -> dry/wet mix (latency-compensated) -> OUTPUT.

Nonlinear stage: pre-gain = DRIVE (0..+28 dB, curved) + INPUT (+-12 dB), then per model:
* TUBE - level-dependent "sag" ahead of a biased tanh (even harmonics). CHARACTER = bias + sag depth.
* TRANSISTOR - transient emphasis ahead of a symmetric curve with adjustable knee hardness (odd harmonics).
* OPTO - two-stage program-dependent compressor (fast + slow release) ahead of a gentle rational curve.
* DIODE - instantaneous asymmetric exponential clipper; CHARACTER = knee steepness/asymmetry.
Switching models crossfades over 20 ms. OUTPUT is applied after the curve, so it never changes saturation.
Latency = oversampler FIR latency + 3 ms (wow centre delay); reported to the host.

## MIDI
CC14 Drive, 15 Tone, 16 Wow/Flutter, 17 Character, 18 Input, 19 Output, 20 Mix, 21 Auto Gain (>=64 = on), 22 Model (4 zones).
Right-click a knob -> MIDI Learn / clear. Program Change n selects preset n (applied on the message thread). Mappings are saved with the session.

## Presets
Six factory presets (named as specified) + user presets (SAVE button) stored as XML in
`~/Library/Application Support/PTZR/FORGE/Presets` (macOS). Factory values were set by calculation and measurement, **not by listening**.
