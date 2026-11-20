#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "src/dsp/BaburDSP.hpp"

using namespace Fraudize;

static bool writeTestWavA4(const std::string& path, float durationSec = 0.60f, float sr = 44100.0f, int maxHarm = 12) {
    uint32_t numSamples = static_cast<uint32_t>(durationSec * sr);
    uint32_t dataBytes = numSamples * 2 * sizeof(int16_t);
    uint32_t riffSize = 36 + dataBytes;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(2);
    w32(static_cast<uint32_t>(sr));
    w32(static_cast<uint32_t>(sr) * 4);
    w16(4); w16(16);
    f.write("data", 4); w32(dataBytes);

    for (uint32_t i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / sr;
        double s = 0.0;
        for (int h = 1; h <= maxHarm; ++h) {
            s += (1.0 / (h * h)) * std::sin(2.0 * 3.14159265358979323846 * 440.0 * h * t);
        }
        int16_t pcm = static_cast<int16_t>(std::clamp(s * 0.55, -0.95, 0.95) * 32767.0);
        f.write(reinterpret_cast<const char*>(&pcm), 2);
        f.write(reinterpret_cast<const char*>(&pcm), 2);
    }
    return true;
}

static void checkFiniteAndPeak(const std::vector<float>& L, const std::vector<float>& R, float& outPeak) {
    outPeak = 0.0f;
    for (size_t i = 0; i < L.size(); ++i) {
        assert(std::isfinite(L[i]) && std::isfinite(R[i]));
        outPeak = std::max(outPeak, std::max(std::fabs(L[i]), std::fabs(R[i])));
    }
}

// Compute ratio of >1500 Hz high-band energy to <600 Hz low-band energy in steady vowel body
static float measureHighToLowRatio(const std::vector<float>& mono, float sr = 44100.0f) {
    float lpAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * 600.0f / sr);
    float hpAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * 1500.0f / sr);
    float lp1 = 0.0f, lp2 = 0.0f;
    float hpLp1 = 0.0f, hpLp2 = 0.0f;
    double lowSq = 0.0;
    double highSq = 0.0;

    // Skip onset consonant + initial glide (first 4096 samples = ~93 ms)
    size_t start = std::min<size_t>(4096, mono.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        float x = mono[i];
        lp1 += lpAlpha * (x - lp1);
        lp2 += lpAlpha * (lp1 - lp2);

        hpLp1 += hpAlpha * (x - hpLp1);
        float hp1 = x - hpLp1;
        hpLp2 += hpAlpha * (hp1 - hpLp2);
        float hp2 = hp1 - hpLp2;

        if (i >= start) {
            lowSq += static_cast<double>(lp2) * lp2;
            highSq += static_cast<double>(hp2) * hp2;
        }
    }
    return static_cast<float>(std::sqrt((highSq + 1e-12) / (lowSq + 1e-12)));
}

static void testDissectorAndClusters() {
    printf("[1/7] Testing English & Turkish Dissector + Consonant Clusters...\n");
    auto en = TextPhonemeDissector::dissectText("Hello world speak singing", false);
    assert(en.size() == 6);
    assert(en[0].display == "HEL");
    assert(en[1].display == "LO");
    assert(en[2].display == "WORLD");
    assert(en[2].onsetCluster.size() == 1 && std::strcmp(en[2].onsetCluster[0].label, "w") == 0);
    assert(en[2].codaCluster.size() == 2);
    assert(std::strcmp(en[2].codaCluster[0].label, "l") == 0);
    assert(std::strcmp(en[2].codaCluster[1].label, "d") == 0);
    assert(en[2].lColored);

    assert(en[3].display == "SPEAK");
    assert(en[3].onsetCluster.size() == 2);
    assert(std::strcmp(en[3].onsetCluster[0].label, "s") == 0);
    assert(std::strcmp(en[3].onsetCluster[1].label, "p") == 0);
    assert(en[3].codaCluster.size() == 1 && std::strcmp(en[3].codaCluster[0].label, "k") == 0);

    // Turkish UTF-8 words: "Merhaba şarkı söyleyen güzel türk"
    std::string trText = "Merhaba \xC5\x9F" "ark\xC4\xB1 s\xC3\xB6yleyen g\xC3\xBCzel t\xC3\xBCrk";
    auto tr = TextPhonemeDissector::dissectText(trText, true);
    assert(tr.size() == 11);
    assert(tr[0].display == "MER");
    assert(tr[1].display == "HA");
    assert(tr[2].display == "BA");
    assert(tr[3].onset.type == C_FRICATIVE_UNVOICED); // ş
    assert(tr[4].vowel1 == V_I_DOTLESS);             // ı
    assert(tr[5].vowel1 == V_OE);                    // ö
    assert(tr[8].vowel1 == V_UE);                    // ü
    assert(tr[10].display == "T\xC3\x9CRK");         // TÜRK
    assert(tr[10].onsetCluster.size() == 1 && std::strcmp(tr[10].onsetCluster[0].label, "t") == 0);
    assert(tr[10].codaCluster.size() == 2);
    assert(std::strcmp(tr[10].codaCluster[0].label, "r") == 0);
    assert(std::strcmp(tr[10].codaCluster[1].label, "k") == 0);
    assert(tr[10].rColored);
    printf("      -> EN (%zu sylls) & TR (%zu sylls) + clusters verified!\n", en.size(), tr.size());
}

static void testMidiSyllableAndLegato() {
    printf("[2/7] Testing 1-Note-1-Syllable Wrap & Legato HOLD/STEP...\n");
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    eng.setParam(PARAM_LANG, 0.0f); // EN
    eng.setText("One two three");
    assert(eng.getNumSyllables() == 3);
    assert(eng.getCurrentSyllableIndex() == 0);

    std::vector<float> outL(512), outR(512);
    auto sendNote = [&](uint8_t st, uint8_t key, uint8_t vel) {
        BaburEngine::RawMidi ev{ 0, st, key, vel };
        eng.processRawMidi(&ev, 1);
        eng.render(outL.data(), outR.data(), 512);
    };

    sendNote(0x90, 60, 100);
    assert(eng.getCurrentSyllableIndex() == 0);
    sendNote(0x80, 60, 0);

    sendNote(0x90, 62, 100);
    assert(eng.getCurrentSyllableIndex() == 1);
    sendNote(0x80, 62, 0);

    sendNote(0x90, 64, 100);
    assert(eng.getCurrentSyllableIndex() == 2);
    sendNote(0x80, 64, 0);

    sendNote(0x90, 65, 100);
    assert(eng.getCurrentSyllableIndex() == 0);

    eng.setParam(PARAM_LEGATO, 1.0f);
    sendNote(0x90, 67, 100);
    assert(eng.getCurrentSyllableIndex() == 0);
    sendNote(0x80, 67, 0);
    sendNote(0x80, 65, 0);

    eng.setParam(PARAM_LEGATO, 0.0f);
    eng.resetSyllableCursor();
    sendNote(0x90, 60, 100);
    assert(eng.getCurrentSyllableIndex() == 0);
    sendNote(0x90, 64, 100);
    assert(eng.getCurrentSyllableIndex() == 1);
    sendNote(0x80, 60, 0);
    sendNote(0x80, 64, 0);

    printf("      -> Syllable wrapping & Legato HOLD/STEP verified!\n");
}

static void testWavAudioLoopAndStretch() {
    printf("[3/7] Testing WAV Loader (A4 root) in LOOP & STRETCH across pitches...\n");
    std::string wavPath = "bin/test_tone_a4.wav";
    assert(writeTestWavA4(wavPath));

    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    assert(eng.loadSourceFile(wavPath));
    assert(eng.getSourceType() == SRC_AUDIO);
    eng.setText("Hello guitar singing");

    std::vector<float> outL(4096), outR(4096);
    int testKeys[5] = { 57, 60, 64, 69, 72 };

    for (int mode = 0; mode <= 1; ++mode) {
        eng.setParam(PARAM_AUDIO_MODE, mode == 0 ? 0.0f : 1.0f);
        eng.setParam(PARAM_CHARACTER, mode == 0 ? 0.15f : 0.85f);
        for (int k : testKeys) {
            BaburEngine::RawMidi on{ 0, 0x90, static_cast<uint8_t>(k), 110 };
            eng.processRawMidi(&on, 1);
            eng.render(outL.data(), outR.data(), 4096);
            float peak = 0.0f;
            checkFiniteAndPeak(outL, outR, peak);
            assert(peak > 0.01f);
            BaburEngine::RawMidi off{ 0, 0x80, static_cast<uint8_t>(k), 0 };
            eng.processRawMidi(&off, 1);
            eng.render(outL.data(), outR.data(), 4096);
        }
    }
    printf("      -> WAV LOOP & STRETCH at MIDI 57..72 verified!\n");
}

static void testDeferredCodaHoldOnNoteOff() {
    printf("[4/7] Testing Deferred Carrier Hold during Coda Release...\n");
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
    eng.setParam(PARAM_DICTION, 0.85f);
    eng.setText("words"); // coda cluster: r, d, z

    std::vector<float> outL(4096), outR(4096);
    BaburEngine::RawMidi on{ 0, 0x90, 60, 115 };
    eng.processRawMidi(&on, 1);
    eng.render(outL.data(), outR.data(), 8192 > 4096 ? 4096 : 4096);
    eng.render(outL.data(), outR.data(), 4096);

    // Send NoteOff and verify strong coda articulation energy immediately after NoteOff
    BaburEngine::RawMidi off{ 0, 0x80, 60, 0 };
    eng.processRawMidi(&off, 1);
    eng.render(outL.data(), outR.data(), 2048); // ~46 ms right inside coda cluster r-d-z
    float codaPeak = 0.0f;
    for (int i = 0; i < 2048; ++i) {
        assert(std::isfinite(outL[i]) && std::isfinite(outR[i]));
        codaPeak = std::max(codaPeak, std::fabs(outL[i]));
    }
    assert(codaPeak > 0.04f);

    // After coda finishes (~300 ms more), output decays cleanly near zero
    for (int b = 0; b < 8; ++b) eng.render(outL.data(), outR.data(), 4096);
    float tailPeak = 0.0f;
    for (int i = 2048; i < 4096; ++i) tailPeak = std::max(tailPeak, std::fabs(outL[i]));
    assert(tailPeak < 0.01f);
    printf("      -> Deferred coda articulation (Peak=%.4f) & clean release (Tail=%.5f) verified!\n", codaPeak, tailPeak);
}

static void testVowelSpectralSeparation() {
    printf("[5/7] Testing Spectral Formant Separation (/i/ vs /u/ & /a/)...\n");
    // Use a dark mellow carrier (only 3 decaying harmonics!) to verify 24-band whitener + exciter
    std::string darkWav = "bin/test_dark_a4.wav";
    assert(writeTestWavA4(darkWav, 0.60f, 44100.0f, 3));

    auto measureWordRatio = [&](const std::string& word, float charVal) -> float {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile(darkWav));
        eng.setParam(PARAM_CHARACTER, charVal);
        eng.setParam(PARAM_DICTION, 0.70f);
        eng.setParam(PARAM_EXCITER, 1.0f);
        eng.setText(word);

        std::vector<float> outL(8192), outR(8192);
        BaburEngine::RawMidi on{ 0, 0x90, 60, 110 };
        eng.processRawMidi(&on, 1);
        eng.render(outL.data(), outR.data(), 8192);
        return measureHighToLowRatio(outL, 44100.0f);
    };

    for (float c : { 0.20f, 0.85f }) {
        float ratioI = measureWordRatio("see", c);
        float ratioU = measureWordRatio("you", c);
        float ratioA = measureWordRatio("are", c);
        printf("      -> char=%.2f | /i/ ratio=%.3f, /a/ ratio=%.3f, /u/ ratio=%.3f\n", c, ratioI, ratioA, ratioU);
        assert(ratioI > ratioU * 1.45f);
    }
    printf("      -> Vowel spectral contrast verified on dark carrier!\n");
}

static void testSf2PresetsAndModes() {
    printf("[6/7] Testing SF2 Carrier across Guitar / Piano / Flute presets...\n");
    const std::string sf2Path = "..\\basicsf2daw\\assets\\TimGM6mb.sf2";
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    if (!eng.loadSourceFile(sf2Path)) {
        printf("      [INFO] TimGM6mb.sf2 not found at %s; skipping live SF2 render check.\n", sf2Path.c_str());
        return;
    }
    assert(eng.getSourceType() == SRC_SF2);
    assert(eng.getNumPresets() > 75);

    eng.setParam(PARAM_LANG, 1.0f); // TR
    eng.setText("Merhaba d\xC3\xBCnya t\xC3\xBCrk");
    int presetsToTest[3] = { 0, 24, 73 };

    std::vector<float> outL(4096), outR(4096);
    for (int p : presetsToTest) {
        eng.setPresetIndex(p);
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_EXCITER, 1.0f);
        BaburEngine::RawMidi on{ 0, 0x90, 60, 115 };
        eng.processRawMidi(&on, 1);
        eng.render(outL.data(), outR.data(), 4096);
        float peak = 0.0f;
        checkFiniteAndPeak(outL, outR, peak);
        assert(peak > 0.01f);

        eng.setParam(PARAM_CHARACTER, 0.90f);
        eng.render(outL.data(), outR.data(), 4096);
        checkFiniteAndPeak(outL, outR, peak);
        assert(peak > 0.01f);

        BaburEngine::RawMidi off{ 0, 0x80, 60, 0 };
        eng.processRawMidi(&off, 1);
        eng.render(outL.data(), outR.data(), 4096);
    }
    printf("      -> SF2 Piano/Guitar/Flute SING + TALKBOX verified!\n");
}

static void testStateSerialization() {
    printf("[7/7] Testing State Chunk Save & Restore...\n");
    BaburEngine a;
    a.setParam(PARAM_CHARACTER, 0.72f);
    a.setParam(PARAM_DICTION, 0.88f);
    a.setParam(PARAM_THROAT, 0.25f);
    a.setParam(PARAM_GLIDE, 0.42f);
    a.setParam(PARAM_LANG, 1.0f);
    a.setParam(PARAM_LEGATO, 0.0f);
    a.setParam(PARAM_AUDIO_MODE, 0.0f);
    a.setParam(PARAM_EXCITER, 0.0f);
    a.loadSourceFile("bin/test_tone_a4.wav");
    a.setText("Merhaba \xC5\x9F" "ark\xC4\xB1");
    a.setCurrentSyllableIndex(2);
    a.setResetMidiKey(24);

    std::vector<uint8_t> chunk = a.saveState();
    assert(!chunk.empty());

    BaburEngine b;
    b.restoreState(chunk.data(), chunk.size());
    assert(std::fabs(b.getParam(PARAM_CHARACTER) - 0.72f) < 1e-4f);
    assert(std::fabs(b.getParam(PARAM_DICTION) - 0.88f) < 1e-4f);
    assert(std::fabs(b.getParam(PARAM_THROAT) - 0.25f) < 1e-4f);
    assert(std::fabs(b.getParam(PARAM_GLIDE) - 0.42f) < 1e-4f);
    assert(b.getParam(PARAM_LANG) >= 0.5f);
    assert(b.getParam(PARAM_LEGATO) < 0.5f);
    assert(b.getParam(PARAM_AUDIO_MODE) < 0.5f);
    assert(std::fabs(b.getParam(PARAM_EXCITER) - 0.0f) < 1e-4f || b.getParam(PARAM_EXCITER) < 0.5f);
    assert(b.getSourceType() == SRC_AUDIO);
    assert(b.getText() == "Merhaba \xC5\x9F" "ark\xC4\xB1");
    assert(b.getCurrentSyllableIndex() == 2);
    assert(b.getResetMidiKey() == 24);
    printf("      -> State chunk serialization (incl. resetMidiKey) verified!\n");
}

static void testDieDyePronunciationAndDiphthong() {
    printf("[8/9] Testing English G2P ('die', 'dye', soft C/G, diphthongs) & Sweep...\n");
    auto tokens = TextPhonemeDissector::dissectText("die dye my cry try type style face nice voice lye byte lace nation future", false);
    // Expected 17 syllables: die, dye, my, cry, try, type, style, face, nice, voice, lye, byte, lace, na-tion, fu-ture
    assert(tokens.size() == 17);

    // "die" & "dye" -> d + /aI/
    for (int idx : { 0, 1 }) {
        assert(tokens[idx].onsetCluster.size() == 1 && std::strcmp(tokens[idx].onsetCluster[0].label, "d") == 0);
        assert(tokens[idx].vowel1 == V_A && tokens[idx].vowel2 == V_I && tokens[idx].isDiphthong);
    }
    // my, cry, try, type, style, lye, byte -> /aI/
    for (int idx : { 2, 3, 4, 5, 6, 10, 11 }) {
        assert(tokens[idx].vowel1 == V_A && tokens[idx].vowel2 == V_I && tokens[idx].isDiphthong);
    }
    // face, nice, voice, lace -> soft 'c' = /s/
    for (int idx : { 7, 8, 9, 12 }) {
        assert(tokens[idx].codaCluster.size() == 1 && std::strcmp(tokens[idx].codaCluster[0].label, "s") == 0);
    }
    // nation -> NA + TION (sh), future -> FU + TURE (ch)
    assert(std::strcmp(tokens[14].onset.label, "sh") == 0);
    assert(std::strcmp(tokens[16].onset.label, "ch") == 0);

    // Render "die" over 12000 samples and verify /a/ -> /i/ spectral sweep
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
    eng.setParam(PARAM_CHARACTER, 0.50f);
    eng.setParam(PARAM_DICTION, 0.75f);
    eng.setText("die");

    std::vector<float> outL(12000), outR(12000);
    BaburEngine::RawMidi on{ 0, 0x90, 60, 115 };
    eng.processRawMidi(&on, 1);
    eng.render(outL.data(), outR.data(), 12000);

    // Measure high/low ratio in /a/ portion [1200..3200] vs /i/ portion [8800..12000]
    auto bandRatioSlice = [&](size_t s0, size_t s1) -> float {
        float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 900.0f / 44100.0f);
        float hpA = 1.0f - std::exp(-2.0f * 3.14159265f * 1800.0f / 44100.0f);
        float lp = 0.0f, hpLp = 0.0f;
        double lowSq = 0.0, highSq = 0.0;
        for (size_t i = 0; i < s1; ++i) {
            float x = outL[i];
            lp += lpA * (x - lp);
            hpLp += hpA * (x - hpLp);
            float hp = x - hpLp;
            if (i >= s0) {
                lowSq += static_cast<double>(lp) * lp;
                highSq += static_cast<double>(hp) * hp;
            }
        }
        return static_cast<float>(std::sqrt((highSq + 1e-12) / (lowSq + 1e-12)));
    };

    float earlyRatio = bandRatioSlice(1200, 3200); // /a/ F1=760, F2=1150
    float lateRatio = bandRatioSlice(8800, 12000); // /i/ F1=265, F2=2420
    printf("      -> 'die' diphthong sweep: early(/a/) ratio=%.3f -> late(/i/) ratio=%.3f\n", earlyRatio, lateRatio);
    assert(lateRatio > earlyRatio * 1.15f);
    printf("      -> English G2P ('die'/'dye'/soft C/G) & diphthong sweep verified!\n");
}

static void testSilentResetKeyswitchAndLearn() {
    printf("[9/9] Testing Silent Reset MIDI Keyswitch & MIDI Learn...\n");
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
    eng.setText("One two three four");

    std::vector<float> outL(2048), outR(2048);
    float peak = 0.0f;

    // 1. MIDI Learn: press key 24 -> sets reset key, makes ZERO sound
    eng.setLearningResetKey(true);
    assert(eng.isLearningResetKey());
    BaburEngine::RawMidi learnOn{ 0, 0x90, 24, 100 };
    eng.processRawMidi(&learnOn, 1);
    eng.render(outL.data(), outR.data(), 2048);
    checkFiniteAndPeak(outL, outR, peak);
    assert(peak == 0.0f);
    assert(!eng.isLearningResetKey());
    assert(eng.getResetMidiKey() == 24);
    BaburEngine::RawMidi learnOff{ 0, 0x80, 24, 0 };
    eng.processRawMidi(&learnOff, 1);
    eng.render(outL.data(), outR.data(), 512);

    // 2. Play two normal notes (MIDI 60 -> syl 0, MIDI 62 -> syl 1)
    BaburEngine::RawMidi n1On{ 0, 0x90, 60, 110 };
    eng.processRawMidi(&n1On, 1);
    eng.render(outL.data(), outR.data(), 2048);
    checkFiniteAndPeak(outL, outR, peak);
    assert(peak > 0.01f);
    assert(eng.getCurrentSyllableIndex() == 0);
    BaburEngine::RawMidi n1Off{ 0, 0x80, 60, 0 };
    eng.processRawMidi(&n1Off, 1);
    eng.render(outL.data(), outR.data(), 2048);

    BaburEngine::RawMidi n2On{ 0, 0x90, 62, 110 };
    eng.processRawMidi(&n2On, 1);
    eng.render(outL.data(), outR.data(), 2048);
    assert(eng.getCurrentSyllableIndex() == 1);
    BaburEngine::RawMidi n2Off{ 0, 0x80, 62, 0 };
    eng.processRawMidi(&n2Off, 1);
    for (int k = 0; k < 14; ++k) eng.render(outL.data(), outR.data(), 2048);

    float preResetPeak = 0.0f;
    eng.render(outL.data(), outR.data(), 2048);
    checkFiniteAndPeak(outL, outR, preResetPeak);
    assert(preResetPeak < 0.0005f);

    // 3. Trigger Reset Key 24 -> ZERO sound, resets cursor to 0
    BaburEngine::RawMidi rstOn{ 0, 0x90, 24, 120 };
    eng.processRawMidi(&rstOn, 1);
    eng.render(outL.data(), outR.data(), 2048);
    checkFiniteAndPeak(outL, outR, peak);
    assert(peak < 0.0005f);
    assert(eng.getCurrentSyllableIndex() == 0);
    assert(eng.getNextSyllableIndex() == 0);

    // 4. Next normal note plays syllable 0 cleanly with non-zero audio
    BaburEngine::RawMidi n3On{ 0, 0x90, 64, 110 };
    eng.processRawMidi(&n3On, 1);
    eng.render(outL.data(), outR.data(), 2048);
    checkFiniteAndPeak(outL, outR, peak);
    assert(peak > 0.01f);
    assert(eng.getCurrentSyllableIndex() == 0);
    assert(eng.getNextSyllableIndex() == 1);
    printf("      -> Silent Reset MIDI Keyswitch & MIDI Learn verified!\n");
}

static void testLargeSf2Loading() {
    printf("[10/12] Testing Large (>256MB) SF2 Loading & Playback (MS_Basic.sf2)...\n");
    const std::string sf2Path = "..\\basicsf2daw\\assets\\MS_Basic.sf2";
    std::ifstream probe(sf2Path, std::ios::binary);
    if (!probe.good()) {
        printf("      -> [SKIP] ..\\basicsf2daw\\assets\\MS_Basic.sf2 not present on disk\n");
        return;
    }
    probe.close();

    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    eng.setText("hello world");
    eng.setParam(PARAM_EXCITER, 1.00f); // VOICE_NORMAL

    auto t0 = std::chrono::steady_clock::now();
    bool ok = eng.loadSourceFile(sf2Path);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    assert(ok);
    assert(eng.getSourceType() == SRC_SF2);
    assert(eng.getNumPresets() > 100);

    BaburEngine::RawMidi noteOn{ 0, 0x90, 60, 100 };
    eng.processRawMidi(&noteOn, 1);
    std::vector<float> outL(4096, 0.0f), outR(4096, 0.0f);
    eng.render(outL.data(), outR.data(), 4096);

    float peak = 0.0f;
    checkFiniteAndPeak(outL, outR, peak);
    assert(peak > 0.01f);
    printf("      -> MS_Basic.sf2 loaded %d presets in %.1f ms (peak=%.3f)\n",
           eng.getNumPresets(), ms, peak);
}

static bool writeGatedSilentLoopWav(const std::string& path, float sr = 44100.0f) {
    // Valid WAV with 64-sample markers at start & end so AudioFileLoader keeps the file,
    // while the entire loop region [15%..85%] is pure digital silence (0.0f).
    uint32_t numSamples = static_cast<uint32_t>(0.90f * sr);
    uint32_t dataBytes = numSamples * 2 * sizeof(int16_t);
    uint32_t riffSize = 36 + dataBytes;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(2);
    w32(static_cast<uint32_t>(sr));
    w32(static_cast<uint32_t>(sr) * 4);
    w16(4); w16(16);
    f.write("data", 4); w32(dataBytes);

    for (uint32_t i = 0; i < numSamples; ++i) {
        int16_t pcm = (i < 64 || i + 64 >= numSamples) ? static_cast<int16_t>(16384) : static_cast<int16_t>(0);
        f.write(reinterpret_cast<const char*>(&pcm), 2);
        f.write(reinterpret_cast<const char*>(&pcm), 2);
    }
    return true;
}

static void testInstrumentOnlyVoiceAndModes() {
    printf("[11/12] Testing Pure Instrument-Only Voice (Zero Self-Oscillation) & 4 Voice Modes...\n");
    assert(writeGatedSilentLoopWav("bin/test_silent_a4.wav", 44100.0f));
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));

    // 1. Held NoteOn inside silent instrument sustain must yield zero output (no internal sine oscillator leaking)
    const float modes[4] = { 0.00f, 0.35f, 0.70f, 1.00f };
    const char* modeNames[4] = { "RAW", "WHISPER", "AGGR", "NORMAL" };

    std::vector<float> outL(4096, 0.0f), outR(4096, 0.0f);
    for (int m = 0; m < 4; ++m) {
        BaburEngine silentEng;
        silentEng.setSampleRate(44100.0f);
        assert(silentEng.loadSourceFile("bin/test_silent_a4.wav"));
        silentEng.setParam(PARAM_AUDIO_MODE, 0.0f); // LOOP (loops 15%..85% digital silence)
        silentEng.setText("a o");
        silentEng.setParam(PARAM_EXCITER, modes[m]);
        BaburEngine::RawMidi noteOn{ 0, 0x90, 60, 110 };
        silentEng.processRawMidi(&noteOn, 1);
        // Advance past the initial 64-sample file marker (~0.55s) into silent sustain loop
        for (int b = 0; b < 6; ++b) silentEng.render(outL.data(), outR.data(), 4096);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        silentEng.render(outL.data(), outR.data(), 4096);
        float silentPeak = 0.0f;
        checkFiniteAndPeak(outL, outR, silentPeak);
        assert(silentPeak < 1e-5f);
    }

    // 2. Active harmonic WAV across all 4 modes produces finite non-zero voice output
    for (int m = 0; m < 4; ++m) {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setText("speak strong");
        eng.setParam(PARAM_EXCITER, modes[m]);
        BaburEngine::RawMidi noteOn{ 0, 0x90, 62, 108 };
        eng.processRawMidi(&noteOn, 1);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        eng.render(outL.data(), outR.data(), 4096);
        float peak = 0.0f;
        checkFiniteAndPeak(outL, outR, peak);
        assert(peak > 0.01f);
        printf("      -> Mode %-7s peak=%.3f\n", modeNames[m], peak);
    }
}

static void testTurkishAllophonesAndEnglishCoarticulation() {
    printf("[12/12] Testing Turkish Front/Back Allophones & English Coarticulation...\n");
    auto tr = TextPhonemeDissector::dissectText("kal gel", true);
    assert(tr.size() == 2);
    assert(!tr[0].onsetCluster.empty() && !tr[1].onsetCluster.empty());
    // "kal" (back vowel 'a') -> velar k (locusF2 < 1400 Hz)
    // "gel" (front vowel 'e') -> palatalized g (locusF2 > 1800 Hz)
    assert(tr[0].onset.locusF2 < 1400.0f);
    assert(tr[1].onset.locusF2 > 1800.0f);
    assert(tr[1].onset.locusF2 > tr[0].onset.locusF2 + 500.0f);

    // English lip-rounding / high-front coarticulation on onset burst frequencies: "key" vs "koo"
    auto en = TextPhonemeDissector::dissectText("key koo", false);
    assert(en.size() == 2);
    assert(en[0].onset.noiseFreq1Hz > en[1].onset.noiseFreq1Hz * 1.20f);
    assert(en[0].onset.noiseFreq2Hz > en[1].onset.noiseFreq2Hz * 1.15f);
    printf("      -> TR kal locusF2=%.0f Hz, gel locusF2=%.0f Hz | EN key burst1=%.0f Hz, koo burst1=%.0f Hz\n",
           tr[0].onset.locusF2, tr[1].onset.locusF2, en[0].onset.noiseFreq1Hz, en[1].onset.noiseFreq1Hz);
}

static void testDynamicFormantServoAndMemoryStability() {
    printf("[13/13] Testing Dynamic Formant Servo Multi-Pitch Separation & Memory/State Stability...\n");
    assert(writeTestWavA4("bin/test_dark_a4.wav", 0.60f, 44100.0f, 3));
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));

    const int notes[3] = { 52, 60, 67 };
    const char* wavs[2] = { "bin/test_dark_a4.wav", "bin/test_tone_a4.wav" };
    std::vector<float> outL(8192), outR(8192);

    for (const char* wav : wavs) {
        for (int note : notes) {
            auto runWord = [&](const std::string& w) {
                BaburEngine eng;
                eng.setSampleRate(44100.0f);
                assert(eng.loadSourceFile(wav));
                eng.setParam(PARAM_CHARACTER, 0.25f);
                eng.setParam(PARAM_DICTION, 0.70f);
                eng.setParam(PARAM_EXCITER, 1.00f);
                eng.setText(w);
                BaburEngine::RawMidi on{ 0, 0x90, static_cast<uint8_t>(note), 110 };
                eng.processRawMidi(&on, 1);
                eng.render(outL.data(), outR.data(), 8192);
                float peak = 0.0f;
                checkFiniteAndPeak(outL, outR, peak);
                assert(peak > 0.01f && peak < 1.0f);
                return measureHighToLowRatio(outL, 44100.0f);
            };
            float rI = runWord("see");
            float rU = runWord("you");
            assert(rI > rU * 1.45f);
        }
    }

    // Stress-test construct/destroy/reset/pitch-bend/NoteOff lifecycle for memory & filter stability
    std::vector<float> blkL(4096), blkR(4096);
    for (int iter = 0; iter < 20; ++iter) {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setText("hello singing voice");
        eng.setParam(PARAM_GLIDE, 0.45f);
        eng.setParam(PARAM_EXCITER, (iter % 2 == 0) ? 1.00f : 0.70f);

        BaburEngine::RawMidi evs[3] = {
            { 0,    0x90, static_cast<uint8_t>(55 + (iter % 12)), 115 },
            { 1024, 0xE0, 0x00, 0x60 }, // pitch bend up
            { 2048, 0x80, static_cast<uint8_t>(55 + (iter % 12)), 0 }
        };
        eng.processRawMidi(evs, 3);
        eng.render(blkL.data(), blkR.data(), 4096);
        float peak = 0.0f;
        checkFiniteAndPeak(blkL, blkR, peak);
        assert(peak > 0.01f && peak < 1.0f);

        // Wait (~0.93s after NoteOff for coda release + 65ms envelope decay) and verify clean silence
        for (int b = 0; b < 10; ++b) {
            eng.render(blkL.data(), blkR.data(), 4096);
        }
        float tailPeak = 0.0f;
        checkFiniteAndPeak(blkL, blkR, tailPeak);
        assert(tailPeak < 1e-4f);
        eng.reset();
    }
    printf("      -> Dynamic Servo multi-pitch separation & 20x lifecycle/decay stability verified!\n");
}

static void testWordContextualVowelsAndCoarticulation() {
    printf("[14/14] Testing Word-Contextual English Vowels, V-to-V Coarticulation & Boundaries...\n");

    // 1. Open vs closed & unstressed syllables in English multisyllabic words
    auto en = TextPhonemeDissector::dissectText("paper silent moment want war", false);
    // Expected: PA-PER (2) + SI-LENT (2) + MO-MENT (2) + WANT (1) + WAR (1) = 8 syllables
    assert(en.size() == 8);
    // "paper" -> PA (diphthong /eI/, stressed, wordStart), PER (unstressed, wordEnd)
    assert(en[0].display == "PA" && en[0].vowel1 == V_E && en[0].vowel2 == V_I && en[0].isDiphthong);
    assert(en[0].isWordStart && !en[0].isWordEnd && en[0].isStressed);
    assert(en[1].display == "PER" && !en[1].isWordStart && en[1].isWordEnd && !en[1].isStressed);
    // "silent" -> SI (/aI/), LENT (reduced closed vowel)
    assert(en[2].display == "SI" && en[2].vowel1 == V_A && en[2].vowel2 == V_I && en[2].isDiphthong);
    // "moment" -> MO (/oU/), MENT (unstressed central vowel)
    assert(en[4].display == "MO" && en[4].vowel1 == V_O && en[4].vowel2 == V_U && en[4].isDiphthong);
    assert(en[5].display == "MENT" && !en[5].isStressed && en[5].vowel1 == V_I_DOTLESS);
    // "want" -> W-colored /A/, "war" -> W+R-colored /O/
    assert(en[6].display == "WANT" && en[6].vowel1 == V_A);
    assert(en[7].display == "WAR" && en[7].vowel1 == V_O && en[7].rColored);

    // 2. Consonant-to-vowel coarticulation on contextF1..F3
    auto weSee = TextPhonemeDissector::dissectText("we see", false);
    assert(weSee.size() == 2);
    assert(weSee[0].contextF2 < weSee[1].contextF2 - 150.0f);

    auto carCat = TextPhonemeDissector::dissectText("car cat", false);
    assert(carCat.size() == 2);
    assert(carCat[0].contextEndF3 < 2000.0f && carCat[0].contextEndF3 < carCat[1].contextEndF3 - 300.0f);

    // 3. Turkish vowel-to-vowel coarticulation inside multi-syllable words ("kalem" vs "kalmak")
    auto trWords = TextPhonemeDissector::dissectText("kalem kalmak", true);
    assert(trWords.size() == 4);
    assert(trWords[0].isWordStart && !trWords[0].isWordEnd);
    assert(!trWords[1].isWordStart && trWords[1].isWordEnd && trWords[1].isStressed);
    // "KA" before front "-LEM" anticipates higher F2 at syllable end than "KAL" before back "-MAK"
    assert(trWords[0].contextEndF2 > trWords[2].contextEndF2 + 120.0f);

    printf("      -> EN context (PA=%s->%s, WE F2=%.0f vs SEE F2=%.0f) | TR V-to-V (kalem KA_endF2=%.0f vs kalmak KAL_endF2=%.0f)\n",
           en[0].phoneticHint.c_str(), en[4].phoneticHint.c_str(),
           weSee[0].contextF2, weSee[1].contextF2,
           trWords[0].contextEndF2, trWords[2].contextEndF2);
}

static void testMouthClosureFilterAndLocusGlide() {
    printf("[15/15] Testing Dynamic Mouth-Closure Lowpass (270->4850 Hz) & Locus-to-Vowel Glide...\n");
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));

    auto bandEnergyRatio = [&](const std::vector<float>& buf, size_t s0, size_t s1) {
        float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 500.0f / 44100.0f);
        float hpA = 1.0f - std::exp(-2.0f * 3.14159265f * 1200.0f / 44100.0f);
        float lp = 0.0f, hpLp = 0.0f;
        double lowSq = 0.0, highSq = 0.0;
        for (size_t i = 0; i < s1 && i < buf.size(); ++i) {
            float x = buf[i];
            lp += lpA * (x - lp);
            hpLp += hpA * (x - hpLp);
            float hp = x - hpLp;
            if (i >= s0) {
                lowSq += static_cast<double>(lp) * lp;
                highSq += static_cast<double>(hp) * hp;
            }
        }
        return static_cast<float>(std::sqrt((highSq + 1e-12) / (lowSq + 1e-12)));
    };

    // 1. Render "mama" and verify nasal /m/ mouth closure [150..900] is >=2.2x darker than open /a/ vowel [3800..6500]
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
    eng.setParam(PARAM_CHARACTER, 0.25f);
    eng.setParam(PARAM_DICTION, 0.85f);
    eng.setParam(PARAM_EXCITER, 1.00f);
    eng.setText("mama");

    std::vector<float> outL(7000), outR(7000);
    BaburEngine::RawMidi on{ 0, 0x90, 60, 115 };
    eng.processRawMidi(&on, 1);
    eng.render(outL.data(), outR.data(), 7000);
    float peak = 0.0f;
    checkFiniteAndPeak(outL, outR, peak);
    assert(peak > 0.01f);

    float closedRatio = bandEnergyRatio(outL, 150, 900);
    float openRatio   = bandEnergyRatio(outL, 3800, 6500);
    assert(openRatio > closedRatio * 2.2f);

    // 2. Render early transition of "we" vs "see" and confirm distinct onset-to-vowel spectral trajectories
    auto renderWordEarly = [&](const std::string& w) {
        BaburEngine e2;
        e2.setSampleRate(44100.0f);
        assert(e2.loadSourceFile("bin/test_tone_a4.wav"));
        e2.setParam(PARAM_CHARACTER, 0.25f);
        e2.setParam(PARAM_DICTION, 0.75f);
        e2.setText(w);
        std::vector<float> l(3000), r(3000);
        BaburEngine::RawMidi n{ 0, 0x90, 60, 110 };
        e2.processRawMidi(&n, 1);
        e2.render(l.data(), r.data(), 3000);
        float p = 0.0f;
        checkFiniteAndPeak(l, r, p);
        assert(p > 0.01f);
        return bandEnergyRatio(l, 600, 2200);
    };
    float earlyWe = renderWordEarly("we");
    float earlySee = renderWordEarly("see");
    assert(earlySee > earlyWe * 1.40f);
    printf("      -> Mouth closure (/m/ ratio=%.3f -> /a/ ratio=%.3f) & locus glide (we=%.3f vs see=%.3f) verified!\n",
           closedRatio, openRatio, earlyWe, earlySee);
}

static void testVelarLociNasalZerosAndPlosiveClosure() {
    printf("[16/16] Testing EN Velar Loci, Plosive Pressure Closure & Place-Specific Nasal Zeros...\n");
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));

    // 1. English velar k/g palatal-to-velar tracking
    auto enVelars = TextPhonemeDissector::dissectText("key cool give go", false);
    assert(enVelars.size() == 4);
    assert(enVelars[0].onset.locusF2 > 2000.0f && enVelars[1].onset.locusF2 < 1300.0f);
    assert(enVelars[2].onset.locusF2 > 1800.0f && enVelars[3].onset.locusF2 < 1300.0f);

    // 2. Plosive pressure closure depth at default DICTION = 0.65 on "a pa"
    BaburEngine eng;
    eng.setSampleRate(44100.0f);
    assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
    eng.setParam(PARAM_CHARACTER, 0.25f);
    eng.setParam(PARAM_DICTION, 0.65f);
    eng.setParam(PARAM_LEGATO, 0.0f); // STEP
    eng.setText("a pa");

    std::vector<float> bufL(4096), bufR(4096);
    BaburEngine::RawMidi n1{ 0, 0x90, 60, 112 };
    eng.processRawMidi(&n1, 1);
    eng.render(bufL.data(), bufR.data(), 4096); // play "A"
    double vSq = 0.0;
    for (int i = 2000; i < 4096; ++i) vSq += static_cast<double>(bufL[i]) * bufL[i];
    float vowelRms = static_cast<float>(std::sqrt(vSq / 2096.0));

    BaburEngine::RawMidi n2{ 0, 0x90, 62, 112 };
    eng.processRawMidi(&n2, 1);
    eng.render(bufL.data(), bufR.data(), 4096); // "PA" starts with p closure (~18ms = ~790 samp)
    float peak = 0.0f;
    checkFiniteAndPeak(bufL, bufR, peak);
    assert(peak > 0.01f);
    double cSq = 0.0;
    for (int i = 180; i < 560; ++i) cSq += static_cast<double>(bufL[i]) * bufL[i];
    float closureRms = static_cast<float>(std::sqrt(cSq / 380.0));
    assert(closureRms < vowelRms * 0.45f);

    // 3. Place-specific nasal acoustic differences: "ma" vs "na" (bilabial 1080Hz zero vs alveolar 1720Hz zero)
    auto measurePlaceBand = [&](const std::string& syl) {
        BaburEngine ne;
        ne.setSampleRate(44100.0f);
        assert(ne.loadSourceFile("bin/test_tone_a4.wav"));
        ne.setParam(PARAM_CHARACTER, 0.20f);
        ne.setParam(PARAM_DICTION, 0.85f);
        ne.setText(syl);
        std::vector<float> l(4096), r(4096);
        BaburEngine::RawMidi mn{ 0, 0x90, 57, 110 };
        ne.processRawMidi(&mn, 1);
        ne.render(l.data(), r.data(), 4096);
        float np = 0.0f;
        checkFiniteAndPeak(l, r, np);
        assert(np > 0.01f);
        // Compare ~1080 Hz vs ~1720 Hz energy via simple 1-pole band isolation during transition [1000..3600]
        float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 1350.0f / 44100.0f);
        float lp2 = 0.0f;
        double below1350 = 0.0, above1350 = 0.0;
        for (int i = 0; i < 3600; ++i) {
            float x = l[i];
            lp2 += lpA * (x - lp2);
            float hp = x - lp2;
            if (i >= 1000) {
                below1350 += static_cast<double>(lp2) * lp2;
                above1350 += static_cast<double>(hp) * hp;
            }
        }
        return static_cast<float>(std::sqrt((above1350 + 1e-12) / (below1350 + 1e-12)));
    };
    float maR = measurePlaceBand("ma");
    float naR = measurePlaceBand("na");
    printf("      -> Nasal place balance (>1350/<1350 Hz): ma=%.4f, na=%.4f\n", maR, naR);
    assert(std::fabs(naR - maR) > 0.02f);

    printf("      -> EN k(key=%.0f vs cool=%.0f), p-closureRMS=%.4f (vowel=%.4f), nasal ma=%.3f vs na=%.3f verified!\n",
           enVelars[0].onset.locusF2, enVelars[1].onset.locusF2, closureRms, vowelRms, maR, naR);
}

static bool writeTestPluckWavA4(const std::string& path, float durationSec = 0.65f, float sr = 44100.0f) {
    uint32_t numSamples = static_cast<uint32_t>(durationSec * sr);
    uint32_t dataBytes = numSamples * 2 * sizeof(int16_t);
    uint32_t riffSize = 36 + dataBytes;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w32(riffSize); f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16); w16(1); w16(2);
    w32(static_cast<uint32_t>(sr));
    w32(static_cast<uint32_t>(sr) * 4);
    w16(4); w16(16);
    f.write("data", 4); w32(dataBytes);

    for (uint32_t i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / sr;
        double env = std::exp(-t / 0.065);
        double s = 0.0;
        for (int h = 1; h <= 10; ++h) {
            s += (1.0 / (h * h)) * std::sin(2.0 * 3.14159265358979323846 * 440.0 * h * t);
        }
        int16_t pcm = static_cast<int16_t>(std::clamp(s * env * 0.72, -0.95, 0.95) * 32767.0);
        f.write(reinterpret_cast<const char*>(&pcm), 2);
        f.write(reinterpret_cast<const char*>(&pcm), 2);
    }
    return true;
}

static void testContinuousPhraseBridgeAndSameOffsetMidi() {
    printf("[17/17] Testing Continuous Phrase Bridge, Leveler & Same-Offset NoteOff Sort...\n");

    // 1. 3-mode LEGATO decode & parameter display
    assert(BaburEngine::decodeLegatoMode(0.00f) == LEGATO_FLOW);
    assert(BaburEngine::decodeLegatoMode(0.50f) == LEGATO_STAC);
    assert(BaburEngine::decodeLegatoMode(1.00f) == LEGATO_HOLD);
    assert(BaburEngine::formatParamDisplay(PARAM_LEGATO, 0.00f) == "FLOW");
    assert(BaburEngine::formatParamDisplay(PARAM_LEGATO, 0.50f) == "STAC");
    assert(BaburEngine::formatParamDisplay(PARAM_LEGATO, 1.00f) == "HOLD");

    // 2. Same-sample-offset NoteOff-before-NoteOn sorting in render() (touching DAW grid notes in HOLD mode)
    BaburEngine sortEng;
    sortEng.setSampleRate(44100.0f);
    sortEng.setParam(PARAM_LEGATO, 1.0f); // HOLD
    sortEng.setText("One two three");
    assert(writeTestWavA4("bin/test_tone_a4.wav"));
    assert(sortEng.loadSourceFile("bin/test_tone_a4.wav"));
    std::vector<float> sL(1024), sR(1024);

    BaburEngine::RawMidi startNote{ 0, 0x90, 60, 100 };
    sortEng.processRawMidi(&startNote, 1);
    sortEng.render(sL.data(), sR.data(), 1024);
    assert(sortEng.getCurrentSyllableIndex() == 0);

    // Queue NoteOn(62) BEFORE NoteOff(60) at exact same sample offset 128
    BaburEngine::RawMidi touching[2] = {
        { 128, 0x90, 62, 100 },
        { 128, 0x80, 60, 0 }
    };
    sortEng.processRawMidi(touching, 2);
    sortEng.render(sL.data(), sR.data(), 1024);
    assert(sortEng.getCurrentSyllableIndex() == 1);

    // 3. Tight 55 ms Inter-Vowel Legato Bridge (FLOW vs STAC)
    auto runGapCheck = [&](float legatoParam, float& outGapRms, float& outNextNoteRms) {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setParam(PARAM_LEGATO, legatoParam);
        eng.setParam(PARAM_AUDIO_MODE, 0.0f);
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.70f);
        eng.setText("see you");

        std::vector<float> bufL(4096), bufR(4096);
        std::vector<float> gapL(2200), gapR(2200);
        BaburEngine::RawMidi n1{ 0, 0x90, 60, 85 };
        eng.processRawMidi(&n1, 1);
        eng.render(bufL.data(), bufR.data(), 4096);

        // Release Note 1 for ~50 ms (2200 samples) -> measure bridge window [900..2100] (~20..48 ms)
        BaburEngine::RawMidi off1{ 0, 0x80, 60, 0 };
        eng.processRawMidi(&off1, 1);
        eng.render(gapL.data(), gapR.data(), 2200);
        double gapSq = 0.0;
        for (int i = 900; i < 2100; ++i) gapSq += static_cast<double>(gapL[i]) * gapL[i];
        outGapRms = static_cast<float>(std::sqrt(gapSq / 1200.0));

        // Next note arrives inside 55 ms bridge
        BaburEngine::RawMidi n2{ 0, 0x90, 64, 85 };
        eng.processRawMidi(&n2, 1);
        eng.render(bufL.data(), bufR.data(), 4096);
        assert(eng.getCurrentSyllableIndex() == 1);
        float pk = 0.0f;
        checkFiniteAndPeak(bufL, bufR, pk);
        double n2Sq = 0.0;
        for (int i = 0; i < 4096; ++i) n2Sq += static_cast<double>(bufL[i]) * bufL[i];
        outNextNoteRms = static_cast<float>(std::sqrt(n2Sq / 4096.0));

        // Clean release after NoteOff + wait (~1.11s) reaches < 1e-4f
        BaburEngine::RawMidi off2{ 0, 0x80, 64, 0 };
        eng.processRawMidi(&off2, 1);
        for (int b = 0; b < 12; ++b) eng.render(bufL.data(), bufR.data(), 4096);
        float tailPeak = 0.0f;
        checkFiniteAndPeak(bufL, bufR, tailPeak);
        if (tailPeak >= 1e-4f) printf("      [DEBUG] legato=%.2f tailPeak=%.6f\n", legatoParam, tailPeak);
        assert(tailPeak < 1e-4f);
    };

    float flowGapRms = 0.0f, flowN2Rms = 0.0f;
    float stacGapRms = 0.0f, stacN2Rms = 0.0f;
    runGapCheck(0.00f, flowGapRms, flowN2Rms); // LEGATO_FLOW
    runGapCheck(0.50f, stacGapRms, stacN2Rms); // LEGATO_STAC
    printf("      -> Bridge Check: FLOW gap=%.4f, STAC gap=%.4f, N2=%.4f\n", flowGapRms, stacGapRms, flowN2Rms);
    assert(flowGapRms > 0.030f && flowGapRms < 0.160f);
    assert(stacGapRms > 0.030f);
    assert(flowN2Rms > 0.04f && stacN2Rms > 0.04f);

    // 4. Cross-Vowel Volume Consistency across /i, e, a, o, u/ + Beat-Synchronized Cluster Onset Timing
    const char* vowelWords[5] = { "see", "say", "are", "go", "you" };
    float minVowelRms = 1e9f, maxVowelRms = 0.0f;
    for (int v = 0; v < 5; ++v) {
        BaburEngine ve;
        ve.setSampleRate(44100.0f);
        assert(ve.loadSourceFile("bin/test_tone_a4.wav"));
        ve.setParam(PARAM_CHARACTER, 0.25f);
        ve.setParam(PARAM_DICTION, 0.65f);
        ve.setText(vowelWords[v]);
        std::vector<float> vL(8192), vR(8192);
        BaburEngine::RawMidi mn{ 0, 0x90, 60, 105 };
        ve.processRawMidi(&mn, 1);
        ve.render(vL.data(), vR.data(), 8192);
        double sq = 0.0;
        for (int i = 3000; i < 7000; ++i) sq += static_cast<double>(vL[i]) * vL[i];
        float rms = static_cast<float>(std::sqrt(sq / 4000.0));
        minVowelRms = std::min(minVowelRms, rms);
        maxVowelRms = std::max(maxVowelRms, rms);
    }
    float vowelRatio = maxVowelRms / std::max(1e-5f, minVowelRms);
    assert(vowelRatio < 1.55f);

    BaburEngine syncEng;
    syncEng.setSampleRate(44100.0f);
    assert(syncEng.loadSourceFile("bin/test_tone_a4.wav"));
    syncEng.setParam(PARAM_CHARACTER, 0.25f);
    syncEng.setParam(PARAM_DICTION, 0.65f);
    syncEng.setText("speak"); // 2-consonant cluster onset s+p must open vowel promptly inside [2000..3600]
    std::vector<float> spL(4096), spR(4096);
    BaburEngine::RawMidi spOn{ 0, 0x90, 60, 110 };
    syncEng.processRawMidi(&spOn, 1);
    syncEng.render(spL.data(), spR.data(), 4096);
    double spSq = 0.0;
    for (int i = 2000; i < 3600; ++i) spSq += static_cast<double>(spL[i]) * spL[i];
    float speakVowelRms = static_cast<float>(std::sqrt(spSq / 1600.0));
    assert(speakVowelRms > 0.05f);

    // 5. Pre-tract Speech Sustainer/Leveler keeps decaying pluck tails speaking during sustained vowels
    assert(writeTestPluckWavA4("bin/test_pluck_a4.wav"));
    BaburEngine pluckEng;
    pluckEng.setSampleRate(44100.0f);
    assert(pluckEng.loadSourceFile("bin/test_pluck_a4.wav"));
    pluckEng.setParam(PARAM_LEGATO, 0.00f);
    pluckEng.setParam(PARAM_AUDIO_MODE, 0.0f); // LOOP follows exponential decay of pluck WAV
    pluckEng.setParam(PARAM_CHARACTER, 0.30f);
    pluckEng.setText("sing");
    std::vector<float> pL(4096), pR(4096);
    BaburEngine::RawMidi pn{ 0, 0x90, 60, 110 };
    pluckEng.processRawMidi(&pn, 1);
    pluckEng.render(pL.data(), pR.data(), 4096); // 0..93 ms
    pluckEng.render(pL.data(), pR.data(), 4096); // 93..186 ms
    pluckEng.render(pL.data(), pR.data(), 4096); // 186..279 ms (deep in decay tail of pluck!)
    double tailHoldSq = 0.0;
    for (int i = 0; i < 4096; ++i) tailHoldSq += static_cast<double>(pL[i]) * pL[i];
    float tailHoldRms = static_cast<float>(std::sqrt(tailHoldSq / 4096.0));
    assert(tailHoldRms > 0.04f);

    // 6. English "motherfucker" (/A/ STRUT + dh + relaxed -er) & unlisted short-'u'/-er rule
    auto mf = TextPhonemeDissector::dissectText("motherfucker", false);
    assert(mf.size() == 4);
    assert(mf[0].display == "MO" && mf[0].vowel1 == V_A);
    assert(mf[1].display == "THER" && std::strcmp(mf[1].onset.label, "dh") == 0 && mf[1].vowel1 == V_I_DOTLESS);
    assert(mf[2].display == "FUC" && mf[2].vowel1 == V_A);
    assert(mf[3].display == "KER" && mf[3].vowel1 == V_I_DOTLESS);
    auto genU = TextPhonemeDissector::dissectText("stepmother drummer", false);
    assert(genU.size() == 5);
    assert(genU[1].vowel1 == V_A && genU[2].vowel1 == V_I_DOTLESS); // MO -> V_A, THER -> V_I_DOTLESS
    assert(genU[3].vowel1 == V_A && genU[4].vowel1 == V_I_DOTLESS); // DRUM -> V_A, MER -> V_I_DOTLESS

    // 7. Turkish final -s high-frequency sibilant coda energy (> 2000 Hz) + default LEGATO=HOLD
    BaburEngine trSib;
    trSib.setSampleRate(44100.0f);
    assert(trSib.loadSourceFile("bin/test_tone_a4.wav"));
    assert(BaburEngine::decodeLegatoMode(trSib.getParam(PARAM_LEGATO)) == LEGATO_HOLD);
    trSib.setParam(PARAM_LANG, 1.0f); // TR
    trSib.setParam(PARAM_DICTION, 0.70f);
    trSib.setText("kas ses");
    std::vector<float> sibL(4096), sibR(4096);
    BaburEngine::RawMidi trOn{ 0, 0x90, 60, 110 };
    trSib.processRawMidi(&trOn, 1);
    trSib.render(sibL.data(), sibR.data(), 4096);
    // Overlapping note glides pitch while holding syllable 0 under default LEGATO_HOLD
    BaburEngine::RawMidi trLeg{ 0, 0x90, 67, 110 };
    trSib.processRawMidi(&trLeg, 1);
    trSib.render(sibL.data(), sibR.data(), 2048);
    assert(trSib.getCurrentSyllableIndex() == 0);
    // Release both notes -> trigger coda /s/ of "kas"
    BaburEngine::RawMidi trOff1{ 0, 0x80, 60, 0 };
    BaburEngine::RawMidi trOff2{ 0, 0x80, 67, 0 };
    BaburEngine::RawMidi offs[2] = { trOff1, trOff2 };
    trSib.processRawMidi(offs, 2);
    trSib.render(sibL.data(), sibR.data(), 2048);
    float hpAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * 2000.0f / 44100.0f);
    float hpLp = 0.0f;
    double hiSq = 0.0;
    for (int i = 0; i < 1600; ++i) {
        hpLp += hpAlpha * (sibL[i] - hpLp);
        float hp = sibL[i] - hpLp;
        if (i >= 200 && i < 1400) hiSq += static_cast<double>(hp) * hp;
    }
    float trSibilantHiRms = static_cast<float>(std::sqrt(hiSq / 1200.0));
    assert(trSibilantHiRms > 0.012f);

    printf("      -> VowelRatio=%.2fx (min=%.3f max=%.3f) | SpeakSync=%.4f | PluckRMS=%.4f | TR_s_HiRMS=%.4f | MF=ok\n",
           vowelRatio, minVowelRms, maxVowelRms, speakVowelRms, tailHoldRms, trSibilantHiRms);
}

static float blockRms(const std::vector<float>& L, const std::vector<float>& R, size_t from, size_t to) {
    double sq = 0.0;
    for (size_t i = from; i < to; ++i) sq += 0.5 * (L[i] * L[i] + R[i] * R[i]);
    return static_cast<float>(std::sqrt(sq / static_cast<double>(std::max<size_t>(1, to - from))));
}

static void testDecayingCarrierLegatoAndSameKeyOverlap() {
    printf("[18/18] Testing decaying SF2 (piano) legato HOLD/FLOW + same-key overlap + limiter...\n");
    const std::string sf2Path = "..\\basicsf2daw\\assets\\TimGM6mb.sf2";
    auto setup = [&](BaburEngine& eng, float legato) -> bool {
        eng.setSampleRate(44100.0f);
        if (!eng.loadSourceFile(sf2Path)) {
            if (!eng.loadSourceFile("bin/test_tone_a4.wav")) return false;
        } else {
            eng.setPresetIndex(0); // Acoustic piano: strongly decaying carrier
        }
        eng.setParam(PARAM_LANG, 0.0f);
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.70f);
        eng.setParam(PARAM_EXCITER, 1.0f);
        eng.setParam(PARAM_LEGATO, legato);
        eng.setText("Hello world speak singing la la");
        return true;
    };
    const int N = 4096;
    std::vector<float> L(N), R(N);

    // HOLD: one long piano note decays, then a legato slur must keep singing at healthy level
    BaburEngine hold;
    assert(setup(hold, 1.0f));
    BaburEngine::RawMidi on1{ 0, 0x90, 60, 110 };
    hold.processRawMidi(&on1, 1);
    hold.render(L.data(), R.data(), N);
    float refRms = blockRms(L, R, 1024, N);
    for (int b = 0; b < 16; ++b) hold.render(L.data(), R.data(), N); // ~1.5 s of decay
    float worstHold = 1.0f;
    int keys[4] = { 62, 64, 65, 67 };
    int prevKey = 60;
    for (int k : keys) {
        BaburEngine::RawMidi ev[2] = { { 0, 0x90, static_cast<uint8_t>(k), 110 }, { 64, 0x80, static_cast<uint8_t>(prevKey), 0 } };
        hold.processRawMidi(ev, 2);
        hold.render(L.data(), R.data(), N);
        float pk = 0.0f;
        checkFiniteAndPeak(L, R, pk);
        assert(pk <= 0.97f);
        for (int b = 0; b < 4; ++b) hold.render(L.data(), R.data(), N);
        worstHold = std::min(worstHold, blockRms(L, R, 0, N) / std::max(1e-6f, refRms));
        prevKey = k;
    }
    assert(worstHold > 0.30f);

    // FLOW: overlapping notes on a decaying carrier — no note may come out skipped/quiet
    BaburEngine flow;
    assert(setup(flow, 0.0f));
    float worstFlow = 1.0f;
    prevKey = -1;
    int flowKeys[6] = { 60, 62, 64, 62, 60, 67 };
    for (int k : flowKeys) {
        if (prevKey < 0) {
            BaburEngine::RawMidi ev{ 0, 0x90, static_cast<uint8_t>(k), 110 };
            flow.processRawMidi(&ev, 1);
        } else {
            BaburEngine::RawMidi ev[2] = { { 0, 0x90, static_cast<uint8_t>(k), 110 }, { 32, 0x80, static_cast<uint8_t>(prevKey), 0 } };
            flow.processRawMidi(ev, 2);
        }
        float noteMax = 0.0f;
        for (int b = 0; b < 3; ++b) {
            flow.render(L.data(), R.data(), N);
            noteMax = std::max(noteMax, blockRms(L, R, 0, N));
        }
        worstFlow = std::min(worstFlow, noteMax / std::max(1e-6f, refRms));
        prevKey = k;
    }
    assert(worstFlow > 0.35f);

    // Same-key overlap: NoteOn 60, NoteOn 60, NoteOff 60 -> the second note is still held
    BaburEngine same;
    assert(setup(same, 1.0f));
    BaburEngine::RawMidi a{ 0, 0x90, 60, 110 };
    same.processRawMidi(&a, 1);
    same.render(L.data(), R.data(), N);
    BaburEngine::RawMidi bb[2] = { { 0, 0x90, 60, 110 }, { 16, 0x80, 60, 0 } };
    same.processRawMidi(bb, 2);
    same.render(L.data(), R.data(), N);
    same.render(L.data(), R.data(), N);
    float sameRms = blockRms(L, R, 0, N);
    assert(sameRms > 0.30f * refRms);

    printf("      -> HoldWorst=%.2fx | FlowWorst=%.2fx | SameKey=%.4f (ref %.4f)\n",
           worstHold, worstFlow, sameRms, refRms);
}

static void testSentenceContextAndArticulatoryPhysics() {
    printf("[19/19] Testing Sentence Context (Sandhi/Prosody) & 6-Articulator Coarticulation...\n");

    // 1. Turkish allophones & sandhi: open 'e' in "gel", n+b->m in "on bir", soft-g bridging
    auto gelGece = TextPhonemeDissector::dissectText("gel gece", true);
    assert(gelGece.size() == 3);
    assert(gelGece[0].contextF1 > 580.0f && gelGece[0].contextF1 > gelGece[1].contextF1 + 65.0f);

    auto onBir = TextPhonemeDissector::dissectText("on bir", true);
    assert(onBir.size() == 2);
    assert(std::strcmp(onBir[0].coda.label, "m") == 0);

    auto softG = TextPhonemeDissector::dissectText("de\xC4\x9F" "il a\xC4\x9F" "a\xC3\xA7", true);
    assert(softG.size() == 4);
    assert(softG[0].hasSoftGLink);
    assert(!softG[1].onsetCluster.empty() && std::strcmp(softG[1].onsetCluster[0].label, "y") == 0);
    assert(softG[2].hasSoftGLink);
    assert(softG[3].onsetCluster.empty());

    // 2. English sentence sandhi, function-word reduction & punctuation prosody
    auto sent = TextPhonemeDissector::dissectText("What a nice day? Yes, I did you see the apple.", false);
    auto findByDisp = [&](const char* disp) -> const SyllableToken* {
        for (const auto& s : sent) {
            if (s.display == disp) return &s;
        }
        return nullptr;
    };
    const SyllableToken* sWhat = findByDisp("WHAT");
    const SyllableToken* sA    = findByDisp("A");
    const SyllableToken* sDay  = findByDisp("DAY");
    const SyllableToken* sYes  = findByDisp("YES");
    const SyllableToken* sDid  = findByDisp("DID");
    const SyllableToken* sThe  = findByDisp("THE");
    assert(sWhat && sA && sDay && sYes && sDid && sThe && !sent.empty());

    assert(std::strcmp(sWhat->coda.label, "d") == 0 && sWhat->coda.type == C_PLOSIVE_VOICED);
    assert(std::strcmp(sDid->coda.label, "j") == 0);
    assert(sThe->vowel1 == V_I && sThe->vowel2 == V_I);
    assert(sA->isFunctionWord && sA->prominence < 0.85f);
    assert(sThe->isFunctionWord && sThe->prominence < 0.85f);
    assert(sDay->breakAfter == SyllableToken::BREAK_QUESTION && sDay->prosodyPitchSemis > 0.05f);
    assert(sYes->breakAfter == SyllableToken::BREAK_MINOR && sYes->prosodyPitchSemis > 0.02f);
    assert(sent.back().breakAfter == SyllableToken::BREAK_MAJOR && sent.back().prosodyPitchSemis < -0.04f);

    // 3. Emergent 6-Articulator coarticulation: lip-rounded "su" vs high-front "si" onset /s/ brightness
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));
    auto measureSibilantTilt = [&](const std::string& word) {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setParam(PARAM_LANG, 1.0f); // TR
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.75f);
        eng.setText(word);
        std::vector<float> l(2048), r(2048);
        BaburEngine::RawMidi on{ 0, 0x90, 60, 110 };
        eng.processRawMidi(&on, 1);
        eng.render(l.data(), r.data(), 2048);
        float pk = 0.0f;
        checkFiniteAndPeak(l, r, pk);
        assert(pk > 0.01f && pk <= 0.97f);

        float lp15A = 1.0f - std::exp(-2.0f * 3.14159265f * 1500.0f / 44100.0f);
        float lp45A = 1.0f - std::exp(-2.0f * 3.14159265f * 4500.0f / 44100.0f);
        float lp15 = 0.0f, lp45 = 0.0f;
        double midSq = 0.0, hiSq = 0.0;
        for (int i = 0; i < 900; ++i) {
            float x = l[i];
            lp15 += lp15A * (x - lp15);
            lp45 += lp45A * (x - lp45);
            float bMid = lp45 - lp15;
            float bHi  = x - lp45;
            if (i >= 120) {
                midSq += static_cast<double>(bMid) * bMid;
                hiSq  += static_cast<double>(bHi) * bHi;
            }
        }
        return static_cast<float>(std::sqrt((hiSq + 1e-12) / (midSq + 1e-12)));
    };
    float suTilt = measureSibilantTilt("su");
    float siTilt = measureSibilantTilt("si");
    assert(siTilt > suTilt * 1.15f);

    // 4. Connected multi-word English phrase "What a nice day?" across 4 notes in FLOW mode
    BaburEngine phr;
    phr.setSampleRate(44100.0f);
    assert(phr.loadSourceFile("bin/test_tone_a4.wav"));
    phr.setParam(PARAM_LANG, 0.0f);
    phr.setParam(PARAM_LEGATO, 0.0f); // FLOW
    phr.setParam(PARAM_CHARACTER, 0.28f);
    phr.setParam(PARAM_DICTION, 0.70f);
    phr.setText("What a nice day?");

    std::vector<float> pL(4096), pR(4096);
    float worstPhraseRms = 1.0f;
    int notes[4] = { 60, 62, 64, 65 };
    for (int nIdx = 0; nIdx < 4; ++nIdx) {
        if (nIdx == 0) {
            BaburEngine::RawMidi ev{ 0, 0x90, static_cast<uint8_t>(notes[nIdx]), 110 };
            phr.processRawMidi(&ev, 1);
        } else {
            BaburEngine::RawMidi evs[2] = {
                { 0, 0x90, static_cast<uint8_t>(notes[nIdx]), 110 },
                { 24, 0x80, static_cast<uint8_t>(notes[nIdx - 1]), 0 }
            };
            phr.processRawMidi(evs, 2);
        }
        float noteMaxRms = 0.0f;
        for (int b = 0; b < 2; ++b) {
            phr.render(pL.data(), pR.data(), 4096);
            float pk = 0.0f;
            checkFiniteAndPeak(pL, pR, pk);
            assert(pk <= 0.97f);
            noteMaxRms = std::max(noteMaxRms, blockRms(pL, pR, 0, 4096));
        }
        assert(noteMaxRms > 0.03f);
        worstPhraseRms = std::min(worstPhraseRms, noteMaxRms);
    }
    BaburEngine::RawMidi lastOff{ 0, 0x80, 65, 0 };
    phr.processRawMidi(&lastOff, 1);
    for (int b = 0; b < 12; ++b) phr.render(pL.data(), pR.data(), 4096);
    float relPeak = 0.0f;
    checkFiniteAndPeak(pL, pR, relPeak);
    assert(relPeak < 1e-4f);

    printf("      -> gelF1=%.0f vs geceF1=%.0f | on_bir=%s | si/su_s_tilt=%.2f/%.2f | phraseMinRMS=%.4f\n",
           gelGece[0].contextF1, gelGece[1].contextF1, onBir[0].coda.label, siTilt, suTilt, worstPhraseRms);
}

static void testLivingThroatPlosivesTempoLexiconEmotion() {
    printf("[20/20] Testing Lexicon, TR Stress, Emotion Tags, Plosive Pop/F1 Cutback, Tap r & Tempo FLOW...\n");

    // 1. Embedded English Lexicon strict ASCII sort order & tricky irregular pronunciations
    assert(kEnglishLexiconSize >= 350);
    for (size_t i = 1; i < kEnglishLexiconSize; ++i) {
        assert(std::strcmp(kEnglishLexicon[i - 1].word, kEnglishLexicon[i].word) < 0);
    }
    const char* tricky[] = {
        "though", "through", "thought", "enough", "laugh", "heart", "heard", "women", "choir", "island", "chaos"
    };
    for (const char* w : tricky) {
        assert(findEnglishLexiconWord(w) != nullptr);
    }
    auto ex1 = TextPhonemeDissector::dissectText("though through heart island chaos", false);
    assert(ex1.size() == 7); // THOUGH(1) + THROUGH(1) + HEART(1) + AI+LAND(2) + KAY+OS(2)
    assert(ex1[0].vowel1 == V_O && ex1[0].isDiphthong);   // though -> /oU/
    assert(ex1[1].vowel1 == V_U && !ex1[1].isDiphthong);  // through -> /u/
    assert(ex1[2].vowel1 == V_A && ex1[2].rColored);      // heart -> /ar/
    assert(ex1[3].display == "AI" && ex1[3].vowel1 == V_A && ex1[3].isDiphthong); // island
    assert(ex1[5].display == "KAY");                      // chaos

    // 2. Turkish Grammatical Stress Rules (negative -me, progressive -yor, question mi?, istanbul, türkiye)
    auto trNeg = TextPhonemeDissector::dissectText("gelme", true);
    assert(trNeg.size() == 2 && trNeg[0].isStressed && !trNeg[1].isStressed);
    auto trProg = TextPhonemeDissector::dissectText("geliyorum", true);
    assert(trProg.size() == 4 && trProg[1].isStressed && !trProg[2].isStressed); // GE-Lİ-YO-RUM
    auto trQ = TextPhonemeDissector::dissectText("geldi mi?", true);
    assert(trQ.size() == 3 && trQ[1].isStressed && trQ[2].isFunctionWord && !trQ[2].isStressed);
    auto trCity = TextPhonemeDissector::dissectText("istanbul t\xC3\xBCrkiye", true);
    assert(trCity.size() == 6);
    assert(!trCity[0].isStressed && trCity[1].isStressed && !trCity[2].isStressed); // is-TAN-bul
    assert(trCity[3].isStressed && !trCity[4].isStressed && !trCity[5].isStressed); // TÜR-ki-ye

    // 3. Inline Emotion Tags & *word* Emphasis (must never emit bracket words as syllables)
    auto emoS = TextPhonemeDissector::dissectText("[sad] hello [angry] *world* [mutlu] day", false);
    assert(emoS.size() == 4); // HEL-LO + WORLD + DAY
    assert(emoS[0].emotion == EMO_SAD && emoS[1].emotion == EMO_SAD);
    assert(emoS[2].emotion == EMO_ANGRY && emoS[2].isEmphasized && emoS[2].prominence > 1.20f);
    assert(emoS[3].emotion == EMO_HAPPY && !emoS[3].isEmphasized);

    // Acoustic brightness contrast: [angry] vs [soft] on same word & MIDI
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));
    auto measureBrightness = [&](const std::string& txt) {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.70f);
        eng.setText(txt);
        std::vector<float> L(6144), R(6144);
        BaburEngine::RawMidi on{ 0, 0x90, 60, 108 };
        eng.processRawMidi(&on, 1);
        eng.render(L.data(), R.data(), 6144);
        float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 1200.0f / 44100.0f);
        float lp = 0.0f;
        double loSq = 0.0, hiSq = 0.0;
        for (int i = 0; i < 6144; ++i) {
            lp += lpA * (L[i] - lp);
            if (i >= 2400) {
                float hi = L[i] - lp;
                loSq += static_cast<double>(lp) * lp;
                hiSq += static_cast<double>(hi) * hi;
            }
        }
        return static_cast<float>(std::sqrt((hiSq + 1e-12) / (loSq + 1e-12)));
    };
    float bAngry = measureBrightness("[angry] hello");
    float bSoft  = measureBrightness("[soft] hello");
    assert(bAngry > bSoft * 1.15f);

    // 4. Plosive Physics (Pop + F1 Cutback: "pa" vs "ba") & Ballistic Turkish Tapped r ("ara")
    auto measureBurstRatio = [&](const std::string& syl) {
        BaburEngine pe;
        pe.setSampleRate(44100.0f);
        assert(pe.loadSourceFile("bin/test_tone_a4.wav"));
        pe.setParam(PARAM_CHARACTER, 0.25f);
        pe.setParam(PARAM_DICTION, 0.85f);
        pe.setText(syl);
        std::vector<float> L(4096), R(4096);
        BaburEngine::RawMidi on{ 0, 0x90, 57, 112 };
        pe.processRawMidi(&on, 1);
        pe.render(L.data(), R.data(), 4096);
        float lpLoA = 1.0f - std::exp(-2.0f * 3.14159265f * 450.0f / 44100.0f);
        float lpHiA = 1.0f - std::exp(-2.0f * 3.14159265f * 1000.0f / 44100.0f);
        float lpLo = 0.0f, lpHi = 0.0f;
        double lfSq = 0.0, hfSq = 0.0;
        for (int i = 0; i < 1600; ++i) {
            lpLo += lpLoA * (L[i] - lpLo);
            lpHi += lpHiA * (L[i] - lpHi);
            if (i >= 650 && i < 1450) {
                float hf = L[i] - lpHi;
                lfSq += static_cast<double>(lpLo) * lpLo;
                hfSq += static_cast<double>(hf) * hf;
            }
        }
        return static_cast<float>(std::sqrt((hfSq + 1e-12) / (lfSq + 1e-12)));
    };
    float paCut = measureBurstRatio("pa");
    float baCut = measureBurstRatio("ba");
    assert(paCut > baCut * 1.35f);

    BaburEngine tapEng;
    tapEng.setSampleRate(44100.0f);
    assert(tapEng.loadSourceFile("bin/test_tone_a4.wav"));
    tapEng.setParam(PARAM_LANG, 1.0f);
    tapEng.setParam(PARAM_LEGATO, 0.0f); // FLOW so legato overlap advances to "ra"
    tapEng.setParam(PARAM_CHARACTER, 0.25f);
    tapEng.setParam(PARAM_DICTION, 0.80f);
    tapEng.setText("a ra");
    std::vector<float> tL(8192), tR(8192);
    BaburEngine::RawMidi t1{ 0, 0x90, 60, 110 };
    tapEng.processRawMidi(&t1, 1);
    tapEng.render(tL.data(), tR.data(), 4096);
    float aRms = blockRms(tL, tR, 1500, 3800);
    BaburEngine::RawMidi t2[2] = { { 0, 0x90, 62, 110 }, { 20, 0x80, 60, 0 } };
    tapEng.processRawMidi(t2, 2);
    tapEng.render(tL.data(), tR.data(), 8192);
    assert(tapEng.getCurrentSyllableIndex() == 1);
    float tapDipRms = blockRms(tL, tR, 250, 750);
    float tapVowRms = blockRms(tL, tR, 3000, 7000);
    assert(aRms > 0.03f && tapDipRms < tapVowRms * 0.72f);

    // 5. Tempo-Adaptive Connected FLOW ("Ben seninle gelmek istiyorum") at Fast 160ms & Slow 480ms
    auto runCadence = [&](int stepSamples) {
        BaburEngine fe;
        fe.setSampleRate(44100.0f);
        assert(fe.loadSourceFile("bin/test_tone_a4.wav"));
        fe.setParam(PARAM_LANG, 1.0f); // TR
        fe.setParam(PARAM_LEGATO, 0.0f); // FLOW
        fe.setParam(PARAM_CHARACTER, 0.28f);
        fe.setParam(PARAM_DICTION, 0.72f);
        fe.setText("Ben seninle gelmek istiyorum");
        const int numNotes = 8;
        int mel[numNotes] = { 60, 62, 64, 65, 64, 62, 64, 67 };
        std::vector<float> blkL(static_cast<size_t>(stepSamples)), blkR(static_cast<size_t>(stepSamples));
        float minRms = 1.0f;
        for (int k = 0; k < numNotes; ++k) {
            if (k == 0) {
                BaburEngine::RawMidi ev{ 0, 0x90, static_cast<uint8_t>(mel[k]), 110 };
                fe.processRawMidi(&ev, 1);
            } else {
                BaburEngine::RawMidi evs[2] = {
                    { 0, 0x90, static_cast<uint8_t>(mel[k]), 110 },
                    { 16, 0x80, static_cast<uint8_t>(mel[k - 1]), 0 }
                };
                fe.processRawMidi(evs, 2);
            }
            fe.render(blkL.data(), blkR.data(), stepSamples);
            float pk = 0.0f;
            checkFiniteAndPeak(blkL, blkR, pk);
            assert(pk <= 0.97f);
            float rms = blockRms(blkL, blkR, 0, static_cast<size_t>(stepSamples));
            minRms = std::min(minRms, rms);
        }
        BaburEngine::RawMidi off{ 0, 0x80, static_cast<uint8_t>(mel[numNotes - 1]), 0 };
        fe.processRawMidi(&off, 1);
        std::vector<float> relL(4096), relR(4096);
        for (int b = 0; b < 12; ++b) fe.render(relL.data(), relR.data(), 4096);
        float tailPk = 0.0f;
        checkFiniteAndPeak(relL, relR, tailPk);
        assert(tailPk < 1e-4f);
        return minRms;
    };
    float fastMinRms = runCadence(7056);  // ~160 ms fast melody
    float slowMinRms = runCadence(21168); // ~480 ms slow melody
    assert(fastMinRms > 0.030f && slowMinRms > 0.030f);

    printf("      -> Lexicon=%zu (sorted) | TRStress=ok | Emo(ang/soft=%.2f/%.2f) | pa/ba=%.2f/%.2f | tapR=%.3f/%.3f | FlowFast/Slow=%.3f/%.3f\n",
           kEnglishLexiconSize, bAngry, bSoft, paCut, baCut, tapDipRms, tapVowRms, fastMinRms, slowMinRms);
}

static void testDistinctEmotionsAndIntraWordContinuity() {
    printf("[21/21] Testing 6-Emotion Physiology (Non-Volume Contrast) & Intra-Word Continuity...\n");
    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));

    struct EmoStats { float rms; float bright; };
    auto measureEmo = [&](const std::string& tagWord) -> EmoStats {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.70f);
        eng.setText(tagWord);
        std::vector<float> L(12288), R(12288);
        BaburEngine::RawMidi on{ 0, 0x90, 60, 108 };
        eng.processRawMidi(&on, 1);
        eng.render(L.data(), R.data(), 12288);
        float pk = 0.0f;
        checkFiniteAndPeak(L, R, pk);
        assert(pk > 0.02f && pk <= 0.97f);
        float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 1200.0f / 44100.0f);
        float lp = 0.0f;
        double loSq = 0.0, hiSq = 0.0;
        for (int i = 0; i < 11500; ++i) {
            lp += lpA * (L[i] - lp);
            if (i >= 4000) {
                float hi = L[i] - lp;
                loSq += static_cast<double>(lp) * lp;
                hiSq += static_cast<double>(hi) * hi;
            }
        }
        return {
            blockRms(L, R, 4000, 11500),
            static_cast<float>(std::sqrt((hiSq + 1e-12) / (loSq + 1e-12)))
        };
    };

    EmoStats stNeu = measureEmo("[neutral] hello");
    EmoStats stAng = measureEmo("[angry] hello");
    EmoStats stSad = measureEmo("[sad] hello");
    EmoStats stSft = measureEmo("[soft] hello");
    EmoStats stHap = measureEmo("[happy] hello");
    EmoStats stExc = measureEmo("[excited] hello");

    float minEmoRms = std::min({ stNeu.rms, stAng.rms, stSad.rms, stSft.rms, stHap.rms, stExc.rms });
    float maxEmoRms = std::max({ stNeu.rms, stAng.rms, stSad.rms, stSft.rms, stHap.rms, stExc.rms });
    assert(maxEmoRms / std::max(1e-5f, minEmoRms) < 1.40f);
    assert(stAng.bright > stSft.bright * 1.45f);
    assert(stHap.bright > stSad.bright * 1.25f);

    // Early pitch scoop gesture & 72 Hz ventricular roughness contrast on sustained vowel "a"
    auto measurePitchAndRoughness = [&](const std::string& tag, float& outEarlyF0, float& outMidF0, float& outRough) {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.65f);
        eng.setText(tag + " a");
        std::vector<float> L(12288), R(12288);
        BaburEngine::RawMidi on{ 0, 0x90, 60, 108 };
        eng.processRawMidi(&on, 1);
        eng.render(L.data(), R.data(), 12288);

        float svfG = std::tan(3.14159265358979323846f * (261.63f / 44100.0f));
        float svfK = 1.0f / 3.5f;
        float svfA1 = 1.0f / (1.0f + svfG * (svfG + svfK));
        float svfA2 = svfG * svfA1, svfA3 = svfG * svfA2;
        float ic1 = 0.0f, ic2 = 0.0f;
        std::vector<float> fund(10000);
        for (int i = 0; i < 10000; ++i) {
            float v3 = L[i] - ic2;
            float v1 = svfA1 * ic1 + svfA2 * v3;
            float v2 = ic2 + svfA2 * ic1 + svfA3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            fund[i] = v1;
        }
        auto estF0 = [&](int from, int to) {
            int crossings = 0;
            double firstT = 0.0, lastT = 0.0;
            for (int i = from + 1; i < to; ++i) {
                if (fund[i - 1] <= 0.0f && fund[i] > 0.0f) {
                    double frac = static_cast<double>(-fund[i - 1]) / std::max(1e-9, static_cast<double>(fund[i] - fund[i - 1]));
                    double t = static_cast<double>(i - 1) + frac;
                    if (crossings == 0) firstT = t;
                    lastT = t;
                    ++crossings;
                }
            }
            assert(crossings >= 4);
            return static_cast<float>((crossings - 1) * 44100.0 / std::max(1.0, lastT - firstT));
        };
        outEarlyF0 = estF0(300, 1550);
        outMidF0   = estF0(2800, 5200);

        double c72 = 0.0, s72 = 0.0, meanEnv = 0.0;
        for (int i = 4800; i < 11500; ++i) {
            double ax = std::fabs(static_cast<double>(L[i]));
            double ph = 2.0 * 3.14159265358979323846 * 72.0 * static_cast<double>(i) / 44100.0;
            c72 += ax * std::cos(ph);
            s72 += ax * std::sin(ph);
            meanEnv += ax;
        }
        outRough = static_cast<float>((2.0 * std::sqrt(c72 * c72 + s72 * s72)) / (meanEnv + 1e-12));
    };

    float sadE = 0.0f, sadM = 0.0f, sadR = 0.0f;
    float angE = 0.0f, angM = 0.0f, angR = 0.0f;
    float sftE = 0.0f, sftM = 0.0f, sftR = 0.0f;
    measurePitchAndRoughness("[sad]", sadE, sadM, sadR);
    measurePitchAndRoughness("[angry]", angE, angM, angR);
    measurePitchAndRoughness("[soft]", sftE, sftM, sftR);
    assert(sadE < sadM * 0.992f);
    assert(angE > angM * 1.005f);
    assert(angR > sftR * 1.35f);

    // 2. Intra-word vowel-to-vowel continuity: connected FLOW medial transition ("geliyorum") & 55ms mid-word bridge
    BaburEngine flowW;
    flowW.setSampleRate(44100.0f);
    assert(flowW.loadSourceFile("bin/test_tone_a4.wav"));
    flowW.setParam(PARAM_LANG, 1.0f); // TR
    flowW.setParam(PARAM_LEGATO, 0.0f); // FLOW
    flowW.setParam(PARAM_CHARACTER, 0.25f);
    flowW.setParam(PARAM_DICTION, 0.72f);
    flowW.setText("geliyorum");
    std::vector<float> bL(4096), bR(4096);
    BaburEngine::RawMidi w1{ 0, 0x90, 60, 110 };
    flowW.processRawMidi(&w1, 1);
    flowW.render(bL.data(), bR.data(), 4096);
    float geVowelRms = blockRms(bL, bR, 2000, 3800);
    BaburEngine::RawMidi w2[2] = { { 0, 0x90, 62, 110 }, { 16, 0x80, 60, 0 } };
    flowW.processRawMidi(w2, 2);
    flowW.render(bL.data(), bR.data(), 4096);
    assert(flowW.getCurrentSyllableIndex() == 1);
    float midWordTransRms = blockRms(bL, bR, 150, 1200);
    assert(midWordTransRms > geVowelRms * 0.48f);

    auto measureGap = [&](const std::string& txt, bool isTr) {
        BaburEngine ge;
        ge.setSampleRate(44100.0f);
        assert(ge.loadSourceFile("bin/test_tone_a4.wav"));
        ge.setParam(PARAM_LANG, isTr ? 1.0f : 0.0f);
        ge.setParam(PARAM_LEGATO, 0.0f); // FLOW
        ge.setParam(PARAM_AUDIO_MODE, 0.0f); // LOOP (direct carrier env decay check)
        ge.setParam(PARAM_CHARACTER, 0.25f);
        ge.setParam(PARAM_DICTION, 0.70f);
        ge.setText(txt);
        std::vector<float> gL(4096), gR(4096), gapL(2400), gapR(2400);
        BaburEngine::RawMidi n1{ 0, 0x90, 60, 96 };
        ge.processRawMidi(&n1, 1);
        ge.render(gL.data(), gR.data(), 4096);
        BaburEngine::RawMidi o1{ 0, 0x80, 60, 0 };
        ge.processRawMidi(&o1, 1);
        ge.render(gapL.data(), gapR.data(), 2400);
        float gapRms = blockRms(gapL, gapR, 900, 2100);
        BaburEngine::RawMidi n2{ 0, 0x90, 62, 96 };
        ge.processRawMidi(&n2, 1);
        ge.render(gL.data(), gR.data(), 4096);
        BaburEngine::RawMidi o2{ 0, 0x80, 62, 0 };
        ge.processRawMidi(&o2, 1);
        for (int b = 0; b < 12; ++b) ge.render(gL.data(), gR.data(), 4096);
        float tailPk = 0.0f;
        checkFiniteAndPeak(gL, gR, tailPk);
        assert(tailPk < 1e-4f);
        return gapRms;
    };
    float midWordGapRms = measureGap("geliyorum", true);
    float wordBoundaryGapRms = measureGap("see you", false);
    assert(midWordGapRms > wordBoundaryGapRms * 1.30f && midWordGapRms > 0.055f);

    printf("      -> 6-Emo RMS=[%.3f..%.3f] | Pitch(sad=%.1f->%.1f, ang=%.1f->%.1f) | Rough(ang/soft=%.3f/%.3f) | MidTrans=%.3f/%.3f | MidGap=%.3f vs WordGap=%.3f\n",
           minEmoRms, maxEmoRms, sadE, sadM, angE, angM, angR, sftR, midWordTransRms, geVowelRms, midWordGapRms, wordBoundaryGapRms);
}

static void testVoiceCharacteristicsAndBackwardCompat() {
    printf("[22/22] Testing 10 Voice Characteristics (Gender, PitchShift, Breath, Grit, Nasal, Bright, Vib, Tremor, Robot) & Chunk Compat...\n");

    // 1. Default values of all 10 new parameters preserve identical baseline behavior
    BaburEngine defEng;
    assert(std::fabs(defEng.getParam(PARAM_GENDER)      - 0.50f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_PITCH_SHIFT) - 0.50f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_BREATHINESS) - 0.00f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_GRIT)        - 0.00f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_NASALITY)    - 0.00f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_BRIGHTNESS)  - 0.50f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_VIB_DEPTH)   - 0.25f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_VIB_RATE)    - (5.2f - 2.5f) / 6.5f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_TREMOR)      - 0.00f) < 1e-4f);
    assert(std::fabs(defEng.getParam(PARAM_ROBOTIC)     - 0.00f) < 1e-4f);

    // 2. Legacy 8-param Chunk v1 backward compatibility & 18-param 'VOIC' chunk round-trip
    std::vector<uint8_t> legacyChunk;
    auto pushU32 = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) legacyChunk.push_back(static_cast<uint8_t>((v >> (b * 8)) & 0xFFu));
    };
    auto pushF32 = [&](float f) {
        uint32_t u = 0; std::memcpy(&u, &f, 4); pushU32(u);
    };
    legacyChunk.push_back('S'); legacyChunk.push_back('N'); legacyChunk.push_back('G'); legacyChunk.push_back('R');
    pushU32(1u);
    const float legBase[8] = { 0.25f, 0.70f, 0.55f, 0.30f, 1.00f, 0.00f, 1.00f, 0.70f };
    for (float f : legBase) pushF32(f);
    pushU32(0u); // preset
    pushU32(0u); // sylIdx
    pushU32(0u); // empty source path
    const char* legTxt = "hello";
    pushU32(5u);
    legacyChunk.insert(legacyChunk.end(), legTxt, legTxt + 5);
    pushU32(static_cast<uint32_t>(-1)); // resetMidiKey

    BaburEngine legRestore;
    for (int i = 8; i < NUM_PARAMS; ++i) legRestore.setParam(i, 0.91f);
    legRestore.restoreState(legacyChunk.data(), legacyChunk.size());
    for (int i = 0; i < 8; ++i) assert(std::fabs(legRestore.getParam(i) - legBase[i]) < 1e-5f);
    assert(std::fabs(legRestore.getParam(PARAM_GENDER)      - 0.50f) < 1e-5f);
    assert(std::fabs(legRestore.getParam(PARAM_PITCH_SHIFT) - 0.50f) < 1e-5f);
    assert(std::fabs(legRestore.getParam(PARAM_BREATHINESS) - 0.00f) < 1e-5f);
    assert(std::fabs(legRestore.getParam(PARAM_ROBOTIC)     - 0.00f) < 1e-5f);

    BaburEngine rtA, rtB;
    rtA.setParam(PARAM_GENDER, 0.88f);
    rtA.setParam(PARAM_PITCH_SHIFT, 0.75f);
    rtA.setParam(PARAM_BREATHINESS, 0.60f);
    rtA.setParam(PARAM_GRIT, 0.45f);
    rtA.setParam(PARAM_NASALITY, 0.50f);
    rtA.setParam(PARAM_BRIGHTNESS, 0.80f);
    rtA.setParam(PARAM_VIB_DEPTH, 0.75f);
    rtA.setParam(PARAM_VIB_RATE, 0.80f);
    rtA.setParam(PARAM_TREMOR, 0.40f);
    rtA.setParam(PARAM_ROBOTIC, 0.35f);
    std::vector<uint8_t> extChunk = rtA.saveState();
    rtB.restoreState(extChunk.data(), extChunk.size());
    for (int i = 0; i < NUM_PARAMS; ++i) {
        assert(std::fabs(rtA.getParam(i) - rtB.getParam(i)) < 1e-5f);
    }

    assert(writeTestWavA4("bin/test_tone_a4.wav", 0.60f, 44100.0f, 12));
    auto renderCustom = [&](auto setupFn, int frames = 12288, const std::string& text = "a") {
        BaburEngine eng;
        eng.setSampleRate(44100.0f);
        assert(eng.loadSourceFile("bin/test_tone_a4.wav"));
        eng.setParam(PARAM_CHARACTER, 0.25f);
        eng.setParam(PARAM_DICTION, 0.50f);
        eng.setText(text);
        setupFn(eng);
        std::vector<float> L(static_cast<size_t>(frames)), R(static_cast<size_t>(frames));
        BaburEngine::RawMidi on{ 0, 0x90, 60, 108 };
        eng.processRawMidi(&on, 1);
        eng.render(L.data(), R.data(), frames);
        float pk = 0.0f;
        checkFiniteAndPeak(L, R, pk);
        assert(pk > 0.02f && pk <= 0.97f);
        return L;
    };

    auto measureBright1200 = [&](const std::vector<float>& L, int from, int to) {
        float lpA = 1.0f - std::exp(-2.0f * 3.14159265f * 1200.0f / 44100.0f);
        float lp = 0.0f;
        double loSq = 0.0, hiSq = 0.0;
        for (int i = 0; i < to; ++i) {
            lp += lpA * (L[static_cast<size_t>(i)] - lp);
            if (i >= from) {
                float hi = L[static_cast<size_t>(i)] - lp;
                loSq += static_cast<double>(lp) * lp;
                hiSq += static_cast<double>(hi) * hi;
            }
        }
        return static_cast<float>(std::sqrt((hiSq + 1e-12) / (loSq + 1e-12)));
    };

    // 3. Gender / Age anatomy contrast (+80% Female/Child vs -80% Deep Bass/Giant)
    auto femL  = renderCustom([](BaburEngine& e) { e.setParam(PARAM_GENDER, 0.90f); });
    auto bassL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_GENDER, 0.10f); });
    float brightFem  = measureBright1200(femL,  3600, 10000);
    float brightBass = measureBright1200(bassL, 3600, 10000);
    assert(brightFem > brightBass * 1.65f);

    // 4. Pitch Shift (+12 st / 0 st / -12 st) register tracking
    auto estSvfPitch = [&](const std::vector<float>& L, float expectedHz, int from, int to) {
        float svfG = std::tan(3.14159265358979323846f * (expectedHz / 44100.0f));
        float svfK = 1.0f / 4.0f;
        float svfA1 = 1.0f / (1.0f + svfG * (svfG + svfK));
        float svfA2 = svfG * svfA1, svfA3 = svfG * svfA2;
        float ic1 = 0.0f, ic2 = 0.0f;
        float ic1b = 0.0f, ic2b = 0.0f;
        std::vector<float> fund(static_cast<size_t>(to));
        for (int i = 0; i < to; ++i) {
            float v3 = L[static_cast<size_t>(i)] - ic2;
            float v1 = svfA1 * ic1 + svfA2 * v3;
            float v2 = ic2 + svfA2 * ic1 + svfA3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            float v3b = v1 - ic2b;
            float v1b = svfA1 * ic1b + svfA2 * v3b;
            float v2b = ic2b + svfA2 * ic1b + svfA3 * v3b;
            ic1b = 2.0f * v1b - ic1b;
            ic2b = 2.0f * v2b - ic2b;
            fund[static_cast<size_t>(i)] = v1b;
        }
        int crossings = 0;
        double firstT = 0.0, lastT = 0.0;
        for (int i = from + 1; i < to; ++i) {
            if (fund[static_cast<size_t>(i - 1)] <= 0.0f && fund[static_cast<size_t>(i)] > 0.0f) {
                double frac = static_cast<double>(-fund[static_cast<size_t>(i - 1)]) /
                              std::max(1e-9, static_cast<double>(fund[static_cast<size_t>(i)] - fund[static_cast<size_t>(i - 1)]));
                double t = static_cast<double>(i - 1) + frac;
                if (crossings == 0) firstT = t;
                lastT = t;
                ++crossings;
            }
        }
        assert(crossings >= 4);
        return static_cast<float>((crossings - 1) * 44100.0 / std::max(1.0, lastT - firstT));
    };
    auto psZeroL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_PITCH_SHIFT, 0.50f); }, 8192);
    auto psUpL   = renderCustom([](BaburEngine& e) { e.setParam(PARAM_PITCH_SHIFT, 0.75f); }, 8192);
    auto psDnL   = renderCustom([](BaburEngine& e) { e.setParam(PARAM_PITCH_SHIFT, 0.25f); }, 8192);
    float f0Zero = estSvfPitch(psZeroL, 261.63f, 2000, 7200);
    float f0Up   = estSvfPitch(psUpL,   523.25f, 2000, 7200);
    float f0Dn   = estSvfPitch(psDnL,   130.81f, 2000, 7200);
    assert(f0Up > f0Zero * 1.90f && f0Up < f0Zero * 2.10f);
    assert(f0Dn < f0Zero * 0.53f && f0Dn > f0Zero * 0.47f);

    // 5. Brightness (+100% vs -100%)
    auto brHiL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_BRIGHTNESS, 1.00f); });
    auto brLoL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_BRIGHTNESS, 0.00f); });
    float brightHi = measureBright1200(brHiL, 3600, 10000);
    float brightLo = measureBright1200(brLoL, 3600, 10000);
    assert(brightHi > brightLo * 1.60f);

    // 6. Grit / Fry (coherent 72 Hz ventricular roughness; DC-subtracted envelope lock-in)
    auto lockInDepth = [&](const std::vector<float>& L, float modHz, int from, int to) {
        double meanEnv = 0.0;
        for (int i = from; i < to; ++i) {
            meanEnv += std::fabs(static_cast<double>(L[static_cast<size_t>(i)]));
        }
        double mean = meanEnv / std::max(1, to - from);
        double c = 0.0, s = 0.0;
        for (int i = from; i < to; ++i) {
            double ax = std::fabs(static_cast<double>(L[static_cast<size_t>(i)])) - mean;
            double ph = 2.0 * 3.14159265358979323846 * static_cast<double>(modHz) * static_cast<double>(i) / 44100.0;
            c += ax * std::cos(ph);
            s += ax * std::sin(ph);
        }
        return static_cast<float>((2.0 * std::sqrt(c * c + s * s)) / (meanEnv + 1e-12));
    };
    auto gritL  = renderCustom([](BaburEngine& e) { e.setParam(PARAM_GRIT, 1.00f); });
    auto cleanL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_GRIT, 0.00f); });
    float grit72  = lockInDepth(gritL,  72.0f, 4800, 11500);
    float clean72 = lockInDepth(cleanL, 72.0f, 4800, 11500);
    assert(grit72 > clean72 * 1.45f);

    // 7. Nasality (nasal murmur <450 Hz vs carved oral F1/F2 600..1500 Hz ratio)
    auto measureNasalRatio = [&](const std::vector<float>& L) {
        float a450 = 1.0f - std::exp(-2.0f * 3.14159265f * 450.0f / 44100.0f);
        float a600 = 1.0f - std::exp(-2.0f * 3.14159265f * 600.0f / 44100.0f);
        float a1500 = 1.0f - std::exp(-2.0f * 3.14159265f * 1500.0f / 44100.0f);
        float lp450 = 0.0f, lp600 = 0.0f, lp1500 = 0.0f;
        double loSq = 0.0, midSq = 0.0;
        for (int i = 0; i < 10000; ++i) {
            float x = L[static_cast<size_t>(i)];
            lp450  += a450  * (x - lp450);
            lp600  += a600  * (x - lp600);
            lp1500 += a1500 * (x - lp1500);
            if (i >= 3600) {
                float mid = lp1500 - lp600;
                loSq  += static_cast<double>(lp450) * lp450;
                midSq += static_cast<double>(mid)   * mid;
            }
        }
        return static_cast<float>(std::sqrt((loSq + 1e-12) / (midSq + 1e-12)));
    };
    auto nasL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_NASALITY, 1.00f); });
    float nasRatio1 = measureNasalRatio(nasL);
    float nasRatio0 = measureNasalRatio(cleanL);
    assert(nasRatio1 > nasRatio0 * 1.35f);

    // 8. Breathiness (2-period lag autocorrelation aligns both F0 & F0/2 chest subharmonic; whisper noise drops it sharply)
    auto maxPeriodCorr = [&](const std::vector<float>& L) {
        float best = 0.0f;
        for (int lag = 334; lag <= 340; ++lag) {
            double num = 0.0, s1 = 0.0, s2 = 0.0;
            for (int i = 4500; i < 10000; ++i) {
                double a = L[static_cast<size_t>(i)];
                double b = L[static_cast<size_t>(i - lag)];
                num += a * b; s1 += a * a; s2 += b * b;
            }
            float corr = static_cast<float>(num / (std::sqrt(s1 * s2) + 1e-12));
            best = std::max(best, corr);
        }
        return best;
    };
    auto breathL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_BREATHINESS, 1.00f); });
    float corrClean  = maxPeriodCorr(cleanL);
    float corrBreath = maxPeriodCorr(breathL);

    // 9. Separated Age / Tremor (~5.8 Hz amplitude flutter) vs Clean
    auto tremL  = renderCustom([](BaburEngine& e) { e.setParam(PARAM_TREMOR, 1.00f); }, 24576);
    auto noTrem = renderCustom([](BaburEngine& e) { e.setParam(PARAM_TREMOR, 0.00f); }, 24576);
    float tremDepth  = std::max({ lockInDepth(tremL,  5.4f, 4000, 24000),
                                  lockInDepth(tremL,  5.8f, 4000, 24000),
                                  lockInDepth(tremL,  6.2f, 4000, 24000) });
    float cleanTrem  = std::max({ lockInDepth(noTrem, 5.4f, 4000, 24000),
                                  lockInDepth(noTrem, 5.8f, 4000, 24000),
                                  lockInDepth(noTrem, 6.2f, 4000, 24000) });

    // 10. Separated Robotic (strips [sad] crying pitch scoop -> rock-flat F0)
    auto robSadL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_ROBOTIC, 1.00f); }, 8192, "[sad] a");
    auto humSadL = renderCustom([](BaburEngine& e) { e.setParam(PARAM_ROBOTIC, 0.00f); }, 8192, "[sad] a");
    float robEarly = estSvfPitch(robSadL, 261.63f, 300, 1550);
    float robMid   = estSvfPitch(robSadL, 261.63f, 2800, 5200);
    float humEarly = estSvfPitch(humSadL, 261.63f, 300, 1550);
    float humMid   = estSvfPitch(humSadL, 261.63f, 2800, 5200);

    assert(corrClean > 0.60f && corrBreath < corrClean * 0.35f);
    assert(tremDepth > cleanTrem * 2.5f);
    assert(humEarly < humMid * 0.992f);
    assert(robEarly >= robMid * 0.995f);

    printf("      -> Fem/Bass=%.2f/%.2f | PitchShift(-12/0/+12)=%.1f/%.1f/%.1f Hz | Bright=%.2f/%.2f | Grit72=%.3f/%.3f | Nasal=%.2f/%.2f | BreathCorr=%.2f/%.2f | Trem=%.3f/%.3f | Robot(hum=%.1f->%.1f rob=%.1f->%.1f)\n",
           brightFem, brightBass, f0Dn, f0Zero, f0Up, brightHi, brightLo, grit72, clean72, nasRatio1, nasRatio0, corrBreath, corrClean, tremDepth, cleanTrem,
           humEarly, humMid, robEarly, robMid);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    testDissectorAndClusters();
    testMidiSyllableAndLegato();
    testWavAudioLoopAndStretch();
    testDeferredCodaHoldOnNoteOff();
    testVowelSpectralSeparation();
    testSf2PresetsAndModes();
    testStateSerialization();
    testDieDyePronunciationAndDiphthong();
    testSilentResetKeyswitchAndLearn();
    testLargeSf2Loading();
    testInstrumentOnlyVoiceAndModes();
    testTurkishAllophonesAndEnglishCoarticulation();
    testDynamicFormantServoAndMemoryStability();
    testWordContextualVowelsAndCoarticulation();
    testMouthClosureFilterAndLocusGlide();
    testVelarLociNasalZerosAndPlosiveClosure();
    testContinuousPhraseBridgeAndSameOffsetMidi();
    testDecayingCarrierLegatoAndSameKeyOverlap();
    testSentenceContextAndArticulatoryPhysics();
    testLivingThroatPlosivesTempoLexiconEmotion();
    testDistinctEmotionsAndIntraWordContinuity();
    testVoiceCharacteristicsAndBackwardCompat();
    printf("\nAll babur DSP unit tests passed!\n");
    return 0;
}
