// Win7Taskbar - vetro Aero per finestre native (overflow)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v2.7: il vero glass composto (blur + tinta del desktop) si ottiene con
// l'API non documentata SetWindowCompositionAttribute (accent
// ACCENT_ENABLE_BLURBEHIND), la stessa che usano le mod Win7-style su
// Win10/11 (es. "Windows 7 Network Flyout Recreation"): stabile da Vista
// a Win11 24H2.

#pragma once
#include <windows.h>
#include <dwmapi.h>

/* v3.2: UN SOLO punto di verita' per i parametri del vetro: ogni finestra
 * (flyout, ricerca app, ecc.) usa queste costanti, cosi' nessuna puo'
 * risultare "piu' trasparente" delle altre per distrazione. */
namespace AeroStyle {
    constexpr unsigned long kDefaultAlpha  = 0x2C;  /* tinta leggera Aero  */
    constexpr unsigned long kReadableAlpha = 0x8F;  /* con testo sopra     */
}

namespace AeroGlass {

/* alias comodi dentro il namespace del vetro */
constexpr unsigned long kDefaultAlpha  = AeroStyle::kDefaultAlpha;
constexpr unsigned long kReadableAlpha = AeroStyle::kReadableAlpha;

enum ACCENT_STATE { ACCENT_DISABLED = 0, ACCENT_ENABLE_GRADIENT = 1,
                    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
                    ACCENT_ENABLE_BLURBEHIND = 3,
                    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4 };

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor; // ABGR
    DWORD        AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 };

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID  pvData;
    SIZE_T cbData;
};

using SetWindowCompositionAttribute_t =
    BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

/// Estende il frame DWM su tutto il client (necessario perche' il blur
/// "prenda" sull'area che disegni tu) e attiva il blur-behind vero.
/// Colora leggermente col colorization corrente di sistema, come
/// faceva Aero con DwmGetColorizationColor.
inline bool EnableGlass(HWND hWnd, DWORD gradientAlpha = AeroStyle::kDefaultAlpha,
                        DWORD colorOverride = 0)
{
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hWnd, &m);

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;
    auto setComposition =
        reinterpret_cast<SetWindowCompositionAttribute_t>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setComposition) return false;

    DWORD colorization = 0; BOOL opaque = FALSE;
    if (colorOverride != 0) {
        colorization = colorOverride;   // tinta fissa (es. blu Win7)
    } else {
        DwmGetColorizationColor(&colorization, &opaque);
    }
    // ABGR: alpha parametrico (0x2C = tinta leggera come Aero; valori
    // piu' alti per finestre dove la leggibilita' conta di piu').
    DWORD gradientColor = ((gradientAlpha & 0xFF) << 24) |
        ((colorization & 0xFF) << 16) |
        (colorization & 0xFF00) |
        ((colorization >> 16) & 0xFF);

    // BLURBEHIND (3) = blur "puro" piu' vicino ad Aero; l'acrilico (4)
    // e' piu' opaco e rumoroso: non fedele a Win7.
    ACCENT_POLICY accent{ ACCENT_ENABLE_BLURBEHIND, 0, gradientColor, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data{ WCA_ACCENT_POLICY, &accent, sizeof(accent) };
    return setComposition(hWnd, &data) != FALSE;
}

} // namespace AeroGlass
