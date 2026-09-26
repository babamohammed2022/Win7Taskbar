// Win7Taskbar - Windows 7/8.1 style language switcher: a port of the
// Windhawk mod "Windows 7/8.1 Language Switcher Restorer" v1.1.0
// (babamohammed2022, GPL-3.0 or later).
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// ============================================================================
// WHAT WAS PORTED (see docs/THIRD-PARTY-NOTICES.md).
//
//   - Layout enumeration and switching: KeyboardLayoutItem,
//     RefreshKeyboardLayouts, SwitchToLayout, FindKlidByLayoutId,
//     GetSubstituteKlid, GetLayoutDisplayName, GetLangAbbrev, the
//     g_LangAbbrevs table, FormatWin7LayoutItemText.
//   - Popup rendering: PaintWin7Menu (classic menu), PaintWin8Flyout
//     (modern card), DrawWin7MenuCheckmark + GDI fallback, init/shutdown
//     of runtime-loaded GDI+.
//   - Popup footer localization: kLocalizedStrings (25 languages) and
//     GetLocalizedFooterStrings (the language comes from the taskbar's
//     single list instead of the mod's settings).
//   - Positioning near the multi-screen/multi-edge taskbar:
//     PositionWindowNearTray, with the anchor passed by the caller (OUR
//     taskbar) instead of Shell_TrayWnd.
//   - Popup window procedure: WM_PAINT, WM_MOUSEMOVE/WM_MOUSELEAVE
//     (hover), WM_LBUTTONUP (pick), WM_KEYDOWN (arrows/Enter/Esc),
//     WM_ACTIVATE (auto-hide when the popup loses the foreground).
//   - Memory of the EXTERNAL target window (g_targetWindow): clicking the
//     taskbar takes the foreground away from the user's window; the
//     layout switch must reach it.
//
// WHAT WAS NOT PORTED (it only exists in the mod because the mod is
// injected into explorer.exe; here the popup and the tray text are OURS):
//   all keyboard/mouse hooks and global shortcut handling,
//   ToolbarWindow32/TrayInputIndicatorWClass subclassing, the
//   ShowWindow hook, the "am I the main shell?" logic, worker/hook threads
//   with hot-unload contexts.
//
// Declared differences from the mod: the style (Win7 menu or Win8.1 card)
// comes from the mode chosen in Properties (1 = Win7, 2/3 = Win8.1); the
// color theme follows the Windows accent and theme (the mod's "auto"
// mode); the preferences command stays ms-settings:regionlanguage.
// ============================================================================

#pragma once

#include <cstdint>

namespace w7t {
namespace langswitcher {

/* Brings the switcher popup up (or to the front). ownerHwnd is the
 * taskbar window (positioning anchor); foregroundHwnd is the window that
 * had keyboard focus BEFORE the click (0 = discover it here); styleMode:
 * 1 = classic Windows 7 menu, 2/3 = Windows 8.1 card. */
void Show(uint64_t ownerHwnd, uint64_t foregroundHwnd, int styleMode);

/* Hides the popup (taskbar shutdown). */
void Hide();

/* Active language: mod-style three-letter abbreviation ("ENG", "ITA",
 * ...), two-letter ISO abbreviation and LANGID. The window with keyboard
 * focus decides the language (the ManagedShell mechanic), not our
 * process. */
void GetActiveInfo(uint32_t* langId, wchar_t* three, int threeCap,
                   wchar_t* two, int twoCap);

/* Callback fired on the popup thread when the active language changes
 * (the popup runs its own 200 ms timer even while hidden). */
typedef void(__stdcall* LangChangedCallback)(uint32_t langId);
void SetChangedCallback(LangChangedCallback callback);

/* orderly shutdown: hides the popup (called on core shutdown). */
void Shutdown();

} // namespace langswitcher
} // namespace w7t
