// Win7Taskbar - Core nativo - finestra barra multi-bordo (05)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 05_taskbar_window (snippet integration): the bar window itself —
// a topmost tool window docked to one edge of one monitor, registered
// as an appbar and ready to rotate. The window class is
// "Win7Taskbar_TrayWnd" (project rule: never squat "Shell_TrayWnd";
// TrayService already owns that name as the tray SERVER emulation).
//
// Callbacks struct carries the managed-bridge hooks (C# side):
//   on_edge_rotated(old, new)   — persistence + band re-layout
//   on_taskbar_recreated()      — tray icons must be re-added (see 08)
//
// Threading rule (docs/native-module-notes.md): per-monitor windows
// live on their creating UI thread (STA apartment documented for the
// ITaskbarList3 client in phase 6). No worker thread owns an appbar.

#pragma once

#include <windows.h>
#include <functional>
#include <utility>

#include "AppBarPositioner.h"
#include "EdgeRotation.h"

namespace w7t {

/* Window class registered by TaskbarWindow. */
constexpr const wchar_t* kTaskbarWindowClassName = L"Win7Taskbar_TrayWnd";

struct TaskbarWindowCallbacks {
    std::function<void(Edge, Edge)> on_edge_rotated;
    std::function<void()> on_taskbar_recreated;
};

class TaskbarWindow {
public:
    /* Creates (or attaches to) the bar window on `monitor`, docked to
     * `edge`. Throws std::runtime_error on failure. */
    TaskbarWindow(HINSTANCE instance, HMONITOR monitor, Edge edge,
                  TaskbarWindowCallbacks callbacks);

    ~TaskbarWindow();

    TaskbarWindow(TaskbarWindow&& other) noexcept;
    TaskbarWindow& operator=(TaskbarWindow&& other) noexcept;
    TaskbarWindow(const TaskbarWindow&) = delete;
    TaskbarWindow& operator=(const TaskbarWindow&) = delete;

    HWND hwnd() const noexcept { return m_hwnd; }
    Edge edge() const noexcept { return m_edge; }

    /* Rotation entry points (04): recompute the edge, re-dock the bar,
     * fire on_edge_rotated. Returns the new edge. */
    Edge RotateClockwise();
    Edge RotateCounterClockwise();

    /* Applies `edge` (from persistence) without firing the callback. */
    void ApplyEdge(Edge edge);

    /* Minimum thickness (cross-axis) in logical pixels. */
    void SetThickness(int logicalThickness) noexcept { m_thickness = logicalThickness; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void RegisterClassOnce(HINSTANCE instance);
    void Dock(Edge edge);

    HWND m_hwnd = nullptr;
    HMONITOR m_monitor = nullptr;
    Edge m_edge = Edge::Bottom;
    int m_thickness = 40;
    AppBarPositioner* m_appbar = nullptr;   /* owned, created after hwnd */
    TaskbarWindowCallbacks m_callbacks;
};

} // namespace w7t
