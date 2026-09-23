// Win7Taskbar - Core nativo - layout orologio/area notifica (15)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 15_clock_tray_layout (snippet integration): how the clock and the
// notification area share the tray band at the END of the bar (Win7
// order along the edge: tasks | tray | clock, clock always the last
// sliver at the outer end). Orientation flags come from 04.
// Pure geometry — UNIT TESTABLE. Clock behaviours (flyout placement)
// are declarations here; the flyout bodies land with the tray polish
// milestone (// TODO: verificare).

#pragma once

#include "EdgeRotation.h"

namespace w7t {

struct TrayBandSizes {
    int tray = 0;      /* notification icons length (logical) */
    int clock = 0;     /* clock length (logical) */
};

struct TrayBandRects {
    RectI tray;
    RectI clock;
};

/* Splits `band` (the tray band slice from BandLayout) into the icon
 * area and the clock: the clock takes the LAST `clock` logical pixels
 * along the edge (outer end), the icons the rest. `reverse` flips the
 * internal order (kBandReverse on back edges). */
TrayBandRects LayoutClockAndTray(Edge edge, const RectI& band,
                                 const TrayBandSizes& sizes,
                                 bool reverse = false) noexcept;

/* --- declarations (// TODO: verificare) ------------------------- */

/* Window behavior flags mirrored from the snippet's clock window. */
enum class ClockFlyoutMode : int {
    Normal = 0,
    FlyoutOnTop = 1   /* flyout paints over the bar (Win7 default) */
};

/* Placement of the calendar flyout against the bar (stub). */
RectI ClockFlyoutRect(Edge edge, const RectI& clockRect,
                      int flyoutWidth, int flyoutHeight);   // TODO: verificare

} // namespace w7t
