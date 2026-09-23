// Win7Taskbar - Core nativo - host icone area di notifica (08)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 08_tray_icon (snippet integration): CLIENT side of the notification
// area — an RAII owner of Shell_NotifyIconW (NIM_ADD/MODIFY/DELETE +
// NIM_SETVERSION) icons with balloon tips. It complements the tray
// SERVER emulation in TrayService.cpp (which owns Shell_TrayWnd and the
// icons' visual hosting); an app that wants its icon in the tray uses
// this host.
// Reliability rules from the public Shell_NotifyIcon contract:
//   - NIM_ADD can fail transiently right after the shell starts: retry
//     on the registered "TaskbarCreated" broadcast (RAII re-add).
//   - Balloon tips are suppressed during quiet time (do-not-disturb):
//     probe QueryUserNotificationState dynamically (shcore, Win7+) and
//     fall back to showing the tip when unavailable.
//   - All events arrive as kMsgTrayIconCallback (NIN_* / mouse msgs,
//     lParam = low word of the original message, wParam = icon id).

#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>
#include <vector>

namespace w7t {

struct TrayIconEvent {
    UINT iconId = 0;
    UINT message = 0;        /* WM_LBUTTONUP, NIN_BALLOONSHOW, ... */
    POINT pt = {};
};

class TrayIconHost {
public:
    /* Events for the managed bridge: click / menu / balloon done. */
    using EventFn = std::function<void(const TrayIconEvent&)>;

    explicit TrayIconHost(HWND owner, EventFn onEvent = nullptr);
    ~TrayIconHost();

    TrayIconHost(const TrayIconHost&) = delete;
    TrayIconHost& operator=(const TrayIconHost&) = delete;

    /* Adds (or re-adds after a shell restart) one icon. */
    bool AddIcon(UINT iconId, HICON icon, const std::wstring& tooltip);
    bool ModifyIcon(UINT iconId, HICON icon, const std::wstring& tooltip);
    void RemoveIcon(UINT iconId);

    /* Balloon tip (Win7 semantics: timeout is advisory, the system
     * queues the tip). Returns false during quiet time or on failure. */
    bool ShowBalloon(UINT iconId, const std::wstring& title,
                     const std::wstring& text, DWORD infoFlags = NIIF_INFO,
                     UINT timeoutMs = 10000);

    /* Quiet time (do-not-disturb) probe. */
    static bool IsQuietTime();

    /* Route TaskbarCreated and kMsgTrayIconCallback here from the
     * owner WndProc. Returns true when consumed. */
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    size_t iconCount() const noexcept { return m_icons.size(); }

private:
    struct Entry {
        UINT id = 0;
        HICON icon = nullptr;
        std::wstring tooltip;
    };

    NOTIFYICONDATAW MakeData(UINT iconId) const;
    void ReAddAll();

    HWND m_owner = nullptr;
    EventFn m_onEvent;
    UINT m_msgTaskbarCreated = 0;
    std::vector<Entry> m_icons;   /* small N: no map needed */
};

} // namespace w7t
