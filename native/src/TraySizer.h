// Win7Taskbar - Core nativo - resize barra sul bordo libero (06)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 06_tray_sizer (snippet integration, bodies implemented): when the
// bar is NOT auto-hidden ("unlocked" state) its FREE edge — the one
// facing the desktop — behaves like a resizable window border: the
// size grip lives in a logical-pixel band around that edge, dragging
// moves the border, the thickness is clamped to min/max LOGICAL values
// scaled with the monitor DPI (DpiMetrics + the 04 rotation values),
// and the new size is consolidated when the drag ends (WM_EXITSIZEMOVE).
// When auto-hide is on the bar is "locked": no resize border.
//
// Multi-row (vertical bar = rows, horizontal = columns) is declared
// here; the row/column bodies land with the multi-row milestone
// (// TODO: verificare).
//
// Pure parts (hit test, clamp, border thickness) are UNIT TESTABLE.

#pragma once

#include <windows.h>
#include <functional>
#include <optional>
#include <utility>

#include "EdgeRotation.h"

namespace w7t {

/* Thickness of the draggable band around the free edge (pure).
 * Returned in PHYSICAL pixels for the given dpi. */
inline int EdgeResizeBorderThicknessPx(int logicalThickness, UINT dpi) noexcept {
    const int scaled = MulDiv(logicalThickness, static_cast<int>(dpi ? dpi : 96), 96);
    return scaled < 2 ? 2 : scaled;
}

/* Clamps a thickness (logical) to the allowed band (pure). */
inline int ClampThickness(int thickness, int minThickness, int maxThickness) noexcept {
    if (thickness < minThickness) return minThickness;
    if (thickness > maxThickness) return maxThickness;
    return thickness;
}

/* Which side of the bar faces the desktop (pure). */
inline Edge FreeEdgeFromEdge(Edge docked) noexcept {
    switch (docked) {
    case Edge::Left:   return Edge::Right;   /* docked left -> free = right */
    case Edge::Top:    return Edge::Bottom;
    case Edge::Right:  return Edge::Left;
    default:           return Edge::Top;
    }
}

/* Hit test: is `pt` (screen) inside the resize band of the free edge
 * of `rc`? `border` is in physical px. Pure. */
bool HitFreeEdge(POINT pt, RECT rc, int border, Edge freeEdge) noexcept;

/* Size limits and drag state for the free-edge sizer. Multi-row
 * declarations included (// TODO: verificare on the bodies). */
class FreeEdgeSizer {
public:
    struct Limits {
        int minThicknessLogical = 28;
        int maxThicknessLogical = 200;
        int borderLogical = 4;
    };

    /* Fired on WM_EXITSIZEMOVE with the consolidated thickness. */
    using ConsolidateFn = std::function<void(int newThicknessLogical)>;

    FreeEdgeSizer(HWND hwnd, Edge dockedEdge, Limits limits,
                  ConsolidateFn onConsolidate);

    /* The bar's "locked" state = auto-hide on: the free edge shows no
     * resize border and drag is refused. */
    void SetLocked(bool locked) noexcept { m_locked = locked; }
    bool locked() const noexcept { return m_locked; }

    void SetThicknessLogical(int thickness) noexcept;
    int thicknessLogical() const noexcept { return m_thickness; }

    Edge freeEdge() const noexcept { return FreeEdgeFromEdge(m_edge); }

    /* Route these from the host window proc. Returns the message
     * result when consumed, nullopt when the host should call
     * DefWindowProc (WM_NCHITTEST yields the HT* code). */
    std::optional<LRESULT> HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    /* DPI-scaled limits for the monitor (04 values x 07 DPI). */
    void SetDpi(UINT dpi) noexcept { m_dpi = dpi ? dpi : 96; }

    /* --- multi-row declarations (// TODO: verificare) ------------- */
    /* Vertical bar = rows of buttons; horizontal = columns. */
    void SetRows(int rows) noexcept;          // TODO: verificare
    void SetColumns(int columns) noexcept;    // TODO: verificare
    int rows() const noexcept { return m_rows; }
    int columns() const noexcept { return m_columns; }

private:
    /* Recomputes the per-row/column size after a resize (stub until
     * the multi-row milestone). */
    void UpdateRowColumnSizes(int thicknessLogical);   // TODO: verificare

    HWND m_hwnd = nullptr;
    Edge m_edge = Edge::Bottom;
    Limits m_limits;
    ConsolidateFn m_onConsolidate;
    UINT m_dpi = 96;
    int m_thickness = 40;
    bool m_locked = false;
    bool m_dragging = false;
    int m_rows = 1;
    int m_columns = 1;
};

} // namespace w7t
