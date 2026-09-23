// Win7Taskbar - Core nativo - DPI per-monitor (8.1 / 10 / 11)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 07_dpi_metrics (snippet integration): rotation and resizing work on
// LOGICAL values scaled with the DPI of the target monitor, or the bar
// is paper-thin on 4K. Public availability ladder:
//   - GetDpiForWindow / GetDpiForSystem : Windows 10 1607+ (user32)
//   - GetDpiForMonitor                  : Windows 8.1+ (shcore)
//   - GetDeviceCaps(LOGPIXELSX)         : always (system DPI fallback)
// The repo already centralises the dynamic-loading ladder in Common.h
// (GetDpiForScreenRect / GetDpiForWindowSafe); this header adds the
// monitor/handle-shaped entry points and the manifest-first check.
// Never hard-import GetDpiForWindow: Windows 8.1's user32 does not
// export it and the loader would refuse the whole DLL (see Common.h).

#pragma once

#include "Common.h"

namespace w7t {

/* DPI of a monitor (falls back to the system DPI); never 0, never < 96. */
UINT MonitorDpi(HMONITOR monitor) noexcept;

/* DPI of a window's monitor (Common.h ladder); never < 96. */
UINT WindowDpi(HWND hwnd) noexcept;

/* Scales a logical (96 dpi) value with correct rounding. */
inline int ScaleForDpi(int logical, UINT dpi) noexcept {
    return MulDiv(logical, static_cast<int>(dpi ? dpi : 96), 96);
}

/* Manifest-first check: true only when the process really is
 * per-monitor aware. False on pre-8.1 systems or when the manifest
 * does not declare the context (project rule: manifest-first, no
 * implicit runtime context changes). */
bool IsProcessPerMonitorDpiAware() noexcept;

} // namespace w7t
