/*
 * Win7Taskbar - conservative Windows 11 tray refresh guard
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * GPL v3 or later; see LICENSE.
 *
 * The refresh strategy is inspired by the MIT-licensed Windhawk
 * "Separate System Tray Icons" mod: the Windows 11 XAML tray is disposable,
 * so the current tree must be rediscovered after Explorer rebuilds it.
 * The taskbar lifecycle/build-detection pattern is also informed by the
 * Windhawk "Taskbar classic context menu" mod.
 * Win7Taskbar reimplements these ideas through its existing UI Automation
 * worker; no Windhawk hook or private taskbar code is copied here.
 */

#include "Win11TrayReader.h"
#include "../include/RaiiWrappers.h"
#include "ScopeGuards.h"

#include <windows.h>
#include <tlhelp32.h>

namespace w7t {
namespace {

UniqueWinEventHook g_taskbarRefreshHook;
ULONGLONG g_lastRefreshTick = 0;
constexpr ULONGLONG kRefreshDebounceMs = 250;

/*
 * Windhawk runs inside Explorer and can use GetModuleHandle directly. We run
 * out-of-process, so the equivalent check must inspect Explorer's module list.
 * This is diagnostic/read-only only; failure never blocks tray operation.
 */
bool ExplorerHasTaskbarModule(const wchar_t* wantedModule) noexcept {
    if (wantedModule == nullptr || *wantedModule == L'\0') {
        return false;
    }

    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar == nullptr) {
        return false;
    }

    DWORD explorerPid = 0;
    GetWindowThreadProcessId(taskbar, &explorerPid);
    if (explorerPid == 0 || explorerPid == GetCurrentProcessId()) {
        return false;
    }

    raii::GenericHandle snapshot(CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, explorerPid));
    if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstW(snapshot.get(), &entry) != FALSE) {
        do {
            if (_wcsicmp(entry.szModule, wantedModule) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot.get(), &entry) != FALSE);
    }

    return found;
}

void SafeRequestTrayRefresh() noexcept {
    try {
        auto& reader = Win11TrayReader::Instance();
        if (!reader.IsRunning() || !Win11TrayReader::Detect()) {
            return;
        }

        /*
         * A rebuild can emit a burst of CREATE/SHOW events. Coalesce them so
         * one Explorer restart cannot turn into dozens of UIA scans.
         */
        const ULONGLONG now = GetTickCount64();
        if (g_lastRefreshTick != 0 &&
            now - g_lastRefreshTick < kRefreshDebounceMs) {
            return;
        }
        g_lastRefreshTick = now;

        /*
         * Taskbar.View.dll is normally the Windows 11 XAML implementation;
         * ExplorerExtensions.dll is a known fallback on some builds. This is
         * deliberately advisory: a protected Explorer can deny module
         * enumeration, and that must never disable the existing UIA reader.
         */
        const bool taskbarView = ExplorerHasTaskbarModule(L"Taskbar.View.dll");
        const bool explorerExtensions =
            ExplorerHasTaskbarModule(L"ExplorerExtensions.dll");

        wchar_t diagnostic[256] = {};
        wsprintfW(diagnostic,
                  L"[TrayRefresh] Taskbar.View.dll=%s "
                  L"ExplorerExtensions.dll=%s\n",
                  taskbarView ? L"loaded" : L"not-loaded",
                  explorerExtensions ? L"loaded" : L"not-loaded");
        OutputDebugStringW(diagnostic);

        /*
         * Do not make module enumeration a hard gate. The important action is
         * still to rediscover the current UIA tree after Explorer rebuilds.
         */
        reader.RequestRead();
    } catch (...) {
        /* Recovery code must never become a new crash source. */
    }
}

void CALLBACK TrayRefreshEventProc(HWINEVENTHOOK, DWORD eventType, HWND hwnd,
                                   LONG idObject, LONG idChild, DWORD, DWORD) {
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != 0) {
        return;
    }
    if (eventType != EVENT_OBJECT_CREATE && eventType != EVENT_OBJECT_DESTROY &&
        eventType != EVENT_OBJECT_SHOW && eventType != EVENT_OBJECT_HIDE) {
        return;
    }

    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, _countof(cls)) == 0) {
        return;
    }

    /* Only react to the Windows 11 XAML taskbar/overflow hosts. */
    if (lstrcmpW(cls, L"Shell_TrayWnd") == 0 ||
        lstrcmpW(cls, L"Shell_SecondaryTrayWnd") == 0 ||
        lstrcmpW(cls, L"TopLevelWindowForOverflowXamlIsland") == 0 ||
        wcsstr(cls, L"DesktopWindowContentBridge") != nullptr ||
        lstrcmpW(cls, L"Windows.UI.Input.InputSite.WindowClass") == 0) {
        SafeRequestTrayRefresh();
    }
}

} // namespace

Win11TrayReader::Win11TrayReader() {
    /* Best-effort only: failure simply leaves the existing periodic/event
     * refresh mechanisms in charge. The hook is outside DllMain and is not
     * required for normal tray operation. */
    g_taskbarRefreshHook.reset(SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE, nullptr,
        TrayRefreshEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS |
            WINEVENT_SKIPOWNTHREAD));
}

Win11TrayReader::~Win11TrayReader() {
    g_taskbarRefreshHook.reset();
    Stop();
}

} // namespace w7t
