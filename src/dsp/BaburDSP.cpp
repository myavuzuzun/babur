#define TSF_IMPLEMENTATION
#include "../../include/tsf.h"
#include "BaburDSP.hpp"

#include <cstdio>

namespace Fraudize {

namespace {

static constexpr float PI_F = 3.14159265358979323846f;
static constexpr float TWO_PI_F = 6.28318530717958647692f;

inline float fastTanh(float x) {
    if (x < -3.5f) return -1.0f;
    if (x > 3.5f) return 1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float hermite4(const std::vector<float>& buf, double pos) {
    size_t n = buf.size();
    if (n == 0) return 0.0f;
    if (pos <= 0.0) return buf[0];
    if (pos >= static_cast<double>(n - 1)) return buf[n - 1];

    size_t i1 = static_cast<size_t>(pos);
    float frac = static_cast<float>(pos - static_cast<double>(i1));
    size_t i0 = (i1 > 0) ? (i1 - 1) : 0;
    size_t i2 = std::min(n - 1, i1 + 1);
    size_t i3 = std::min(n - 1, i1 + 2);

    float xm1 = buf[i0];
    float x0  = buf[i1];
    float x1  = buf[i2];
    float x2  = buf[i3];

    float c0 = x0;
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

inline void writeU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

inline void writeI32LE(std::vector<uint8_t>& out, int32_t v) {
    writeU32LE(out, static_cast<uint32_t>(v));
}

inline void writeF32LE(std::vector<uint8_t>& out, float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(float));
    writeU32LE(out, u);
}

inline bool readU32LE(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<uint32_t>(data[pos]) |
          (static_cast<uint32_t>(data[pos + 1]) << 8) |
          (static_cast<uint32_t>(data[pos + 2]) << 16) |
          (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    return true;
}

inline bool readI32LE(const uint8_t* data, size_t size, size_t& pos, int32_t& out) {
    uint32_t u = 0;
    if (!readU32LE(data, size, pos, u)) return false;
    out = static_cast<int32_t>(u);
    return true;
}

inline bool readF32LE(const uint8_t* data, size_t size, size_t& pos, float& out) {
    uint32_t u = 0;
    if (!readU32LE(data, size, pos, u)) return false;
    std::memcpy(&out, &u, sizeof(float));
    return true;
}

inline bool isUnvoicedObstruent(ConsonantType t) {
    return t == C_PLOSIVE_UNVOICED || t == C_FRICATIVE_UNVOICED || t == C_AFFRICATE_UNVOICED;
}

enum ConsonantPlace {
    PLACE_NONE = 0,
    PLACE_BILABIAL,
    PLACE_LABIODENTAL,
    PLACE_ALVEOLAR,
    PLACE_PALATAL,
    PLACE_VELAR,
    PLACE_LABIAL_VELAR
};

inline float vowelLipRounding(VowelId v) {
    switch (v) {
        case V_U:  return 0.95f;
        case V_UE: return 0.85f;
        case V_O:  return 0.78f;
        case V_OE: return 0.65f;
        case V_A:  return 0.12f;
        default:   return 0.00f;
    }
}

inline int classifyConsonantPlace(const ConsonantSpec& cs) {
    if (cs.type == C_NONE) return PLACE_NONE;
    const char* l = cs.label;
    if (l[0] == 'h') return PLACE_NONE;
    if (std::strcmp(l, "p") == 0 || std::strcmp(l, "b") == 0 || std::strcmp(l, "m") == 0) return PLACE_BILABIAL;
    if (std::strcmp(l, "f") == 0 || std::strcmp(l, "v") == 0) return PLACE_LABIODENTAL;
    if (std::strcmp(l, "k") == 0 || std::strcmp(l, "g") == 0 || std::strcmp(l, "ng") == 0) return PLACE_VELAR;
    if (std::strcmp(l, "w") == 0) return PLACE_LABIAL_VELAR;
    if (std::strcmp(l, "sh") == 0 || std::strcmp(l, "zh") == 0 ||
        std::strcmp(l, "ch") == 0 || std::strcmp(l, "j") == 0 ||
        std::strcmp(l, "y") == 0) return PLACE_PALATAL;
    return PLACE_ALVEOLAR;
}

struct EmotionProfile {
    float tilt;
    float breath;
    float formantScale;
    float jawScale;
    float speedScale;
    float dictionScale;
    float pitchBias;
    float vibRate;
    float edgeDrive;
    float larynxScale;
    float qScale;
    float whisperMix;
    float roughness;
    float nasalFloor;
    float vibDelaySec;
    float vibDepthSemis;
    float jitterScale;
    float onsetScoopSemis;
    float scoopDecaySec;
    float holdDroopSemis;
    float bloomMs;
    float bloomStartGain;
    float dynGain;
};

inline EmotionProfile getEmotionProfile(EmotionId emo) {
    switch (emo) {
        //                       tilt    breath form   jaw    speed  dict   pBias   vibHz edge   larynx qScale whisp  rough  nasal  vibDel vibDep jit    scoop   scDec   droop   bloom  blSt   dyn
        case EMO_HAPPY:   return { +0.24f, 0.018f, 1.090f, 1.10f, 0.86f, 1.10f, +0.20f, 5.8f, 0.08f, 1.05f, 1.12f, 0.00f, 0.00f, 0.00f, 0.12f, 0.18f, 0.95f, -0.38f, 0.030f,  0.00f, 16.0f, 0.92f, 1.03f };
        case EMO_SAD:     return { -0.30f, 0.042f, 0.955f, 0.84f, 1.24f, 0.82f, -0.10f, 4.5f, 0.00f, 0.90f, 0.78f, 0.14f, 0.00f, 0.18f, 0.08f, 0.26f, 2.40f, -0.70f, 0.085f, -0.30f, 58.0f, 0.72f, 0.93f };
        case EMO_ANGRY:   return { +0.40f, 0.008f, 1.030f, 1.22f, 0.76f, 1.24f, +0.06f, 7.2f, 0.55f, 1.04f, 1.28f, 0.00f, 0.65f, 0.00f, 0.16f, 0.07f, 0.65f, +0.60f, 0.036f,  0.00f,  7.0f, 1.12f, 1.07f };
        case EMO_SOFT:    return { -0.38f, 0.045f, 0.980f, 0.86f, 1.20f, 0.68f, -0.04f, 4.3f, 0.00f, 0.96f, 0.66f, 0.42f, 0.00f, 0.00f, 0.20f, 0.09f, 1.20f, -0.20f, 0.052f, -0.08f, 65.0f, 0.68f, 0.89f };
        case EMO_EXCITED: return { +0.32f, 0.016f, 1.075f, 1.24f, 0.74f, 1.24f, +0.34f, 6.9f, 0.28f, 1.07f, 1.20f, 0.00f, 0.14f, 0.00f, 0.09f, 0.22f, 1.15f, -0.50f, 0.022f,  0.00f, 10.0f, 1.08f, 1.06f };
        case EMO_NEUTRAL:
        default:          return {  0.00f, 0.022f, 1.000f, 1.00f, 1.00f, 1.00f,  0.00f, 5.2f, 0.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.25f, 0.11f, 1.00f,  0.00f, 0.022f,  0.00f, 24.0f, 0.86f, 1.00f };
    }
}

} // namespace

BaburEngine::BaburEngine() {
    pendingMidi.reserve(256);
    renderMidiScratch.reserve(256);
    initVocoderBands();
    setText("Hello world singing voice");
    setSampleRate(44100.0f);
    reset();
}

BaburEngine::~BaburEngine() {
    std::lock_guard<std::mutex> lock(sourceMutex);
    if (sfHandle) {
        tsf_close(sfHandle);
        sfHandle = nullptr;
    }
}

void BaburEngine::initVocoderBands() {
    static constexpr float kBandHz[NUM_VOCODER_BANDS] = {
         140.0f,  175.0f,  220.0f,  275.0f,  340.0f,  420.0f,
         515.0f,  630.0f,  765.0f,  925.0f, 1115.0f, 1340.0f,
        1605.0f, 1920.0f, 2290.0f, 2725.0f, 3235.0f, 3830.0f,
        4320.0f, 4760.0f, 5180.0f, 5590.0f, 6000.0f, 6400.0f
    };
    for (int b = 0; b < NUM_VOCODER_BANDS; ++b) {
        vocoderBandFreqs[b] = kBandHz[b];
        targetBandMask[b] = 1.0f;
    }
}

void BaburEngine::updateFilterBanks(float sr) {
    for (int b = 0; b < NUM_VOCODER_BANDS; ++b) {
        // Moderate non-ringing Bark bandwidth (Q = 4.2) for pure instrument tone
        vocoderCarSvf[b].setParams(vocoderBandFreqs[b], 4.2f, sr);
    }
    singTiltSvf.setParams(1350.0f, 0.707f, sr);
    pharynxSvf.setParams(540.0f, 4.5f, sr);
    singerFormantSvf.setParams(3050.0f, 7.5f, sr);
    vocalCeilingSvf.setParams(4750.0f, 0.72f, sr);
    nasalMurmurSvf.setParams(260.0f, 3.2f, sr);
    nasalZeroSvf.setParams(1500.0f, 3.5f, sr);
    voiceBarSvf.setParams(160.0f, 2.2f, sr);
    aggrPresenceSvf.setParams(2500.0f, 1.1f, sr);
}

void BaburEngine::setSampleRate(float sr) {
    float safeSr = std::clamp(sr, 8000.0f, 384000.0f);
    sampleRate = safeSr;
    updateFilterBanks(safeSr);

    std::lock_guard<std::mutex> lock(sourceMutex);
    if (sfHandle) {
        tsf_set_output(sfHandle, TSF_STEREO_INTERLEAVED, static_cast<int>(safeSr), 0.0f);
    }
}

void BaburEngine::reset() {
    {
        std::lock_guard<std::mutex> mLock(midiMutex);
        pendingMidi.clear();
    }
    allNotesOff(true);

    for (int f = 0; f < NUM_FORMANTS; ++f) {
        singFormantSvf[f].reset();
        cascadeFormantSvf[f].reset();
        formantEnv[f] = 0.0f;
        dynFormantGain[f] = 1.0f;
        formantPeakBoost[f] = 1.0f;
    }
    singTiltSvf.reset();
    pharynxSvf.reset();
    singerFormantSvf.reset();
    vocalCeilingSvf.reset();
    nasalMurmurSvf.reset();
    nasalZeroSvf.reset();
    voiceBarSvf.reset();
    aggrPresenceSvf.reset();

    for (int b = 0; b < NUM_VOCODER_BANDS; ++b) {
        vocoderCarSvf[b].reset();
        carrierBandEnv[b] = 0.0f;
        targetBandMask[b] = 1.0f;
    }
    consonantNoiseSvf1.reset();
    consonantNoiseSvf2.reset();
    aspirationSvf.reset();
    consFormantSvf2.reset();
    consFormantSvf3.reset();

    carrierEnv = 0.0f;
    carrierAttackPeak = 0.0f;
    outPeakEnv = 0.0f;
    outLimGain = 1.0f;
    speechLeveler = 1.0f;
    vowelOutEnv = 0.0f;
    vowelLeveler = 1.0f;
    preEmphPrevL = preEmphPrevR = 0.0f;
    hpExcL = hpExcR = 0.0f;
    normPrevL = normPrevR = 0.0f;
    subOctSignL = subOctSignR = 1.0f;
    subOctHoldL = subOctHoldR = 0;
    dcPrevInL = dcPrevOutL = 0.0f;
    dcPrevInR = dcPrevOutR = 0.0f;
    growlPhase = 0.0;
    tremorPhase = 0.0;
    robotRingPhase = 0.0;
    vibratoPhase = 0.0;
    scoopOffsetSemis = 0.0f;
    audioEnvGain = 0.0f;
    audioLoopPos = 0.0;
    stretchAnchorPos = 0.0;
    stretchForward = true;
    stretchGrains[0] = { 0.0, 0.0f };
    stretchGrains[1] = { 0.0, 0.5f };

    clearPhonemeQueue();
    hasActivePhoneme = false;
    hasPrevSyllable = false;
    hasUpcomingSyllable = false;
    hasActiveOnsetCons = false;
    codaConsumed = true;
    codaCarrierHold = false;
    pendingCarrierAttack = false;
    sf2VoiceRunning = false;
    phraseBridgeRemaining = 0;
    bridgeIsMidWord = false;
    syllablePhase = PHASE_IDLE;
    phaseSample = 0;
    onsetCarrySamples = 0;
    vowelSamples = 0;
    diphthongFinishRemaining = 0;
    diphthongMorph = 0.0f;
    smoothOralGate = 1.0f;
    smoothNasalMix = 0.0f;
    smoothVoiceBar = 0.0f;
    smoothProsodyPitch = 0.0f;
    smoothProminence = 1.0f;
    samplesSinceLastNoteOn = 0;
    avgNoteIntervalSec = 0.32f;
    cadenceConfidence = 0.0f;
    currentTempoScale = 1.0f;
    preCodaWeight = 0.0f;
    glottalTiltStateL = glottalTiltStateR = 0.0f;
    jitterWalk = jitterSmooth = shimmerSmooth = 0.0f;
    f1CutbackGain = 1.0f;
    smoothSyllEnv = 1.0f;
    emoTilt = 0.0f;
    emoBreath = 0.022f;
    emoFormantScale = 1.0f;
    emoJawScale = 1.0f;
    emoSpeedScale = 1.0f;
    emoDictionScale = 1.0f;
    emoPitchBias = 0.0f;
    emoVibRate = 5.2f;
    emoEdgeDrive = 0.0f;
    emoLarynxScale = 1.0f;
    emoQScale = 1.0f;
    emoWhisperMix = 0.0f;
    emoRoughness = 0.0f;
    emoNasalFloor = 0.0f;
    emoVibDelaySec = 0.25f;
    emoVibDepthSemis = 0.11f;
    emoJitterScale = 1.0f;
    emoScoopDecaySec = 0.022f;
    emoHoldDroopSemis = 0.0f;
    emoBloomMs = 24.0f;
    emoBloomStartGain = 0.86f;
    emoDynGain = 1.0f;
    softBreathLpL = softBreathLpR = 0.0f;
    smoothGender = (paramGender.load(std::memory_order_relaxed) - 0.5f) * 2.0f;
    smoothPitchShift = decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed));
    smoothBreathiness = paramBreathiness.load(std::memory_order_relaxed);
    smoothGrit = paramGrit.load(std::memory_order_relaxed);
    smoothNasality = paramNasality.load(std::memory_order_relaxed);
    smoothBrightness = (paramBrightness.load(std::memory_order_relaxed) - 0.5f) * 2.0f;
    smoothVibDepth = paramVibDepth.load(std::memory_order_relaxed);
    smoothVibRate = decodeVibRateHz(paramVibRate.load(std::memory_order_relaxed));
    smoothTremor = paramTremor.load(std::memory_order_relaxed);
    smoothRobotic = paramRobotic.load(std::memory_order_relaxed);
    resetArticulators(760.0f, 1150.0f, 2550.0f, 3500.0f, 0.12f);
}

void BaburEngine::resetArticulators(float f1, float f2, float f3, float f4, float lipRound) {
    artJaw.reset(std::clamp((f1 - 250.0f) / 530.0f, -0.10f, 1.65f));
    artTongueBodyX.reset(std::clamp((f2 - 1480.0f) / 960.0f, -1.10f, 1.50f));
    artTongueBodyY.reset(0.35f);
    artTongueTip.reset(0.0f);
    artLips.reset(std::clamp(lipRound, 0.0f, 1.0f));
    artLipClose.reset(0.0f);
    artVelum.reset(0.0f);
    artVelarClose.reset(0.0f);
    artF3.reset(f3);
    artF4.reset(f4);
    smoothF[0] = f1;
    smoothF[1] = f2;
    smoothF[2] = f3;
    smoothF[3] = f4;
}

void BaburEngine::snapArticulatorsForColdStart() {
    const VowelFormants& vf1 = getVowelFormants(activeSyllable.vowel1);
    float sF1 = (activeSyllable.contextF1 > 0.0f) ? activeSyllable.contextF1 : vf1.f1;
    float sF2 = (activeSyllable.contextF2 > 0.0f) ? activeSyllable.contextF2 : vf1.f2;
    float sF3 = (activeSyllable.contextF3 > 0.0f) ? activeSyllable.contextF3 : vf1.f3;
    float lipR = vowelLipRounding(activeSyllable.vowel1);
    resetArticulators(sF1, sF2, sF3, vf1.f4, lipR);
    smoothProminence = std::clamp(activeSyllable.prominence, 0.72f, 1.26f);
    smoothProsodyPitch = activeSyllable.prosodyPitchSemis;
    EmotionProfile ep = getEmotionProfile(activeSyllable.emotion);
    smoothSyllEnv = ep.bloomStartGain;
    f1CutbackGain = 1.0f;
    emoTilt = ep.tilt;
    emoBreath = ep.breath;
    emoFormantScale = ep.formantScale;
    emoJawScale = ep.jawScale;
    emoSpeedScale = ep.speedScale;
    emoDictionScale = ep.dictionScale;
    emoPitchBias = ep.pitchBias;
    emoVibRate = ep.vibRate;
    emoEdgeDrive = ep.edgeDrive;
    emoLarynxScale = ep.larynxScale;
    emoQScale = ep.qScale;
    emoWhisperMix = ep.whisperMix;
    emoRoughness = ep.roughness;
    emoNasalFloor = ep.nasalFloor;
    emoVibDelaySec = ep.vibDelaySec;
    emoVibDepthSemis = ep.vibDepthSemis;
    emoJitterScale = ep.jitterScale;
    emoScoopDecaySec = ep.scoopDecaySec;
    emoHoldDroopSemis = ep.holdDroopSemis;
    emoBloomMs = ep.bloomMs;
    emoBloomStartGain = ep.bloomStartGain;
    emoDynGain = ep.dynGain;
    smoothGender = (paramGender.load(std::memory_order_relaxed) - 0.5f) * 2.0f;
    smoothPitchShift = decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed));
    smoothBreathiness = paramBreathiness.load(std::memory_order_relaxed);
    smoothGrit = paramGrit.load(std::memory_order_relaxed);
    smoothNasality = paramNasality.load(std::memory_order_relaxed);
    smoothBrightness = (paramBrightness.load(std::memory_order_relaxed) - 0.5f) * 2.0f;
    smoothVibDepth = paramVibDepth.load(std::memory_order_relaxed);
    smoothVibRate = decodeVibRateHz(paramVibRate.load(std::memory_order_relaxed));
    smoothTremor = paramTremor.load(std::memory_order_relaxed);
    smoothRobotic = paramRobotic.load(std::memory_order_relaxed);

    if (activeSyllable.nasalized) {
        artVelum.reset(0.16f);
    }
    if (hasActivePhoneme && currentPhoneme.kind == PH_CONSONANT) {
        const ConsonantSpec& cs = currentPhoneme.cons;
        float dict = std::clamp(paramDiction.load(std::memory_order_relaxed), 0.0f, 1.0f);
        if (cs.type == C_NASAL) {
            artVelum.reset(std::clamp(cs.murmurGain * dict, 0.0f, 1.0f));
        }
        int place = classifyConsonantPlace(cs);
        if (place == PLACE_BILABIAL) {
            artLipClose.reset(0.92f);
            artJaw.reset(0.10f);
        } else if (place == PLACE_LABIODENTAL) {
            artLipClose.reset(0.68f);
            artJaw.reset(0.18f);
        } else if (place == PLACE_ALVEOLAR) {
            artTongueTip.reset(0.92f);
            tipLocusF[0] = cs.locusF1;
            tipLocusF[1] = cs.locusF2;
            tipLocusF[2] = cs.locusF3;
            artJaw.reset(std::clamp((cs.locusF1 - 250.0f) / 530.0f, 0.0f, 1.0f));
        } else if (place == PLACE_PALATAL) {
            artTongueTip.reset(0.80f);
            tipLocusF[0] = cs.locusF1;
            tipLocusF[1] = cs.locusF2;
            tipLocusF[2] = cs.locusF3;
            artLips.reset(std::max(artLips.pos, 0.35f));
        } else if (place == PLACE_VELAR) {
            artVelarClose.reset(0.92f);
            velarLocusF[0] = cs.locusF1;
            velarLocusF[1] = cs.locusF2;
            velarLocusF[2] = cs.locusF3;
            artTongueBodyY.reset(0.95f);
        } else if (place == PLACE_LABIAL_VELAR) {
            artLips.reset(0.95f);
            artTongueBodyX.reset(-0.82f);
            artJaw.reset(0.12f);
        }
        // Reconstruct initial formants from pre-positioned cold-start articulators
        float f1 = 250.0f + 530.0f * artJaw.pos;
        float f2 = 1480.0f + 960.0f * artTongueBodyX.pos;
        float f3 = artF3.pos;
        float lc = artLipClose.pos;
        if (lc > 0.001f) { f1 = lerp(f1, 235.0f, lc * 0.92f); f2 = lerp(f2, 860.0f, lc * 0.85f); f3 = lerp(f3, 2120.0f, lc * 0.72f); }
        float tt = artTongueTip.pos;
        if (tt > 0.001f) { f1 = lerp(f1, tipLocusF[0], tt * 0.90f); f2 = lerp(f2, tipLocusF[1], tt * 0.82f); f3 = lerp(f3, tipLocusF[2], tt * 0.78f); }
        float vc = artVelarClose.pos;
        if (vc > 0.001f) { f1 = lerp(f1, velarLocusF[0], vc * 0.90f); f2 = lerp(f2, velarLocusF[1], vc * 0.88f); f3 = lerp(f3, velarLocusF[2], vc * 0.80f); }
        smoothF[0] = std::clamp(f1, 180.0f, 1150.0f);
        smoothF[1] = std::clamp(f2, 550.0f, 3000.0f);
        smoothF[2] = std::clamp(f3, 1400.0f, 3600.0f);
    }
}

std::string BaburEngine::extractFileName(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

float BaburEngine::decodeThroatSemis(float val) {
    float st = (std::clamp(val, 0.0f, 1.0f) - 0.5f) * 24.0f;
    return (std::fabs(st) < 0.10f) ? 0.0f : st;
}

int BaburEngine::decodeVoiceMode(float val) {
    float v = std::clamp(val, 0.0f, 1.0f);
    if (v < 0.20f) return VOICE_RAW;
    if (v < 0.55f) return VOICE_WHISPER;
    if (v < 0.82f) return VOICE_AGGR;
    return VOICE_NORMAL;
}

int BaburEngine::decodeLegatoMode(float val) {
    float v = std::clamp(val, 0.0f, 1.0f);
    if (v < 0.35f) return LEGATO_FLOW;
    if (v < 0.65f) return LEGATO_STAC;
    return LEGATO_HOLD;
}

void BaburEngine::setParam(int index, float val) {
    float v = std::clamp(val, 0.0f, 1.0f);
    switch (index) {
        case PARAM_CHARACTER:  paramCharacter.store(v, std::memory_order_relaxed); break;
        case PARAM_DICTION:    paramDiction.store(v, std::memory_order_relaxed); break;
        case PARAM_THROAT:     paramThroat.store(v, std::memory_order_relaxed); break;
        case PARAM_GLIDE:      paramGlide.store(v, std::memory_order_relaxed); break;
        case PARAM_LANG: {
            float prev = paramLang.exchange(v, std::memory_order_relaxed);
            if ((prev >= 0.5f) != (v >= 0.5f)) {
                setText(getText());
            }
            break;
        }
        case PARAM_LEGATO:      paramLegato.store(v, std::memory_order_relaxed); break;
        case PARAM_AUDIO_MODE:  paramAudioMode.store(v, std::memory_order_relaxed); break;
        case PARAM_EXCITER:     paramExciter.store(v, std::memory_order_relaxed); break;
        case PARAM_GENDER:      paramGender.store(v, std::memory_order_relaxed); break;
        case PARAM_PITCH_SHIFT: paramPitchShift.store(v, std::memory_order_relaxed); break;
        case PARAM_BREATHINESS: paramBreathiness.store(v, std::memory_order_relaxed); break;
        case PARAM_GRIT:        paramGrit.store(v, std::memory_order_relaxed); break;
        case PARAM_NASALITY:    paramNasality.store(v, std::memory_order_relaxed); break;
        case PARAM_BRIGHTNESS:  paramBrightness.store(v, std::memory_order_relaxed); break;
        case PARAM_VIB_DEPTH:   paramVibDepth.store(v, std::memory_order_relaxed); break;
        case PARAM_VIB_RATE:    paramVibRate.store(v, std::memory_order_relaxed); break;
        case PARAM_TREMOR:      paramTremor.store(v, std::memory_order_relaxed); break;
        case PARAM_ROBOTIC:     paramRobotic.store(v, std::memory_order_relaxed); break;
        default: break;
    }
    uiDirty.store(true, std::memory_order_relaxed);
}

float BaburEngine::getParam(int index) const {
    switch (index) {
        case PARAM_CHARACTER:   return paramCharacter.load(std::memory_order_relaxed);
        case PARAM_DICTION:     return paramDiction.load(std::memory_order_relaxed);
        case PARAM_THROAT:      return paramThroat.load(std::memory_order_relaxed);
        case PARAM_GLIDE:       return paramGlide.load(std::memory_order_relaxed);
        case PARAM_LANG:        return paramLang.load(std::memory_order_relaxed);
        case PARAM_LEGATO:      return paramLegato.load(std::memory_order_relaxed);
        case PARAM_AUDIO_MODE:  return paramAudioMode.load(std::memory_order_relaxed);
        case PARAM_EXCITER:     return paramExciter.load(std::memory_order_relaxed);
        case PARAM_GENDER:      return paramGender.load(std::memory_order_relaxed);
        case PARAM_PITCH_SHIFT: return paramPitchShift.load(std::memory_order_relaxed);
        case PARAM_BREATHINESS: return paramBreathiness.load(std::memory_order_relaxed);
        case PARAM_GRIT:        return paramGrit.load(std::memory_order_relaxed);
        case PARAM_NASALITY:    return paramNasality.load(std::memory_order_relaxed);
        case PARAM_BRIGHTNESS:  return paramBrightness.load(std::memory_order_relaxed);
        case PARAM_VIB_DEPTH:   return paramVibDepth.load(std::memory_order_relaxed);
        case PARAM_VIB_RATE:    return paramVibRate.load(std::memory_order_relaxed);
        case PARAM_TREMOR:      return paramTremor.load(std::memory_order_relaxed);
        case PARAM_ROBOTIC:     return paramRobotic.load(std::memory_order_relaxed);
        default: return 0.0f;
    }
}

std::string BaburEngine::getParamName(int index) const {
    switch (index) {
        case PARAM_CHARACTER:   return "Character";
        case PARAM_DICTION:     return "Diction";
        case PARAM_THROAT:      return "Throat";
        case PARAM_GLIDE:       return "Glide";
        case PARAM_LANG:        return "Language";
        case PARAM_LEGATO:      return "Legato";
        case PARAM_AUDIO_MODE:  return "Audio Mode";
        case PARAM_EXCITER:     return "Voice Mode";
        case PARAM_GENDER:      return "Gender";
        case PARAM_PITCH_SHIFT: return "Pitch Shift";
        case PARAM_BREATHINESS: return "Breathiness";
        case PARAM_GRIT:        return "Grit";
        case PARAM_NASALITY:    return "Nasality";
        case PARAM_BRIGHTNESS:  return "Brightness";
        case PARAM_VIB_DEPTH:   return "Vibrato Depth";
        case PARAM_VIB_RATE:    return "Vibrato Rate";
        case PARAM_TREMOR:      return "Age / Tremor";
        case PARAM_ROBOTIC:     return "Robotic";
        default: return "";
    }
}

std::string BaburEngine::getParamDisplay(int index) const {
    return formatParamDisplay(index, getParam(index));
}

std::string BaburEngine::formatParamDisplay(int index, float val) {
    float v = std::clamp(val, 0.0f, 1.0f);
    char buf[48];
    switch (index) {
        case PARAM_CHARACTER: {
            int pct = static_cast<int>(std::round(v * 100.0f));
            if (pct <= 15) std::snprintf(buf, sizeof(buf), "SING (%d%%)", 100 - pct);
            else if (pct >= 85) std::snprintf(buf, sizeof(buf), "TALK (%d%%)", pct);
            else std::snprintf(buf, sizeof(buf), "%d%% TALK", pct);
            return buf;
        }
        case PARAM_DICTION: {
            int pct = static_cast<int>(std::round(v * 100.0f));
            std::snprintf(buf, sizeof(buf), "%d%%", pct);
            return buf;
        }
        case PARAM_THROAT: {
            float st = decodeThroatSemis(v);
            std::snprintf(buf, sizeof(buf), "%+.1f st", st);
            return buf;
        }
        case PARAM_GLIDE: {
            float ms = 8.0f + v * 212.0f;
            std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
            return buf;
        }
        case PARAM_LANG:
            return (v < 0.5f) ? "EN" : "TR";
        case PARAM_LEGATO: {
            int mode = decodeLegatoMode(v);
            if (mode == LEGATO_FLOW) return "FLOW";
            if (mode == LEGATO_STAC) return "STAC";
            return "HOLD";
        }
        case PARAM_AUDIO_MODE:
            return (v < 0.5f) ? "LOOP" : "STRETCH";
        case PARAM_EXCITER: {
            int mode = decodeVoiceMode(v);
            if (mode == VOICE_RAW) return "RAW";
            if (mode == VOICE_WHISPER) return "WHISPER";
            if (mode == VOICE_AGGR) return "AGGR";
            return "NORMAL";
        }
        case PARAM_GENDER: {
            int pct = static_cast<int>(std::round((v - 0.5f) * 200.0f));
            if (pct == 0) return "0% NEUT";
            if (pct <= -45) std::snprintf(buf, sizeof(buf), "%+d%% BASS", pct);
            else if (pct >= 70) std::snprintf(buf, sizeof(buf), "%+d%% CHILD", pct);
            else if (pct >= 25) std::snprintf(buf, sizeof(buf), "%+d%% FEM", pct);
            else std::snprintf(buf, sizeof(buf), "%+d%%", pct);
            return buf;
        }
        case PARAM_PITCH_SHIFT: {
            float st = decodePitchShiftSemis(v);
            if (std::fabs(st) < 0.05f) return "0.0 st";
            std::snprintf(buf, sizeof(buf), "%+.1f st", st);
            return buf;
        }
        case PARAM_BREATHINESS:
        case PARAM_GRIT:
        case PARAM_NASALITY:
        case PARAM_TREMOR:
        case PARAM_ROBOTIC: {
            int pct = static_cast<int>(std::round(v * 100.0f));
            std::snprintf(buf, sizeof(buf), "%d%%", pct);
            return buf;
        }
        case PARAM_BRIGHTNESS: {
            int pct = static_cast<int>(std::round((v - 0.5f) * 200.0f));
            if (pct == 0) return "0%";
            std::snprintf(buf, sizeof(buf), "%+d%%", pct);
            return buf;
        }
        case PARAM_VIB_DEPTH: {
            int pct = static_cast<int>(std::round(v * 100.0f));
            if (pct <= 0) return "OFF";
            std::snprintf(buf, sizeof(buf), "%d%%", pct);
            return buf;
        }
        case PARAM_VIB_RATE: {
            std::snprintf(buf, sizeof(buf), "%.1f Hz", decodeVibRateHz(v));
            return buf;
        }
        default:
            return "";
    }
}

void BaburEngine::setResetMidiKey(int key) {
    int normalized = (key < 0) ? -1 : std::clamp(key, 0, 127);
    resetMidiKey.store(normalized, std::memory_order_relaxed);
    learningResetKey.store(false, std::memory_order_relaxed);
    uiDirty.store(true, std::memory_order_relaxed);
}

void BaburEngine::setLearningResetKey(bool learn) {
    learningResetKey.store(learn, std::memory_order_relaxed);
    uiDirty.store(true, std::memory_order_relaxed);
}

std::string BaburEngine::formatMidiNoteName(int key) {
    if (key < 0 || key > 127) return "OFF";
    static constexpr const char* kNotes[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int octave = (key / 12) - 1;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%d (%d)", kNotes[key % 12], octave, key);
    return buf;
}

bool BaburEngine::loadSourceFile(const std::string& path) {
    if (path.empty()) return false;

    std::string lower = path;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    bool isSf2 = (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".sf2") == 0);

    if (isSf2) {
        tsf* loaded = nullptr;

        // Stream directly via 64-bit Unicode FILE* (supports >256 MB SoundFonts & Unicode paths)
        struct FileStreamCtx {
            FILE* f = nullptr;
        };
        FileStreamCtx ctx;
#if defined(_WIN32)
        ctx.f = _wfopen(AudioFileLoader::utf8ToWide(path).c_str(), L"rb");
#else
        ctx.f = std::fopen(path.c_str(), "rb");
#endif
        if (ctx.f) {
            tsf_stream stream;
            stream.data = &ctx;
            stream.read = [](void* data, void* ptr, unsigned int size) -> int {
                FILE* fp = static_cast<FileStreamCtx*>(data)->f;
                return fp ? static_cast<int>(std::fread(ptr, 1, size, fp)) : 0;
            };
            stream.skip = [](void* data, unsigned int count) -> int {
                FILE* fp = static_cast<FileStreamCtx*>(data)->f;
                if (!fp) return 0;
#if defined(_WIN32)
                return (_fseeki64(fp, static_cast<long long>(count), SEEK_CUR) == 0) ? 1 : 0;
#else
                return (std::fseek(fp, static_cast<long>(count), SEEK_CUR) == 0) ? 1 : 0;
#endif
            };
            loaded = tsf_load(&stream);
            std::fclose(ctx.f);
        }
        if (!loaded) {
            loaded = tsf_load_filename(path.c_str());
        }
        if (!loaded) return false;

        tsf_set_output(loaded, TSF_STEREO_INTERLEAVED, static_cast<int>(sampleRate), 0.0f);
        tsf_channel_set_pitchrange(loaded, 0, 24.0f);

        int count = tsf_get_presetcount(loaded);
        std::vector<std::string> names;
        names.reserve(static_cast<size_t>(std::max(0, count)));
        int firstValidPreset = 0;
        bool foundValidPreset = false;
        for (int i = 0; i < count; ++i) {
            const char* pName = tsf_get_presetname(loaded, i);
            std::string nm = pName ? pName : ("Preset " + std::to_string(i));
            if (loaded->presets && loaded->presets[i].bank > 0) {
                char bankBuf[32];
                std::snprintf(bankBuf, sizeof(bankBuf), "[B%03u] ", static_cast<unsigned>(loaded->presets[i].bank));
                nm = std::string(bankBuf) + nm;
            }
            names.push_back(std::move(nm));
            if (!foundValidPreset && loaded->presets && loaded->presets[i].regionNum > 0) {
                firstValidPreset = i;
                foundValidPreset = true;
            }
        }

        std::lock_guard<std::mutex> lock(sourceMutex);
        if (sfHandle) {
            tsf_close(sfHandle);
            sfHandle = nullptr;
        }
        audioBuf.clear();

        sfHandle = loaded;
        sfPresetNames = std::move(names);
        sfCurrentPreset = firstValidPreset;
        if (!sfPresetNames.empty()) {
            tsf_channel_set_presetindex(sfHandle, 0, sfCurrentPreset);
        }
        sourcePath = path;
        sourceFileName = extractFileName(path);
        sourceType.store(SRC_SF2, std::memory_order_release);
    } else {
        LoadedAudioBuffer buf;
        if (!AudioFileLoader::load(path, buf)) return false;

        std::lock_guard<std::mutex> lock(sourceMutex);
        if (sfHandle) {
            tsf_close(sfHandle);
            sfHandle = nullptr;
        }
        sfPresetNames.clear();
        sfCurrentPreset = 0;

        audioBuf = std::move(buf);
        audioLoopPos = 0.0;
        stretchAnchorPos = 0.0;
        stretchForward = true;
        stretchGrains[0] = { 0.0, 0.0f };
        stretchGrains[1] = { 0.0, 0.5f };
        audioEnvGain = 0.0f;

        sourcePath = path;
        sourceFileName = extractFileName(path);
        sourceType.store(SRC_AUDIO, std::memory_order_release);
    }

    noteGate = false;
    codaCarrierHold = false;
    pendingCarrierAttack = false;
    sf2VoiceRunning = false;
    phraseBridgeRemaining = 0;
    bridgeIsMidWord = false;
    hasUpcomingSyllable = false;
    hasActiveOnsetCons = false;
    onsetCarrySamples = 0;
    speechLeveler = 1.0f;
    vowelOutEnv = 0.0f;
    vowelLeveler = 1.0f;
    numHeldKeys = 0;
    uiDirty.store(true, std::memory_order_relaxed);
    return true;
}

void BaburEngine::unloadSource() {
    std::lock_guard<std::mutex> lock(sourceMutex);
    if (sfHandle) {
        tsf_close(sfHandle);
        sfHandle = nullptr;
    }
    sfPresetNames.clear();
    sfCurrentPreset = 0;
    audioBuf.clear();
    sourcePath.clear();
    sourceFileName.clear();
    sourceType.store(SRC_NONE, std::memory_order_release);
    noteGate = false;
    codaCarrierHold = false;
    pendingCarrierAttack = false;
    sf2VoiceRunning = false;
    phraseBridgeRemaining = 0;
    bridgeIsMidWord = false;
    hasUpcomingSyllable = false;
    hasActiveOnsetCons = false;
    onsetCarrySamples = 0;
    speechLeveler = 1.0f;
    vowelOutEnv = 0.0f;
    vowelLeveler = 1.0f;
    numHeldKeys = 0;
    uiDirty.store(true, std::memory_order_relaxed);
}

std::string BaburEngine::getSourcePath() const {
    std::lock_guard<std::mutex> lock(sourceMutex);
    return sourcePath;
}

std::string BaburEngine::getSourceName() const {
    std::lock_guard<std::mutex> lock(sourceMutex);
    return sourceFileName;
}

int BaburEngine::getNumPresets() const {
    std::lock_guard<std::mutex> lock(sourceMutex);
    return static_cast<int>(sfPresetNames.size());
}

std::string BaburEngine::getPresetName(int idx) const {
    std::lock_guard<std::mutex> lock(sourceMutex);
    if (idx < 0 || idx >= static_cast<int>(sfPresetNames.size())) return "";
    return sfPresetNames[static_cast<size_t>(idx)];
}

int BaburEngine::getCurrentPreset() const {
    std::lock_guard<std::mutex> lock(sourceMutex);
    return sfCurrentPreset;
}

void BaburEngine::setPresetIndex(int idx) {
    std::lock_guard<std::mutex> lock(sourceMutex);
    if (!sfHandle || sfPresetNames.empty()) return;
    int clamped = std::clamp(idx, 0, static_cast<int>(sfPresetNames.size()) - 1);
    sfCurrentPreset = clamped;
    tsf_channel_set_presetindex(sfHandle, 0, clamped);
    uiDirty.store(true, std::memory_order_relaxed);
}

void BaburEngine::setText(const std::string& utf8Text) {
    bool isTurkish = (paramLang.load(std::memory_order_relaxed) >= 0.5f);
    std::vector<SyllableToken> parsed = TextPhonemeDissector::dissectText(utf8Text, isTurkish);

    {
        std::lock_guard<std::mutex> lock(textMutex);
        lyricsText = utf8Text;
        syllables = std::move(parsed);
        int count = std::max(1, static_cast<int>(syllables.size()));
        numSyllables.store(count, std::memory_order_relaxed);

        int cur = currentSyllableIdx.load(std::memory_order_relaxed);
        int nxt = nextSyllableIdx.load(std::memory_order_relaxed);
        if (cur >= count) currentSyllableIdx.store(0, std::memory_order_relaxed);
        if (nxt >= count) nextSyllableIdx.store(0, std::memory_order_relaxed);
    }
    uiDirty.store(true, std::memory_order_relaxed);
}

std::string BaburEngine::getText() const {
    std::lock_guard<std::mutex> lock(textMutex);
    return lyricsText;
}

std::vector<SyllableToken> BaburEngine::getSyllablesCopy() const {
    std::lock_guard<std::mutex> lock(textMutex);
    return syllables;
}

void BaburEngine::setCurrentSyllableIndex(int idx) {
    std::lock_guard<std::mutex> lock(textMutex);
    int count = std::max(1, static_cast<int>(syllables.size()));
    int clamped = std::clamp(idx, 0, count - 1);
    currentSyllableIdx.store(clamped, std::memory_order_relaxed);
    nextSyllableIdx.store(clamped, std::memory_order_relaxed);
    uiDirty.store(true, std::memory_order_relaxed);
}

void BaburEngine::resetSyllableCursor() {
    setCurrentSyllableIndex(0);
}

void BaburEngine::processRawMidi(const RawMidi* events, size_t count) {
    if (!events || count == 0) return;
    std::lock_guard<std::mutex> lock(midiMutex);
    pendingMidi.insert(pendingMidi.end(), events, events + count);
}

void BaburEngine::handleMidiEvent(uint8_t msg1, uint8_t msg2, uint8_t msg3) {
    uint8_t status = msg1 & 0xF0u;
    if (status == 0x90u) {
        int key = msg2 & 0x7Fu;
        int vel = msg3 & 0x7Fu;
        if (vel > 0) triggerNoteOn(key, static_cast<float>(vel) / 127.0f);
        else triggerNoteOff(key);
    } else if (status == 0x80u) {
        triggerNoteOff(msg2 & 0x7Fu);
    } else if (status == 0xE0u) {
        pitchWheel = static_cast<int>(msg2 & 0x7Fu) | (static_cast<int>(msg3 & 0x7Fu) << 7);
    } else if (status == 0xB0u) {
        int cc = msg2 & 0x7Fu;
        if (cc == 120 || cc == 123) {
            allNotesOff(cc == 120);
        }
    }
}

void BaburEngine::clearPhonemeQueue() {
    queueHead = 0;
    queueTail = 0;
    hasActivePhoneme = false;
}

void BaburEngine::pushPhoneme(const QueuedPhoneme& qp) {
    if (queueTail < MAX_PHONEME_QUEUE) {
        phonemeQueue[static_cast<size_t>(queueTail++)] = qp;
    }
}

void BaburEngine::configureCurrentPhonemeDurations() {
    phaseSample = 0;
    if (!hasActivePhoneme) {
        syllablePhase = PHASE_IDLE;
        return;
    }

    if (currentPhoneme.kind == PH_VOWEL_CORE) {
        syllablePhase = PHASE_VOWEL_SUSTAIN;
        vowelSamples = 0;
        closureSamples = burstSamples = transSamples = 0;
        return;
    }

    syllablePhase = currentPhoneme.isCoda ? PHASE_CODA_RELEASE : PHASE_ONSET;
    float dict = std::clamp(paramDiction.load(std::memory_order_relaxed), 0.0f, 1.0f);
    float tempoSpeed = std::clamp(currentTempoScale * emoSpeedScale, 0.50f, 1.45f);
    float capScale = std::clamp(currentTempoScale, 0.72f, 1.30f);
    float durFactor = (0.45f + 0.70f * dict) * currentPhoneme.durationScale * tempoSpeed;
    float msToSamp = 0.001f * sampleRate * durFactor;

    if (currentPhoneme.unreleased) {
        closureSamples = std::clamp(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.closureMs * msToSamp)),
            static_cast<uint64_t>(0.005f * sampleRate),
            static_cast<uint64_t>(0.011f * sampleRate));
        burstSamples = 0;
        transSamples = std::max<uint64_t>(1, static_cast<uint64_t>(0.004f * sampleRate));
    } else if (!currentPhoneme.isCoda) {
        closureSamples = std::min(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.closureMs * msToSamp)),
            static_cast<uint64_t>(0.016f * capScale * sampleRate));
        burstSamples = std::min(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.burstOrFricMs * msToSamp)),
            static_cast<uint64_t>(0.026f * capScale * sampleRate));
        transSamples = static_cast<uint64_t>(std::clamp(
            currentPhoneme.cons.transitionMs * msToSamp * 0.85f,
            1.0f, 0.018f * capScale * sampleRate));
    } else if (currentPhoneme.durationScale < 0.50f) {
        closureSamples = std::min(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.closureMs * msToSamp)),
            static_cast<uint64_t>(0.008f * capScale * sampleRate));
        burstSamples = std::min(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.burstOrFricMs * msToSamp)),
            static_cast<uint64_t>(0.020f * capScale * sampleRate));
        transSamples = static_cast<uint64_t>(std::clamp(
            currentPhoneme.cons.transitionMs * msToSamp * 0.55f,
            1.0f, 0.006f * capScale * sampleRate));
    } else {
        closureSamples = std::min(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.closureMs * msToSamp)),
            static_cast<uint64_t>(0.018f * capScale * sampleRate));
        burstSamples = std::min(
            static_cast<uint64_t>(std::max(0.0f, currentPhoneme.cons.burstOrFricMs * msToSamp)),
            static_cast<uint64_t>(0.048f * capScale * sampleRate));
        transSamples = static_cast<uint64_t>(std::clamp(
            currentPhoneme.cons.transitionMs * msToSamp * 0.90f,
            1.0f, 0.020f * capScale * sampleRate));
    }

    int place = classifyConsonantPlace(currentPhoneme.cons);
    if (place == PLACE_ALVEOLAR || place == PLACE_PALATAL) {
        tipLocusF[0] = currentPhoneme.cons.locusF1;
        tipLocusF[1] = currentPhoneme.cons.locusF2;
        tipLocusF[2] = currentPhoneme.cons.locusF3;
    } else if (place == PLACE_VELAR) {
        velarLocusF[0] = currentPhoneme.cons.locusF1;
        velarLocusF[1] = currentPhoneme.cons.locusF2;
        velarLocusF[2] = currentPhoneme.cons.locusF3;
    }
}

bool BaburEngine::advancePhonemeQueue() {
    if (queueHead < queueTail) {
        currentPhoneme = phonemeQueue[static_cast<size_t>(queueHead++)];
        hasActivePhoneme = true;
        configureCurrentPhonemeDurations();
        return true;
    }
    hasActivePhoneme = false;
    return false;
}

void BaburEngine::startSyllableSequence(const SyllableToken& tok) {
    float dict = std::clamp(paramDiction.load(std::memory_order_relaxed), 0.0f, 1.0f);
    bool justEnteredCoda = (codaCarrierHold && phaseSample < 32);
    clearPhonemeQueue();
    codaCarrierHold = false;
    diphthongFinishRemaining = 0;
    diphthongMorph = 0.0f;

    // Coarticulatory memory + connected FLOW coda linking (fricative/affricate link,
    // unreleased boundary stop pinch, or subtle pre-boundary nasal/liquid gesture)
    bool midWord = !tok.isWordStart;
    if (hasPrevSyllable && (!codaConsumed || justEnteredCoda) && dict > 0.04f && !prevSyllable.codaCluster.empty()) {
        const ConsonantSpec& pc = prevSyllable.codaCluster.back();
        bool sameAsNextOnset = !tok.onsetCluster.empty() && std::strcmp(pc.label, tok.onsetCluster.front().label) == 0;
        bool nextHasOnset = !tok.onsetCluster.empty();
        if (!sameAsNextOnset && pc.type != C_NONE) {
            if (!nextHasOnset) {
                pushPhoneme({ PH_CONSONANT, pc, true, midWord ? 0.24f : 0.35f, false });
            } else if (pc.type == C_FRICATIVE_UNVOICED || pc.type == C_AFFRICATE_UNVOICED || pc.type == C_AFFRICATE_VOICED) {
                pushPhoneme({ PH_CONSONANT, pc, true, midWord ? 0.18f : 0.25f, false });
            } else if ((pc.type == C_PLOSIVE_UNVOICED || pc.type == C_PLOSIVE_VOICED) && preCodaWeight < 0.35f) {
                pushPhoneme({ PH_CONSONANT, pc, true, midWord ? 0.16f : 0.22f, true });
            } else if (pc.type == C_NASAL && preCodaWeight < 0.30f) {
                artVelum.pos = std::min(1.0f, artVelum.pos + 0.28f * dict);
            } else if (pc.type == C_LIQUID_GLIDE && preCodaWeight < 0.30f) {
                if (pc.label[0] == 'l' || pc.label[0] == 'r') {
                    artTongueTip.pos = std::min(1.0f, artTongueTip.pos + 0.25f * dict);
                }
            }
        }
    }
    preCodaWeight = 0.0f;

    activeSyllable = tok;
    codaConsumed = false;

    hasActiveOnsetCons = (dict > 0.02f) && !activeSyllable.onsetCluster.empty();
    if (hasActiveOnsetCons) {
        activeOnsetCons = activeSyllable.onsetCluster.back();
        float carryScale = std::clamp(currentTempoScale, 0.55f, 1.35f);
        onsetCarrySamples = static_cast<uint64_t>((0.028f + 0.025f * dict) * carryScale * sampleRate);
    } else {
        onsetCarrySamples = 0;
    }

    if (hasActiveOnsetCons) {
        float nOn = static_cast<float>(activeSyllable.onsetCluster.size());
        float onsetScale = (tok.isWordStart ? 1.00f : 0.58f) / nOn;
        for (const auto& cs : activeSyllable.onsetCluster) {
            if (cs.type != C_NONE) {
                pushPhoneme({ PH_CONSONANT, cs, false, onsetScale, false });
            }
        }
    }
    pushPhoneme({ PH_VOWEL_CORE, ConsonantSpec{}, false, 1.0f, false });
    advancePhonemeQueue();
}

bool BaburEngine::carrierDecayed() const {
    // A carrier counts as "decayed" once it fell >12 dB below its attack peak (piano/pluck/one-shot
    // SF2 regions) or is practically silent. Riding such a tail makes legato notes quiet or silent.
    if (sourceType.load(std::memory_order_relaxed) == SRC_SF2 && !sf2VoiceRunning) return true;
    return (carrierEnv < 0.012f) || (carrierEnv < 0.25f * carrierAttackPeak);
}

void BaburEngine::reexciteSf2Carrier(int key) {
    // Legato re-excite: releases the old voice (>=25 ms tsf release, no click) and starts a fresh one.
    if (sourceType.load(std::memory_order_relaxed) != SRC_SF2 || !sfHandle) return;
    tsf_channel_note_off_all(sfHandle, 0);
    int shiftSemis = static_cast<int>(std::round(decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed))));
    int playKey = std::clamp(key + shiftSemis, 12, 115);
    sf2BaseKey = playKey;
    tsf_channel_set_presetindex(sfHandle, 0, sfCurrentPreset);
    tsf_channel_set_tuning(sfHandle, 0, (smoothedMidiPitch + smoothPitchShift) - static_cast<float>(sf2BaseKey));
    tsf_channel_note_on(sfHandle, 0, playKey, lastVelocity);
    sf2VoiceRunning = true;
    carrierAttackPeak = 0.0f;
}

void BaburEngine::fireDeferredCarrierAttack() {
    if (pendingCarrierAttack) {
        scoopOffsetSemis = 0.16f;
    }
    pendingCarrierAttack = false;
    // Only re-seed the vowel leveler from silence; stepping its gain while a previous tail
    // is still audible produced an audible click.
    if (carrierEnv < 0.02f) {
        vowelOutEnv = lastVelocity * 0.115f;
        vowelLeveler = 1.0f;
    }
    carrierAttackPeak = 0.0f;
    int sType = sourceType.load(std::memory_order_relaxed);
    if (sType == SRC_SF2 && sfHandle) {
        tsf_channel_note_off_all(sfHandle, 0);
        tsf_channel_set_presetindex(sfHandle, 0, sfCurrentPreset);
        tsf_channel_set_tuning(sfHandle, 0, (smoothedMidiPitch + smoothPitchShift) + scoopOffsetSemis - static_cast<float>(sf2BaseKey));
        tsf_channel_note_on(sfHandle, 0, sf2BaseKey, lastVelocity);
        sf2VoiceRunning = true;
    } else if (sType == SRC_AUDIO && audioEnvGain <= 0.02f) {
        audioLoopPos = 0.0;
        stretchAnchorPos = 0.0;
        stretchForward = true;
        stretchGrains[0] = { 0.0, 0.0f };
        stretchGrains[1] = { 0.0, 0.5f };
    }
}

void BaburEngine::beginCodaReleaseAfterDiphthong() {
    float dict = std::clamp(paramDiction.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (!codaConsumed && dict > 0.04f && !activeSyllable.codaCluster.empty()) {
        diphthongMorph = 1.0f;
        codaConsumed = true;
        codaCarrierHold = true;
        clearPhonemeQueue();
        for (const auto& cs : activeSyllable.codaCluster) {
            if (cs.type != C_NONE) {
                pushPhoneme({ PH_CONSONANT, cs, true, 1.0f });
            }
        }
        if (advancePhonemeQueue()) return;
    }
    codaCarrierHold = false;
    syllablePhase = PHASE_IDLE;
    if (decodeLegatoMode(paramLegato.load(std::memory_order_relaxed)) != LEGATO_STAC) {
        bridgeIsMidWord = !activeSyllable.isWordEnd;
        phraseBridgeRemaining = static_cast<uint64_t>((bridgeIsMidWord ? 0.160f : 0.090f) * sampleRate);
    } else if (sourceType.load(std::memory_order_relaxed) == SRC_SF2 && sfHandle) {
        tsf_channel_note_off_all(sfHandle, 0);
        sf2VoiceRunning = false;
    }
}

void BaburEngine::triggerNoteOn(int key, float vel) {
    if (learningResetKey.exchange(false, std::memory_order_acq_rel)) {
        resetMidiKey.store(key, std::memory_order_relaxed);
        resetSyllableCursor();
        hasPrevSyllable = false;
        codaConsumed = true;
        uiDirty.store(true, std::memory_order_relaxed);
        return;
    }
    if (key == resetMidiKey.load(std::memory_order_relaxed)) {
        resetSyllableCursor();
        hasPrevSyllable = false;
        codaConsumed = true;
        uiDirty.store(true, std::memory_order_relaxed);
        return;
    }

    bool wasGateOpen = noteGate;
    int prevHeldCount = numHeldKeys;

    // Duplicate entries are allowed: a same-pitch overlap (NoteOn B, then NoteOff A on the
    // same key) must not close the gate while note B is still held.
    if (numHeldKeys < static_cast<int>(heldKeys.size())) {
        heldKeys[static_cast<size_t>(numHeldKeys++)] = key;
    } else {
        for (size_t i = 0; i + 1 < heldKeys.size(); ++i) heldKeys[i] = heldKeys[i + 1];
        heldKeys.back() = key;
    }

    int legatoMode = decodeLegatoMode(paramLegato.load(std::memory_order_relaxed));
    int sType = sourceType.load(std::memory_order_relaxed);
    bool wasConnected = wasGateOpen || codaCarrierHold || (phraseBridgeRemaining > 0);

    bool isLegatoOverlap = (wasGateOpen && prevHeldCount > 0);
    bool advanceSyllable = (!isLegatoOverlap) || (legatoMode != LEGATO_HOLD);

    phraseBridgeRemaining = 0;
    bridgeIsMidWord = false;
    lastVelocity = std::clamp(vel, 0.05f, 1.0f);
    targetMidiKey = key;
    noteGate = true;

    if (advanceSyllable) {
        // Track live playing cadence for tempo-adaptive articulation & pre-coda anticipation
        float deltaSec = static_cast<float>(samplesSinceLastNoteOn) / std::max(8000.0f, sampleRate);
        if (hasPrevSyllable && deltaSec >= 0.06f && deltaSec <= 1.25f) {
            avgNoteIntervalSec = lerp(avgNoteIntervalSec, std::clamp(deltaSec, 0.09f, 0.90f), 0.35f);
            cadenceConfidence = std::min(1.0f, cadenceConfidence + 0.45f);
        } else if (deltaSec > 1.80f || !hasPrevSyllable) {
            avgNoteIntervalSec = 0.32f;
            cadenceConfidence = 0.0f;
        }
        samplesSinceLastNoteOn = 0;
        currentTempoScale = std::clamp(avgNoteIntervalSec / 0.30f, 0.52f, 1.45f);

        noteHoldSamples = 0;
        if (hasActivePhoneme || syllablePhase != PHASE_IDLE) {
            prevSyllable = activeSyllable;
            hasPrevSyllable = true;
        }

        SyllableToken tok;
        SyllableToken nextTok;
        bool hasNextTok = false;
        {
            std::lock_guard<std::mutex> tLock(textMutex);
            int count = static_cast<int>(syllables.size());
            if (count > 0) {
                int playIdx = nextSyllableIdx.load(std::memory_order_relaxed) % count;
                if (playIdx < 0) playIdx = 0;
                tok = syllables[static_cast<size_t>(playIdx)];
                int nxtIdx = (playIdx + 1) % count;
                if (count > 1) {
                    nextTok = syllables[static_cast<size_t>(nxtIdx)];
                    hasNextTok = true;
                }
                currentSyllableIdx.store(playIdx, std::memory_order_relaxed);
                nextSyllableIdx.store(nxtIdx, std::memory_order_relaxed);
            }
        }
        upcomingSyllable = nextTok;
        hasUpcomingSyllable = hasNextTok;
        uiDirty.store(true, std::memory_order_relaxed);
        startSyllableSequence(tok);
        if (!wasConnected) {
            snapArticulatorsForColdStart();
        }

        if (!wasConnected || (!wasGateOpen && paramGlide.load(std::memory_order_relaxed) < 0.02f)) {
            smoothedMidiPitch = static_cast<float>(key);
        }

        int shiftSemis = static_cast<int>(std::round(decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed))));
        int shiftedKey = std::clamp(key + shiftSemis, 12, 115);

        // SF2 only counts as warm while the voice still has real energy: decaying presets
        // (piano/pluck/one-shot regions) get a fresh note instead of riding a dying tail.
        bool carrierWarm = (sType == SRC_SF2 && sfHandle && sf2VoiceRunning && !carrierDecayed()) ||
                           (sType == SRC_AUDIO &&
                            (wasGateOpen || codaCarrierHold || audioEnvGain > 0.015f));
        bool withinTuneWindow = (sType != SRC_SF2) || (std::abs(shiftedKey - sf2BaseKey) <= 14);
        bool keepContinuousCarrier = (legatoMode != LEGATO_STAC) && wasConnected && carrierWarm &&
                                     withinTuneWindow && !pendingCarrierAttack;

        // Natural speech & emotion onset pitch inflection
        bool startsVoiced = !hasActivePhoneme ||
                            currentPhoneme.kind == PH_VOWEL_CORE ||
                            !isUnvoicedObstruent(currentPhoneme.cons.type);
        EmotionProfile onsetEp = getEmotionProfile(activeSyllable.emotion);
        if (activeSyllable.emotion != EMO_NEUTRAL) {
            scoopOffsetSemis = onsetEp.onsetScoopSemis * (keepContinuousCarrier ? 0.70f : 1.0f);
        } else if (keepContinuousCarrier) {
            scoopOffsetSemis = startsVoiced ? -0.14f : 0.10f;
        } else {
            scoopOffsetSemis = startsVoiced ? -0.28f : 0.16f;
        }

        float dict = std::clamp(paramDiction.load(std::memory_order_relaxed), 0.0f, 1.0f);
        bool delayAttack = !keepContinuousCarrier &&
                           (activeSyllable.isWordStart || (!wasConnected && !carrierWarm)) &&
                           hasActivePhoneme &&
                           currentPhoneme.kind == PH_CONSONANT &&
                           !currentPhoneme.isCoda &&
                           isUnvoicedObstruent(currentPhoneme.cons.type) &&
                           dict > 0.15f;

        if (!keepContinuousCarrier || delayAttack) {
            sf2BaseKey = shiftedKey;
            if (delayAttack) {
                pendingCarrierAttack = true;
                if (sType == SRC_SF2 && sfHandle) {
                    tsf_channel_note_off_all(sfHandle, 0);
                    sf2VoiceRunning = false;
                }
            } else {
                fireDeferredCarrierAttack();
            }
        }
    } else {
        // HOLD legato overlap: same syllable keeps singing. Cancel any in-flight release state and
        // make sure we are sustaining the vowel core (never a coda / idle tract) while slurring.
        codaCarrierHold = false;
        diphthongFinishRemaining = 0;
        phraseBridgeRemaining = 0;
        bridgeIsMidWord = false;
        int shiftSemis = static_cast<int>(std::round(decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed))));
        int shiftedKey = std::clamp(key + shiftSemis, 12, 115);
        if (!hasActivePhoneme || currentPhoneme.isCoda || syllablePhase == PHASE_IDLE) {
            clearPhonemeQueue();
            codaConsumed = false;
            pushPhoneme({ PH_VOWEL_CORE, ConsonantSpec{}, false, 1.0f });
            advancePhonemeQueue();
        }
        if (pendingCarrierAttack) {
            sf2BaseKey = shiftedKey;
            fireDeferredCarrierAttack();
        } else if (sType == SRC_SF2 && sfHandle) {
            if (!sf2VoiceRunning || std::abs(shiftedKey - sf2BaseKey) > 12 || carrierDecayed()) {
                reexciteSf2Carrier(key);
            }
        }
    }
}

void BaburEngine::triggerNoteOff(int key) {
    if (key == resetMidiKey.load(std::memory_order_relaxed)) {
        return;
    }

    int found = -1;
    for (int i = 0; i < numHeldKeys; ++i) {
        if (heldKeys[static_cast<size_t>(i)] == key) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        for (int i = found; i + 1 < numHeldKeys; ++i) {
            heldKeys[static_cast<size_t>(i)] = heldKeys[static_cast<size_t>(i + 1)];
        }
        --numHeldKeys;
    }

    if (numHeldKeys > 0) {
        int fallbackKey = heldKeys[static_cast<size_t>(numHeldKeys - 1)];
        targetMidiKey = fallbackKey;
        noteGate = true;
        int sType = sourceType.load(std::memory_order_relaxed);
        int shiftSemis = static_cast<int>(std::round(decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed))));
        int shiftedFallback = std::clamp(fallbackKey + shiftSemis, 12, 115);
        if (sType == SRC_SF2 && sfHandle && (!sf2VoiceRunning || std::abs(shiftedFallback - sf2BaseKey) > 14)) {
            tsf_channel_note_off_all(sfHandle, 0);
            sf2BaseKey = shiftedFallback;
            tsf_channel_set_presetindex(sfHandle, 0, sfCurrentPreset);
            tsf_channel_set_tuning(sfHandle, 0, (smoothedMidiPitch + smoothPitchShift) - static_cast<float>(sf2BaseKey));
            tsf_channel_note_on(sfHandle, 0, shiftedFallback, lastVelocity);
            sf2VoiceRunning = true;
        }
    } else {
        noteGate = false;
        if (pendingCarrierAttack) {
            fireDeferredCarrierAttack();
        }
        if (activeSyllable.isDiphthong && diphthongMorph < 0.95f && hasActivePhoneme) {
            codaCarrierHold = true;
            diphthongFinishRemaining = static_cast<uint64_t>(
                (!activeSyllable.codaCluster.empty() ? 0.025f : 0.075f) * sampleRate);
            return;
        }
        beginCodaReleaseAfterDiphthong();
    }
}

void BaburEngine::allNotesOff(bool immediate) {
    numHeldKeys = 0;
    noteGate = false;
    codaCarrierHold = false;
    pendingCarrierAttack = false;
    sf2VoiceRunning = false;
    phraseBridgeRemaining = 0;
    bridgeIsMidWord = false;
    hasUpcomingSyllable = false;
    diphthongFinishRemaining = 0;
    clearPhonemeQueue();
    if (immediate) {
        syllablePhase = PHASE_IDLE;
        audioEnvGain = 0.0f;
    }
    if (sourceType.load(std::memory_order_relaxed) == SRC_SF2 && sfHandle) {
        if (immediate) tsf_channel_kill_all(sfHandle, 0);
        else tsf_channel_note_off_all(sfHandle, 0);
    }
}

void BaburEngine::renderAudioCarrierSample(float effectiveMidiPitch, bool stretchMode, float& outL, float& outR) {
    size_t totalFrames = audioBuf.numFrames();
    if (!audioBuf.valid || totalFrames < 2 || pendingCarrierAttack) {
        outL = outR = 0.0f;
        return;
    }

    bool carrierAlive = noteGate || codaCarrierHold || (phraseBridgeRemaining > 0);
    float bridgeLevel = bridgeIsMidWord ? 0.80f : 0.38f;
    float targetEnv = noteGate ? lastVelocity
                               : (codaCarrierHold ? lastVelocity * 0.85f
                                                  : ((phraseBridgeRemaining > 0) ? lastVelocity * bridgeLevel : 0.0f));
    float envCoeff = (carrierAlive && targetEnv >= audioEnvGain)
                         ? (1.0f - std::exp(-1.0f / (0.004f * sampleRate)))
                         : (1.0f - std::exp(-1.0f / (0.065f * sampleRate)));
    audioEnvGain += envCoeff * (targetEnv - audioEnvGain);
    if (audioEnvGain < 1e-6f && !carrierAlive) {
        outL = outR = 0.0f;
        return;
    }

    float pitchRatio = std::pow(2.0f, (effectiveMidiPitch - 69.0f) / 12.0f) * (audioBuf.sampleRate / sampleRate);
    pitchRatio = std::clamp(pitchRatio, 0.05f, 16.0f);

    double lStart = static_cast<double>(audioBuf.loopStart);
    double lEnd = static_cast<double>(audioBuf.loopEnd);
    double loopLen = std::max(16.0, lEnd - lStart);

    float sL = 0.0f, sR = 0.0f;

    if (!stretchMode) {
        double xfade = std::min(1024.0, loopLen * 0.25);
        if (audioLoopPos >= lEnd - xfade && xfade > 4.0) {
            double u = audioLoopPos - (lEnd - xfade);
            float t = std::clamp(static_cast<float>(u / xfade), 0.0f, 1.0f);
            float wA = std::cos(t * (PI_F * 0.5f));
            float wB = std::sin(t * (PI_F * 0.5f));

            double posB = lStart + u;
            sL = wA * hermite4(audioBuf.samplesL, audioLoopPos) + wB * hermite4(audioBuf.samplesL, posB);
            sR = wA * hermite4(audioBuf.samplesR, audioLoopPos) + wB * hermite4(audioBuf.samplesR, posB);
        } else {
            sL = hermite4(audioBuf.samplesL, audioLoopPos);
            sR = hermite4(audioBuf.samplesR, audioLoopPos);
        }

        audioLoopPos += static_cast<double>(pitchRatio);
        if (audioLoopPos >= lEnd) {
            audioLoopPos = lStart + xfade + std::fmod(audioLoopPos - lEnd, std::max(1.0, loopLen - xfade));
        }
    } else {
        constexpr float kGrainFrames = 2048.0f;
        float invGrain = 1.0f / kGrainFrames;
        double anchorRate = 0.40 * static_cast<double>(audioBuf.sampleRate / sampleRate);

        if (stretchForward) {
            stretchAnchorPos += anchorRate;
            if (stretchAnchorPos >= lEnd - 64.0) {
                stretchAnchorPos = std::max(lStart, lEnd - 64.0);
                stretchForward = false;
            }
        } else {
            stretchAnchorPos -= anchorRate;
            if (stretchAnchorPos <= lStart) {
                stretchAnchorPos = lStart;
                stretchForward = true;
            }
        }

        for (int g = 0; g < 2; ++g) {
            StretchGrain& gr = stretchGrains[g];
            float p = gr.phase;
            float w = 0.5f * (1.0f - std::cos(TWO_PI_F * p));
            double readPos = gr.anchor;
            if (readPos >= lEnd) {
                readPos = lStart + std::fmod(readPos - lStart, loopLen);
            } else if (readPos < lStart) {
                readPos = lStart;
            }
            sL += w * hermite4(audioBuf.samplesL, readPos);
            sR += w * hermite4(audioBuf.samplesR, readPos);

            gr.anchor = readPos + static_cast<double>(pitchRatio);
            gr.phase += invGrain;
            if (gr.phase >= 1.0f) {
                gr.phase -= 1.0f;
                gr.anchor = stretchAnchorPos;
            }
        }
    }

    outL = sL * audioEnvGain;
    outR = sR * audioEnvGain;
}

float BaburEngine::nextNoiseSample() {
    uint32_t x = noiseRngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    noiseRngState = x;
    return (static_cast<float>(x & 0xFFFFu) / 32768.0f) - 1.0f;
}

void BaburEngine::render(float* outL, float* outR, int numFrames) {
    if (!outL || !outR || numFrames <= 0) return;
    ScopedDenormalsZero ftzGuard;

    {
        std::lock_guard<std::mutex> mLock(midiMutex);
        renderMidiScratch.swap(pendingMidi);
    }

    if (renderMidiScratch.size() > 1) {
        std::stable_sort(renderMidiScratch.begin(), renderMidiScratch.end(),
            [](const RawMidi& a, const RawMidi& b) {
                if (a.offset != b.offset) return a.offset < b.offset;
                auto isOff = [](const RawMidi& ev) {
                    uint8_t st = ev.msg1 & 0xF0u;
                    return st == 0x80u || (st == 0x90u && (ev.msg3 & 0x7Fu) == 0);
                };
                return isOff(a) && !isOff(b);
            });
    }

    std::unique_lock<std::mutex> srcLock(sourceMutex, std::try_to_lock);
    int sType = srcLock.owns_lock() ? sourceType.load(std::memory_order_acquire) : SRC_NONE;

    if (sType == SRC_NONE) {
        for (const auto& ev : renderMidiScratch) {
            handleMidiEvent(ev.msg1, ev.msg2, ev.msg3);
        }
        renderMidiScratch.clear();
        std::fill(outL, outL + numFrames, 0.0f);
        std::fill(outR, outR + numFrames, 0.0f);
        return;
    }

    size_t midiCursor = 0;
    int rendered = 0;

    while (rendered < numFrames) {
        int chunk = std::min(BLOCK_CHUNK, numFrames - rendered);

        while (midiCursor < renderMidiScratch.size() &&
               renderMidiScratch[midiCursor].offset <= rendered) {
            const auto& ev = renderMidiScratch[midiCursor];
            handleMidiEvent(ev.msg1, ev.msg2, ev.msg3);
            ++midiCursor;
        }

        float tgtChar = paramCharacter.load(std::memory_order_relaxed);
        float tgtDict = paramDiction.load(std::memory_order_relaxed);
        float tgtThroat = paramThroat.load(std::memory_order_relaxed);
        float tgtGlide = paramGlide.load(std::memory_order_relaxed);
        float tgtGender = (paramGender.load(std::memory_order_relaxed) - 0.5f) * 2.0f;
        float tgtPitchShift = decodePitchShiftSemis(paramPitchShift.load(std::memory_order_relaxed));
        float tgtBreathiness = paramBreathiness.load(std::memory_order_relaxed);
        float tgtGrit = paramGrit.load(std::memory_order_relaxed);
        float tgtNasality = paramNasality.load(std::memory_order_relaxed);
        float tgtBrightness = (paramBrightness.load(std::memory_order_relaxed) - 0.5f) * 2.0f;
        float tgtVibDepth = paramVibDepth.load(std::memory_order_relaxed);
        float tgtVibRate = decodeVibRateHz(paramVibRate.load(std::memory_order_relaxed));
        float tgtTremor = paramTremor.load(std::memory_order_relaxed);
        float tgtRobotic = paramRobotic.load(std::memory_order_relaxed);
        int voiceMode = decodeVoiceMode(paramExciter.load(std::memory_order_relaxed));
        int legatoMode = decodeLegatoMode(paramLegato.load(std::memory_order_relaxed));
        bool stretchMode = (paramAudioMode.load(std::memory_order_relaxed) >= 0.5f);

        float paramCoeff = 1.0f - std::exp(-static_cast<float>(chunk) / (0.015f * sampleRate));
        smoothChar += paramCoeff * (tgtChar - smoothChar);
        smoothDiction += paramCoeff * (tgtDict - smoothDiction);
        smoothThroat += paramCoeff * (tgtThroat - smoothThroat);
        smoothGlide += paramCoeff * (tgtGlide - smoothGlide);
        smoothGender += paramCoeff * (tgtGender - smoothGender);
        smoothPitchShift += paramCoeff * (tgtPitchShift - smoothPitchShift);
        smoothBreathiness += paramCoeff * (tgtBreathiness - smoothBreathiness);
        smoothGrit += paramCoeff * (tgtGrit - smoothGrit);
        smoothNasality += paramCoeff * (tgtNasality - smoothNasality);
        smoothBrightness += paramCoeff * (tgtBrightness - smoothBrightness);
        smoothVibDepth += paramCoeff * (tgtVibDepth - smoothVibDepth);
        smoothVibRate += paramCoeff * (tgtVibRate - smoothVibRate);
        smoothTremor += paramCoeff * (tgtTremor - smoothTremor);
        smoothRobotic += paramCoeff * (tgtRobotic - smoothRobotic);

        float humanAmt = 1.0f - smoothRobotic;
        float glideSec = ((numHeldKeys > 1 || legatoMode != LEGATO_STAC) ? 0.016f : 0.008f) + smoothGlide * 0.210f;
        glideSec *= lerp(1.0f, 0.14f, smoothRobotic);
        float pitchCoeff = 1.0f - std::exp(-1.0f / (glideSec * sampleRate));
        float scoopDecay = std::exp(-1.0f / (std::max(0.012f, emoScoopDecaySec) * sampleRate));
        float midiBendSemis = (static_cast<float>(pitchWheel - 8192) / 8192.0f) * 2.0f;
        float throatScale = std::pow(2.0f, decodeThroatSemis(smoothThroat) / 12.0f);

        float vibDepthScale = (smoothVibDepth / 0.25f) * humanAmt;
        float effVibDelay = (smoothVibDepth > 0.25f)
            ? lerp(emoVibDelaySec, 0.06f, std::min(1.0f, (smoothVibDepth - 0.25f) / 0.75f))
            : emoVibDelaySec;
        float effVibDepth = emoVibDepthSemis * vibDepthScale;
        float effVibRateHz = std::clamp(emoVibRate + (smoothVibRate - 5.2f), 2.2f, 10.5f);
        float effJitterScale = (emoJitterScale + smoothTremor * 2.60f) * humanAmt;
        float blockTremorPitch = smoothTremor * 0.34f * std::sin(static_cast<float>(tremorPhase)) * (0.65f + 0.35f * jitterSmooth) * humanAmt;

        float vibratoDepth = 0.0f;
        uint64_t vibDelaySamp = static_cast<uint64_t>(std::max(0.04f, effVibDelay) * sampleRate);
        if (noteGate && noteHoldSamples > vibDelaySamp) {
            float rampSec = (effVibDelay < 0.15f) ? 0.18f : 0.50f;
            float ramp = static_cast<float>(noteHoldSamples - vibDelaySamp) / (rampSec * sampleRate);
            vibratoDepth = std::min(1.0f, ramp);
        }

        float prosFadeBlock = (noteHoldSamples < static_cast<uint64_t>(0.25f * sampleRate))
            ? 1.0f
            : std::max(0.0f, 1.0f - static_cast<float>(noteHoldSamples - static_cast<uint64_t>(0.25f * sampleRate)) / (0.25f * sampleRate));
        float emoBiasStress = activeSyllable.isStressed ? 1.15f : 0.45f;
        float blockDroop = (noteGate && noteHoldSamples > static_cast<uint64_t>(0.08f * sampleRate))
            ? emoHoldDroopSemis * std::min(1.0f, static_cast<float>(noteHoldSamples - static_cast<uint64_t>(0.08f * sampleRate)) / (0.28f * sampleRate))
            : 0.0f;

        if (sType == SRC_SF2 && sfHandle) {
            if (!pendingCarrierAttack) {
                float vib = vibratoDepth * effVibDepth * std::sin(static_cast<float>(vibratoPhase));
                float sf2Tuning = (smoothedMidiPitch + smoothPitchShift - static_cast<float>(sf2BaseKey)) + midiBendSemis + vib +
                                  (scoopOffsetSemis + smoothProsodyPitch + emoPitchBias * emoBiasStress * prosFadeBlock + blockDroop) * humanAmt +
                                  blockTremorPitch + jitterSmooth * 0.12f * effJitterScale;
                tsf_channel_set_tuning(sfHandle, 0, sf2Tuning);
                tsf_render_float(sfHandle, scratchRaw.data(), chunk, 0);
            } else {
                std::fill(scratchRaw.data(), scratchRaw.data() + chunk * 2, 0.0f);
            }
        }

        float hpExcHz = (voiceMode == VOICE_AGGR) ? 600.0f : 820.0f;
        float hpExcAlpha = 1.0f - std::exp(-TWO_PI_F * hpExcHz / sampleRate);
        float tiltLpAlpha = 1.0f - std::exp(-TWO_PI_F * 1100.0f / sampleRate);
        float softBreathAlpha = 1.0f - std::exp(-TWO_PI_F * 1650.0f / sampleRate);
        float syllEnvCoeff = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
        float f1CutCoeff = 1.0f - std::exp(-1.0f / (0.003f * sampleRate));
        float envAtt = 1.0f - std::exp(-1.0f / (0.0015f * sampleRate));
        float envRel = 1.0f - std::exp(-1.0f / (0.032f * sampleRate));
        float levelUpCoeff = 1.0f - std::exp(-1.0f / (0.028f * sampleRate));
        float levelDownCoeff = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
        float bandAtt = 1.0f - std::exp(-1.0f / (0.002f * sampleRate));
        float bandRel = 1.0f - std::exp(-1.0f / (0.028f * sampleRate));
        float gateSmoothCoeff = 1.0f - std::exp(-1.0f / (0.0025f * sampleRate));
        float formEnvAtt = 1.0f - std::exp(-1.0f / (0.0025f * sampleRate));
        float formEnvRel = 1.0f - std::exp(-1.0f / (0.020f * sampleRate));
        float servoDownCoeff = 1.0f - std::exp(-1.0f / (0.009f * sampleRate));
        float servoUpCoeff = 1.0f - std::exp(-1.0f / (0.030f * sampleRate));
        float limAtt = 1.0f - std::exp(-1.0f / (0.0005f * sampleRate));
        float limRel = 1.0f - std::exp(-1.0f / (0.120f * sampleRate));

        float whiteMixSing = std::clamp(0.45f + 0.30f * smoothDiction, 0.45f, 0.78f);
        float whiteMixTalk = (voiceMode == VOICE_RAW) ? 0.55f : 0.92f;

        float vowelNasalColor = 0.0f;
        float f2FrontCached = 0.0f;
        float transPresence = 1.0f;
        float transF3Boost = 1.0f;
        float curPitchCached = smoothedMidiPitch;
        float prosodyHoldFadeCached = prosFadeBlock;
        bool lateralZeroActive = false;

        for (int s = 0; s < chunk; ++s) {
            if ((s & (FORMANT_SUBSTEP - 1)) == 0) {
                if (pendingCarrierAttack) {
                    bool shouldFire = !hasActivePhoneme ||
                                      currentPhoneme.kind == PH_VOWEL_CORE ||
                                      !isUnvoicedObstruent(currentPhoneme.cons.type) ||
                                      (phaseSample >= closureSamples + burstSamples);
                    if (shouldFire) {
                        fireDeferredCarrierAttack();
                    }
                }

                const VowelFormants& vf1 = getVowelFormants(activeSyllable.vowel1);
                const VowelFormants& vf2 = getVowelFormants(activeSyllable.vowel2);

                float startF1 = (activeSyllable.contextF1 > 0.0f) ? activeSyllable.contextF1 : vf1.f1;
                float startF2 = (activeSyllable.contextF2 > 0.0f) ? activeSyllable.contextF2 : vf1.f2;
                float startF3 = (activeSyllable.contextF3 > 0.0f) ? activeSyllable.contextF3 : vf1.f3;
                float endF1   = (activeSyllable.contextEndF1 > 0.0f) ? activeSyllable.contextEndF1 : vf2.f1;
                float endF2   = (activeSyllable.contextEndF2 > 0.0f) ? activeSyllable.contextEndF2 : vf2.f2;
                float endF3   = (activeSyllable.contextEndF3 > 0.0f) ? activeSyllable.contextEndF3 : vf2.f3;

                float vowF[NUM_FORMANTS] = {
                    lerp(startF1, endF1, diphthongMorph),
                    lerp(startF2, endF2, diphthongMorph),
                    lerp(startF3, endF3, diphthongMorph),
                    lerp(vf1.f4,  vf2.f4, diphthongMorph)
                };
                float vowQ[NUM_FORMANTS] = { vf1.q1, vf1.q2, vf1.q3, vf1.q4 };

                vowelNasalColor = 0.0f;
                if (hasActivePhoneme && currentPhoneme.kind == PH_VOWEL_CORE) {
                    float prog = std::min(1.0f, static_cast<float>(vowelSamples) / (0.085f * sampleRate));
                    if (activeSyllable.nasalized) {
                        vowelNasalColor = 0.16f + 0.24f * prog;
                    }
                    if (hasActiveOnsetCons && vowelSamples < onsetCarrySamples) {
                        float linC = 1.0f - static_cast<float>(vowelSamples) / static_cast<float>(std::max<uint64_t>(1, onsetCarrySamples));
                        float carryW = linC * linC;
                        vowF[0] = lerp(vowF[0], activeOnsetCons.locusF1, 0.42f * carryW);
                        vowF[1] = lerp(vowF[1], activeOnsetCons.locusF2, 0.42f * carryW);
                        vowF[2] = lerp(vowF[2], activeOnsetCons.locusF3, 0.42f * carryW);
                        if (activeOnsetCons.type == C_NASAL) {
                            vowelNasalColor = std::max(vowelNasalColor, 0.45f * smoothDiction * carryW);
                        }
                    }
                    if (activeSyllable.rColored) {
                        float rTargetF3 = (paramLang.load(std::memory_order_relaxed) >= 0.5f) ? 2100.0f : 1620.0f;
                        vowF[2] = lerp(vowF[2], rTargetF3, 0.25f * prog);
                    }
                    if (activeSyllable.lColored) {
                        vowF[1] = lerp(vowF[1], activeSyllable.coda.locusF2, 0.20f * prog);
                    }
                }

                // 6-Articulator Gestural Physics, Emotion Profile, Organic 1/f Drift & Pre-Coda Anticipation
                float dtSub = static_cast<float>(FORMANT_SUBSTEP) / std::max(8000.0f, sampleRate);
                float prosCoeff = 1.0f - std::exp(-dtSub / 0.020f);
                smoothProminence += prosCoeff * (std::clamp(activeSyllable.prominence, 0.72f, 1.26f) - smoothProminence);
                prosodyHoldFadeCached = (noteHoldSamples < static_cast<uint64_t>(0.25f * sampleRate))
                    ? 1.0f
                    : std::max(0.0f, 1.0f - static_cast<float>(noteHoldSamples - static_cast<uint64_t>(0.25f * sampleRate)) / (0.25f * sampleRate));
                float tgtProsPitch = noteGate ? (activeSyllable.prosodyPitchSemis * prosodyHoldFadeCached) : activeSyllable.prosodyPitchSemis;
                smoothProsodyPitch += prosCoeff * (tgtProsPitch - smoothProsodyPitch);

                EmotionProfile ep = getEmotionProfile(activeSyllable.emotion);
                float emoCoeff = 1.0f - std::exp(-dtSub / 0.035f);
                emoTilt += emoCoeff * (ep.tilt - emoTilt);
                emoBreath += emoCoeff * (ep.breath - emoBreath);
                emoFormantScale += emoCoeff * (ep.formantScale - emoFormantScale);
                emoJawScale += emoCoeff * (ep.jawScale - emoJawScale);
                emoSpeedScale += emoCoeff * (ep.speedScale - emoSpeedScale);
                emoDictionScale += emoCoeff * (ep.dictionScale - emoDictionScale);
                emoPitchBias += emoCoeff * (ep.pitchBias - emoPitchBias);
                emoVibRate += emoCoeff * (ep.vibRate - emoVibRate);
                emoEdgeDrive += emoCoeff * (ep.edgeDrive - emoEdgeDrive);
                emoLarynxScale += emoCoeff * (ep.larynxScale - emoLarynxScale);
                emoQScale += emoCoeff * (ep.qScale - emoQScale);
                emoWhisperMix += emoCoeff * (ep.whisperMix - emoWhisperMix);
                emoRoughness += emoCoeff * (ep.roughness - emoRoughness);
                emoNasalFloor += emoCoeff * (ep.nasalFloor - emoNasalFloor);
                emoVibDelaySec += emoCoeff * (ep.vibDelaySec - emoVibDelaySec);
                emoVibDepthSemis += emoCoeff * (ep.vibDepthSemis - emoVibDepthSemis);
                emoJitterScale += emoCoeff * (ep.jitterScale - emoJitterScale);
                emoScoopDecaySec += emoCoeff * (ep.scoopDecaySec - emoScoopDecaySec);
                emoHoldDroopSemis += emoCoeff * (ep.holdDroopSemis - emoHoldDroopSemis);
                emoBloomMs += emoCoeff * (ep.bloomMs - emoBloomMs);
                emoBloomStartGain += emoCoeff * (ep.bloomStartGain - emoBloomStartGain);
                emoDynGain += emoCoeff * (ep.dynGain - emoDynGain);

                // 1/f random-walk vocal-fold micro-drift (jitter & shimmer)
                jitterWalk = jitterWalk * 0.94f + nextNoiseSample() * 0.06f;
                jitterSmooth += (1.0f - std::exp(-dtSub / 0.045f)) * (jitterWalk - jitterSmooth);
                shimmerSmooth += (1.0f - std::exp(-dtSub / 0.038f)) * (nextNoiseSample() * 0.14f - shimmerSmooth);

                float vowLipRound = lerp(vowelLipRounding(activeSyllable.vowel1),
                                         vowelLipRounding(activeSyllable.vowel2),
                                         diphthongMorph);

                float tgtJaw = std::clamp(((vowF[0] - 250.0f) / 530.0f) * emoJawScale, -0.10f, 1.65f);
                float tgtTBX = std::clamp((vowF[1] - 1480.0f) / 960.0f, -1.10f, 1.50f);
                float tgtTBY = std::clamp(1.0f - tgtJaw, 0.0f, 1.0f);
                float tgtLipRound = vowLipRound;
                float tgtVelum = std::max(vowelNasalColor, std::clamp(emoNasalFloor + smoothNasality * 0.78f, 0.0f, 0.88f));
                float tgtLipClose = 0.0f;
                float tgtTip = 0.0f;
                float tgtVelar = 0.0f;
                float tgtF3 = vowF[2];
                float tgtF4 = vowF[3];

                // Anticipatory coarticulation lookahead + beat-timed pre-coda anticipation in PH_VOWEL_CORE
                preCodaWeight = 0.0f;
                if (hasActivePhoneme && currentPhoneme.kind == PH_VOWEL_CORE) {
                    float vowelSec = static_cast<float>(vowelSamples) / std::max(8000.0f, sampleRate);
                    if (hasUpcomingSyllable && activeSyllable.breakAfter == SyllableToken::BREAK_NONE && vowelSec > 0.065f) {
                        float anticU = std::clamp((vowelSec - 0.065f) / 0.140f, 0.0f, 1.0f);
                        if (activeSyllable.codaCluster.empty()) {
                            float nextLip = vowelLipRounding(upcomingSyllable.vowel1);
                            if (!upcomingSyllable.onsetCluster.empty() &&
                                std::strcmp(upcomingSyllable.onsetCluster.front().label, "w") == 0) {
                                nextLip = 0.95f;
                            }
                            tgtLipRound = lerp(tgtLipRound, nextLip, 0.28f * anticU);
                            if (!upcomingSyllable.onsetCluster.empty() &&
                                upcomingSyllable.onsetCluster.front().type == C_NASAL) {
                                tgtVelum = std::max(tgtVelum, 0.22f * anticU);
                            }
                            if (upcomingSyllable.contextF2 > 0.0f) {
                                float nextTBX = std::clamp((upcomingSyllable.contextF2 - 1480.0f) / 960.0f, -1.10f, 1.50f);
                                tgtTBX = lerp(tgtTBX, nextTBX, 0.14f * anticU);
                            }
                        }
                    }

                    // Smoothly prepare active syllable's own coda gesture just before expected next beat
                    if (!activeSyllable.codaCluster.empty() && cadenceConfidence > 0.35f && avgNoteIntervalSec < 0.65f) {
                        float beatU = vowelSec / std::max(0.09f, avgNoteIntervalSec);
                        if (beatU > 0.62f && beatU < 1.30f) {
                            float bw = (beatU < 0.92f) ? ((beatU - 0.62f) / 0.30f) : (1.0f - (beatU - 0.92f) / 0.38f);
                            preCodaWeight = std::clamp(bw, 0.0f, 1.0f) * cadenceConfidence;
                        }
                        if (preCodaWeight > 0.01f) {
                            const ConsonantSpec& pc = activeSyllable.codaCluster.front();
                            int cPlace = classifyConsonantPlace(pc);
                            if (pc.type == C_NASAL) {
                                tgtVelum = std::max(tgtVelum, 0.72f * preCodaWeight);
                            }
                            if (cPlace == PLACE_BILABIAL) {
                                tgtLipClose = std::max(tgtLipClose, 0.45f * preCodaWeight);
                            } else if (cPlace == PLACE_ALVEOLAR || cPlace == PLACE_PALATAL) {
                                tgtTip = std::max(tgtTip, 0.45f * preCodaWeight);
                                tipLocusF[0] = pc.locusF1; tipLocusF[1] = pc.locusF2; tipLocusF[2] = pc.locusF3;
                            } else if (cPlace == PLACE_VELAR) {
                                tgtVelar = std::max(tgtVelar, 0.45f * preCodaWeight);
                                velarLocusF[0] = pc.locusF1; velarLocusF[1] = pc.locusF2; velarLocusF[2] = pc.locusF3;
                            }
                        }
                    }
                }
                if (!noteGate && !codaCarrierHold && phraseBridgeRemaining > 0 && bridgeIsMidWord && hasUpcomingSyllable) {
                    float brMax = std::max(1.0f, 0.160f * sampleRate);
                    float brU = std::clamp(1.0f - static_cast<float>(phraseBridgeRemaining) / brMax, 0.0f, 1.0f);
                    float morphW = 0.45f * brU;
                    const VowelFormants& uVf = getVowelFormants(upcomingSyllable.vowel1);
                    float uF1 = (upcomingSyllable.contextF1 > 0.0f) ? upcomingSyllable.contextF1 : uVf.f1;
                    float uF2 = (upcomingSyllable.contextF2 > 0.0f) ? upcomingSyllable.contextF2 : uVf.f2;
                    tgtJaw = lerp(tgtJaw, std::clamp(((uF1 - 250.0f) / 530.0f) * emoJawScale, -0.10f, 1.65f), morphW);
                    tgtTBX = lerp(tgtTBX, std::clamp((uF2 - 1480.0f) / 960.0f, -1.10f, 1.50f), morphW);
                    tgtLipRound = lerp(tgtLipRound, vowelLipRounding(upcomingSyllable.vowel1), morphW);
                    if (!upcomingSyllable.onsetCluster.empty() && upcomingSyllable.onsetCluster.front().type == C_NASAL) {
                        tgtVelum = std::max(tgtVelum, 0.38f * brU);
                    }
                }

                float tgtQ[NUM_FORMANTS] = { vowQ[0], vowQ[1], vowQ[2], vowQ[3] };
                bool inBurstHold = false;
                if (hasActivePhoneme && currentPhoneme.kind == PH_CONSONANT) {
                    const ConsonantSpec& cs = currentPhoneme.cons;
                    uint64_t burstEnd = closureSamples + burstSamples;
                    float transU = 0.0f;
                    if (phaseSample >= burstEnd && transSamples > 0) {
                        float linU = std::clamp(static_cast<float>(phaseSample - burstEnd) / static_cast<float>(transSamples), 0.0f, 1.0f);
                        transU = std::pow(linU, 1.85f);
                    }
                    inBurstHold = (phaseSample < burstEnd);
                    float locusWeight = inBurstHold ? 1.0f : (1.0f - transU);

                    if (cs.type == C_NASAL) {
                        tgtVelum = std::max(tgtVelum, cs.murmurGain * smoothDiction * locusWeight);
                        tgtQ[0] = lerp(vowQ[0], 5.8f, locusWeight);
                        tgtQ[1] = lerp(vowQ[1], 4.2f, locusWeight);
                    }

                    int place = classifyConsonantPlace(cs);
                    if (place == PLACE_BILABIAL) {
                        tgtLipClose = locusWeight;
                        tgtJaw = lerp(tgtJaw, 0.08f, locusWeight * 0.85f);
                    } else if (place == PLACE_LABIODENTAL) {
                        tgtLipClose = 0.72f * locusWeight;
                        tgtJaw = lerp(tgtJaw, 0.18f, locusWeight * 0.65f);
                    } else if (place == PLACE_ALVEOLAR) {
                        tgtTip = locusWeight;
                        tipLocusF[0] = cs.locusF1;
                        tipLocusF[1] = cs.locusF2;
                        tipLocusF[2] = cs.locusF3;
                        tgtJaw = lerp(tgtJaw, (cs.locusF1 - 250.0f) / 530.0f, locusWeight * 0.65f);
                        float alvTBX = std::clamp((cs.locusF2 - 1480.0f) / 960.0f, -1.0f, 1.0f);
                        tgtTBX = lerp(tgtTBX, alvTBX, locusWeight * 0.52f);
                    } else if (place == PLACE_PALATAL) {
                        tgtTip = 0.85f * locusWeight;
                        tipLocusF[0] = cs.locusF1;
                        tipLocusF[1] = cs.locusF2;
                        tipLocusF[2] = cs.locusF3;
                        float palTBX = std::clamp((cs.locusF2 - 1480.0f) / 960.0f, -1.0f, 1.0f);
                        tgtTBX = lerp(tgtTBX, palTBX, locusWeight * 0.75f);
                        tgtLipRound = std::max(tgtLipRound, 0.35f * locusWeight);
                    } else if (place == PLACE_VELAR) {
                        tgtVelar = locusWeight;
                        velarLocusF[0] = cs.locusF1;
                        velarLocusF[1] = cs.locusF2;
                        velarLocusF[2] = cs.locusF3;
                        tgtTBY = lerp(tgtTBY, 0.95f, locusWeight);
                        float velTBX = std::clamp((cs.locusF2 - 1480.0f) / 960.0f, -1.0f, 1.0f);
                        tgtTBX = lerp(tgtTBX, velTBX, locusWeight * 0.85f);
                    } else if (place == PLACE_LABIAL_VELAR) {
                        tgtLipRound = lerp(tgtLipRound, 1.0f, locusWeight);
                        tgtTBX = lerp(tgtTBX, -0.82f, locusWeight);
                        tgtJaw = lerp(tgtJaw, 0.12f, locusWeight);
                    }
                }

                float gScale = (0.55f + 1.80f * smoothGlide) * std::clamp(currentTempoScale * emoSpeedScale, 0.55f, 1.45f) *
                               (1.0f + 0.20f * smoothTremor) * lerp(1.0f, 0.55f, smoothRobotic);
                artJaw.step(tgtJaw, dtSub, 0.017f * gScale);
                artTongueBodyX.step(tgtTBX, dtSub, 0.015f * gScale);
                artTongueBodyY.step(tgtTBY, dtSub, 0.015f * gScale);
                artLips.step(tgtLipRound, dtSub, 0.013f * gScale);
                artLipClose.step(tgtLipClose, dtSub, inBurstHold ? 0.0045f : (0.016f * gScale));
                artTongueTip.step(tgtTip, dtSub, inBurstHold ? 0.0040f : (0.015f * gScale));
                artVelarClose.step(tgtVelar, dtSub, inBurstHold ? 0.0045f : (0.017f * gScale));
                float velumTau = (tgtVelum >= artVelum.pos) ? 0.016f : 0.011f;
                artVelum.step(tgtVelum, dtSub, velumTau * (0.75f + 0.50f * smoothGlide));
                artF3.step(tgtF3, dtSub, 0.015f * gScale);
                artF4.step(tgtF4, dtSub, 0.018f * gScale);

                float f1Rec = 250.0f + 530.0f * std::clamp(artJaw.pos, -0.10f, 1.65f);
                float f2Rec = 1480.0f + 960.0f * std::clamp(artTongueBodyX.pos, -1.10f, 1.50f);
                float f3Rec = artF3.pos;
                float f4Rec = artF4.pos;

                // Subtle organic formant wander + emotion smile/darken scaling during vowels
                bool isOpenVowel = (!hasActivePhoneme || currentPhoneme.kind == PH_VOWEL_CORE);
                if (isOpenVowel) {
                    f2Rec *= emoFormantScale;
                    f3Rec *= (0.5f + 0.5f * emoFormantScale);
                    f1Rec += jitterSmooth * 18.0f * humanAmt;
                    f2Rec += jitterSmooth * 28.0f * humanAmt;
                }

                float dLipRound = artLips.pos - vowLipRound;
                f2Rec -= 260.0f * dLipRound;
                f3Rec -= 160.0f * dLipRound;

                float lc = std::clamp(artLipClose.pos, 0.0f, 1.0f);
                if (lc > 0.001f) {
                    f1Rec = lerp(f1Rec, 235.0f, lc * 0.92f);
                    f2Rec = lerp(f2Rec, 860.0f, lc * 0.90f);
                    f3Rec = lerp(f3Rec, 2120.0f, lc * 0.72f);
                }
                float tt = std::clamp(artTongueTip.pos, 0.0f, 1.0f);
                if (tt > 0.001f) {
                    f1Rec = lerp(f1Rec, tipLocusF[0], tt * 0.90f);
                    f2Rec = lerp(f2Rec, tipLocusF[1], tt * 0.90f);
                    f3Rec = lerp(f3Rec, tipLocusF[2], tt * 0.78f);
                }
                float vc = std::clamp(artVelarClose.pos, 0.0f, 1.0f);
                if (vc > 0.001f) {
                    f1Rec = lerp(f1Rec, velarLocusF[0], vc * 0.90f);
                    f2Rec = lerp(f2Rec, velarLocusF[1], vc * 0.88f);
                    f3Rec = lerp(f3Rec, velarLocusF[2], vc * 0.80f);
                }

                smoothF[0] = std::clamp(f1Rec, 180.0f, 1150.0f);
                smoothF[1] = std::clamp(f2Rec, 550.0f, 3000.0f);
                smoothF[2] = std::clamp(f3Rec, 1400.0f, 3600.0f);
                smoothF[3] = std::clamp(f4Rec, 2600.0f, 4400.0f);

                float qSmoothTime = (activeSyllable.isDiphthong && diphthongMorph > 0.0f && diphthongMorph < 1.0f)
                                    ? (0.010f + smoothGlide * 0.045f)
                                    : (0.014f + smoothGlide * 0.100f);
                float subCoeff = 1.0f - std::exp(-dtSub / qSmoothTime);
                float vibVal = vibratoDepth * std::sin(static_cast<float>(vibratoPhase));

                float subHoldDroop = (noteGate && noteHoldSamples > static_cast<uint64_t>(0.08f * sampleRate))
                    ? emoHoldDroopSemis * std::min(1.0f, static_cast<float>(noteHoldSamples - static_cast<uint64_t>(0.08f * sampleRate)) / (0.28f * sampleRate))
                    : 0.0f;
                float curPitch = smoothedMidiPitch + smoothPitchShift + midiBendSemis +
                                 (scoopOffsetSemis + smoothProsodyPitch + emoPitchBias * emoBiasStress * prosodyHoldFadeCached + subHoldDroop) * humanAmt +
                                 blockTremorPitch + jitterSmooth * 0.12f * effJitterScale;
                curPitchCached = curPitch;
                float f0Hz = std::clamp(440.0f * std::pow(2.0f, (curPitch - 69.0f) / 12.0f), 55.0f, 1400.0f);
                bool inVowelCore = (!hasActivePhoneme || currentPhoneme.kind == PH_VOWEL_CORE) && (voiceMode != VOICE_WHISPER);
                float voicingCore = inVowelCore ? std::clamp(smoothOralGate, 0.0f, 1.0f) : 0.0f;
                float effThroatScale = throatScale * emoLarynxScale;

                // Non-uniform Gender/Age vocal-tract scaling (1.0 at smoothGender == 0.0)
                float genderFScale[NUM_FORMANTS] = { 1.0f, 1.0f, 1.0f, 1.0f };
                if (smoothGender > 0.001f) {
                    genderFScale[0] = 1.0f + smoothGender * (0.28f + 0.14f * std::clamp((smoothF[0] - 320.0f) / 450.0f, 0.0f, 1.0f));
                    genderFScale[1] = 1.0f + smoothGender * (0.24f + 0.10f * std::clamp((smoothF[1] - 900.0f) / 1400.0f, 0.0f, 1.0f));
                    genderFScale[2] = 1.0f + smoothGender * 0.24f;
                    genderFScale[3] = 1.0f + smoothGender * 0.20f;
                } else if (smoothGender < -0.001f) {
                    genderFScale[0] = 1.0f + smoothGender * 0.24f;
                    genderFScale[1] = 1.0f + smoothGender * 0.26f;
                    genderFScale[2] = 1.0f + smoothGender * 0.22f;
                    genderFScale[3] = 1.0f + smoothGender * 0.18f;
                }

                static constexpr float kHarmGravity[NUM_FORMANTS] = { 0.28f, 0.22f, 0.10f, 0.00f };
                static constexpr float kBaseBwHz[NUM_FORMANTS] = { 52.0f, 70.0f, 95.0f, 130.0f };

                float shiftedF[NUM_FORMANTS]{};
                float barkBw[NUM_FORMANTS]{};
                for (int f = 0; f < NUM_FORMANTS; ++f) {
                    float nomHz = std::clamp(smoothF[f] * effThroatScale * genderFScale[f] * (1.0f + 0.012f * vibVal), 110.0f, 0.44f * sampleRate);
                    float pull = (kHarmGravity[f] * voicingCore * f0Hz / TWO_PI_F) * std::sin(TWO_PI_F * nomHz / f0Hz);
                    shiftedF[f] = std::clamp(nomHz - pull, 110.0f, 0.44f * sampleRate);
                }

                float openWidening = 1.0f + 1.50f * (1.0f - voicingCore);
                float effQScale = emoQScale * (1.0f - 0.18f * std::max(0.0f, smoothGender)) *
                                  (1.0f - 0.34f * smoothBreathiness) * (1.0f + 0.22f * smoothGrit) * (1.0f + 0.65f * smoothRobotic);
                float minQ = (effQScale < 0.99f) ? 2.0f : 3.2f;
                float maxQ = (effQScale > 1.01f) ? (smoothRobotic > 0.20f ? 16.0f : 13.0f) : 11.5f;
                float minCasQ = (effQScale < 0.99f) ? 1.8f : 2.8f;
                float maxCasQ = (effQScale > 1.01f) ? (smoothRobotic > 0.20f ? 13.0f : 11.0f) : 9.5f;
                for (int f = 0; f < NUM_FORMANTS; ++f) {
                    float pitchFloor = (f == 0) ? (0.38f * f0Hz) : (0.22f * f0Hz);
                    float dynBw = std::max(kBaseBwHz[f] * openWidening, pitchFloor);
                    float rawQ = (shiftedF[f] / std::max(35.0f, dynBw)) * (tgtQ[f] / 5.5f);
                    float proxDamp = 0.0f;
                    if (f + 1 < NUM_FORMANTS) {
                        float df = shiftedF[f + 1] - shiftedF[f];
                        proxDamp += 0.35f * std::exp(-(df * df) / (2.0f * 260.0f * 260.0f));
                    }
                    if (f > 0) {
                        float dfPrev = shiftedF[f] - shiftedF[f - 1];
                        proxDamp += 0.25f * std::exp(-(dfPrev * dfPrev) / (2.0f * 260.0f * 260.0f));
                    }
                    float targetEffQ = std::clamp((rawQ / (1.0f + proxDamp)) * effQScale, minQ, maxQ);
                    smoothQ[f] += subCoeff * (targetEffQ - smoothQ[f]);
                    float effQ = std::clamp(smoothQ[f], minQ, maxQ);
                    singFormantSvf[f].setParams(shiftedF[f], effQ, sampleRate);
                    cascadeFormantSvf[f].setParams(shiftedF[f], std::clamp(effQ * 0.82f, minCasQ, maxCasQ), sampleRate);
                    // Applied AFTER closed-loop servo with gentle exponent so harmonic collisions don't spike volume
                    formantPeakBoost[f] = std::pow(effQ / 4.5f, 0.24f);
                    // Wide Bark Lorentzian bandwidth so formants never starve between Bark band centers
                    barkBw[f] = std::max(95.0f, shiftedF[f] / 6.2f);
                }
                pharynxSvf.setParams(std::clamp(shiftedF[0] * 0.92f, 220.0f, 780.0f), 5.0f, sampleRate);
                float genderSfShift = (smoothGender >= 0.0f) ? (1.0f + 0.22f * smoothGender) : (1.0f + 0.18f * smoothGender);
                singerFormantSvf.setParams(std::clamp(3050.0f * (0.85f + 0.15f * effThroatScale) * genderSfShift, 1800.0f, 4800.0f), 7.5f, sampleRate);
                float gateCurved = smoothOralGate * std::sqrt(std::clamp(smoothOralGate, 0.0f, 1.0f));
                float subTotalTilt = std::clamp(emoTilt + smoothBrightness * 0.50f, -0.75f, 0.75f);
                float emoCeilingScale = std::clamp(1.0f + subTotalTilt * 0.75f, 0.55f, 1.32f) *
                                        (1.0f - 0.22f * smoothBreathiness) *
                                        (smoothGender < 0.0f ? (1.0f + 0.16f * smoothGender) : 1.0f);
                float mouthCutoffHz = (270.0f + (4850.0f - 270.0f) * gateCurved) * emoCeilingScale;
                float mouthQ = 0.72f + 1.60f * smoothOralGate * (1.0f - smoothOralGate);
                vocalCeilingSvf.setParams(std::clamp(mouthCutoffHz * (0.90f + 0.10f * effThroatScale), 240.0f, 0.45f * sampleRate), mouthQ, sampleRate);

                // Place-specific nasal & lateral oral side-branch anti-resonance zero (/m/ vs /n/ vs /ng/ vs /l/)
                float zeroFreqHz = 1720.0f;
                lateralZeroActive = false;
                if (hasActivePhoneme && currentPhoneme.kind == PH_CONSONANT) {
                    const ConsonantSpec& zc = currentPhoneme.cons;
                    if (zc.type == C_NASAL) {
                        if (zc.locusF2 < 1100.0f) zeroFreqHz = 1080.0f;      // bilabial /m/
                        else if (zc.locusF2 < 1350.0f) zeroFreqHz = 2250.0f; // velar /ng/
                        else zeroFreqHz = 1720.0f;                           // alveolar /n/
                    } else if (zc.label[0] == 'l') {
                        zeroFreqHz = 2050.0f;
                        lateralZeroActive = true;
                    }
                } else if (hasActiveOnsetCons && vowelSamples < onsetCarrySamples && activeOnsetCons.type == C_NASAL) {
                    if (activeOnsetCons.locusF2 < 1100.0f) zeroFreqHz = 1080.0f;
                    else if (activeOnsetCons.locusF2 < 1350.0f) zeroFreqHz = 2250.0f;
                    else zeroFreqHz = 1720.0f;
                } else if (activeSyllable.nasalized && !activeSyllable.codaCluster.empty()) {
                    const ConsonantSpec& zc = activeSyllable.codaCluster.front();
                    if (zc.locusF2 < 1100.0f) zeroFreqHz = 1080.0f;
                    else if (zc.locusF2 < 1350.0f) zeroFreqHz = 2250.0f;
                    else zeroFreqHz = 1720.0f;
                } else if (activeSyllable.lColored) {
                    zeroFreqHz = 2050.0f;
                    lateralZeroActive = true;
                } else if (artVelum.pos > 0.04f && hasUpcomingSyllable &&
                           !upcomingSyllable.onsetCluster.empty() && upcomingSyllable.onsetCluster.front().type == C_NASAL) {
                    const ConsonantSpec& zc = upcomingSyllable.onsetCluster.front();
                    if (zc.locusF2 < 1100.0f) zeroFreqHz = 1080.0f;
                    else if (zc.locusF2 < 1350.0f) zeroFreqHz = 2250.0f;
                    else zeroFreqHz = 1720.0f;
                }
                nasalZeroSvf.setParams(std::clamp(zeroFreqHz * effThroatScale * (1.0f + 0.14f * smoothGender),
                    (smoothGender < 0.0f ? 650.0f : 750.0f), (smoothGender > 0.0f ? 3800.0f : 3400.0f)), 3.6f, sampleRate);

                // Analytic 24-Band Bark Vocal Tract Transfer Mask (balanced across front/back vowels)
                f2FrontCached = std::clamp((smoothF[1] - 1150.0f) / 1150.0f, 0.0f, 1.0f);
                float f2Vel = std::clamp(std::fabs(artTongueBodyX.vel) / 14.0f, 0.0f, 1.0f);
                transPresence = 1.0f + 0.18f * f2Vel;
                transF3Boost = 1.0f + 0.14f * f2Vel;
                float aggrBoost = (voiceMode == VOICE_AGGR) ? 1.25f : 1.0f;
                float a0 = 0.88f - 0.22f * f2FrontCached;
                float a1 = (0.98f + 0.65f * f2FrontCached) * aggrBoost * transPresence;
                float a2 = (0.14f + 0.58f * f2FrontCached) * aggrBoost * transF3Boost;
                float a3 = 0.04f + 0.22f * f2FrontCached;

                for (int b = 0; b < NUM_VOCODER_BANDS; ++b) {
                    float fb = vocoderBandFreqs[b];
                    auto lorentz = [&](int fi) {
                        float d = fb - shiftedF[fi];
                        float bw2 = barkBw[fi] * barkBw[fi];
                        return bw2 / (d * d + bw2);
                    };
                    float r0 = lorentz(0);
                    float r1 = lorentz(1);
                    float r2 = lorentz(2);
                    float r3 = lorentz(3);

                    float tractSum = a0 * r0 + a1 * r1 + a2 * r2 + a3 * r3;
                    float maxPeak = std::max({ r0, r1, r2 * (0.35f + 0.65f * f2FrontCached) });
                    float valleyFloor = 0.008f + 0.992f * std::pow(std::clamp(maxPeak, 0.0f, 1.0f), 1.35f);
                    float mask = std::clamp(tractSum * valleyFloor, 0.008f, 2.50f);

                    if (smoothNasalMix > 0.01f) {
                        if (fb >= 200.0f && fb <= 360.0f) {
                            mask = lerp(mask, 1.65f, smoothNasalMix);
                        } else if (fb >= 760.0f && fb <= 1350.0f) {
                            mask *= (1.0f - 0.78f * smoothNasalMix);
                        }
                    }
                    targetBandMask[b] = mask * smoothOralGate;
                }

                if (hasActivePhoneme && currentPhoneme.kind == PH_CONSONANT) {
                    const ConsonantSpec& cs = currentPhoneme.cons;
                    float genderNoiseScale = 1.0f + 0.12f * smoothGender;
                    float f1Hz = cs.noiseFreq1Hz * (0.78f + 0.22f * throatScale) * genderNoiseScale;
                    float f2Hz = cs.noiseFreq2Hz * (0.78f + 0.22f * throatScale) * genderNoiseScale;
                    if (cs.label[0] == 'h') {
                        f1Hz = std::clamp(shiftedF[1], 500.0f, 3800.0f);
                        f2Hz = std::clamp(shiftedF[2], 1500.0f, 4500.0f);
                    } else {
                        float lipShift = 1.0f - 0.16f * std::clamp(artLips.pos, 0.0f, 1.0f);
                        float tbShift  = 1.0f + 0.08f * std::clamp(artTongueBodyX.pos, -1.0f, 1.0f);
                        float artScale = std::clamp(lipShift * tbShift, 0.72f, 1.15f);
                        f1Hz *= artScale;
                        f2Hz *= artScale;
                    }
                    consonantNoiseSvf1.setParams(f1Hz, std::clamp(f1Hz / std::max(150.0f, cs.noiseBw1Hz), 0.7f, 6.5f), sampleRate);
                    consonantNoiseSvf2.setParams(f2Hz, std::clamp(f2Hz / std::max(180.0f, cs.noiseBw2Hz), 0.7f, 6.5f), sampleRate);
                    aspirationSvf.setParams(std::clamp(shiftedF[1], 450.0f, 3800.0f), 2.2f, sampleRate);
                }
                consFormantSvf2.setParams(shiftedF[1], 5.2f, sampleRate);
                consFormantSvf3.setParams(shiftedF[2], 5.5f, sampleRate);
            }

            smoothedMidiPitch += pitchCoeff * (static_cast<float>(targetMidiKey) - smoothedMidiPitch);
            scoopOffsetSemis *= scoopDecay;
            double effVibHz = static_cast<double>(effVibRateHz) * (1.0 + 0.06 * static_cast<double>(jitterSmooth) * static_cast<double>(humanAmt));
            vibratoPhase += TWO_PI_F * effVibHz / static_cast<double>(sampleRate);
            if (vibratoPhase >= TWO_PI_F) vibratoPhase -= TWO_PI_F;
            if (noteGate) ++noteHoldSamples;
            if (samplesSinceLastNoteOn < static_cast<uint64_t>(10.0f * sampleRate)) ++samplesSinceLastNoteOn;

            growlPhase += TWO_PI_F * 72.0 / static_cast<double>(sampleRate);
            if (growlPhase >= TWO_PI_F) growlPhase -= TWO_PI_F;
            tremorPhase += TWO_PI_F * (5.8 + 0.8 * static_cast<double>(jitterSmooth)) / static_cast<double>(sampleRate);
            if (tremorPhase >= TWO_PI_F) tremorPhase -= TWO_PI_F;
            robotRingPhase += TWO_PI_F * 115.0 / static_cast<double>(sampleRate);
            if (robotRingPhase >= TWO_PI_F) robotRingPhase -= TWO_PI_F;

            float vibSemis = vibratoDepth * effVibDepth * std::sin(static_cast<float>(vibratoPhase));
            float sampleHoldDroop = (noteGate && noteHoldSamples > static_cast<uint64_t>(0.08f * sampleRate))
                ? emoHoldDroopSemis * std::min(1.0f, static_cast<float>(noteHoldSamples - static_cast<uint64_t>(0.08f * sampleRate)) / (0.28f * sampleRate))
                : 0.0f;
            float sampleTremorPitch = smoothTremor * 0.34f * std::sin(static_cast<float>(tremorPhase)) * (0.65f + 0.35f * jitterSmooth) * humanAmt;
            float effPitch = smoothedMidiPitch + smoothPitchShift + midiBendSemis + vibSemis +
                             (scoopOffsetSemis + smoothProsodyPitch + emoPitchBias * emoBiasStress * prosodyHoldFadeCached + sampleHoldDroop) * humanAmt +
                             sampleTremorPitch + jitterSmooth * 0.12f * effJitterScale;

            // 1. Obtain raw stereo carrier (100% from SF2 or loaded audio)
            float rawL = 0.0f, rawR = 0.0f;
            if (sType == SRC_SF2) {
                rawL = scratchRaw[static_cast<size_t>(s) * 2];
                rawR = scratchRaw[static_cast<size_t>(s) * 2 + 1];
            } else if (sType == SRC_AUDIO) {
                renderAudioCarrierSample(effPitch, stretchMode, rawL, rawR);
            }

            float absMax = std::max(std::fabs(rawL), std::fabs(rawR));
            carrierEnv += ((absMax > carrierEnv) ? envAtt : envRel) * (absMax - carrierEnv);
            if (carrierEnv > carrierAttackPeak) carrierAttackPeak = carrierEnv;

            // Pre-tract sustain leveler: gently supports decaying SF2/Audio tails without harsh pumping
            float targetLeveler = 1.0f;
            if (noteGate || codaCarrierHold) {
                targetLeveler = std::clamp(0.24f / (carrierEnv + 0.035f), 0.75f, 2.80f);
            } else if (phraseBridgeRemaining > 0) {
                float bridgeFrac = static_cast<float>(phraseBridgeRemaining) / std::max(1.0f, (bridgeIsMidWord ? 0.160f : 0.090f) * sampleRate);
                float holdLvl = std::clamp(0.24f / (carrierEnv + 0.035f), 0.75f, 2.80f);
                targetLeveler = lerp(bridgeIsMidWord ? 0.85f : 1.0f, holdLvl, bridgeFrac);
            }
            speechLeveler += ((targetLeveler > speechLeveler) ? levelUpCoeff : levelDownCoeff) * (targetLeveler - speechLeveler);

            float lvlL = rawL * speechLeveler;
            float lvlR = rawR * speechLeveler;
            float effEnv = carrierEnv * speechLeveler;

            float radL = lvlL - 0.72f * preEmphPrevL;
            float radR = lvlR - 0.72f * preEmphPrevR;
            preEmphPrevL = lvlL;
            preEmphPrevR = lvlR;

            // 2. 100% Instrument-Derived Vocal Mode Excitation + Sub-Harmonic Gap Filler
            float excL = radL;
            float excR = radR;
            float normL = 0.0f;
            float normR = 0.0f;

            if (carrierEnv > 1e-5f) {
                float invEnv = 1.0f / std::max(0.05f, carrierEnv);
                normL = std::clamp(rawL * invEnv, -2.5f, 2.5f);
                normR = std::clamp(rawR * invEnv, -2.5f, 2.5f);

                // Positive zero-crossing half-period divider: adds phase-locked (n+0.5)*F0 subharmonics
                ++subOctHoldL;
                ++subOctHoldR;
                int minHalfPeriod = std::max(16, static_cast<int>(0.0008f * sampleRate));
                if (normPrevL <= 0.0f && normL > 0.04f && subOctHoldL >= minHalfPeriod) {
                    subOctSignL = -subOctSignL;
                    subOctHoldL = 0;
                }
                if (normPrevR <= 0.0f && normR > 0.04f && subOctHoldR >= minHalfPeriod) {
                    subOctSignR = -subOctSignR;
                    subOctHoldR = 0;
                }
                normPrevL = normL;
                normPrevR = normR;
                float genderSubScale = (smoothGender >= 0.0f)
                    ? (1.0f - 0.92f * std::min(1.0f, smoothGender * 1.4f))
                    : (1.0f + (-smoothGender) * 0.65f);
                float subL = fastTanh(normL * subOctSignL * 3.2f) * effEnv * 0.34f * genderSubScale;
                float subR = fastTanh(normR * subOctSignR * 3.2f) * effEnv * 0.34f * genderSubScale;

                if (voiceMode == VOICE_NORMAL) {
                    float edgeL = (fastTanh(normL * 8.0f + 0.35f) + 0.30f * fastTanh(normL * 2.6f)) * effEnv;
                    float edgeR = (fastTanh(normR * 8.0f + 0.35f) + 0.30f * fastTanh(normR * 2.6f)) * effEnv;
                    float diffL = edgeL - 0.86f * hpExcL;
                    float diffR = edgeR - 0.86f * hpExcR;
                    hpExcL = edgeL;
                    hpExcR = edgeR;
                    excL = radL + 1.35f * diffL + subL;
                    excR = radR + 1.35f * diffR + subR;
                } else if (voiceMode == VOICE_AGGR) {
                    float growlMod = 1.0f + 0.28f * std::sin(static_cast<float>(growlPhase));
                    float edgeL = (fastTanh(normL * 10.5f + 0.42f) + 0.38f * fastTanh(normL * 3.4f)) * effEnv * growlMod;
                    float edgeR = (fastTanh(normR * 10.5f + 0.42f) + 0.38f * fastTanh(normR * 3.4f)) * effEnv * growlMod;
                    float diffL = edgeL - 0.82f * hpExcL;
                    float diffR = edgeR - 0.82f * hpExcR;
                    hpExcL = edgeL;
                    hpExcR = edgeR;
                    float presL = 0.0f, presR = 0.0f;
                    aggrPresenceSvf.processBandpass(diffL, diffR, presL, presR);
                    excL = (radL * growlMod) + 1.55f * diffL + presL * aggrPresenceSvf.k * 0.70f + subL * growlMod;
                    excR = (radR * growlMod) + 1.55f * diffR + presR * aggrPresenceSvf.k * 0.70f + subR * growlMod;
                } else if (voiceMode == VOICE_WHISPER) {
                    float nL = nextNoiseSample();
                    float nR = nextNoiseSample();
                    float breathL = effEnv * (0.65f + 0.35f * std::fabs(normL));
                    float breathR = effEnv * (0.65f + 0.35f * std::fabs(normR));
                    hpExcL += hpExcAlpha * (radL - hpExcL);
                    hpExcR += hpExcAlpha * (radR - hpExcR);
                    excL = 0.82f * nL * breathL + 0.18f * (radL - hpExcL);
                    excR = 0.82f * nR * breathR + 0.18f * (radR - hpExcR);
                }

                // Voice characteristics & emotion phonation physics: warm whisper blend, pressed fold edge, 72 Hz growl & robotic harmonic lock
                float effWhisperMix = std::clamp((emoWhisperMix + smoothBreathiness * 0.72f + smoothTremor * 0.14f + std::max(0.0f, smoothGender) * 0.06f) * humanAmt, 0.0f, 0.85f);
                float effRoughness = std::clamp((emoRoughness + smoothGrit * 0.85f + smoothTremor * 0.12f) * (1.0f - 0.5f * smoothRobotic), 0.0f, 1.05f);
                float effEdgeDrive = std::clamp(emoEdgeDrive + smoothGrit * 0.75f + smoothRobotic * 0.45f, 0.0f, 1.10f);
                if (effWhisperMix > 0.01f && voiceMode != VOICE_WHISPER) {
                    float bnAirL = nextNoiseSample() * effEnv * (0.45f + 0.55f * std::fabs(normL));
                    float bnAirR = nextNoiseSample() * effEnv * (0.45f + 0.55f * std::fabs(normR));
                    softBreathLpL += softBreathAlpha * (bnAirL - softBreathLpL);
                    softBreathLpR += softBreathAlpha * (bnAirR - softBreathLpR);
                    float baseL = (voiceMode == VOICE_NORMAL || voiceMode == VOICE_AGGR) ? (excL - subL * 0.65f) : excL;
                    float baseR = (voiceMode == VOICE_NORMAL || voiceMode == VOICE_AGGR) ? (excR - subR * 0.65f) : excR;
                    excL = lerp(baseL, softBreathLpL * 1.45f, effWhisperMix);
                    excR = lerp(baseR, softBreathLpR * 1.45f, effWhisperMix);
                }
                if ((effEdgeDrive > 0.02f || effRoughness > 0.02f) && voiceMode != VOICE_RAW && voiceMode != VOICE_WHISPER) {
                    float growlLfo = 1.0f + effRoughness * 0.34f * std::sin(static_cast<float>(growlPhase));
                    float pressGain = 6.5f + 4.0f * effEdgeDrive;
                    float foldBias = 0.35f * effEdgeDrive;
                    float pressedL = (fastTanh(normL * pressGain + foldBias) - fastTanh(foldBias)) * effEnv;
                    float pressedR = (fastTanh(normR * pressGain + foldBias) - fastTanh(foldBias)) * effEnv;
                    excL = excL * growlLfo + effEdgeDrive * 0.52f * pressedL + effRoughness * 0.22f * subL * growlLfo;
                    excR = excR * growlLfo + effEdgeDrive * 0.52f * pressedR + effRoughness * 0.22f * subR * growlLfo;
                }
                if (smoothRobotic > 0.01f && voiceMode != VOICE_RAW && voiceMode != VOICE_WHISPER) {
                    excL += smoothRobotic * 0.40f * fastTanh(normL * 14.0f) * effEnv;
                    excR += smoothRobotic * 0.40f * fastTanh(normR * 14.0f) * effEnv;
                }
            }

            // 3. 24-Band Bark Analysis & Spectral Whitening on Pure Instrument Bands
            float bandOutL[NUM_VOCODER_BANDS];
            float bandOutR[NUM_VOCODER_BANDS];
            float totalBandEnv = 0.0f;
            for (int b = 0; b < NUM_VOCODER_BANDS; ++b) {
                float cL = 0.0f, cR = 0.0f;
                vocoderCarSvf[b].processBandpass(excL, excR, cL, cR);
                cL *= vocoderCarSvf[b].k;
                cR *= vocoderCarSvf[b].k;
                bandOutL[b] = cL;
                bandOutR[b] = cR;

                float mag = 0.5f * (std::fabs(cL) + std::fabs(cR));
                carrierBandEnv[b] += ((mag > carrierBandEnv[b]) ? bandAtt : bandRel) * (mag - carrierBandEnv[b]);
                totalBandEnv += carrierBandEnv[b];
            }

            float meanBandEnv = totalBandEnv * (1.0f / static_cast<float>(NUM_VOCODER_BANDS));
            float envLow = 0.0f, envMid = 0.0f, envHigh = 0.0f;
            for (int b = 0; b < 7; ++b) envLow += carrierBandEnv[b];
            for (int b = 7; b < 15; ++b) envMid += carrierBandEnv[b];
            for (int b = 15; b < NUM_VOCODER_BANDS; ++b) envHigh += carrierBandEnv[b];
            envLow *= (1.0f / 7.0f);
            envMid *= (1.0f / 8.0f);
            envHigh *= (1.0f / 9.0f);

            float tiltBpL = 0.0f, tiltBpR = 0.0f, tiltLpL = 0.0f, tiltLpR = 0.0f;
            singTiltSvf.processBandpassAndLowpass(excL, excR, tiltBpL, tiltBpR, tiltLpL, tiltLpR);
            float tiltHpL = excL - tiltLpL - tiltBpL * singTiltSvf.k;
            float tiltHpR = excR - tiltLpR - tiltBpR * singTiltSvf.k;
            float gLow  = lerp(1.0f, std::clamp(meanBandEnv / (envLow  + 2e-4f), 0.45f, 14.0f), whiteMixSing);
            float gMid  = lerp(1.0f, std::clamp(meanBandEnv / (envMid  + 2e-4f), 0.45f, 14.0f), whiteMixSing);
            float gHigh = lerp(1.0f, std::clamp(meanBandEnv / (envHigh + 2e-4f), 0.45f, 14.0f), whiteMixSing);
            float cohSingL = tiltLpL * gLow + tiltBpL * (singTiltSvf.k * gMid) + tiltHpL * gHigh;
            float cohSingR = tiltLpR * gLow + tiltBpR * (singTiltSvf.k * gMid) + tiltHpR * gHigh;

            float whiteSingL = 0.0f, whiteSingR = 0.0f;
            float talkL = 0.0f, talkR = 0.0f;

            for (int b = 0; b < NUM_VOCODER_BANDS; ++b) {
                float rawAgc = std::clamp(meanBandEnv / (carrierBandEnv[b] + 2e-4f), 0.35f, 24.0f);
                float mask = targetBandMask[b];

                // SING path: hybrid 24-band contour + phase-coherent 3-zone broadband excitation
                float agcSing = lerp(1.0f, rawAgc, whiteMixSing);
                float softContour = lerp(0.12f, mask, 0.82f);
                whiteSingL += bandOutL[b] * agcSing * softContour;
                whiteSingR += bandOutR[b] * agcSing * softContour;

                // TALKBOX path: full whitening directly sculpted by high-contrast analytic vocal mask
                if (smoothChar > 0.01f) {
                    float agcTalk = lerp(1.0f, rawAgc, whiteMixTalk);
                    float talkGain = std::pow(mask, 1.35f) * 1.45f;
                    talkL += bandOutL[b] * agcTalk * talkGain;
                    talkR += bandOutR[b] * agcTalk * talkGain;
                }
            }
            whiteSingL = 0.58f * cohSingL * 0.65f + 0.42f * (whiteSingL * 0.72f);
            whiteSingR = 0.58f * cohSingR * 0.65f + 0.42f * (whiteSingR * 0.72f);
            talkL *= 0.56f;
            talkR *= 0.56f;

            // Pitch-synchronous vocal-tract breath sheen & organic shimmer (0 when carrierEnv == 0)
            if (carrierEnv > 1e-4f && voiceMode != VOICE_RAW && voiceMode != VOICE_WHISPER) {
                float shimScale = 1.0f + std::clamp(shimmerSmooth * 0.14f, -0.025f, 0.025f) * humanAmt;
                float breathAmt = std::clamp((emoBreath + smoothBreathiness * 0.065f + std::max(0.0f, smoothGender) * 0.018f +
                    (curPitchCached > 72.0f ? 0.015f : 0.0f) +
                    (activeSyllable.isFunctionWord ? 0.014f : 0.0f)) * humanAmt, 0.0f, 0.18f);
                float bnL = nextNoiseSample() * effEnv * (0.35f + 0.65f * std::fabs(normL)) * breathAmt * 0.95f;
                float bnR = nextNoiseSample() * effEnv * (0.35f + 0.65f * std::fabs(normR)) * breathAmt * 0.95f;
                whiteSingL = whiteSingL * shimScale + bnL;
                whiteSingR = whiteSingR * shimScale + bnR;
            }

            // 4. Sample-Accurate Phoneme Queue State Machine (Living Throat + Plosives + Tap r)
            float targetOralGate = 1.0f;
            float targetNasalMix = std::max(vowelNasalColor, std::clamp(artVelum.pos, 0.0f, 1.0f));
            float targetVoiceBar = 0.0f;
            float targetF1Cutback = 1.0f;
            float consNoiseGain = 0.0f;
            float consAspGain = 0.0f;
            float consVoiceMod = 1.0f;

            if (hasActivePhoneme) {
                if (currentPhoneme.kind == PH_CONSONANT) {
                    const ConsonantSpec& cs = currentPhoneme.cons;
                    uint64_t burstEnd = closureSamples + burstSamples;
                    uint64_t totalPhoneme = burstEnd + transSamples;
                    float unvoicedGain = (pendingCarrierAttack || currentPhoneme.isCoda) ? 0.92f : (0.55f + 0.45f * std::min(1.0f, carrierEnv * 4.5f));
                    float localDict = std::clamp(smoothDiction * emoDictionScale * (1.0f - 0.25f * smoothBreathiness) * (0.78f + 0.22f * std::clamp(smoothProminence, 0.72f, 1.22f)), 0.0f, 1.0f);
                    bool isBallisticTap = (cs.type == C_LIQUID_GLIDE && cs.label[0] == 'r' && cs.locusF3 > 1950.0f) ||
                                          (cs.type == C_PLOSIVE_VOICED && cs.label[0] == 'd' && cs.closureMs <= 9.0f);
                    bool isMidWordOnset = !activeSyllable.isWordStart && !currentPhoneme.isCoda;

                    if (phaseSample < closureSamples && closureSamples > 0) {
                        float maxClose = isMidWordOnset ? 0.84f : 0.98f;
                        float closeDepth = std::clamp(localDict * (currentPhoneme.unreleased ? 1.15f : 1.42f), 0.0f, maxClose);
                        if (isBallisticTap) {
                            targetOralGate = 1.0f - std::clamp(0.88f * localDict, 0.0f, 0.90f);
                            targetVoiceBar = 0.24f * localDict;
                        } else if (cs.type == C_PLOSIVE_UNVOICED || cs.type == C_AFFRICATE_UNVOICED) {
                            targetOralGate = 1.0f - closeDepth;
                            targetF1Cutback = currentPhoneme.unreleased ? 0.60f : (isMidWordOnset ? 0.34f : 0.18f);
                        } else if (cs.type == C_PLOSIVE_VOICED || cs.type == C_AFFRICATE_VOICED) {
                            targetOralGate = 1.0f - (isMidWordOnset ? 0.78f : 0.94f) * closeDepth;
                            targetVoiceBar = cs.murmurGain * localDict;
                        }
                    } else if (phaseSample < burstEnd && burstSamples > 0) {
                        float u = static_cast<float>(phaseSample - closureSamples) / static_cast<float>(burstSamples);
                        float burstSec = static_cast<float>(phaseSample - closureSamples) / std::max(8000.0f, sampleRate);
                        float pop = (burstSec < 0.0032f) ? std::exp(-burstSec / 0.00085f) : 0.0f;

                        if (isBallisticTap) {
                            // Crisp ~15 ms ballistic tongue-tip tap (Turkish tapped 'r' / English flapped 't')
                            float tapShape = std::pow(std::sin(u * PI_F), 1.15f);
                            targetOralGate = 1.0f - std::clamp(0.92f * localDict * tapShape, 0.0f, 0.92f);
                            targetVoiceBar = 0.24f * localDict * tapShape;
                            float tapPuff = std::exp(-std::pow((u - 0.65f) / 0.16f, 2.0f));
                            consNoiseGain = (0.20f + cs.noiseGain * 0.45f) * localDict * tapPuff;
                        } else if (cs.type == C_PLOSIVE_UNVOICED) {
                            float env = std::exp(-4.5f * u);
                            consNoiseGain = cs.noiseGain * localDict * (env + 0.85f * pop);
                            consAspGain = cs.aspirationGain * localDict * (1.0f - 0.6f * u);
                            targetOralGate = lerp(1.0f - 0.55f * localDict, 1.0f, u);
                            targetF1Cutback = 0.18f;
                            consVoiceMod = unvoicedGain;
                        } else if (cs.type == C_PLOSIVE_VOICED) {
                            float env = std::exp(-5.5f * u);
                            consNoiseGain = cs.noiseGain * localDict * (env + 0.45f * pop);
                            consVoiceMod = std::min(1.0f, carrierEnv * 5.0f);
                        } else if (cs.type == C_AFFRICATE_UNVOICED || cs.type == C_AFFRICATE_VOICED) {
                            float shape = std::pow(std::sin(u * PI_F), 0.50f);
                            targetOralGate = (cs.type == C_AFFRICATE_UNVOICED) ? (1.0f - 0.60f * localDict * shape) : (1.0f - 0.30f * localDict * shape);
                            if (cs.type == C_AFFRICATE_UNVOICED) targetF1Cutback = 0.25f;
                            consNoiseGain = cs.noiseGain * localDict * (shape + 0.35f * pop);
                            consVoiceMod = (cs.type == C_AFFRICATE_UNVOICED) ? unvoicedGain : std::min(1.0f, carrierEnv * 5.0f);
                        } else if (cs.type == C_FRICATIVE_UNVOICED) {
                            float shape = std::pow(std::sin(u * PI_F), 0.60f);
                            if (cs.label[0] == 'h') {
                                targetOralGate = 1.0f - (isMidWordOnset ? 0.18f : 0.35f) * localDict * shape;
                                consAspGain = cs.aspirationGain * localDict * shape;
                                consNoiseGain = cs.noiseGain * localDict * shape;
                            } else {
                                float sibilantClose = currentPhoneme.isCoda ? 0.88f : 0.78f;
                                targetOralGate = 1.0f - std::clamp(localDict * sibilantClose * 1.25f, 0.0f, 0.92f) * shape;
                                consNoiseGain = cs.noiseGain * localDict * shape * (currentPhoneme.isCoda ? 1.28f : 1.05f);
                                consAspGain = cs.aspirationGain * localDict * shape;
                            }
                            consVoiceMod = unvoicedGain;
                        } else if (cs.type == C_FRICATIVE_VOICED) {
                            float shape = std::pow(std::sin(u * PI_F), 0.65f);
                            targetOralGate = 1.0f - (isMidWordOnset ? 0.14f : 0.22f) * localDict * shape;
                            consNoiseGain = cs.noiseGain * localDict * shape;
                            consVoiceMod = std::min(1.0f, carrierEnv * 5.0f);
                        } else if (cs.type == C_NASAL) {
                            float shape = std::sin(u * PI_F * 0.5f + PI_F * 0.25f);
                            targetNasalMix = std::max(targetNasalMix, cs.murmurGain * localDict * shape);
                            targetOralGate = 1.0f - (isMidWordOnset ? 0.62f : 0.85f) * localDict;
                        } else if (cs.type == C_LIQUID_GLIDE) {
                            float shape = std::sin(u * PI_F);
                            float dip = (cs.label[0] == 'w' || cs.label[0] == 'y') ? (isMidWordOnset ? 0.12f : 0.36f)
                                      : (cs.label[0] == 'l') ? (isMidWordOnset ? 0.22f : 0.48f)
                                                             : (isMidWordOnset ? 0.18f : 0.42f);
                            targetOralGate = 1.0f - dip * localDict * shape;
                            targetVoiceBar = cs.murmurGain * localDict * shape;
                        }
                    } else {
                        float u = (transSamples > 0) ? std::clamp(static_cast<float>(phaseSample - burstEnd) / static_cast<float>(transSamples), 0.0f, 1.0f) : 1.0f;
                        if (cs.type == C_PLOSIVE_UNVOICED || cs.type == C_AFFRICATE_UNVOICED) {
                            consAspGain = cs.aspirationGain * 0.45f * localDict * (1.0f - u);
                            consVoiceMod = unvoicedGain;
                            // F1 stays suppressed longer than F2/F3 during unvoiced VOT release (true F1 cutback)
                            targetF1Cutback = lerp(0.18f, 1.0f, std::clamp((u - 0.15f) / 0.65f, 0.0f, 1.0f));
                        } else if (cs.type == C_FRICATIVE_UNVOICED && cs.label[0] != 'h') {
                            consNoiseGain = cs.noiseGain * 0.45f * localDict * (1.0f - u);
                            targetOralGate = lerp(1.0f - 0.55f * localDict, 1.0f, u);
                            consVoiceMod = unvoicedGain;
                        } else if (cs.type == C_NASAL) {
                            targetNasalMix = std::max(targetNasalMix, cs.murmurGain * localDict * (1.0f - u));
                            targetOralGate = lerp(1.0f - (isMidWordOnset ? 0.55f : 0.80f) * localDict, 1.0f, u);
                        }
                    }

                    ++phaseSample;
                    if (phaseSample >= totalPhoneme) {
                        bool more = advancePhonemeQueue();
                        if (!more) {
                            syllablePhase = PHASE_IDLE;
                            if (codaCarrierHold) {
                                codaCarrierHold = false;
                                if (legatoMode != LEGATO_STAC) {
                                    bridgeIsMidWord = !activeSyllable.isWordEnd;
                                    phraseBridgeRemaining = static_cast<uint64_t>((bridgeIsMidWord ? 0.160f : 0.090f) * sampleRate);
                                } else if (sType == SRC_SF2 && sfHandle) {
                                    tsf_channel_note_off_all(sfHandle, 0);
                                    sf2VoiceRunning = false;
                                }
                            }
                        }
                    }
                } else {
                    ++vowelSamples;
                    if (activeSyllable.isDiphthong) {
                        if (diphthongFinishRemaining > 0) {
                            diphthongMorph += (1.0f - diphthongMorph) / static_cast<float>(diphthongFinishRemaining);
                            --diphthongFinishRemaining;
                            if (diphthongFinishRemaining == 0) {
                                diphthongMorph = 1.0f;
                                beginCodaReleaseAfterDiphthong();
                            }
                        } else {
                            // Singing diphthong sustain: hold primary nucleus vowel1, glide up to 68% max while held
                            uint64_t waitSamp = static_cast<uint64_t>(0.055f * sampleRate);
                            uint64_t sweepSamp = static_cast<uint64_t>(0.135f * sampleRate);
                            if (vowelSamples > waitSamp) {
                                float dU = static_cast<float>(vowelSamples - waitSamp) / static_cast<float>(std::max<uint64_t>(1, sweepSamp));
                                diphthongMorph = std::min(0.68f, dU * 0.68f);
                            }
                        }
                    } else if (diphthongFinishRemaining > 0) {
                        diphthongFinishRemaining = 0;
                        beginCodaReleaseAfterDiphthong();
                    } else {
                        // Monophthong coarticulation sweep across the syllable (continues gently so long vowels stay alive)
                        uint64_t ctxSweep = static_cast<uint64_t>(0.095f * sampleRate);
                        float dU = static_cast<float>(vowelSamples) / static_cast<float>(std::max<uint64_t>(1, ctxSweep));
                        if (dU <= 1.0f) {
                            diphthongMorph = dU * 0.65f;
                        } else {
                            float lateU = std::min(1.0f, static_cast<float>(vowelSamples - ctxSweep) / (0.28f * sampleRate));
                            diphthongMorph = 0.65f + 0.15f * lateU;
                        }
                    }
                }
            }
            if (!noteGate && !codaCarrierHold && phraseBridgeRemaining > 0) {
                targetOralGate = bridgeIsMidWord ? 0.98f : 0.78f;
            }

            if (phraseBridgeRemaining > 0) {
                --phraseBridgeRemaining;
                if (phraseBridgeRemaining == 0) {
                    bridgeIsMidWord = false;
                    if (!noteGate && !codaCarrierHold && sType == SRC_SF2 && sfHandle && sf2VoiceRunning) {
                        tsf_channel_note_off_all(sfHandle, 0);
                        sf2VoiceRunning = false;
                    }
                }
            }

            smoothOralGate += gateSmoothCoeff * (targetOralGate - smoothOralGate);
            smoothNasalMix += gateSmoothCoeff * (targetNasalMix - smoothNasalMix);
            smoothVoiceBar += gateSmoothCoeff * (targetVoiceBar - smoothVoiceBar);
            f1CutbackGain  += f1CutCoeff * (targetF1Cutback - f1CutbackGain);

            // 5. Dynamic Closed-Loop 4-Pole Cascaded Formant Tract + Throat/Singer Formants
            float aggrScale = (voiceMode == VOICE_AGGR) ? 1.25f : 1.0f;
            float totalTilt = std::clamp(emoTilt + smoothBrightness * 0.50f, -0.75f, 0.75f);
            float emoHighTint = std::clamp(1.0f + totalTilt * 0.72f, 0.58f, 1.50f);
            float emoLowTint  = std::clamp(1.0f - totalTilt * 0.35f, 0.75f, 1.28f);
            float targetAmp[NUM_FORMANTS] = {
                (1.08f - 0.42f * f2FrontCached) * emoLowTint,
                (0.92f + 0.68f * f2FrontCached) * aggrScale * transPresence * emoHighTint,
                (0.18f + 0.48f * f2FrontCached) * aggrScale * transF3Boost * emoHighTint,
                (0.05f + 0.18f * f2FrontCached) * emoHighTint
            };
            static constexpr float kPolarity[NUM_FORMANTS] = { 1.0f, -1.0f, 1.0f, -1.0f };

            float unit4L[NUM_FORMANTS]{};
            float unit4R[NUM_FORMANTS]{};
            for (int f = 0; f < NUM_FORMANTS; ++f) {
                float bp1L = 0.0f, bp1R = 0.0f;
                singFormantSvf[f].processBandpass(whiteSingL, whiteSingR, bp1L, bp1R);
                float st1L = bp1L * singFormantSvf[f].k;
                float st1R = bp1R * singFormantSvf[f].k;

                // Second cascaded formant resonator stage (-12 dB/oct skirts carves out non-vocal valleys)
                float bp2L = 0.0f, bp2R = 0.0f;
                cascadeFormantSvf[f].processBandpass(st1L, st1R, bp2L, bp2R);
                unit4L[f] = bp2L * cascadeFormantSvf[f].k;
                unit4R[f] = bp2R * cascadeFormantSvf[f].k;

                // Measure envelope BEFORE formantPeakBoost so servo never divides out Q boost
                float mag = 0.5f * (std::fabs(unit4L[f]) + std::fabs(unit4R[f]));
                formantEnv[f] += ((mag > formantEnv[f]) ? formEnvAtt : formEnvRel) * (mag - formantEnv[f]);
            }

            float refFormEnv = std::max(0.015f, effEnv * 0.22f);
            bool isSonorantCons = hasActivePhoneme && currentPhoneme.kind == PH_CONSONANT &&
                                  !currentPhoneme.isCoda &&
                                  ((currentPhoneme.cons.type == C_LIQUID_GLIDE &&
                                    !(currentPhoneme.cons.label[0] == 'r' && currentPhoneme.cons.locusF3 > 1950.0f)) ||
                                   currentPhoneme.cons.type == C_FRICATIVE_VOICED);
            bool inVoicedTract = (!hasActivePhoneme || currentPhoneme.kind == PH_VOWEL_CORE || isSonorantCons);
            bool servoActive = (carrierEnv > 0.012f) && inVoicedTract;
            float parL = 0.0f, parR = 0.0f;
            for (int f = 0; f < NUM_FORMANTS; ++f) {
                if (servoActive && refFormEnv > 1e-4f) {
                    float ratio = (targetAmp[f] * refFormEnv) / (formantEnv[f] + 1.2e-4f);
                    float desiredGain = std::clamp(lerp(1.0f, ratio, 0.78f), 0.30f, 2.60f);
                    float sCoeff = (desiredGain < dynFormantGain[f]) ? servoDownCoeff : servoUpCoeff;
                    dynFormantGain[f] += sCoeff * (desiredGain - dynFormantGain[f]);
                } else if (carrierEnv <= 0.012f) {
                    dynFormantGain[f] += servoUpCoeff * (1.0f - dynFormantGain[f]);
                }

                float g = kPolarity[f] * targetAmp[f] * dynFormantGain[f] * formantPeakBoost[f] * 0.92f;
                if (f == 0) g *= f1CutbackGain;
                parL += unit4L[f] * g;
                parR += unit4R[f] * g;
            }

            // Fixed Human Vocal Tract Cavities: Pharynx (~540 Hz) + Piriform / Singer's Formant (~3.05..3.72 kHz)
            float phL = 0.0f, phR = 0.0f;
            pharynxSvf.processBandpass(whiteSingL, whiteSingR, phL, phR);
            float genderPhScale = (smoothGender >= 0.0f) ? (1.0f - 0.55f * smoothGender) : (1.0f + 0.65f * (-smoothGender));
            float phGain = 0.24f * genderPhScale * pharynxSvf.k * std::pow(pharynxSvf.k, -0.35f); // chest resonance peak

            float sfBpL = 0.0f, sfBpR = 0.0f;
            singerFormantSvf.processBandpass(whiteSingL, whiteSingR, sfBpL, sfBpR);
            float emoSfScale = std::clamp(1.0f + totalTilt * 1.40f, 0.28f, 1.85f);
            float sfGain = (0.14f + 0.10f * f2FrontCached) * aggrScale * emoSfScale * std::pow(singerFormantSvf.k, 0.45f);

            float throatL = (phL * phGain + sfBpL * sfGain) * smoothOralGate;
            float throatR = (phR * phGain + sfBpR * sfGain) * smoothOralGate;

            float singL = parL * 1.18f * smoothOralGate;
            float singR = parR * 1.18f * smoothOralGate;

            // Place-specific nasal / lateral oral side-branch anti-resonance zero notch
            float zeroDepth = std::clamp(smoothNasalMix * 0.72f + (lateralZeroActive ? 0.28f : 0.0f), 0.0f, 0.75f);
            if (zeroDepth > 0.01f) {
                float nzL = 0.0f, nzR = 0.0f;
                nasalZeroSvf.processBandpass(singL, singR, nzL, nzR);
                singL -= nzL * nasalZeroSvf.k * zeroDepth;
                singR -= nzR * nasalZeroSvf.k * zeroDepth;
            }

            if (smoothNasalMix > 0.002f || smoothVoiceBar > 0.002f) {
                float nmL = 0.0f, nmR = 0.0f;
                nasalMurmurSvf.processBandpass(excL, excR, nmL, nmR);
                float vbL = 0.0f, vbR = 0.0f;
                voiceBarSvf.processBandpass(lvlL, lvlR, vbL, vbR);

                float nGain = smoothNasalMix * nasalMurmurSvf.k * 1.20f;
                float vGain = smoothVoiceBar * voiceBarSvf.k * 1.45f;
                singL += nmL * nGain + vbL * vGain;
                singR += nmR * nGain + vbR * vGain;
            }

            // 6. Crossfade SING <-> TALKBOX, imprint Throat Cavities & Dynamic Mouth Lowpass
            float effChar = lerp(smoothChar, std::max(smoothChar, 0.72f), smoothRobotic);
            float rawMixL = lerp(singL, talkL, effChar) + throatL;
            float rawMixR = lerp(singR, talkR, effChar) + throatR;
            // Dynamic Living-Throat Spectral Tilt (applied post-servo so whitener/formant servo does not cancel emotion/brightness color)
            float bloomTilt = 0.0f;
            if (hasActivePhoneme && currentPhoneme.kind == PH_VOWEL_CORE) {
                const float vU = std::min(1.0f, static_cast<float>(vowelSamples) / std::max(1.0f, 0.025f * sampleRate));
                bloomTilt = lerp(-0.16f, 0.06f, vU);
            }
            if (activeSyllable.breakAfter == SyllableToken::BREAK_MAJOR) bloomTilt -= 0.10f;
            const float glottalBright = std::clamp(totalTilt + 0.18f * (smoothProminence - 1.0f) + 0.14f * (lastVelocity - 0.75f) + bloomTilt * humanAmt, -0.75f, 0.75f);
            glottalTiltStateL += tiltLpAlpha * (rawMixL - glottalTiltStateL);
            glottalTiltStateR += tiltLpAlpha * (rawMixR - glottalTiltStateR);
            if (glottalBright < 0.0f) {
                const float warmMix = std::clamp(-glottalBright * 1.35f, 0.0f, 0.85f);
                rawMixL = lerp(rawMixL, glottalTiltStateL, warmMix);
                rawMixR = lerp(rawMixR, glottalTiltStateR, warmMix);
            } else if (glottalBright > 0.0f) {
                const float biteAmt = glottalBright * 1.10f;
                rawMixL += (rawMixL - glottalTiltStateL) * biteAmt;
                rawMixR += (rawMixR - glottalTiltStateR) * biteAmt;
            }
            float mixL = 0.0f, mixR = 0.0f;
            vocalCeilingSvf.processLowpass(rawMixL, rawMixR, mixL, mixR);

            // Post-tract voiced loudness equalizer: stays continuous through voiced sonorants and holds across closures/mid-word bridges
            float vMag = 0.5f * (std::fabs(mixL) + std::fabs(mixR));
            if (inVoicedTract && smoothOralGate > 0.30f) {
                vowelOutEnv += ((vMag > vowelOutEnv) ? formEnvAtt : formEnvRel) * (vMag - vowelOutEnv);
            }
            bool inMidWordBridge = (!noteGate && !codaCarrierHold && bridgeIsMidWord && phraseBridgeRemaining > 0);
            if ((noteGate || codaCarrierHold || inMidWordBridge) && inVoicedTract && carrierEnv > 0.01f && smoothOralGate > 0.25f) {
                float bridgeAtten = inMidWordBridge ? 0.95f : 1.0f;
                float desiredV = lastVelocity * 0.145f * std::clamp(smoothProminence, 0.82f, 1.20f) * bridgeAtten;
                float rawVL = std::clamp(desiredV / (vowelOutEnv + 0.005f), 0.36f, 2.60f);
                float openW = std::clamp((smoothOralGate - 0.25f) / 0.40f, 0.0f, 1.0f);
                float targetVLevel = lerp(1.0f, rawVL, openW);
                vowelLeveler += ((targetVLevel < vowelLeveler) ? servoDownCoeff : formEnvRel) * (targetVLevel - vowelLeveler);
            } else if (!noteGate && !codaCarrierHold && !bridgeIsMidWord) {
                vowelLeveler += ((1.0f < vowelLeveler) ? servoDownCoeff : formEnvRel) * (1.0f - vowelLeveler);
            }
            mixL *= vowelLeveler;
            mixR *= vowelLeveler;

            // Living syllabic bell envelope AFTER vowelLeveler + Grit growl + Aging tremor + Robotic lock
            float targetSyllEnv = 1.0f;
            if (hasActivePhoneme && currentPhoneme.kind == PH_VOWEL_CORE) {
                float bloomSamples = std::max(1.0f, (emoBloomMs * 0.001f) * sampleRate);
                float settleSamples = std::max(1.0f, 0.050f * sampleRate);
                float bloom = std::clamp(static_cast<float>(vowelSamples) / bloomSamples, 0.0f, 1.0f);
                float startG = activeSyllable.isWordStart ? emoBloomStartGain : std::max(0.94f, emoBloomStartGain);
                float peakG  = activeSyllable.isWordStart ? (emoRoughness > 0.30f ? 1.12f : 1.06f) : 1.04f;
                float holdG  = activeSyllable.isWordStart ? 0.96f : 0.98f;
                float baseEnv = lerp(startG, peakG, bloom);
                if (static_cast<float>(vowelSamples) > bloomSamples) {
                    float sU = std::min(1.0f, (static_cast<float>(vowelSamples) - bloomSamples) / settleSamples);
                    baseEnv = lerp(peakG, holdG, sU);
                }
                float codaDip = activeSyllable.isWordEnd ? (1.0f - 0.14f * preCodaWeight) : 1.0f;
                targetSyllEnv = baseEnv * codaDip * emoDynGain;
            }
            if (!noteGate && !codaCarrierHold && phraseBridgeRemaining > 0) {
                targetSyllEnv = (bridgeIsMidWord ? 1.00f : 0.86f) * emoDynGain;
            }
            if (smoothRobotic > 0.01f) {
                targetSyllEnv = lerp(targetSyllEnv, 1.0f, smoothRobotic);
            }
            smoothSyllEnv += syllEnvCoeff * (targetSyllEnv - smoothSyllEnv);
            mixL *= smoothSyllEnv;
            mixR *= smoothSyllEnv;
            float postEffRough = std::clamp((emoRoughness + smoothGrit * 0.85f + smoothTremor * 0.12f) * (1.0f - 0.5f * smoothRobotic), 0.0f, 1.05f);
            if (postEffRough > 0.01f && voiceMode != VOICE_RAW && voiceMode != VOICE_WHISPER &&
                (!hasActivePhoneme || currentPhoneme.kind == PH_VOWEL_CORE)) {
                float postGrowl = 1.0f + postEffRough * 0.28f * std::sin(static_cast<float>(growlPhase));
                mixL *= postGrowl;
                mixR *= postGrowl;
            }
            if (smoothTremor > 0.01f && (!hasActivePhoneme || currentPhoneme.kind == PH_VOWEL_CORE)) {
                float tremAmp = 1.0f + smoothTremor * 0.22f * std::sin(static_cast<float>(tremorPhase) * 1.07f + 0.9f) * humanAmt;
                mixL *= tremAmp;
                mixR *= tremAmp;
            }
            if (smoothRobotic > 0.01f && (!hasActivePhoneme || currentPhoneme.kind == PH_VOWEL_CORE)) {
                float robMod = lerp(1.0f, 0.76f + 0.24f * std::cos(static_cast<float>(robotRingPhase)), smoothRobotic * 0.70f);
                mixL *= robMod;
                mixR *= robMod;
            }

            if ((consNoiseGain > 0.001f || consAspGain > 0.001f) &&
                (noteGate || codaCarrierHold || carrierEnv > 1e-4f)) {
                float grainModL = (noteGate && carrierEnv > 0.01f) ? (0.68f + 0.32f * fastTanh(normL * 2.5f)) : 1.0f;
                float grainModR = (noteGate && carrierEnv > 0.01f) ? (0.68f + 0.32f * fastTanh(normR * 2.5f)) : 1.0f;
                float nL = nextNoiseSample() * grainModL;
                float nR = nextNoiseSample() * grainModR;
                float f1L = 0.0f, f1R = 0.0f, f2L = 0.0f, f2R = 0.0f, aL = 0.0f, aR = 0.0f;
                consonantNoiseSvf1.processBandpass(nL, nR, f1L, f1R);
                consonantNoiseSvf2.processBandpass(nL, nR, f2L, f2R);
                aspirationSvf.processBandpass(nL, nR, aL, aR);

                float rawFricL = f1L * consonantNoiseSvf1.k * 0.75f + f2L * consonantNoiseSvf2.k * 0.55f;
                float rawFricR = f1R * consonantNoiseSvf1.k * 0.75f + f2R * consonantNoiseSvf2.k * 0.55f;

                // Preserve high-frequency sibilant energy (>2.6 kHz) above F2/F3 tract resonators
                float tr2L = 0.0f, tr2R = 0.0f, tr3L = 0.0f, tr3R = 0.0f;
                consFormantSvf2.processBandpass(rawFricL, rawFricR, tr2L, tr2R);
                consFormantSvf3.processBandpass(rawFricL, rawFricR, tr3L, tr3R);
                float tractFricL = tr2L * consFormantSvf2.k * 0.75f + tr3L * consFormantSvf3.k * 0.55f;
                float tractFricR = tr2R * consFormantSvf2.k * 0.75f + tr3R * consFormantSvf3.k * 0.55f;

                float directMix = (hasActivePhoneme && currentPhoneme.cons.noiseFreq1Hz >= 2600.0f) ? 0.78f : 0.38f;
                float dualFricL = directMix * rawFricL + (1.0f - directMix) * tractFricL;
                float dualFricR = directMix * rawFricR + (1.0f - directMix) * tractFricR;

                float aggrConsBoost = (voiceMode == VOICE_AGGR) ? 1.25f : 1.0f;
                float codaBoost = (hasActivePhoneme && currentPhoneme.isCoda) ? 1.25f : 1.0f;
                float cScale = consVoiceMod * lastVelocity * 0.48f * aggrConsBoost * codaBoost;
                mixL += (dualFricL * consNoiseGain + aL * aspirationSvf.k * consAspGain * 0.58f) * cScale;
                mixR += (dualFricR * consNoiseGain + aR * aspirationSvf.k * consAspGain * 0.58f) * cScale;
            }

            // 7. DC block & transparent soft-limiter
            float dcL = mixL - dcPrevInL + 0.9975f * dcPrevOutL;
            dcPrevInL = mixL;
            dcPrevOutL = dcL;

            float dcR = mixR - dcPrevInR + 0.9975f * dcPrevOutR;
            dcPrevInR = mixR;
            dcPrevOutR = dcR;

            // Safety peak limiter (0.5 ms attack / 120 ms release): keeps loud vowels out of the
            // tanh's hard-saturation region instead of squaring them off (audible distortion).
            float outPk = std::max(std::fabs(dcL), std::fabs(dcR));
            outPeakEnv += ((outPk > outPeakEnv) ? limAtt : limRel) * (outPk - outPeakEnv);
            float limTarget = (outPeakEnv > 0.85f) ? (0.85f / outPeakEnv) : 1.0f;
            outLimGain += ((limTarget < outLimGain) ? limAtt : limRel) * (limTarget - outLimGain);
            dcL *= outLimGain;
            dcR *= outLimGain;

            outL[rendered + s] = fastTanh(dcL * 0.90f) * 0.96f;
            outR[rendered + s] = fastTanh(dcR * 0.90f) * 0.96f;
        }

        rendered += chunk;
    }

    while (midiCursor < renderMidiScratch.size()) {
        const auto& ev = renderMidiScratch[midiCursor];
        handleMidiEvent(ev.msg1, ev.msg2, ev.msg3);
        ++midiCursor;
    }
    renderMidiScratch.clear();
}

std::vector<uint8_t> BaburEngine::saveState() const {
    std::vector<uint8_t> buf;
    buf.reserve(640);

    buf.push_back('S'); buf.push_back('N'); buf.push_back('G'); buf.push_back('R');
    writeU32LE(buf, 1u);

    // Keep first 8 parameters in the original Chunk v1 layout for backward compatibility
    for (int i = 0; i < 8; ++i) {
        writeF32LE(buf, getParam(i));
    }

    int32_t preset = getCurrentPreset();
    int32_t sylIdx = currentSyllableIdx.load(std::memory_order_relaxed);
    std::string path = getSourcePath();
    std::string text = getText();

    writeI32LE(buf, preset);
    writeI32LE(buf, sylIdx);

    writeU32LE(buf, static_cast<uint32_t>(path.size()));
    buf.insert(buf.end(), path.begin(), path.end());

    writeU32LE(buf, static_cast<uint32_t>(text.size()));
    buf.insert(buf.end(), text.begin(), text.end());

    int32_t rKey = resetMidiKey.load(std::memory_order_relaxed);
    writeI32LE(buf, rKey);

    // Append 'VOIC' extension block for voice-sculpting parameters (8..NUM_PARAMS-1)
    buf.push_back('V'); buf.push_back('O'); buf.push_back('I'); buf.push_back('C');
    uint32_t extCount = static_cast<uint32_t>(NUM_PARAMS - 8);
    writeU32LE(buf, extCount);
    for (int i = 8; i < NUM_PARAMS; ++i) {
        writeF32LE(buf, getParam(i));
    }

    return buf;
}

void BaburEngine::restoreState(const uint8_t* data, size_t size) {
    constexpr size_t kBaseParamCount = 8;
    if (!data || size < 4 + 4 + kBaseParamCount * 4 + 16) return;
    if (data[0] != 'S' || data[1] != 'N' || data[2] != 'G' || data[3] != 'R') return;

    size_t pos = 4;
    uint32_t version = 0;
    if (!readU32LE(data, size, pos, version) || version != 1u) return;

    float loadedBase[kBaseParamCount]{};
    for (size_t i = 0; i < kBaseParamCount; ++i) {
        if (!readF32LE(data, size, pos, loadedBase[i])) return;
    }

    int32_t preset = 0;
    int32_t sylIdx = 0;
    if (!readI32LE(data, size, pos, preset)) return;
    if (!readI32LE(data, size, pos, sylIdx)) return;

    uint32_t pathLen = 0;
    if (!readU32LE(data, size, pos, pathLen) || pos + pathLen > size) return;
    std::string path(reinterpret_cast<const char*>(data + pos), pathLen);
    pos += pathLen;

    uint32_t textLen = 0;
    if (!readU32LE(data, size, pos, textLen) || pos + textLen > size) return;
    std::string text(reinterpret_cast<const char*>(data + pos), textLen);
    pos += textLen;

    int32_t rKey = -1;
    if (pos + 4 <= size) {
        readI32LE(data, size, pos, rKey);
    }

    paramCharacter.store(std::clamp(loadedBase[PARAM_CHARACTER], 0.0f, 1.0f), std::memory_order_relaxed);
    paramDiction.store(std::clamp(loadedBase[PARAM_DICTION], 0.0f, 1.0f), std::memory_order_relaxed);
    paramThroat.store(std::clamp(loadedBase[PARAM_THROAT], 0.0f, 1.0f), std::memory_order_relaxed);
    paramGlide.store(std::clamp(loadedBase[PARAM_GLIDE], 0.0f, 1.0f), std::memory_order_relaxed);
    paramLang.store(std::clamp(loadedBase[PARAM_LANG], 0.0f, 1.0f), std::memory_order_relaxed);
    paramLegato.store(std::clamp(loadedBase[PARAM_LEGATO], 0.0f, 1.0f), std::memory_order_relaxed);
    paramAudioMode.store(std::clamp(loadedBase[PARAM_AUDIO_MODE], 0.0f, 1.0f), std::memory_order_relaxed);
    paramExciter.store(std::clamp(loadedBase[PARAM_EXCITER], 0.0f, 1.0f), std::memory_order_relaxed);

    // Reset voice-sculpting parameters to neutral defaults before checking optional 'VOIC' block
    paramGender.store(0.50f, std::memory_order_relaxed);
    paramPitchShift.store(0.50f, std::memory_order_relaxed);
    paramBreathiness.store(0.00f, std::memory_order_relaxed);
    paramGrit.store(0.00f, std::memory_order_relaxed);
    paramNasality.store(0.00f, std::memory_order_relaxed);
    paramBrightness.store(0.50f, std::memory_order_relaxed);
    paramVibDepth.store(0.25f, std::memory_order_relaxed);
    paramVibRate.store((5.2f - 2.5f) / 6.5f, std::memory_order_relaxed);
    paramTremor.store(0.00f, std::memory_order_relaxed);
    paramRobotic.store(0.00f, std::memory_order_relaxed);

    if (pos + 8 <= size &&
        data[pos] == 'V' && data[pos + 1] == 'O' && data[pos + 2] == 'I' && data[pos + 3] == 'C') {
        pos += 4;
        uint32_t extCount = 0;
        if (readU32LE(data, size, pos, extCount)) {
            uint32_t toRead = std::min<uint32_t>(extCount, static_cast<uint32_t>(NUM_PARAMS - 8));
            for (uint32_t i = 0; i < toRead; ++i) {
                float v = 0.0f;
                if (readF32LE(data, size, pos, v)) {
                    setParam(8 + static_cast<int>(i), v);
                }
            }
        }
    }

    if (!path.empty()) {
        if (loadSourceFile(path) && sourceType.load(std::memory_order_relaxed) == SRC_SF2) {
            setPresetIndex(preset);
        }
    } else {
        unloadSource();
    }

    setText(text);
    setCurrentSyllableIndex(sylIdx);
    setResetMidiKey(rKey);
    uiDirty.store(true, std::memory_order_relaxed);
}

} // namespace Fraudize
