#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "../../include/vst3_plugin.h"
#include "../dsp/BaburDSP.hpp"
#include "../ui/BaburUI.hpp"
#include "BaburVST3Editor.hpp"

using namespace Fraudize;

class BaburParameter : public VST3_Parameter {
public:
    BaburParameter(int id, const char* nameStr, const char* unitLabel, float defVal, uint32_t steps = 0)
        : VST3_Parameter(nullptr, defVal), paramId(id) {
        std::memset(paramName, 0, sizeof(paramName));
        std::strncpy(paramName, nameStr, sizeof(paramName) - 1);
        name = paramName;
        short_name = paramName;
        label = unitLabel;
        num_steps = steps;
    }

    const char* displayText(float val, char* buf) override {
        std::string disp = BaburEngine::formatParamDisplay(paramId, val);
        std::strncpy(buf, disp.c_str(), 63);
        buf[63] = '\0';
        return buf;
    }

private:
    int paramId;
    char paramName[64];
};

class BaburDescriptor : public VST3_Descriptor {
public:
    BaburDescriptor() {
        name = "babur";
        author = "myavuzuzun";
        url = "https://github.com/myavuzuzun";
        email = "memtyovz@gmail.com";
        version_text = "1.0.0";
        version = 1000;
        is_synth = true;
        has_editor = true;
        can_send_midi = false;
        can_receive_midi = true;
        editor_width = BaburUI::UI_WIDTH;
        editor_height = BaburUI::UI_HEIGHT;
        num_inputs = 0;
        num_outputs = 2;

        static const uint8_t p_id[16] = { 0x42, 0x41, 0x42, 0x52, 0x00, 0x01, 0x4D, 0x59, 0x55, 0x5A, 0x55, 0x4E, 0x42, 0x41, 0x42, 0x31 };
        static const uint8_t e_id[16] = { 0x42, 0x41, 0x42, 0x52, 0x00, 0x02, 0x4D, 0x59, 0x55, 0x5A, 0x55, 0x4E, 0x42, 0x41, 0x42, 0x32 };
        std::memcpy(const_cast<uint8_t*>(plugin_id), p_id, 16);
        std::memcpy(const_cast<uint8_t*>(editor_id), e_id, 16);

        appendParameter(new BaburParameter(PARAM_CHARACTER,  "Character",  "%",  0.35f));
        appendParameter(new BaburParameter(PARAM_DICTION,    "Diction",    "%",  0.65f));
        appendParameter(new BaburParameter(PARAM_THROAT,     "Throat",     "st", 0.50f));
        appendParameter(new BaburParameter(PARAM_GLIDE,      "Glide",      "ms", 0.25f));
        appendParameter(new BaburParameter(PARAM_LANG,       "Language",   "",   0.00f, 1));
        appendParameter(new BaburParameter(PARAM_LEGATO,     "Legato",     "",   1.00f, 2));
        appendParameter(new BaburParameter(PARAM_AUDIO_MODE,  "Audio Mode",    "",   1.00f, 1));
        appendParameter(new BaburParameter(PARAM_EXCITER,     "Voice Mode",    "",   1.00f, 3));
        appendParameter(new BaburParameter(PARAM_GENDER,      "Gender",        "%",  0.50f));
        appendParameter(new BaburParameter(PARAM_PITCH_SHIFT, "Pitch Shift",   "st", 0.50f));
        appendParameter(new BaburParameter(PARAM_BREATHINESS, "Breathiness",   "%",  0.00f));
        appendParameter(new BaburParameter(PARAM_GRIT,        "Grit",          "%",  0.00f));
        appendParameter(new BaburParameter(PARAM_NASALITY,    "Nasality",      "%",  0.00f));
        appendParameter(new BaburParameter(PARAM_BRIGHTNESS,  "Brightness",    "%",  0.50f));
        appendParameter(new BaburParameter(PARAM_VIB_DEPTH,   "Vibrato Depth", "%",  0.25f));
        appendParameter(new BaburParameter(PARAM_VIB_RATE,    "Vibrato Rate",  "Hz", (5.2f - 2.5f) / 6.5f));
        appendParameter(new BaburParameter(PARAM_TREMOR,      "Age / Tremor",  "%",  0.00f));
        appendParameter(new BaburParameter(PARAM_ROBOTIC,     "Robotic",       "%",  0.00f));
    }
};

class BaburInstance : public VST3_Instance {
public:
    BaburInstance() {
        incomingMidi.reserve(128);
    }
    ~BaburInstance() override = default;

    void on_deactivate() override {
        incomingMidi.clear();
        engine.reset();
    }

    void on_prepare(float sampleRate) override {
        engine.setSampleRate(sampleRate);
    }

    void on_parameter(uint32_t index, float value, uint32_t /*mode*/) override {
        if (index < NUM_PARAMS) {
            engine.setParam(static_cast<int>(index), value);
        }
    }

    void on_midi(uint32_t offset, uint8_t msg1, uint8_t msg2, uint8_t msg3) override {
        incomingMidi.push_back({ static_cast<int32_t>(offset), msg1, msg2, msg3 });
    }

    void on_process(VST3_ProcessContext* ctx) override {
        if (ctx->sample_rate >= 8000.0f && std::fabs(ctx->sample_rate - engine.getSampleRate()) > 1.0f) {
            engine.setSampleRate(ctx->sample_rate);
        }

        if (!incomingMidi.empty()) {
            engine.processRawMidi(incomingMidi.data(), incomingMidi.size());
            incomingMidi.clear();
        }

        if (ctx->num_samples > 0 && ctx->outputs[0]) {
            float* outR = ctx->outputs[1] ? ctx->outputs[1] : ctx->outputs[0];
            engine.render(ctx->outputs[0], outR, static_cast<int>(ctx->num_samples));
        }
    }

    VST3_Editor* on_openEditor(void* parent) override {
        return new BaburVST3EditorWrapper<BaburUI, BaburEngine>(this, MDescriptor, parent, &engine);
    }

    void on_closeEditor(VST3_Editor* editor) override {
        delete editor;
    }

    uint32_t on_saveState(void** ABuffer, uint32_t /*AMode*/) override {
        stateBuffer = engine.saveState();
        *ABuffer = stateBuffer.data();
        return static_cast<uint32_t>(stateBuffer.size());
    }

    void on_restoreState(uint32_t ASize, void* APointer, uint32_t /*AMode*/) override {
        if (!APointer || ASize == 0) return;
        engine.restoreState(reinterpret_cast<const uint8_t*>(APointer), static_cast<size_t>(ASize));
    }

    bool on_getParameter(uint32_t index, float& outPlainValue) override {
        if (index < NUM_PARAMS) {
            outPlainValue = engine.getParam(static_cast<int>(index));
            return true;
        }
        return false;
    }

    uint32 on_getTailSamples() override {
        return static_cast<uint32>(engine.getSampleRate() * 2.0f);
    }

private:
    BaburEngine engine;
    std::vector<BaburEngine::RawMidi> incomingMidi;
    std::vector<uint8_t> stateBuffer;
};

VST3_ENTRYPOINT(BaburDescriptor, BaburInstance)
