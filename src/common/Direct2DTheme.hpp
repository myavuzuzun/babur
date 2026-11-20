#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#ifdef DrawText
#undef DrawText
#endif
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

namespace Fraudize {

struct D2DColor {
    float r, g, b, a;
    constexpr D2DColor(float red = 0.0f, float green = 0.0f, float blue = 0.0f, float alpha = 1.0f)
        : r(red), g(green), b(blue), a(alpha) {}
    D2D1_COLOR_F toD2D() const { return D2D1::ColorF(r, g, b, a); }
};

// Exact Fraudize Palette
namespace Colors {
    static constexpr D2DColor BgChassis(0.075f, 0.080f, 0.090f);       // Matte Dark Slate #131417
    static constexpr D2DColor BgHeader(0.055f, 0.058f, 0.065f);        // Top Header #0E0F11
    static constexpr D2DColor BgCard(0.090f, 0.095f, 0.105f);          // Recessed Module Card #17181D
    static constexpr D2DColor BorderChassis(0.18f, 0.19f, 0.22f);      // Border #2E3138
    static constexpr D2DColor BorderCard(0.16f, 0.17f, 0.19f);         // Border #292B30

    static constexpr D2DColor TextActive(0.90f, 0.92f, 0.96f);         // Crisp Off-White / Silver #E6EBF5
    static constexpr D2DColor TextBright(0.96f, 0.97f, 0.98f);         // Pure Title White
    static constexpr D2DColor TextMuted(0.50f, 0.53f, 0.58f);          // Secondary Muted Pewter #808794
    static constexpr D2DColor TextDim(0.32f, 0.34f, 0.38f);            // Dim / Inactive Text

    static constexpr D2DColor KnobTrack(0.18f, 0.19f, 0.22f);          // Dark Track
    static constexpr D2DColor KnobArc(0.90f, 0.92f, 0.96f);            // Active Silver
    static constexpr D2DColor KnobInner(0.11f, 0.12f, 0.14f);          // Center Cap

    static constexpr D2DColor ButtonActiveBg(0.92f, 0.94f, 0.97f);     // Inverted High-Contrast On
    static constexpr D2DColor ButtonActiveText(0.055f, 0.058f, 0.065f); // Dark Text on Active
    static constexpr D2DColor ButtonInactiveBg(0.12f, 0.13f, 0.15f);   // Off state fill
    static constexpr D2DColor ButtonInactiveBorder(0.24f, 0.26f, 0.30f);

    static constexpr D2DColor BadgeMidi(0.60f, 0.65f, 0.75f);          // Slate Blue Badge
    static constexpr D2DColor BadgeAudio(0.75f, 0.70f, 0.60f);         // Warm Pewter Badge
}

struct SliderWidget {
    int paramId;
    float x, y, w, h;
};

class UITheme {
public:
    static void initTextFormats(IDWriteFactory* pDWriteFactory,
                                IDWriteTextFormat** ppPreset,
                                IDWriteTextFormat** ppChevron) {
        if (!pDWriteFactory) return;

        auto createFmt = [&](const wchar_t* font, float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** ppFmt, DWRITE_TEXT_ALIGNMENT align, bool singleLineEllipsis) {
            pDWriteFactory->CreateTextFormat(
                font, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                size, L"en-us", ppFmt
            );
            if (*ppFmt) {
                (*ppFmt)->SetTextAlignment(align);
                (*ppFmt)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                if (singleLineEllipsis) {
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

        createFmt(L"Segoe UI", 10.5f, DWRITE_FONT_WEIGHT_BOLD, ppPreset, DWRITE_TEXT_ALIGNMENT_CENTER, true);
        createFmt(L"Segoe UI", 11.0f, DWRITE_FONT_WEIGHT_BOLD, ppChevron, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    }
};

} // namespace Fraudize
#endif
