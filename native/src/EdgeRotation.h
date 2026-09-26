// Win7Taskbar - Core nativo - rotazione barra ai bordi (04)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 04_edge_rotation (snippet integration): pure rotation arithmetic.
// The bar lives on one of the four monitor edges (like the Win7 bar is
// docked); rotating = computing the next edge and the new orientation.
//   - next_edge_cw / next_edge_ccw: clockwise / counterclockwise walk
//     of Left -> Top -> Right -> Bottom (the documented shell order).
//   - edge_relation: derives Left/Right/Top/Bottom/Unrelated from one
//     rectangle against another (taskbar against monitor).
//   - Vertical() / Reverse(): orientation flags for bands.
// Purity guarantees: no Win32 handles, no side effects. UNIT TESTABLE.

#pragma once

#include <cstdint>

namespace w7t {

enum class Edge : std::uint8_t { Left = 0, Top, Right, Bottom };

/* Orientation / layout flags for a band on the current edge. */
constexpr std::uint32_t kBandVertical = 0x1u;   /* edge is Left/Right */
constexpr std::uint32_t kBandReverse  = 0x2u;   /* scroll order flipped */

inline bool IsVerticalEdge(Edge e) noexcept {
    return e == Edge::Left || e == Edge::Right;
}

constexpr Edge NextEdgeCw(Edge e) noexcept {
    switch (e) {
    case Edge::Left:   return Edge::Top;
    case Edge::Top:    return Edge::Right;
    case Edge::Right:  return Edge::Bottom;
    default:           return Edge::Left;
    }
}

constexpr Edge NextEdgeCcw(Edge e) noexcept {
    switch (e) {
    case Edge::Left:   return Edge::Bottom;
    case Edge::Top:    return Edge::Left;
    case Edge::Right:  return Edge::Top;
    default:           return Edge::Right;
    }
}

/* EdgeRelation (snippet 04 helper): where sits `subject` relative to
 * `reference`? Used to classify the taskbar against its monitor. */
enum class EdgeRelation : std::uint8_t {
    Unrelated = 0, Left, Right, Top, Bottom
};

struct RectI {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int width() const noexcept { return right - left; }
    int height() const noexcept { return bottom - top; }
};

inline EdgeRelation EdgeRelationFor(const RectI& subject,
                                   const RectI& reference) noexcept {
    /* Anchored to a side and spanning its length: that side wins. */
    const bool flush_left   = subject.left  <= reference.left;
    const bool flush_right  = subject.right >= reference.right;
    const bool flush_top    = subject.top   <= reference.top;
    const bool flush_bottom = subject.bottom >= reference.bottom;

    const bool spans_x = subject.width() >= (reference.width() * 3) / 4;
    const bool spans_y = subject.height() >= (reference.height() * 3) / 4;

    if (flush_left && spans_y && !flush_right)  return EdgeRelation::Left;
    if (flush_right && spans_y && !flush_left)  return EdgeRelation::Right;
    if (flush_top && spans_x && !flush_bottom)  return EdgeRelation::Top;
    if (flush_bottom && spans_x && !flush_top)  return EdgeRelation::Bottom;
    return EdgeRelation::Unrelated;
}

} // namespace w7t
