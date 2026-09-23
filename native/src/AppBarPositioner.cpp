// Win7Taskbar - Core nativo - appbar per-monitor RAII
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "AppBarPositioner.h"

#include <stdexcept>

namespace w7t {

namespace {

APPBARDATA MakeData(HWND hwnd, UINT callbackMessage, HMONITOR monitor) noexcept {
    APPBARDATA data = {};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uCallbackMessage = callbackMessage;
    data.uEdge = ABE_LEFT;
    data.rc = RECT{ 0, 0, 0, 0 };
    data.lParam = 0;
    (void)monitor;   /* per-monitor targeting rides on the SETPOS rect */
    return data;
}

} // namespace

AppBarPositioner::AppBarPositioner(HWND hwnd, UINT callbackMessage, HMONITOR monitor)
    : m_data(MakeData(hwnd, callbackMessage, monitor)),
      m_hwnd(hwnd), m_monitor(monitor) {
    if (hwnd == nullptr) {
        throw std::invalid_argument("AppBarPositioner: null hwnd");
    }
    /* ABM_NEW: 1 = registered, 0 = failure (docs). */
    m_registered = SHAppBarMessage(ABM_NEW, &m_data) != 0;
    if (!m_registered) {
        throw std::runtime_error("AppBarPositioner: ABM_NEW failed");
    }
}

AppBarPositioner::~AppBarPositioner() {
    Unregister();
}

AppBarPositioner::AppBarPositioner(AppBarPositioner&& other) noexcept
    : m_data(other.m_data), m_hwnd(other.m_hwnd), m_monitor(other.m_monitor),
      m_rect(other.m_rect), m_registered(std::exchange(other.m_registered, false)) {}

AppBarPositioner& AppBarPositioner::operator=(AppBarPositioner&& other) noexcept {
    if (this != &other) {
        Unregister();
        m_data = other.m_data;
        m_hwnd = other.m_hwnd;
        m_monitor = other.m_monitor;
        m_rect = other.m_rect;
        m_registered = std::exchange(other.m_registered, false);
    }
    return *this;
}

void AppBarPositioner::Unregister() noexcept {
    if (m_registered) {
        SHAppBarMessage(ABM_REMOVE, &m_data);
        m_registered = false;
    }
}

Edge AppBarPositioner::SetPos(Edge desired, const RectI& desiredRect) {
    if (!m_registered) {
        throw std::runtime_error("AppBarPositioner: not registered");
    }
    switch (desired) {
    case Edge::Left:   m_data.uEdge = ABE_LEFT;   break;
    case Edge::Top:    m_data.uEdge = ABE_TOP;    break;
    case Edge::Right:  m_data.uEdge = ABE_RIGHT;  break;
    default:           m_data.uEdge = ABE_BOTTOM; break;
    }
    m_data.rc.left = desiredRect.left;
    m_data.rc.top = desiredRect.top;
    m_data.rc.right = desiredRect.right;
    m_data.rc.bottom = desiredRect.bottom;

    /* ABM_SETPOS: rc is in/out — the shell shrinks it around the work
     * area and other appbars (public documented behaviour). The rect is
     * in screen coordinates: the per-monitor targeting happens because
     * the rect falls inside the intended monitor. */
    SHAppBarMessage(ABM_SETPOS, &m_data);

    m_rect = RectI{ m_data.rc.left, m_data.rc.top,
                    m_data.rc.right, m_data.rc.bottom };
    return desired;
}

bool AppBarPositioner::QueryPos(RectI* out) const noexcept {
    if (!m_registered || out == nullptr) return false;
    APPBARDATA probe = m_data;
    probe.uEdge = m_data.uEdge;
    if (SHAppBarMessage(ABM_QUERYPOS, &probe) == 0) {
        /* Docs: returns the shell-adjusted rect via probe.rc regardless;
         * 0 is still usable on older shells, so trust the rect. */
    }
    *out = RectI{ probe.rc.left, probe.rc.top, probe.rc.right, probe.rc.bottom };
    return true;
}

UINT AppBarPositioner::SetState(UINT state) noexcept {
    if (!m_registered) return 0;
    APPBARDATA probe = m_data;
    probe.lParam = state;
    return static_cast<UINT>(SHAppBarMessage(ABM_SETSTATE, &probe));
}

} // namespace w7t
