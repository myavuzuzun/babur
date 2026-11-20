# babur

monophonic singing & speaking SoundFont (`.sf2`) and Audio (`.wav`/`.mp3`) synthesizer plugin (**VST3** & legacy `.dll` via open-source **VeSTige**).featuring a hardware-accelerated Direct2D/DirectWrite dark-slate UI.

## Features

- **Universal Carrier Input (`.sf2` or `.wav`/`.mp3`)**: Load any SoundFont preset or audio recording (root assumed to be **A4 / MIDI 69**) via **LOAD SF2 / AUDIO** or drag-and-drop. Audio files can sustain indefinitely in either **LOOP** (zero-crossing equal-power crossfade) or **STRETCH** (dual-grain Hann overlap-add) mode while tracking MIDI pitch.
- **English & Turkish Text Dissector (`LANG: EN | TR`)**: Type, paste (`Ctrl+V` / `PASTE`), or load `.txt` sentences of any length. Splits words into syllables with onset/coda consonants, vowels, diphthongs, embedded English pronunciation lexicon, Turkish grammatical stress rules, and inline emotion tags (`[happy]`, `[sad]`, `[angry]`, `[soft]`, `[excited]`, `[neutral]`).
- **1 Note = 1 Syllable (+ Legato Modes)**:
  - **`FLOW`**, **`STAC`**, and **`HOLD`** articulation modes with tempo-adaptive coarticulation and intra-word continuity.
- **Dual Vocal Engine & 3-Tab Voice Sculpting (`MAIN` | `VOICE` | `STYLE`)**:
  - **MAIN**: `CHARACTER` (`SING` ↔ `TALKBOX`), `DICTION`, `THROAT`, `GLIDE`.
  - **VOICE**: `GENDER / AGE` (non-uniform Male ↔ Female ↔ Child formant scaling), `PITCH SHIFT` (`-24 .. +24 st`), `BREATHINESS`, `GRIT / FRY`.
  - **STYLE**: `NASALITY`, `BRIGHTNESS`, `VIB DEPTH`, `VIB RATE`, `AGE / TREMOR`, `ROBOTIC`.

## Building & Testing (Windows x64 / MinGW-w64)

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Outputs:
- `dist/babur.dll` (64-bit legacy plugin built with open-source VeSTige)
- `dist/babur.vst3` (64-bit VST3 plugin built with open-source GPLv3 VST3 headers)
- `bin/babur_gui.exe` (Standalone Direct2D UI preview)

## License & Third-Party Notices

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)** — see [LICENSE](LICENSE).

- **`include/vestige/vestige.h`**: Clean-room open-source VeSTige compatibility header (GNU GPL v2 or later, originally by Javier Serrano Polo; sourced from Carla/LMMS). Contains **no** proprietary Steinberg files.
- **`include/vst3.h`** & **`include/vst3_plugin.h`**: Open-source GPLv3 VST3 headers (derived from Steinberg VST3 SDK under GPLv3 and `vst3_plugin` by Tor-Helge Skei under GPLv3).
- **`include/tsf.h`**: TinySoundFont by Bernhard Schelling (MIT License).
- *VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.*
