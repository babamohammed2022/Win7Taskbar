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
// This class is the bridge between the two. It asks the core for the frame at
// the exact device size of one preview and returns it as a frozen Pbgra32
// BitmapSource; the template then paints it through the brush of its
// NativeAeroFrameRect and collapses the eight masked rectangles. The grayscale
// overlay, the clipped static blur and the close button stay exactly as they
// are: only the accent layer changes hands.
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

        /// <summary>Frames kept, one per (device size, accent, DPI). A group
        /// shows a handful of previews, each within the 202x109 aperture plus
        /// borders, so this stays tiny; it is dropped wholesale when it grows
        /// and whenever the accent colour changes.</summary>
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
        /// size is the frame's size (the preview's frame ContentControl).
        /// Returns null - meaning "keep the XAML frame" - when the element is
        /// not laid out yet or the core cannot supply the frame.
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
                /* v1.21.18: the size handed to the core is the size the frame
                 * is RENDERED at, in device pixels, read from the visual
                 * itself (PointToScreen maps through the whole transform
                 * chain). "DIP size x monitor DPI" was only true while the
                 * preview popup scaled with the monitor: the popup now keeps
                 * its 100%-DPI pixel geometry (see
                 * TaskbarWindow.ApplyPreviewPopupDpiNormalisation), and at
                 * 125% a 166-DIP-tall frame lands on 207.5 device pixels, so
                 * the raw multiplication could hand the core a size the frame
                 * is not painted at - and the image would then be resampled,
                 * which is exactly the soft border this class exists to
                 * avoid. */
                Point topLeft = host.PointToScreen(new Point(0, 0));
                Point bottomRight = host.PointToScreen(new Point(width, height));
                int pxWidth = (int)Math.Round(bottomRight.X - topLeft.X);
                int pxHeight = (int)Math.Round(bottomRight.Y - topLeft.Y);
                if (pxWidth <= 0 || pxHeight <= 0 ||
                    pxWidth > MaxSidePx || pxHeight > MaxSidePx)
                {
                    return null;
                }

                /* Effective device pixels per DIP, from the measured size: the
                 * bitmap is declared at that DPI, so the brush maps it 1:1
                 * onto the element and the 9-slice corners are never
                 * resampled. */
                double scaleX = pxWidth / width;
                double scaleY = pxHeight / height;
                if (scaleX <= 0 || scaleY <= 0)
                {
                    return null;
                }

                if (!TryGetAccentArgb(out uint accent))
                {
                    return null;
                }

                var key = new FrameKey(pxWidth, pxHeight, accent, scaleX, scaleY);
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
                 * width * 4: that is Pbgra32 exactly, no conversion. */
                BitmapSource bitmap = BitmapSource.Create(
                    pxWidth, pxHeight,
                    96.0 * scaleX, 96.0 * scaleY,
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
                $"native 9-slice frame in use: {pxWidth}x{pxHeight}px " +
                $"accent=0x{accent:X6} (the XAML accent layer is collapsed; " +
                "it returns if this render ever fails)");
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

        /// <summary>Cache identity of one rendered frame.</summary>
        private readonly struct FrameKey : IEquatable<FrameKey>
        {
            private readonly int _width;
            private readonly int _height;
            private readonly uint _accent;
            private readonly double _scaleX;
            private readonly double _scaleY;

            public FrameKey(int width, int height, uint accent,
                            double scaleX, double scaleY)
            {
                _width = width;
                _height = height;
                _accent = accent;
                _scaleX = scaleX;
                _scaleY = scaleY;
            }

            public bool Equals(FrameKey other)
                => _width == other._width &&
                   _height == other._height &&
                   _accent == other._accent &&
                   _scaleX.Equals(other._scaleX) &&
                   _scaleY.Equals(other._scaleY);

            public override bool Equals(object? obj)
                => obj is FrameKey other && Equals(other);

            public override int GetHashCode()
            {
                /* Small value set: the two sizes and the accent are enough to
                 * keep collisions rare, the scales only refine it. */
                int hash = _width;
                hash = (hash * 397) ^ _height;
                hash = (hash * 397) ^ (int)_accent;
                hash = (hash * 397) ^ _scaleX.GetHashCode();
                hash = (hash * 397) ^ _scaleY.GetHashCode();
                return hash;
            }
        }
    }
}
