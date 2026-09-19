// Win7Taskbar - preview frame rendered by the native core
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// ============================================================================
// WHY THIS FILE EXISTS
//
// The border around a live preview is the 9-slice of the supplied
// DWMBorder.png: four pixel-sized corners, four edges stretched only along
// their own direction, and a transparent centre where the DWM thumbnail
// lives. Two implementations of that border exist in the project:
//
//   * the XAML frame template (Themes/Overrides.xaml, TaskPreviewFrameVista),
//     which builds it with eight masked rectangles filled with the DWM accent
//     brush plus a restrained grayscale overlay;
//   * the core's GDI renderer (native/src/AeroThumbnailFrame.cpp), which
//     builds it from the eight slice PNGs that ship next to the executable.
//
// This class is the bridge between the two. It asks the core for the frame in
// the LAYOUT units of one preview - the same 17/38/19-unit slices and the same
// 202x109 aperture the frame template declares - and returns it as a frozen
// Pbgra32 BitmapSource; the template then paints it through the brush of its
// NativeAeroFrameRect and collapses the eight masked rectangles. The grayscale
// overlay, the clipped static blur and the close button stay exactly as they
// are: only the accent layer changes hands.
//
// WHY LAYOUT UNITS, NOT SCREEN PIXELS. The 9-slice has to surround the same
// rectangle DWM is told to fill (TaskThumbnail.TryGetDestinationRect): the
// frame template's middle row/column, 17/38/19 units thick. Asking the core
// for the frame at the element's device size made the slices stay 17/38/19
// SCREEN pixels while that aperture moved with the popup - 17/38/19 DIP, i.e.
// 18.7/41.8/20.9 px at 125% under the 10% high-DPI enlargement - so the live
// surface stopped short of the border and left an unpainted strip inside it
// (the "empty edge" reported on a 125% display). Describing the bitmap with
// the layout's own unit makes the opening of the 9-slice and the aperture the
// same rectangle by construction at every scale, and the compositor scales
// border and aperture together.
//
// FALLBACK IS THE RULE, NOT THE EXCEPTION. Every failure - core not
// initialized, slice PNGs missing from the extracted package, size smaller
// than the border sum, a translucent accent brush this path cannot reproduce,
// any exception - returns null, and the caller leaves the XAML frame in
// place. Nothing here can take the preview popup down, and a package that
// lost its Resources folder simply keeps drawing the frame it drew before.
//
// THREADING: the cache is a plain dictionary, so this class must be called
// from the WPF UI thread only (it is: from the frame's Loaded/SizeChanged
// handlers and from the accent-colour update).
// ============================================================================

using System;
using System.Collections.Generic;
using System.Threading;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Supplies the Aero preview frame rendered by the core's native 9-slice
    /// path, or <c>null</c> when the XAML frame has to stay.
    /// </summary>
    internal static class NativePreviewFrame
    {
        /// <summary>Resource key the XAML frame fills its accent layer with.
        /// Read here so the two paths always tint with the same colour.</summary>
        private const string AccentBrushKey = "DwmPreviewAccentBrush";

        /// <summary>Frames kept, one per (layout size, accent). A group shows a
        /// handful of previews, each within the 202x109 aperture plus borders,
        /// so this stays tiny; it is dropped wholesale when it grows and
        /// whenever the accent colour changes.</summary>
        private const int MaxCacheEntries = 24;

        /// <summary>Largest frame the core accepts (it rejects anything
        /// bigger, and 4096 x 4096 x 4 still fits an int byte count).</summary>
        private const int MaxSidePx = 4096;

        private static readonly Dictionary<FrameKey, BitmapSource> Cache = new();

        /* Diagnostics are strictly best-effort and must not repeat: one line
         * says which path is drawing the frame, one line reports the first
         * failure. Both go through DiagnosticLogger, which is a no-op unless
         * logging is enabled. */
        private static int _appliedLogged;
        private static int _failureLogged;

        /// <summary>
        /// Renders the frame for <paramref name="host"/>, the element whose
        /// size is the frame's size (the preview's frame ContentControl). The
        /// bitmap is produced in that element's layout units, so its 9-slice
        /// opening is the same rectangle as the template's aperture on any
        /// monitor (see the file header). Returns null - meaning "keep the
        /// XAML frame" - when the element is not laid out yet or the core
        /// cannot supply the frame.
        /// </summary>
        public static BitmapSource? TryRender(FrameworkElement? host)
        {
            if (host == null)
            {
                return null;
            }

            /* Size in device-independent units; 0 means "not laid out yet",
             * and the frame's SizeChanged will call us again once it is. */
            double width = host.ActualWidth;
            double height = host.ActualHeight;
            if (width <= 0 || height <= 0)
            {
                return null;
            }

            try
            {
                /* v1.1.1: the core is given the element's LAYOUT size (device
                 * independent pixels), not its device size.
                 *
                 * The frame is a 9-slice of 17/38/19-unit slices whose opening
                 * must be exactly the cell the live thumbnail is placed in -
                 * the same 17/38/19 the template reserves around its
                 * ContentPresenter. Only a bitmap described in the layout's
                 * own unit lands there at every display scaling: the brush
                 * fills the element, so a bitmap of 236x166 units maps onto a
                 * 259.6x182.6-pixel frame at 125% and its 17-unit slice ends up
                 * 18.7 px thick, exactly like the template's 17-DIP band.
                 *
                 * Rendering at the DEVICE size instead (as this class did up
                 * to v1.1.0) kept the slices 17/38/19 pixels wide while the
                 * aperture moved with the popup, and the live surface never
                 * reached the border: a 1.7 px strip on the sides and 3.8 px
                 * under the title band were left unpainted at 125%. */
                int pxWidth = (int)Math.Round(width);
                int pxHeight = (int)Math.Round(height);
                if (pxWidth <= 0 || pxHeight <= 0 ||
                    pxWidth > MaxSidePx || pxHeight > MaxSidePx)
                {
                    return null;
                }

                if (!TryGetAccentArgb(out uint accent))
                {
                    return null;
                }

                var key = new FrameKey(pxWidth, pxHeight, accent);
                if (Cache.TryGetValue(key, out var cached))
                {
                    return cached;
                }

                /* Query, then render: the same two-call contract the icon
                * exports of the core use. A negative answer means the frame
                * is not applicable (missing slices, size below the border
                * sum), which is a normal condition, not an error. */
                int needed = NativeMethods.W7T_RenderAeroThumbnailFrame(
                    pxWidth, pxHeight, accent, null, 0);
                if (needed != pxWidth * pxHeight * 4)
                {
                    LogFailure($"query returned {needed} for {pxWidth}x{pxHeight}");
                    return null;
                }

                var pixels = new byte[needed];
                int written = NativeMethods.W7T_RenderAeroThumbnailFrame(
                    pxWidth, pxHeight, accent, pixels, pixels.Length);
                if (written != needed)
                {
                    LogFailure($"render returned {written} of {needed} bytes");
                    return null;
                }

                /* The core writes premultiplied BGRA, top-down, stride
                 * width * 4: that is Pbgra32 exactly, no conversion.
                 *
                 * Declared at 96 DPI on purpose: one bitmap pixel is one
                 * layout unit of the element the brush fills (Stretch.Fill
                 * stretches it to whatever device size the popup has), so the
                 * same bitmap is valid at 100%, 125% and 150% and the cache is
                 * shared between monitors. */
                BitmapSource bitmap = BitmapSource.Create(
                    pxWidth, pxHeight,
                    96.0, 96.0,
                    PixelFormats.Pbgra32, null,
                    pixels, pxWidth * 4);
                bitmap.Freeze();

                if (Cache.Count >= MaxCacheEntries)
                {
                    Cache.Clear();
                }
                Cache[key] = bitmap;

                LogApplied(pxWidth, pxHeight, accent);
                return bitmap;
            }
            catch (Exception ex)
            {
                /* The XAML frame is still there: a failure here is invisible
                 * to the user, so it is logged once and swallowed. */
                LogFailure(ex.GetType().Name + ": " + ex.Message);
                return null;
            }
        }

        /// <summary>
        /// Drops every cached frame. Called when the DWM colorization colour
        /// changes: the cached bitmaps were tinted with the previous accent
        /// and would keep showing it until the popup is rebuilt.
        /// </summary>
        public static void Invalidate()
        {
            try
            {
                Cache.Clear();
            }
            catch
            {
                /* Nothing to recover: the next render simply misses. */
            }
        }

        /// <summary>
        /// Reads the accent colour the XAML frame uses, as 0x00RRGGBB for the
        /// core. Returns false - keeping the XAML frame - when the resource is
        /// missing or when the brush is not a plain opaque colour, because the
        /// native tint reproduces an opaque accent only.
        /// </summary>
        private static bool TryGetAccentArgb(out uint argb)
        {
            argb = 0;
            try
            {
                if (Application.Current?.TryFindResource(AccentBrushKey)
                    is not SolidColorBrush brush)
                {
                    return false;
                }

                if (brush.Opacity < 1.0 || brush.Color.A != 0xFF)
                {
                    return false;
                }

                argb = ((uint)brush.Color.R << 16) |
                       ((uint)brush.Color.G << 8) |
                       brush.Color.B;

                /* 0 is the core's "no tint" value: a pure black accent would
                 * be drawn as the untouched grayscale slices, so leave that
                 * (never observed, DWM colorization is not black) to XAML. */
                return argb != 0;
            }
            catch
            {
                return false;
            }
        }

        private static void LogApplied(int pxWidth, int pxHeight, uint accent)
        {
            if (Interlocked.CompareExchange(ref _appliedLogged, 1, 0) != 0)
            {
                return;
            }

            DiagnosticLogger.Write("PREVIEW",
                $"native 9-slice frame in use: {pxWidth}x{pxHeight} layout " +
                $"units (17/38/19 slices, 202x109 aperture, scaled by the " +
                $"popup) accent=0x{accent:X6} (the XAML accent layer is " +
                "collapsed; it returns if this render ever fails)");
        }

        private static void LogFailure(string reason)
        {
            if (Interlocked.CompareExchange(ref _failureLogged, 1, 0) != 0)
            {
                return;
            }

            DiagnosticLogger.Write("PREVIEW",
                "native 9-slice frame not applied, keeping the XAML frame: " +
                reason);
        }

        /// <summary>Cache identity of one rendered frame. The size is in layout
        /// units (see <see cref="TryRender"/>), so the entry is valid on every
        /// monitor: the DPI is applied by the brush, not by the bitmap.</summary>
        private readonly struct FrameKey : IEquatable<FrameKey>
        {
            private readonly int _width;
            private readonly int _height;
            private readonly uint _accent;

            public FrameKey(int width, int height, uint accent)
            {
                _width = width;
                _height = height;
                _accent = accent;
            }

            public bool Equals(FrameKey other)
                => _width == other._width &&
                   _height == other._height &&
                   _accent == other._accent;

            public override bool Equals(object? obj)
                => obj is FrameKey other && Equals(other);

            public override int GetHashCode()
            {
                /* Small value set: the two sizes and the accent are enough. */
                int hash = _width;
                hash = (hash * 397) ^ _height;
                hash = (hash * 397) ^ (int)_accent;
                return hash;
            }
        }
    }
}
