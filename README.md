# babur

monophonic singing & speaking soundfont (`.sf2`) and audio (`.wav`/`.mp3`) synthesizer plugin (**VST3** & legacy `.dll` via open-source **VeSTige**).featuring a hardware-accelerated Direct2D/DirectWrite dark-slate ui.

## features

- **universal carrier input (`.sf2` or `.wav`/`.mp3`)**: load any soundfont preset or audio recording (root assumed to be **A4 / MIDI 69**) via **LOAD SF2 / AUDIO** or drag-and-drop. audio files can sustain indefinitely in either **LOOP** (zero-crossing equal-power crossfade) or **STRETCH** (dual-grain Hann overlap-add) mode while tracking midi pitch.
- **english & turkish text dissector (`LANG: EN | TR`)**: type, paste (`Ctrl+V` / `PASTE`), or load `.txt` sentences of any length. splits words into syllables with onset/coda consonants, vowels, diphthongs, embedded english pronunciation lexicon, turkish grammatical stress rules, and inline emotion tags (`[happy]`, `[sad]`, `[angry]`, `[soft]`, `[excited]`, `[neutral]`).
- **1 note = 1 syllable (+ legato modes)**:
  - **`FLOW`**, **`STAC`**, and **`HOLD`** articulation modes with tempo-adaptive coarticulation and intra-word continuity.
- **dual vocal engine & 3-tab voice sculpting (`MAIN` | `VOICE` | `STYLE`)**:
  - **MAIN**: `CHARACTER` (`SING` ↔ `TALKBOX`), `DICTION`, `THROAT`, `GLIDE`.
  - **VOICE**: `GENDER / AGE` (non-uniform male ↔ female ↔ child formant scaling), `PITCH SHIFT` (`-24 .. +24 st`), `BREATHINESS`, `GRIT / FRY`.
  - **STYLE**: `NASALITY`, `BRIGHTNESS`, `VIB DEPTH`, `VIB RATE`, `AGE / TREMOR`, `ROBOTIC`.

## building & testing (Windows x64 / MinGW-w64)

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

outputs:
- `dist/babur.dll` (64-bit legacy plugin built with open-source VeSTige)
- `dist/babur.vst3` (64-bit VST3 plugin built with open-source GPLv3 VST3 headers)
- `bin/babur_gui.exe` (standalone Direct2D ui preview)

## license & third-party notices

this project is licensed under the **GNU General Public License v3.0 (GPL-3.0)** — see [LICENSE](LICENSE).

- **`include/vestige/vestige.h`**: VeSTige compatibility header (GNU GPL v2 or later, originally by Javier Serrano Polo; sourced from Carla/LMMS).
- **`include/vst3.h`** & **`include/vst3_plugin.h`**: Open-source GPLv3 VST3 headers (derived from Steinberg VST3 SDK under GPLv3 and `vst3_plugin` by Tor-Helge Skei under GPLv3).
- **`include/tsf.h`**: TinySoundFont by Bernhard Schelling (MIT License).
- *VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.*
