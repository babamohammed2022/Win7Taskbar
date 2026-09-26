// Win7Taskbar - Core nativo - geometria bande
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "BandLayout.h"

namespace w7t {

namespace {

RectI Slice(const RectI& area, Edge edge, int start, int length) noexcept {
    /* Places a [start, start+length) slice along `edge` of `area`. */
    RectI r = area;
    if (IsVerticalEdge(edge)) {
        r.top = area.top + start;
        r.bottom = r.top + length;
    } else {
        r.left = area.left + start;
        r.right = r.left + length;
    }
    return r;
}

} // namespace

BandRects LayoutBandsOnEdge(Edge edge, const RectI& bar_area,
                            const BandSizes& sizes) noexcept {
    const int total = IsVerticalEdge(edge) ? bar_area.height() : bar_area.width();

    int tray = sizes.tray < 0 ? 0 : sizes.tray;
    int clock = sizes.clock < 0 ? 0 : sizes.clock;
    int tasks = sizes.task_list < 0 ? 0 : sizes.task_list;

    int fixed = tray + clock;
    if (tasks + fixed > total) {
        /* Insufficient space: shrink the fixed bands first, keeping the
         * clock at least half of its desired size, then the task list. */
        int overflow = (tasks + fixed) - total;
        const int shrinkable = tray + (clock - clock / 2);
        const int take = overflow < shrinkable ? overflow : shrinkable;
        if (take <= tray) {
            tray -= take;
        } else {
            const int from_clock = take - tray;
            tray = 0;
            clock -= from_clock;
        }
        overflow -= take;
        tasks = tasks > overflow ? tasks - overflow : 0;
    }

    /* Regola Win7 (e contratto dell'header): il task list assorbe il
     * resto della barra, le bande fisse restano alle loro dimensioni
     * desiderate all'estremita' esterna. */
    tasks = total - tray - clock;
    if (tasks < 0) tasks = 0;

    /* Win7 order along the edge: tasks first, then tray, then clock. */
    BandRects out;
    int cursor = 0;
    out.task_list = Slice(bar_area, edge, cursor, tasks);
    cursor += tasks;
    out.tray = Slice(bar_area, edge, cursor, tray);
    cursor += tray;
    out.clock = Slice(bar_area, edge, cursor, clock);
    return out;
}

} // namespace w7t
