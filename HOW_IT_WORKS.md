# babur — Technical Architecture, Code Map & DSP Internals (English / Türkçe)

**babur** is a zero-dependency C++17 monophonic singing and speaking instrument plugin for Windows x64 (**VST3**, legacy `.dll` via open-source **VeSTige**, and Standalone Direct2D) that turns any loaded SoundFont (`.sf2`) preset or audio sample (`.wav`, `.mp3`, etc.) into the vocal tract itself—no external pitch-tracking carrier and no pitched synthetic oscillator layered over the instrument.

> Navigation:
> - [Quick Code & Symbol Map (Exact File & Line Index)](#quick-code--symbol-map-exact-file--line-index)
> - [Part 1 — English: How babur Works Technically](#part-1--english-how-babur-works-technically)
> - [Bölüm 2 — Türkçe: babur Teknik Olarak Nasıl Çalışır?](#bölüm-2--türkçe-babur-teknik-olarak-nasıl-çalışır)

---

## Quick Code & Symbol Map (Exact File & Line Index)

Use this lookup table to jump directly to the implementation without scanning unrelated files:

| Subsystem | File | Key Symbols & Line Ranges |
| :--- | :--- | :--- |
| **VST Parameters & Mode Enums** | `src/dsp/BaburDSP.hpp` | `BaburParam` (`L22-L42`, `NUM_PARAMS = 18`: `0..7` core + `8..17` voice characteristics `PARAM_GENDER`..`PARAM_ROBOTIC`), `BaburLegatoMode`, `BaburVoiceMode`, `StereoSVF`, `ArticulatorSpring`, `QueuedPhoneme::unreleased`, `BaburEngine` state |
| **SoundFont Loading (`.sf2`)** | `src/dsp/BaburDSP.cpp` | `BaburEngine::loadSourceFile` 64-bit `_wfopen`/`tsf_stream` (`L490-L620`) |
| **RIFF / Empty Preset Hardening** | `include/tsf.h` | `tsf_riffchunk_read` (`L488+`), `tsf_load_presets` (`L720+`), `tsf_voice_envelope_release_samples` 25 ms min (`L1019+`), `tsf_voice_render` (`L1232+`) |
| **WAV / Media Foundation Loader** | `src/dsp/AudioFileLoader.hpp` | `AudioFileLoader::load` (`L91-L110`), `finalizeBuffer` trim/normalize/zero-crossings (`L363-L431`) |
| **Audio Carrier (`A4=69`, LOOP/STRETCH)** | `src/dsp/BaburDSP.cpp` | `renderAudioCarrierSample` Hermite loop + phase-accurate `gr.anchor += pitchRatio` dual-grain Hann stretch + `90 ms` boundary / `160 ms` (`0.80x`) mid-word bridge (`L1325+`) |
| **MIDI Sort, Tempo Cadence & Legato** | `src/dsp/BaburDSP.cpp` | Same-offset `std::stable_sort`, `decodeLegatoMode`, `handleMidiEvent`, `triggerNoteOn` incl. `avgNoteIntervalSec`/`currentTempoScale`, emotion onset pitch scoop & no mid-word carrier kill (`L1120+`), `triggerNoteOff` |
| **Onset Timing, FLOW Pinches & Codas** | `src/dsp/BaburDSP.cpp` | `configureCurrentPhonemeDurations` tempo/emotion-scaled & `unreleased` (`L855+`), `startSyllableSequence` incl. tight `0.58x` mid-word onsets & FLOW pinches (`L960+`), `carrierDecayed` / `reexciteSf2Carrier` / `fireDeferredCarrierAttack` |
| **Filter Init, Reset, Place & Emotions** | `src/dsp/BaburDSP.cpp` | `classifyConsonantPlace` (`L115+`), 23-field physiological `getEmotionProfile` (`L140+` — larynx, glottal Q, whisper blend, `72 Hz` ventricular growl, nasal tears, pitch scoop/droop & vibrato curves), `updateFilterBanks`, `BaburEngine::reset`, `snapArticulatorsForColdStart` |
| **6-Articulator Physics, Anatomy & Drift** | `src/dsp/BaburDSP.cpp` | `render()` 16-sample substep: emotion & voice-characteristic smoothing (`smoothGender..smoothRobotic`), non-uniform `genderFScale[0..3]` (Bass/Giant <-> Female/Child), `smoothNasality` velum opening, live mid-word bridge morphing, `1/f` jitter/shimmer (`+smoothTremor` / stripped by `smoothRobotic`), `F0` gravity + combined `effQScale` (`[2.0..16.0]`), gender-scaled fricative SVFs & 24-band mask |
| **Living Throat Tilt, Growl & Whisper** | `src/dsp/BaburDSP.cpp` | `render()` per-sample `speechLeveler`, gender chest-subharmonic scaling (`genderSubScale`), warm `1650 Hz` LP whisper air (`smoothBreathiness`), `72 Hz` ventricular growl + fold edge (`smoothGrit`), crisp buzz (`smoothRobotic`), 24-band Bark/tilt `SING`, pitch-synced breath sheen & shimmer, `smoothBrightness` + emotion-scaled `vocalCeilingSvf` & post-servo `glottalBright` |
| **Plosive Pop, F1 Cutback & Intra-Word Continuity** | `src/dsp/BaburDSP.cpp` | Mid-word softened oral dips (`w/y/l/r`, nasals, voiced stops), ballistic tap `r` / flap `d`, `1–3 ms` burst pop & `f1CutbackGain` VOT suppression, `PH_VOWEL_CORE` + `phraseBridgeRemaining` (`bridgeIsMidWord`) |
| **4-Pole Formants, `effEnv` Servo & `syllEnv`** | `src/dsp/BaburDSP.cpp` | `singFormantSvf->cascadeFormantSvf` (F1 scaled by `f1CutbackGain`), boosted `1.20x` nasal murmur & `1.45x` voice bar, brightness/emotion-tinted `targetAmp[0..3]` & gender-shifted `singerFormantSvf` / `pharynxSvf`, `dynFormantGain` & `vowelLeveler` continuous through voiced sonorants and held across brief closures & `bridgeIsMidWord`, `nasalZeroSvf`, `vocalCeilingSvf`, post-leveler `smoothSyllEnv` + growl + `smoothTremor` flutter + `smoothRobotic` lock, DC + peak limiter + tanh |
| **Binary State Chunk (`"SNGR"` + `"VOIC"`)** | `src/dsp/BaburDSP.cpp` | `saveState`, `restoreState`: preserves Chunk v1 8-param prefix (`backward-compatible`) + appends optional `'VOIC'` block for params `8..17` |
| **Embedded Sorted EN Lexicon (`396` words)** | `src/dsp/EnglishLexicon.hpp` | `LexWord kEnglishLexicon[]`, `findEnglishLexiconWord` binary search (`O(log N)`) |
| **Emotion Tags, TR Stress, Sandhi & G2P** | `src/dsp/TextPhonemeDissector.hpp` | `EmotionId` (`L27-L34`), `SyllableToken` (`L100+`), `parseEmotionTag` (`L200+`), `dissectText` (`L230+`), `applyTurkishGrammaticalStress` (`L559+`), `applyWordContext` (`L618+`), `applySentenceContext` (`L794+`), `tryLookupEnglishBase` + Lexicon (`L1598+`) |
| **Direct2D 3-Tab UI & Pill Emotion Bars** | `src/ui/BaburUI.hpp` | 3-tab (`MAIN` \| `VOICE` \| `STYLE`) bottom control card (`drawControlTabs`), `openLegatoModeMenu`, `openVoiceModeMenu`, pill `emotion != EMO_NEUTRAL` accent bar & `isEmphasized` highlight |
| **Plugin Wrappers & Automated Tests** | `src/vestige/BaburVestige.cpp`, `src/vst3/BaburVST3.cpp`, `test_babur_dsp.cpp` | Open-source VeSTige legacy `.dll` adapter (`18` params, ID `'BABR'`), GPLv3 VST3 (`18` `BaburParameter`s), `[1/7]`..`[22/22]` DSP tests (`build.ps1`; `[22/22]` = 10 Voice Characteristics & Legacy Chunk v1 Compat) |

---

# Part 1 — English: How babur Works Technically

## 1. Architecture & Threading Model

All three build targets—VeSTige legacy `.dll` (`dist/babur.dll`, ID `'BABR'`), VST3 (`dist/babur.vst3`), and Standalone (`bin/babur_gui.exe`)—wrap a single memory-safe engine (`Fraudize::BaburEngine`) and Direct2D/DirectWrite dark-slate UI (`460 × 436 px`).

- **Zero Audio-Thread Heap Allocation**: All filter banks (`singFormantSvf[4]`, `cascadeFormantSvf[4]`, `pharynxSvf`, `singerFormantSvf`, `vocalCeilingSvf`, `vocoderCarSvf[24]`, consonant SVFs), envelope detectors, and scratch buffers are fixed-size value members directly inside `BaburEngine`. No `new`/`malloc` or vector growth happens in `render()`.
- **Locking & Numerics**: `render()` runs under `ScopedDenormalsZero` (`FTZ/DAZ`), swaps `pendingMidi` under `midiMutex`, uses `try_to_lock` on `sourceMutex` so background UI file loads never block audio, processes `BLOCK_CHUNK = 64` sample blocks, and updates vocal-tract parameters every `FORMANT_SUBSTEP = 16` samples.

```text
MIDI Events + Lyrics Text
          │
          ▼
[ TextPhonemeDissector ]
  ├──► EN G2P (Lexicon, Morphology, Open/Closed Syllable Rules, W-Coloring)
  ├──► TR Phonetics (8 Vowels, k/g/l Harmony, Open e, ğ Vowel Melt, Tapped r)
  ├──► applyWordContext() ──► Word Stress + Onset/Coda/V-to-V + ğ Bridging
  └──► applySentenceContext() ──► Cross-Word Sandhi (n+b->m, flapping, d+y->j, the->thee) + Punctuation Prosody & Function Words
          │
          ▼
[ Monophonic Stack, Same-Offset NoteOff Sort & 3-Mode LEGATO (FLOW | STAC | HOLD [default]) ]
  ├──► 6-Articulator Gestural Physics (Jaw, Tongue Body X/Y, Tongue Tip, Lips, Lip Closure, Velum, Velar Closure)
  ├──► Anticipatory Lookahead into upcomingSyllable + Overlapping Muscle Springs + Lip/Tongue-Coupled Fricatives
  └──► Deferred Coda (78-88% sibilant vowel muting) + 90 ms Legato Bridge
          │
          ├──► Phrase Carrier (SF2 re-triggers if decayed <= 0.005 or cold; overlapping legato glides smoothly)
          │         │
          │         ▼
          │    Pre-Tract Speech Sustainer (speechLeveler [0.75..2.80]) + Lip Radiation + Half-Period Sub-Harmonic Divider
          │         │
          │         ▼
          │    Voice Mode Shaping (NORM / AGGR / WHISP / RAW) ── zero internal pitch oscillators
          │         │
          │         ▼
          │    24-Band Bark SVF Bank (140..6400 Hz, Q=4.2) + Per-Band AGC (up to 24x)
          │         ├──► SING: 4-Pole Cascaded Formants (Q 3.2..11.5, sinusoidal F0 gravity)
          │         │            → effEnv-Anchored Servo [0.30, 2.60] → Gentle Peak Boost (pow 0.24)
          │         └──► TALKBOX: 24-Band Analytic Lorentzian Mask (maxPeak^1.35, 0.008 valley floor)
          │                   │
          │                   ▼  crossfade via PARAM_CHARACTER + balanced Pharynx & Singer's Formant
          │         Dynamic Mouth-Opening Lowpass (270->4850 Hz) → Vowel-Core-Gated vowelLeveler
          │                   │
          └──────────► [ + Instrument-Grain-Modulated Consonants (78% direct >=2.6 kHz sibilants / 38% otherwise) ]
                              │
                              ▼
                       DC Blocker (0.9975 pole) → Peak Limiter (0.85) → fastTanh → Stereo Output
```

---

## 2. Bilingual Dissector & Word-Contextual Coarticulation (`TextPhonemeDissector.hpp`)

When lyrics change (`setText`) or `LANG` switches between `EN` and `TR`, `dissectText` converts UTF-8 words off the audio thread:

### English G2P Rules (`dissectEnglishWord`, `mapEnglishVowelGroup`)
1. **Lexicon, Compound Decomposition & Morphology**: High-frequency irregular words (`die, dye, mother, motherfucker, money, love, come, done, under`, etc.) resolve via `tryLookupEnglishBase` + suffix inflection rules (`-s/-es/-ies`, `-ed`, `-ing`, `-er`, `-tion/-sion`, `-ture`). Unlisted compound words (`>= 6` chars, `split >= 3`) decompose via `lookupRightPart` so stem stress and vowel quality transfer cleanly.
2. **Context-Sensitive Orthography & Vowel Quality Rules**:
   - **Open `/ʌ/` (`STRUT`) & relaxed `-er` (`/ɚ/`)**: Short stressed `'u'` (`cut, duck, luck, drum, just, sun`), `'o'` before `th/v` at any syllable index (`mother, stepmother, brother, cover, discover`), and lexicon `/ʌ/` words map to open `V_A` (`/ʌ/`) instead of tense `V_I_DOTLESS` (`ı`) or `/oʊ/`; unstressed final `-er, -or, -ar` and medial `-ther` (`"dh"`) map to relaxed `V_I_DOTLESS + r` instead of rounded `V_OE` (`ö`).
   - **Word-final `-ie/-ye` & `-y`**: `"die"`, `"dye"`, `"lie"`, `"tie"`, `"cry"`, `"my"` map to diphthong `/aɪ/` (`V_A -> V_I`), while multisyllabic final `-y` (`"happy"`, `"melody"`) maps to `/i/`.
   - **`W`-coloring on `'a'`**: Post-`w`/`qu` syllables (`want, watch, wash, what, swap`) shift to open back `/ɑ/` (`V_A`), or `/ɔ/` before `r` (`war, warm`).
   - **Open Stressed First Syllables & Reduced Endings**: Open first syllables (`baby, paper, silent, tiger, moment, music`) take tense/diphthong nuclei; final `-o` takes `/oʊ/`, final multisyllabic `-a` and unstressed `-en/-el/-on/-an/-ment/-ant/-ent` reduce to `V_I_DOTLESS`.
   - **Vowel Teams & Contexts**: Digraphs (`ee/ea/ie`, `oo/ew/ue`, `ou/ow`, `ai/ay/ey`, `oi/oy`, `au/aw`, `oa`), silent trailing `e`, `-igh`, `-ind/-ild`, `-old/-ost/-oll`, `-all/-alk`, `-aught/-ought`, and soft `c`/`g`.

### Turkish Phonetic Engine (`dissectTurkishWord`, `collectTurkishCluster`)
1. **UTF-8 & 1-to-1 Vowel Targets**: Maps `a, e, ı, i, o, ö, u, ü` and consonants `ç, ğ, ş` cleanly (handling dotted `İ`/`I`).
2. **Vowel-First Syllable Splitting & `ğ` Bridging**: Segments words into Turkish `V/VC/CV/CVC/CVCC` structures; softens intervocalic `ğ` (`hasSoftGLink`) with a gentle `"y"` glide across front vowels (*"değil, eğer"*), front offglide on front coda `ğ` (*"eğlen, öğren"*), or `42%` smooth F1–F3 formant melt across back vowels (*"ağaç, soğuk"*).
3. **Vowel-Harmony Allophones, Open `e` & Non-Retroflex Tapped `r`**: Palatalizes `k` (`2050 Hz` front vs `1150 Hz` back), `g` (`2000 Hz` vs `1120 Hz`), and `l` (`1920 Hz` vs `960 Hz`), shifts `e` before coda `r, l, n, m` toward open `/æ/` (`645 / 1780 Hz`, *"gel, ben, sen, yer"*), and models `r` as a non-retroflex alveolar tap (`locusF3 = 2180 Hz`, gentle `2100 Hz` pull + subtle devoiced word-final release).

### Word & Sentence Context Passes (`applyWordContext`, `applySentenceContext`)
Before emitting syllables, `applyWordContext` shapes intra-word coarticulation and `applySentenceContext` models whole-phrase cross-word phonology and prosody:
- **Boundaries & Stress (`isWordStart`, `isWordEnd`, `isStressed`)**: Detects weak prefixes and unstressed syllables (`18%` EN / `10%` TR centralization toward neutral `500/1480/2480 Hz`).
- **Onset, Coda & V-to-V Coarticulation**: Pulls entry/exit formants toward onset (`14–22%`), dark `l` (`36%`), nasals (`20–24%`), English retroflex `r` (`1620 Hz`), and adjacent vowels (`12–18%`), plus `11%/9%` cross-word F1–F2 V-to-V blending and `0.89x`/`1.06x` cross-word coda spectral anticipation.
- **Cross-Word Sandhi & Assimilation**: Both EN & TR assimilate `n + p/b/m -> m` (*"on bir"*) and `n + k/g -> ng`; EN adds `"the"` $\to$ `/ði/` before vowels (*"the apple"*), intervocalic `t` flapping (*"what a"*), and `d/t/s + y -> j/ch/sh` palatalization (*"did you"*, *"got you"*, *"miss you"*); TR softens intervocalic word-final consonants (`0.75x/0.78x`) and reduces `"bir"` before consonants.
- **Function-Word Reduction, Emotion Tags (`[happy] [sad] [angry] [soft] [excited]`), Emphasis & Turkish Grammatical Stress**:
  - `EnglishLexicon.hpp` embeds **396 sorted irregular/complex English words** (`though, through, thought, enough, laugh, heart, heard, women, choir, island, chaos`, etc.) with O(log N) binary lookup and explicit stressed-syllable indices.
  - `applyTurkishGrammaticalStress` handles Turkish non-final lexical stress (`türkiye, istanbul, şimdi, nasıl, sinema...`), progressive `-yor` / `-yo+r` (skipping negative `-mi-/-mı-` before `-yor`), negative verbs `-ma/-me`, and sentence-level `mi/mı/mu/mü` question stress shift.
  - Inline `[emotion]` tags (EN & Turkish aliases `[mutlu]`, `[üzgün]`, `[kızgın]`, `[yumuşak]`, `[heyecanlı]`, `[nötr]`) switch `EmotionId` seamlessly mid-lyrics without emitting syllables; `*word*` emphasis marks boost `prominence = 1.26`, expand formants `10%`, and add `+0.20 st` inflection; UI pills draw bottom color accents and highlights.
  - Punctuation `, ; : . ! ?` and `>= 3`-word phrases reduce weak function words and apply subtle pitch endings that settle cleanly onto exact MIDI pitch on long held notes (`> 0.25 s`).

---

## 3. Carriers (`include/tsf.h`, `AudioFileLoader.hpp`, `BaburDSP.cpp`)

- **SoundFont (`.sf2`)**: Streams files via 64-bit `_wfopen` / `_fseeki64` callbacks (`tsf_stream`) with `tsf_load_filename` fallback—eliminating the `256 MB` memory ceiling (`MS_Basic.sf2` ~`489 MB` / `309` presets loads in `~240–350 ms`). `tsf.h` safely skips 1-byte odd RIFF padding, empty `0`-item chunks, and zero-region presets (auto-selecting the first preset with `regionNum > 0` and prefixing `[B001]` bank numbers). Pitch bend range is `±24 st` via `tsf_channel_set_tuning`.
- **WAV / Media Foundation Audio (`Root = A4 / MIDI 69 = 440 Hz`)**: Decodes PCM/float WAVs directly and `.mp3` / compressed files via Windows Media Foundation; trims silence (`< 1e-4f`), peak-normalizes to `0.85f`, and finds positive zero-crossings in `15%..85%`. Supports:
  - `LOOP (0)`: 4-point Hermite cubic interpolation with equal-power sine/cosine loop crossfading (`up to 1024 frames`).
  - `STRETCH (1)`: Dual overlapping `2048`-frame Hann-windowed granular synthesis (`180°` out of phase) scrubbing back and forth across the loop to decouple pitch transposition from speed.

---

## 4. Monophonic MIDI, Beat-Synchronized Phoneme Queue & Sibilant Coda Articulation

- **Same-Sample-Offset MIDI Ordering & 3-Mode `LEGATO` (`FLOW | STAC | HOLD`, Default `HOLD`)**:
  - `render()` runs `std::stable_sort` on `renderMidiScratch` ordering by sample `offset` ascending and forcing `NoteOff` (`0x80` / `0x90 vel=0`) strictly **before** `NoteOn` at identical sample offsets—ensuring touching DAW piano-roll notes (e.g. LMMS) advance syllables cleanly without false overlap locks.
  - `PARAM_LEGATO` defaults to **`1.00` (`LEGATO_HOLD`)** so overlapping legato notes hold the active vowel and glide smoothly in pitch, while UI left-click or right-click (`openLegatoModeMenu`) also provides **`FLOW`** (`< 0.35`, every note steps syllables while bridging continuously) and **`STAC`** (`0.35..0.65`, per-note raw retrigger).
- **Legato Carrier Glide & `90 ms` Legato Bridge (`sf2VoiceRunning`, `phraseBridgeRemaining`)**:
  - In `HOLD` / `FLOW`, `triggerNoteOn()` preserves the running SF2 voice (`<= 14 st` glide via `tsf_channel_set_tuning`) only while it is **not decayed** (`carrierDecayed()`: `sf2VoiceRunning`, `carrierEnv >= 0.012` and `>= 25%` of `carrierAttackPeak`, the peak since the last (re)attack). Decayed voices (piano / pluck / one-shot SF2 regions) get a fresh note instead of riding a dying tail (this was the cause of FLOW notes coming out skipped/quiet). Pitch glides with a minimum `16 ms` portamento floor (`0.016f + smoothGlide * 0.210f`) whenever slurring legato, snapping only on cold starts. On unvoiced stops mid-phrase (`keepContinuousCarrier == true`), `delayAttack` stays disabled.
  - **`HOLD` overlap branch** (`!advanceSyllable`): cancels any in-flight release state (`codaCarrierHold`, `diphthongFinishRemaining`, `phraseBridgeRemaining`), re-pushes `PH_VOWEL_CORE` if the tract was idle or in a coda, and calls `reexciteSf2Carrier(key)` when the SF2 voice is decayed or the interval is `> 12 st` (this was the cause of HOLD "just stopping" on decaying presets).
  - **Held-key stack allows duplicates**: a same-pitch overlap (NoteOn B, then NoteOff A on the same key) no longer closes the gate while B is held.
  - **Linking coda, Boundary Pinches & Tempo-Adaptive FLOW**: `triggerNoteOn` tracks the running note-to-note cadence (`avgNoteIntervalSec`, `currentTempoScale` `0.52..1.45`) so fast 16th melodies tighten closures and muscle springs while slow ballads relax; in `PH_VOWEL_CORE` (`beatU = 0.62..1.30`), `preCodaWeight` anticipates the current syllable's own closing coda right before the next beat. At `startSyllableSequence`, touching notes get fricative/affricate links (`0.25x`), **`10 ms` unreleased stop pinches** (`unreleased = true`, no muddy delay), or subtle nasal/liquid pre-gestures (`artVelum`/`artTongueTip`).
- **True Plosive Physics (Pop + F1 Cutback) & Ballistic Tap `r`**: Unvoiced/voiced stops fire a `1–3 ms` burst pressure pop, unvoiced stops apply **VOT F1 cutback** (`f1CutbackGain = 0.18 -> 1.0` on `unit4[0]` while F2/F3 open first), and Turkish tapped `r` / EN flapped `d` execute a crisp `~15 ms` ballistic tongue-tip dip (`sin^1.6`) + release puff.
  - After `NoteOff` and deferred coda completion in non-`STAC` modes, `phraseBridgeRemaining` holds a **`90 ms`** connective bridge (`0.38f` WAV env with smooth `65 ms` step-down release, `0.68f` oral gate, zero pre-note vowel morphing).
- **Fast Linking Final Coda & Sibilant `-s` Clarity (`startSyllableSequence`, `configureCurrentPhonemeDurations`, `render`)**:
  - When touching/connected notes cut off the previous syllable before its final consonant plays, `startSyllableSequence()` blends `smoothF[0..2]` (`28%`) AND appends the final non-geminate coda consonant (`durationScale = 0.35f`, capped `<= 8 ms` closure / `<= 20 ms` burst / `<= 6 ms` trans) so word-final `-s, -ş, -z, -k, -t` speaks cleanly at note transitions without pushing the next vowel off beat. Normal `NoteOff` codas cap at `<= 18 ms` closure / `<= 48 ms` burst / `<= 20 ms` trans, and codas after diphthongs wait only `25 ms`.
  - Unvoiced fricatives (`s, ş, f, th`) suppress the pitched vowel by `78–88%` (`1 - clamp(smoothDiction * sibilantClose * 1.25, 0, 0.92)`) including transition decay, retain `78%` direct un-attenuated high-frequency noise whenever `noiseFreq1Hz >= 2600 Hz` (`1.25x` coda scale), and bypass `vowelLeveler` (`inVowelCorePhoneme == false`) so `-s` sibilants aren't drowned out.
- **Cluster-Normalized Onsets & Diphthong Sustain**: Multi-consonant onset clusters scale by `1/N` (`<= 16 ms` closure, `<= 26 ms` burst, `<= 18 ms` pre-vowel transition) while `onsetCarrySamples` (`28..53 ms`) completes quadratic locus/nasal carryover inside early `PH_VOWEL_CORE`. Diphthongs sustain at `68%` cap while held and finish `-> 100%` on `NoteOff`.

---

## 5. Dual-Stage Levelers, `effEnv`-Anchored Formant Servo & Vocal Tract Filtering

1. **Pre-Tract Speech Sustainer (`speechLeveler`)**: Gentle sustain leveling `clamp(0.24 / (carrierEnv + 0.035), 0.75, 2.80)` (`τ_up = 28 ms`, `τ_down = 6 ms`), relaxing to `1.0` across `90 ms` bridges and releases.
2. **Instrument Excitation & Half-Period Sub-Harmonic Gap Filler (`subOctSignL/R`)**: `lvl - 0.72 * prev` lip radiation + zero-crossing $(n + 0.5)F_0$ sub-harmonic fill scaled by `effEnv` (`0` when silent), selectable via `PARAM_EXCITER` (`RAW`, `WHISPER`, `AGGR`, `NORMAL`).
3. **Hybrid Phase-Coherent `SING` Excitation (`singTiltSvf`) + 24-Band Bark Whitening**: `TALKBOX` uses 24-band Bark whitening; `SING` blends `58%` 3-zone tilt (`1350 Hz`) + `42%` Bark contour with balanced weights (`a0..a3`).
4. **6-Articulator Gestural Physics, Pitch-Adaptive Formants & Gravity**: Replaces rigid per-phoneme F1–F4 snaps with critically-damped 2nd-order `ArticulatorSpring` muscles (`artJaw`, `artTongueBodyX`, `artTongueBodyY`, `artTongueTip`, `artLips`, `artLipClose`, `artVelum`, `artVelarClose`, `artF3`, `artF4`, pre-positioned on cold starts via `snapArticulatorsForColdStart` and smoothly overlapping mid-phrase):
   - Place-specific gestures grab only the articulators a consonant owns (e.g., `/s, t, d, n, l/` grab `artTongueTip` while leaving `artLips` free to pre-round on `"su"`; `/p, b, m/` grab `artLipClose` leaving tongue body free), with anticipatory lookahead (`> 65 ms` vowel hold) into `upcomingSyllable`'s lip rounding, velum opening, and tongue body target.
   - Reconstructs `smoothF[0..3]` dynamically, shifts consonant fricative noise SVFs (`consonantNoiseSvf1/2`) via `artLips` (`-16%` when rounded) and `artTongueBodyX` (`+8%` when front), drives `targetNasalMix` via `artVelum.pos`, scales transition emphasis (`transPresence`) from true physical `|artTongueBodyX.vel|`, and applies pitch-adaptive gravity (`Q = 3.2..11.5`, `pow(effQ/4.5, 0.24)`).
5. **4-Pole Cascaded Formants, `effEnv`-Anchored Servo & Vowel-Core-Gated `vowelLeveler`**: Anchors closed-loop servo (`[0.30, 2.60]`, `9 ms` down / `30 ms` up — slower than one pitch period so it no longer amplitude-modulates the waveform, which was audible as distortion on some vowels) to `effEnv * 0.22` with balanced `targetAmp`/`singerFormantSvf`, applies `nasalZeroSvf` (`/m, n, ng, l/`) + `vocalCeilingSvf` (`270 → 4850 Hz`), and equalizes voiced loudness via `vowelLeveler` (`[0.36, 2.60]`) **only during `PH_VOWEL_CORE`** (`inVowelCorePhoneme && smoothOralGate > 0.25`). `fireDeferredCarrierAttack()` only re-seeds `vowelLeveler` from silence (`carrierEnv < 0.02`) to avoid gain-step clicks. Output: DC blocker → **safety peak limiter** (`0.5 ms` attack / `120 ms` release, ceiling `0.85`, `outPeakEnv`/`outLimGain`) → `fastTanh`, so loud vowels are turned down instead of squared off.
6. **Click hardening in `include/tsf.h`**: `tsf_voice_envelope_release_samples` enforces a `25 ms` minimum release, so SF2 retriggers / legato re-excites never hard-cut a voice.
7. **Living Throat Micro-Dynamics & Emotion DSP (`getEmotionProfile`)**: Dynamic glottal spectral tilt (`glottalBright`: dark `-0.16` onset $\to$ bright `+0.06` vowel bloom, `emoTilt`, prominence & velocity scaling), emotion pressed-glottis edge (`emoEdgeDrive`), pitch-synchronous vocal-tract breath sheen mixed BEFORE the 4-pole formants (strictly `0` when `carrierEnv == 0`), organic `1/f` jitter/shimmer random walk, and a post-`vowelLeveler` living syllabic bell envelope (`smoothSyllEnv`).

---

## 6. Parameters & Binary State Layout (`"SNGR"` Chunk v1)

| Index | Enum | UI Label | Default | Mapping |
| :---: | :--- | :--- | :---: | :--- |
| `0` | `PARAM_CHARACTER` | `CHARACTER` | `0.35` | `SING (0.0)` ↔ `TALKBOX (1.0)` |
| `1` | `PARAM_DICTION` | `DICTION` | `0.65` | `0..100%` consonant strength, closure depth, burst duration & attack delay |
| `2` | `PARAM_THROAT` | `THROAT` | `0.50` | `-12.0 st` (`0.0`) .. `+12.0 st` (`1.0`) formant shift |
| `3` | `PARAM_GLIDE` | `GLIDE` | `0.25` | `8 .. 220 ms` portamento & formant transition speed |
| `4` | `PARAM_LANG` | `LANG` | `0.00` | `< 0.5`: `EN` / `>= 0.5`: `TR` |
| `5` | `PARAM_LEGATO` | `LEGATO` | `1.00` | `< 0.35`: `FLOW`, `0.35..0.65`: `STAC`, `>= 0.65`: `HOLD` |
| `6` | `PARAM_AUDIO_MODE` | `AUDIO` | `1.00` | `< 0.5`: `LOOP` / `>= 0.5`: `STRETCH` |
| `7` | `PARAM_EXCITER` | `VOICE` | `1.00` | `<0.20`: `RAW`, `<0.55`: `WHISP`, `<0.82`: `AGGR`, `>=0.82`: `NORM` |

Little-endian state block: `'S','N','G','R'` | `uint32 version=1` | `8×float32 params` | `int32 preset` | `int32 syllableIdx` | `uint32 pathLen + bytes` | `uint32 textLen + bytes` | `int32 resetMidiKey` (optional).

---

## 7. Acoustic Limits Inherent to Instrument-Driven Speech
- **Harmonic Grid Trade-off**: Because all voiced energy originates strictly from the loaded SF2/audio wave (zero internal pitched oscillator), vowel recognition on very high notes (`> G5`, sparse harmonics above `800 Hz`) naturally transitions into vocal whistle-like overtone resonances bridged by the sub-harmonic zero-crossing divider.
- **Monophonic Word Cadence**: Intelligibility is strongest when lyrics are phrased near natural syllabic speech/singing speed rather than sub-`40 ms` machine-gun gating so plosive closures (`15–22 ms`) and locus glides (`50–70 ms`) have time to articulate.

---

# Bölüm 2 — Türkçe: babur Teknik Olarak Nasıl Çalışır?

## 1. Mimari ve Gerçek Zamanlı İş Parçacığı Yapısı

Üç derleme hedefinin tamamı (açık kaynaklı `VeSTige` `.dll`: `dist/babur.dll`, `VST3`: `dist/babur.vst3`, Bağımsız: `bin/babur_gui.exe`) tek bir gerçek zamanlı DSP motorunu (`Fraudize::BaburEngine`) ve donanım hızlandırmalı Direct2D/DirectWrite koyu arayüzü (`460 × 436 px`) paylaşır:

- **Ses İş Parçacığında Sıfır Bellek Tahsisi (Zero-Leak)**: Tüm filtre bankaları (`singFormantSvf[4]`, `cascadeFormantSvf[4]`, `pharynxSvf`, `singerFormantSvf`, `vocalCeilingSvf`, `vocoderCarSvf[24]`, ünsüz SVF'leri), zarf izleyiciler ve geçici diziler doğrudan `BaburEngine` sınıfı içinde sabit boyutlu değer üyeleridir; `render()` içinde asla `new`, `malloc` veya dinamik büyüme kullanılmaz.
- **Kilitleme ve Sayısal Güvenlik**: `render()`, `ScopedDenormalsZero` (`FTZ/DAZ`) koruması altında çalışır, `sourceMutex` üzerinde `try_to_lock` kullanarak dosya yüklemelerinin sesi kesmesini engeller, `BLOCK_CHUNK = 64` örneklik bloklar ve `FORMANT_SUBSTEP = 16` örneklik kontrol adımlarıyla işler.

```text
MIDI Olayları + Şarkı Sözü
          │
          ▼
[ TextPhonemeDissector ]
  ├──► EN G2P (Sözlük, Ek Ayrıştırma, Açık/Kapalı Hece Kuralları, W-Renklendirme)
  ├──► TR Fonetik (8 Ünlü, k/g/l Uyumu, Açık e, ğ Ünlü Kaynaması, Vurmalı r)
  ├──► applyWordContext() ──► Kelime Vurgusu + Önceki/Sonraki Yörüngeler + ğ Geçişi
  └──► applySentenceContext() ──► Kelimeler Arası Ulama/Benzeşme (n+b->m, flapping, the->thee) + Noktalama Prozodisi ve İşlev Kelimeleri
          │
          ▼
[ Monofonik Yığın, Aynı-Örnek MIDI Sıralaması ve 3 Modlu LEGATO (FLOW | STAC | HOLD [varsayılan]) ]
  ├──► 6 Eklemli Hareket Fiziği (Çene, Dil Gövdesi X/Y, Dil Ucu, Dudaklar, Dudak Kapanışı, Yumuşak Damak, Artdamak)
  ├──► Sonraki Heceye Erken Bakış (Anticipatory Lookahead) + Örtüşen Yay Kasları + Dudak/Dil Duyarlı Frikasyon
  └──► NoteOff'ta Bitiş Ünsüzü (%78-88 sibilant ünlü kısma) + 90 ms Legato Köprüsü
          │
          ├──► Cümle Taşıyıcısı (SF2 canlıysa <= 0.005 üstü; çakışan legato yumuşak portamento kayar)
          │         │
          │         ▼
          │    Ses Yolu Öncesi Dengeleyici (speechLeveler [0.75..2.80]) + Dudak Işıması + Alt-Harmonik Bölücü
          │         │
          │         ▼
          │    Ses Modu Şekillendirme (NORM / AGGR / WHISP / RAW) ── sıfır dahili osilatör
          │         │
          │         ▼
          │    24 Bantlı Bark SVF Bankası (140..6400 Hz, Q=4.2) + Bant İçi AGC (24x'e kadar)
          │         ├──► SING: 4 Kutuplu Kaskad Formantlar (Q 3.2..11.5, sinüzoidal F0 çekimi)
          │         │            → effEnv Referanslı Servo [0.30, 2.60] → Yumuşak Tepe (pow 0.24)
          │         └──► TALKBOX: 24 Bantlı Analitik Lorentzian Maske (maxPeak^1.35, 0.008 vadi tabanı)
          │                   │
          │                   ▼  PARAM_CHARACTER çapraz geçiş + dengeli Pharynx ve Şarkıcı Formantı
          │         Dinamik Ağız Açılış Alçak-Geçiren Filtresi (270->4850 Hz) → Ünlü-Çekirdek Kilitli vowelLeveler
          │                   │
          └──────────► [ + Enstrüman Dokulu Ünsüz Patlamaları (>=2.6 kHz sibilantta %78 doğrudan / %38 diğerleri) ]
                              │
                              ▼
                       DC Engelleyici (0.9975 kutup) → Tepe Sınırlayıcı (0.85) → fastTanh → Stereo Çıkış
```

---

## 2. İki Dilli Ayrıştırıcı ve Kelime-Bağlamlı Koartikülasyon (`TextPhonemeDissector.hpp`)

Şarkı sözü girildiğinde (`setText`) veya `LANG` (`EN` ↔ `TR`) değiştiğinde metin bir kez ayrıştırılır:

### İngilizce G2P Kuralları (`dissectEnglishWord`, `mapEnglishVowelGroup`)
1. **Sözlük, Bileşik Kelime Bölme ve Ek Çözümleme**: Sık kullanılan kelimeler (`die, dye, mother, motherfucker, money, love, come, done, under` vb.) `tryLookupEnglishBase` + ek kuralları (`-s/-es/-ies`, `-ed`, `-ing`, `-er`, `-tion/-sion`, `-ture`) ve bileşik kelime ayrıştırıcı (`lookupRightPart`) ile doğrudan çözülür.
2. **İngilizce `/ʌ/` (STRUT) ve Gevşek `-er` (`/ɚ/`) Kuralları**:
   - **Açık `/ʌ/` (`V_A`)**: Vurgulu kısa `'u'` (`cut, duck, drum, just, fuck`), `th/v` önündeki açık `'o'` (`mother, stepmother, brother, another, cover, discover`) ve sözlük `/ʌ/` kelimeleri gergin `'ı'` (`V_I_DOTLESS`) veya `/oʊ/` yerine doğru açık `V_A` ünlüsünü alır. Son vurgusuz `-er/-or/-ar` ve `-ther` (`dh`) ise yuvarlak `'ö'` (`V_OE`) yerine rahat `V_I_DOTLESS + r` olarak seslendirilir.
   - **Diğer Bağlam Kuralları**: Kelime sonu `-ie/-ye`/`-y` (`die, cry` `/aɪ/`, çok heceli `-y` `/i/`), `w/qu` sonrası `'a'` (`want, war`), açık ilk hece ve vurgusuz kapalı ekler (`-en, -el, -on, -ment`) bağlama göre seçilir.

### Türkçe Fonetik Motoru (`dissectTurkishWord`, `collectTurkishCluster`)
1. **UTF-8 Birebir Eşleme ve `ğ` Yumuşaması**: `a, e, ı, i, o, ö, u, ü` ünlüleri ile `ç, ğ, ş` ünsüzlerini doğrudan çözer. Hece içi/arası `ğ` (`hasSoftGLink`) ön ünlüler arasında yumuşak `"y"` kayması (*"değil, eğer"*), ön ünlülü hece sonunda ön kayma (*"eğlen, öğren"*), kalın ünlüler arasında ise `%42` pürüzsüz formant kaynaşması (*"ağaç, soğuk"*) üretir.
2. **Ünlü Uyumu, Açık `e` (`/æ/`) ve Vurmalı `r`**: **`k`**, **`g`**, **`l`** lokuslarını ön/arka uyuma göre kaydırır; sonu `r, l, n, m` ile biten kapalı hecelerde **`e`** ünlüsünü açık `/æ/` bölgesine (`645 / 1780 Hz`: *"gel, ben, sen, yer"*) açar; **`r`**'yi Amerikan geriye kıvrımlı `r` yerine Türkçe alveoler vurmalı (`locusF3 = 2180 Hz`) olarak işler.

### Kelime ve Cümle İçi Koartikülasyon (`applyWordContext`, `applySentenceContext`)
- **Kelime İçi Geçiş ve Vurgu**: Zayıf ön ekleri ve vurgusuz heceleri (`%18` EN / `%10` TR), başlangıç/bitiş ünsüzlerini (`%14–36`) ve kelime içi V-to-V etkilerini (`%12–18`) hesaplar.
- **Kelimeler Arası Ulama ve Benzeşme**: İki dilde de `n + p/b/m -> m` (*"on bir"*) ve `n + k/g -> ng` dudaksıllaşması uygulanır; İngilizce'de ünlü öncesi `"the"` $\to$ `/ði/`, ünlüler arası `t -> d` (*"what a"*) ve `d/t/s + y -> j/ch/sh` (*"did you"*) dönüşür; Türkçe'de ünlüyle başlayan sonraki kelime önünde son ünsüz yumuşatılır ve `bir` ünsüz önünde hafifler. Kelimeler arası `%11/%9` V-to-V F1–F2 geçişi kurulur.
- **İngilizce Gömülü Sözlük (`396` kelime) ve Türkçe Dilbilgisi Vurgusu**: `EnglishLexicon.hpp` düzensiz İngilizce kelimeleri (*though, through, thought, enough, laugh, heart, women, choir, island, chaos* vb.) $O(\log N)$ ikili aramayla çözer; `applyTurkishGrammaticalStress` istisnai vurguları (*türkiye, istanbul, şimdi, nasıl*), `-yor`/`-yo+r` şimdiki zaman kuralını (*geliyorum*, *bilmiyorum*), `-ma/-me` olumsuzluk eki vurgusunu ve `mi/mı/mu/mü` soru edatı vurgusunu uygular.
- **Satır İçi Duygu Etiketleri ve `*vurgulama*`**: Sözler içinde `[happy] [sad] [angry] [soft] [excited] [neutral]` (Türkçe: `[mutlu] [üzgün] [kızgın] [yumuşak] [heyecanlı] [nötr]`) etiketleri hece üretmeden `EmotionProfile` ayarlarını (`tilt, breath, formantScale, jawScale, speedScale, edgeDrive`) yumuşakça değiştirir; `*kelime*` vurgusu belirginliği `1.26` katına çıkarıp formantları genişletir. Arayüz hece kutularının altında renkli duygu çizgileri gösterilir.
- **Noktalama Prozodisi ve İşlev Kelimeleri**: `, ; : . ! ?` durakları ile `>= 3` kelimelik öbeklerde işlev kelimeleri (`ve, de, da, bir, the, a, to...`) hafifletilir (`prominence`), cümle sonu odaklı vurgu açılır (`1.14`) ve noktalama perde eğimi (`? +0.16`, `, +0.07`, `. -0.10 st`) uygulanır (uzun tutulan notalarda tam akorda döner).

---

## 3. Taşıyıcılar (`include/tsf.h`, `AudioFileLoader.hpp`, `BaburDSP.cpp`)

- **SoundFont (`.sf2`)**: `_wfopen` / `_fseeki64` tabanlı 64-bit `tsf_stream` ile `256 MB` sınırı olmadan akış okuma (`MS_Basic.sf2` ~`489 MB` / `309` preset ~`240–350 ms` içinde yüklenir); tek baytlık RIFF dolguları, boş alt bloklar ve `0`-bölgeli presetlere karşı korumalıdır. Perde kontrolü `±24` yarım ton aralığında `tsf_channel_set_tuning` ile çalışır.
- **Ses Dosyası Taşıyıcısı (`A4 = MIDI 69 = 440 Hz`)**: WAV ve Windows Media Foundation formatlarını okuyup baştaki/sondaki sessizliği kırpar, `0.85f` seviyesine normalleştirir ve `%15..%85` arasında sıfır geçişi döngüsü kurar (`LOOP`: 4-noktalı Hermite enterpolasyon; `STRETCH`: `180°` kayık çift `2048`-örneklik Hann granülüyle ileri-geri tarama).

---

## 4. Monofonik MIDI, Vuruş Senkronlu Fonem Zamanlaması ve Ağız Kapanışı Dinamikleri

- **Aynı Örnekteki `NoteOff`-Önce MIDI Sıralaması ve `LEGATO: HOLD` (Varsayılan `1.00`)**:
  - `render()`, `renderMidiScratch` olaylarını artan örnek sırasında ve aynı örnekte **`NoteOff` daima `NoteOn`'dan önce** gelecek biçimde (`std::stable_sort`) sıralar; `PARAM_LEGATO = 1.00` (`HOLD`) varsayılanında çakışan notalarda ünlüyü kesintisiz tutup perdeyi kaydırır (`FLOW = 0.00`, `STAC = 0.50`).
- **Gerçek SF2/Audio Legato Kayması ve `90 ms` Köprü (`sf2VoiceRunning`, `phraseBridgeRemaining`)**:
  - `HOLD` / `FLOW` modlarında çalışan SF2 sesi yalnızca **sönmemişse** korunur (`carrierDecayed()`: `carrierEnv >= 0.012` ve son atak tepesinin `carrierAttackPeak` `%25`'inden büyük). Sönmüş piyano/pluck seslerinde yeni nota tetiklenir (FLOW'da notaların atlanması/kısık çıkmasının nedeni buydu). `HOLD` çakışmasında bekleyen bırakma durumu iptal edilir, ünlü çekirdeği geri yüklenir ve gerekirse `reexciteSf2Carrier()` çağrılır (HOLD'da sesin "durması"nın nedeni buydu). Basılı tuş yığını aynı tuşun tekrarına izin verir. En az `16 ms` portamento tabanı ve `NoteOff` sonrasında `90 ms` köprü korunur.
- **Bağlayıcı Kısa Son Ünsüz ve Türkçe `-s` Sibilant Koruması (`startSyllableSequence`, `render`)**:
  - Bitişik notalarda kalan son coda ünsüzü (`-s, -ş, -z, -k, -t`) kısa `0.35x` ölçekle (`<= 8 ms` kapanış, `<= 20 ms` üfleme) eklenir; `NoteOff` coda kapanışında `s/ş/f` ünlüyü **`%78–88`** bastırır, `>= 2.6 kHz` sibilantların **`%78`**'i doğrudan çıkar ve `vowelLeveler` yalnızca `PH_VOWEL_CORE` sırasında çalışır.
- **Sıfır Ön-Gecikmeli Heceler, Sınırlı Başlangıçlar ve Kuadratik Devralma (`onsetCarrySamples`)**:
  - Önceki hecenin kalan coda'sı yeni notanın önüne eklenmez (gecikmesiz `%28` anlık `smoothF` hafızası); çoklu ünsüz başlangıç öbekleri `1/N` ölçeklenip `<= 16 ms` kapanış / `<= 26 ms` patlama / `<= 18 ms` kayma ile sınırlandırılır. Kalan kayma ve nazal renk/sıfırlar (`carryW^2`, `%42` lokus, `%45` nazal), ağız açılmışken erken `PH_VOWEL_CORE` içine devralınır.

---

## 5. Çift Aşamalı Dengeleyici, `effEnv` Referanslı Servo ve Ünlü Ses Eşitleyici

1. **Ses Yolu Öncesi Dengeleyici (`speechLeveler`)**: Sönümlenen vuruşları dengeli biçimde (`clamp(0.24 / (carrierEnv + 0.035), 0.75, 2.80)`) taşır ve bırakmada `1.0`'a döner.
2. **Enstrüman Fazlı Alt-Harmonik Bölücü (`subOctSignL/R`) + 24 Bant / 3-Bölgeli `SING` Ön-Dengeleyicisi**: `normL/R` sıfır geçişlerinden ($n+0.5)F_0$ üretir (`effEnv` ölçekli), `singTiltSvf` (`%58`) + Bark (`%42`) ile dengeli ön/arka formanta (`a0..a3`) verir.
3. **6 Eklemli Hareket Fiziği (`ArticulatorSpring`), Sınırlı `Q` ve Geçiş Vurgusu**: `artJaw, artTongueBodyX/Y, artTongueTip, artLips, artLipClose, artVelum, artVelarClose` yay-kütle kasları ünsüz ve ünlü hareketlerini doğal biçimde üst üste bindirir (örn. `"su"` hecesinde `/s/` söylenirken dudaklar yuvarlanır ve `/s/` ıslığı `%16` pesleşir; `artVelum` nazal açılışı doğal taşır; `vowelSamples > 65 ms` sonrasında `upcomingSyllable` erken bakışı başlar). `Q` üst sınırı `11.5`'tir (`pow(effQ/4.5, 0.24)`); `transPresence` doğrudan fiziksel dil hızından (`|artTongueBodyX.vel|`) türetilir.
4. **4 Kutuplu Kaskad Formantlar, `effEnv` Referanslı Servo ve Çıkış Ünlü Eşitleyicisi (`vowelLeveler`)**:
   - Servo hedefi $F_1$ çıkışı yerine doğrudan **`effEnv * 0.22`** giriş enerjisine (`[0.30, 2.60]`, `9 ms` iniş / `30 ms` çıkış — perde periyodundan yavaş, bu yüzden dalgayı modüle edip bozulma yaratmaz) kilitlenir. `vocalCeilingSvf` (`270 → 4850 Hz`) sonrasında çalışan `vowelLeveler` (`PH_VOWEL_CORE` kilitli) ünlü seviyelerini eşitler; çıkışta `fastTanh` öncesi **güvenlik tepe sınırlayıcısı** (`0.5 ms` / `120 ms`, tavan `0.85`) vardır.
5. **Yaşayan Boğaz (Living Throat), Gerçek Patlamalı Fiziği ve Tempo Uyumlu `FLOW`**:
   - **Yaşayan Boğaz**: Dinamik glottal parlaklık eğimi (`glottalBright`), formant öncesi perde eşzamanlı nefes ışıltısı (`effEnv` sıfırken tam sıfır), `1/f` doğal perde/formant titremesi (`jitterSmooth/shimmerSmooth`), duygusal gerginlik (`emoEdgeDrive`) ve `vowelLeveler` sonrası doğal hece çanı (`smoothSyllEnv`).
   - **Patlamalılar & Vurmalı `r`**: `p/t/k/b/d/g` için `1–3 ms` basınç patlaması, ötümsüz patlamalarda F1 kesintisi (`f1CutbackGain` ile F2/F3 önce açılır), Türkçe `r` için `15 ms` balistik dil ucu vuruşu (`sin^1.6`).
   - **Tempo Uyumlu `FLOW`**: Notalar arası hız (`avgNoteIntervalSec`) takip edilerek hızlı melodilerde kapanış ve kas yayları sıkılaştırılır, yavaş baladlarda açılır; `beatU = 0.62..1.30` aralığında sonraki vuruştan hemen önce hece sonu ünsüzü hazırlanır (`preCodaWeight`) ve bitişik duraklar `10 ms` serbest bırakmasız boğumlanmayla (`unreleased`) birbirine akar.
6. **Tık Önleme**: `tsf.h` en az `25 ms` bırakma süresi uygular.

---

## 6. Parametreler ve İkili Durum Formatı (`"SNGR"` Sürüm 1)

| İndeks | Enum | Etiket | Varsayılan | İşlev |
| :---: | :--- | :--- | :---: | :--- |
| `0` | `PARAM_CHARACTER` | `CHARACTER` | `0.35` | `SING (0.0)` ↔ `TALKBOX (1.0)` |
| `1` | `PARAM_DICTION` | `DICTION` | `0.65` | `%0..%100` ünsüz şiddeti, kapanış derinliği, süre ve atak gecikmesi |
| `2` | `PARAM_THROAT` | `THROAT` | `0.50` | `-12.0 st` .. `+12.0 st` boğaz/formant kaydırma |
| `3` | `PARAM_GLIDE` | `GLIDE` | `0.25` | `8 .. 220 ms` perde kayması (portamento) ve formant geçiş hızı |
| `4` | `PARAM_LANG` | `LANG` | `0.00` | `< 0.5`: `EN` / `>= 0.5`: `TR` |
| `5` | `PARAM_LEGATO` | `LEGATO` | `1.00` | `< 0.35`: `FLOW`, `0.35..0.65`: `STAC`, `>= 0.65`: `HOLD` |
| `6` | `PARAM_AUDIO_MODE` | `AUDIO` | `1.00` | `< 0.5`: `LOOP` / `>= 0.5`: `STRETCH` |
| `7` | `PARAM_EXCITER` | `VOICE` | `1.00` | `<0.20`: `RAW`, `<0.55`: `WHISP`, `<0.82`: `AGGR`, `>=0.82`: `NORM` |

Little-endian kayıt sırası: `'S','N','G','R'` | `uint32 version=1` | `8×float32 params` | `int32 preset` | `int32 syllableIdx` | `uint32 pathLen + bytes` | `uint32 textLen + bytes` | `int32 resetMidiKey`.

---

## 7. Enstrüman-Tabanlı Konuşmanın Doğal Akustik Sınırları
- **Harmonik Izgarası Etkisi**: Sesin tamamı dahili sentetik osilatör olmadan doğrudan `.sf2` veya ses dosyasından üretildiği için çok tiz notalarda (`> G5`, harmonikler arası geniş boşluklarda) motor alt-harmonik bölücüyle (`subOctSign`) boşlukları mümkün olduğunca doldurarak akıcı şarkı tınısını korur.
- **Hece Zamanlaması**: Patlamalı kapanışlarının (`15–22 ms`) ve yer-değişimi kaymalarının (`50–70 ms`) net anlaşılması için notaların doğal konuşma/şarkı temposuna yakın çalınması en yüksek kelime anlaşılırlığını verir.
