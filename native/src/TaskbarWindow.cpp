// Win7Taskbar - Core nativo - finestra barra multi-bordo
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "TaskbarWindow.h"

#include <stdexcept>

#include "DpiMetrics.h"
#include "ExceptionGuards.h"
#include "PrivateWindowMessages.h"

namespace w7t {

namespace {

constexpr wchar_t kPropThis[] = L"W7T_TaskbarWindow";

} // namespace

void TaskbarWindow::RegisterClassOnce(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TaskbarWindow::WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kTaskbarWindowClassName;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("TaskbarWindow: RegisterClassExW failed");
    }
    registered = true;
}

TaskbarWindow::TaskbarWindow(HINSTANCE instance, HMONITOR monitor, Edge edge,
                             TaskbarWindowCallbacks callbacks)
    : m_monitor(monitor), m_edge(edge), m_callbacks(std::move(callbacks)) {
    RegisterClassOnce(instance);

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kTaskbarWindowClassName, L"", WS_POPUP | WS_VISIBLE,
        0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (m_hwnd == nullptr) {
        throw std::runtime_error("TaskbarWindow: CreateWindowExW failed");
    }

    /* Appbar registration owns the reserved work-area slot; created
     * after the window so the callback message can reference it. */
    m_appbar = new AppBarPositioner(m_hwnd, kMsgAppBarNotify, m_monitor);
    Dock(edge);
}

TaskbarWindow::~TaskbarWindow() {
    delete m_appbar;
    if (m_hwnd != nullptr) {
        DestroyWindow(m_hwnd);
    }
}

TaskbarWindow::TaskbarWindow(TaskbarWindow&& other) noexcept
    : m_hwnd(std::exchange(other.m_hwnd, nullptr)),
      m_monitor(other.m_monitor), m_edge(other.m_edge),
      m_thickness(other.m_thickness),
      m_appbar(std::exchange(other.m_appbar, nullptr)),
      m_callbacks(std::move(other.m_callbacks)) {}

TaskbarWindow& TaskbarWindow::operator=(TaskbarWindow&& other) noexcept {
    if (this != &other) {
        delete m_appbar;
        if (m_hwnd != nullptr) DestroyWindow(m_hwnd);
        m_hwnd = std::exchange(other.m_hwnd, nullptr);
        m_monitor = other.m_monitor;
        m_edge = other.m_edge;
        m_thickness = other.m_thickness;
        m_appbar = std::exchange(other.m_appbar, nullptr);
        m_callbacks = std::move(other.m_callbacks);
    }
    return *this;
}

void TaskbarWindow::Dock(Edge edge) {
    if (m_appbar == nullptr) return;

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (m_monitor == nullptr || !GetMonitorInfoW(m_monitor, &mi)) {
        mi.rcMonitor = RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN),
                             GetSystemMetrics(SM_CYSCREEN) };
    }
    const UINT dpi = MonitorDpi(m_monitor);
    const int thickness = ScaleForDpi(m_thickness, dpi);
    const RectI mon{ mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right, mi.rcMonitor.bottom };

    RectI desired = mon;
    switch (edge) {
    case Edge::Left:
        desired.right = desired.left + thickness;
        break;
    case Edge::Top:
        desired.bottom = desired.top + thickness;
        break;
    case Edge::Right:
        desired.left = desired.right - thickness;
        break;
    default:
        desired.top = desired.bottom - thickness;
        break;
    }

    m_edge = m_appbar->SetPos(edge, desired);
    SetWindowPos(m_hwnd, HWND_TOPMOST,
                 m_appbar->rect().left, m_appbar->rect().top,
                 m_appbar->rect().width(), m_appbar->rect().height(),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

Edge TaskbarWindow::RotateClockwise() {
    const Edge from = m_edge;
    const Edge to = NextEdgeCw(from);
    Dock(to);
    if (m_callbacks.on_edge_rotated) m_callbacks.on_edge_rotated(from, to);
    return to;
}

Edge TaskbarWindow::RotateCounterClockwise() {
    const Edge from = m_edge;
    const Edge to = NextEdgeCcw(from);
    Dock(to);
    if (m_callbacks.on_edge_rotated) m_callbacks.on_edge_rotated(from, to);
    return to;
}

void TaskbarWindow::ApplyEdge(Edge edge) {
    Dock(edge);
}

LRESULT CALLBACK TaskbarWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetPropW(hwnd, kPropThis, static_cast<HANDLE>(cs->lpCreateParams));
    }
    TaskbarWindow* self =
        static_cast<TaskbarWindow*>(GetPropW(hwnd, kPropThis));

    return GuardedWndProc([&]() -> LRESULT {
        if (self != nullptr) {
            return self->HandleMessage(msg, wp, lp);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    });
}

LRESULT TaskbarWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case kMsgAppBarNotify:
        /* ABN_FULLSCREENAPP / ABN_POSCHANGED: the shell moved us.
         * Re-dock to keep the reserved slot. */
        if (wp == ABN_POSCHANGED || wp == ABN_FULLSCREENAPP) {
            Dock(m_edge);
        }
        return 0;

    case WM_DPICHANGED:
        Dock(m_edge);
        return 0;

    case WM_NCDESTROY:
        RemovePropW(m_hwnd, kPropThis);
        break;

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace w7t
