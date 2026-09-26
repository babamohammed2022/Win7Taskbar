// Win7Taskbar - Core nativo - geometria bande (12)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 12_band_geometry (snippet integration): how the task list, the
// notification area and the clock divide the available length of the
// bar. Split is integer-arithmetic only (no handles, no DPI here: the
// caller passes LOGICAL pixels already scaled with DpiMetrics).
// Purity guarantees: UNIT TESTABLE.

#pragma once

#include <cstdint>

#include "EdgeRotation.h"

namespace w7t {

struct BandSizes {
    int task_list = 0;      /* desired length of the running-task band */
    int tray = 0;           /* desired length of the notification area */
    int clock = 0;          /* desired length of the clock */
};

struct BandRects {
    RectI task_list;
    RectI tray;
    RectI clock;
};

/* Distributes `available` (the bar's inner length, margins excluded)
 * across the three bands in fixed Win7 order (tasks, tray, clock).
 * Rules:
 *   - fixed bands (tray, clock) always get their desired size;
 *   - the task list absorbs the remainder and may shrink to zero;
 *   - when the space is insufficient the fixed bands shrink together,
 *     keeping the clock at least half of its desired size.
 */
BandRects LayoutBandsOnEdge(Edge edge, const RectI& bar_area,
                            const BandSizes& sizes) noexcept;

} // namespace w7t
