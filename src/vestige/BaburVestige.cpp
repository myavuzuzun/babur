#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "../../include/vestige/vestige.h"
#include "../dsp/BaburDSP.hpp"
#include "../ui/BaburUI.hpp"

using namespace Fraudize;

// Additional VeSTige numeric opcodes & flags not listed in the minimal header
#ifndef effSetProgramName
#define effSetProgramName 4
#endif
#ifndef effGetParamLabel
#define effGetParamLabel 6
#endif
#ifndef effGetParamDisplay
#define effGetParamDisplay 7
#endif
#ifndef effGetChunk
#define effGetChunk 23
#endif
#ifndef effSetChunk
#define effSetChunk 24
#endif
#ifndef effGetProgramNameIndexed
#define effGetProgramNameIndexed 29
#endif
#ifndef effGetTailSize
#define effGetTailSize 52
#endif
#ifndef effFlagsProgramChunks
#define effFlagsProgramChunks (1 << 5)
#endif

struct BaburRect {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
};

struct BaburEffectBlock {
    AEffect fx;
    void*   processDoubleReplacing;
    char    future[56];
};

class BaburVestigePlugin {
public:
    BaburVestigePlugin(audioMasterCallback cb) : hostCallback(cb) {
        timeBeginPeriod(1);
        std::memset(&effectBlock, 0, sizeof(BaburEffectBlock));
        AEffect& effect = effectBlock.fx;
        effect.magic = kEffectMagic;
        effect.dispatcher = dispatcherStatic;
        effect.process = processStatic;
        effect.processReplacing = processReplacingStatic;
        effect.setParameter = setParameterStatic;
        effect.getParameter = getParameterStatic;
        effect.numPrograms = 1;
        effect.numParams = NUM_PARAMS;
        effect.numInputs = 0;
        effect.numOutputs = 2;
        effect.flags = effFlagsHasEditor | effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
        effect.uniqueID = CCONST('B', 'A', 'B', 'R');
        effect.version = 1000;
        effect.object = this;

        editRect.top = 0;
        editRect.left = 0;
        editRect.bottom = BaburUI::UI_HEIGHT;
        editRect.right = BaburUI::UI_WIDTH;

        midiScratch.reserve(128);
    }

    ~BaburVestigePlugin() {
        gui.reset();
        timeEndPeriod(1);
    }

    AEffect* getAEffect() { return &effectBlock.fx; }

    static intptr_t __cdecl dispatcherStatic(AEffect* e, int opcode, int index, intptr_t value, void* ptr, float opt) {
        BaburVestigePlugin* p = reinterpret_cast<BaburVestigePlugin*>(e->object);
        return p ? p->dispatcher(opcode, index, value, ptr, opt) : 0;
    }

    static void __cdecl processStatic(AEffect* e, float** inputs, float** outputs, int sampleFrames) {
        BaburVestigePlugin* p = reinterpret_cast<BaburVestigePlugin*>(e->object);
        if (p) p->processReplacing(inputs, outputs, sampleFrames);
    }

    static void __cdecl processReplacingStatic(AEffect* e, float** inputs, float** outputs, int sampleFrames) {
        BaburVestigePlugin* p = reinterpret_cast<BaburVestigePlugin*>(e->object);
        if (p) p->processReplacing(inputs, outputs, sampleFrames);
    }

    static void __cdecl setParameterStatic(AEffect* e, int index, float parameter) {
        BaburVestigePlugin* p = reinterpret_cast<BaburVestigePlugin*>(e->object);
        if (p) p->engine.setParam(index, parameter);
    }

    static float __cdecl getParameterStatic(AEffect* e, int index) {
        BaburVestigePlugin* p = reinterpret_cast<BaburVestigePlugin*>(e->object);
        return p ? p->engine.getParam(index) : 0.0f;
    }

    static void onGuiParamChanged(void* userData, int paramId, float val) {
        BaburVestigePlugin* p = reinterpret_cast<BaburVestigePlugin*>(userData);
        if (p && p->hostCallback) {
            p->hostCallback(&p->effectBlock.fx, audioMasterAutomate, paramId, 0, nullptr, val);
        }
    }

    intptr_t dispatcher(int opcode, int index, intptr_t value, void* ptr, float opt) {
        switch (opcode) {
            case effOpen:
                return 1;
            case effClose:
                delete this;
                return 1;
            case effSetProgram:
                return 1;
            case effGetProgram:
                return 0;
            case effSetProgramName:
                return 1;
            case effGetProgramName:
                if (ptr) {
                    std::strncpy(reinterpret_cast<char*>(ptr), "Default", 24);
                    reinterpret_cast<char*>(ptr)[23] = '\0';
                }
                return 1;
            case effGetProgramNameIndexed:
                if (ptr && index == 0) {
                    std::strncpy(reinterpret_cast<char*>(ptr), "Default", 24);
                    reinterpret_cast<char*>(ptr)[23] = '\0';
                    return 1;
                }
                return 0;
            case effGetParamLabel:
                if (ptr) {
                    const char* lbl = "";
                    if (index == PARAM_CHARACTER || index == PARAM_DICTION ||
                        index == PARAM_GENDER || index == PARAM_BREATHINESS ||
                        index == PARAM_GRIT || index == PARAM_NASALITY ||
                        index == PARAM_BRIGHTNESS || index == PARAM_VIB_DEPTH ||
                        index == PARAM_TREMOR || index == PARAM_ROBOTIC) lbl = "%";
                    else if (index == PARAM_THROAT || index == PARAM_PITCH_SHIFT) lbl = "st";
                    else if (index == PARAM_GLIDE) lbl = "ms";
                    else if (index == PARAM_VIB_RATE) lbl = "Hz";
                    std::strncpy(reinterpret_cast<char*>(ptr), lbl, 8);
                    reinterpret_cast<char*>(ptr)[7] = '\0';
                }
                return 1;
            case effGetParamDisplay:
                if (ptr) {
                    std::string s = engine.getParamDisplay(index);
                    std::strncpy(reinterpret_cast<char*>(ptr), s.c_str(), 16);
                    reinterpret_cast<char*>(ptr)[15] = '\0';
                }
                return 1;
            case effGetParamName:
                if (ptr) {
                    std::string s = engine.getParamName(index);
                    std::strncpy(reinterpret_cast<char*>(ptr), s.c_str(), 16);
                    reinterpret_cast<char*>(ptr)[15] = '\0';
                }
                return 1;
            case effSetSampleRate:
                engine.setSampleRate(opt > 1000.0f ? opt : static_cast<float>(value));
                return 1;
            case effSetBlockSize:
                return 1;
            case effMainsChanged:
                if (value == 0) engine.reset();
                return 1;
            case effEditGetRect:
                if (ptr) {
                    *reinterpret_cast<BaburRect**>(ptr) = &editRect;
                    return 1;
                }
                return 0;
            case effEditOpen:
                if (ptr) {
                    gui = std::make_unique<BaburUI>(&engine, &BaburVestigePlugin::onGuiParamChanged, this);
                    if (gui->openWindow(ptr)) return 1;
                }
                return 0;
            case effEditClose:
                gui.reset();
                return 1;
            case effProcessEvents:
                if (ptr) {
                    const VstEvents* evs = reinterpret_cast<const VstEvents*>(ptr);
                    if (evs->numEvents > 0) {
                        midiScratch.clear();
                        for (int i = 0; i < evs->numEvents; ++i) {
                            const VstEvent* ev = evs->events[i];
                            if (!ev) continue;
                            const VstMidiEvent* mev = reinterpret_cast<const VstMidiEvent*>(ev);
                            if (mev->type != kVstMidiType) continue;
                            midiScratch.push_back({
                                static_cast<int32_t>(mev->deltaFrames),
                                static_cast<uint8_t>(mev->midiData[0]),
                                static_cast<uint8_t>(mev->midiData[1]),
                                static_cast<uint8_t>(mev->midiData[2])
                            });
                        }
                        if (!midiScratch.empty()) {
                            engine.processRawMidi(midiScratch.data(), midiScratch.size());
                        }
                    }
                    return 1;
                }
                return 0;
            case effGetChunk:
                if (ptr) {
                    chunkData = engine.saveState();
                    *reinterpret_cast<void**>(ptr) = chunkData.data();
                    return static_cast<intptr_t>(chunkData.size());
                }
                return 0;
            case effSetChunk:
                if (ptr && value > 0) {
                    engine.restoreState(reinterpret_cast<const uint8_t*>(ptr), static_cast<size_t>(value));
                    if (hostCallback) {
                        hostCallback(&effectBlock.fx, audioMasterUpdateDisplay, 0, 0, nullptr, 0.0f);
                    }
                    return 1;
                }
                return 0;
            case effCanDo:
                if (ptr) {
                    const char* canDoStr = reinterpret_cast<const char*>(ptr);
                    if (std::strcmp(canDoStr, "receiveVstEvents") == 0) return 1;
                    if (std::strcmp(canDoStr, "receiveVstMidiEvent") == 0) return 1;
                }
                return 0;
            case effGetTailSize:
                return static_cast<intptr_t>(engine.getSampleRate() * 2.0f);
            case effGetPlugCategory:
                return kPlugCategSynth;
            case effGetEffectName:
                if (ptr) {
                    std::strncpy(reinterpret_cast<char*>(ptr), "babur", 32);
                    reinterpret_cast<char*>(ptr)[31] = '\0';
                }
                return 1;
            case effGetVendorString:
                if (ptr) {
                    std::strncpy(reinterpret_cast<char*>(ptr), "myavuzuzun", 32);
                    reinterpret_cast<char*>(ptr)[31] = '\0';
                }
                return 1;
            case effGetProductString:
                if (ptr) {
                    std::strncpy(reinterpret_cast<char*>(ptr), "babur", 32);
                    reinterpret_cast<char*>(ptr)[31] = '\0';
                }
                return 1;
            case effGetVendorVersion:
                return 1000;
            case effGetVstVersion:
                return 2400;
            default:
                return 0;
        }
    }

    void processReplacing(float** /*inputs*/, float** outputs, int sampleFrames) {
        if (!outputs || !outputs[0] || !outputs[1]) return;
        engine.render(outputs[0], outputs[1], sampleFrames);
    }

private:
    audioMasterCallback hostCallback = nullptr;
    BaburEffectBlock effectBlock;
    BaburRect editRect;
    BaburEngine engine;
    std::unique_ptr<BaburUI> gui;
    std::vector<uint8_t> chunkData;
    std::vector<BaburEngine::RawMidi> midiScratch;
};

extern "C" {
__declspec(dllexport) AEffect* VSTPluginMain(audioMasterCallback audioMaster) {
    if (!audioMaster) return nullptr;
    BaburVestigePlugin* plugin = new BaburVestigePlugin(audioMaster);
    return plugin->getAEffect();
}
}
