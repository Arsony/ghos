# The Bloodline Collection - Generation 2

Monorepo for the CrushAudio plugins. This drop is **Phase 0 + Phase 1 (G1/G3)** from the Gen 2 design doc:
the shared `Kit`, the GHOST engine with offline tests, a JUCE 8 VST3 shell, and CI.

```
Kit/include/bloodline/   Biquad, Oversampler, Loudness (K-weighting), AudioFeatures, Tokens   (plain C++)
Ghost/Source/            GhostEngine.h (the DSP, no JUCE) + PluginProcessor / PluginEditor (JUCE)
Ghost/tests/             run_engine.cpp - offline runner, wired into ctest
.github/workflows/       dsp-tests (Ubuntu, no JUCE) -> ghost-vst3 (Windows, uploads GHOST.vst3)
Art/                     hero layers, tracked by Git LFS
```

## Drop it in
1. Copy the folders into the repo root (or push as a new repo). If you keep your existing `Bloodline.h`, delete `Kit/include/bloodline/Tokens.h` and point `PluginEditor.cpp` at yours.
2. Push. Actions builds `GHOST-VST3-Windows`; download the artifact and drop `GHOST.vst3` into `C:\Program Files\Common Files\VST3`.
3. Local: `cmake -S . -B build && cmake --build build --config Release --target Ghost_VST3`, tests: `cmake -S . -B build -DBLOODLINE_BUILD_PLUGINS=OFF && cmake --build build && ctest --test-dir build`.
4. Change `GIT_TAG` in the root `CMakeLists.txt` to the JUCE tag your SonyTune / Crush+Lock builds use.

## What was actually verified
The engine was compiled and run offline (g++ 17, 48 kHz). All passed:
null test at MIX 0 (exact, Match on and off), reported latency = real latency (31 samples, identical at 2x and 4x),
block-size independence, mono-sum change 0.000 dB with MIST at 100%, AIR aliasing floor (-104 dB measured at 2x and 4x, target -90),
Match within 0.01 dB of dry loudness, no NaN/Inf on silence / +12 dBFS / noise.
CPU on the sandbox: about 1.8% of a core at 2x, 2.8% at 4x (48 kHz stereo, everything on); your PC will differ.

## What was NOT verified
- **The JUCE plugin shell has never been compiled** (no JUCE in my sandbox). Expect a small compile fix or two on the first Actions run. Paste me the log and I'll fix it.
- **Nothing has been listened to.** Every constant in `tune::` is a starting value. Tweak by ear in FL Studio.
- Not run through pluginval yet; do that at strictness 5+.

## Differences from the design doc (on purpose)
- **Topology:** the dry path is never split. SPIRIT / PRESENCE / AIR are generated from filtered copies and added to a latency-aligned dry, so lows are untouched by construction and there are no Linkwitz-Riley crossovers to phase-compensate. The 250 Hz / 2 kHz / 6 kHz edges appear as each path's filter edges.
- **Equal-power MIX + Match:** MIX is equal-power as specified, and Match then trims the *final* output (within 0.5 dB, clamped +-6 dB) to the dry's K-weighted loudness. This avoids the +3 dB bump equal-power gives on correlated signals.
- **2x AIR drive is gentler than 4x** (3.0 vs 6.0). At 6.0 the 2x aliasing floor measured -65 dB; at 3.0 it clears -90 dB.
- **Extra control:** a Limiter toggle (soft ceiling at -0.3 dBFS), default off, for the Crypt drawer.
- The editor is a placeholder (five knobs in the dock arch and a moon glow driven by the audio features). The cathedral and spirit scene is milestone G2.

## Next
G2 (fog shader + spirit in a standalone test app), then FLASH's warp engine prototype, then VEIL. The browser prototype from the doc's "suggested next step" is still open if you want to judge the look first.
