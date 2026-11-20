#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Fraudize {

enum VowelId {
    V_A = 0,          // /ɑ/ TR a, EN father
    V_AE,             // /æ/ EN cat
    V_E,              // /ɛ/ TR e, EN bed
    V_I_DOTLESS,      // /ɯ/ TR ı, EN /ʌ/ cup/the
    V_I,              // /i/ TR i, EN see
    V_IH,             // /ɪ/ EN sit
    V_O,              // /o/ TR o, EN go/more
    V_OE,             // /ø/ TR ö, EN /ɜː/ bird/world
    V_U,              // /u/ TR u, EN boot/you
    V_UE,             // /y/ TR ü
    NUM_VOWELS
};

enum EmotionId : uint8_t {
    EMO_NEUTRAL = 0,
    EMO_HAPPY,
    EMO_SAD,
    EMO_ANGRY,
    EMO_SOFT,
    EMO_EXCITED
};

} // namespace Fraudize

#include "EnglishLexicon.hpp"

namespace Fraudize {

struct VowelFormants {
    float f1, f2, f3, f4;
    float q1, q2, q3, q4;
    const char* shortName;
};

inline const VowelFormants& getVowelFormants(VowelId v) {
    // Natural human vocal tract bandwidths (non-ringing Q=4.8..6.2) with crisp formant contrast
    static constexpr VowelFormants kTable[NUM_VOWELS] = {
        { 760.0f, 1150.0f, 2550.0f, 3500.0f, 4.8f, 5.5f, 6.0f, 6.2f, "A"  }, // V_A
        { 690.0f, 1760.0f, 2480.0f, 3450.0f, 4.8f, 5.5f, 6.0f, 6.2f, "AE" }, // V_AE
        { 520.0f, 1900.0f, 2550.0f, 3500.0f, 4.8f, 5.5f, 6.0f, 6.2f, "E"  }, // V_E
        { 440.0f, 1350.0f, 2450.0f, 3400.0f, 4.8f, 5.4f, 6.0f, 6.2f, "I'" }, // V_I_DOTLESS
        { 265.0f, 2420.0f, 3100.0f, 3750.0f, 4.8f, 5.8f, 6.0f, 6.2f, "I"  }, // V_I
        { 385.0f, 2020.0f, 2620.0f, 3550.0f, 4.8f, 5.5f, 6.0f, 6.2f, "IH" }, // V_IH
        { 450.0f,  820.0f, 2480.0f, 3400.0f, 4.8f, 5.5f, 6.0f, 6.2f, "O"  }, // V_O
        { 440.0f, 1540.0f, 2320.0f, 3400.0f, 4.8f, 5.5f, 6.0f, 6.2f, "OE" }, // V_OE
        { 285.0f,  760.0f, 2320.0f, 3350.0f, 4.8f, 5.6f, 6.0f, 6.2f, "U"  }, // V_U
        { 280.0f, 1840.0f, 2260.0f, 3400.0f, 4.8f, 5.6f, 6.0f, 6.2f, "UE" }  // V_UE
    };
    int idx = static_cast<int>(v);
    if (idx < 0 || idx >= NUM_VOWELS) idx = 0;
    return kTable[idx];
}

enum ConsonantType {
    C_NONE = 0,
    C_PLOSIVE_UNVOICED,
    C_PLOSIVE_VOICED,
    C_FRICATIVE_UNVOICED,
    C_FRICATIVE_VOICED,
    C_AFFRICATE_UNVOICED,
    C_AFFRICATE_VOICED,
    C_NASAL,
    C_LIQUID_GLIDE
};

struct ConsonantSpec {
    ConsonantType type = C_NONE;
    float noiseFreq1Hz = 2500.0f;
    float noiseBw1Hz = 800.0f;
    float noiseFreq2Hz = 4200.0f;
    float noiseBw2Hz = 1200.0f;
    float noiseGain = 0.0f;
    float aspirationGain = 0.0f;
    float murmurGain = 0.0f;
    float locusF1 = 400.0f;
    float locusF2 = 1500.0f;
    float locusF3 = 2500.0f;
    float closureMs = 0.0f;
    float burstOrFricMs = 0.0f;
    float transitionMs = 20.0f;
    char label[4] = { '-', 0, 0, 0 };

    float totalDurationMs() const {
        return closureMs + burstOrFricMs + transitionMs;
    }
};

struct SyllableToken {
    enum PhraseBreak : uint8_t {
        BREAK_NONE = 0,
        BREAK_MINOR,     // , ; :
        BREAK_MAJOR,     // . ! \n
        BREAK_QUESTION   // ?
    };

    std::string display;
    std::string phoneticHint;
    std::vector<ConsonantSpec> onsetCluster;
    ConsonantSpec onset;
    VowelId vowel1 = V_A;
    VowelId vowel2 = V_A;
    bool isDiphthong = false;
    bool rColored = false;
    bool lColored = false;
    bool nasalized = false;
    std::vector<ConsonantSpec> codaCluster;
    ConsonantSpec coda;

    // Word-level context & coarticulated acoustic trajectory (computed per word)
    bool isWordStart = true;
    bool isWordEnd = true;
    bool isStressed = true;
    float contextF1 = 0.0f;
    float contextF2 = 0.0f;
    float contextF3 = 0.0f;
    float contextEndF1 = 0.0f;
    float contextEndF2 = 0.0f;
    float contextEndF3 = 0.0f;

    // Sentence-level phonology & prosody (computed across phrases by applySentenceContext)
    PhraseBreak breakAfter = BREAK_NONE;
    bool isFunctionWord = false;
    float prominence = 1.0f;         // 0.74 (weak function word) .. 1.26 (emphasized word)
    float prosodyPitchSemis = 0.0f;  // subtle sentence contour (-0.10 .. +0.36 st)
    bool hasSoftGLink = false;       // Turkish ğ smooth vowel bridge / lengthening
    EmotionId emotion = EMO_NEUTRAL; // inline [emotion] tag active at this word
    bool isEmphasized = false;       // wrapped in *word* emphasis markers
    int8_t explicitStress = -1;      // -1=auto, 0=unstressed, 1=primary stress (lexicon/TR grammar)
};

struct WordSpan {
    size_t startIdx = 0;
    size_t endIdx = 0;
    std::string lowerWord;
    SyllableToken::PhraseBreak breakAfter = SyllableToken::BREAK_NONE;
    EmotionId emotion = EMO_NEUTRAL;
    bool isEmphasized = false;
    bool hasExclamation = false;
};

class TextPhonemeDissector {
public:
    static ConsonantSpec makeConsonant(const char* key) {
        if (!key || !key[0] || key[0] == '-') return ConsonantSpec{};
        auto eq = [&](const char* s) { return std::strcmp(key, s) == 0; };

        if (eq("p"))  return { C_PLOSIVE_UNVOICED,   750.0f,  500.0f, 1650.0f,  900.0f, 0.72f, 0.50f, 0.00f, 240.0f,  850.0f, 2100.0f, 22.0f, 14.0f, 22.0f, "p"  };
        if (eq("t"))  return { C_PLOSIVE_UNVOICED,  3900.0f, 1200.0f, 5600.0f, 1600.0f, 0.92f, 0.55f, 0.00f, 280.0f, 1780.0f, 2700.0f, 20.0f, 16.0f, 22.0f, "t"  };
        if (eq("k"))  return { C_PLOSIVE_UNVOICED,  1850.0f,  750.0f, 3400.0f, 1100.0f, 0.88f, 0.58f, 0.00f, 290.0f, 1450.0f, 2320.0f, 22.0f, 16.0f, 24.0f, "k"  };

        if (eq("ch")) return { C_AFFRICATE_UNVOICED,2850.0f,  900.0f, 4600.0f, 1400.0f, 0.96f, 0.30f, 0.00f, 300.0f, 2000.0f, 2750.0f, 18.0f, 36.0f, 20.0f, "ch" };
        if (eq("j"))  return { C_AFFRICATE_VOICED,  2650.0f,  850.0f, 4300.0f, 1300.0f, 0.65f, 0.10f, 0.45f, 280.0f, 1950.0f, 2650.0f, 16.0f, 34.0f, 20.0f, "j"  };

        if (eq("b"))  return { C_PLOSIVE_VOICED,     620.0f,  450.0f, 1350.0f,  800.0f, 0.34f, 0.05f, 0.65f, 220.0f,  840.0f, 2100.0f, 20.0f, 10.0f, 24.0f, "b"  };
        if (eq("d"))  return { C_PLOSIVE_VOICED,    3400.0f, 1100.0f, 5000.0f, 1400.0f, 0.42f, 0.08f, 0.60f, 240.0f, 1760.0f, 2660.0f, 18.0f, 10.0f, 24.0f, "d"  };
        if (eq("g"))  return { C_PLOSIVE_VOICED,    1650.0f,  700.0f, 2850.0f, 1000.0f, 0.40f, 0.08f, 0.60f, 250.0f, 1420.0f, 2280.0f, 20.0f, 10.0f, 25.0f, "g"  };

        if (eq("s"))  return { C_FRICATIVE_UNVOICED,5400.0f, 1300.0f, 7200.0f, 1800.0f, 1.00f, 0.10f, 0.00f, 310.0f, 1680.0f, 2680.0f,  0.0f, 58.0f, 18.0f, "s"  };
        if (eq("sh")) return { C_FRICATIVE_UNVOICED,2750.0f,  850.0f, 4400.0f, 1300.0f, 0.98f, 0.15f, 0.00f, 310.0f, 2000.0f, 2680.0f,  0.0f, 58.0f, 18.0f, "sh" };
        if (eq("f"))  return { C_FRICATIVE_UNVOICED,3600.0f, 1600.0f, 6200.0f, 2200.0f, 0.65f, 0.25f, 0.00f, 310.0f, 1150.0f, 2350.0f,  0.0f, 48.0f, 18.0f, "f"  };
        if (eq("th")) return { C_FRICATIVE_UNVOICED,4100.0f, 1700.0f, 6400.0f, 2200.0f, 0.60f, 0.25f, 0.00f, 310.0f, 1480.0f, 2500.0f,  0.0f, 46.0f, 18.0f, "th" };
        if (eq("h"))  return { C_FRICATIVE_UNVOICED,1800.0f, 1000.0f, 3200.0f, 1400.0f, 0.42f, 0.85f, 0.00f, 480.0f, 1500.0f, 2500.0f,  0.0f, 38.0f, 18.0f, "h"  };

        if (eq("z"))  return { C_FRICATIVE_VOICED,  5100.0f, 1300.0f, 6900.0f, 1800.0f, 0.72f, 0.00f, 0.48f, 300.0f, 1660.0f, 2620.0f,  0.0f, 48.0f, 18.0f, "z"  };
        if (eq("zh")) return { C_FRICATIVE_VOICED,  2600.0f,  850.0f, 4200.0f, 1300.0f, 0.68f, 0.00f, 0.48f, 300.0f, 1960.0f, 2620.0f,  0.0f, 46.0f, 18.0f, "zh" };
        if (eq("v"))  return { C_FRICATIVE_VOICED,  3300.0f, 1500.0f, 5600.0f, 2000.0f, 0.45f, 0.00f, 0.55f, 290.0f, 1140.0f, 2320.0f,  0.0f, 40.0f, 18.0f, "v"  };
        if (eq("dh")) return { C_FRICATIVE_VOICED,  3600.0f, 1500.0f, 5800.0f, 2000.0f, 0.42f, 0.00f, 0.55f, 300.0f, 1450.0f, 2450.0f,  0.0f, 38.0f, 18.0f, "dh" };

        if (eq("m"))  return { C_NASAL,              900.0f,  400.0f, 1800.0f,  600.0f, 0.00f, 0.00f, 0.85f, 255.0f,  950.0f, 2150.0f,  0.0f, 38.0f, 22.0f, "m"  };
        if (eq("n"))  return { C_NASAL,             1400.0f,  500.0f, 2400.0f,  700.0f, 0.00f, 0.00f, 0.82f, 265.0f, 1500.0f, 2480.0f,  0.0f, 36.0f, 22.0f, "n"  };
        if (eq("ng")) return { C_NASAL,             1200.0f,  450.0f, 2200.0f,  650.0f, 0.00f, 0.00f, 0.85f, 275.0f, 1220.0f, 2280.0f,  0.0f, 38.0f, 22.0f, "ng" };

        if (eq("l"))  return { C_LIQUID_GLIDE,      1200.0f,  400.0f, 2400.0f,  600.0f, 0.00f, 0.00f, 0.25f, 350.0f, 1180.0f, 2780.0f,  0.0f, 26.0f, 24.0f, "l"  };
        if (eq("r"))  return { C_LIQUID_GLIDE,      1400.0f,  500.0f, 1750.0f,  600.0f, 0.00f, 0.00f, 0.22f, 410.0f, 1180.0f, 1640.0f,  0.0f, 28.0f, 24.0f, "r"  };
        if (eq("w"))  return { C_LIQUID_GLIDE,       800.0f,  350.0f, 1500.0f,  500.0f, 0.00f, 0.00f, 0.18f, 300.0f,  720.0f, 2280.0f,  0.0f, 26.0f, 26.0f, "w"  };
        if (eq("y"))  return { C_LIQUID_GLIDE,      2200.0f,  600.0f, 3000.0f,  700.0f, 0.00f, 0.00f, 0.15f, 275.0f, 2320.0f, 3020.0f,  0.0f, 26.0f, 24.0f, "y"  };

        return ConsonantSpec{};
    }

    static std::string toLowerUtf8(const std::vector<uint32_t>& cps) {
        std::string out;
        for (uint32_t cp : cps) appendUtf8(out, cp);
        return out;
    }

    static bool parseEmotionTag(const std::string& tag, EmotionId& outEmo) {
        if (tag == "neutral" || tag == "normal" || tag == "notr" || tag == "n\xC3\xB6tr") {
            outEmo = EMO_NEUTRAL; return true;
        }
        if (tag == "happy" || tag == "joy" || tag == "smile" ||
            tag == "mutlu" || tag == "nese" || tag == "ne\xC5\x9F" "e") {
            outEmo = EMO_HAPPY; return true;
        }
        if (tag == "sad" || tag == "cry" || tag == "melancholy" ||
            tag == "uzgun" || tag == "\xC3\xBCzg\xC3\xBCn" ||
            tag == "huzun" || tag == "h\xC3\xBCz\xC3\xBCn") {
            outEmo = EMO_SAD; return true;
        }
        if (tag == "angry" || tag == "mad" || tag == "rage" || tag == "furious" ||
            tag == "kizgin" || tag == "k\xC4\xB1zg\xC4\xB1n" ||
            tag == "sinirli" || tag == "ofkeli" || tag == "\xC3\xB6" "fkeli") {
            outEmo = EMO_ANGRY; return true;
        }
        if (tag == "soft" || tag == "calm" || tag == "whisper" || tag == "intimate" || tag == "gentle" ||
            tag == "yumusak" || tag == "yumu\xC5\x9F" "ak" || tag == "sakin") {
            outEmo = EMO_SOFT; return true;
        }
        if (tag == "excited" || tag == "hype" || tag == "energetic" ||
            tag == "heyecanli" || tag == "heyecanl\xC4\xB1" ||
            tag == "coskulu" || tag == "co\xC5\x9Fkulu") {
            outEmo = EMO_EXCITED; return true;
        }
        return false;
    }

    static std::vector<SyllableToken> dissectText(const std::string& text, bool isTurkish) {
        std::vector<uint32_t> cps = decodeUtf8(text);
        std::vector<SyllableToken> result;
        std::vector<WordSpan> words;

        EmotionId currentEmotion = EMO_NEUTRAL;
        bool inEmphasis = false;
        bool wordEmphasized = false;

        std::vector<uint32_t> curWord;
        auto flushWord = [&]() {
            if (curWord.empty()) return;
            std::string lowerW = toLowerUtf8(curWord);
            std::vector<SyllableToken> wordSylls = isTurkish
                ? dissectTurkishWord(curWord)
                : dissectEnglishWord(curWord);
            if (isTurkish) {
                applyTurkishGrammaticalStress(wordSylls, lowerW);
            }
            applyWordContext(wordSylls, isTurkish);
            if (!wordSylls.empty()) {
                bool emph = (inEmphasis || wordEmphasized);
                for (auto& t : wordSylls) {
                    t.emotion = currentEmotion;
                    t.isEmphasized = emph;
                }
                WordSpan ws;
                ws.startIdx = result.size();
                result.insert(result.end(), wordSylls.begin(), wordSylls.end());
                ws.endIdx = result.size();
                ws.lowerWord = std::move(lowerW);
                ws.breakAfter = SyllableToken::BREAK_NONE;
                ws.emotion = currentEmotion;
                ws.isEmphasized = emph;
                words.push_back(std::move(ws));
            }
            curWord.clear();
            wordEmphasized = inEmphasis;
        };

        auto upgradeBreak = [&](SyllableToken::PhraseBreak brk) {
            if (words.empty()) return;
            auto& cur = words.back().breakAfter;
            if (brk == SyllableToken::BREAK_QUESTION) {
                cur = SyllableToken::BREAK_QUESTION;
            } else if (brk == SyllableToken::BREAK_MAJOR && cur != SyllableToken::BREAK_QUESTION) {
                cur = SyllableToken::BREAK_MAJOR;
            } else if (brk == SyllableToken::BREAK_MINOR && cur == SyllableToken::BREAK_NONE) {
                cur = SyllableToken::BREAK_MINOR;
            }
        };

        for (size_t i = 0; i < cps.size(); ++i) {
            uint32_t cp = cps[i];
            if (cp == 0x0307) continue;
            if (cp == '[') {
                flushWord();
                std::vector<uint32_t> tagCps;
                size_t j = i + 1;
                while (j < cps.size() && cps[j] != ']') {
                    uint32_t tc = cps[j];
                    if (tc != ' ' && tc != '\t') {
                        tagCps.push_back(isLetter(tc) ? toLowerLetter(tc, false) : tc);
                    }
                    ++j;
                }
                EmotionId parsedEmo = currentEmotion;
                if (parseEmotionTag(toLowerUtf8(tagCps), parsedEmo)) {
                    currentEmotion = parsedEmo;
                }
                i = (j < cps.size()) ? j : (cps.size() - 1);
                continue;
            }
            if (cp == '*') {
                if (!curWord.empty()) {
                    wordEmphasized = true;
                    flushWord();
                } else if (inEmphasis && !words.empty()) {
                    words.back().isEmphasized = true;
                    for (size_t si = words.back().startIdx; si < words.back().endIdx; ++si) {
                        result[si].isEmphasized = true;
                    }
                }
                inEmphasis = !inEmphasis;
                wordEmphasized = inEmphasis;
                continue;
            }
            if (isLetter(cp)) {
                curWord.push_back(toLowerLetter(cp, isTurkish));
            } else {
                if ((cp == '\'' || cp == 0x2019) && !curWord.empty() &&
                    (i + 1 < cps.size()) && isLetter(cps[i + 1])) {
                    continue;
                }
                flushWord();
                if (cp == '?') {
                    upgradeBreak(SyllableToken::BREAK_QUESTION);
                } else if (cp == '!') {
                    if (!words.empty()) words.back().hasExclamation = true;
                    upgradeBreak(SyllableToken::BREAK_MAJOR);
                } else if (cp == '.' || cp == '\n') {
                    upgradeBreak(SyllableToken::BREAK_MAJOR);
                } else if (cp == ',' || cp == ';' || cp == ':') {
                    upgradeBreak(SyllableToken::BREAK_MINOR);
                }
            }
        }
        flushWord();

        for (const auto& w : words) {
            if (w.endIdx > w.startIdx) {
                result[w.endIdx - 1].breakAfter = w.breakAfter;
            }
        }
        applySentenceContext(result, words, isTurkish);

        if (result.empty()) {
            SyllableToken defTok = makeToken("AH", {}, V_A, V_A, false, {});
            defTok.emotion = currentEmotion;
            result.push_back(std::move(defTok));
        }
        return result;
    }

private:
    static std::vector<uint32_t> decodeUtf8(const std::string& s) {
        std::vector<uint32_t> out;
        size_t i = 0;
        while (i < s.size()) {
            uint8_t c = static_cast<uint8_t>(s[i]);
            if (c < 0x80) {
                out.push_back(c);
                ++i;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
                uint32_t cp = ((c & 0x1F) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3F);
                out.push_back(cp);
                i += 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
                uint32_t cp = ((c & 0x0F) << 12) |
                              ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 6) |
                              (static_cast<uint8_t>(s[i + 2]) & 0x3F);
                out.push_back(cp);
                i += 3;
            } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
                uint32_t cp = ((c & 0x07) << 18) |
                              ((static_cast<uint8_t>(s[i + 1]) & 0x3F) << 12) |
                              ((static_cast<uint8_t>(s[i + 2]) & 0x3F) << 6) |
                              (static_cast<uint8_t>(s[i + 3]) & 0x3F);
                out.push_back(cp);
                i += 4;
            } else {
                ++i;
            }
        }
        return out;
    }

    static void appendUtf8(std::string& dst, uint32_t cp) {
        if (cp < 0x80) {
            dst.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            dst.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            dst.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            dst.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    static bool isLetter(uint32_t cp) {
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
        switch (cp) {
            case 0x00C7: case 0x00E7:
            case 0x011E: case 0x011F:
            case 0x0130: case 0x0131:
            case 0x00D6: case 0x00F6:
            case 0x015E: case 0x015F:
            case 0x00DC: case 0x00FC:
            case 0x00C2: case 0x00E2:
            case 0x00CE: case 0x00EE:
            case 0x00DB: case 0x00FB:
                return true;
            default:
                return false;
        }
    }

    static uint32_t toLowerLetter(uint32_t cp, bool isTurkish) {
        if (cp == 'I') return isTurkish ? 0x0131u : 'i';
        if (cp == 0x0130u) return 'i';
        if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 'a';
        if (cp == 0x00C7u) return 0x00E7u;
        if (cp == 0x011Eu) return 0x011Fu;
        if (cp == 0x00D6u) return 0x00F6u;
        if (cp == 0x015Eu) return 0x015Fu;
        if (cp == 0x00DCu) return 0x00FCu;
        if (cp == 0x00C2u || cp == 0x00E2u) return 'a';
        if (cp == 0x00CEu || cp == 0x00EEu) return 'i';
        if (cp == 0x00DBu || cp == 0x00FBu) return 'u';
        return cp;
    }

    static std::string toUpperUtf8(const std::vector<uint32_t>& cps, bool isTurkish) {
        std::string out;
        for (uint32_t cp : cps) {
            if (cp == 'i') appendUtf8(out, isTurkish ? 0x0130u : 'I');
            else if (cp == 0x0131u) appendUtf8(out, 'I');
            else if (cp >= 'a' && cp <= 'z') appendUtf8(out, cp - 'a' + 'A');
            else if (cp == 0x00E7u) appendUtf8(out, 0x00C7u);
            else if (cp == 0x011Fu) appendUtf8(out, 0x011Eu);
            else if (cp == 0x00F6u) appendUtf8(out, 0x00D6u);
            else if (cp == 0x015Fu) appendUtf8(out, 0x015Eu);
            else if (cp == 0x00FCu) appendUtf8(out, 0x00DCu);
            else appendUtf8(out, cp);
        }
        return out;
    }

    static std::string cpToAsciiStr(const std::vector<uint32_t>& cps) {
        std::string s;
        for (uint32_t cp : cps) {
            if (cp < 0x80) s.push_back(static_cast<char>(cp));
        }
        return s;
    }

    static std::vector<ConsonantSpec> parseConsonantKeys(const char* keys) {
        std::vector<ConsonantSpec> out;
        if (!keys || !keys[0] || keys[0] == '-') return out;
        std::string s(keys);
        size_t pos = 0;
        while (pos < s.size() && out.size() < 3) {
            size_t comma = s.find(',', pos);
            std::string part = (comma == std::string::npos) ? s.substr(pos) : s.substr(pos, comma - pos);
            ConsonantSpec cs = makeConsonant(part.c_str());
            if (cs.type != C_NONE) out.push_back(cs);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return out;
    }

    static ConsonantSpec pickDominantConsonant(const std::vector<ConsonantSpec>& cluster, bool preferLast) {
        if (cluster.empty()) return ConsonantSpec{};
        return preferLast ? cluster.back() : cluster.front();
    }

    static SyllableToken makeToken(const std::string& disp,
                                   const std::vector<ConsonantSpec>& onsetClus,
                                   VowelId v1, VowelId v2, bool diph,
                                   const std::vector<ConsonantSpec>& codaClus) {
        SyllableToken tok;
        tok.display = disp;
        tok.onsetCluster = onsetClus;
        if (tok.onsetCluster.size() > 3) tok.onsetCluster.resize(3);

        tok.vowel1 = v1;
        tok.vowel2 = diph ? v2 : v1;
        tok.isDiphthong = diph;

        tok.codaCluster = codaClus;
        if (tok.codaCluster.size() > 3) tok.codaCluster.resize(3);

        // Vowel-dependent velar palatalization, lateral/lip-rounding & palatal coarticulation
        const VowelFormants& vf = getVowelFormants(v1);
        float front = std::clamp((vf.f2 - 1100.0f) / 1200.0f, 0.0f, 1.0f);
        bool isRounded = (v1 == V_O || v1 == V_OE || v1 == V_U || v1 == V_UE);
        bool isHighFront = (v1 == V_I || v1 == V_IH || v1 == V_E);
        auto coarticulate = [&](std::vector<ConsonantSpec>& clus, bool isCodaCluster) {
            for (auto& c : clus) {
                if (std::strcmp(c.label, "k") == 0 && std::fabs(c.locusF2 - 1450.0f) < 5.0f) {
                    c.locusF2 = 1120.0f + 1080.0f * front;
                    c.noiseFreq1Hz = 1480.0f + 1220.0f * front;
                    c.noiseFreq2Hz = 2750.0f + 1450.0f * front;
                } else if (std::strcmp(c.label, "g") == 0 && std::fabs(c.locusF2 - 1420.0f) < 5.0f) {
                    c.locusF2 = 1100.0f + 1020.0f * front;
                    c.noiseFreq1Hz = 1380.0f + 1020.0f * front;
                    c.noiseFreq2Hz = 2400.0f + 1200.0f * front;
                } else if (std::strcmp(c.label, "l") == 0 && std::fabs(c.locusF2 - 1180.0f) < 5.0f) {
                    c.locusF2 = isCodaCluster ? (960.0f + 320.0f * front) : (1260.0f + 620.0f * front);
                } else if (isRounded) {
                    c.noiseFreq1Hz *= 0.84f;
                    c.noiseFreq2Hz *= 0.84f;
                } else if (isHighFront) {
                    c.noiseFreq1Hz *= 1.08f;
                }
            }
        };
        coarticulate(tok.onsetCluster, false);
        coarticulate(tok.codaCluster, true);

        tok.onset = pickDominantConsonant(tok.onsetCluster, false);
        tok.coda = pickDominantConsonant(tok.codaCluster, true);

        if (!tok.codaCluster.empty()) {
            const char* lbl = tok.codaCluster[0].label;
            if (std::strcmp(lbl, "r") == 0) tok.rColored = true;
            else if (std::strcmp(lbl, "l") == 0) tok.lColored = true;
            else if (tok.codaCluster[0].type == C_NASAL) tok.nasalized = true;
        }

        tok.phoneticHint = buildPhoneticHint(tok);
        return tok;
    }

    static std::string buildPhoneticHint(const SyllableToken& tok) {
        std::string hint;
        if (!tok.onsetCluster.empty()) {
            for (const auto& c : tok.onsetCluster) hint += c.label;
            hint += "\xC2\xB7"; // ·
        }
        hint += getVowelFormants(tok.vowel1).shortName;
        if (tok.isDiphthong && tok.vowel2 != tok.vowel1) {
            hint += "-";
            hint += getVowelFormants(tok.vowel2).shortName;
        }
        if (!tok.codaCluster.empty()) {
            hint += "\xC2\xB7";
            for (const auto& c : tok.codaCluster) hint += c.label;
        }
        return hint;
    }

    static void applyTurkishGrammaticalStress(std::vector<SyllableToken>& wordSylls, const std::string& lowerW) {
        const size_t n = wordSylls.size();
        if (n <= 1) return;

        auto markOnly = [&](size_t targetIdx) {
            if (targetIdx >= n) return;
            for (size_t i = 0; i < n; ++i) {
                wordSylls[i].explicitStress = (i == targetIdx) ? 1 : 0;
            }
        };

        // 1. Irregular non-final Turkish lexical stress (city/country names & adverbs)
        struct TrStressEx { const char* w; size_t idx; };
        static constexpr TrStressEx kTrEx[] = {
            { "t\xC3\xBCrkiye", 0 }, { "istanbul", 1 }, { "ankara", 0 }, { "izmir", 0 },
            { "\xC5\x9F" "imdi", 0 }, { "sadece", 0 }, { "b\xC3\xB6yle", 0 },
            { "\xC5\x9F\xC3\xB6yle", 0 }, { "\xC3\xB6yle", 0 }, { "hangi", 0 },
            { "nas\xC4\xB1l", 0 }, { "neden", 0 }, { "ni\xC3\xA7in", 0 },
            { "masa", 0 }, { "sinema", 1 }, { "galiba", 0 }, { "belki", 0 },
            { "yaln\xC4\xB1z", 0 }, { "ayr\xC4\xB1" "ca", 0 }, { "sonra", 0 },
            { "\xC3\xB6nce", 0 }, { "burada", 0 }, { "\xC5\x9F" "urada", 0 }, { "orada", 0 }
        };
        for (const auto& ex : kTrEx) {
            if (lowerW == ex.w && ex.idx < n) {
                markOnly(ex.idx);
                return;
            }
        }

        // 2. Progressive suffix -(I)yor throws primary stress onto the syllable BEFORE -yor
        // (and skips negative -mi-/-mI-/-mu-/-mü- before -yor, e.g. BİL-mi-yo-rum)
        for (size_t i = 1; i < n; ++i) {
            const std::string& d = wordSylls[i].display;
            bool endsYor = (d.size() >= 3 && d.compare(d.size() - 3, 3, "YOR") == 0);
            bool yoBeforeR = (d.size() >= 2 && d.compare(d.size() - 2, 2, "YO") == 0) &&
                             (i + 1 < n) &&
                             !wordSylls[i + 1].onsetCluster.empty() &&
                             std::strcmp(wordSylls[i + 1].onsetCluster[0].label, "r") == 0 &&
                             (wordSylls[i - 1].vowel1 == V_I || wordSylls[i - 1].vowel1 == V_I_DOTLESS ||
                              wordSylls[i - 1].vowel1 == V_U || wordSylls[i - 1].vowel1 == V_UE);
            if (endsYor || yoBeforeR) {
                size_t stIdx = i - 1;
                if (stIdx >= 1) {
                    const std::string& prevD = wordSylls[stIdx].display;
                    if (prevD == "M\xC4\xB0" || prevD == "MI" || prevD == "MU" || prevD == "M\xC3\x9C" ||
                        prevD == "M\xC4\xB0Y" || prevD == "MIY" || prevD == "MUY" || prevD == "M\xC3\x9CY") {
                        --stIdx;
                    }
                }
                markOnly(stIdx);
                return;
            }
        }

        // 3. Verb negation suffix -ma / -me throws stress onto the stem syllable BEFORE -ma/-me
        const std::string& lastD = wordSylls[n - 1].display;
        if ((lastD == "MA" || lastD == "ME") && lowerW.size() >= 4) {
            bool isNounException = (lowerW == "elma" || lowerW == "ama" || lowerW == "yazma" ||
                                    lowerW == "basma" || lowerW == "hurma" || lowerW == "s\xC4\xB1rma");
            if (!isNounException) {
                markOnly(n - 2);
                return;
            }
        }
    }

    static void applyWordContext(std::vector<SyllableToken>& wordSylls, bool isTurkish) {
        const size_t n = wordSylls.size();
        if (n == 0) return;

        auto lerpF = [](float a, float b, float t) { return a + (b - a) * t; };
        auto isWeakPrefix = [](const std::string& disp) {
            return disp == "A" || disp == "BE" || disp == "DE" || disp == "RE" ||
                   disp == "UN" || disp == "IN" || disp == "IM" || disp == "CON" ||
                   disp == "PRO" || disp == "TO" || disp == "FOR";
        };

        bool hasExplicit = false;
        for (const auto& t : wordSylls) {
            if (t.explicitStress >= 0) { hasExplicit = true; break; }
        }

        for (size_t i = 0; i < n; ++i) {
            SyllableToken& tok = wordSylls[i];
            tok.isWordStart = (i == 0);
            tok.isWordEnd = (i + 1 == n);

            if (n == 1) {
                tok.isStressed = true;
            } else if (hasExplicit) {
                tok.isStressed = (tok.explicitStress > 0);
            } else if (isTurkish) {
                tok.isStressed = (i + 1 == n) || (i == 0);
            } else {
                if (i == 0) {
                    tok.isStressed = !isWeakPrefix(wordSylls[0].display);
                } else if (i == 1 && isWeakPrefix(wordSylls[0].display)) {
                    tok.isStressed = true;
                } else {
                    tok.isStressed = tok.isDiphthong;
                }
            }
        }

        for (size_t i = 0; i < n; ++i) {
            SyllableToken& tok = wordSylls[i];
            const VowelFormants& vf1 = getVowelFormants(tok.vowel1);
            const VowelFormants& vf2 = getVowelFormants(tok.vowel2);

            float sF1 = vf1.f1, sF2 = vf1.f2, sF3 = vf1.f3;
            float eF1 = vf2.f1, eF2 = vf2.f2, eF3 = vf2.f3;

            // 1. Onset Consonant -> Vowel Coarticulation
            if (!tok.onsetCluster.empty()) {
                const ConsonantSpec& ons = tok.onsetCluster.back();
                float aOn = (ons.type == C_LIQUID_GLIDE) ? 0.22f : 0.14f;
                sF1 = lerpF(sF1, ons.locusF1, aOn);
                sF2 = lerpF(sF2, ons.locusF2, aOn);
                sF3 = lerpF(sF3, ons.locusF3, aOn);
                if (!tok.isDiphthong) {
                    eF2 = lerpF(eF2, ons.locusF2, 0.06f);
                }
            }

            // 1b. Turkish open /æ/-like 'e' allophone before coda r, l, n, m (e.g., gel, ben, sen, yer, kalem)
            if (isTurkish && tok.vowel1 == V_E && !tok.codaCluster.empty()) {
                const char* cLbl = tok.codaCluster.front().label;
                if (std::strcmp(cLbl, "r") == 0 || std::strcmp(cLbl, "l") == 0 ||
                    std::strcmp(cLbl, "n") == 0 || std::strcmp(cLbl, "m") == 0) {
                    sF1 = lerpF(sF1, 645.0f, 0.85f);
                    eF1 = lerpF(eF1, 645.0f, 0.85f);
                    sF2 = lerpF(sF2, 1780.0f, 0.70f);
                    eF2 = lerpF(eF2, 1780.0f, 0.70f);
                }
            }

            // 2. Coda Consonant -> Vowel Coarticulation
            if (!tok.codaCluster.empty()) {
                const ConsonantSpec& cod = tok.codaCluster.front();
                if (tok.rColored) {
                    if (!isTurkish) {
                        sF3 = lerpF(sF3, 1680.0f, 0.25f);
                        eF2 = lerpF(eF2, 1320.0f, 0.28f);
                        eF3 = lerpF(eF3, 1620.0f, 0.65f);
                    } else {
                        // Turkish tapped 'r' is non-retroflex: gentle alveolar F3 inflection
                        eF2 = lerpF(eF2, 1420.0f, 0.14f);
                        eF3 = lerpF(eF3, 2100.0f, 0.25f);
                    }
                } else if (tok.lColored) {
                    sF2 = lerpF(sF2, cod.locusF2, 0.15f);
                    eF1 = lerpF(eF1, 460.0f, 0.22f);
                    eF2 = lerpF(eF2, cod.locusF2, 0.36f);
                } else if (tok.nasalized) {
                    sF1 = lerpF(sF1, 540.0f, 0.14f);
                    eF1 = lerpF(eF1, 520.0f, 0.20f);
                    eF2 = lerpF(eF2, cod.locusF2, 0.24f);
                } else {
                    eF1 = lerpF(eF1, cod.locusF1, 0.18f);
                    eF2 = lerpF(eF2, cod.locusF2, 0.18f);
                    eF3 = lerpF(eF3, cod.locusF3, 0.18f);
                }

                // Turkish word-final 'r' subtle devoiced alveolar air release
                if (isTurkish && tok.isWordEnd && std::strcmp(tok.codaCluster.back().label, "r") == 0) {
                    ConsonantSpec& rc = tok.codaCluster.back();
                    rc.noiseFreq1Hz = 3150.0f;
                    rc.noiseBw1Hz = 1100.0f;
                    rc.noiseGain = 0.22f;
                    rc.aspirationGain = 0.14f;
                    tok.coda = rc;
                }
            }

            // 3. Vowel-to-Vowel (V-to-V) Anticipatory & Carryover Coarticulation inside the Word
            if (i > 0) {
                const VowelFormants& prevVf = getVowelFormants(wordSylls[i - 1].vowel2);
                sF1 = lerpF(sF1, prevVf.f1, 0.12f);
                sF2 = lerpF(sF2, prevVf.f2, 0.15f);
            }
            if (i + 1 < n) {
                const VowelFormants& nextVf = getVowelFormants(wordSylls[i + 1].vowel1);
                sF2 = lerpF(sF2, nextVf.f2, 0.08f);
                eF1 = lerpF(eF1, nextVf.f1, 0.18f);
                eF2 = lerpF(eF2, nextVf.f2, 0.18f);
            }

            // 4. Unstressed Syllable Vowel Centralization (keeps Turkish cleaner than English)
            if (!tok.isStressed && n > 1) {
                float cAmt = isTurkish ? 0.10f : 0.18f;
                sF1 = lerpF(sF1, 500.0f, cAmt);  sF2 = lerpF(sF2, 1480.0f, cAmt); sF3 = lerpF(sF3, 2480.0f, cAmt);
                eF1 = lerpF(eF1, 500.0f, cAmt);  eF2 = lerpF(eF2, 1480.0f, cAmt); eF3 = lerpF(eF3, 2480.0f, cAmt);
            }

            tok.contextF1    = std::clamp(sF1, 200.0f, 1100.0f);
            tok.contextF2    = std::clamp(sF2, 600.0f, 2900.0f);
            tok.contextF3    = std::clamp(sF3, 1450.0f, 3600.0f);
            tok.contextEndF1 = std::clamp(eF1, 200.0f, 1100.0f);
            tok.contextEndF2 = std::clamp(eF2, 600.0f, 2900.0f);
            tok.contextEndF3 = std::clamp(eF3, 1450.0f, 3600.0f);
        }

        // 5. Turkish intervocalic 'ğ' smooth vowel-to-vowel bridge (e.g., ağaç, soğuk, boğaz)
        if (isTurkish && n > 1) {
            for (size_t i = 0; i + 1 < n; ++i) {
                SyllableToken& a = wordSylls[i];
                SyllableToken& b = wordSylls[i + 1];
                if (a.hasSoftGLink && b.onsetCluster.empty()) {
                    float aEndF1 = a.contextEndF1, aEndF2 = a.contextEndF2, aEndF3 = a.contextEndF3;
                    float bStF1 = b.contextF1, bStF2 = b.contextF2, bStF3 = b.contextF3;
                    a.contextEndF1 = std::clamp(lerpF(aEndF1, bStF1, 0.42f), 200.0f, 1100.0f);
                    a.contextEndF2 = std::clamp(lerpF(aEndF2, bStF2, 0.42f), 600.0f, 2900.0f);
                    a.contextEndF3 = std::clamp(lerpF(aEndF3, bStF3, 0.42f), 1450.0f, 3600.0f);
                    b.contextF1 = std::clamp(lerpF(bStF1, aEndF1, 0.42f), 200.0f, 1100.0f);
                    b.contextF2 = std::clamp(lerpF(bStF2, aEndF2, 0.42f), 600.0f, 2900.0f);
                    b.contextF3 = std::clamp(lerpF(bStF3, aEndF3, 0.42f), 1450.0f, 3600.0f);
                }
            }
        }
    }

    static bool isFunctionWordStr(const std::string& w, bool isTurkish) {
        if (isTurkish) {
            static const char* kTrFunc[] = {
                "ve", "de", "da", "ki", "bir", "ile", "mi",
                "m\xC4\xB1", "mu", "m\xC3\xBC", "ya", "ama", "fakat", "i\xC3\xA7in", "gibi"
            };
            for (const char* f : kTrFunc) {
                if (w == f) return true;
            }
            return false;
        }
        static const char* kEnFunc[] = {
            "a", "an", "the", "to", "of", "and", "in", "on", "at", "for", "with", "by", "from",
            "as", "is", "are", "was", "were", "be", "am", "that", "or", "but", "if", "so", "than"
        };
        for (const char* f : kEnFunc) {
            if (w == f) return true;
        }
        return false;
    }

    static void applySentenceContext(std::vector<SyllableToken>& sylls,
                                     const std::vector<WordSpan>& words,
                                     bool isTurkish) {
        if (sylls.empty() || words.empty()) return;
        auto lerpF = [](float a, float b, float t) { return a + (b - a) * t; };

        // 1. Intonational phrase segmentation, function-word reduction & prominence contour
        size_t numW = words.size();
        size_t pStart = 0;
        while (pStart < numW) {
            size_t pEnd = pStart;
            while (pEnd + 1 < numW && words[pEnd].breakAfter == SyllableToken::BREAK_NONE) {
                ++pEnd;
            }
            size_t phraseWordCount = pEnd - pStart + 1;

            ptrdiff_t firstContentW = -1;
            ptrdiff_t lastContentW = -1;

            for (size_t wi = pStart; wi <= pEnd; ++wi) {
                const WordSpan& w = words[wi];
                bool isTrQuestionPart = isTurkish && (wi > pStart) &&
                    (w.lowerWord == "mi" || w.lowerWord == "m\xC4\xB1" || w.lowerWord == "mu" || w.lowerWord == "m\xC3\xBC");
                bool isFunc = isTrQuestionPart || ((phraseWordCount >= 3) && (wi < pEnd) && isFunctionWordStr(w.lowerWord, isTurkish));
                if (isFunc) {
                    float cAmt = isTurkish ? 0.12f : 0.24f;
                    float funcProm = isTurkish ? (isTrQuestionPart ? 0.78f : 0.80f) : 0.74f;
                    for (size_t si = w.startIdx; si < w.endIdx; ++si) {
                        SyllableToken& t = sylls[si];
                        t.isFunctionWord = true;
                        t.isStressed = false;
                        t.prominence = funcProm;
                        t.contextF1    = std::clamp(lerpF(t.contextF1, 495.0f, cAmt), 200.0f, 1100.0f);
                        t.contextF2    = std::clamp(lerpF(t.contextF2, 1460.0f, cAmt), 600.0f, 2900.0f);
                        t.contextF3    = std::clamp(lerpF(t.contextF3, 2480.0f, cAmt), 1450.0f, 3600.0f);
                        t.contextEndF1 = std::clamp(lerpF(t.contextEndF1, 495.0f, cAmt), 200.0f, 1100.0f);
                        t.contextEndF2 = std::clamp(lerpF(t.contextEndF2, 1460.0f, cAmt), 600.0f, 2900.0f);
                        t.contextEndF3 = std::clamp(lerpF(t.contextEndF3, 2480.0f, cAmt), 1450.0f, 3600.0f);
                    }
                    if (isTrQuestionPart && words[wi - 1].endIdx > words[wi - 1].startIdx) {
                        SyllableToken& prevLast = sylls[words[wi - 1].endIdx - 1];
                        prevLast.isStressed = true;
                        prevLast.prominence = std::max(prevLast.prominence, 1.16f);
                    }
                    if (isTurkish && w.lowerWord == "bir" && wi + 1 <= pEnd) {
                        const SyllableToken& nxt = sylls[words[wi + 1].startIdx];
                        if (!nxt.onsetCluster.empty() && w.endIdx > w.startIdx) {
                            SyllableToken& lastBir = sylls[w.endIdx - 1];
                            if (!lastBir.codaCluster.empty() && std::strcmp(lastBir.codaCluster.back().label, "r") == 0) {
                                lastBir.codaCluster.back().burstOrFricMs *= 0.50f;
                                lastBir.codaCluster.back().noiseGain *= 0.50f;
                                lastBir.coda = lastBir.codaCluster.back();
                            }
                        }
                    }
                } else {
                    if (firstContentW < 0) firstContentW = static_cast<ptrdiff_t>(wi);
                    lastContentW = static_cast<ptrdiff_t>(wi);
                }

                // Inline *word* emphasis and '!' boost
                if (w.isEmphasized || w.hasExclamation) {
                    size_t emphSi = w.startIdx;
                    for (size_t si = w.startIdx; si < w.endIdx; ++si) {
                        if (sylls[si].isStressed) { emphSi = si; break; }
                    }
                    if (w.isEmphasized) {
                        for (size_t si = w.startIdx; si < w.endIdx; ++si) {
                            SyllableToken& t = sylls[si];
                            t.isFunctionWord = false;
                            if (si == emphSi || (w.endIdx - w.startIdx == 1)) {
                                t.isStressed = true;
                                t.prominence = 1.26f;
                                t.prosodyPitchSemis += 0.20f;
                                auto pushOut10 = [](float val, float center, float mn, float mx) {
                                    return std::clamp(val + (val - center) * 0.10f, mn, mx);
                                };
                                t.contextF1    = pushOut10(t.contextF1, 500.0f, 200.0f, 1100.0f);
                                t.contextF2    = pushOut10(t.contextF2, 1480.0f, 600.0f, 2900.0f);
                                t.contextEndF1 = pushOut10(t.contextEndF1, 500.0f, 200.0f, 1100.0f);
                                t.contextEndF2 = pushOut10(t.contextEndF2, 1480.0f, 600.0f, 2900.0f);
                            } else {
                                t.prominence = std::max(t.prominence, 1.05f);
                            }
                        }
                    } else if (emphSi < w.endIdx) {
                        sylls[emphSi].prominence = std::min(1.26f, sylls[emphSi].prominence + 0.08f);
                    }
                }
            }

            if (phraseWordCount >= 2) {
                auto applyProm = [&](ptrdiff_t wi, float promTarget, bool expandFormants) {
                    if (wi < 0) return;
                    const WordSpan& w = words[static_cast<size_t>(wi)];
                    size_t targetSi = w.startIdx;
                    for (size_t si = w.startIdx; si < w.endIdx; ++si) {
                        if (sylls[si].isStressed) { targetSi = si; break; }
                    }
                    if (targetSi < w.endIdx) {
                        SyllableToken& st = sylls[targetSi];
                        st.prominence = std::max(st.prominence, promTarget);
                        if (expandFormants) {
                            auto pushOut = [](float val, float center, float mn, float mx) {
                                return std::clamp(val + (val - center) * 0.07f, mn, mx);
                            };
                            st.contextF1    = pushOut(st.contextF1, 500.0f, 200.0f, 1100.0f);
                            st.contextF2    = pushOut(st.contextF2, 1480.0f, 600.0f, 2900.0f);
                            st.contextEndF1 = pushOut(st.contextEndF1, 500.0f, 200.0f, 1100.0f);
                            st.contextEndF2 = pushOut(st.contextEndF2, 1480.0f, 600.0f, 2900.0f);
                        }
                    }
                };
                applyProm(firstContentW, 1.05f, false);
                applyProm(lastContentW, 1.14f, true);
            }

            if (words[pEnd].endIdx > words[pEnd].startIdx) {
                SyllableToken& pFinal = sylls[words[pEnd].endIdx - 1];
                if (words[pEnd].breakAfter == SyllableToken::BREAK_QUESTION) {
                    pFinal.prosodyPitchSemis = 0.16f;
                } else if (words[pEnd].breakAfter == SyllableToken::BREAK_MINOR) {
                    pFinal.prosodyPitchSemis = 0.07f;
                } else if (words[pEnd].breakAfter == SyllableToken::BREAK_MAJOR) {
                    pFinal.prosodyPitchSemis = -0.10f;
                    pFinal.prominence = std::min(pFinal.prominence, 0.94f);
                }
            }

            pStart = pEnd + 1;
        }

        // 2. Cross-word sandhi, place assimilation, flapping, palatalization & V-to-V
        for (size_t wi = 0; wi + 1 < numW; ++wi) {
            if (words[wi].breakAfter != SyllableToken::BREAK_NONE) continue;
            if (words[wi].endIdx <= words[wi].startIdx || words[wi + 1].endIdx <= words[wi + 1].startIdx) continue;

            SyllableToken& prev = sylls[words[wi].endIdx - 1];
            SyllableToken& next = sylls[words[wi + 1].startIdx];

            // 2a. English "the" -> /ði/ (V_I) before vowel-initial word
            if (!isTurkish && words[wi].lowerWord == "the" && next.onsetCluster.empty()) {
                prev.vowel1 = V_I;
                prev.vowel2 = V_I;
                prev.isDiphthong = false;
                const VowelFormants& vi = getVowelFormants(V_I);
                float dhF1 = 300.0f, dhF2 = 1450.0f, dhF3 = 2450.0f;
                if (!prev.onsetCluster.empty()) {
                    dhF1 = prev.onsetCluster.back().locusF1;
                    dhF2 = prev.onsetCluster.back().locusF2;
                    dhF3 = prev.onsetCluster.back().locusF3;
                }
                prev.contextF1 = std::clamp(lerpF(vi.f1, dhF1, 0.14f), 200.0f, 1100.0f);
                prev.contextF2 = std::clamp(lerpF(vi.f2, dhF2, 0.14f), 600.0f, 2900.0f);
                prev.contextF3 = std::clamp(lerpF(vi.f3, dhF3, 0.14f), 1450.0f, 3600.0f);
                prev.contextEndF1 = vi.f1;
                prev.contextEndF2 = vi.f2;
                prev.contextEndF3 = vi.f3;
                prev.phoneticHint = buildPhoneticHint(prev);
            }

            // 2b. Cross-word nasal place assimilation: n + p/b/m -> m; n + k/g -> ng (EN & TR)
            if (!prev.codaCluster.empty() && std::strcmp(prev.codaCluster.back().label, "n") == 0 && !next.onsetCluster.empty()) {
                const char* nl = next.onsetCluster.front().label;
                if (std::strcmp(nl, "p") == 0 || std::strcmp(nl, "b") == 0 || std::strcmp(nl, "m") == 0) {
                    prev.codaCluster.back() = makeConsonant("m");
                    prev.coda = prev.codaCluster.back();
                    prev.phoneticHint = buildPhoneticHint(prev);
                } else if (std::strcmp(nl, "k") == 0 || std::strcmp(nl, "g") == 0) {
                    prev.codaCluster.back() = makeConsonant("ng");
                    prev.coda = prev.codaCluster.back();
                    prev.phoneticHint = buildPhoneticHint(prev);
                }
            }

            // 2c. EN intervocalic 't' flapping (e.g., "what a", "not a", "get it")
            if (!isTurkish && prev.codaCluster.size() == 1 &&
                std::strcmp(prev.codaCluster[0].label, "t") == 0 && next.onsetCluster.empty()) {
                ConsonantSpec& tc = prev.codaCluster[0];
                tc.type = C_PLOSIVE_VOICED;
                tc.closureMs = 8.0f;
                tc.burstOrFricMs = 7.0f;
                tc.transitionMs = 14.0f;
                tc.noiseGain = 0.20f;
                tc.aspirationGain = 0.0f;
                tc.murmurGain = 0.55f;
                tc.label[0] = 'd';
                tc.label[1] = '\0';
                prev.coda = tc;
                prev.phoneticHint = buildPhoneticHint(prev);
            }

            // 2d. EN palatalization: d/t/s + y -> j/ch/sh (e.g., "did you", "got you", "miss you")
            if (!isTurkish && !prev.codaCluster.empty() && !next.onsetCluster.empty() &&
                std::strcmp(next.onsetCluster.front().label, "y") == 0) {
                const char* cl = prev.codaCluster.back().label;
                if (std::strcmp(cl, "d") == 0) {
                    ConsonantSpec jc = makeConsonant("j");
                    jc.closureMs = 12.0f; jc.burstOrFricMs = 24.0f;
                    prev.codaCluster.back() = jc;
                    prev.coda = jc;
                    prev.phoneticHint = buildPhoneticHint(prev);
                } else if (std::strcmp(cl, "t") == 0) {
                    ConsonantSpec chc = makeConsonant("ch");
                    chc.closureMs = 14.0f; chc.burstOrFricMs = 24.0f;
                    prev.codaCluster.back() = chc;
                    prev.coda = chc;
                    prev.phoneticHint = buildPhoneticHint(prev);
                } else if (std::strcmp(cl, "s") == 0) {
                    ConsonantSpec shc = makeConsonant("sh");
                    shc.burstOrFricMs = 36.0f;
                    prev.codaCluster.back() = shc;
                    prev.coda = shc;
                    prev.phoneticHint = buildPhoneticHint(prev);
                }
            }

            // 2e. Turkish intervocalic resyllabification softening
            if (isTurkish && !prev.codaCluster.empty() && next.onsetCluster.empty()) {
                ConsonantSpec& pc = prev.codaCluster.back();
                pc.closureMs *= 0.75f;
                pc.burstOrFricMs *= 0.78f;
                prev.coda = pc;
            }

            // 2f. Cross-word coda spectral anticipation (lip rounding / front vowel of next word)
            if (!prev.codaCluster.empty()) {
                ConsonantSpec& pc = prev.codaCluster.back();
                VowelId nv = next.vowel1;
                if (nv == V_O || nv == V_OE || nv == V_U || nv == V_UE) {
                    pc.noiseFreq1Hz *= 0.89f;
                    pc.noiseFreq2Hz *= 0.89f;
                    prev.coda = pc;
                } else if (nv == V_I || nv == V_IH || nv == V_E) {
                    pc.noiseFreq1Hz *= 1.06f;
                    prev.coda = pc;
                }
            }

            // 2g. Cross-word V-to-V coarticulation (F1 & F2 only so rhotic F3 targets stay intact)
            float prevEndF1 = prev.contextEndF1;
            float prevEndF2 = prev.contextEndF2;
            float nextStF1  = next.contextF1;
            float nextStF2  = next.contextF2;
            prev.contextEndF1 = std::clamp(lerpF(prevEndF1, nextStF1, 0.11f), 200.0f, 1100.0f);
            prev.contextEndF2 = std::clamp(lerpF(prevEndF2, nextStF2, 0.11f), 600.0f, 2900.0f);
            next.contextF1    = std::clamp(lerpF(nextStF1, prevEndF1, 0.09f), 200.0f, 1100.0f);
            next.contextF2    = std::clamp(lerpF(nextStF2, prevEndF2, 0.09f), 600.0f, 2900.0f);
        }
    }

    // ==================== TURKISH PHONEMIC ENGINE ====================
    static bool isTurkishVowel(uint32_t cp) {
        return cp == 'a' || cp == 'e' || cp == 0x0131u || cp == 'i' ||
               cp == 'o' || cp == 0x00F6u || cp == 'u' || cp == 0x00FCu;
    }

    static VowelId mapTurkishVowel(uint32_t cp) {
        switch (cp) {
            case 'a': return V_A;
            case 'e': return V_E;
            case 0x0131u: return V_I_DOTLESS;
            case 'i': return V_I;
            case 'o': return V_O;
            case 0x00F6u: return V_OE;
            case 'u': return V_U;
            case 0x00FCu: return V_UE;
            default: return V_A;
        }
    }

    static ConsonantSpec mapTurkishConsonant(uint32_t cp) {
        switch (cp) {
            case 'b': return makeConsonant("b");
            case 'c': return makeConsonant("j");
            case 0x00E7u: return makeConsonant("ch");
            case 'd': return makeConsonant("d");
            case 'f': return makeConsonant("f");
            case 'g': return makeConsonant("g");
            case 0x011Fu: return makeConsonant("-");
            case 'h': return makeConsonant("h");
            case 'j': return makeConsonant("zh");
            case 'k': return makeConsonant("k");
            case 'l': return makeConsonant("l");
            case 'm': return makeConsonant("m");
            case 'n': return makeConsonant("n");
            case 'p': return makeConsonant("p");
            case 'r': return makeConsonant("r");
            case 's': return makeConsonant("s");
            case 0x015Fu: return makeConsonant("sh");
            case 't': return makeConsonant("t");
            case 'v': return makeConsonant("v");
            case 'y': return makeConsonant("y");
            case 'z': return makeConsonant("z");
            default: return makeConsonant("-");
        }
    }

    static bool isTurkishFrontVowel(uint32_t cp) {
        return cp == 'e' || cp == 'i' || cp == 0x00F6u || cp == 0x00FCu;
    }

    static std::vector<ConsonantSpec> collectTurkishCluster(const std::vector<uint32_t>& cps, bool frontHarmony) {
        std::vector<ConsonantSpec> out;
        for (uint32_t cp : cps) {
            ConsonantSpec c = mapTurkishConsonant(cp);
            if (c.type != C_NONE && out.size() < 3) {
                // Turkish palatal/velar harmony on k, g, l + alveolar tap r
                if (cp == 'k') {
                    if (frontHarmony) {
                        c.locusF2 = 2050.0f; c.noiseFreq1Hz = 2600.0f; c.noiseFreq2Hz = 4200.0f;
                    } else {
                        c.locusF2 = 1150.0f; c.noiseFreq1Hz = 1550.0f; c.noiseFreq2Hz = 2800.0f;
                    }
                } else if (cp == 'g') {
                    if (frontHarmony) {
                        c.locusF2 = 2000.0f; c.noiseFreq1Hz = 2350.0f; c.noiseFreq2Hz = 3600.0f;
                    } else {
                        c.locusF2 = 1120.0f; c.noiseFreq1Hz = 1400.0f; c.noiseFreq2Hz = 2400.0f;
                    }
                } else if (cp == 'l') {
                    if (frontHarmony) {
                        c.locusF2 = 1920.0f; c.locusF3 = 2900.0f;
                    } else {
                        c.locusF2 = 960.0f; c.locusF3 = 2650.0f;
                    }
                } else if (cp == 'r') {
                    c.closureMs = 0.0f; c.burstOrFricMs = 19.0f; c.transitionMs = 16.0f; c.murmurGain = 0.24f;
                    c.locusF3 = 2180.0f; // Turkish alveolar tap r (not retroflex)
                }

                if (out.empty() || std::strcmp(out.back().label, c.label) != 0) {
                    out.push_back(c);
                }
            }
        }
        return out;
    }

    static std::vector<SyllableToken> dissectTurkishWord(const std::vector<uint32_t>& word) {
        std::vector<size_t> vowelIdx;
        for (size_t i = 0; i < word.size(); ++i) {
            if (isTurkishVowel(word[i])) vowelIdx.push_back(i);
        }
        if (vowelIdx.empty()) {
            return { makeToken(toUpperUtf8(word, true), collectTurkishCluster(word, false), V_I_DOTLESS, V_I_DOTLESS, false, {}) };
        }

        std::vector<SyllableToken> out;
        size_t syllStart = 0;

        for (size_t k = 0; k < vowelIdx.size(); ++k) {
            size_t vPos = vowelIdx[k];
            size_t syllEnd = word.size();
            if (k + 1 < vowelIdx.size()) {
                size_t nextV = vowelIdx[k + 1];
                size_t consBetween = nextV - vPos - 1;
                syllEnd = (consBetween == 0) ? (vPos + 1) : (nextV - 1);
            }

            std::vector<uint32_t> rawCps(word.begin() + static_cast<ptrdiff_t>(syllStart),
                                         word.begin() + static_cast<ptrdiff_t>(syllEnd));
            std::vector<uint32_t> onsetCps(word.begin() + static_cast<ptrdiff_t>(syllStart),
                                           word.begin() + static_cast<ptrdiff_t>(vPos));
            std::vector<uint32_t> codaCps(word.begin() + static_cast<ptrdiff_t>(vPos + 1),
                                          word.begin() + static_cast<ptrdiff_t>(syllEnd));

            bool hasSoftGOnset = std::find(onsetCps.begin(), onsetCps.end(), 0x011Fu) != onsetCps.end();
            bool hasSoftGCoda  = std::find(codaCps.begin(), codaCps.end(), 0x011Fu) != codaCps.end();

            bool front = isTurkishFrontVowel(word[vPos]);
            std::vector<ConsonantSpec> onsetClus = collectTurkishCluster(onsetCps, front);
            VowelId v1 = mapTurkishVowel(word[vPos]);
            VowelId v2 = v1;
            bool diph = false;
            std::vector<ConsonantSpec> codaClus = collectTurkishCluster(codaCps, front);

            if (codaCps.size() == 1 && codaCps[0] == 'y') {
                v2 = (v1 == V_O || v1 == V_OE || v1 == V_UE) ? V_UE : V_I;
                diph = (v2 != v1);
                codaClus.clear();
            }

            if (hasSoftGCoda && !diph && (v1 == V_E || v1 == V_OE || v1 == V_UE)) {
                v2 = (v1 == V_OE || v1 == V_UE) ? V_UE : V_I;
                diph = (v2 != v1);
            }

            if (hasSoftGOnset && !out.empty()) {
                out.back().hasSoftGLink = true;
                VowelId prevV = out.back().vowel1;
                bool eitherFront = (prevV == V_E || prevV == V_I || prevV == V_OE || prevV == V_UE ||
                                    v1 == V_E || v1 == V_I || v1 == V_OE || v1 == V_UE);
                if (eitherFront && onsetClus.empty()) {
                    ConsonantSpec softY = makeConsonant("y");
                    softY.burstOrFricMs = 14.0f;
                    softY.transitionMs = 18.0f;
                    softY.murmurGain = 0.08f;
                    onsetClus.push_back(softY);
                }
            }

            SyllableToken tok = makeToken(toUpperUtf8(rawCps, true), onsetClus, v1, v2, diph, codaClus);
            if (hasSoftGCoda) tok.hasSoftGLink = true;
            out.push_back(std::move(tok));
            syllStart = syllEnd;
        }
        return out;
    }

    // ==================== ENGLISH PRONUNCIATION DICTIONARY & G2P ====================
    struct DictSyllable {
        const char* disp;
        const char* onset;
        VowelId v1;
        VowelId v2;
        bool diph;
        const char* coda;
    };

    struct DictWord {
        const char* word;
        int count;
        DictSyllable sylls[4];
    };

    static bool tryLookupEnglishBase(const std::string& w, std::vector<SyllableToken>& out) {
        static const DictWord kDict[] = {
            // /aI/ diphthongs: -ie, -ye, -y, -igh, i_e, y_e
            { "die",       1, { { "DIE",   "d",   V_A,  V_I, true,  "-"   } } },
            { "dye",       1, { { "DYE",   "d",   V_A,  V_I, true,  "-"   } } },
            { "dying",     2, { { "DY",    "d",   V_A,  V_I, true,  "-"   }, { "ING",  "-",   V_IH, V_IH,false, "ng"  } } },
            { "dyeing",    2, { { "DYE",   "d",   V_A,  V_I, true,  "-"   }, { "ING",  "-",   V_IH, V_IH,false, "ng"  } } },
            { "lie",       1, { { "LIE",   "l",   V_A,  V_I, true,  "-"   } } },
            { "lying",     2, { { "LY",    "l",   V_A,  V_I, true,  "-"   }, { "ING",  "-",   V_IH, V_IH,false, "ng"  } } },
            { "tie",       1, { { "TIE",   "t",   V_A,  V_I, true,  "-"   } } },
            { "tying",     2, { { "TY",    "t",   V_A,  V_I, true,  "-"   }, { "ING",  "-",   V_IH, V_IH,false, "ng"  } } },
            { "pie",       1, { { "PIE",   "p",   V_A,  V_I, true,  "-"   } } },
            { "vie",       1, { { "VIE",   "v",   V_A,  V_I, true,  "-"   } } },
            { "eye",       1, { { "EYE",   "-",   V_A,  V_I, true,  "-"   } } },
            { "eyes",      1, { { "EYES",  "-",   V_A,  V_I, true,  "z"   } } },
            { "bye",       1, { { "BYE",   "b",   V_A,  V_I, true,  "-"   } } },
            { "buy",       1, { { "BUY",   "b",   V_A,  V_I, true,  "-"   } } },
            { "guy",       1, { { "GUY",   "g",   V_A,  V_I, true,  "-"   } } },
            { "cry",       1, { { "CRY",   "k,r", V_A,  V_I, true,  "-"   } } },
            { "try",       1, { { "TRY",   "t,r", V_A,  V_I, true,  "-"   } } },
            { "dry",       1, { { "DRY",   "d,r", V_A,  V_I, true,  "-"   } } },
            { "fly",       1, { { "FLY",   "f,l", V_A,  V_I, true,  "-"   } } },
            { "sky",       1, { { "SKY",   "s,k", V_A,  V_I, true,  "-"   } } },
            { "why",       1, { { "WHY",   "w",   V_A,  V_I, true,  "-"   } } },
            { "by",        1, { { "BY",    "b",   V_A,  V_I, true,  "-"   } } },
            { "my",        1, { { "MY",    "m",   V_A,  V_I, true,  "-"   } } },
            { "shy",       1, { { "SHY",   "sh",  V_A,  V_I, true,  "-"   } } },
            { "type",      1, { { "TYPE",  "t",   V_A,  V_I, true,  "p"   } } },
            { "style",     1, { { "STYLE", "s,t", V_A,  V_I, true,  "l"   } } },
            { "rhyme",     1, { { "RHYME", "r",   V_A,  V_I, true,  "m"   } } },
            { "life",      1, { { "LIFE",  "l",   V_A,  V_I, true,  "f"   } } },
            { "lives",     1, { { "LIVES", "l",   V_A,  V_I, true,  "v,z" } } },
            { "alive",     2, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "LIVE", "l", V_A, V_I, true, "v" } } },
            { "survive",   2, { { "SUR",   "s",   V_OE, V_OE,false, "r"   }, { "VIVE", "v", V_A, V_I, true, "v" } } },
            { "drive",     1, { { "DRIVE", "d,r", V_A,  V_I, true,  "v"   } } },
            { "five",      1, { { "FIVE",  "f",   V_A,  V_I, true,  "v"   } } },
            { "side",      1, { { "SIDE",  "s",   V_A,  V_I, true,  "d"   } } },
            { "inside",    2, { { "IN",    "-",   V_IH, V_IH,false, "n"   }, { "SIDE", "s", V_A, V_I, true, "d" } } },
            { "outside",   2, { { "OUT",   "-",   V_A,  V_U, true,  "t"   }, { "SIDE", "s", V_A, V_I, true, "d" } } },
            { "wide",      1, { { "WIDE",  "w",   V_A,  V_I, true,  "d"   } } },
            { "ride",      1, { { "RIDE",  "r",   V_A,  V_I, true,  "d"   } } },
            { "hide",      1, { { "HIDE",  "h",   V_A,  V_I, true,  "d"   } } },
            { "slide",     1, { { "SLIDE", "s,l", V_A,  V_I, true,  "d"   } } },
            { "pride",     1, { { "PRIDE", "p,r", V_A,  V_I, true,  "d"   } } },
            { "bride",     1, { { "BRIDE", "b,r", V_A,  V_I, true,  "d"   } } },
            { "guide",     1, { { "GUIDE", "g",   V_A,  V_I, true,  "d"   } } },
            { "smile",     1, { { "SMILE", "s,m", V_A,  V_I, true,  "l"   } } },
            { "while",     1, { { "WHILE", "w",   V_A,  V_I, true,  "l"   } } },
            { "mile",      1, { { "MILE",  "m",   V_A,  V_I, true,  "l"   } } },
            { "file",      1, { { "FILE",  "f",   V_A,  V_I, true,  "l"   } } },
            { "line",      1, { { "LINE",  "l",   V_A,  V_I, true,  "n"   } } },
            { "fine",      1, { { "FINE",  "f",   V_A,  V_I, true,  "n"   } } },
            { "mine",      1, { { "MINE",  "m",   V_A,  V_I, true,  "n"   } } },
            { "nine",      1, { { "NINE",  "n",   V_A,  V_I, true,  "n"   } } },
            { "wine",      1, { { "WINE",  "w",   V_A,  V_I, true,  "n"   } } },
            { "shine",     1, { { "SHINE", "sh",  V_A,  V_I, true,  "n"   } } },
            { "sign",      1, { { "SIGN",  "s",   V_A,  V_I, true,  "n"   } } },
            { "design",    2, { { "DE",    "d",   V_IH, V_IH,false, "-"   }, { "SIGN", "z", V_A, V_I, true, "n" } } },
            { "time",      1, { { "TIME",  "t",   V_A,  V_I, true,  "m"   } } },
            { "crime",     1, { { "CRIME", "k,r", V_A,  V_I, true,  "m"   } } },
            { "fire",      1, { { "FIRE",  "f",   V_A,  V_I, true,  "r"   } } },
            { "wire",      1, { { "WIRE",  "w",   V_A,  V_I, true,  "r"   } } },
            { "desire",    2, { { "DE",    "d",   V_IH, V_IH,false, "-"   }, { "SIRE", "z", V_A, V_I, true, "r" } } },
            { "inspire",   2, { { "IN",    "-",   V_IH, V_IH,false, "n"   }, { "SPIRE","s,p", V_A, V_I, true, "r" } } },
            { "white",     1, { { "WHITE", "w",   V_A,  V_I, true,  "t"   } } },
            { "write",     1, { { "WRITE", "r",   V_A,  V_I, true,  "t"   } } },
            { "bite",      1, { { "BITE",  "b",   V_A,  V_I, true,  "t"   } } },
            { "kite",      1, { { "KITE",  "k",   V_A,  V_I, true,  "t"   } } },
            { "quite",     1, { { "QUITE", "k,w", V_A,  V_I, true,  "t"   } } },
            { "site",      1, { { "SITE",  "s",   V_A,  V_I, true,  "t"   } } },
            { "night",     1, { { "NIGHT", "n",   V_A,  V_I, true,  "t"   } } },
            { "light",     1, { { "LIGHT", "l",   V_A,  V_I, true,  "t"   } } },
            { "right",     1, { { "RIGHT", "r",   V_A,  V_I, true,  "t"   } } },
            { "fight",     1, { { "FIGHT", "f",   V_A,  V_I, true,  "t"   } } },
            { "sight",     1, { { "SIGHT", "s",   V_A,  V_I, true,  "t"   } } },
            { "bright",    1, { { "BRIGHT","b,r", V_A,  V_I, true,  "t"   } } },
            { "high",      1, { { "HIGH",  "h",   V_A,  V_I, true,  "-"   } } },
            { "mind",      1, { { "MIND",  "m",   V_A,  V_I, true,  "n,d" } } },
            { "find",      1, { { "FIND",  "f",   V_A,  V_I, true,  "n,d" } } },
            { "kind",      1, { { "KIND",  "k",   V_A,  V_I, true,  "n,d" } } },
            { "blind",     1, { { "BLIND", "b,l", V_A,  V_I, true,  "n,d" } } },
            { "wild",      1, { { "WILD",  "w",   V_A,  V_I, true,  "l,d" } } },
            { "child",     1, { { "CHILD", "ch",  V_A,  V_I, true,  "l,d" } } },
            { "like",      1, { { "LIKE",  "l",   V_A,  V_I, true,  "k"   } } },
            { "i",         1, { { "I",     "-",   V_A,  V_I, true,  "-"   } } },

            // Soft C / Soft G words
            { "face",      1, { { "FACE",  "f",   V_E,  V_I, true,  "s"   } } },
            { "place",     1, { { "PLACE", "p,l", V_E,  V_I, true,  "s"   } } },
            { "space",     1, { { "SPACE", "s,p", V_E,  V_I, true,  "s"   } } },
            { "race",      1, { { "RACE",  "r",   V_E,  V_I, true,  "s"   } } },
            { "grace",     1, { { "GRACE", "g,r", V_E,  V_I, true,  "s"   } } },
            { "ice",       1, { { "ICE",   "-",   V_A,  V_I, true,  "s"   } } },
            { "nice",      1, { { "NICE",  "n",   V_A,  V_I, true,  "s"   } } },
            { "price",     1, { { "PRICE", "p,r", V_A,  V_I, true,  "s"   } } },
            { "twice",     1, { { "TWICE", "t,w", V_A,  V_I, true,  "s"   } } },
            { "voice",     1, { { "VOICE", "v",   V_O,  V_I, true,  "s"   } } },
            { "choice",    1, { { "CHOICE","ch",  V_O,  V_I, true,  "s"   } } },
            { "peace",     1, { { "PEACE", "p",   V_I,  V_I, false, "s"   } } },
            { "dance",     1, { { "DANCE", "d",   V_AE, V_AE,false, "n,s" } } },
            { "chance",    1, { { "CHANCE","ch",  V_AE, V_AE,false, "n,s" } } },
            { "change",    1, { { "CHANGE","ch",  V_E,  V_I, true,  "n,j" } } },
            { "strange",   1, { { "STRANGE","s,t,r",V_E,V_I, true,  "n,j" } } },
            { "page",      1, { { "PAGE",  "p",   V_E,  V_I, true,  "j"   } } },
            { "stage",     1, { { "STAGE", "s,t", V_E,  V_I, true,  "j"   } } },
            { "age",       1, { { "AGE",   "-",   V_E,  V_I, true,  "j"   } } },
            { "bridge",    1, { { "BRIDGE","b,r", V_IH, V_IH,false, "j"   } } },
            { "edge",      1, { { "EDGE",  "-",   V_E,  V_E, false, "j"   } } },
            { "judge",     1, { { "JUDGE", "j",   V_A,  V_A,  false, "j"   } } },
            { "angel",     2, { { "AN",    "-",   V_E,  V_I, true,  "n"   }, { "GEL", "j", V_I_DOTLESS, V_I_DOTLESS, false, "l" } } },
            { "magic",     2, { { "MA",    "m",   V_AE, V_AE,false, "-"   }, { "GIC", "j", V_IH, V_IH, false, "k" } } },

            // /oU/ diphthongs & -old
            { "hello",     2, { { "HEL",   "h",   V_E,  V_E, false, "l"   }, { "LO",  "l", V_O, V_U, true, "-" } } },
            { "know",      1, { { "KNOW",  "n",   V_O,  V_U, true,  "-"   } } },
            { "known",     1, { { "KNOWN", "n",   V_O,  V_U, true,  "n"   } } },
            { "knew",      1, { { "KNEW",  "n",   V_U,  V_U, false, "-"   } } },
            { "show",      1, { { "SHOW",  "sh",  V_O,  V_U, true,  "-"   } } },
            { "slow",      1, { { "SLOW",  "s,l", V_O,  V_U, true,  "-"   } } },
            { "low",       1, { { "LOW",   "l",   V_O,  V_U, true,  "-"   } } },
            { "grow",      1, { { "GROW",  "g,r", V_O,  V_U, true,  "-"   } } },
            { "flow",      1, { { "FLOW",  "f,l", V_O,  V_U, true,  "-"   } } },
            { "glow",      1, { { "GLOW",  "g,l", V_O,  V_U, true,  "-"   } } },
            { "blow",      1, { { "BLOW",  "b,l", V_O,  V_U, true,  "-"   } } },
            { "snow",      1, { { "SNOW",  "s,n", V_O,  V_U, true,  "-"   } } },
            { "throw",     1, { { "THROW", "th,r",V_O,  V_U, true,  "-"   } } },
            { "own",       1, { { "OWN",   "-",   V_O,  V_U, true,  "n"   } } },
            { "alone",     2, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "LONE", "l", V_O, V_U, true, "n" } } },
            { "home",      1, { { "HOME",  "h",   V_O,  V_U, true,  "m"   } } },
            { "stone",     1, { { "STONE", "s,t", V_O,  V_U, true,  "n"   } } },
            { "bone",      1, { { "BONE",  "b",   V_O,  V_U, true,  "n"   } } },
            { "phone",     1, { { "PHONE", "f",   V_O,  V_U, true,  "n"   } } },
            { "tone",      1, { { "TONE",  "t",   V_O,  V_U, true,  "n"   } } },
            { "zone",      1, { { "ZONE",  "z",   V_O,  V_U, true,  "n"   } } },
            { "hope",      1, { { "HOPE",  "h",   V_O,  V_U, true,  "p"   } } },
            { "rope",      1, { { "ROPE",  "r",   V_O,  V_U, true,  "p"   } } },
            { "open",      2, { { "O",     "-",   V_O,  V_U, true,  "-"   }, { "PEN", "p", V_I_DOTLESS, V_I_DOTLESS, false, "n" } } },
            { "close",     1, { { "CLOSE", "k,l", V_O,  V_U, true,  "z"   } } },
            { "rose",      1, { { "ROSE",  "r",   V_O,  V_U, true,  "z"   } } },
            { "chose",     1, { { "CHOSE", "ch",  V_O,  V_U, true,  "z"   } } },
            { "nose",      1, { { "NOSE",  "n",   V_O,  V_U, true,  "z"   } } },
            { "those",     1, { { "THOSE", "dh",  V_O,  V_U, true,  "z"   } } },
            { "go",        1, { { "GO",    "g",   V_O,  V_U, true,  "-"   } } },
            { "goes",      1, { { "GOES",  "g",   V_O,  V_U, true,  "z"   } } },
            { "gone",      1, { { "GONE",  "g",   V_O,  V_O, false, "n"   } } },
            { "soul",      1, { { "SOUL",  "s",   V_O,  V_U, true,  "l"   } } },
            { "hold",      1, { { "HOLD",  "h",   V_O,  V_U, true,  "l,d" } } },
            { "cold",      1, { { "COLD",  "k",   V_O,  V_U, true,  "l,d" } } },
            { "gold",      1, { { "GOLD",  "g",   V_O,  V_U, true,  "l,d" } } },
            { "told",      1, { { "TOLD",  "t",   V_O,  V_U, true,  "l,d" } } },
            { "sold",      1, { { "SOLD",  "s",   V_O,  V_U, true,  "l,d" } } },
            { "old",       1, { { "OLD",   "-",   V_O,  V_U, true,  "l,d" } } },
            { "though",    1, { { "THOUGH","dh",  V_O,  V_U, true,  "-"   } } },

            // /aU/ diphthongs (now, out, sound, power...)
            { "now",       1, { { "NOW",   "n",   V_A,  V_U, true,  "-"   } } },
            { "how",       1, { { "HOW",   "h",   V_A,  V_U, true,  "-"   } } },
            { "down",      1, { { "DOWN",  "d",   V_A,  V_U, true,  "n"   } } },
            { "town",      1, { { "TOWN",  "t",   V_A,  V_U, true,  "n"   } } },
            { "brown",     1, { { "BROWN", "b,r", V_A,  V_U, true,  "n"   } } },
            { "crown",     1, { { "CROWN", "k,r", V_A,  V_U, true,  "n"   } } },
            { "out",       1, { { "OUT",   "-",   V_A,  V_U, true,  "t"   } } },
            { "about",     2, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "BOUT", "b", V_A, V_U, true, "t" } } },
            { "shout",     1, { { "SHOUT", "sh",  V_A,  V_U, true,  "t"   } } },
            { "doubt",     1, { { "DOUBT", "d",   V_A,  V_U, true,  "t"   } } },
            { "around",    2, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "ROUND", "r", V_A, V_U, true, "n,d" } } },
            { "sound",     1, { { "SOUND", "s",   V_A,  V_U, true,  "n,d" } } },
            { "ground",    1, { { "GROUND","g,r", V_A,  V_U, true,  "n,d" } } },
            { "found",     1, { { "FOUND", "f",   V_A,  V_U, true,  "n,d" } } },
            { "round",     1, { { "ROUND", "r",   V_A,  V_U, true,  "n,d" } } },
            { "house",     1, { { "HOUSE", "h",   V_A,  V_U, true,  "s"   } } },
            { "mouth",     1, { { "MOUTH", "m",   V_A,  V_U, true,  "th"  } } },
            { "south",     1, { { "SOUTH", "s",   V_A,  V_U, true,  "th"  } } },
            { "cloud",     1, { { "CLOUD", "k,l", V_A,  V_U, true,  "d"   } } },
            { "proud",     1, { { "PROUD", "p,r", V_A,  V_U, true,  "d"   } } },
            { "loud",      1, { { "LOUD",  "l",   V_A,  V_U, true,  "d"   } } },
            { "count",     1, { { "COUNT", "k",   V_A,  V_U, true,  "n,t" } } },
            { "mountain",  2, { { "MOUN",  "m",   V_A,  V_U, true,  "n"   }, { "TAIN", "t", V_IH, V_IH, false, "n" } } },
            { "our",       1, { { "OUR",   "-",   V_A,  V_U, true,  "r"   } } },
            { "hour",      1, { { "HOUR",  "-",   V_A,  V_U, true,  "r"   } } },
            { "power",     2, { { "POW",   "p",   V_A,  V_U, true,  "-"   }, { "ER",   "-", V_OE, V_OE, false, "r" } } },
            { "flower",    2, { { "FLOW",  "f,l", V_A,  V_U, true,  "-"   }, { "ER",   "-", V_OE, V_OE, false, "r" } } },
            { "tower",     2, { { "TOW",   "t",   V_A,  V_U, true,  "-"   }, { "ER",   "-", V_OE, V_OE, false, "r" } } },

            // Core words, irregulars, R-vowels, and music/singing vocabulary
            { "world",     1, { { "WORLD", "w",   V_OE, V_OE,false, "l,d" } } },
            { "words",     1, { { "WORDS", "w",   V_OE, V_OE,false, "r,d,z"} } },
            { "word",      1, { { "WORD",  "w",   V_OE, V_OE,false, "r,d" } } },
            { "work",      1, { { "WORK",  "w",   V_OE, V_OE,false, "r,k" } } },
            { "bird",      1, { { "BIRD",  "b",   V_OE, V_OE,false, "r,d" } } },
            { "first",     1, { { "FIRST", "f",   V_OE, V_OE,false, "s,t" } } },
            { "girl",      1, { { "GIRL",  "g",   V_OE, V_OE,false, "r,l" } } },
            { "turn",      1, { { "TURN",  "t",   V_OE, V_OE,false, "r,n" } } },
            { "burn",      1, { { "BURN",  "b",   V_OE, V_OE,false, "r,n" } } },
            { "hurt",      1, { { "HURT",  "h",   V_OE, V_OE,false, "r,t" } } },
            { "earth",     1, { { "EARTH", "-",   V_OE, V_OE,false, "r,th"} } },
            { "heart",     1, { { "HEART", "h",   V_A,  V_A, false, "r,t" } } },
            { "learn",     1, { { "LEARN", "l",   V_OE, V_OE,false, "r,n" } } },
            { "heard",     1, { { "HEARD", "h",   V_OE, V_OE,false, "r,d" } } },
            { "early",     2, { { "EAR",   "-",   V_OE, V_OE,false, "r"   }, { "LY",  "l", V_I, V_I, false, "-" } } },
            { "you",       1, { { "YOU",   "y",   V_U,  V_U, false, "-"   } } },
            { "your",      1, { { "YOUR",  "y",   V_O,  V_O, false, "r"   } } },
            { "are",       1, { { "ARE",   "-",   V_A,  V_A, false, "r"   } } },
            { "the",       1, { { "THE",   "dh",  V_I_DOTLESS, V_I_DOTLESS, false, "-" } } },
            { "a",         1, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" } } },
            { "one",       1, { { "ONE",   "w",   V_A,  V_A, false, "n"   } } },
            { "two",       1, { { "TWO",   "t",   V_U,  V_U, false, "-"   } } },
            { "three",     1, { { "THREE", "th,r",V_I,  V_I, false, "-"   } } },
            { "four",      1, { { "FOUR",  "f",   V_O,  V_O, false, "r"   } } },
            { "love",      1, { { "LOVE",  "l",   V_A,  V_A,  false, "v" } } },
            { "come",      1, { { "COME",  "k",   V_A,  V_A,  false, "m" } } },
            { "some",      1, { { "SOME",  "s",   V_A,  V_A,  false, "m" } } },
            { "done",      1, { { "DONE",  "d",   V_A,  V_A,  false, "n" } } },
            { "does",      1, { { "DOES",  "d",   V_A,  V_A,  false, "z" } } },
            { "do",        1, { { "DO",    "d",   V_U,  V_U, false, "-"   } } },
            { "to",        1, { { "TO",    "t",   V_U,  V_U, false, "-"   } } },
            { "who",       1, { { "WHO",   "h",   V_U,  V_U, false, "-"   } } },
            { "into",      2, { { "IN",    "-",   V_IH, V_IH,false, "n"   }, { "TO", "t", V_U, V_U, false, "-" } } },
            { "have",      1, { { "HAVE",  "h",   V_AE, V_AE,false, "v"   } } },
            { "has",       1, { { "HAS",   "h",   V_AE, V_AE,false, "z"   } } },
            { "there",     1, { { "THERE", "dh",  V_E,  V_E, false, "r"   } } },
            { "where",     1, { { "WHERE", "w",   V_E,  V_E, false, "r"   } } },
            { "here",      1, { { "HERE",  "h",   V_IH, V_IH,false, "r"   } } },
            { "they",      1, { { "THEY",  "dh",  V_E,  V_I, true,  "-"   } } },
            { "them",      1, { { "THEM",  "dh",  V_E,  V_E, false, "m"   } } },
            { "their",     1, { { "THEIR", "dh",  V_E,  V_E, false, "r"   } } },
            { "this",      1, { { "THIS",  "dh",  V_IH, V_IH,false, "s"   } } },
            { "that",      1, { { "THAT",  "dh",  V_AE, V_AE,false, "t"   } } },
            { "what",      1, { { "WHAT",  "w",   V_A,  V_A, false, "t"   } } },
            { "when",      1, { { "WHEN",  "w",   V_E,  V_E, false, "n"   } } },
            { "with",      1, { { "WITH",  "w",   V_IH, V_IH,false, "th"  } } },
            { "from",      1, { { "FROM",  "f,r", V_A,  V_A, false, "m"   } } },
            { "through",   1, { { "THROUGH","th,r",V_U, V_U, false, "-"   } } },
            { "thought",   1, { { "THOUGHT","th", V_O,  V_O, false, "t"   } } },
            { "water",     2, { { "WA",    "w",   V_O,  V_O, false, "-"   }, { "TER", "t", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "father",    2, { { "FA",    "f",   V_A,  V_A, false, "-"   }, { "THER","dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "mother",    2, { { "MO",    "m",   V_A,  V_A, false, "-"   }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "brother",   2, { { "BRO",   "b,r", V_A,  V_A, false, "-"   }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "sister",    2, { { "SIS",   "s",   V_IH, V_IH,false, "s"   }, { "TER", "t", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "other",     2, { { "O",     "-",   V_A,  V_A, false, "-"   }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "another",   3, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "NO", "n", V_A, V_A, false, "-" }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "fuck",      1, { { "FUCK",  "f",   V_A,  V_A, false, "k"   } } },
            { "fucker",    2, { { "FUC",   "f",   V_A,  V_A, false, "-"   }, { "KER",  "k", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "fucking",   2, { { "FUC",   "f",   V_A,  V_A, false, "-"   }, { "KING", "k", V_IH, V_IH, false, "ng" } } },
            { "motherfucker", 4, { { "MO", "m",   V_A,  V_A, false, "-"   }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" }, { "FUC", "f", V_A, V_A, false, "-" }, { "KER", "k", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "motherfucking",4, { { "MO", "m",   V_A,  V_A, false, "-"   }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" }, { "FUC", "f", V_A, V_A, false, "-" }, { "KING", "k", V_IH, V_IH, false, "ng" } } },
            { "money",     2, { { "MO",    "m",   V_A,  V_A, false, "-"   }, { "NEY",  "n", V_I, V_I, false, "-" } } },
            { "honey",     2, { { "HO",    "h",   V_A,  V_A, false, "-"   }, { "NEY",  "n", V_I, V_I, false, "-" } } },
            { "cover",     2, { { "CO",    "k",   V_A,  V_A, false, "-"   }, { "VER",  "v", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "color",     2, { { "CO",    "k",   V_A,  V_A, false, "-"   }, { "LOR",  "l", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "wonder",    2, { { "WON",   "w",   V_A,  V_A, false, "n"   }, { "DER",  "d", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "together",  3, { { "TO",    "t",   V_U,  V_U, false, "-"   }, { "GE", "g", V_E, V_E, false, "-" }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "forever",   3, { { "FOR",   "f",   V_O,  V_O, false, "r"   }, { "E", "-", V_E, V_E, false, "-" }, { "VER", "v", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "never",     2, { { "NE",    "n",   V_E,  V_E, false, "-"   }, { "VER", "v", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "ever",      2, { { "E",     "-",   V_E,  V_E, false, "-"   }, { "VER", "v", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "over",      2, { { "O",     "-",   V_O,  V_U, true,  "-"   }, { "VER", "v", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "under",     2, { { "UN",    "-",   V_A,  V_A, false, "n"   }, { "DER", "d", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "after",     2, { { "AF",    "-",   V_AE, V_AE,false, "f"   }, { "TER", "t", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "before",    2, { { "BE",    "b",   V_IH, V_IH,false, "-"   }, { "FORE","f", V_O, V_O, false, "r" } } },
            { "again",     2, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "GAIN", "g", V_E, V_E, false, "n" } } },
            { "more",      1, { { "MORE",  "m",   V_O,  V_O, false, "r"   } } },
            { "door",      1, { { "DOOR",  "d",   V_O,  V_O, false, "r"   } } },
            { "floor",     1, { { "FLOOR", "f,l", V_O,  V_O, false, "r"   } } },
            { "head",      1, { { "HEAD",  "h",   V_E,  V_E, false, "d"   } } },
            { "dead",      1, { { "DEAD",  "d",   V_E,  V_E, false, "d"   } } },
            { "bread",     1, { { "BREAD", "b,r", V_E,  V_E, false, "d"   } } },
            { "read",      1, { { "READ",  "r",   V_I,  V_I, false, "d"   } } },
            { "ready",     2, { { "REA",   "r",   V_E,  V_E, false, "-"   }, { "DY", "d", V_I, V_I, false, "-" } } },
            { "heavy",     2, { { "HEA",   "h",   V_E,  V_E, false, "-"   }, { "VY", "v", V_I, V_I, false, "-" } } },
            { "weather",   2, { { "WEA",   "w",   V_E,  V_E, false, "-"   }, { "THER", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "r" } } },
            { "breath",    1, { { "BREATH","b,r", V_E,  V_E, false, "th"  } } },
            { "death",     1, { { "DEATH", "d",   V_E,  V_E, false, "th"  } } },
            { "heaven",    2, { { "HEA",   "h",   V_E,  V_E, false, "-"   }, { "VEN", "v", V_I_DOTLESS, V_I_DOTLESS, false, "n" } } },
            { "great",     1, { { "GREAT", "g,r", V_E,  V_I, true,  "t"   } } },
            { "break",     1, { { "BREAK", "b,r", V_E,  V_I, true,  "k"   } } },
            { "speak",     1, { { "SPEAK", "s,p", V_I,  V_I, false, "k"   } } },
            { "dream",     1, { { "DREAM", "d,r", V_I,  V_I, false, "m"   } } },
            { "feel",      1, { { "FEEL",  "f",   V_I,  V_I, false, "l"   } } },
            { "see",       1, { { "SEE",   "s",   V_I,  V_I, false, "-"   } } },
            { "free",      1, { { "FREE",  "f,r", V_I,  V_I, false, "-"   } } },
            { "tree",      1, { { "TREE",  "t,r", V_I,  V_I, false, "-"   } } },
            { "deep",      1, { { "DEEP",  "d",   V_I,  V_I, false, "p"   } } },
            { "key",       1, { { "KEY",   "k",   V_I,  V_I, false, "-"   } } },
            { "keep",      1, { { "KEEP",  "k",   V_I,  V_I, false, "p"   } } },
            { "sleep",     1, { { "SLEEP", "s,l", V_I,  V_I, false, "p"   } } },
            { "sweet",     1, { { "SWEET", "s,w", V_I,  V_I, false, "t"   } } },
            { "meet",      1, { { "MEET",  "m",   V_I,  V_I, false, "t"   } } },
            { "street",    1, { { "STREET","s,t,r",V_I, V_I, false, "t"   } } },
            { "beat",      1, { { "BEAT",  "b",   V_I,  V_I, false, "t"   } } },
            { "heat",      1, { { "HEAT",  "h",   V_I,  V_I, false, "t"   } } },
            { "mean",      1, { { "MEAN",  "m",   V_I,  V_I, false, "n"   } } },
            { "clean",     1, { { "CLEAN", "k,l", V_I,  V_I, false, "n"   } } },
            { "leave",     1, { { "LEAVE", "l",   V_I,  V_I, false, "v"   } } },
            { "believe",   2, { { "BE",    "b",   V_IH, V_IH,false, "-"   }, { "LIEVE", "l", V_I, V_I, false, "v" } } },
            { "receive",   2, { { "RE",    "r",   V_IH, V_IH,false, "-"   }, { "CEIVE", "s", V_I, V_I, false, "v" } } },
            { "people",    2, { { "PEO",   "p",   V_I,  V_I, false, "-"   }, { "PLE", "p", V_I_DOTLESS, V_I_DOTLESS, false, "l" } } },
            { "little",    2, { { "LIT",   "l",   V_IH, V_IH,false, "t"   }, { "TLE", "t", V_I_DOTLESS, V_I_DOTLESS, false, "l" } } },
            { "music",     2, { { "MU",    "m,y", V_U,  V_U, false, "-"   }, { "SIC", "z", V_IH, V_IH, false, "k" } } },
            { "human",     2, { { "HU",    "h,y", V_U,  V_U, false, "-"   }, { "MAN", "m", V_I_DOTLESS, V_I_DOTLESS, false, "n" } } },
            { "machine",   2, { { "MA",    "m",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "CHINE", "sh", V_I, V_I, false, "n" } } },
            { "robot",     2, { { "RO",    "r",   V_O,  V_U, true,  "-"   }, { "BOT", "b", V_A, V_A, false, "t" } } },
            { "electric",  3, { { "E",     "-",   V_IH, V_IH,false, "-"   }, { "LEC", "l", V_E, V_E, false, "k" }, { "TRIC", "t,r", V_IH, V_IH, false, "k" } } },
            { "digital",   3, { { "DI",    "d",   V_IH, V_IH,false, "-"   }, { "GI", "j", V_IH, V_IH, false, "-" }, { "TAL", "t", V_I_DOTLESS, V_I_DOTLESS, false, "l" } } },
            { "sing",      1, { { "SING",  "s",   V_IH, V_IH,false, "ng"  } } },
            { "singing",   2, { { "SING",  "s",   V_IH, V_IH,false, "ng"  }, { "ING", "-", V_IH, V_IH, false, "ng" } } },
            { "song",      1, { { "SONG",  "s",   V_O,  V_O, false, "ng"  } } },
            { "vocal",     2, { { "VO",    "v",   V_O,  V_U, true,  "-"   }, { "CAL", "k", V_I_DOTLESS, V_I_DOTLESS, false, "l" } } },
            { "melody",    3, { { "ME",    "m",   V_E,  V_E, false, "-"   }, { "LO", "l", V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "DY", "d", V_I, V_I, false, "-" } } },
            { "harmony",   3, { { "HAR",   "h",   V_A,  V_A, false, "r"   }, { "MO", "m", V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "NY", "n", V_I, V_I, false, "-" } } },
            { "rhythm",    2, { { "RHY",   "r",   V_IH, V_IH,false, "-"   }, { "THM", "dh", V_I_DOTLESS, V_I_DOTLESS, false, "m" } } },
            { "guitar",    2, { { "GUI",   "g",   V_IH, V_IH,false, "-"   }, { "TAR", "t", V_A, V_A, false, "r" } } },
            { "piano",     3, { { "PI",    "p",   V_IH, V_IH,false, "-"   }, { "AN", "-", V_AE, V_AE, false, "n" }, { "O", "-", V_O, V_U, true, "-" } } },
            { "violin",    3, { { "VI",    "v",   V_A,  V_I, true,  "-"   }, { "O", "-", V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "LIN", "l", V_IH, V_IH, false, "n" } } },
            { "drum",      1, { { "DRUM",  "d,r", V_A,  V_A, false, "m" } } },
            { "bass",      1, { { "BASS",  "b",   V_E,  V_I, true,  "s"   } } },
            { "synth",     1, { { "SYNTH", "s",   V_IH, V_IH,false, "n,th"} } },
            { "get",       1, { { "GET",   "g",   V_E,  V_E, false, "t"   } } },
            { "give",      1, { { "GIVE",  "g",   V_IH, V_IH,false, "v"   } } },
            { "begin",     2, { { "BE",    "b",   V_IH, V_IH,false, "-"   }, { "GIN", "g", V_IH, V_IH, false, "n" } } },
            { "good",      1, { { "GOOD",  "g",   V_U,  V_U, false, "d"   } } },
            { "beauty",    2, { { "BEAU",  "b,y", V_U,  V_U, false, "-"   }, { "TY", "t", V_I, V_I, false, "-" } } },
            { "beautiful", 3, { { "BEAU",  "b,y", V_U,  V_U, false, "-"   }, { "TI", "t", V_IH, V_IH, false, "-" }, { "FUL", "f", V_U, V_U, false, "l" } } },
            { "everything",3, { { "EV",    "-",   V_E,  V_E, false, "v"   }, { "RY", "r", V_IH, V_IH, false, "-" }, { "THING", "th", V_IH, V_IH, false, "ng" } } },
            { "nothing",   2, { { "NO",    "n",   V_A,  V_A, false, "-"   }, { "THING", "th", V_IH, V_IH, false, "ng" } } },
            { "something", 2, { { "SOME",  "s",   V_A,  V_A, false, "m"   }, { "THING", "th", V_IH, V_IH, false, "ng" } } },
            { "someone",   2, { { "SOME",  "s",   V_A,  V_A, false, "m"   }, { "ONE", "w", V_A, V_A, false, "n" } } },
            { "always",    2, { { "AL",    "-",   V_O,  V_O, false, "l"   }, { "WAYS", "w", V_E, V_I, true, "z" } } },
            { "today",     2, { { "TO",    "t",   V_U,  V_U, false, "-"   }, { "DAY", "d", V_E, V_I, true, "-" } } },
            { "tonight",   2, { { "TO",    "t",   V_U,  V_U, false, "-"   }, { "NIGHT","n", V_A, V_I, true, "t" } } },
            { "make",      1, { { "MAKE",  "m",   V_E,  V_I, true,  "k"   } } },
            { "take",      1, { { "TAKE",  "t",   V_E,  V_I, true,  "k"   } } },
            { "play",      1, { { "PLAY",  "p,l", V_E,  V_I, true,  "-"   } } },
            { "say",       1, { { "SAY",   "s",   V_E,  V_I, true,  "-"   } } },
            { "day",       1, { { "DAY",   "d",   V_E,  V_I, true,  "-"   } } },
            { "way",       1, { { "WAY",   "w",   V_E,  V_I, true,  "-"   } } },
            { "stay",      1, { { "STAY",  "s,t", V_E,  V_I, true,  "-"   } } },
            { "away",      2, { { "A",     "-",   V_I_DOTLESS, V_I_DOTLESS, false, "-" }, { "WAY", "w", V_E, V_I, true, "-" } } },
            { "me",        1, { { "ME",    "m",   V_I,  V_I, false, "-"   } } },
            { "we",        1, { { "WE",    "w",   V_I,  V_I, false, "-"   } } },
            { "he",        1, { { "HE",    "h",   V_I,  V_I, false, "-"   } } },
            { "she",       1, { { "SHE",   "sh",  V_I,  V_I, false, "-"   } } },
            { "be",        1, { { "BE",    "b",   V_I,  V_I, false, "-"   } } },
            { "want",      1, { { "WANT",  "w",   V_A,  V_A, false, "n,t" } } },
            { "need",      1, { { "NEED",  "n",   V_I,  V_I, false, "d"   } } },
            { "said",      1, { { "SAID",  "s",   V_E,  V_E, false, "d"   } } },
            { "says",      1, { { "SAYS",  "s",   V_E,  V_E, false, "z"   } } },
            { "could",     1, { { "COULD", "k",   V_U,  V_U, false, "d"   } } },
            { "would",     1, { { "WOULD", "w",   V_U,  V_U, false, "d"   } } },
            { "should",    1, { { "SHOULD","sh",  V_U,  V_U, false, "d"   } } },
            { "just",      1, { { "JUST",  "j",   V_A,  V_A, false, "s,t" } } },
            { "new",       1, { { "NEW",   "n",   V_U,  V_U, false, "-"   } } },
            { "true",      1, { { "TRUE",  "t,r", V_U,  V_U, false, "-"   } } },
            { "blue",      1, { { "BLUE",  "b,l", V_U,  V_U, false, "-"   } } },
            { "pure",      1, { { "PURE",  "p,y", V_U,  V_U, false, "r"   } } },
            { "sure",      1, { { "SURE",  "sh",  V_U,  V_U, false, "r"   } } },
            { "yeah",      1, { { "YEAH",  "y",   V_AE, V_AE,false, "-"   } } },
            { "talk",      1, { { "TALK",  "t",   V_O,  V_O, false, "k"   } } },
            { "walk",      1, { { "WALK",  "w",   V_O,  V_O, false, "k"   } } },
            { "listen",    2, { { "LIS",   "l",   V_IH, V_IH,false, "s"   }, { "TEN", "-", V_I_DOTLESS, V_I_DOTLESS, false, "n" } } },
            { "hear",      1, { { "HEAR",  "h",   V_IH, V_IH,false, "r"   } } },
            { "clear",     1, { { "CLEAR", "k,l", V_IH, V_IH,false, "r"   } } },
            { "understand",3, { { "UN",    "-",   V_A,  V_A, false, "n"   }, { "DER", "d", V_I_DOTLESS, V_I_DOTLESS, false, "r" }, { "STAND", "s,t", V_AE, V_AE, false, "n,d" } } },
            { "apply",     2, { { "AP",    "-",   V_I_DOTLESS, V_I_DOTLESS, false, "p" }, { "PLY", "p,l", V_A, V_I, true, "-" } } },
            { "reply",     2, { { "RE",    "r",   V_IH, V_IH,false, "-"   }, { "PLY", "p,l", V_A, V_I, true, "-" } } },
            { "supply",    2, { { "SUP",   "s",   V_A,  V_A, false, "p"   }, { "PLY", "p,l", V_A, V_I, true, "-" } } },
            { "deny",      2, { { "DE",    "d",   V_IH, V_IH,false, "-"   }, { "NY",  "n", V_A, V_I, true, "-" } } },
            { "defy",      2, { { "DE",    "d",   V_IH, V_IH,false, "-"   }, { "FY",  "f", V_A, V_I, true, "-" } } }
        };

        for (const auto& entry : kDict) {
            if (w == entry.word) {
                for (int i = 0; i < entry.count; ++i) {
                    const auto& s = entry.sylls[i];
                    out.push_back(makeToken(s.disp, parseConsonantKeys(s.onset), s.v1, s.v2, s.diph, parseConsonantKeys(s.coda)));
                }
                return true;
            }
        }
        if (const LexWord* lw = findEnglishLexiconWord(w)) {
            for (int i = 0; i < lw->count; ++i) {
                const auto& s = lw->sylls[i];
                SyllableToken tok = makeToken(s.disp, parseConsonantKeys(s.onset), s.v1, s.v2, s.diph, parseConsonantKeys(s.coda));
                tok.explicitStress = (i == lw->stressedIdx) ? 1 : 0;
                out.push_back(std::move(tok));
            }
            return true;
        }
        return false;
    }

    static bool tryLookupEnglishDict(const std::string& w, std::vector<SyllableToken>& out) {
        if (tryLookupEnglishBase(w, out)) return true;

        auto appendCodaToLast = [&](const char* suffixDisp, const char* cUnvoiced, const char* cVoiced) {
            if (out.empty()) return;
            auto& last = out.back();
            last.display += suffixDisp;
            if (last.codaCluster.size() < 3) {
                bool unvoiced = !last.codaCluster.empty() &&
                    (last.codaCluster.back().type == C_PLOSIVE_UNVOICED || last.codaCluster.back().type == C_FRICATIVE_UNVOICED);
                last.codaCluster.push_back(makeConsonant(unvoiced ? cUnvoiced : cVoiced));
                last.coda = last.codaCluster.back();
                last.phoneticHint += last.coda.label;
            }
        };

        // -ies (cries -> cry + z, skies -> sky + z)
        if (w.size() > 4 && w.compare(w.size() - 3, 3, "ies") == 0) {
            std::string stem = w.substr(0, w.size() - 3) + "y";
            if (tryLookupEnglishBase(stem, out)) {
                if (!out.empty()) {
                    out.back().display = toUpperAscii(w);
                    appendCodaToLast("", "z", "z");
                }
                return true;
            }
        }
        // -ied (cried -> cry + d, tried -> try + d)
        if (w.size() > 4 && w.compare(w.size() - 3, 3, "ied") == 0) {
            std::string stem = w.substr(0, w.size() - 3) + "y";
            if (tryLookupEnglishBase(stem, out)) {
                if (!out.empty()) {
                    out.back().display = toUpperAscii(w);
                    appendCodaToLast("", "d", "d");
                }
                return true;
            }
        }
        // -es
        if (w.size() > 4 && w.compare(w.size() - 2, 2, "es") == 0) {
            std::string stem = w.substr(0, w.size() - 2);
            if (tryLookupEnglishBase(stem, out)) {
                appendCodaToLast("ES", "s", "z");
                return true;
            }
            std::string stemE = w.substr(0, w.size() - 1);
            if (tryLookupEnglishBase(stemE, out)) {
                appendCodaToLast("S", "s", "z");
                return true;
            }
        }
        // -s
        if (w.size() > 3 && w.back() == 's' && w[w.size() - 2] != 's') {
            std::string stem = w.substr(0, w.size() - 1);
            if (tryLookupEnglishBase(stem, out)) {
                appendCodaToLast("S", "s", "z");
                return true;
            }
        }
        // -ed
        if (w.size() > 4 && w.compare(w.size() - 2, 2, "ed") == 0) {
            std::string stem = w.substr(0, w.size() - 2);
            if (tryLookupEnglishBase(stem, out)) {
                appendCodaToLast("ED", "t", "d");
                return true;
            }
            std::string stemE = w.substr(0, w.size() - 1);
            if (tryLookupEnglishBase(stemE, out)) {
                appendCodaToLast("D", "t", "d");
                return true;
            }
        }
        // -ing
        if (w.size() > 5 && w.compare(w.size() - 3, 3, "ing") == 0) {
            std::string stem = w.substr(0, w.size() - 3);
            if (tryLookupEnglishBase(stem, out) || tryLookupEnglishBase(stem + "e", out)) {
                out.push_back(makeToken("ING", {}, V_IH, V_IH, false, { makeConsonant("ng") }));
                return true;
            }
        }
        // -er
        if (w.size() > 4 && w.compare(w.size() - 2, 2, "er") == 0) {
            std::string stem = w.substr(0, w.size() - 2);
            if (tryLookupEnglishBase(stem, out) || tryLookupEnglishBase(stem + "e", out)) {
                out.push_back(makeToken("ER", {}, V_I_DOTLESS, V_I_DOTLESS, false, { makeConsonant("r") }));
                return true;
            }
        }
        // -ly
        if (w.size() > 4 && w.compare(w.size() - 2, 2, "ly") == 0) {
            std::string stem = w.substr(0, w.size() - 2);
            if (tryLookupEnglishBase(stem, out)) {
                out.push_back(makeToken("LY", { makeConsonant("l") }, V_I, V_I, false, {}));
                return true;
            }
        }

        // 2-part compound decomposition (e.g. step-mother, mother-fucker, every-one, over-time)
        if (w.size() >= 6) {
            auto lookupRightPart = [&](const std::string& r, std::vector<SyllableToken>& rOut) -> bool {
                if (tryLookupEnglishBase(r, rOut)) return true;
                if (r.size() > 3 && r.back() == 's' && r[r.size() - 2] != 's' &&
                    tryLookupEnglishBase(r.substr(0, r.size() - 1), rOut) && !rOut.empty()) {
                    rOut.back().display += "S";
                    bool unv = !rOut.back().codaCluster.empty() &&
                               (rOut.back().codaCluster.back().type == C_PLOSIVE_UNVOICED ||
                                rOut.back().codaCluster.back().type == C_FRICATIVE_UNVOICED);
                    if (rOut.back().codaCluster.size() < 3) {
                        rOut.back().codaCluster.push_back(makeConsonant(unv ? "s" : "z"));
                        rOut.back().coda = rOut.back().codaCluster.back();
                    }
                    return true;
                }
                if (r.size() > 4 && r.compare(r.size() - 2, 2, "er") == 0) {
                    std::string rStem = r.substr(0, r.size() - 2);
                    if (tryLookupEnglishBase(rStem, rOut) || tryLookupEnglishBase(rStem + "e", rOut)) {
                        rOut.push_back(makeToken("ER", {}, V_I_DOTLESS, V_I_DOTLESS, false, { makeConsonant("r") }));
                        return true;
                    }
                }
                if (r.size() > 5 && r.compare(r.size() - 3, 3, "ing") == 0) {
                    std::string rStem = r.substr(0, r.size() - 3);
                    if (tryLookupEnglishBase(rStem, rOut) || tryLookupEnglishBase(rStem + "e", rOut)) {
                        rOut.push_back(makeToken("ING", {}, V_IH, V_IH, false, { makeConsonant("ng") }));
                        return true;
                    }
                }
                if (r.size() > 4 && r.compare(r.size() - 2, 2, "ed") == 0) {
                    std::string rStem = r.substr(0, r.size() - 2);
                    if ((tryLookupEnglishBase(rStem, rOut) || tryLookupEnglishBase(rStem + "e", rOut)) && !rOut.empty()) {
                        rOut.back().display += "ED";
                        bool unv = !rOut.back().codaCluster.empty() &&
                                   (rOut.back().codaCluster.back().type == C_PLOSIVE_UNVOICED ||
                                    rOut.back().codaCluster.back().type == C_FRICATIVE_UNVOICED);
                        if (rOut.back().codaCluster.size() < 3) {
                            rOut.back().codaCluster.push_back(makeConsonant(unv ? "t" : "d"));
                            rOut.back().coda = rOut.back().codaCluster.back();
                        }
                        return true;
                    }
                }
                return false;
            };

            for (size_t split = 3; split + 3 <= w.size(); ++split) {
                std::vector<SyllableToken> leftOut, rightOut;
                if (tryLookupEnglishBase(w.substr(0, split), leftOut) &&
                    lookupRightPart(w.substr(split), rightOut)) {
                    out = std::move(leftOut);
                    out.insert(out.end(), rightOut.begin(), rightOut.end());
                    return true;
                }
            }
        }
        return false;
    }

    static std::string toUpperAscii(const std::string& s) {
        std::string r = s;
        for (char& ch : r) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return r;
    }

    static bool isEnglishVowelLetter(char c, bool yAsVowel) {
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') return true;
        return yAsVowel && (c == 'y');
    }

    static std::vector<ConsonantSpec> parseEnglishConsonantClusterList(const std::string& clus, bool isOnset, char nextChar = '\0') {
        std::vector<ConsonantSpec> out;
        if (clus.empty()) return out;

        auto pushUnique = [&](const char* key) {
            ConsonantSpec cs = makeConsonant(key);
            if (cs.type != C_NONE && out.size() < 3) {
                if (out.empty() || std::strcmp(out.back().label, cs.label) != 0) {
                    out.push_back(cs);
                }
            }
        };

        size_t i = 0;
        while (i < clus.size() && out.size() < 3) {
            auto match = [&](const char* pat) -> bool {
                size_t len = std::strlen(pat);
                return i + len <= clus.size() && clus.compare(i, len, pat) == 0;
            };

            if (match("tch")) { pushUnique("ch"); i += 3; continue; }
            if (match("dge")) { pushUnique("j");  i += 3; continue; }
            if (match("sh"))  { pushUnique("sh"); i += 2; continue; }
            if (match("ch"))  { pushUnique("ch"); i += 2; continue; }
            if (match("th"))  { pushUnique("th"); i += 2; continue; }
            if (match("ph"))  { pushUnique("f");  i += 2; continue; }
            if (match("wh"))  { pushUnique("w");  i += 2; continue; }
            if (match("ng"))  { pushUnique("ng"); i += 2; continue; }
            if (match("ck"))  { pushUnique("k");  i += 2; continue; }
            if (match("qu"))  { pushUnique("k"); pushUnique("w"); i += 2; continue; }
            if (match("dg"))  { pushUnique("j");  i += 2; continue; }
            if (isOnset && i == 0 && match("kn")) { pushUnique("n"); i += 2; continue; }
            if (isOnset && i == 0 && match("wr")) { pushUnique("r"); i += 2; continue; }
            if (!isOnset && match("mb") && i + 2 == clus.size()) { pushUnique("m"); i += 2; continue; }
            if (match("gh"))  { i += 2; continue; }

            char c = clus[i++];
            while (i < clus.size() && clus[i] == c) ++i;
            char following = (i < clus.size()) ? clus[i] : nextChar;
            bool softNext = (following == 'e' || following == 'i' || following == 'y');

            switch (c) {
                case 'b': pushUnique("b"); break;
                case 'c': pushUnique(softNext ? "s" : "k"); break;
                case 'd': pushUnique("d"); break;
                case 'f': pushUnique("f"); break;
                case 'g': pushUnique((softNext && (i == clus.size() || following == 'e' || following == 'y')) ? "j" : "g"); break;
                case 'h': pushUnique("h"); break;
                case 'j': pushUnique("j"); break;
                case 'k': pushUnique("k"); break;
                case 'l': pushUnique("l"); break;
                case 'm': pushUnique("m"); break;
                case 'n': pushUnique("n"); break;
                case 'p': pushUnique("p"); break;
                case 'q': pushUnique("k"); break;
                case 'r': pushUnique("r"); break;
                case 's': pushUnique("s"); break;
                case 't': pushUnique("t"); break;
                case 'v': pushUnique("v"); break;
                case 'w': pushUnique("w"); break;
                case 'x':
                    if (isOnset) pushUnique("z");
                    else { pushUnique("k"); pushUnique("s"); }
                    break;
                case 'y': pushUnique("y"); break;
                case 'z': pushUnique("z"); break;
                default: break;
            }
        }
        return out;
    }

    static void mapEnglishVowelGroup(const std::string& vg, const std::string& codaStr, const std::string& onsetStr,
                                       const std::string& afterSyllStr,
                                       bool trailingSilentE, bool isWordFinalSyll, bool isMonoSyllable,
                                       size_t syllIdx, size_t totalBaseSylls,
                                       VowelId& v1, VowelId& v2, bool& diph) {
        diph = false;
        bool followedByR = (!codaStr.empty() && codaStr[0] == 'r');

        // Word-final -ie or -ye -> /aI/ ("die", "dye", "lie", "tie", "pie", "bye")
        if (vg == "ie" && isWordFinalSyll && codaStr.empty()) {
            v1 = V_A; v2 = V_I; diph = true; return;
        }
        if (vg == "ye" && isWordFinalSyll && codaStr.empty()) {
            v1 = V_A; v2 = V_I; diph = true; return;
        }
        // Word-final -y -> /aI/ in monosyllables ("cry, my, try, fly, sky") or /i/ in multisyllables ("happy, city")
        if (vg == "y" && isWordFinalSyll && codaStr.empty() && !trailingSilentE) {
            if (isMonoSyllable) {
                v1 = V_A; v2 = V_I; diph = true;
            } else {
                v1 = v2 = V_I;
            }
            return;
        }

        if (vg == "ee" || vg == "ea" || vg == "ie") { v1 = v2 = V_I; return; }
        if (vg == "oo" || vg == "ew" || vg == "ue" || vg == "ui") { v1 = v2 = V_U; return; }
        if (vg == "ou" || vg == "ow") { v1 = V_A; v2 = V_U; diph = true; return; }
        if (vg == "ai" || vg == "ay" || vg == "ey") { v1 = V_E; v2 = V_I; diph = true; return; }
        if (vg == "oi" || vg == "oy") { v1 = V_O; v2 = V_I; diph = true; return; }
        if (vg == "au" || vg == "aw") { v1 = v2 = V_O; return; }
        if (vg == "oa") { v1 = V_O; v2 = V_U; diph = true; return; }

        char c = vg.empty() ? 'a' : vg[0];
        if (trailingSilentE) {
            switch (c) {
                case 'a': v1 = V_E; v2 = V_I; diph = true; return;
                case 'e': v1 = v2 = V_I; return;
                case 'i': case 'y': v1 = V_A; v2 = V_I; diph = true; return;
                case 'o': v1 = V_O; v2 = V_U; diph = true; return;
                case 'u': v1 = v2 = V_U; return;
            }
        }
        // Context rules: -igh, -ind/-ild, -old/-olt/-ost/-oll, -alk/-all, -aught/-ought
        if (c == 'i' && codaStr.find("gh") == 0) { v1 = V_A; v2 = V_I; diph = true; return; }
        if (c == 'i' && isMonoSyllable && (codaStr == "nd" || codaStr == "ld")) { v1 = V_A; v2 = V_I; diph = true; return; }
        if (c == 'o' && (codaStr == "ld" || codaStr == "lt" || codaStr == "st" || codaStr == "ll")) {
            v1 = V_O; v2 = V_U; diph = true; return;
        }
        if (c == 'a' && (codaStr == "ll" || codaStr == "lk")) { v1 = v2 = V_O; return; }
        if ((vg == "au" || vg == "ou" || c == 'o' || c == 'a') && codaStr.find("ght") != std::string::npos) {
            v1 = v2 = V_O; return;
        }

        // W-coloring on 'a' ("war, warm, want, watch, what, swap")
        if (vg == "a" && !onsetStr.empty() && (onsetStr.back() == 'w' || onsetStr.back() == 'u') && !trailingSilentE) {
            if (followedByR) { v1 = v2 = V_O; return; }
            if (codaStr != "k" && codaStr != "g" && codaStr != "ng" && codaStr != "x") {
                v1 = v2 = V_A; return;
            }
        }

        // Word-final open single-vowel syllables ("go, zero, me, comma, sofa")
        if (isWordFinalSyll && codaStr.empty() && !trailingSilentE && vg.size() == 1) {
            if (c == 'o') { v1 = V_O; v2 = V_U; diph = true; return; }
            if (c == 'e' && isMonoSyllable) { v1 = v2 = V_I; return; }
            if (c == 'a' && !isMonoSyllable) { v1 = v2 = V_I_DOTLESS; return; }
        }

        // English /ʌ/ spelling 'o' before -th / -v ("mother, brother, another, stepmother, cover, discover")
        if (vg == "o" && codaStr.empty() && !trailingSilentE &&
            (afterSyllStr.compare(0, 2, "th") == 0 || (!afterSyllStr.empty() && afterSyllStr[0] == 'v'))) {
            v1 = v2 = V_A;
            return;
        }

        // Open stressed first syllable in multisyllabic words ("baby, paper, silent, tiger, moment, music")
        if (!isMonoSyllable && syllIdx == 0 && codaStr.empty() && !trailingSilentE && vg.size() == 1 && (totalBaseSylls > 1 || !isWordFinalSyll)) {
            switch (c) {
                case 'a': v1 = V_E; v2 = V_I; diph = true; return;
                case 'e': v1 = v2 = V_I; return;
                case 'i': case 'y': v1 = V_A; v2 = V_I; diph = true; return;
                case 'o': v1 = V_O; v2 = V_U; diph = true; return;
                case 'u': v1 = v2 = V_U; return;
            }
        }

        // Unstressed final closed syllables (-en, -el, -on, -an, -ment, -ant, -ent -> reduced central vowel)
        if (!isMonoSyllable && syllIdx > 0 && !trailingSilentE && vg.size() == 1 && isWordFinalSyll) {
            if ((codaStr == "l" || codaStr == "n" || codaStr == "m" || codaStr == "nt" || codaStr == "nd") &&
                (c == 'a' || c == 'e' || c == 'o' || c == 'u')) {
                v1 = v2 = V_I_DOTLESS;
                return;
            }
        }

        if (followedByR) {
            if (!isMonoSyllable && syllIdx > 0 && isWordFinalSyll && (c == 'e' || c == 'o' || c == 'a' || c == 'u')) {
                v1 = v2 = V_I_DOTLESS; return;
            }
            if (c == 'a') { v1 = v2 = V_A; return; }
            if (c == 'o') { v1 = v2 = V_O; return; }
            if (c == 'e' || c == 'i' || c == 'u' || c == 'y') { v1 = v2 = V_OE; return; }
        }
        switch (c) {
            case 'a': v1 = v2 = V_AE; break;
            case 'e': v1 = v2 = V_E;  break;
            case 'i': case 'y': v1 = v2 = V_IH; break;
            case 'o': v1 = v2 = V_O;  break;
            case 'u': v1 = v2 = (!isMonoSyllable && syllIdx > 0 && isWordFinalSyll) ? V_I_DOTLESS : V_A; break;
            default:  v1 = v2 = V_A;  break;
        }
    }

    static std::vector<SyllableToken> dissectEnglishWord(const std::vector<uint32_t>& wordCps) {
        std::string w = cpToAsciiStr(wordCps);
        if (w.empty()) return {};

        std::vector<SyllableToken> out;
        if (tryLookupEnglishDict(w, out)) return out;

        // Check special suffixes (-tion, -sion, -ture) -> split stem + suffix token
        std::vector<SyllableToken> tailSuffix;
        std::string baseW = w;
        if (w.size() >= 5 && (w.compare(w.size() - 4, 4, "tion") == 0 || w.compare(w.size() - 4, 4, "sion") == 0)) {
            bool isSion = (w[w.size() - 4] == 's');
            baseW = w.substr(0, w.size() - 4);
            bool voicedZ = isSion && !baseW.empty() && isEnglishVowelLetter(baseW.back(), false);
            tailSuffix.push_back(makeToken(isSion ? "SION" : "TION",
                                           { makeConsonant(voicedZ ? "zh" : "sh") },
                                           V_I_DOTLESS, V_I_DOTLESS, false,
                                           { makeConsonant("n") }));
        } else if (w.size() >= 5 && w.compare(w.size() - 4, 4, "ture") == 0) {
            baseW = w.substr(0, w.size() - 4);
            tailSuffix.push_back(makeToken("TURE", { makeConsonant("ch") }, V_OE, V_OE, false, { makeConsonant("r") }));
        }

        // Preserve word-final -ie / -ye ("die", "dye", "lie", "tie") without stripping silent 'e'
        bool endsWithIeOrYe = (baseW.size() >= 3 &&
            (baseW.compare(baseW.size() - 2, 2, "ie") == 0 || baseW.compare(baseW.size() - 2, 2, "ye") == 0));

        bool hasSilentTrailingE = false;
        size_t effectiveLen = baseW.size();
        if (!endsWithIeOrYe && baseW.size() >= 3 && baseW.back() == 'e') {
            bool prevCons = !isEnglishVowelLetter(baseW[baseW.size() - 2], true);
            bool syllabicLe = (baseW[baseW.size() - 2] == 'l' && baseW.size() >= 4 && !isEnglishVowelLetter(baseW[baseW.size() - 3], true));
            bool earlierVowel = false;
            for (size_t i = 0; i + 2 < baseW.size(); ++i) {
                if (isEnglishVowelLetter(baseW[i], true)) { earlierVowel = true; break; }
            }
            if (prevCons && earlierVowel && !syllabicLe) {
                hasSilentTrailingE = true;
                effectiveLen = baseW.size() - 1;
            }
        }

        struct Nucleus { size_t start; size_t end; };
        std::vector<Nucleus> nuclei;
        size_t pos = 0;
        while (pos < effectiveLen) {
            bool yVow = (pos > 0);
            if (isEnglishVowelLetter(baseW[pos], yVow)) {
                size_t st = pos;
                if (pos > 0 && baseW[pos] == 'u' && baseW[pos - 1] == 'q') {
                    ++pos;
                    continue;
                }
                while (pos < effectiveLen && isEnglishVowelLetter(baseW[pos], pos > 0) && (pos - st) < 2) {
                    ++pos;
                }
                nuclei.push_back({ st, pos });
            } else {
                ++pos;
            }
        }

        if (nuclei.empty()) {
            if (!baseW.empty()) {
                out.push_back(makeToken(toUpperAscii(baseW), parseEnglishConsonantClusterList(baseW, true), V_I_DOTLESS, V_I_DOTLESS, false, {}));
            }
            out.insert(out.end(), tailSuffix.begin(), tailSuffix.end());
            return out;
        }

        auto isDigraphPair = [&](char a, char b) {
            return (a == 's' && b == 'h') || (a == 'c' && b == 'h') || (a == 't' && b == 'h') ||
                   (a == 'p' && b == 'h') || (a == 'w' && b == 'h') || (a == 'n' && b == 'g') ||
                   (a == 'c' && b == 'k') || (a == 'q' && b == 'u');
        };

        bool isMono = (nuclei.size() == 1 && tailSuffix.empty());
        size_t syllStart = 0;
        for (size_t k = 0; k < nuclei.size(); ++k) {
            const auto& nuc = nuclei[k];
            size_t syllEnd = baseW.size();
            if (k + 1 < nuclei.size()) {
                size_t nextStart = nuclei[k + 1].start;
                size_t consCount = nextStart - nuc.end;
                if (consCount == 0 || consCount == 1) {
                    syllEnd = nuc.end;
                } else if (consCount == 2 && isDigraphPair(baseW[nuc.end], baseW[nuc.end + 1])) {
                    syllEnd = nuc.end;
                } else {
                    syllEnd = nextStart - 1;
                    if (syllEnd > nuc.end && isDigraphPair(baseW[syllEnd - 1], baseW[syllEnd])) {
                        --syllEnd;
                    }
                }
            }

            std::string rawDisp = toUpperAscii(baseW.substr(syllStart, syllEnd - syllStart));
            std::string onsetStr = baseW.substr(syllStart, nuc.start - syllStart);
            std::string vgStr = baseW.substr(nuc.start, nuc.end - nuc.start);
            size_t codaBound = std::min(syllEnd, effectiveLen);
            std::string codaStr = (codaBound > nuc.end) ? baseW.substr(nuc.end, codaBound - nuc.end) : "";

            bool isLastBaseSyll = (k + 1 == nuclei.size());
            char nextAfterOnset = vgStr.empty() ? '\0' : vgStr[0];
            char nextAfterCoda = (isLastBaseSyll && hasSilentTrailingE) ? 'e' : '\0';
            std::string afterSyllStr = (codaBound < baseW.size()) ? baseW.substr(codaBound) : "";

            VowelId v1 = V_A, v2 = V_A;
            bool diph = false;
            mapEnglishVowelGroup(vgStr, codaStr, onsetStr, afterSyllStr, isLastBaseSyll && hasSilentTrailingE,
                                 isLastBaseSyll && tailSuffix.empty(), isMono, k, nuclei.size(), v1, v2, diph);

            std::string effCoda = codaStr;
            if (effCoda == "lk") effCoda = "k"; // silent 'l' in -alk
            if (effCoda.find("gh") == 0) effCoda = effCoda.substr(2); // silent gh in -igh / -ight / -ought

            std::vector<ConsonantSpec> onsetClus = parseEnglishConsonantClusterList(onsetStr, true, nextAfterOnset);
            if (k > 0 && onsetStr == "th" && vgStr == "e" && !codaStr.empty() && codaStr[0] == 'r') {
                onsetClus = { makeConsonant("dh") };
            }
            std::vector<ConsonantSpec> codaClus = parseEnglishConsonantClusterList(effCoda, false, nextAfterCoda);

            out.push_back(makeToken(rawDisp, onsetClus, v1, v2, diph, codaClus));
            syllStart = syllEnd;
        }

        out.insert(out.end(), tailSuffix.begin(), tailSuffix.end());
        return out;
    }
};

} // namespace Fraudize
