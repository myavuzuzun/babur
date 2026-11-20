#pragma once

#if defined(_WIN32)
#include <commdlg.h>
#include <shellapi.h>
#include "../common/Direct2DTheme.hpp"
#include "../dsp/BaburDSP.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace Fraudize {

class BaburUI {
public:
    static constexpr int UI_WIDTH = 460;
    static constexpr int UI_HEIGHT = 436;

    using ParamChangedCallback = void (*)(void* userData, int paramId, float value);

    BaburUI(BaburEngine* eng, ParamChangedCallback cb = nullptr, void* cbUser = nullptr)
        : engine(eng), onParamCb(cb), onParamUser(cbUser) {
        initLayout();
        initDirect2D();
        syncTextFromEngine(true);
    }

    ~BaburUI() {
        closeWindow();
        cleanupDirect2D();
    }

    void setParamCallback(ParamChangedCallback cb, void* cbUser) {
        onParamCb = cb;
        onParamUser = cbUser;
    }

    bool openWindow(void* parentWin) {
        closeWindow();
        parentHWnd = static_cast<HWND>(parentWin);

        HINSTANCE hInstance = getModuleInstance();
        registerWindowClass();

        hWnd = CreateWindowExW(
            0,
            L"BaburFraudizeWindow",
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, UI_WIDTH, UI_HEIGHT,
            parentHWnd,
            nullptr,
            hInstance,
            this
        );

        if (!hWnd) return false;

        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        DragAcceptFiles(hWnd, TRUE);
        createDeviceResources();
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        SetTimer(hWnd, TIMER_ID, 33, nullptr);
        syncTextFromEngine(true);
        return true;
    }

    void closeWindow() {
        if (hWnd) {
            KillTimer(hWnd, TIMER_ID);
            DestroyWindow(hWnd);
            hWnd = nullptr;
        }
        discardDeviceResources();
    }

    HWND getHWnd() const { return hWnd; }

private:
    static constexpr UINT_PTR TIMER_ID = 5012;

    // Header Row (Source File + Preset / Audio Info)
    static constexpr float HDR_Y = 10.0f;
    static constexpr float HDR_H = 28.0f;
    static constexpr float FILE_X = 14.0f;
    static constexpr float FILE_W = 186.0f;
    static constexpr float PATCH_X = 206.0f;
    static constexpr float PATCH_W = 240.0f;
    static constexpr float ARROW_W = 28.0f;

    // Lyrics & Syllable Card
    static constexpr float LYRICS_CARD_X = 14.0f;
    static constexpr float LYRICS_CARD_Y = 58.0f;
    static constexpr float LYRICS_CARD_W = 432.0f;
    static constexpr float LYRICS_CARD_H = 122.0f;

    static constexpr float MINI_BTN_Y = 62.0f;
    static constexpr float MINI_BTN_H = 16.0f;
    static constexpr float PASTE_X = 266.0f;
    static constexpr float PASTE_W = 54.0f;
    static constexpr float TXT_X = 325.0f;
    static constexpr float TXT_W = 62.0f;
    static constexpr float CLEAR_X = 392.0f;
    static constexpr float CLEAR_W = 44.0f;

    static constexpr float TEXTBOX_X = 24.0f;
    static constexpr float TEXTBOX_Y = 82.0f;
    static constexpr float TEXTBOX_W = 412.0f;
    static constexpr float TEXTBOX_H = 44.0f;

    static constexpr float RESET_X = 24.0f;
    static constexpr float RESET_Y = 133.0f;
    static constexpr float RESET_W = 54.0f;
    static constexpr float RESET_H = 17.0f;

    static constexpr float RKEY_X = 24.0f;
    static constexpr float RKEY_Y = 152.0f;
    static constexpr float RKEY_W = 54.0f;
    static constexpr float RKEY_H = 17.0f;

    static constexpr float STRIP_X = 84.0f;
    static constexpr float STRIP_Y = 132.0f;
    static constexpr float STRIP_W = 352.0f;
    static constexpr float STRIP_H = 40.0f;

    // Mode Toggles Row
    static constexpr float TOGGLE_X = 14.0f;
    static constexpr float TOGGLE_Y = 188.0f;
    static constexpr float TOGGLE_GAP = 6.0f;
    static constexpr float TOGGLE_W = (432.0f - 3.0f * 6.0f) / 4.0f; // 103.5f
    static constexpr float TOGGLE_H = 24.0f;

    // Main Control Card (3 tabs: MAIN | VOICE | STYLE)
    static constexpr float CTRL_CARD_X = 14.0f;
    static constexpr float CTRL_CARD_Y = 220.0f;
    static constexpr float CTRL_CARD_W = 432.0f;
    static constexpr float CTRL_CARD_H = 202.0f;
    static constexpr float TAB_BAR_Y = CTRL_CARD_Y;
    static constexpr float TAB_BAR_H = 22.0f;
    static constexpr float TAB_W = CTRL_CARD_W / 3.0f;

    struct PillHit {
        int index;
        float x0;
        float x1;
    };

    enum HoverZone {
        HZ_NONE = 0,
        HZ_FILE = 1,
        HZ_PATCH_PREV = 2,
        HZ_PATCH_MID = 3,
        HZ_PATCH_NEXT = 4,
        HZ_BTN_PASTE = 5,
        HZ_BTN_TXT = 6,
        HZ_BTN_CLEAR = 7,
        HZ_TEXTBOX = 8,
        HZ_BTN_RESET = 9,
        HZ_BTN_RKEY = 10,
        HZ_TOGGLE_0 = 11,
        HZ_TOGGLE_1 = 12,
        HZ_TOGGLE_2 = 13,
        HZ_TOGGLE_3 = 14,
        HZ_TAB_0 = 15,
        HZ_TAB_1 = 16,
        HZ_TAB_2 = 17
    };

    BaburEngine* engine = nullptr;
    ParamChangedCallback onParamCb = nullptr;
    void* onParamUser = nullptr;

    HWND parentHWnd = nullptr;
    HWND hWnd = nullptr;

    ID2D1Factory* pD2DFactory = nullptr;
    IDWriteFactory* pDWriteFactory = nullptr;
    ID2D1HwndRenderTarget* pRenderTarget = nullptr;
    ID2D1SolidColorBrush* pSolidBrush = nullptr;

    IDWriteTextFormat* pFormatPreset = nullptr;
    IDWriteTextFormat* pFormatChevron = nullptr;
    IDWriteTextFormat* pFormatFile = nullptr;
    IDWriteTextFormat* pFormatMicro = nullptr;
    IDWriteTextFormat* pFormatLabelLeft = nullptr;
    IDWriteTextFormat* pFormatValueRight = nullptr;
    IDWriteTextFormat* pFormatEditor = nullptr;
    IDWriteTextFormat* pFormatPillMain = nullptr;
    IDWriteTextFormat* pFormatPillSub = nullptr;

    std::vector<SliderWidget> tabSliders[3];
    std::vector<PillHit> pillHits;
    int activeTab = 0;

    int activeSliderId = -1;
    int hoverSliderId = -1;
    int hoverZone = HZ_NONE;
    int hoverPillIdx = -1;
    bool trackingMouseLeave = false;
    bool fineDragActive = false;
    int dragStartX = 0;
    float dragStartVal = 0.0f;

    // Editable Lyrics State
    bool textBoxFocused = false;
    bool textSelectedAll = false;
    std::wstring editTextW;
    std::string lastSyncedUtf8;
    size_t caretPos = 0;
    float textScrollY = 0.0f;
    uint32_t blinkTicks = 0;

    // Syllable Strip scroll state
    float stripScrollX = 0.0f;
    float stripTotalW = 0.0f;
    int lastAutoScrollSyl = -1;

    std::mutex renderMutex;

    static std::wstring utf8ToWide(const std::string& s) {
        if (s.empty()) return L"";
        int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0) return std::wstring(s.begin(), s.end());
        std::wstring out(static_cast<size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], needed);
        return out;
    }

    static std::string wideToUtf8(const std::wstring& w) {
        if (w.empty()) return "";
        int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return "";
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &out[0], needed, nullptr, nullptr);
        return out;
    }

    void syncTextFromEngine(bool force) {
        if (!engine) return;
        std::string engText = engine->getText();
        if (force || (!textBoxFocused && engText != lastSyncedUtf8)) {
            lastSyncedUtf8 = engText;
            editTextW = utf8ToWide(engText);
            caretPos = std::min(caretPos, editTextW.size());
            if (force) caretPos = editTextW.size();
            textSelectedAll = false;
        }
    }

    void pushEditTextToEngine() {
        if (!engine) return;
        std::string utf8 = wideToUtf8(editTextW);
        lastSyncedUtf8 = utf8;
        engine->setText(utf8);
        lastAutoScrollSyl = -1;
    }

    void registerWindowClass() {
        HINSTANCE hInstance = getModuleInstance();
        WNDCLASSEXW existing = {};
        existing.cbSize = sizeof(WNDCLASSEXW);
        if (!GetClassInfoExW(hInstance, L"BaburFraudizeWindow", &existing)) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WndProcStatic;
            wc.hInstance = hInstance;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"BaburFraudizeWindow";
            RegisterClassExW(&wc);
        }
    }

    HINSTANCE getModuleInstance() const {
        HMODULE hModule = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&BaburUI::WndProcStatic),
            &hModule
        );
        return hModule ? hModule : GetModuleHandle(nullptr);
    }

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        BaburUI* pThis = reinterpret_cast<BaburUI*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pThis = reinterpret_cast<BaburUI*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        return pThis ? pThis->handleMessage(hwnd, uMsg, wParam, lParam) : DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    LRESULT handleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                render();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_GETDLGCODE:
                if (textBoxFocused) return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
                return 0;
            case WM_TIMER:
                if (wParam == TIMER_ID) {
                    bool dirty = false;
                    if (engine && engine->isUiDirty()) {
                        engine->clearUiDirty();
                        syncTextFromEngine(false);
                        dirty = true;
                    }
                    if (textBoxFocused) {
                        ++blinkTicks;
                        if ((blinkTicks % 15) == 0) dirty = true;
                    }
                    if (dirty) InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_DROPFILES: {
                HDROP hDrop = reinterpret_cast<HDROP>(wParam);
                wchar_t wPath[MAX_PATH] = {};
                if (DragQueryFileW(hDrop, 0, wPath, MAX_PATH) > 0 && engine) {
                    std::string pathUtf8 = wideToUtf8(wPath);
                    handleLoadedOrDroppedPath(pathUtf8);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                DragFinish(hDrop);
                return 0;
            }
            case WM_KILLFOCUS:
                if (textBoxFocused) {
                    textBoxFocused = false;
                    textSelectedAll = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONDOWN: {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                bool shift = (wParam & MK_SHIFT) != 0;
                onMouseDown(mx, my, false, shift);
                return 0;
            }
            case WM_LBUTTONDBLCLK: {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                bool shift = (wParam & MK_SHIFT) != 0;
                onMouseDown(mx, my, true, shift);
                return 0;
            }
            case WM_RBUTTONDOWN: {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                int hz = hitTestZone(mx, my);
                if (engine && hz == HZ_BTN_RKEY) {
                    engine->setLearningResetKey(!engine->isLearningResetKey());
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (engine && hz == HZ_TOGGLE_1) {
                    openLegatoModeMenu();
                } else if (engine && hz == HZ_TOGGLE_3) {
                    openVoiceModeMenu();
                }
                return 0;
            }
            case WM_CAPTURECHANGED:
                if (activeSliderId >= 0) {
                    activeSliderId = -1;
                    fineDragActive = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_MOUSEMOVE: {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                bool shift = (wParam & MK_SHIFT) != 0;
                if (!trackingMouseLeave) {
                    TRACKMOUSEEVENT tme = {};
                    tme.cbSize = sizeof(TRACKMOUSEEVENT);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    trackingMouseLeave = true;
                }
                onMouseMove(mx, my, shift);
                return 0;
            }
            case WM_MOUSELEAVE:
                trackingMouseLeave = false;
                if (hoverSliderId != -1 || hoverZone != HZ_NONE || hoverPillIdx != -1) {
                    hoverSliderId = -1;
                    hoverZone = HZ_NONE;
                    hoverPillIdx = -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONUP:
                ReleaseCapture();
                if (activeSliderId >= 0) {
                    activeSliderId = -1;
                    fineDragActive = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case WM_MOUSEWHEEL: {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                bool shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
                onMouseWheel(pt.x, pt.y, delta, shift);
                return 0;
            }
            case WM_KEYDOWN:
                if (textBoxFocused) {
                    onEditorKeyDown(wParam);
                    return 0;
                }
                break;
            case WM_CHAR:
                if (textBoxFocused) {
                    onEditorChar(static_cast<wchar_t>(wParam));
                    return 0;
                }
                break;
            case WM_DESTROY:
                discardDeviceResources();
                return 0;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    void notifyParam(int id, float val) {
        if (!engine) return;
        engine->setParam(id, val);
        if (onParamCb) {
            onParamCb(onParamUser, id, val);
        }
    }

    void handleLoadedOrDroppedPath(const std::string& pathUtf8) {
        if (!engine || pathUtf8.empty()) return;
        std::string lower = pathUtf8;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".txt") == 0) {
            loadTextFromFile(pathUtf8);
        } else {
            engine->loadSourceFile(pathUtf8);
        }
    }

    void loadTextFromFile(const std::string& pathUtf8) {
        std::vector<uint8_t> bytes;
        if (!AudioFileLoader::readAllBytes(pathUtf8, bytes) || bytes.empty()) return;
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            offset = 3; // UTF-8 BOM
        }
        std::string txt(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
        for (char& ch : txt) {
            if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        }
        editTextW = utf8ToWide(txt);
        caretPos = editTextW.size();
        textSelectedAll = false;
        pushEditTextToEngine();
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    void pasteClipboardText() {
        if (!hWnd || !OpenClipboard(hWnd)) return;
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t* pwsz = static_cast<const wchar_t*>(GlobalLock(hData));
            if (pwsz) {
                std::wstring pasted(pwsz);
                GlobalUnlock(hData);
                for (wchar_t& ch : pasted) {
                    if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                }
                if (textSelectedAll) {
                    editTextW = pasted;
                    caretPos = editTextW.size();
                    textSelectedAll = false;
                } else {
                    editTextW.insert(caretPos, pasted);
                    caretPos += pasted.size();
                }
                pushEditTextToEngine();
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        CloseClipboard();
    }

    void copyTextToClipboard() {
        if (!hWnd || editTextW.empty() || !OpenClipboard(hWnd)) return;
        EmptyClipboard();
        size_t bytes = (editTextW.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem) {
            void* dst = GlobalLock(hMem);
            if (dst) {
                std::memcpy(dst, editTextW.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }

    void onEditorKeyDown(WPARAM vk) {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        blinkTicks = 0;

        if (ctrl && (vk == 'V' || vk == 'v')) {
            pasteClipboardText();
            return;
        }
        if (ctrl && (vk == 'C' || vk == 'c')) {
            copyTextToClipboard();
            return;
        }
        if (ctrl && (vk == 'A' || vk == 'a')) {
            textSelectedAll = true;
            caretPos = editTextW.size();
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }
        if (ctrl && (vk == 'X' || vk == 'x')) {
            copyTextToClipboard();
            editTextW.clear();
            caretPos = 0;
            textSelectedAll = false;
            pushEditTextToEngine();
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        switch (vk) {
            case VK_LEFT:
                textSelectedAll = false;
                if (caretPos > 0) --caretPos;
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
            case VK_RIGHT:
                textSelectedAll = false;
                if (caretPos < editTextW.size()) ++caretPos;
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
            case VK_HOME:
                textSelectedAll = false;
                caretPos = 0;
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
            case VK_END:
                textSelectedAll = false;
                caretPos = editTextW.size();
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
            case VK_BACK:
                if (textSelectedAll) {
                    editTextW.clear();
                    caretPos = 0;
                    textSelectedAll = false;
                    pushEditTextToEngine();
                } else if (caretPos > 0) {
                    editTextW.erase(caretPos - 1, 1);
                    --caretPos;
                    pushEditTextToEngine();
                }
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
            case VK_DELETE:
                if (textSelectedAll) {
                    editTextW.clear();
                    caretPos = 0;
                    textSelectedAll = false;
                    pushEditTextToEngine();
                } else if (caretPos < editTextW.size()) {
                    editTextW.erase(caretPos, 1);
                    pushEditTextToEngine();
                }
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
            case VK_RETURN:
            case VK_ESCAPE:
                textBoxFocused = false;
                textSelectedAll = false;
                InvalidateRect(hWnd, nullptr, FALSE);
                break;
        }
    }

    void onEditorChar(wchar_t ch) {
        if (ch < 32) return; // Ignore control chars already handled in WM_KEYDOWN
        blinkTicks = 0;
        if (textSelectedAll) {
            editTextW.clear();
            caretPos = 0;
            textSelectedAll = false;
        }
        editTextW.insert(caretPos, 1, ch);
        ++caretPos;
        pushEditTextToEngine();
        InvalidateRect(hWnd, nullptr, FALSE);
    }

    void openSourceDialog() {
        if (!engine || !hWnd) return;
        wchar_t szFile[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = hWnd;
        ofn.lpstrFilter =
            L"Audio & SoundFont (*.sf2;*.wav;*.mp3)\0*.sf2;*.wav;*.mp3\0"
            L"SoundFont Files (*.sf2)\0*.sf2\0"
            L"Audio Files (*.wav;*.mp3)\0*.wav;*.mp3\0"
            L"Text Files (*.txt)\0*.txt\0"
            L"All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Load SoundFont (.sf2) or Audio (.wav / .mp3)";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&ofn)) {
            handleLoadedOrDroppedPath(wideToUtf8(szFile));
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }

    void openTextFileDialog() {
        if (!engine || !hWnd) return;
        wchar_t szFile[MAX_PATH] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = hWnd;
        ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Load Lyrics / Sentence Text (.txt)";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&ofn)) {
            loadTextFromFile(wideToUtf8(szFile));
        }
    }

    void openPatchMenu() {
        if (!engine || !hWnd || engine->getSourceType() != SRC_SF2) return;
        int total = engine->getNumPresets();
        if (total <= 0) return;

        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        int current = engine->getCurrentPreset();
        if (total <= 30) {
            for (int i = 0; i < total; ++i) {
                std::string name = engine->getPresetName(i);
                char itemText[128];
                std::snprintf(itemText, sizeof(itemText), "%03d: %s", i, name.c_str());
                UINT flags = MF_STRING | (i == current ? MF_CHECKED : 0);
                AppendMenuA(hMenu, flags, i + 1, itemText);
            }
        } else {
            int groupSize = 20;
            int numGroups = (total + groupSize - 1) / groupSize;
            for (int g = 0; g < numGroups; ++g) {
                int start = g * groupSize;
                int end = std::min(start + groupSize - 1, total - 1);
                HMENU hSub = CreatePopupMenu();
                char groupTitle[128];
                std::snprintf(groupTitle, sizeof(groupTitle), "[%03d - %03d] %s...",
                              start, end, engine->getPresetName(start).c_str());
                for (int i = start; i <= end; ++i) {
                    std::string name = engine->getPresetName(i);
                    char itemText[128];
                    std::snprintf(itemText, sizeof(itemText), "%03d: %s", i, name.c_str());
                    UINT flags = MF_STRING | (i == current ? MF_CHECKED : 0);
                    AppendMenuA(hSub, flags, i + 1, itemText);
                }
                AppendMenuA(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSub), groupTitle);
            }
        }

        POINT pt = { static_cast<LONG>(PATCH_X), static_cast<LONG>(HDR_Y + HDR_H) };
        ClientToScreen(hWnd, &pt);

        int selectedId = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hWnd, nullptr);
        DestroyMenu(hMenu);

        if (selectedId > 0 && selectedId <= total) {
            engine->setPresetIndex(selectedId - 1);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }

    void openResetKeyMenu() {
        if (!engine || !hWnd) return;
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        bool isLearn = engine->isLearningResetKey();
        int curKey = engine->getResetMidiKey();

        AppendMenuA(hMenu, MF_STRING | (isLearn ? MF_CHECKED : 0), 1000, "MIDI Learn (Press any MIDI key...)");
        AppendMenuA(hMenu, MF_STRING | (curKey < 0 && !isLearn ? MF_CHECKED : 0), 1001, "OFF (Disabled)");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, nullptr);

        static constexpr int kPresetKeys[8] = { 12, 24, 25, 26, 36, 48, 60, 72 };
        for (int i = 0; i < 8; ++i) {
            int k = kPresetKeys[i];
            std::string label = BaburEngine::formatMidiNoteName(k);
            AppendMenuA(hMenu, MF_STRING | (curKey == k && !isLearn ? MF_CHECKED : 0), 2000 + k, label.c_str());
        }

        POINT pt = { static_cast<LONG>(RKEY_X), static_cast<LONG>(RKEY_Y + RKEY_H) };
        ClientToScreen(hWnd, &pt);

        int sel = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hWnd, nullptr);
        DestroyMenu(hMenu);

        if (sel == 1000) {
            engine->setLearningResetKey(true);
            InvalidateRect(hWnd, nullptr, FALSE);
        } else if (sel == 1001) {
            engine->setResetMidiKey(-1);
            InvalidateRect(hWnd, nullptr, FALSE);
        } else if (sel >= 2000 && sel <= 2127) {
            engine->setResetMidiKey(sel - 2000);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }

    void openLegatoModeMenu() {
        if (!engine || !hWnd) return;
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        int curMode = BaburEngine::decodeLegatoMode(engine->getParam(PARAM_LEGATO));
        AppendMenuA(hMenu, MF_STRING | (curMode == LEGATO_FLOW ? MF_CHECKED : 0), 3100, "FLOW (Continuous Phrase)");
        AppendMenuA(hMenu, MF_STRING | (curMode == LEGATO_HOLD ? MF_CHECKED : 0), 3102, "HOLD (Overlap Holds Vowel)");
        AppendMenuA(hMenu, MF_STRING | (curMode == LEGATO_STAC ? MF_CHECKED : 0), 3101, "STAC (Retrigger Per Note)");

        float x0 = TOGGLE_X + 1.0f * (TOGGLE_W + TOGGLE_GAP);
        POINT pt = { static_cast<LONG>(x0), static_cast<LONG>(TOGGLE_Y + TOGGLE_H) };
        ClientToScreen(hWnd, &pt);

        int sel = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hWnd, nullptr);
        DestroyMenu(hMenu);

        if (sel == 3100) notifyParam(PARAM_LEGATO, 0.00f);
        else if (sel == 3101) notifyParam(PARAM_LEGATO, 0.50f);
        else if (sel == 3102) notifyParam(PARAM_LEGATO, 1.00f);
        if (sel >= 3100) InvalidateRect(hWnd, nullptr, FALSE);
    }

    void openVoiceModeMenu() {
        if (!engine || !hWnd) return;
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu) return;

        int curMode = BaburEngine::decodeVoiceMode(engine->getParam(PARAM_EXCITER));
        AppendMenuA(hMenu, MF_STRING | (curMode == VOICE_NORMAL  ? MF_CHECKED : 0), 3003, "NORMAL (Full Voice)");
        AppendMenuA(hMenu, MF_STRING | (curMode == VOICE_AGGR    ? MF_CHECKED : 0), 3002, "AGGRESSIVE (Growl / Bite)");
        AppendMenuA(hMenu, MF_STRING | (curMode == VOICE_WHISPER ? MF_CHECKED : 0), 3001, "WHISPER (Breathy)");
        AppendMenuA(hMenu, MF_STRING | (curMode == VOICE_RAW     ? MF_CHECKED : 0), 3000, "RAW (Pure Instrument)");

        float x0 = TOGGLE_X + 3.0f * (TOGGLE_W + TOGGLE_GAP);
        POINT pt = { static_cast<LONG>(x0), static_cast<LONG>(TOGGLE_Y + TOGGLE_H) };
        ClientToScreen(hWnd, &pt);

        int sel = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, hWnd, nullptr);
        DestroyMenu(hMenu);

        if (sel == 3003) notifyParam(PARAM_EXCITER, 1.00f);
        else if (sel == 3002) notifyParam(PARAM_EXCITER, 0.70f);
        else if (sel == 3001) notifyParam(PARAM_EXCITER, 0.35f);
        else if (sel == 3000) notifyParam(PARAM_EXCITER, 0.00f);
        if (sel >= 3000) InvalidateRect(hWnd, nullptr, FALSE);
    }

    int hitTestZone(int mx, int my) const {
        if (my >= HDR_Y && my <= HDR_Y + HDR_H) {
            if (mx >= FILE_X && mx <= FILE_X + FILE_W) return HZ_FILE;
            if (mx >= PATCH_X && mx <= PATCH_X + PATCH_W) {
                if (engine && engine->getSourceType() == SRC_SF2) {
                    if (mx < PATCH_X + ARROW_W) return HZ_PATCH_PREV;
                    if (mx > PATCH_X + PATCH_W - ARROW_W) return HZ_PATCH_NEXT;
                }
                return HZ_PATCH_MID;
            }
        }
        if (my >= MINI_BTN_Y && my <= MINI_BTN_Y + MINI_BTN_H) {
            if (mx >= PASTE_X && mx <= PASTE_X + PASTE_W) return HZ_BTN_PASTE;
            if (mx >= TXT_X && mx <= TXT_X + TXT_W) return HZ_BTN_TXT;
            if (mx >= CLEAR_X && mx <= CLEAR_X + CLEAR_W) return HZ_BTN_CLEAR;
        }
        if (mx >= TEXTBOX_X && mx <= TEXTBOX_X + TEXTBOX_W &&
            my >= TEXTBOX_Y && my <= TEXTBOX_Y + TEXTBOX_H) {
            return HZ_TEXTBOX;
        }
        if (mx >= RESET_X && mx <= RESET_X + RESET_W &&
            my >= RESET_Y && my <= RESET_Y + RESET_H) {
            return HZ_BTN_RESET;
        }
        if (mx >= RKEY_X && mx <= RKEY_X + RKEY_W &&
            my >= RKEY_Y && my <= RKEY_Y + RKEY_H) {
            return HZ_BTN_RKEY;
        }
        if (my >= TOGGLE_Y && my <= TOGGLE_Y + TOGGLE_H) {
            for (int i = 0; i < 4; ++i) {
                float x0 = TOGGLE_X + static_cast<float>(i) * (TOGGLE_W + TOGGLE_GAP);
                if (mx >= x0 && mx <= x0 + TOGGLE_W) return HZ_TOGGLE_0 + i;
            }
        }
        if (my >= TAB_BAR_Y && my <= TAB_BAR_Y + TAB_BAR_H &&
            mx >= CTRL_CARD_X && mx <= CTRL_CARD_X + CTRL_CARD_W) {
            int t = static_cast<int>((static_cast<float>(mx) - CTRL_CARD_X) / TAB_W);
            return HZ_TAB_0 + std::clamp(t, 0, 2);
        }
        return HZ_NONE;
    }

    int hitTestPill(int mx, int my) const {
        if (mx < STRIP_X || mx > STRIP_X + STRIP_W || my < STRIP_Y || my > STRIP_Y + STRIP_H) return -1;
        for (const auto& ph : pillHits) {
            if (mx >= ph.x0 && mx <= ph.x1) return ph.index;
        }
        return -1;
    }

    int hitTestSlider(int mx, int my) const {
        for (const auto& s : tabSliders[activeTab]) {
            if (mx >= s.x - 14.0f && mx <= s.x + s.w + 14.0f &&
                my >= s.y - 17.0f && my <= s.y + s.h + 13.0f) {
                return s.paramId;
            }
        }
        return -1;
    }

    const SliderWidget* findSlider(int paramId) const {
        for (const auto& s : tabSliders[activeTab]) {
            if (s.paramId == paramId) return &s;
        }
        return nullptr;
    }

    static float getDefaultParam(int id) {
        switch (id) {
            case PARAM_CHARACTER:   return 0.35f;
            case PARAM_DICTION:     return 0.65f;
            case PARAM_THROAT:      return 0.50f;
            case PARAM_GLIDE:       return 0.25f;
            case PARAM_GENDER:      return 0.50f;
            case PARAM_PITCH_SHIFT: return 0.50f;
            case PARAM_BREATHINESS: return 0.00f;
            case PARAM_GRIT:        return 0.00f;
            case PARAM_NASALITY:    return 0.00f;
            case PARAM_BRIGHTNESS:  return 0.50f;
            case PARAM_VIB_DEPTH:   return 0.25f;
            case PARAM_VIB_RATE:    return (5.2f - 2.5f) / 6.5f;
            case PARAM_TREMOR:      return 0.00f;
            case PARAM_ROBOTIC:     return 0.00f;
            default: return 0.50f;
        }
    }

    static const wchar_t* getSliderTitle(int id) {
        switch (id) {
            case PARAM_CHARACTER:   return L"CHARACTER  (SING \u2194 TALKBOX)";
            case PARAM_DICTION:     return L"DICTION  (VOWEL \u2194 CONSONANT)";
            case PARAM_THROAT:      return L"THROAT SIZE  (FORMANT)";
            case PARAM_GLIDE:       return L"GLIDE  (SPEED)";
            case PARAM_GENDER:      return L"GENDER / AGE  (BASS \u2194 FEM / CHILD)";
            case PARAM_PITCH_SHIFT: return L"PITCH SHIFT  (\u00B124 ST)";
            case PARAM_BREATHINESS: return L"BREATHINESS  (AIR / WHISPER)";
            case PARAM_GRIT:        return L"GRIT / FRY  (RASP / GROWL)";
            case PARAM_NASALITY:    return L"NASALITY  (TWANG / VELUM)";
            case PARAM_BRIGHTNESS:  return L"BRIGHTNESS  (DARK \u2194 FORWARD)";
            case PARAM_VIB_DEPTH:   return L"VIB DEPTH";
            case PARAM_VIB_RATE:    return L"VIB RATE";
            case PARAM_TREMOR:      return L"AGE / TREMOR";
            case PARAM_ROBOTIC:     return L"ROBOTIC";
            default: return L"";
        }
    }

    void placeCaretAtClick(int mx, int my) {
        if (!pDWriteFactory || !pFormatEditor) {
            caretPos = editTextW.size();
            return;
        }
        const std::wstring& drawStr = editTextW.empty() ? std::wstring(L" ") : editTextW;
        IDWriteTextLayout* pLayout = nullptr;
        if (SUCCEEDED(pDWriteFactory->CreateTextLayout(
                drawStr.c_str(), static_cast<UINT32>(drawStr.size()),
                pFormatEditor, TEXTBOX_W - 14.0f, 1000.0f, &pLayout)) && pLayout) {
            BOOL isTrailing = FALSE;
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS htm{};
            float localX = static_cast<float>(mx) - (TEXTBOX_X + 7.0f);
            float localY = static_cast<float>(my) - (TEXTBOX_Y + 5.0f) + textScrollY;
            if (SUCCEEDED(pLayout->HitTestPoint(localX, localY, &isTrailing, &isInside, &htm))) {
                size_t pos = static_cast<size_t>(htm.textPosition + (isTrailing ? 1 : 0));
                caretPos = std::min(pos, editTextW.size());
            }
            pLayout->Release();
        }
    }

    void onMouseDown(int mx, int my, bool isDoubleClick, bool isShift) {
        if (!engine) return;

        int zone = hitTestZone(mx, my);

        // Textbox click focus
        if (zone == HZ_TEXTBOX) {
            textBoxFocused = true;
            SetFocus(hWnd);
            blinkTicks = 0;
            if (isDoubleClick) {
                textSelectedAll = true;
                caretPos = editTextW.size();
            } else {
                textSelectedAll = false;
                placeCaretAtClick(mx, my);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        if (textBoxFocused) {
            textBoxFocused = false;
            textSelectedAll = false;
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        if (zone == HZ_BTN_RKEY && isDoubleClick) {
            engine->setLearningResetKey(!engine->isLearningResetKey());
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }
        if (zone >= HZ_TAB_0 && zone <= HZ_TAB_2) {
            activeTab = zone - HZ_TAB_0;
            activeSliderId = -1;
            hoverSliderId = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        if (zone > HZ_NONE && !isDoubleClick) {
            if (zone == HZ_FILE) {
                openSourceDialog();
                return;
            }
            if (zone == HZ_PATCH_PREV || zone == HZ_PATCH_MID || zone == HZ_PATCH_NEXT) {
                int sType = engine->getSourceType();
                if (sType == SRC_SF2) {
                    int n = engine->getNumPresets();
                    if (n > 0) {
                        if (zone == HZ_PATCH_PREV) {
                            int p = (engine->getCurrentPreset() - 1 + n) % n;
                            engine->setPresetIndex(p);
                        } else if (zone == HZ_PATCH_NEXT) {
                            int p = (engine->getCurrentPreset() + 1) % n;
                            engine->setPresetIndex(p);
                        } else {
                            openPatchMenu();
                        }
                        InvalidateRect(hWnd, nullptr, FALSE);
                    }
                } else if (sType == SRC_NONE) {
                    openSourceDialog();
                }
                return;
            }
            if (zone == HZ_BTN_PASTE) {
                pasteClipboardText();
                return;
            }
            if (zone == HZ_BTN_TXT) {
                openTextFileDialog();
                return;
            }
            if (zone == HZ_BTN_CLEAR) {
                editTextW.clear();
                caretPos = 0;
                textSelectedAll = false;
                pushEditTextToEngine();
                InvalidateRect(hWnd, nullptr, FALSE);
                return;
            }
            if (zone == HZ_BTN_RESET) {
                engine->resetSyllableCursor();
                lastAutoScrollSyl = -1;
                InvalidateRect(hWnd, nullptr, FALSE);
                return;
            }
            if (zone == HZ_BTN_RKEY) {
                openResetKeyMenu();
                return;
            }
            if (zone == HZ_TOGGLE_0 || zone == HZ_TOGGLE_2) {
                int pid = (zone == HZ_TOGGLE_0) ? PARAM_LANG : PARAM_AUDIO_MODE;
                float nextVal = (engine->getParam(pid) >= 0.5f) ? 0.0f : 1.0f;
                notifyParam(pid, nextVal);
                InvalidateRect(hWnd, nullptr, FALSE);
                return;
            }
            if (zone == HZ_TOGGLE_1) {
                int curMode = BaburEngine::decodeLegatoMode(engine->getParam(PARAM_LEGATO));
                float nextVal = 0.00f;
                if (curMode == LEGATO_HOLD) nextVal = 0.00f;
                else if (curMode == LEGATO_FLOW) nextVal = 0.50f;
                else nextVal = 1.00f;
                notifyParam(PARAM_LEGATO, nextVal);
                InvalidateRect(hWnd, nullptr, FALSE);
                return;
            }
            if (zone == HZ_TOGGLE_3) {
                int curMode = BaburEngine::decodeVoiceMode(engine->getParam(PARAM_EXCITER));
                float nextVal = 1.00f;
                if (curMode == VOICE_NORMAL) nextVal = 0.70f;
                else if (curMode == VOICE_AGGR) nextVal = 0.35f;
                else if (curMode == VOICE_WHISPER) nextVal = 0.00f;
                else nextVal = 1.00f;
                notifyParam(PARAM_EXCITER, nextVal);
                InvalidateRect(hWnd, nullptr, FALSE);
                return;
            }
        }

        int pillIdx = hitTestPill(mx, my);
        if (pillIdx >= 0 && !isDoubleClick) {
            engine->setCurrentSyllableIndex(pillIdx);
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        int sid = hitTestSlider(mx, my);
        if (sid >= 0) {
            const SliderWidget* s = findSlider(sid);
            if (!s) return;
            if (isDoubleClick) {
                notifyParam(sid, getDefaultParam(sid));
                InvalidateRect(hWnd, nullptr, FALSE);
                return;
            }
            activeSliderId = sid;
            SetCapture(hWnd);
            fineDragActive = isShift;
            dragStartX = mx;
            if (isShift) {
                dragStartVal = engine->getParam(sid);
            } else {
                float newVal = std::clamp((static_cast<float>(mx) - s->x) / s->w, 0.0f, 1.0f);
                dragStartVal = newVal;
                notifyParam(sid, newVal);
            }
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }

    void onMouseMove(int mx, int my, bool isShift) {
        if (!engine) return;

        if (activeSliderId >= 0) {
            const SliderWidget* s = findSlider(activeSliderId);
            if (!s) return;
            float newVal;
            if (isShift || fineDragActive) {
                if (!fineDragActive) {
                    fineDragActive = true;
                    dragStartX = mx;
                    dragStartVal = engine->getParam(activeSliderId);
                }
                float delta = static_cast<float>(mx - dragStartX) / (s->w * 8.0f);
                newVal = std::clamp(dragStartVal + delta, 0.0f, 1.0f);
            } else {
                newVal = std::clamp((static_cast<float>(mx) - s->x) / s->w, 0.0f, 1.0f);
            }
            notifyParam(activeSliderId, newVal);
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        int newHoverSlider = hitTestSlider(mx, my);
        int newHoverZone = hitTestZone(mx, my);
        int newHoverPill = hitTestPill(mx, my);
        if (newHoverSlider != hoverSliderId || newHoverZone != hoverZone || newHoverPill != hoverPillIdx) {
            hoverSliderId = newHoverSlider;
            hoverZone = newHoverZone;
            hoverPillIdx = newHoverPill;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }

    void onMouseWheel(int mx, int my, int delta, bool isShift) {
        if (!engine) return;

        if (mx >= STRIP_X && mx <= STRIP_X + STRIP_W && my >= STRIP_Y && my <= STRIP_Y + STRIP_H) {
            float maxScroll = std::max(0.0f, stripTotalW - STRIP_W);
            stripScrollX = std::clamp(stripScrollX - (delta > 0 ? 48.0f : -48.0f), 0.0f, maxScroll);
            InvalidateRect(hWnd, nullptr, FALSE);
            return;
        }

        int zone = hitTestZone(mx, my);
        if (zone == HZ_PATCH_PREV || zone == HZ_PATCH_MID || zone == HZ_PATCH_NEXT) {
            if (engine->getSourceType() == SRC_SF2) {
                int n = engine->getNumPresets();
                if (n > 0) {
                    int cur = engine->getCurrentPreset();
                    int next = (delta < 0) ? ((cur + 1) % n) : ((cur - 1 + n) % n);
                    engine->setPresetIndex(next);
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
            return;
        }

        int sid = hitTestSlider(mx, my);
        if (sid >= 0) {
            float dir = (delta > 0) ? 1.0f : -1.0f;
            float step = (sid == PARAM_THROAT)
                         ? (dir * (isShift ? (0.1f / 24.0f) : (1.0f / 24.0f)))
                         : ((sid == PARAM_PITCH_SHIFT)
                            ? (dir * (isShift ? (0.1f / 48.0f) : (1.0f / 48.0f)))
                            : (dir * (isShift ? 0.004f : 0.025f)));
            float cur = engine->getParam(sid);
            notifyParam(sid, std::clamp(cur + step, 0.0f, 1.0f));
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    }

    void initLayout() {
        for (int t = 0; t < 3; ++t) tabSliders[t].clear();
        // Tab 0: MAIN
        tabSliders[0].push_back({ PARAM_CHARACTER, 28.0f, 266.0f, 404.0f, 8.0f });
        tabSliders[0].push_back({ PARAM_DICTION,   28.0f, 308.0f, 404.0f, 8.0f });
        tabSliders[0].push_back({ PARAM_THROAT,    28.0f, 350.0f, 404.0f, 8.0f });
        tabSliders[0].push_back({ PARAM_GLIDE,     28.0f, 392.0f, 404.0f, 8.0f });
        // Tab 1: VOICE (Anatomy & Phonation)
        tabSliders[1].push_back({ PARAM_GENDER,      28.0f, 266.0f, 404.0f, 8.0f });
        tabSliders[1].push_back({ PARAM_PITCH_SHIFT, 28.0f, 308.0f, 404.0f, 8.0f });
        tabSliders[1].push_back({ PARAM_BREATHINESS, 28.0f, 350.0f, 404.0f, 8.0f });
        tabSliders[1].push_back({ PARAM_GRIT,        28.0f, 392.0f, 404.0f, 8.0f });
        // Tab 2: STYLE (Color, Vibrato & Tremor / Robotic)
        tabSliders[2].push_back({ PARAM_NASALITY,   28.0f,  266.0f, 404.0f, 8.0f });
        tabSliders[2].push_back({ PARAM_BRIGHTNESS, 28.0f,  308.0f, 404.0f, 8.0f });
        tabSliders[2].push_back({ PARAM_VIB_DEPTH,  28.0f,  350.0f, 190.0f, 8.0f });
        tabSliders[2].push_back({ PARAM_VIB_RATE,   242.0f, 350.0f, 190.0f, 8.0f });
        tabSliders[2].push_back({ PARAM_TREMOR,     28.0f,  392.0f, 190.0f, 8.0f });
        tabSliders[2].push_back({ PARAM_ROBOTIC,    242.0f, 392.0f, 190.0f, 8.0f });
    }

    void initDirect2D() {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&pDWriteFactory));
        UITheme::initTextFormats(pDWriteFactory, &pFormatPreset, &pFormatChevron);

        if (pDWriteFactory) {
            auto makeFmt = [&](float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                               bool noWrap, IDWriteTextFormat** ppFmt) {
                pDWriteFactory->CreateTextFormat(
                    L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", ppFmt
                );
                if (*ppFmt) {
                    (*ppFmt)->SetTextAlignment(align);
                    (*ppFmt)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    if (noWrap) {
                        (*ppFmt)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
                        IDWriteInlineObject* ellipsis = nullptr;
                        if (SUCCEEDED(pDWriteFactory->CreateEllipsisTrimmingSign(*ppFmt, &ellipsis)) && ellipsis) {
                            (*ppFmt)->SetTrimming(&trimming, ellipsis);
                            ellipsis->Release();
                        }
                    }
                }
            };

            makeFmt(10.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, true, &pFormatFile);
            makeFmt(8.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, true, &pFormatMicro);
            makeFmt(8.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, true, &pFormatLabelLeft);
            makeFmt(8.5f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_TRAILING, true, &pFormatValueRight);
            makeFmt(10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, true, &pFormatPillMain);
            makeFmt(8.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, true, &pFormatPillSub);

            pDWriteFactory->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &pFormatEditor
            );
            if (pFormatEditor) {
                pFormatEditor->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                pFormatEditor->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                pFormatEditor->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            }
        }
    }

    void cleanupDirect2D() {
        discardDeviceResources();
        auto rel = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
        rel(pFormatPillSub);
        rel(pFormatPillMain);
        rel(pFormatEditor);
        rel(pFormatValueRight);
        rel(pFormatLabelLeft);
        rel(pFormatMicro);
        rel(pFormatFile);
        rel(pFormatChevron);
        rel(pFormatPreset);
        rel(pDWriteFactory);
        rel(pD2DFactory);
    }

    void createDeviceResources() {
        if (!pRenderTarget && hWnd && pD2DFactory) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            D2D1_SIZE_U size = D2D1::SizeU(
                std::max<UINT32>(1, static_cast<UINT32>(rc.right - rc.left)),
                std::max<UINT32>(1, static_cast<UINT32>(rc.bottom - rc.top))
            );
            D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );
            D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
                hWnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY
            );
            if (SUCCEEDED(pD2DFactory->CreateHwndRenderTarget(rtProps, hwndProps, &pRenderTarget))) {
                pRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                pRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
                pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pSolidBrush);
            }
        }
    }

    void discardDeviceResources() {
        if (pSolidBrush) { pSolidBrush->Release(); pSolidBrush = nullptr; }
        if (pRenderTarget) { pRenderTarget->Release(); pRenderTarget = nullptr; }
    }

    void setBrush(const D2DColor& c) {
        if (pSolidBrush) pSolidBrush->SetColor(c.toD2D());
    }

    void drawText(const wchar_t* text, float x, float y, float w, float h, IDWriteTextFormat* fmt) {
        if (!pRenderTarget || !pSolidBrush || !fmt || !text) return;
        D2D1_RECT_F r = D2D1::RectF(x, y, x + w, y + h);
        pRenderTarget->DrawText(text, static_cast<UINT32>(wcslen(text)), fmt, r, pSolidBrush);
    }

    void render() {
        std::lock_guard<std::mutex> lock(renderMutex);
        if (!pRenderTarget) {
            createDeviceResources();
            if (!pRenderTarget) return;
        }

        pRenderTarget->BeginDraw();

        // 1. Matte Slate Chassis
        setBrush(Colors::BgChassis);
        pRenderTarget->FillRectangle(D2D1::RectF(0, 0, UI_WIDTH, UI_HEIGHT), pSolidBrush);
        setBrush(Colors::BorderChassis);
        pRenderTarget->DrawRectangle(D2D1::RectF(0.5f, 0.5f, UI_WIDTH - 0.5f, UI_HEIGHT - 0.5f), pSolidBrush, 1.0f);

        // 2. Top Header Bar
        setBrush(Colors::BgHeader);
        pRenderTarget->FillRectangle(D2D1::RectF(1.0f, 1.0f, UI_WIDTH - 1.0f, 48.0f), pSolidBrush);
        setBrush(Colors::BorderCard);
        pRenderTarget->DrawLine(D2D1::Point2F(1.0f, 48.0f), D2D1::Point2F(UI_WIDTH - 1.0f, 48.0f), pSolidBrush, 1.0f);
        drawHeaderBar();

        // 3. Lyrics & Live Syllable Card
        setBrush(Colors::BgCard);
        pRenderTarget->FillRectangle(D2D1::RectF(LYRICS_CARD_X, LYRICS_CARD_Y, LYRICS_CARD_X + LYRICS_CARD_W, LYRICS_CARD_Y + LYRICS_CARD_H), pSolidBrush);
        setBrush(Colors::BorderCard);
        pRenderTarget->DrawRectangle(D2D1::RectF(LYRICS_CARD_X + 0.5f, LYRICS_CARD_Y + 0.5f, LYRICS_CARD_X + LYRICS_CARD_W - 0.5f, LYRICS_CARD_Y + LYRICS_CARD_H - 0.5f), pSolidBrush, 1.0f);
        drawLyricsCard();

        // 4. Mode Toggles Row
        drawTogglesRow();

        // 5. Recessed 3-Tab Control Card (MAIN | VOICE | STYLE)
        setBrush(Colors::BgCard);
        pRenderTarget->FillRectangle(D2D1::RectF(CTRL_CARD_X, CTRL_CARD_Y, CTRL_CARD_X + CTRL_CARD_W, CTRL_CARD_Y + CTRL_CARD_H), pSolidBrush);
        drawControlTabs();
        setBrush(Colors::BorderCard);
        pRenderTarget->DrawRectangle(D2D1::RectF(CTRL_CARD_X + 0.5f, CTRL_CARD_Y + 0.5f, CTRL_CARD_X + CTRL_CARD_W - 0.5f, CTRL_CARD_Y + CTRL_CARD_H - 0.5f), pSolidBrush, 1.0f);

        for (const auto& s : tabSliders[activeTab]) {
            drawSlider(s);
        }

        HRESULT hr = pRenderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            discardDeviceResources();
        }
    }

    void drawHeaderBar() {
        if (!engine) return;
        int sType = engine->getSourceType();
        bool loaded = (sType != SRC_NONE);

        // Left: LOAD SF2 / AUDIO button
        D2D1_RECT_F rFile = D2D1::RectF(FILE_X, HDR_Y, FILE_X + FILE_W, HDR_Y + HDR_H);
        setBrush(loaded ? Colors::BgCard : Colors::ButtonInactiveBg);
        pRenderTarget->FillRectangle(rFile, pSolidBrush);
        setBrush((hoverZone == HZ_FILE) ? Colors::BorderChassis : (loaded ? Colors::BorderCard : Colors::ButtonInactiveBorder));
        pRenderTarget->DrawRectangle(D2D1::RectF(FILE_X + 0.5f, HDR_Y + 0.5f, FILE_X + FILE_W - 0.5f, HDR_Y + HDR_H - 0.5f), pSolidBrush, 1.0f);

        std::string fileLabel;
        if (sType == SRC_SF2) fileLabel = "SF2: " + engine->getSourceName();
        else if (sType == SRC_AUDIO) fileLabel = "AUD: " + engine->getSourceName();
        else fileLabel = "LOAD SF2 / AUDIO";

        std::wstring wFile = utf8ToWide(fileLabel);
        setBrush((hoverZone == HZ_FILE) ? Colors::TextBright : (loaded ? Colors::TextActive : Colors::TextMuted));
        drawText(wFile.c_str(), FILE_X + 6.0f, HDR_Y, FILE_W - 12.0f, HDR_H, pFormatFile ? pFormatFile : pFormatPreset);

        // Right: Preset Selector (< | Patch v | >) or Audio Root badge
        setBrush(Colors::BgCard);
        pRenderTarget->FillRectangle(D2D1::RectF(PATCH_X, HDR_Y, PATCH_X + PATCH_W, HDR_Y + HDR_H), pSolidBrush);
        bool hoverPatch = (hoverZone >= HZ_PATCH_PREV && hoverZone <= HZ_PATCH_NEXT);
        setBrush(hoverPatch ? Colors::BorderChassis : Colors::BorderCard);
        pRenderTarget->DrawRectangle(D2D1::RectF(PATCH_X + 0.5f, HDR_Y + 0.5f, PATCH_X + PATCH_W - 0.5f, HDR_Y + HDR_H - 0.5f), pSolidBrush, 1.0f);

        if (sType == SRC_SF2) {
            setBrush(Colors::BorderCard);
            pRenderTarget->DrawLine(D2D1::Point2F(PATCH_X + ARROW_W, HDR_Y + 5.0f),
                                    D2D1::Point2F(PATCH_X + ARROW_W, HDR_Y + HDR_H - 5.0f), pSolidBrush, 1.0f);
            pRenderTarget->DrawLine(D2D1::Point2F(PATCH_X + PATCH_W - ARROW_W, HDR_Y + 5.0f),
                                    D2D1::Point2F(PATCH_X + PATCH_W - ARROW_W, HDR_Y + HDR_H - 5.0f), pSolidBrush, 1.0f);

            setBrush((hoverZone == HZ_PATCH_PREV) ? Colors::TextBright : Colors::TextMuted);
            drawText(L"<", PATCH_X, HDR_Y, ARROW_W, HDR_H, pFormatChevron);

            std::string disp = (engine->getNumPresets() > 0)
                               ? (engine->getPresetName(engine->getCurrentPreset()) + "   v")
                               : "No Preset";
            std::wstring wDisp = utf8ToWide(disp);
            setBrush((hoverZone == HZ_PATCH_MID) ? Colors::TextBright : Colors::TextActive);
            drawText(wDisp.c_str(), PATCH_X + ARROW_W + 4.0f, HDR_Y, PATCH_W - 2.0f * ARROW_W - 8.0f, HDR_H, pFormatPreset);

            setBrush((hoverZone == HZ_PATCH_NEXT) ? Colors::TextBright : Colors::TextMuted);
            drawText(L">", PATCH_X + PATCH_W - ARROW_W, HDR_Y, ARROW_W, HDR_H, pFormatChevron);
        } else if (sType == SRC_AUDIO) {
            setBrush(Colors::BadgeAudio);
            drawText(L"AUDIO ROOT: A (MIDI 69)", PATCH_X + 6.0f, HDR_Y, PATCH_W - 12.0f, HDR_H, pFormatPreset);
        } else {
            setBrush((hoverZone == HZ_PATCH_MID) ? Colors::TextActive : Colors::TextDim);
            drawText(L"Drop .sf2 / .wav / .mp3 here", PATCH_X + 6.0f, HDR_Y, PATCH_W - 12.0f, HDR_H, pFormatFile);
        }
    }

    void drawMiniButton(float x, float y, float w, float h, const wchar_t* label, int zoneId) {
        bool hov = (hoverZone == zoneId);
        setBrush(hov ? Colors::ButtonInactiveBorder : Colors::BgHeader);
        pRenderTarget->FillRectangle(D2D1::RectF(x, y, x + w, y + h), pSolidBrush);
        setBrush(hov ? Colors::TextMuted : Colors::BorderCard);
        pRenderTarget->DrawRectangle(D2D1::RectF(x + 0.5f, y + 0.5f, x + w - 0.5f, y + h - 0.5f), pSolidBrush, 1.0f);
        setBrush(hov ? Colors::TextBright : Colors::TextMuted);
        drawText(label, x, y, w, h, pFormatMicro);
    }

    void drawLyricsCard() {
        // Card header label & mini utility buttons
        setBrush(Colors::TextMuted);
        drawText(L"LYRICS / SENTENCE INPUT", TEXTBOX_X, MINI_BTN_Y, 220.0f, MINI_BTN_H, pFormatLabelLeft);

        drawMiniButton(PASTE_X, MINI_BTN_Y, PASTE_W, MINI_BTN_H, L"PASTE", HZ_BTN_PASTE);
        drawMiniButton(TXT_X, MINI_BTN_Y, TXT_W, MINI_BTN_H, L"LOAD .TXT", HZ_BTN_TXT);
        drawMiniButton(CLEAR_X, MINI_BTN_Y, CLEAR_W, MINI_BTN_H, L"CLEAR", HZ_BTN_CLEAR);

        // Editable Text Box
        D2D1_RECT_F tbRect = D2D1::RectF(TEXTBOX_X, TEXTBOX_Y, TEXTBOX_X + TEXTBOX_W, TEXTBOX_Y + TEXTBOX_H);
        setBrush(Colors::BgHeader);
        pRenderTarget->FillRectangle(tbRect, pSolidBrush);
        setBrush(textBoxFocused ? Colors::TextActive : ((hoverZone == HZ_TEXTBOX) ? Colors::ButtonInactiveBorder : Colors::BorderChassis));
        pRenderTarget->DrawRectangle(D2D1::RectF(TEXTBOX_X + 0.5f, TEXTBOX_Y + 0.5f, TEXTBOX_X + TEXTBOX_W - 0.5f, TEXTBOX_Y + TEXTBOX_H - 0.5f),
                                     pSolidBrush, textBoxFocused ? 1.25f : 1.0f);

        pRenderTarget->PushAxisAlignedClip(D2D1::RectF(TEXTBOX_X + 2.0f, TEXTBOX_Y + 2.0f, TEXTBOX_X + TEXTBOX_W - 2.0f, TEXTBOX_Y + TEXTBOX_H - 2.0f),
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (editTextW.empty() && !textBoxFocused) {
            setBrush(Colors::TextDim);
            drawText(L"Click to type or paste English / Turkish lyrics...", TEXTBOX_X + 7.0f, TEXTBOX_Y + 4.0f, TEXTBOX_W - 14.0f, TEXTBOX_H - 8.0f, pFormatEditor);
        } else if (pDWriteFactory && pFormatEditor) {
            const std::wstring& drawStr = editTextW.empty() ? std::wstring(L"") : editTextW;
            IDWriteTextLayout* pLayout = nullptr;
            if (SUCCEEDED(pDWriteFactory->CreateTextLayout(
                    drawStr.c_str(), static_cast<UINT32>(drawStr.size()),
                    pFormatEditor, TEXTBOX_W - 14.0f, 1000.0f, &pLayout)) && pLayout) {
                float cx = 0.0f, cy = 0.0f;
                DWRITE_HIT_TEST_METRICS cMetrics{};
                pLayout->HitTestTextPosition(static_cast<UINT32>(std::min(caretPos, drawStr.size())), FALSE, &cx, &cy, &cMetrics);
                float lineH = std::max(14.0f, cMetrics.height);
                float visibleH = TEXTBOX_H - 10.0f;
                if (cy < textScrollY) textScrollY = cy;
                else if (cy + lineH > textScrollY + visibleH) textScrollY = (cy + lineH) - visibleH;

                if (textSelectedAll && !drawStr.empty()) {
                    setBrush(D2DColor(0.24f, 0.29f, 0.38f, 0.75f));
                    pRenderTarget->FillRectangle(D2D1::RectF(TEXTBOX_X + 5.0f, TEXTBOX_Y + 4.0f, TEXTBOX_X + TEXTBOX_W - 5.0f, TEXTBOX_Y + TEXTBOX_H - 4.0f), pSolidBrush);
                }

                setBrush(Colors::TextActive);
                pRenderTarget->DrawTextLayout(D2D1::Point2F(TEXTBOX_X + 7.0f, TEXTBOX_Y + 5.0f - textScrollY), pLayout, pSolidBrush);

                if (textBoxFocused && ((blinkTicks / 15) % 2 == 0)) {
                    setBrush(Colors::TextBright);
                    float caretX = TEXTBOX_X + 7.0f + cx;
                    float caretY = TEXTBOX_Y + 5.0f - textScrollY + cy;
                    pRenderTarget->DrawLine(D2D1::Point2F(caretX, caretY + 1.0f),
                                            D2D1::Point2F(caretX, caretY + lineH - 1.0f), pSolidBrush, 1.5f);
                }
                pLayout->Release();
            }
        }
        pRenderTarget->PopAxisAlignedClip();

        // Reset Button
        bool hovReset = (hoverZone == HZ_BTN_RESET);
        setBrush(hovReset ? Colors::ButtonInactiveBorder : Colors::ButtonInactiveBg);
        pRenderTarget->FillRectangle(D2D1::RectF(RESET_X, RESET_Y, RESET_X + RESET_W, RESET_Y + RESET_H), pSolidBrush);
        setBrush(hovReset ? Colors::TextMuted : Colors::ButtonInactiveBorder);
        pRenderTarget->DrawRectangle(D2D1::RectF(RESET_X + 0.5f, RESET_Y + 0.5f, RESET_X + RESET_W - 0.5f, RESET_Y + RESET_H - 0.5f), pSolidBrush, 1.0f);
        setBrush(hovReset ? Colors::TextBright : Colors::TextActive);
        drawText(L"<< RST", RESET_X, RESET_Y, RESET_W, RESET_H, pFormatMicro);

        // Silent Reset MIDI Keyswitch Button
        bool isLearn = engine && engine->isLearningResetKey();
        int rKey = engine ? engine->getResetMidiKey() : -1;
        bool hovRKey = (hoverZone == HZ_BTN_RKEY);
        if (isLearn) {
            setBrush(Colors::ButtonActiveBg);
            pRenderTarget->FillRectangle(D2D1::RectF(RKEY_X, RKEY_Y, RKEY_X + RKEY_W, RKEY_Y + RKEY_H), pSolidBrush);
            setBrush(Colors::ButtonActiveText);
            drawText(L"LEARN..", RKEY_X, RKEY_Y, RKEY_W, RKEY_H, pFormatMicro);
        } else {
            setBrush(hovRKey ? Colors::ButtonInactiveBorder : Colors::BgHeader);
            pRenderTarget->FillRectangle(D2D1::RectF(RKEY_X, RKEY_Y, RKEY_X + RKEY_W, RKEY_Y + RKEY_H), pSolidBrush);
            setBrush(rKey >= 0 ? Colors::BadgeMidi : (hovRKey ? Colors::TextMuted : Colors::BorderCard));
            pRenderTarget->DrawRectangle(D2D1::RectF(RKEY_X + 0.5f, RKEY_Y + 0.5f, RKEY_X + RKEY_W - 0.5f, RKEY_Y + RKEY_H - 0.5f), pSolidBrush, 1.0f);
            std::string kLbl = "KEY: OFF";
            if (rKey >= 0) {
                static constexpr const char* kNotes[12] = {
                    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
                };
                char b[16];
                std::snprintf(b, sizeof(b), "KEY:%s%d", kNotes[rKey % 12], (rKey / 12) - 1);
                kLbl = b;
            }
            std::wstring wKey = utf8ToWide(kLbl);
            setBrush(hovRKey ? Colors::TextBright : (rKey >= 0 ? Colors::TextActive : Colors::TextMuted));
            drawText(wKey.c_str(), RKEY_X, RKEY_Y, RKEY_W, RKEY_H, pFormatMicro);
        }

        // Live Syllable Strip
        drawSyllableStrip();
    }

    void drawSyllableStrip() {
        if (!engine) return;
        std::vector<SyllableToken> sylls = engine->getSyllablesCopy();
        int curIdx = engine->getCurrentSyllableIndex();
        int nextIdx = engine->getNextSyllableIndex();

        // Measure total widths
        std::vector<float> widths(sylls.size(), 40.0f);
        float totalW = 0.0f;
        constexpr float kPillGap = 5.0f;
        for (size_t i = 0; i < sylls.size(); ++i) {
            std::wstring wDisp = utf8ToWide(sylls[i].display);
            std::wstring wHint = utf8ToWide(sylls[i].phoneticHint);
            float chars = static_cast<float>(std::max(wDisp.size(), wHint.size()));
            widths[i] = std::max(38.0f, chars * 8.0f + 16.0f);
            totalW += widths[i] + ((i + 1 < sylls.size()) ? kPillGap : 0.0f);
        }
        stripTotalW = totalW;

        // Auto-scroll active syllable into view when it changes
        if (curIdx != lastAutoScrollSyl && curIdx >= 0 && curIdx < static_cast<int>(sylls.size())) {
            lastAutoScrollSyl = curIdx;
            float pillStart = 0.0f;
            for (int i = 0; i < curIdx; ++i) pillStart += widths[static_cast<size_t>(i)] + kPillGap;
            float pillEnd = pillStart + widths[static_cast<size_t>(curIdx)];
            float maxScroll = std::max(0.0f, stripTotalW - STRIP_W);
            if (pillStart < stripScrollX + 12.0f) {
                stripScrollX = std::max(0.0f, pillStart - 24.0f);
            } else if (pillEnd > stripScrollX + STRIP_W - 12.0f) {
                stripScrollX = std::min(maxScroll, pillEnd - STRIP_W + 24.0f);
            }
        }
        stripScrollX = std::clamp(stripScrollX, 0.0f, std::max(0.0f, stripTotalW - STRIP_W));

        pRenderTarget->PushAxisAlignedClip(D2D1::RectF(STRIP_X, STRIP_Y, STRIP_X + STRIP_W, STRIP_Y + STRIP_H),
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        pillHits.clear();
        float xCursor = STRIP_X - stripScrollX;
        for (size_t i = 0; i < sylls.size(); ++i) {
            float pw = widths[i];
            float x0 = xCursor;
            float x1 = x0 + pw;
            xCursor = x1 + kPillGap;

            if (x1 < STRIP_X || x0 > STRIP_X + STRIP_W) continue;
            pillHits.push_back({ static_cast<int>(i), std::max(STRIP_X, x0), std::min(STRIP_X + STRIP_W, x1) });

            bool isCur = (static_cast<int>(i) == curIdx);
            bool isNext = (static_cast<int>(i) == nextIdx && nextIdx != curIdx);
            bool isHov = (hoverPillIdx == static_cast<int>(i));

            D2D1_ROUNDED_RECT pillR = D2D1::RoundedRect(
                D2D1::RectF(x0, STRIP_Y + 3.0f, x1, STRIP_Y + STRIP_H - 3.0f), 3.0f, 3.0f
            );

            if (isCur) {
                setBrush(Colors::ButtonActiveBg);
                pRenderTarget->FillRoundedRectangle(pillR, pSolidBrush);
            } else {
                setBrush(isHov ? Colors::ButtonInactiveBg : Colors::BgHeader);
                pRenderTarget->FillRoundedRectangle(pillR, pSolidBrush);
                if (sylls[i].isEmphasized) {
                    setBrush(Colors::TextBright);
                    pRenderTarget->DrawRoundedRectangle(pillR, pSolidBrush, 1.35f);
                } else {
                    setBrush(isNext ? Colors::BadgeMidi : (isHov ? Colors::BorderChassis : Colors::BorderCard));
                    pRenderTarget->DrawRoundedRectangle(pillR, pSolidBrush, isNext ? 1.25f : 1.0f);
                }
            }

            if (sylls[i].emotion != EMO_NEUTRAL) {
                D2DColor emoCol = Colors::BadgeMidi;
                if (sylls[i].emotion == EMO_HAPPY || sylls[i].emotion == EMO_EXCITED) {
                    emoCol = D2DColor(0.95f, 0.72f, 0.24f); // warm gold
                } else if (sylls[i].emotion == EMO_SAD) {
                    emoCol = D2DColor(0.36f, 0.58f, 0.88f); // cool slate-blue
                } else if (sylls[i].emotion == EMO_ANGRY) {
                    emoCol = D2DColor(0.92f, 0.30f, 0.24f); // crimson-amber
                } else if (sylls[i].emotion == EMO_SOFT) {
                    emoCol = D2DColor(0.28f, 0.76f, 0.68f); // soft teal
                }
                setBrush(emoCol);
                pRenderTarget->FillRectangle(
                    D2D1::RectF(x0 + 3.0f, STRIP_Y + STRIP_H - 5.5f, x1 - 3.0f, STRIP_Y + STRIP_H - 3.5f),
                    pSolidBrush
                );
            }

            std::wstring wDisp = utf8ToWide(sylls[i].display);
            std::wstring wHint = utf8ToWide(sylls[i].phoneticHint);
            setBrush(isCur ? Colors::ButtonActiveText : Colors::TextActive);
            drawText(wDisp.c_str(), x0 + 2.0f, STRIP_Y + 4.0f, pw - 4.0f, 18.0f, pFormatPillMain);
            setBrush(isCur ? D2DColor(0.24f, 0.26f, 0.30f) : Colors::TextMuted);
            drawText(wHint.c_str(), x0 + 2.0f, STRIP_Y + 20.0f, pw - 4.0f, 14.0f, pFormatPillSub);
        }
        pRenderTarget->PopAxisAlignedClip();
    }

    void drawTogglesRow() {
        if (!engine) return;

        struct ToggleSpec {
            int paramId;
            const wchar_t* offText;
            const wchar_t* onText;
            bool alwaysNeutralAccent;
        };
        static constexpr ToggleSpec kToggles[4] = {
            { PARAM_LANG,       L"LANG: EN",       L"LANG: TR",       true  },
            { PARAM_LEGATO,     L"LEGATO: FLOW",   L"LEGATO: HOLD",   false },
            { PARAM_AUDIO_MODE, L"AUDIO: LOOP",    L"AUDIO: STRETCH", false },
            { PARAM_EXCITER,    L"EXCITER: OFF",   L"EXCITER: ON",    false }
        };

        for (int i = 0; i < 4; ++i) {
            float x0 = TOGGLE_X + static_cast<float>(i) * (TOGGLE_W + TOGGLE_GAP);
            bool isHov = (hoverZone == HZ_TOGGLE_0 + i);
            const wchar_t* text = nullptr;
            bool highContrast = false;

            if (i == 0 || i == 2) {
                bool isOn = (engine->getParam(kToggles[i].paramId) >= 0.5f);
                highContrast = !kToggles[i].alwaysNeutralAccent && isOn;
                text = isOn ? kToggles[i].onText : kToggles[i].offText;
            } else if (i == 1) {
                int lMode = BaburEngine::decodeLegatoMode(engine->getParam(PARAM_LEGATO));
                if (lMode == LEGATO_FLOW) { text = L"LEGATO: FLOW"; highContrast = true; }
                else if (lMode == LEGATO_STAC) { text = L"LEGATO: STAC"; highContrast = false; }
                else { text = L"LEGATO: HOLD"; highContrast = true; }
            } else {
                int vMode = BaburEngine::decodeVoiceMode(engine->getParam(PARAM_EXCITER));
                if (vMode == VOICE_RAW) { text = L"VOICE: RAW"; highContrast = false; }
                else if (vMode == VOICE_WHISPER) { text = L"VOICE: WHISP"; highContrast = true; }
                else if (vMode == VOICE_AGGR) { text = L"VOICE: AGGR"; highContrast = true; }
                else { text = L"VOICE: NORM"; highContrast = true; }
            }

            D2D1_ROUNDED_RECT r = D2D1::RoundedRect(
                D2D1::RectF(x0, TOGGLE_Y, x0 + TOGGLE_W, TOGGLE_Y + TOGGLE_H), 2.5f, 2.5f
            );

            if (highContrast) {
                setBrush(Colors::ButtonActiveBg);
                pRenderTarget->FillRoundedRectangle(r, pSolidBrush);
                setBrush(Colors::ButtonActiveText);
            } else {
                setBrush(isHov ? Colors::ButtonInactiveBorder : Colors::BgCard);
                pRenderTarget->FillRoundedRectangle(r, pSolidBrush);
                setBrush(isHov ? Colors::TextMuted : ((i == 0) ? Colors::BadgeMidi : Colors::BorderCard));
                pRenderTarget->DrawRoundedRectangle(r, pSolidBrush, 1.0f);
                setBrush(isHov ? Colors::TextBright : Colors::TextActive);
            }

            drawText(text, x0, TOGGLE_Y, TOGGLE_W, TOGGLE_H, pFormatMicro);
        }
    }

    void drawControlTabs() {
        static constexpr const wchar_t* kTabNames[3] = { L"MAIN", L"VOICE", L"STYLE" };
        for (int t = 0; t < 3; ++t) {
            float tx = CTRL_CARD_X + static_cast<float>(t) * TAB_W;
            bool isSel = (activeTab == t);
            bool isHov = (hoverZone == (HZ_TAB_0 + t));
            setBrush(isSel ? Colors::BgCard : (isHov ? D2DColor(0.080f, 0.085f, 0.098f) : Colors::BgHeader));
            pRenderTarget->FillRectangle(D2D1::RectF(tx, TAB_BAR_Y, tx + TAB_W, TAB_BAR_Y + TAB_BAR_H), pSolidBrush);
            if (t > 0) {
                setBrush(Colors::BorderCard);
                pRenderTarget->DrawLine(D2D1::Point2F(tx, TAB_BAR_Y + 1.0f), D2D1::Point2F(tx, TAB_BAR_Y + TAB_BAR_H), pSolidBrush, 1.0f);
            }
            if (isSel) {
                setBrush(Colors::TextActive);
                pRenderTarget->FillRectangle(D2D1::RectF(tx + 12.0f, TAB_BAR_Y + TAB_BAR_H - 2.0f, tx + TAB_W - 12.0f, TAB_BAR_Y + TAB_BAR_H), pSolidBrush);
            } else {
                setBrush(Colors::BorderCard);
                pRenderTarget->DrawLine(D2D1::Point2F(tx, TAB_BAR_Y + TAB_BAR_H), D2D1::Point2F(tx + TAB_W, TAB_BAR_Y + TAB_BAR_H), pSolidBrush, 1.0f);
            }
            bool hasNonDefault = false;
            if (engine && t > 0) {
                for (const auto& s : tabSliders[t]) {
                    if (std::fabs(engine->getParam(s.paramId) - getDefaultParam(s.paramId)) > 0.015f) {
                        hasNonDefault = true;
                        break;
                    }
                }
            }
            setBrush(isSel ? Colors::TextBright : (isHov ? Colors::TextActive : Colors::TextMuted));
            drawText(kTabNames[t], tx, TAB_BAR_Y, TAB_W, TAB_BAR_H - 1.0f, pFormatMicro);
            if (hasNonDefault) {
                setBrush(Colors::BadgeMidi);
                pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + TAB_W * 0.5f + 26.0f, TAB_BAR_Y + TAB_BAR_H * 0.5f), 2.2f, 2.2f), pSolidBrush);
            }
        }
    }

    void drawSlider(const SliderWidget& s) {
        if (!engine) return;
        float val = engine->getParam(s.paramId);
        bool isActive = (activeSliderId == s.paramId);
        bool isHover = (hoverSliderId == s.paramId);
        bool isBipolar = (s.paramId == PARAM_THROAT || s.paramId == PARAM_GENDER ||
                          s.paramId == PARAM_PITCH_SHIFT || s.paramId == PARAM_BRIGHTNESS);

        float labelY = s.y - 17.0f;
        setBrush((isActive || isHover) ? Colors::TextActive : Colors::TextMuted);
        drawText(getSliderTitle(s.paramId), s.x, labelY, s.w * 0.65f, 13.0f, pFormatLabelLeft);

        std::wstring valText = utf8ToWide(engine->getParamDisplay(s.paramId));
        setBrush(isActive ? Colors::TextBright : Colors::TextActive);
        drawText(valText.c_str(), s.x + s.w * 0.52f, labelY, s.w * 0.48f, 13.0f, pFormatValueRight);

        float cy = s.y + s.h * 0.5f;

        int numTicks = (s.w < 250.0f) ? 6 : 10;
        for (int i = 0; i <= numTicks; ++i) {
            float tx = s.x + (static_cast<float>(i) / static_cast<float>(numTicks)) * s.w;
            bool center = (i * 2 == numTicks);
            float tickH = (i == 0 || center || i == numTicks) ? 8.5f : 5.5f;
            setBrush((center && isBipolar) ? Colors::TextDim : Colors::BorderCard);
            pRenderTarget->DrawLine(D2D1::Point2F(tx, cy - tickH), D2D1::Point2F(tx, cy + tickH), pSolidBrush, 1.0f);
        }

        D2D1_ROUNDED_RECT trackRect = D2D1::RoundedRect(
            D2D1::RectF(s.x, s.y, s.x + s.w, s.y + s.h), 2.0f, 2.0f
        );
        setBrush(Colors::BgHeader);
        pRenderTarget->FillRoundedRectangle(trackRect, pSolidBrush);
        setBrush((isActive || isHover) ? Colors::ButtonInactiveBorder : Colors::BorderChassis);
        pRenderTarget->DrawRoundedRectangle(trackRect, pSolidBrush, 1.0f);

        setBrush(Colors::KnobTrack);
        pRenderTarget->DrawLine(D2D1::Point2F(s.x + 3.0f, cy), D2D1::Point2F(s.x + s.w - 3.0f, cy), pSolidBrush, 1.5f);

        float thumbX = s.x + val * s.w;
        float capW = 18.0f;
        float capH = 22.0f;
        D2D1_ROUNDED_RECT capRect = D2D1::RoundedRect(
            D2D1::RectF(thumbX - capW * 0.5f, cy - capH * 0.5f, thumbX + capW * 0.5f, cy + capH * 0.5f),
            2.5f, 2.5f
        );

        setBrush(isActive ? D2DColor(0.145f, 0.155f, 0.185f) : (isHover ? D2DColor(0.125f, 0.135f, 0.160f) : Colors::KnobInner));
        pRenderTarget->FillRoundedRectangle(capRect, pSolidBrush);
        setBrush(isActive ? Colors::TextBright : (isHover ? Colors::TextActive : Colors::ButtonInactiveBorder));
        pRenderTarget->DrawRoundedRectangle(capRect, pSolidBrush, 1.25f);

        setBrush(isActive ? Colors::ButtonActiveBg : Colors::TextActive);
        pRenderTarget->FillRectangle(D2D1::RectF(thumbX - 1.25f, cy - 7.0f, thumbX + 1.25f, cy + 7.0f), pSolidBrush);
    }
};

} // namespace Fraudize
#endif
