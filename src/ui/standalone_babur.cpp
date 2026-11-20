#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>
#include "../dsp/BaburDSP.hpp"
#include "BaburUI.hpp"

static LRESULT CALLBACK ParentWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    auto engine = std::make_unique<Fraudize::BaburEngine>();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ParentWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"BaburStandaloneParent";
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, Fraudize::BaburUI::UI_WIDTH, Fraudize::BaburUI::UI_HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, FALSE);

    HWND hParent = CreateWindowExW(
        0,
        L"BaburStandaloneParent",
        L"babur",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hParent) return 1;

    Fraudize::BaburUI ui(engine.get());
    ui.openWindow(hParent);

    ShowWindow(hParent, nCmdShow);
    UpdateWindow(hParent);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
