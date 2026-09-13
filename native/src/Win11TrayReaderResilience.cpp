/*
 * Win7Taskbar - conservative Windows 11 tray refresh guard
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * GPL v3 or later; see LICENSE.
 *
 * The refresh strategy is inspired by the MIT-licensed Windhawk
 * "Separate System Tray Icons" mod: the Windows 11 XAML tray is disposable,
 * so the current tree must be rediscovered after Explorer rebuilds it.
 * Win7Taskbar reimplements that idea through its existing UI Automation
 * worker; no Windhawk hook or private taskbar code is copied here.
 */

#include "Win11TrayReader.h"

#include <windows.h>

namespace w7t {
namespace {

HWINEVENTHOOK g_taskbarRefreshHook = nullptr;

void SafeRequestTrayRefresh() noexcept {
    try {
        auto& reader = Win11TrayReader::Instance();
        if (reader.IsRunning() && Win11TrayReader::Detect()) {
            reader.RequestRead();
        }
    } catch (...) {
        /* Recovery code must never become a new crash source. */
    }
}

void CALLBACK TrayRefreshEventProc(HWINEVENTHOOK, DWORD eventType, HWND hwnd,
                                   LONG idObject, LONG idChild, DWORD, DWORD) {
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != 0) {
        return;
    }
    if (eventType != EVENT_OBJECT_CREATE && eventType != EVENT_OBJECT_SHOW) {
        return;
    }

    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, _countof(cls)) == 0) {
        return;
    }

    /* Only react to the Windows 11 XAML taskbar/overflow hosts. */
    if (lstrcmpW(cls, L"Shell_TrayWnd") == 0 ||
        lstrcmpW(cls, L"TopLevelWindowForOverflowXamlIsland") == 0) {
        SafeRequestTrayRefresh();
    }
}

} // namespace

Win11TrayReader::Win11TrayReader() {
    /* Best-effort only: failure simply leaves the existing periodic/event
     * refresh mechanisms in charge. The hook is outside DllMain and is not
     * required for normal tray operation. */
    g_taskbarRefreshHook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr,
        TrayRefreshEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
}

Win11TrayReader::~Win11TrayReader() {
    if (g_taskbarRefreshHook != nullptr) {
        UnhookWinEvent(g_taskbarRefreshHook);
        g_taskbarRefreshHook = nullptr;
    }
    Stop();
}

} // namespace w7t
