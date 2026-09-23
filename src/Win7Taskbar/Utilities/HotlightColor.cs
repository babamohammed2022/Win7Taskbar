// Win7Taskbar - Color hot-track of the taskbar buttons
// English: the light that lights up a taskbar button while the mouse is over
// it, tinted with the most dominant colour of the application icon.
// Italiano: la luce che si accende sul pulsante della barra sotto il mouse,
// tinta col colore dominante dell'icona dell'applicazione.
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// ---------------------------------------------------------------------------
// WHAT MICROSOFT DOCUMENTS
//
// The behaviour is the "Color hot-track" described by Raymond Chen on the
// official Microsoft developer blog ("A feature I didn't even know existed
// much less had a name: Color hot-track", The Old New Thing, 2011-12-06):
//
//   * "When you hover your mouse over a button in the Windows 7 taskbar
//     which corresponds to a running application, the taskbar button lights
//     up in a color that matches the colors in the icon itself";
//   * "the lighting effect is centered on the mouse";
//   * "The code just looks for the predominant color in the icon. (And, since
//     visual designers are sticklers for this sort of thing, black, white, and
//     shades of gray are not considered 'colors' for the purpose of this
//     calculation.)".
//
// Those three sentences are what this file implements, and nothing more:
// the dominant colour of the icon (greys, white and black excluded), a radial
// light centered on the cursor. No Windows API is involved and nothing about
// the icon, the theme or the system is ever modified: the light is drawn by
// our own WPF buttons, on top of the glossy tile of the theme.
// ---------------------------------------------------------------------------

using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Dominant-colour extraction and radial light of the Color hot-track.
    /// </summary>
    internal static class HotlightColor
    {
        /// <summary>Edge of the square the icon is sampled at. The colour of
        /// an icon does not change at this size, and it keeps the whole
        /// computation to a few hundred pixels on the UI thread.</summary>
        private const int SampleEdge = 24;

        /// <summary>4 bits per channel: 4096 candidate colours.</summary>
        private const int BucketShift = 4;
        private const int BucketsPerChannel = 1 << (8 - BucketShift);
        private const int BucketCount = BucketsPerChannel * BucketsPerChannel * BucketsPerChannel;

        /// <summary>Below this value a channel is "black" for the calculation.</summary>
        private const int MinValue = 0x28;

        /// <summary>A pixel counts as a colour only above this saturation
        /// (saturation in percent of the brightest channel).</summary>
        private const int MinSaturationPercent = 18;

        /// <summary>Minimum share of the icon's opaque pixels that must be
        /// colours before the icon is said to have a dominant colour at all
        /// (percent). A badge or a thin highlight is not a dominant colour:
        /// without this floor the accent would take its hue from a handful of
        /// pixels and jump around between icons that are really colourless.
        /// Below the floor the icon counts as black/white/grey and lights
        /// neutral (v2.65).</summary>
        private const int MinCoveragePercent = 10;

        /// <summary>Blend of the winning colour toward white, so the light is
        /// a light and not the flat colour of the icon.</summary>
        private const float LightenTint = 0.35f;

        /// <summary>Overall strength of the accent: it is not a constant of
        /// this file. The theme owns it, in the two `Hotlight` opacity
        /// animations of each template (12%, inside the 10-15% band asked
        /// for after the 5% of v1.21.34 was judged too faint).</summary>

        /// <summary>Neutral light: white-blue Aero glass, used when the icon
        /// has no colour at all (a black, white or grey icon).</summary>
        public static readonly Color NeutralLight = Color.FromRgb(0xD6, 0xF0, 0xFF);

        /// <summary>Vertical centre of the light on a horizontal taskbar, the
        /// same bias the previous static glow had.</summary>
        public const double DefaultCenterX = 0.5;
        public const double DefaultCenterY = 0.62;

        /// <summary>Radii of the spot, as fractions of the button. Small on
        /// purpose: the accent has to read as a spot of light around the
        /// cursor, not as a wash over the whole tile. The vertical radius is
        /// the larger one so the spot fits the tall shape of a taskbar
        /// button.</summary>
        private const double SpotRadiusX = 0.55;
        private const double SpotRadiusY = 0.72;

        private const int MaxCacheEntries = 256;

        private static readonly object Sync = new();
        private static readonly Dictionary<ulong, Color> Cache = new();

        /// <summary>
        /// Dominant colour of an icon, or <see cref="NeutralLight"/> when the
        /// icon carries no colour. <paramref name="cacheKey"/> identifies the
        /// sampled pixels so a caller can tell two icons apart without
        /// recomputing anything.
        /// </summary>
        public static Color DominantLight(ImageSource? icon, out ulong cacheKey)
        {
            cacheKey = 0;
            try
            {
                Color fallback = NeutralLight;
                if (icon is not BitmapSource bitmap || bitmap.PixelWidth <= 0 || bitmap.PixelHeight <= 0)
                {
                    return fallback;
                }

                var converted = new FormatConvertedBitmap(bitmap, PixelFormats.Bgra32, null, 0);
                BitmapSource sampled = converted;
                double scale = (double)SampleEdge / Math.Max(converted.PixelWidth, converted.PixelHeight);
                if (scale < 1.0)
                {
                    sampled = new TransformedBitmap(converted, new ScaleTransform(scale, scale));
                }

                int width = sampled.PixelWidth;
                int height = sampled.PixelHeight;
                if (width <= 0 || height <= 0)
                {
                    return fallback;
                }

                int stride = width * 4;
                byte[] pixels = new byte[stride * height];
                sampled.CopyPixels(pixels, stride, 0);

                cacheKey = Fnv1a(pixels);
                lock (Sync)
                {
                    if (Cache.TryGetValue(cacheKey, out Color cached))
                    {
                        return cached;
                    }
                }

                Color light = DominantLightFromPixels(pixels);
                lock (Sync)
                {
                    if (Cache.Count >= MaxCacheEntries)
                    {
                        Cache.Clear();
                    }
                    Cache[cacheKey] = light;
                }
                return light;
            }
            catch (Exception)
            {
                /* An icon that cannot be sampled must not break a hover: the
                 * neutral light is the documented fallback. */
                return NeutralLight;
            }
        }

        /// <summary>
        /// The three rules of the blog post applied to a BGRA32 buffer: skip
        /// black, white and the shades of grey, then pick the predominant
        /// colour among the pixels that are left. On top of them one
        /// robustness rule of our own (v2.65): the colours must cover enough
        /// of the icon (<see cref="MinCoveragePercent"/>) to deserve the name
        /// "dominant", otherwise the light stays neutral.
        /// </summary>
        private static Color DominantLightFromPixels(byte[] bgra)
        {
            int[] weight = new int[BucketCount];
            long[] sumR = new long[BucketCount];
            long[] sumG = new long[BucketCount];
            long[] sumB = new long[BucketCount];
            int opaque = 0;
            int coloured = 0;

            for (int i = 0; i + 3 < bgra.Length; i += 4)
            {
                byte b = bgra[i];
                byte g = bgra[i + 1];
                byte r = bgra[i + 2];
                byte a = bgra[i + 3];

                /* Transparent pixels are not part of the icon. */
                if (a < 128)
                {
                    continue;
                }
                opaque++;

                int max = Math.Max(r, Math.Max(g, b));
                int min = Math.Min(r, Math.Min(g, b));
                int saturation = max - min;

                /* Black: nothing to light up. */
                if (max < MinValue)
                {
                    continue;
                }
                /* White and the shades of grey: not "colours" for this
                 * calculation (this also covers white, where saturation ~ 0). */
                if (saturation * 100 < max * MinSaturationPercent)
                {
                    continue;
                }
                coloured++;

                int bucket = ((r >> BucketShift) << (2 * (8 - BucketShift)))
                           | ((g >> BucketShift) << (8 - BucketShift))
                           | (b >> BucketShift);

                /* The more saturated (vivid) the pixel, the stronger its vote:
                 * a big pale area must not outvote a vivid one. */
                int vote = saturation * saturation;
                weight[bucket] += vote;
                sumR[bucket] += (long)r * vote;
                sumG[bucket] += (long)g * vote;
                sumB[bucket] += (long)b * vote;
            }

            /* v2.65: a few coloured pixels are not a dominant colour. */
            if (opaque <= 0 || coloured * 100 < opaque * MinCoveragePercent)
            {
                return NeutralLight;
            }

            int best = -1;
            for (int i = 0; i < BucketCount; i++)
            {
                if (weight[i] > 0 && (best < 0 || weight[i] > weight[best]))
                {
                    best = i;
                }
            }
            if (best < 0)
            {
                /** Every pixel was black, white or grey. */
                return NeutralLight;
            }

            long total = weight[best];
            var dominant = Color.FromRgb(
                (byte)(sumR[best] / total),
                (byte)(sumG[best] / total),
                (byte)(sumB[best] / total));

            return Lighten(dominant, LightenTint);
        }

        /// <summary>
        /// Radial light of the hot-track: a small bright tinted core that
        /// fades through the application colour into transparency. The
        /// overall weight is not set here: it is the opacity the theme
        /// animates (12%), so the two things stay independent. Proportional
        /// coordinates, so it scales with the button at any DPI.
        /// </summary>
        public static RadialGradientBrush CreateLightBrush(Color color)
        {
            Color bright = Lighten(color, 0.55f);

            var brush = new RadialGradientBrush
            {
                MappingMode = BrushMappingMode.RelativeToBoundingBox,
                Center = new Point(DefaultCenterX, DefaultCenterY),
                GradientOrigin = new Point(DefaultCenterX, DefaultCenterY),
                RadiusX = SpotRadiusX,
                RadiusY = SpotRadiusY
            };
            brush.GradientStops.Add(new GradientStop(WithAlpha(bright, 0xE8), 0.0));
            brush.GradientStops.Add(new GradientStop(WithAlpha(color, 0x9C), 0.38));
            brush.GradientStops.Add(new GradientStop(WithAlpha(color, 0x3C), 0.72));
            brush.GradientStops.Add(new GradientStop(WithAlpha(color, 0x00), 1.0));
            return brush;
        }

        /// <summary>
        /// Moves the light source ("the lighting effect is centered on the
        /// mouse"): the fractions are the cursor position inside the button,
        /// 0..1 on both axes.
        /// </summary>
        public static void MoveLight(RadialGradientBrush? brush, double fractionX, double fractionY)
        {
            if (brush == null)
            {
                return;
            }

            double x = Clamp(fractionX);
            double y = Clamp(fractionY);
            var point = new Point(x, y);
            brush.Center = point;
            brush.GradientOrigin = point;
        }

        public static void ResetLight(RadialGradientBrush? brush)
        {
            if (brush == null)
            {
                return;
            }
            var point = new Point(DefaultCenterX, DefaultCenterY);
            brush.Center = point;
            brush.GradientOrigin = point;
        }

        private static double Clamp(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value))
            {
                return 0.5;
            }
            return Math.Max(0.06, Math.Min(0.94, value));
        }

        private static Color Lighten(Color color, float amount)
        {
            float keep = 1f - amount;
            return Color.FromRgb(
                (byte)(color.R * keep + 255 * amount),
                (byte)(color.G * keep + 255 * amount),
                (byte)(color.B * keep + 255 * amount));
        }

        private static Color WithAlpha(Color color, byte alpha)
        {
            return Color.FromArgb(alpha, color.R, color.G, color.B);
        }

        /// <summary>FNV-1a: identifies the sampled pixels, so the extraction
        /// runs once per icon instead of once per hover.</summary>
        private static ulong Fnv1a(byte[] data)
        {
            const ulong offset = 14695981039346656037UL;
            const ulong prime = 1099511628211UL;
            ulong hash = offset;
            for (int i = 0; i < data.Length; i++)
            {
                hash ^= data[i];
                hash *= prime;
            }
            return hash;
        }
    }
}
