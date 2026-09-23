// Win7Taskbar - Core nativo - eventi shell (09)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 09_shell_hook (snippet integration): a shell hook receives HSHELL_*
// notifications for top-level window events (created, destroyed,
// activated, moved, monitor change, full-screen "rude app"). Public
// pattern: RegisterShellHookWindow(hwnd) + RegisterWindowMessageW with
// the SHELLHOOK id as the notification message; wParam = HSHELL_*
// code, lParam = window handle (or monitor index on MONITORCHANGED).
//
// Used to keep the task list and per-monitor bars in sync and to hide
// the bar behind full-screen apps (HSHELL_RUDEAPP / FULLSCREENAPP).

#pragma once

#include <windows.h>

#include <functional>

namespace w7t {

/* HSHELL_* (public winuser values). Fallbacks cover SDKs where the
 * newer names are missing. */
#ifndef HSHELL_WINDOWCREATED
constexpr int kShellHookWindowCreated   = 1;
constexpr int kShellHookWindowDestroyed = 2;
constexpr int kShellHookActivated       = 4;
constexpr int kShellHookWindowMoved     = 11;
#else
constexpr int kShellHookWindowCreated   = HSHELL_WINDOWCREATED;
constexpr int kShellHookWindowDestroyed = HSHELL_WINDOWDESTROYED;
constexpr int kShellHookActivated       = HSHELL_ACTIVATED;
constexpr int kShellHookWindowMoved     = HSHELL_WINDOWMOVED;
#endif
#ifdef HSHELL_MONITORCHANGED
constexpr int kShellHookMonitorChanged  = HSHELL_MONITORCHANGED;
#else
constexpr int kShellHookMonitorChanged  = 17;   /* Win10+ (docs) */
#endif
#ifdef HSHELL_RUDEAPP
constexpr int kShellHookRudeApp         = HSHELL_RUDEAPP;
#else
constexpr int kShellHookRudeApp         = 0x8003;   /* HSHELL_HIGHBIT|3 */
#endif

struct ShellHookEvent {
    int code = 0;          /* HSHELL_* */
    HWND hwnd = nullptr;   /* affected window (monitor events: see docs) */
};

class ShellHookReceiver {
public:
    using EventFn = std::function<void(const ShellHookEvent&)>;

    /* Registers `hwnd` as a shell hook window. Throws on failure. */
    explicit ShellHookReceiver(HWND hwnd, EventFn onEvent = nullptr);
    ~ShellHookReceiver();

    ShellHookReceiver(const ShellHookReceiver&) = delete;
    ShellHookReceiver& operator=(const ShellHookReceiver&) = delete;

    /* Route the registered SHELLHOOK message here; true when consumed. */
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    UINT hookMessage() const noexcept { return m_msgShellHook; }

private:
    HWND m_hwnd = nullptr;
    EventFn m_onEvent;
    UINT m_msgShellHook = 0;
    bool m_registered = false;
};

} // namespace w7t
