// Win7Taskbar - unit tests for the icon bitmap guards
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Covers the two checks that decide whether a converted icon is good enough
// to put on a taskbar button: BitmapSane (shape) and BitmapHasContent (does
// it actually draw something). A conversion that comes back well formed but
// completely transparent is what an empty button looks like, and it used to
// be cached for good: these tests pin both behaviours.
//
// Plain main() + CHECK: no test framework dependency.

#include <windows.h>

#include <cstdio>
#include <vector>

#include "../src/Common.h"

using namespace w7t;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            /* "::error::" is a workflow command: it becomes a visible      \
             * annotation even without the job log (repo convention). */   \
            std::printf("::error::FAIL %s:%d  %s\n", __FILE__, __LINE__,   \
                        #cond);                                            \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

/* A width*height*4 BGRA buffer, fully transparent by default. */
static ArgbBitmap MakeBitmap(int w, int h) {
    ArgbBitmap bmp;
    bmp.width = w;
    bmp.height = h;
    bmp.pixels.assign(static_cast<size_t>(w) * h * 4u, 0u);
    return bmp;
}

static void SetPixel(ArgbBitmap& bmp, int x, int y,
                     uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    const size_t i = (static_cast<size_t>(y) * bmp.width + x) * 4u;
    bmp.pixels[i + 0] = b;
    bmp.pixels[i + 1] = g;
    bmp.pixels[i + 2] = r;
    bmp.pixels[i + 3] = a;
}

static void TestBitmapSane() {
    const ArgbBitmap good = MakeBitmap(32, 32);
    CHECK(BitmapSane(good));

    ArgbBitmap wrong = MakeBitmap(32, 32);
    wrong.pixels.resize(wrong.pixels.size() - 4u);
    CHECK(!BitmapSane(wrong));

    ArgbBitmap zero = MakeBitmap(0, 0);
    CHECK(!BitmapSane(zero));

    ArgbBitmap huge = MakeBitmap(2, 2);
    huge.width = 99999;
    CHECK(!BitmapSane(huge));

    /* Shape is all BitmapSane judges: a transparent bitmap is still sane,
     * which is exactly why BitmapHasContent has to exist next to it. */
    CHECK(BitmapSane(MakeBitmap(16, 16)));
}

static void TestBitmapHasContent() {
    CHECK(!BitmapHasContent(MakeBitmap(0, 0)));

    ArgbBitmap transparent = MakeBitmap(32, 32);
    CHECK(!BitmapHasContent(transparent));

    ArgbBitmap black = MakeBitmap(32, 32);
    for (size_t i = 3; i < black.pixels.size(); i += 4) {
        black.pixels[i] = 255u;
    }
    CHECK(!BitmapHasContent(black));

    ArgbBitmap drawn = MakeBitmap(32, 32);
    SetPixel(drawn, 0, 0, 0x00, 0x00, 0xFF, 0xFF);   /* opaque red */
    CHECK(BitmapHasContent(drawn));

    ArgbBitmap semi = MakeBitmap(8, 8);
    SetPixel(semi, 7, 7, 0x10, 0x20, 0x30, 0x01);    /* barely visible */
    CHECK(BitmapHasContent(semi));
}

static void TestStockIconConverts() {
    /* End-to-end on the one icon every Windows install has: the conversion
     * has to produce a bitmap that passes BOTH guards, or the button would
     * come out empty. */
    HICON stock = LoadIconW(nullptr, IDI_APPLICATION);
    if (stock == nullptr) {
        return;   /* no default icon on this system: nothing to assert */
    }
    ArgbBitmap bmp;
    CHECK(IconToArgb(stock, bmp));
    CHECK(BitmapSane(bmp));
    CHECK(BitmapHasContent(bmp));
}

int main() {
    TestBitmapSane();
    TestBitmapHasContent();
    TestStockIconConverts();
    if (g_failures == 0) {
        std::printf("icon_bitmap_tests: all checks passed\n");
        return 0;
    }
    std::printf("icon_bitmap_tests: %d check(s) failed\n", g_failures);
    return 1;
}
