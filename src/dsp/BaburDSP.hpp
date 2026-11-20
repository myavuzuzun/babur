#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../common/ScopedDenormals.hpp"
#include "../../include/tsf.h"
#include "AudioFileLoader.hpp"
#include "TextPhonemeDissector.hpp"

namespace Fraudize {

enum BaburParam {
    PARAM_CHARACTER = 0,   // SING <-> TALKBOX
    PARAM_DICTION = 1,     // VOWEL ONLY <-> CLEAR CONSONANTS
    PARAM_THROAT = 2,      // -12 .. +12 st formant shift
    PARAM_GLIDE = 3,       // Portamento & formant glide speed
    PARAM_LANG = 4,        // 0 = EN, 1 = TR
    PARAM_LEGATO = 5,      // 0 = FLOW, 0.5 = STAC, 1 = HOLD
    PARAM_AUDIO_MODE = 6,  // 0 = LOOP, 1 = STRETCH
    PARAM_EXCITER = 7,     // Voice Mode: RAW | WHISPER | AGGR | NORMAL
    PARAM_GENDER = 8,      // -100% Bass/Giant .. 0% Neutral .. +100% Female/Child
    PARAM_PITCH_SHIFT = 9, // -24 .. +24 st register shift
    PARAM_BREATHINESS = 10,// 0 .. 100% warm whisper air & open-chink Q
    PARAM_GRIT = 11,       // 0 .. 100% pressed edge, vocal fry & growl
    PARAM_NASALITY = 12,   // 0 .. 100% velum opening & nasal twang
    PARAM_BRIGHTNESS = 13, // -100% Dark .. +100% Bright post-servo spectral tilt
    PARAM_VIB_DEPTH = 14,  // 0 .. 100% vibrato depth (0.25 = 1.0x baseline)
    PARAM_VIB_RATE = 15,   // 2.5 .. 9.0 Hz vibrato rate (0.4154 = 5.2 Hz baseline)
    PARAM_TREMOR = 16,     // 0 .. 100% aging tremor & jitter
    PARAM_ROBOTIC = 17,    // 0 .. 100% robotic formant lock & pitch flattening
    NUM_PARAMS = 18
};

enum BaburLegatoMode {
    LEGATO_FLOW = 0, // Continuous phrase carrier + 160ms bridge, step syllable every note
    LEGATO_STAC = 1, // Re-trigger raw instrument attack on every note
    LEGATO_HOLD = 2  // Overlap holds vowel, connected notes bridge continuously
};

enum BaburVoiceMode {
    VOICE_RAW = 0,
    VOICE_WHISPER = 1,
    VOICE_AGGR = 2,
    VOICE_NORMAL = 3
};

enum BaburSourceType {
    SRC_NONE = 0,
    SRC_SF2 = 1,
    SRC_AUDIO = 2
};

class BaburEngine {
public:
    static constexpr int BLOCK_CHUNK = 64;
    static constexpr int FORMANT_SUBSTEP = 16;
    static constexpr int NUM_FORMANTS = 4;
    static constexpr int NUM_VOCODER_BANDS = 24;
    static constexpr int MAX_PHONEME_QUEUE = 12;

    struct RawMidi {
        int32_t offset;
        uint8_t msg1;
        uint8_t msg2;
        uint8_t msg3;
    };

    BaburEngine();
    ~BaburEngine();

    void setSampleRate(float sr);
    float getSampleRate() const { return sampleRate; }
    void reset();

    void setParam(int index, float val);
    float getParam(int index) const;
    std::string getParamName(int index) const;
    std::string getParamDisplay(int index) const;
    static std::string formatParamDisplay(int index, float val);
    static int decodeVoiceMode(float val);
    static int decodeLegatoMode(float val);
    static float decodePitchShiftSemis(float val) { return (std::clamp(val, 0.0f, 1.0f) - 0.5f) * 48.0f; }
    static float decodeVibRateHz(float val) { return 2.5f + std::clamp(val, 0.0f, 1.0f) * 6.5f; }

    // Source loading (.sf2 or .wav/.mp3)
    bool loadSourceFile(const std::string& path);
    void unloadSource();
    int getSourceType() const { return sourceType.load(std::memory_order_relaxed); }
    bool isLoaded() const { return sourceType.load(std::memory_order_relaxed) != SRC_NONE; }
    std::string getSourcePath() const;
    std::string getSourceName() const;

    // SF2 preset navigation
    int getNumPresets() const;
    std::string getPresetName(int idx) const;
    int getCurrentPreset() const;
    void setPresetIndex(int idx);

    // Lyrics & Syllable controls
    void setText(const std::string& utf8Text);
    std::string getText() const;
    std::vector<SyllableToken> getSyllablesCopy() const;
    int getNumSyllables() const { return numSyllables.load(std::memory_order_relaxed); }
    int getCurrentSyllableIndex() const { return currentSyllableIdx.load(std::memory_order_relaxed); }
    int getNextSyllableIndex() const { return nextSyllableIdx.load(std::memory_order_relaxed); }
    void setCurrentSyllableIndex(int idx);
    void resetSyllableCursor();

    // Silent Reset MIDI Keyswitch & MIDI Learn
    int getResetMidiKey() const { return resetMidiKey.load(std::memory_order_relaxed); }
    void setResetMidiKey(int key);
    bool isLearningResetKey() const { return learningResetKey.load(std::memory_order_relaxed); }
    void setLearningResetKey(bool learn);
    static std::string formatMidiNoteName(int key);

    // MIDI & Audio processing
    void processRawMidi(const RawMidi* events, size_t count);
    void render(float* outL, float* outR, int numFrames);

    // State persistence
    std::vector<uint8_t> saveState() const;
    void restoreState(const uint8_t* data, size_t size);

    bool isUiDirty() const { return uiDirty.load(std::memory_order_relaxed); }
    void clearUiDirty() { uiDirty.store(false, std::memory_order_relaxed); }
    void setUiDirty() { uiDirty.store(true, std::memory_order_relaxed); }

private:
    struct StereoSVF {
        float ic1eqL = 0.0f, ic2eqL = 0.0f;
        float ic1eqR = 0.0f, ic2eqR = 0.0f;
        float g = 0.1f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

        void reset() {
            ic1eqL = ic2eqL = 0.0f;
            ic1eqR = ic2eqR = 0.0f;
        }

        void setParams(float freqHz, float q, float sr) {
            float fc = std::clamp(freqHz, 20.0f, sr * 0.45f);
            float safeQ = std::max(0.5f, q);
            g = std::tan(3.14159265358979323846f * (fc / sr));
            k = 1.0f / safeQ;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void processBandpassAndLowpass(float inL, float inR, float& bpL, float& bpR, float& lpL, float& lpR) {
            float v3L = inL - ic2eqL;
            float v1L = a1 * ic1eqL + a2 * v3L;
            float v2L = ic2eqL + a2 * ic1eqL + a3 * v3L;
            ic1eqL = 2.0f * v1L - ic1eqL;
            ic2eqL = 2.0f * v2L - ic2eqL;
            bpL = v1L;
            lpL = v2L;

            float v3R = inR - ic2eqR;
            float v1R = a1 * ic1eqR + a2 * v3R;
            float v2R = ic2eqR + a2 * ic1eqR + a3 * v3R;
            ic1eqR = 2.0f * v1R - ic1eqR;
            ic2eqR = 2.0f * v2R - ic2eqR;
            bpR = v1R;
            lpR = v2R;
        }

        void processBandpass(float inL, float inR, float& outL, float& outR) {
            float lpL = 0.0f, lpR = 0.0f;
            processBandpassAndLowpass(inL, inR, outL, outR, lpL, lpR);
        }

        void processLowpass(float inL, float inR, float& lpL, float& lpR) {
            float bpL = 0.0f, bpR = 0.0f;
            processBandpassAndLowpass(inL, inR, bpL, bpR, lpL, lpR);
        }
    };

    // Critically-damped 2nd-order spring-mass muscle for physical articulator motion
    struct ArticulatorSpring {
        float pos = 0.0f;
        float vel = 0.0f;
        void reset(float p) { pos = p; vel = 0.0f; }
        void step(float target, float dt, float tauSec) {
            float w = 1.0f / std::max(0.0015f, tauSec);
            float e = std::exp(-w * dt);
            float dx = pos - target;
            float nextPos = target + (dx + (vel + w * dx) * dt) * e;
            float nextVel = (vel - w * (vel + w * dx) * dt) * e;
            pos = nextPos;
            vel = nextVel;
        }
    };

    enum PhonemeKind {
        PH_CONSONANT = 0,
        PH_VOWEL_CORE
    };

    struct QueuedPhoneme {
        PhonemeKind kind = PH_VOWEL_CORE;
        ConsonantSpec cons{};
        bool isCoda = false;
        float durationScale = 1.0f;
        bool unreleased = false;
    };

    enum SyllablePhase {
        PHASE_IDLE = 0,
        PHASE_ONSET,
        PHASE_VOWEL_SUSTAIN,
        PHASE_CODA_RELEASE
    };

    float sampleRate = 44100.0f;

    std::atomic<float> paramCharacter{ 0.35f };
    std::atomic<float> paramDiction{ 0.65f };
    std::atomic<float> paramThroat{ 0.50f };
    std::atomic<float> paramGlide{ 0.25f };
    std::atomic<float> paramLang{ 0.00f };
    std::atomic<float> paramLegato{ 1.00f }; // default: LEGATO_HOLD
    std::atomic<float> paramAudioMode{ 1.00f };
    std::atomic<float> paramExciter{ 1.00f }; // default: VOICE_NORMAL
    std::atomic<float> paramGender{ 0.50f };
    std::atomic<float> paramPitchShift{ 0.50f };
    std::atomic<float> paramBreathiness{ 0.00f };
    std::atomic<float> paramGrit{ 0.00f };
    std::atomic<float> paramNasality{ 0.00f };
    std::atomic<float> paramBrightness{ 0.50f };
    std::atomic<float> paramVibDepth{ 0.25f };
    std::atomic<float> paramVibRate{ (5.2f - 2.5f) / 6.5f };
    std::atomic<float> paramTremor{ 0.00f };
    std::atomic<float> paramRobotic{ 0.00f };
    std::atomic<int> resetMidiKey{ -1 };
    std::atomic<bool> learningResetKey{ false };
    std::atomic<bool> uiDirty{ false };

    mutable std::mutex sourceMutex;
    mutable std::mutex textMutex;
    std::mutex midiMutex;

    std::atomic<int> sourceType{ SRC_NONE };
    std::string sourcePath;
    std::string sourceFileName;

    // SF2 carrier state
    tsf* sfHandle = nullptr;
    std::vector<std::string> sfPresetNames;
    int sfCurrentPreset = 0;

    // Audio carrier state (Root = A4 / MIDI 69)
    LoadedAudioBuffer audioBuf;
    double audioLoopPos = 0.0;
    struct StretchGrain {
        double anchor = 0.0;
        float phase = 0.0f;
    };
    StretchGrain stretchGrains[2]{};
    double stretchAnchorPos = 0.0;
    bool stretchForward = true;
    float audioEnvGain = 0.0f;

    // Text & syllable queue
    std::string lyricsText;
    std::vector<SyllableToken> syllables;
    std::atomic<int> numSyllables{ 1 };
    std::atomic<int> currentSyllableIdx{ 0 };
    std::atomic<int> nextSyllableIdx{ 0 };

    // Monophonic MIDI state
    std::array<int, 16> heldKeys{};
    int numHeldKeys = 0;
    bool noteGate = false;
    bool codaCarrierHold = false;
    bool pendingCarrierAttack = false;
    bool sf2VoiceRunning = false;
    int targetMidiKey = 60;
    int sf2BaseKey = 60;
    float smoothedMidiPitch = 60.0f;
    float scoopOffsetSemis = 0.0f;
    float lastVelocity = 0.85f;
    int pitchWheel = 8192;
    uint64_t noteHoldSamples = 0;
    uint64_t phraseBridgeRemaining = 0;
    bool bridgeIsMidWord = false;
    double vibratoPhase = 0.0;

    // Sequential Phoneme Queue state machine
    SyllableToken activeSyllable;
    SyllableToken prevSyllable;
    SyllableToken upcomingSyllable{};
    ConsonantSpec activeOnsetCons{};
    bool hasPrevSyllable = false;
    bool hasUpcomingSyllable = false;
    bool hasActiveOnsetCons = false;
    bool codaConsumed = true;
    std::array<QueuedPhoneme, MAX_PHONEME_QUEUE> phonemeQueue{};
    int queueHead = 0;
    int queueTail = 0;
    QueuedPhoneme currentPhoneme{};
    bool hasActivePhoneme = false;
    SyllablePhase syllablePhase = PHASE_IDLE;

    uint64_t phaseSample = 0;
    uint64_t closureSamples = 0;
    uint64_t burstSamples = 0;
    uint64_t transSamples = 0;
    uint64_t onsetCarrySamples = 0;
    uint64_t vowelSamples = 0;
    uint64_t diphthongFinishRemaining = 0;
    float diphthongMorph = 0.0f;

    // Smoothed formant state (moderate non-ringing bandwidths)
    float smoothF[NUM_FORMANTS] = { 760.0f, 1150.0f, 2550.0f, 3500.0f };
    float smoothQ[NUM_FORMANTS] = { 4.8f, 5.5f, 6.0f, 6.2f };
    float smoothChar = 0.35f;
    float smoothDiction = 0.65f;
    float smoothThroat = 0.50f;
    float smoothGlide = 0.25f;
    float smoothOralGate = 1.0f;
    float smoothNasalMix = 0.0f;
    float smoothVoiceBar = 0.0f;
    float smoothGender = 0.0f;      // -1 .. +1
    float smoothPitchShift = 0.0f;  // -24 .. +24 st
    float smoothBreathiness = 0.0f; // 0 .. 1
    float smoothGrit = 0.0f;        // 0 .. 1
    float smoothNasality = 0.0f;    // 0 .. 1
    float smoothBrightness = 0.0f;  // -1 .. +1
    float smoothVibDepth = 0.25f;   // 0 .. 1 (0.25 = 1.0x baseline)
    float smoothVibRate = 5.2f;     // 2.5 .. 9.0 Hz
    float smoothTremor = 0.0f;      // 0 .. 1
    float smoothRobotic = 0.0f;     // 0 .. 1

    // 6-Articulator Gestural Physics (critically-damped 2nd-order spring-mass muscles)
    ArticulatorSpring artJaw;          // 0=closed .. 1=open (/a/) -> drives F1
    ArticulatorSpring artTongueBodyX;  // -1=back (/u,o/) .. +1=front (/i,e/) -> drives F2
    ArticulatorSpring artTongueBodyY;  // 0=low .. 1=high -> height & velar constriction
    ArticulatorSpring artTongueTip;    // 0=relaxed .. 1=alveolar/dental/palatal contact
    ArticulatorSpring artLips;         // 0=spread .. 1=rounded (/u,o,ö,ü,w/)
    ArticulatorSpring artLipClose;     // 0=open .. 1=bilabial/labiodental closure (p,b,m,f,v)
    ArticulatorSpring artVelum;        // 0=oral sealed .. 1=nasal port open (m,n,ng)
    ArticulatorSpring artVelarClose;   // 0=open .. 1=velar closure (k,g,ng)
    ArticulatorSpring artF3;           // F3 smooth spring (rhotic/lateral/palatal)
    ArticulatorSpring artF4;           // F4 smooth spring
    float tipLocusF[3]   = { 300.0f, 1720.0f, 2650.0f };
    float velarLocusF[3] = { 280.0f, 1500.0f, 2320.0f };
    float smoothProsodyPitch = 0.0f;
    float smoothProminence = 1.0f;

    // Playing-speed / Tempo-Adaptive Articulation & Flow Cadence
    uint64_t samplesSinceLastNoteOn = 0;
    float avgNoteIntervalSec = 0.32f;
    float cadenceConfidence = 0.0f;      // 0..1 ramps up after consecutive connected notes
    float currentTempoScale = 1.0f;      // 0.52 (fast 16ths) .. 1.45 (slow ballad)
    float preCodaWeight = 0.0f;          // 0..1 anticipatory pre-coda closure inside PH_VOWEL_CORE

    // Living Throat Micro-Dynamics (spectral tilt, pitch-sync breath, organic 1/f drift, F1 cutback)
    float glottalTiltStateL = 0.0f, glottalTiltStateR = 0.0f;
    float jitterWalk = 0.0f;
    float jitterSmooth = 0.0f;
    float shimmerSmooth = 0.0f;
    float f1CutbackGain = 1.0f;          // F1 attenuation during unvoiced stop VOT release
    float smoothSyllEnv = 1.0f;          // post-leveler living syllabic bell envelope

    // Smoothed Emotion Parameters (updated from activeSyllable.emotion at 35 ms tau)
    float emoTilt = 0.0f;
    float emoBreath = 0.022f;
    float emoFormantScale = 1.0f;
    float emoJawScale = 1.0f;
    float emoSpeedScale = 1.0f;
    float emoDictionScale = 1.0f;
    float emoPitchBias = 0.0f;
    float emoVibRate = 5.2f;
    float emoEdgeDrive = 0.0f;
    float emoLarynxScale = 1.0f;
    float emoQScale = 1.0f;
    float emoWhisperMix = 0.0f;
    float emoRoughness = 0.0f;
    float emoNasalFloor = 0.0f;
    float emoVibDelaySec = 0.25f;
    float emoVibDepthSemis = 0.11f;
    float emoJitterScale = 1.0f;
    float emoScoopDecaySec = 0.022f;
    float emoHoldDroopSemis = 0.0f;
    float emoBloomMs = 24.0f;
    float emoBloomStartGain = 0.86f;
    float emoDynGain = 1.0f;
    float softBreathLpL = 0.0f;
    float softBreathLpR = 0.0f;

    // Pure instrument-as-voice filter banks (no pitched oscillators, zero heap allocations)
    StereoSVF singFormantSvf[NUM_FORMANTS];
    StereoSVF cascadeFormantSvf[NUM_FORMANTS];
    StereoSVF singTiltSvf;
    StereoSVF pharynxSvf;
    StereoSVF singerFormantSvf;
    StereoSVF vocalCeilingSvf;
    StereoSVF nasalMurmurSvf;
    StereoSVF nasalZeroSvf;
    StereoSVF voiceBarSvf;
    StereoSVF aggrPresenceSvf;
    StereoSVF vocoderCarSvf[NUM_VOCODER_BANDS];
    StereoSVF consonantNoiseSvf1;
    StereoSVF consonantNoiseSvf2;
    StereoSVF aspirationSvf;
    StereoSVF consFormantSvf2;
    StereoSVF consFormantSvf3;
    std::array<float, NUM_FORMANTS> formantEnv{};
    std::array<float, NUM_FORMANTS> dynFormantGain{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, NUM_FORMANTS> formantPeakBoost{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, NUM_VOCODER_BANDS> carrierBandEnv{};
    std::array<float, NUM_VOCODER_BANDS> targetBandMask{};
    std::array<float, NUM_VOCODER_BANDS> vocoderBandFreqs{};

    // Envelope & harmonic shaping state
    float carrierEnv = 0.0f;
    float carrierAttackPeak = 0.0f;   // peak carrierEnv since the last carrier (re)attack; detects decayed SF2/one-shot voices
    float outPeakEnv = 0.0f;          // output safety limiter peak follower
    float outLimGain = 1.0f;          // output safety limiter gain (keeps tanh out of hard saturation)
    float speechLeveler = 1.0f;
    float vowelOutEnv = 0.0f;
    float vowelLeveler = 1.0f;
    float preEmphPrevL = 0.0f, preEmphPrevR = 0.0f;
    float hpExcL = 0.0f, hpExcR = 0.0f;
    float normPrevL = 0.0f, normPrevR = 0.0f;
    float subOctSignL = 1.0f, subOctSignR = 1.0f;
    int subOctHoldL = 0, subOctHoldR = 0;
    float dcPrevInL = 0.0f, dcPrevOutL = 0.0f;
    float dcPrevInR = 0.0f, dcPrevOutR = 0.0f;
    double growlPhase = 0.0;
    double tremorPhase = 0.0;
    double robotRingPhase = 0.0;
    uint32_t noiseRngState = 0x12345678u;

    // Scratch buffers
    std::vector<RawMidi> pendingMidi;
    std::vector<RawMidi> renderMidiScratch;
    std::array<float, BLOCK_CHUNK * 2> scratchRaw{};

    static std::string extractFileName(const std::string& path);
    static float decodeThroatSemis(float val);
    void initVocoderBands();
    void updateFilterBanks(float sr);

    void clearPhonemeQueue();
    void pushPhoneme(const QueuedPhoneme& qp);
    bool advancePhonemeQueue();
    void configureCurrentPhonemeDurations();
    void startSyllableSequence(const SyllableToken& tok);
    void fireDeferredCarrierAttack();
    bool carrierDecayed() const;
    void reexciteSf2Carrier(int key);
    void beginCodaReleaseAfterDiphthong();
    void resetArticulators(float f1, float f2, float f3, float f4, float lipRound);
    void snapArticulatorsForColdStart();

    void handleMidiEvent(uint8_t msg1, uint8_t msg2, uint8_t msg3);
    void triggerNoteOn(int key, float vel);
    void triggerNoteOff(int key);
    void allNotesOff(bool immediate);

    void renderAudioCarrierSample(float effectiveMidiPitch, bool stretchMode, float& outL, float& outR);
    float nextNoiseSample();
};

} // namespace Fraudize
