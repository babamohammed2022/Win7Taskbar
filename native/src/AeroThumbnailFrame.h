// Win7Taskbar - cornice 9-slice delle anteprime (Aero thumbnail frame)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// v3.9: disegno GDI della cornice delle anteprime con la tecnica del
// 9-slice. Le otto porzioni del bordo vivono nella cartella Resources/
// accanto all'eseguibile (top_left, top_center, top_right, mid_left,
// mid_right, bottom_left, bottom_center, bottom_right; lo slot centrale
// NON esiste: resta trasparente, li' ci vive il contenuto vero della
// thumbnail o il fill di ripiego del chiamante).
//
// Contratto: DrawAeroThumbnailFrame9Slice ritorna false quando il
// rettangolo di destinazione non contiene la somma dei bordi o quando
// anche una sola immagine non si carica; in quel caso il chiamante
// disegna il rettangolo tradizionale con
// DrawAeroThumbnailFrameFallback, cosi' a schermo c'e' comunque
// qualcosa. Nessun hook, nessuna dipendenza esterna: solo WIC (decodifica
// PNG, gia' in uso nel progetto) e GDI (AlphaBlend via DrawBitmapScaled).

#pragma once
#include <windows.h>
#include <cstddef>   /* size_t  */
#include <cstdint>   /* uint8_t */

namespace w7t {

/* Spessori del bordo, in pixel, riferiti al bordo sorgente originale
 * (DWMBorder.png, 236x166): la fascia superiore ospita il chrome col
 * titolo (38 px), i fianchi e il fondo sono il bordo sottile. */
constexpr int kAeroFrameLeft   = 17;
constexpr int kAeroFrameRight  = 17;
constexpr int kAeroFrameTop    = 38;
constexpr int kAeroFrameBottom = 19;

/* Draws the 9-part frame into `dst`.
 *
 * - the 4 corners are drawn 1:1, unscaled, anchored to the four corners;
 * - the 4 edges are stretched ONLY along the direction they run
 *   (top_center/bottom_center horizontally, mid_left/mid_right vertically),
 *   keeping their source thickness in the other direction;
 * - the centre is never touched: that is where the thumbnail content lives.
 *
 * `accent` == 0: the slices are drawn exactly as they are on disk.
 * `accent` != 0: the slices are born as a grayscale mask (they are cut from
 * DWMBorder.png) and get tinted with that colour. The alpha is not copied
 * verbatim: it is the source alpha modulated by the slice luminance with the
 * same integer formula the frontend uses to build its shaded mask
 * (TaskbarWindow.EnsureDwmPreviewBorderMask), because the WPF frame fills
 * DwmPreviewAccentBrush through that derived mask. Same input, same output:
 * the two paths draw the same border.
 *
 * Returns false when there is not enough room (width < left+right or
 * height < top+bottom) or when the images did not load: the caller then falls
 * back to the plain rectangle. */
bool DrawAeroThumbnailFrame9Slice(HDC hdc, const RECT& dst,
                                  COLORREF accent = 0);

/* Plain-rectangle fallback: solid dark fill + simple border.
 * Only meaningful when DrawAeroThumbnailFrame9Slice returned false. */
void DrawAeroThumbnailFrameFallback(HDC hdc, const RECT& dst);

/* Renders the frame into a caller-owned buffer instead of a window DC:
 * premultiplied BGRA, top-down, stride = width * 4 - the byte layout the
 * other bitmap exports of the core already use (W7T_GetWindowIconBitmap), so
 * a WPF frontend can wrap the result in a Pbgra32 BitmapSource with no
 * conversion. This is the entry point that lets the managed preview popup use
 * the 9-slice renderer: the frame is drawn here, once per size and accent,
 * and handed over as an image.
 *
 * Returns false - without touching the buffer - when the 9-slice set is not
 * applicable: slices missing, size below the border sum, or a failed DC/DIB
 * creation. The caller then keeps its own frame path. */
bool RenderAeroThumbnailFramePbgra(int width, int height, COLORREF accent,
                                   uint8_t* pixels, size_t pixelsBytes);

} /* namespace w7t */
