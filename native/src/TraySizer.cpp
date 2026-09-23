// Win7Taskbar - Core nativo - resize barra sul bordo libero
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "TraySizer.h"

#include "DpiMetrics.h"

namespace w7t {

bool HitFreeEdge(POINT pt, RECT rc, int border, Edge freeEdge) noexcept {
    if (border < 1) border = 1;
    switch (freeEdge) {
    case Edge::Left:
        return pt.x >= rc.left && pt.x < rc.left + border &&
               pt.y >= rc.top && pt.y < rc.bottom;
    case Edge::Right:
        return pt.x < rc.right && pt.x >= rc.right - border &&
               pt.y >= rc.top && pt.y < rc.bottom;
    case Edge::Top:
        return pt.y >= rc.top && pt.y < rc.top + border &&
               pt.x >= rc.left && pt.x < rc.right;
    default: /* Bottom */
        return pt.y < rc.bottom && pt.y >= rc.bottom - border &&
               pt.x >= rc.left && pt.x < rc.right;
    }
}

FreeEdgeSizer::FreeEdgeSizer(HWND hwnd, Edge dockedEdge, Limits limits,
                             ConsolidateFn onConsolidate)
    : m_hwnd(hwnd), m_edge(dockedEdge), m_limits(limits),
      m_onConsolidate(std::move(onConsolidate)) {}

void FreeEdgeSizer::SetThicknessLogical(int thickness) noexcept {
    m_thickness = ClampThickness(thickness,
                                 m_limits.minThicknessLogical,
                                 m_limits.maxThicknessLogical);
    UpdateRowColumnSizes(m_thickness);
}

std::optional<LRESULT> FreeEdgeSizer::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        if (m_locked) return std::nullopt;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT rc = {};
        if (m_hwnd == nullptr || !GetWindowRect(m_hwnd, &rc)) return std::nullopt;
        const int border = EdgeResizeBorderThicknessPx(m_limits.borderLogical, m_dpi);
        if (!HitFreeEdge(pt, rc, border, freeEdge())) return std::nullopt;
        switch (freeEdge()) {
        case Edge::Left:   return HTLEFT;
        case Edge::Right:  return HTRIGHT;
        case Edge::Top:    return HTTOP;
        default:           return HTBOTTOM;
        }
    }

    case WM_ENTERSIZEMOVE:
        if (m_locked) return std::nullopt;
        m_dragging = true;
        return std::nullopt;   /* let the system modal size loop run */

    case WM_SIZING: {
        if (m_locked || !m_dragging) return std::nullopt;
        /* Clamp the dragged border to min/max logical * DPI. */
        RECT* rc = reinterpret_cast<RECT*>(lp);
        const int minPx = ScaleForDpi(m_limits.minThicknessLogical, m_dpi);
        const int maxPx = ScaleForDpi(m_limits.maxThicknessLogical, m_dpi);
        const Edge fe = freeEdge();
        if (fe == Edge::Right) {
            const int w = rc->right - rc->left;
            if (w < minPx) rc->right = rc->left + minPx;
            else if (w > maxPx) rc->right = rc->left + maxPx;
        } else if (fe == Edge::Left) {
            const int w = rc->right - rc->left;
            if (w < minPx) rc->left = rc->right - minPx;
            else if (w > maxPx) rc->left = rc->right - maxPx;
        } else if (fe == Edge::Bottom) {
            const int h = rc->bottom - rc->top;
            if (h < minPx) rc->bottom = rc->top + minPx;
            else if (h > maxPx) rc->bottom = rc->top + maxPx;
        } else {
            const int h = rc->bottom - rc->top;
            if (h < minPx) rc->top = rc->bottom - minPx;
            else if (h > maxPx) rc->top = rc->bottom - maxPx;
        }
        return TRUE;
    }

    case WM_EXITSIZEMOVE: {
        if (!m_dragging) return std::nullopt;
        m_dragging = false;
        /* Consolidate: read the final thickness and hand it to the
         * host (no silent state change: the host decides persistence). */
        RECT rc = {};
        if (m_hwnd != nullptr && GetWindowRect(m_hwnd, &rc)) {
            const int thicknessPx = IsVerticalEdge(m_edge) ? rc.right - rc.left
                                                           : rc.bottom - rc.top;
            const int thicknessLogical = MulDiv(thicknessPx, 96,
                                                static_cast<int>(m_dpi ? m_dpi : 96));
            m_thickness = ClampThickness(thicknessLogical,
                                         m_limits.minThicknessLogical,
                                         m_limits.maxThicknessLogical);
            UpdateRowColumnSizes(m_thickness);
            if (m_onConsolidate) m_onConsolidate(m_thickness);
        }
        return std::nullopt;
    }

    case WM_GETMINMAXINFO: {
        if (m_locked) return std::nullopt;
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        const int minPx = ScaleForDpi(m_limits.minThicknessLogical, m_dpi);
        const int maxPx = ScaleForDpi(m_limits.maxThicknessLogical, m_dpi);
        if (IsVerticalEdge(m_edge)) {
            mmi->ptMinTrackSize.x = minPx;
            mmi->ptMaxTrackSize.x = maxPx;
        } else {
            mmi->ptMinTrackSize.y = minPx;
            mmi->ptMaxTrackSize.y = maxPx;
        }
        return 0;
    }

    default:
        return std::nullopt;
    }
}

/* --- multi-row stubs (// TODO: verificare) ---------------------- */

void FreeEdgeSizer::SetRows(int rows) noexcept {
    m_rows = rows < 1 ? 1 : rows;
    UpdateRowColumnSizes(m_thickness);
}

void FreeEdgeSizer::SetColumns(int columns) noexcept {
    m_columns = columns < 1 ? 1 : columns;
    UpdateRowColumnSizes(m_thickness);
}

void FreeEdgeSizer::UpdateRowColumnSizes(int thicknessLogical) {
    /* Multi-row split of the cross-axis into rows (vertical bar) or
     * columns (horizontal bar) lands with the multi-row milestone.
     * Single row/column today: nothing to redistribute yet. */
    (void)thicknessLogical;   // TODO: verificare
}

} // namespace w7t
