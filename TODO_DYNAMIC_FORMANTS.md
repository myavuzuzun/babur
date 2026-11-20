# Dynamic Formant & Memory-Safe Vocal Tract Implementation To-Do List

## Memory-Safety & Zero-Leak Architecture Guarantees
1. **Zero Heap Allocations in DSP (`render()`)**:
   - No `new`, `malloc`, or dynamic container resizing inside the audio callback.
   - All new filters (`singerFormantSvf`, `vocalCeilingSvf`) and dynamic servo arrays (`formantEnv`, `dynFormantGain`, `formantPeakBoost`) are fixed-size value members inside `Fraudize::BaburEngine` (`src/dsp/BaburDSP.hpp`), automatically destroyed with the engine instance.
2. **Deterministic Initialization, Reset & Numerical Safety**:
   - Explicit zero/unity initialization in constructor and `BaburEngine::reset()`.
   - Positive denominator floor (`1.5e-4f`) and `std::clamp` bounds (`[0.45f, 3.60f]` gain servo, `[3.2f, 18.0f]` `Q`) guarded by `ScopedDenormalsZero` (`FTZ/DAZ`).
3. **RAII Handle Management**:
   - Existing `tsf_close(sfHandle)` lifetime remains guarded under `sourceMutex` in destructor, `unloadSource()`, and `loadSourceFile()`.

---

## Implementation Checklist

- [x] **Step 1 — Extend `StereoSVF` & Add Fixed-Size State Members (`src/dsp/BaburDSP.hpp`)**
  - [x] `1.1`: Add `processBandpassAndLowpass(...)` and `processLowpass(...)` to `BaburEngine::StereoSVF` reusing existing `v1` and `v2` states (zero extra memory).
  - [x] `1.2`: Add fixed value members `singerFormantSvf`, `vocalCeilingSvf`, `formantEnv`, `dynFormantGain`, and `formantPeakBoost` to `BaburEngine`.

- [x] **Step 2 — Initialize & Reset Fixed States (`src/dsp/BaburDSP.cpp`)**
  - [x] `2.1`: Initialize `singerFormantSvf` (`3050 Hz`, `Q=6.5`) and `vocalCeilingSvf` (`4750 Hz`, `Q=0.72`) inside `updateFilterBanks(float sr)`.
  - [x] `2.2`: Reset `singerFormantSvf`, `vocalCeilingSvf`, `formantEnv`, `dynFormantGain`, and `formantPeakBoost` inside `BaburEngine::reset()`.

- [x] **Step 3 — Pitch-Locked Sinusoidal Harmonic Gravity & Dynamic Bandwidths (`src/dsp/BaburDSP.cpp`)**
  - [x] `3.1`: Compute instantaneous fundamental frequency $F_0(t)$ per 16-sample substep.
  - [x] `3.2`: Compute articulatory voicing openness $v(t) \in [0, 1]$ (0 during consonant bursts and whisper, 1 in voiced vowels).
  - [x] `3.3`: Apply $C^\infty$ continuous sinusoidal harmonic gravity ($\gamma = \{0.28, 0.22, 0.10, 0.0\}$) to eliminate pitch-glide/vibrato zipper jumps.
  - [x] `3.4`: Compute dynamic bandwidths $B_i(t)$ from physical Hz targets (`62, 88, 125, 165 Hz`), consonant burst widening (`2.4x`), pitch-adaptive floor (`0.55 * F0`), and Gaussian proximity damping ($\sigma = 320\text{ Hz}$).
  - [x] `3.5`: Update `singFormantSvf`, `cascadeF1Svf`, `singerFormantSvf`, `vocalCeilingSvf`, and fractional-$Q$ `formantPeakBoost` ($k^{0.58}$).

- [x] **Step 4 — Cascade-$F_1$ Coupling, Closed-Loop Gain Servo & Vocal Ceiling (`src/dsp/BaburDSP.cpp`)**
  - [x] `4.1`: Generate phase-aligned excitation `cascExcite` through `cascadeF1Svf` (`52%` direct + `48%` cascaded $F_1$).
  - [x] `4.2`: Feed `singFormantSvf[0..3]` scaled by `formantPeakBoost` and track per-formant output envelopes `formantEnv[0..3]`.
  - [x] `4.3`: Apply asymmetric closed-loop dynamic gain servo ($\tau_{\text{down}}=3\text{ ms}$, $\tau_{\text{up}}=24\text{ ms}$, frozen toward $1.0$ on silence/bursts) bounded to `[0.45, 3.60]`.
  - [x] `4.4`: Blend Piriform / Singer's Formant ring (`~3.05 kHz`) and pass the voiced output through `vocalCeilingSvf` (`~4.75 kHz` 2-pole lowpass) before mixing consonant bursts.

- [x] **Step 5 — Stress/Leak Testing, Verification & Documentation (`test_babur_dsp.cpp`, `HOW_IT_WORKS.md`, `build.ps1`)**
  - [x] `5.1`: Add `[13/13] testDynamicFormantServoAndMemoryStability()` testing multi-pitch dark/bright carriers and 20x construct/load/render/reset memory & stability stress loops.
  - [x] `5.2`: Update `HOW_IT_WORKS.md` (English & Turkish) with the Dynamic Formant Servo, Sinusoidal Gravity, Singer's Formant, and Vocal Ceiling details.
  - [x] `5.3`: Run `powershell -ExecutionPolicy Bypass -File .\build.ps1` and verify all 13 DSP unit tests, VeSTige tests, and VST3 tests pass with exit code `0`.
