#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include "../../include/vst3_plugin.h"

class MockEventList : public IEventList {
public:
    std::vector<Event> events;

    tresult PLUGIN_API queryInterface(const FUID /*_iid*/, void** obj) override {
        *obj = nullptr;
        return kResultFalse;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override {
        return static_cast<int32>(events.size());
    }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index >= 0 && index < static_cast<int32>(events.size())) {
            e = events[index];
            return kResultOk;
        }
        return kResultFalse;
    }
    tresult PLUGIN_API addEvent(Event& e) override {
        events.push_back(e);
        return kResultOk;
    }
};

class MockBStream : public IBStream {
public:
    std::vector<uint8_t> buffer;
    int64_t cursor = 0;

    tresult PLUGIN_API queryInterface(const FUID /*_iid*/, void** obj) override {
        *obj = nullptr;
        return kResultFalse;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buf, int32_t numBytes, int32_t* numBytesRead) override {
        int64_t avail = static_cast<int64_t>(buffer.size()) - cursor;
        if (avail <= 0) {
            if (numBytesRead) *numBytesRead = 0;
            return kResultFalse;
        }
        int32_t toRead = static_cast<int32_t>(avail < numBytes ? avail : numBytes);
        std::memcpy(buf, buffer.data() + cursor, toRead);
        cursor += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buf, int32_t numBytes, int32_t* numBytesWritten) override {
        const uint8_t* src = static_cast<const uint8_t*>(buf);
        buffer.insert(buffer.end(), src, src + numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64_t pos, int32_t mode, int64_t* result) override {
        if (mode == kIBSeekSet) cursor = pos;
        else if (mode == kIBSeekCur) cursor += pos;
        else if (mode == kIBSeekEnd) cursor = static_cast<int64_t>(buffer.size()) + pos;
        if (result) *result = cursor;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64_t* pos) override {
        if (pos) *pos = cursor;
        return kResultOk;
    }
};

int main(int argc, char* argv[]) {
    const char* dllPath = (argc > 1) ? argv[1] : "dist/babur.vst3";
    printf("Loading: %s\n", dllPath);

    HMODULE hMod = LoadLibraryA(dllPath);
    if (!hMod) {
        printf("ERROR: LoadLibrary failed, error code %lu\n", GetLastError());
        return 1;
    }

    typedef bool (__stdcall *InitDllFunc)();
    typedef bool (__stdcall *ExitDllFunc)();
    typedef IPluginFactory* (__stdcall *GetPluginFactoryFunc)();

    InitDllFunc initDll = (InitDllFunc)GetProcAddress(hMod, "InitDll");
    ExitDllFunc exitDll = (ExitDllFunc)GetProcAddress(hMod, "ExitDll");
    GetPluginFactoryFunc getFactory = (GetPluginFactoryFunc)GetProcAddress(hMod, "GetPluginFactory");

    if (!initDll || !exitDll || !getFactory) {
        printf("ERROR: Missing exports\n");
        FreeLibrary(hMod);
        return 1;
    }

    initDll();
    IPluginFactory* factory = getFactory();
    if (!factory) {
        printf("ERROR: getFactory returned null\n");
        FreeLibrary(hMod);
        return 1;
    }

    PFactoryInfo fInfo{};
    factory->getFactoryInfo(&fInfo);
    printf("Vendor: '%s'\n", fInfo.vendor);

    PClassInfo classInfo{};
    factory->getClassInfo(0, &classInfo);
    printf("Plugin Name: '%s', Category: '%s'\n", classInfo.name, classInfo.category);

    void* compObj = nullptr;
    tresult res = factory->createInstance(classInfo.cid, IComponent_iid, &compObj);
    if (res != kResultOk || !compObj) {
        printf("ERROR: createInstance failed\n");
        FreeLibrary(hMod);
        return 1;
    }

    IComponent* component = (IComponent*)compObj;
    component->initialize(nullptr);

    int32 audioIns = component->getBusCount(kAudio, kInput);
    int32 audioOuts = component->getBusCount(kAudio, kOutput);
    int32 eventIns = component->getBusCount(kEvent, kInput);
    printf("Buses: AudioIn=%d, AudioOut=%d, EventIn=%d\n", audioIns, audioOuts, eventIns);
    if (audioIns != 0 || audioOuts != 1 || eventIns != 1) {
        printf("ERROR: Unexpected bus layout!\n");
        return 1;
    }

    void* procObj = nullptr;
    res = component->queryInterface(IAudioProcessor_iid, &procObj);
    if (res != kResultOk || !procObj) {
        printf("ERROR: queryInterface(IAudioProcessor) failed!\n");
        return 1;
    }

    IAudioProcessor* processor = (IAudioProcessor*)procObj;
    ProcessSetup setup{};
    setup.processMode = 0;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 512;
    setup.sampleRate = 44100.0;
    processor->setupProcessing(setup);
    processor->setProcessing(true);

    float outL[512] = {0};
    float outR[512] = {0};
    float* outChs[2] = { outL, outR };

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outChs;

    ProcessContext procCtx{};
    procCtx.sampleRate = 44100.0;
    procCtx.tempo = 120.0;
    procCtx.state = ProcessContext::kPlaying;

    MockEventList mockInEvents;
    Event noteOnEv{};
    noteOnEv.type = Event::kNoteOnEvent;
    noteOnEv.busIndex = 0;
    noteOnEv.sampleOffset = 0;
    noteOnEv.noteOn.channel = 0;
    noteOnEv.noteOn.pitch = 60;
    noteOnEv.noteOn.velocity = 0.85f;
    noteOnEv.noteOn.length = -1;
    noteOnEv.noteOn.noteId = -1;
    mockInEvents.addEvent(noteOnEv);

    ProcessData data{};
    data.symbolicSampleSize = kSample32;
    data.numSamples = 512;
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputEvents = &mockInEvents;
    data.processContext = &procCtx;

    processor->process(data);
    for (int s = 0; s < 512; ++s) {
        if (!std::isfinite(outL[s]) || !std::isfinite(outR[s])) {
            printf("ERROR: Non-finite audio output!\n");
            return 1;
        }
    }
    printf("Audio process check (no source loaded -> clean silence) passed.\n");

    void* editObj = nullptr;
    res = component->queryInterface(IEditController_iid, &editObj);
    if (res != kResultOk || !editObj) {
        printf("ERROR: queryInterface(IEditController) failed!\n");
        return 1;
    }
    IEditController* controller = (IEditController*)editObj;
    int32 numParams = controller->getParameterCount();
    printf("Parameter count: %d\n", numParams);
    if (numParams != 18) {
        printf("ERROR: Expected strictly 18 parameters, got %d\n", numParams);
        return 1;
    }
    for (int i = 0; i < numParams; ++i) {
        ParameterInfo pInfo{};
        controller->getParameterInfo(i, pInfo);
        char title[128] = {0};
        utf16_to_char(pInfo.title, title);
        printf("  Param %d: '%s' (default=%.4f)\n", i, title, pInfo.defaultNormalizedValue);
    }

    // State save & restore check
    MockBStream strm;
    controller->setParamNormalized(0, 0.7777);
    controller->setParamNormalized(1, 0.3333);
    controller->setParamNormalized(2, 0.8125);
    controller->setParamNormalized(3, 0.4400);
    controller->setParamNormalized(4, 1.0000);
    controller->setParamNormalized(5, 0.0000);
    controller->setParamNormalized(6, 0.0000);
    controller->setParamNormalized(7, 0.0000);
    controller->setParamNormalized(8, 0.8500);
    controller->setParamNormalized(17, 0.6250);
    component->getState(&strm);

    controller->setParamNormalized(0, 0.1111);
    controller->setParamNormalized(1, 0.2222);
    controller->setParamNormalized(4, 0.0000);
    controller->setParamNormalized(7, 1.0000);
    controller->setParamNormalized(8, 0.1500);
    controller->setParamNormalized(17, 0.0500);
    strm.cursor = 0;
    component->setState(&strm);

    double r[18];
    for (int i = 0; i < 18; ++i) r[i] = controller->getParamNormalized(i);
    printf("State Restore Check: p0=%.4f p1=%.4f p2=%.4f p3=%.4f p4=%.4f p5=%.4f p6=%.4f p7=%.4f p8=%.4f p17=%.4f\n",
           r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[17]);
    if (std::abs(r[0] - 0.7777) > 1e-4 || std::abs(r[1] - 0.3333) > 1e-4 ||
        std::abs(r[2] - 0.8125) > 1e-4 || std::abs(r[3] - 0.4400) > 1e-4 ||
        std::abs(r[4] - 1.0000) > 1e-4 || std::abs(r[5] - 0.0000) > 1e-4 ||
        std::abs(r[6] - 0.0000) > 1e-4 || std::abs(r[7] - 0.0000) > 1e-4 ||
        std::abs(r[8] - 0.8500) > 1e-4 || std::abs(r[17] - 0.6250) > 1e-4) {
        printf("ERROR: State restore did not sync controller parameter values!\n");
        return 1;
    }

    IPlugView* plugView = controller->createView(kEditor);
    if (!plugView) {
        printf("ERROR: createView failed!\n");
        return 1;
    }
    ViewRect rect{};
    plugView->getSize(&rect);
    printf("Editor Rect: %dx%d (HWND supported: %s)\n",
           rect.right - rect.left, rect.bottom - rect.top,
           plugView->isPlatformTypeSupported(kPlatformTypeHWND) == kResultOk ? "YES" : "NO");

    HWND hHost = CreateWindowExA(0, "STATIC", "VST3Host", WS_POPUP, 0, 0,
                                 rect.right - rect.left, rect.bottom - rect.top,
                                 nullptr, nullptr, nullptr, nullptr);
    if (hHost) {
        plugView->attached(hHost, kPlatformTypeHWND);
        plugView->removed();
        DestroyWindow(hHost);
        printf("Direct2D VST3 GUI attached/removed cleanly!\n");
    }

    plugView->release();
    controller->release();
    processor->setProcessing(false);
    processor->release();
    component->terminate();
    component->release();
    exitDll();
    FreeLibrary(hMod);

    printf("ALL VST3 CHECKS PASSED FOR %s!\n", dllPath);
    return 0;
}
