// Win7Taskbar - Core nativo - appbar per-monitor RAII (03)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 03_appbar_raii (snippet integration): registers a window as an
// application desktop toolbar (SHAppBarMessage ABM_NEW/ABM_SETPOS/
// ABM_REMOVE) with automatic unregistration. One instance = one bar
// window on one monitor. On 8.1/10/11 pass the per-monitor rect:
// ABM_SETPOS's rc is in/out and the shell clamps it to the work area
// of the monitor the rect falls in (public documented behaviour).
//
// Convergence note: AppBarService.h (the singleton used by the current
// W7T_AppBarRegister export) serves the existing single-bar path; this
// class is the per-monitor primitive for multi-monitor bars. The two
// can be merged into one implementation once the multi-bar path lands
// (documented in docs/native-module-notes.md).

#pragma once

#include <windows.h>
#include <shellapi.h>

#include "EdgeRotation.h"
#include "RaiiWrappers.h"

namespace w7t {

class AppBarPositioner {
public:
    /* Registers `hwnd` as an appbar. uCallback receives ABN_* events
     * (see PrivateWindowMessages.h for the message number to use). */
    AppBarPositioner(HWND hwnd, UINT callbackMessage, HMONITOR monitor = nullptr);

    ~AppBarPositioner();

    AppBarPositioner(AppBarPositioner&& other) noexcept;
    AppBarPositioner& operator=(AppBarPositioner&& other) noexcept;
    AppBarPositioner(const AppBarPositioner&) = delete;
    AppBarPositioner& operator=(const AppBarPositioner&) = delete;

    bool registered() const noexcept { return m_registered; }

    /* ABM_SETPOS: `desired` (logical, monitor-relative) is clamped by
     * the shell; the returned rect is what other appbars must respect.
     * Returns the edge the bar actually occupies. */
    Edge SetPos(Edge desired, const RectI& desiredRect);

    /* Last rect returned by the shell (logical). */
    const RectI& rect() const noexcept { return m_rect; }

    /* ABM_GETTASKBARPOS-style query of our own reserved space. */
    bool QueryPos(RectI* out) const noexcept;

    /* ABM_SETSTATE: always-on-top + autohide bits (ABS_*). */
    UINT SetState(UINT state) noexcept;

private:
    void Unregister() noexcept;

    APPBARDATA m_data = {};
    HWND m_hwnd = nullptr;
    HMONITOR m_monitor = nullptr;
    RectI m_rect = {};
    bool m_registered = false;
};

} // namespace w7t
