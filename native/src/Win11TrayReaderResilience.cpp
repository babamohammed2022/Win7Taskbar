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

#include <windows.h>
#include <tlhelp32.h>

namespace w7t {
namespace {

HWINEVENTHOOK g_taskbarRefreshHook = nullptr;
ULONGLONG g_lastRefreshTick = 0;
constexpr ULONGLONG kRefreshDebounceMs = 250;

/*
 * Get the OS build without relying on the process manifest. RtlGetVersion is
 * resolved dynamically so this file does not add an ntdll import dependency.
 * Failure is deliberately non-fatal: the existing XAML/taskbar detection is
 * still authoritative.
 */
DWORD GetWindowsBuildNumber() noexcept {
    using RtlGetVersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return 0;
    }

    auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtlGetVersion == nullptr) {
        return 0;
    }

    RTL_OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0) {
        return 0;
    }
    return version.dwBuildNumber;
}

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

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, explorerPid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstW(snapshot, &entry) != FALSE) {
        do {
            if (_wcsicmp(entry.szModule, wantedModule) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry) != FALSE);
    }

    CloseHandle(snapshot);
    return found;
}

void SafeRequestTrayRefresh() noexcept {
    try {
        auto& reader = Win11TrayReader::Instance();
        if (!reader.IsRunning() || !Win11TrayReader::Detect()) {
            return;
        }

        /*
         * The taskbar-classic-menu approach is useful here as a compatibility
         * principle: identify the Windows build first, then use the most
         * specific implementation available. Windows 11 starts at build
         * 22000. If version detection itself is unavailable, do not block the
         * already-working XAML detection.
         */
        const DWORD build = GetWindowsBuildNumber();
        if (build != 0 && build < 22000) {
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
                  L"[TrayRefresh] WindowsBuild=%lu Taskbar.View.dll=%s "
                  L"ExplorerExtensions.dll=%s\n",
                  static_cast<unsigned long>(build),
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
        TrayRefreshEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS |
            WINEVENT_SKIPOWNTHREAD);
}

Win11TrayReader::~Win11TrayReader() {
    if (g_taskbarRefreshHook != nullptr) {
        UnhookWinEvent(g_taskbarRefreshHook);
        g_taskbarRefreshHook = nullptr;
    }
    Stop();
}

} // namespace w7t
