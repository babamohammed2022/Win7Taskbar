// Win7Taskbar - test unitari puri: rotazione/bande/resize/clock
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Covers the pure parts of the integrated modules (mapping in
// docs/native-module-notes.md): 04 EdgeRotation, 12 BandLayout,
// 15 ClockTrayLayout, the pure helpers of 06 TraySizer.
// Plain main() + CHECK: no test framework dependency.

#include <windows.h>

#include <cstdio>

#include "../src/BandLayout.h"
#include "../src/ClockTrayLayout.h"
#include "../src/TraySizer.h"

using namespace w7t;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static void TestEdgeRotation() {
    /* 04: ciclo orario Left->Top->Right->Bottom->Left e inverso. */
    CHECK(NextEdgeCw(Edge::Left) == Edge::Top);
    CHECK(NextEdgeCw(Edge::Top) == Edge::Right);
    CHECK(NextEdgeCw(Edge::Right) == Edge::Bottom);
    CHECK(NextEdgeCw(Edge::Bottom) == Edge::Left);
    CHECK(NextEdgeCcw(Edge::Left) == Edge::Bottom);
    CHECK(NextEdgeCcw(Edge::Bottom) == Edge::Right);
    CHECK(NextEdgeCcw(Edge::Right) == Edge::Top);
    CHECK(NextEdgeCcw(Edge::Top) == Edge::Left);
    CHECK(IsVerticalEdge(Edge::Left) && IsVerticalEdge(Edge::Right));
    CHECK(!IsVerticalEdge(Edge::Top) && !IsVerticalEdge(Edge::Bottom));

    /* 04: classificazione bordo contro il monitor. */
    const RectI mon{ 0, 0, 1920, 1080 };
    const RectI bottomBar{ 0, 1040, 1920, 1080 };
    const RectI leftBar{ 0, 0, 40, 1080 };
    const RectI floating{ 800, 400, 1000, 500 };
    CHECK(EdgeRelationFor(bottomBar, mon) == EdgeRelation::Bottom);
    CHECK(EdgeRelationFor(leftBar, mon) == EdgeRelation::Left);
    CHECK(EdgeRelationFor(floating, mon) == EdgeRelation::Unrelated);
}

static void TestBandLayout() {
    /* 12: split ordinario tasks|tray|clock lungo un bordo orizzontale. */
    const RectI bar{ 0, 1040, 1000, 1080 };
    BandSizes sizes;
    sizes.task_list = 600;
    sizes.tray = 200;
    sizes.clock = 100;
    const BandRects r = LayoutBandsOnEdge(Edge::Bottom, bar, sizes);
    CHECK(r.task_list.width() == 600);
    CHECK(r.tray.width() == 200);
    CHECK(r.clock.width() == 100);
    CHECK(r.task_list.left == 0 && r.task_list.right == 600);
    CHECK(r.tray.left == 600 && r.tray.right == 800);
    CHECK(r.clock.left == 800 && r.clock.right == 1000);

    /* 12: spazio insufficiente — le fisse stringono, il clock resta
     * almeno meta', il task list si azzera per ultimo. */
    const RectI small{ 0, 0, 200, 40 };
    BandSizes big;
    big.task_list = 500;
    big.tray = 200;
    big.clock = 100;
    const BandRects s = LayoutBandsOnEdge(Edge::Top, small, big);
    CHECK(s.clock.width() >= 50);           /* mai sotto meta' */
    CHECK(s.task_list.width() + s.tray.width() + s.clock.width() == 200);
    CHECK(s.task_list.left == 0);
}

static void TestClockTrayLayout() {
    /* 15: l'orologio occupa l'estremita' esterna del band. */
    const RectI band{ 700, 1040, 1000, 1080 };
    TrayBandSizes sizes;
    sizes.tray = 200;
    sizes.clock = 80;
    const TrayBandRects r = LayoutClockAndTray(Edge::Bottom, band, sizes, false);
    CHECK(r.clock.width() == 80 && r.clock.right == 1000);
    CHECK(r.tray.width() == 220 && r.tray.left == 700);

    /* 15: reverse (faccia interna) — orologio dall'altra estremita'. */
    const TrayBandRects rev = LayoutClockAndTray(Edge::Bottom, band, sizes, true);
    CHECK(rev.clock.left == 700 && rev.clock.right == 780);
    CHECK(rev.tray.left == 780 && rev.tray.right == 1000);

    /* 15: verticale (barra a destra). */
    const RectI vband{ 1880, 0, 1920, 400 };
    const TrayBandRects v = LayoutClockAndTray(Edge::Right, vband, sizes, false);
    CHECK(v.clock.height() == 80 && v.clock.bottom == 400);
    CHECK(v.tray.top == 0 && v.tray.bottom == 320);
}

static void TestTraySizerPure() {
    /* 06: hit test sul bordo libero. */
    const RECT rc{ 0, 1040, 1000, 1080 };
    POINT onBottom{ 500, 1078 };
    POINT inside{ 500, 1050 };
    POINT outside{ 500, 1100 };
    CHECK(HitFreeEdge(onBottom, rc, 4, Edge::Bottom));
    CHECK(!HitFreeEdge(inside, rc, 4, Edge::Bottom));
    CHECK(!HitFreeEdge(outside, rc, 4, Edge::Bottom));

    const RECT vrc{ 1880, 0, 1920, 800 };
    POINT onRight{ 1918, 400 };
    POINT onLeft{ 1881, 400 };
    CHECK(HitFreeEdge(onRight, vrc, 4, Edge::Right));
    CHECK(!HitFreeEdge(onLeft, vrc, 4, Edge::Right));

    /* 06: clamp e spessore bordo scalato DPI. */
    CHECK(ClampThickness(10, 28, 200) == 28);
    CHECK(ClampThickness(500, 28, 200) == 200);
    CHECK(ClampThickness(40, 28, 200) == 40);
    CHECK(EdgeResizeBorderThicknessPx(4, 96) == 4);
    CHECK(EdgeResizeBorderThicknessPx(4, 192) == 8);
    CHECK(EdgeResizeBorderThicknessPx(1, 96) >= 2);   /* mai invisibile */

    /* 06: bordo libero = lato opposto al dock. */
    CHECK(FreeEdgeFromEdge(Edge::Left) == Edge::Right);
    CHECK(FreeEdgeFromEdge(Edge::Top) == Edge::Bottom);
    CHECK(FreeEdgeFromEdge(Edge::Right) == Edge::Left);
    CHECK(FreeEdgeFromEdge(Edge::Bottom) == Edge::Top);
}

int main() {
    TestEdgeRotation();
    TestBandLayout();
    TestClockTrayLayout();
    TestTraySizerPure();
    if (g_failures != 0) {
        std::printf("layout_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("layout_tests: all checks passed\n");
    return 0;
}
