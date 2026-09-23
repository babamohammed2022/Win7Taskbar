// Win7Taskbar - Core nativo - DPI per-monitor
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "DpiMetrics.h"

namespace w7t {

namespace {

using GetThreadDpiAwarenessContextFn = HANDLE (WINAPI*)();
using GetAwarenessFromDpiAwarenessContextFn = int (WINAPI*)(HANDLE);

struct ApiTable {
    GetThreadDpiAwarenessContextFn get_ctx = nullptr;
    GetAwarenessFromDpiAwarenessContextFn awareness_from = nullptr;

    ApiTable() {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            get_ctx = reinterpret_cast<GetThreadDpiAwarenessContextFn>(
                reinterpret_cast<void*>(GetProcAddress(user32, "GetThreadDpiAwarenessContext")));
            awareness_from = reinterpret_cast<GetAwarenessFromDpiAwarenessContextFn>(
                reinterpret_cast<void*>(GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext")));
        }
    }
};

const ApiTable& Api() noexcept {
    static const ApiTable table;   /* C++11 magic statics: thread-safe init */
    return table;
}

} // namespace

UINT MonitorDpi(HMONITOR monitor) noexcept {
    if (monitor != nullptr) {
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(monitor, &mi)) {
            /* Reuse the repo's ladder on the monitor rectangle. */
            return GetDpiForScreenRect(mi.rcMonitor);
        }
    }
    return GetDpiForScreenRect(RECT{ 0, 0, 0, 0 });
}

UINT WindowDpi(HWND hwnd) noexcept {
    /* Common.h already implements the full ladder (GetDpiForWindow
     * dynamic on 10 1607+, monitor fallback, GetDeviceCaps, >= 96). */
    return GetDpiForWindowSafe(hwnd);
}

bool IsProcessPerMonitorDpiAware() noexcept {
    const ApiTable& api = Api();
    if (api.get_ctx == nullptr || api.awareness_from == nullptr) {
        return false;   /* pre-8.1: no per-monitor context API */
    }
    /* DPI_AWARENESS enum: -1 unknown, 0 unaware, 1 system, 2 per-monitor. */
    const int awareness = api.awareness_from(api.get_ctx());
    return awareness == 2;
}

} // namespace w7t
