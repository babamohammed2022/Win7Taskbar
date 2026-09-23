// Win7Taskbar - Core nativo - layout orologio/area notifica
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "ClockTrayLayout.h"

namespace w7t {

TrayBandRects LayoutClockAndTray(Edge edge, const RectI& band,
                                 const TrayBandSizes& sizes,
                                 bool reverse) noexcept {
    const bool vertical = IsVerticalEdge(edge);
    const int total = vertical ? band.height() : band.width();

    int clock = sizes.clock < 0 ? 0 : sizes.clock;
    int tray = sizes.tray < 0 ? 0 : sizes.tray;
    if (tray + clock > total) {
        /* Icons shrink first (Win7: the clock never disappears). */
        const int overflow = (tray + clock) - total;
        tray = tray > overflow ? tray - overflow : 0;
        if (tray + clock > total) {
            clock = total - tray;
            if (clock < 0) clock = 0;
        }
    }

    TrayBandRects out;
    RectI trayR = band;
    RectI clockR = band;
    if (vertical) {
        /* Clock at the outer end of the edge (bottom for Left/Right). */
        clockR.top = band.bottom - clock;
        trayR = band;
        trayR.bottom = clockR.top;
        if (reverse) {
            /* Flip: clock at the inner end (top), icons below. */
            clockR = band;
            clockR.bottom = band.top + clock;
            trayR = band;
            trayR.top = clockR.bottom;
        }
    } else {
        /* Clock at the right end for Top/Bottom (outer end). */
        clockR.left = band.right - clock;
        trayR = band;
        trayR.right = clockR.left;
        if (reverse) {
            clockR = band;
            clockR.right = band.left + clock;
            trayR = band;
            trayR.left = clockR.right;
        }
    }
    out.tray = trayR;
    out.clock = clockR;
    return out;
}

RectI ClockFlyoutRect(Edge edge, const RectI& clockRect,
                      int flyoutWidth, int flyoutHeight) {
    /* The calendar flyout anchors to the clock on the free side of the
     * bar (above a bottom bar, below a top bar...). Stub until the
     * tray polish milestone. // TODO: verificare */
    (void)edge;
    (void)flyoutWidth;
    (void)flyoutHeight;
    return clockRect;
}

} // namespace w7t
