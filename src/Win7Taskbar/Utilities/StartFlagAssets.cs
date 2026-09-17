// Win7Taskbar - bandierina Start 8.1 renderizzata via GDI+
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

using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// v1.21.27 - pulsante Start della skin Windows 8.1 piu' nitido.
    ///
    /// Lo sprite incorporato (GraphicalResourceBundle.startwin81flag*) e' una
    /// bandierina a tre stati da 54/108 px che il tema mappa sul pulsante da
    /// 53 DIP: il ricampionamento non intero di WPF (filtro bilineare) ammorbidisce
    /// i bordi netti dei quattro riquadri e l'icona appare sfocata, specie sopra il
    /// 100% di ridimensionamento.
    ///
    /// Qui la bandierina viene RICAMPIONATA una volta sola con GDI+
    /// (System.Drawing, InterpolationMode.HighQualityBicubic) alla dimensione
    /// dispositivo ESATTA del pulsante (53 DIP * scala), lavorando in alpha
    /// premoltiplicato per non creare aloni sui bordi trasparenti; il risultato e'
    /// mappato 1:1 dal pennello del tema e resta nitido.
    ///
    /// Il membro statico <see cref="Sprite"/> e' letto dal tema
    /// (Themes/Windows8.1.xaml, chiavi StartButtonImageLarge/...Scaled) come
    /// x:Static a tempo di parse; <see cref="ApplyToResources"/> lo ripubblica
    /// nella radice di Application.Resources quando cambia la scala del monitor,
    /// cosi' la barra resta nitida anche su schermi misti. Qualunque errore
    /// ripiega sullo sprite incorporato originale: mai null, mai un avvio rotto.
    /// </summary>
    public static class StartFlagAssets
    {
        /// <summary>Larghezza logica del pulsante Start nella skin 8.1.</summary>
        private const double ButtonDip = 53.0;

        private static readonly object Gate = new object();
        private static double _cachedScale = -1.0;
        private static BitmapSource? _cached;

        /// <summary>
        /// Sprite a tre stati (normale/hover/premuto) impilati, ricampionato alla
        /// scala del monitor primario. Non ritorna mai null: su qualunque errore
        /// consegna lo sprite incorporato originale.
        /// </summary>
        public static BitmapSource? Sprite => RenderCached(CurrentScale());

        /// <summary>
        /// Ripubblica lo sprite nitido nelle chiavi del tema quando la scala del
        /// monitor della barra cambia. Chiamato da TaskbarWindow.UpdateDpiScaling.
        /// Agisce solo con la skin 8.1 attiva, per non toccare l'orb di Windows 7.
        /// </summary>
        public static void ApplyToResources(double deviceScale)
        {
            try
            {
                if (RetroBar.Utilities.TaskbarThemeIds.Normalize(
                        RetroBar.Utilities.Settings.Instance.ThemeSelection)
                    != RetroBar.Utilities.TaskbarThemeIds.Windows81)
                {
                    return;
                }

                var application = Application.Current;
                if (application == null)
                {
                    return;
                }

                BitmapSource? sprite = RenderCached(deviceScale);
                if (sprite == null)
                {
                    return;
                }

                application.Resources["StartButtonImageLarge"] = sprite;
                application.Resources["StartButtonImageLargeScaled"] = sprite;
            }
            catch
            {
                // Puramente cosmetico: il tema resta con lo sprite di parse.
            }
        }

        /// <summary>Scala del monitor primario, 1.0 se non rilevabile.</summary>
        private static double CurrentScale()
        {
            try
            {
                using var g = System.Drawing.Graphics.FromHwnd(IntPtr.Zero);
                float dpi = g.DpiX;
                if (dpi > 0f)
                {
                    return dpi / 96.0;
                }
            }
            catch
            {
                // Ripiego sotto.
            }

            return 1.0;
        }

        private static BitmapSource? RenderCached(double scale)
        {
            lock (Gate)
            {
                if (_cached != null && Math.Abs(scale - _cachedScale) < 0.0001)
                {
                    return _cached;
                }

                BitmapSource? rendered = Render(scale);
                _cachedScale = scale;
                _cached = rendered;
                return rendered;
            }
        }

        /// <summary>
        /// Ricampiona lo sprite con GDI+ alla dimensione dispositivo del pulsante.
        /// Lavora in Pbgra32 (alpha premoltiplicato) sia in ingresso sia in uscita,
        /// cosi' il filtro bicubico di GDI+ non scurisce i bordi trasparenti.
        /// </summary>
        private static BitmapSource? Render(double scale)
        {
            BitmapSource? stock =
                GraphicalResourceBundle.startwin81flagscaled ??
                GraphicalResourceBundle.startwin81flag;
            if (stock == null || stock.PixelHeight < 3 || stock.PixelWidth < 1)
            {
                return stock;
            }

            int slice = (int)Math.Round(ButtonDip * scale);
            if (slice <= 0)
            {
                slice = (int)ButtonDip;
            }

            int srcSlice = stock.PixelHeight / 3;
            if (srcSlice <= 0)
            {
                return stock;
            }

            try
            {
                // Sorgente WPF -> pixel premoltiplicati.
                var converted = new FormatConvertedBitmap(
                    stock, PixelFormats.Pbgra32, null, 0);
                int srcStride = converted.PixelWidth * 4;
                byte[] srcPixels = new byte[srcStride * converted.PixelHeight];
                converted.CopyPixels(srcPixels, srcStride, 0);

                using var source = new System.Drawing.Bitmap(
                    converted.PixelWidth, converted.PixelHeight,
                    System.Drawing.Imaging.PixelFormat.Format32bppPArgb);

                var srcData = source.LockBits(
                    new System.Drawing.Rectangle(0, 0, source.Width, source.Height),
                    System.Drawing.Imaging.ImageLockMode.WriteOnly,
                    System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
                for (int y = 0; y < source.Height; y++)
                {
                    Marshal.Copy(srcPixels, y * srcStride,
                                 IntPtr.Add(srcData.Scan0, y * srcData.Stride),
                                 srcStride);
                }
                source.UnlockBits(srcData);

                int dstWidth = slice;
                int dstHeight = slice * 3;

                using var target = new System.Drawing.Bitmap(
                    dstWidth, dstHeight,
                    System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
                using (var g = System.Drawing.Graphics.FromImage(target))
                {
                    g.InterpolationMode =
                        System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                    g.SmoothingMode =
                        System.Drawing.Drawing2D.SmoothingMode.HighQuality;
                    g.PixelOffsetMode =
                        System.Drawing.Drawing2D.PixelOffsetMode.HighQuality;
                    g.CompositingQuality =
                        System.Drawing.Drawing2D.CompositingQuality.HighQuality;

                    for (int state = 0; state < 3; state++)
                    {
                        g.DrawImage(
                            source,
                            new System.Drawing.Rectangle(0, state * slice, slice, slice),
                            new System.Drawing.Rectangle(0, state * srcSlice, srcSlice, srcSlice),
                            System.Drawing.GraphicsUnit.Pixel);
                    }

                    g.Flush();
                }

                var dstData = target.LockBits(
                    new System.Drawing.Rectangle(0, 0, target.Width, target.Height),
                    System.Drawing.Imaging.ImageLockMode.ReadOnly,
                    System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
                int dstStride = slice * 4;
                byte[] dstPixels = new byte[dstStride * dstHeight];
                for (int y = 0; y < dstHeight; y++)
                {
                    Marshal.Copy(IntPtr.Add(dstData.Scan0, y * dstData.Stride),
                                 dstPixels, y * dstStride, dstStride);
                }
                target.UnlockBits(dstData);

                var result = BitmapSource.Create(
                    dstWidth, dstHeight, 96.0, 96.0,
                    PixelFormats.Pbgra32, null, dstPixels, dstStride);
                if (result.CanFreeze)
                {
                    result.Freeze();
                }

                return result;
            }
            catch
            {
                // Il ricampionamento e' un miglioramento: se non riesce, lo sprite
                // originale resta al suo posto.
                return stock;
            }
        }
    }
}
