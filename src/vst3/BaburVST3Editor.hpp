#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <memory>
#include "../../include/vst3_plugin.h"

template<typename UIType, typename EngineType>
class BaburVST3EditorWrapper : public VST3_Editor {
public:
    BaburVST3EditorWrapper(VST3_EditorListener* listener, VST3_Descriptor* desc, void* parent, EngineType* eng)
        : VST3_Editor(listener, desc, parent), engine(eng) {
        gui = std::make_unique<UIType>(eng, &BaburVST3EditorWrapper::onGuiParam, this);
        gui->openWindow(parent);
    }

    ~BaburVST3EditorWrapper() override {
        gui.reset();
    }

    void updateParameterFromHost(uint32_t /*index*/, float /*value*/) override {
        // UI timer polls engine dirty flag and repaints cleanly
    }

    static void onGuiParam(void* userData, int paramId, float value) {
        BaburVST3EditorWrapper* self = reinterpret_cast<BaburVST3EditorWrapper*>(userData);
        if (self && paramId >= 0) {
            self->updateParameterFromGui(static_cast<uint32_t>(paramId), value);
        }
    }

    UIType* getUI() { return gui.get(); }

private:
    EngineType* engine = nullptr;
    std::unique_ptr<UIType> gui;
};
