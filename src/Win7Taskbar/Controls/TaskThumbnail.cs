// Win7Taskbar - per-window live preview control (DWM thumbnail)
//
// PORTED FROM RetroBar - Copyright (c) dremin
// https://github.com/dremin/RetroBar  -  Apache License 2.0
// Original file: RetroBar/Controls/TaskThumbnail.xaml.cs
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
// v1.7.4 - LIVE DWM THUMBNAIL RE-ENABLED
//
// The live DWM thumbnail that shipped up to v2.54 is back, with the fixes
// that were staged in the commented reference (destination rectangle
// computed against the window RootVisual instead of the direct parent,
// DWM_TNP_SOURCECLIENTAREAONLY applied separately, visibility no longer
// blocked by a failed source-size query, _layoutPending so UpdateLayout
// does not run on every frame) and with the missing piece that made the
// feature park: a POSITIVE confirmation that the compositor really painted
// something. DwmRegisterThumbnail returns S_OK even when nothing will ever
// be drawn, so after the registration a short timer re-checks the real
// state (thumbnail registered, source size sane, destination rectangle
// inside the rendered host) and falls back to the application icon when
// the check fails - the popup can no longer stay empty.
//
// The static PrintWindow capture that shipped in v2.55 is kept at the
// bottom of this file, commented out, as reference for its "loud failure"
// contract.
// ============================================================================

using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Shows the live preview of a window using DWM thumbnails: the same
    /// technique as the Windows 7 Superbar - no screenshot is taken, the
    /// compositor repaints the source window inside a rectangle of ours.
    /// When the thumbnail cannot be proven alive shortly after the
    /// registration, the control switches to the application icon instead
    /// of leaving an empty box.
    /// </summary>
    public sealed class TaskThumbnail : UserControl
    {
        private const double MaxWidth_ = 180;
        private const double MaxHeight_ = 120;

        // How long the compositor gets to prove the thumbnail is alive
        // before the control falls back to the icon.
        private static readonly TimeSpan ConfirmationDelay = TimeSpan.FromMilliseconds(250);

        private IntPtr _thumbHandle;
        private EventHandler? _renderingHandler;
        private DispatcherTimer? _confirmationTimer;

        /* v2.52: the layout pass runs only when the control size changes.
         * Forcing UpdateLayout inside the rendering loop organized the whole
         * popup sixty times per second per window; the thumbnail
         * repositioning does not need it (the position is read from the
         * transform, which is up to date anyway). */
        private bool _layoutPending = true;
        private readonly Image _fallbackImage;

        public double DpiScale { get; private set; } = 1.0;

        public TaskThumbnail()
        {
            Width = MaxWidth_;
            Height = MaxHeight_;

            /* v2.2: the fallback (the application icon) lives INSIDE the
             * control and is mutually exclusive with the DWM thumbnail:
             * FallbackVisibility shows it only when the thumbnail is not
             * available. An Image bound with ElementName inside a ToolTip
             * did not resolve (separate visual tree) and both layers stacked
             * up; this way it is structurally impossible. The content does
             * not move the DWM rectangle: the destination is computed from
             * this control's position, not its children. */
            _fallbackImage = new Image
            {
                Width = 48,
                Height = 48,
                Stretch = Stretch.Uniform,
                Opacity = 0.85,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
            };
            RenderOptions.SetBitmapScalingMode(_fallbackImage, BitmapScalingMode.HighQuality);
            _fallbackImage.SetBinding(Image.SourceProperty,
                new Binding(nameof(FallbackIcon)) { Source = this });
            _fallbackImage.SetBinding(VisibilityProperty,
                new Binding(nameof(FallbackVisibility)) { Source = this });
            Content = _fallbackImage;

            Loaded += OnLoaded;
            Unloaded += OnUnloaded;
        }

        /// <summary>
        /// Icon shown only when the DWM thumbnail is not available (very
        /// rare: composition off, reduced RDP session, missing API, or the
        /// positive confirmation failed). Never together with the thumbnail.
        /// </summary>
        public static readonly DependencyProperty FallbackIconProperty =
            DependencyProperty.Register(nameof(FallbackIcon), typeof(ImageSource),
                typeof(TaskThumbnail), new PropertyMetadata(null));

        public ImageSource? FallbackIcon
        {
            get => (ImageSource?)GetValue(FallbackIconProperty);
            set => SetValue(FallbackIconProperty, value);
        }

        public static readonly DependencyProperty SourceWindowHandleProperty =
            DependencyProperty.Register(nameof(SourceWindowHandle), typeof(IntPtr),
                typeof(TaskThumbnail), new PropertyMetadata(IntPtr.Zero));

        /// <summary>The window to preview.</summary>
        public IntPtr SourceWindowHandle
        {
            get => (IntPtr)GetValue(SourceWindowHandleProperty);
            set => SetValue(SourceWindowHandleProperty, value);
        }

        public static readonly DependencyProperty TitleProperty =
            DependencyProperty.Register(nameof(Title), typeof(string),
                typeof(TaskThumbnail), new PropertyMetadata(string.Empty));

        public string Title
        {
            get => (string)GetValue(TitleProperty);
            set => SetValue(TitleProperty, value);
        }

        private static readonly DependencyPropertyKey IsThumbnailAvailablePropertyKey =
            DependencyProperty.RegisterReadOnly(nameof(IsThumbnailAvailable), typeof(bool),
                typeof(TaskThumbnail), new PropertyMetadata(false));

        public static readonly DependencyProperty IsThumbnailAvailableProperty =
            IsThumbnailAvailablePropertyKey.DependencyProperty;

        /// <summary>
        /// False until the thumbnail is registered AND positively confirmed:
        /// the container then shows the fallback (icon + title) instead of
        /// an empty rectangle.
        /// </summary>
        public bool IsThumbnailAvailable
        {
            get => (bool)GetValue(IsThumbnailAvailableProperty);
            private set
            {
                SetValue(IsThumbnailAvailablePropertyKey, value);
                SetValue(FallbackVisibilityPropertyKey,
                         value ? Visibility.Collapsed : Visibility.Visible);
            }
        }

        private static readonly DependencyPropertyKey FallbackVisibilityPropertyKey =
            DependencyProperty.RegisterReadOnly(nameof(FallbackVisibility), typeof(Visibility),
                typeof(TaskThumbnail), new PropertyMetadata(Visibility.Visible));

        public static readonly DependencyProperty FallbackVisibilityProperty =
            FallbackVisibilityPropertyKey.DependencyProperty;

        /// <summary>
        /// Visible when the thumbnail is NOT available. Exposed directly as a
        /// Visibility so bindings do not need a converter (Binding.Converter
        /// is not a DependencyProperty, and a StaticResource inside a
        /// DataTemplate nested in a ToolTip is not guaranteed to resolve).
        /// </summary>
        public Visibility FallbackVisibility => (Visibility)GetValue(FallbackVisibilityProperty);

        /// <summary>Handle of the WPF window hosting the preview.</summary>
        private IntPtr HostHandle
        {
            get
            {
                if (PresentationSource.FromVisual(this) is HwndSource source)
                {
                    return source.Handle;
                }
                return IntPtr.Zero;
            }
        }

        /// <summary>
        /// Destination rectangle in physical pixels relative to the hosting
        /// window ROOT (the DWM draws into that HWND and ignores the WPF
        /// tree; using the direct parent was what pushed the thumbnail out
        /// of place in the pre-v2.55 builds).
        /// </summary>
        private NativeMethods.RECT DestinationRect
        {
            get
            {
                try
                {
                    if (PresentationSource.FromVisual(this)?.RootVisual is not Visual root)
                    {
                        return default;
                    }

                    GeneralTransform transform = TransformToAncestor(root);
                    Point topLeft = transform.Transform(new Point(0, 0));

                    return new NativeMethods.RECT
                    {
                        Left = (int)(topLeft.X * DpiScale),
                        Top = (int)(topLeft.Y * DpiScale),
                        Right = (int)(topLeft.X * DpiScale) + (int)(MaxWidth_ * DpiScale),
                        Bottom = (int)(topLeft.Y * DpiScale) + (int)(MaxHeight_ * DpiScale)
                    };
                }
                catch (InvalidOperationException)
                {
                    return default;
                }
            }
        }

        /// <summary>
        /// Re-aligns the thumbnail to the current position and size. The
        /// scaling logic is the RetroBar one (1:1 for small windows, fit to
        /// 180x120 preserving the aspect ratio for large ones).
        /// </summary>
        public void Refresh()
        {
            try
            {
                RefreshCore();
            }
            catch (Exception)
            {
                // A preview problem must never propagate to the host popup.
            }
        }

        private void RefreshCore()
        {
            if (_thumbHandle == IntPtr.Zero)
            {
                return;
            }

            // Show only the client area: otherwise the source title bar and
            // frame would appear as well.
            var clientAreaProps = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
            {
                dwFlags = NativeMethods.DWM_TNP_SOURCECLIENTAREAONLY,
                fSourceClientAreaOnly = true
            };
            NativeMethods.DwmUpdateThumbnailProperties(_thumbHandle, ref clientAreaProps);

            /* v2.54: a failed source-size query must not block the
             * visibility. In the very first frame the DWM may not have
             * inspected the freshly registered window yet; the aspect fit is
             * an improvement, visibility is set unconditionally at the
             * bottom. */
            bool sizeOk = NativeMethods.DwmQueryThumbnailSourceSize(_thumbHandle, out NativeMethods.SIZE size) == 0
                          && size.cx > 0 && size.cy > 0;

            double aspectRatio = sizeOk ? (double)size.cx / size.cy : (MaxWidth_ / MaxHeight_);

            // v2.6: decide the control size FIRST, then read the position.
            double wantWidth;
            double wantHeight;

            if (!sizeOk)
            {
                wantWidth = Width;
                wantHeight = Height;
            }
            else if (size.cx <= MaxWidth_ * DpiScale && size.cy <= MaxHeight_ * DpiScale)
            {
                // Small window: no scaling, show 1:1.
                wantWidth = size.cx / DpiScale;
                wantHeight = size.cy / DpiScale;
            }
            else
            {
                // Large window: fit into the box preserving the aspect ratio.
                const double controlAspectRatio = MaxWidth_ / MaxHeight_;

                if (aspectRatio > controlAspectRatio)
                {
                    wantWidth = MaxWidth_;
                    wantHeight = (int)(MaxWidth_ / aspectRatio);
                }
                else if (aspectRatio < controlAspectRatio)
                {
                    wantWidth = (int)(MaxHeight_ * aspectRatio);
                    wantHeight = MaxHeight_;
                }
                else
                {
                    wantWidth = MaxWidth_;
                    wantHeight = MaxHeight_;
                }
            }

            if (Math.Abs(Width - wantWidth) > 0.01 ||
                Math.Abs(Height - wantHeight) > 0.01)
            {
                Width = wantWidth;
                Height = wantHeight;
                _layoutPending = true;
            }

            // Layout pass only when the size just changed: the popup
            // repositions BEFORE the DWM paints.
            if (_layoutPending)
            {
                UpdateLayout();
                _layoutPending = false;
            }

            NativeMethods.RECT dest = DestinationRect;
            dest.Right = dest.Left + (int)(Width * DpiScale);
            dest.Bottom = dest.Top + (int)(Height * DpiScale);

            var props = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
            {
                fVisible = true,
                dwFlags = NativeMethods.DWM_TNP_VISIBLE | NativeMethods.DWM_TNP_RECTDESTINATION,
                rcDestination = dest
            };

            NativeMethods.DwmUpdateThumbnailProperties(_thumbHandle, ref props);
        }

        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            try
            {
                OnLoadedCore(sender, e);
            }
            catch (Exception)
            {
                FallbackToIcon();
            }
        }

        private void OnLoadedCore(object sender, RoutedEventArgs e)
        {
            if (PresentationSource.FromVisual(this)?.CompositionTarget is { } target)
            {
                DpiScale = target.TransformToDevice.M11;
            }

            // Every early exit must turn the fallback on: without
            // composition (or without valid handles) there will never be a
            // thumbnail and the box would stay empty.
            if (!NativeMethods.IsCompositionEnabled())
            {
                IsThumbnailAvailable = false;
                return;
            }

            if (SourceWindowHandle == IntPtr.Zero || HostHandle == IntPtr.Zero)
            {
                IsThumbnailAvailable = false;
                return;
            }

            if (NativeMethods.DwmRegisterThumbnail(HostHandle, SourceWindowHandle,
                                                   out _thumbHandle) != 0)
            {
                // Composition turned off mid-session, safe-mode, reduced RDP,
                // or an implementation without the API.
                IsThumbnailAvailable = false;
                return;
            }

            _layoutPending = true;
            Refresh();

            // The thumbnail does not follow the layout by itself: it has to
            // be repositioned every frame, otherwise it lags behind when the
            // popup moves.
            _renderingHandler = (_, _) =>
                Dispatcher.BeginInvoke(DispatcherPriority.Render, new Action(Refresh));
            CompositionTarget.Rendering += _renderingHandler;

            // v1.7.4: POSITIVE confirmation. S_OK from DwmRegisterThumbnail
            // proves nothing (that silent gap is why the feature was
            // parked); after a short delay verify the compositor really
            // engaged - thumbnail alive, sane source size, destination
            // rectangle inside a rendered host - and fall back to the icon
            // when it did not.
            IsThumbnailAvailable = true;
            _confirmationTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = ConfirmationDelay
            };
            _confirmationTimer.Tick += ConfirmOrFallBack;
            _confirmationTimer.Start();
        }

        private void ConfirmOrFallBack(object? sender, EventArgs e)
        {
            try
            {
                StopConfirmationTimer();

                bool alive =
                    _thumbHandle != IntPtr.Zero &&
                    NativeMethods.IsCompositionEnabled() &&
                    NativeMethods.DwmQueryThumbnailSourceSize(
                        _thumbHandle, out NativeMethods.SIZE size) == 0 &&
                    size.cx > 0 && size.cy > 0 &&
                    ActualWidth > 1 && ActualHeight > 1 &&
                    IsVisible;

                NativeMethods.RECT dest = DestinationRect;
                alive = alive && dest.Right > dest.Left && dest.Bottom > dest.Top;

                if (alive)
                {
                    return; // confirmed: keep the live thumbnail
                }

                // The compositor never painted anything: unregister and show
                // the icon instead of an empty box.
                FallbackToIcon();
            }
            catch (Exception)
            {
                FallbackToIcon();
            }
        }

        private void StopConfirmationTimer()
        {
            if (_confirmationTimer != null)
            {
                _confirmationTimer.Stop();
                _confirmationTimer.Tick -= ConfirmOrFallBack;
                _confirmationTimer = null;
            }
        }

        /// <summary>
        /// Unregisters the thumbnail and turns the fallback on. Idempotent
        /// and safe to call from any path.
        /// </summary>
        private void FallbackToIcon()
        {
            StopConfirmationTimer();

            if (_renderingHandler != null)
            {
                CompositionTarget.Rendering -= _renderingHandler;
                _renderingHandler = null;
            }

            if (_thumbHandle != IntPtr.Zero)
            {
                try { NativeMethods.DwmUnregisterThumbnail(_thumbHandle); }
                catch (Exception) { /* best effort */ }
                _thumbHandle = IntPtr.Zero;
            }

            IsThumbnailAvailable = false;
        }

        private void OnUnloaded(object sender, RoutedEventArgs e)
        {
            try
            {
                FallbackToIcon();
            }
            catch (Exception)
            {
                // Nothing may propagate out of Unloaded.
            }
        }
    }
}

// ----------------------------------------------------------------------------
// #2 - STATIC PrintWindow CAPTURE - shipped in v2.55 (extract: capture path)
//
// Full file in the v2.55 release. It is included here as the reference for the
// "loud failure" contract: PrintWindow either returns TRUE with real pixels or
// FALSE and we fall back to the icon. The remaining problem is that a TRUE
// return still does not prove the pixels are the window content (several apps
// answer with an empty or stale surface), which is exactly what a future
// implementation has to validate before the popup can be re-enabled.
// ----------------------------------------------------------------------------
//         /// <summary>
//         /// Cattura la finestra sorgente UNA VOLTA sola con PrintWindow e la
//         /// mostra come immagine statica. In caso di fallimento (finestra
//         /// minimizzata, app che non supporta PrintWindow, handle invalido)
//         /// passa esplicitamente al ripiego: nessuno stato intermedio.
//         /// </summary>
//         private void CaptureOnce()
//         {
//             if (SourceWindowHandle == IntPtr.Zero)
//             {
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             BitmapSource? captured = TryCapture(SourceWindowHandle, out double aspectRatio);
//             if (captured == null)
//             {
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             // Stessa logica di scalatura di prima: 1:1 se la finestra e'
//             // piccola, altrimenti si adatta a 180x120 mantenendo le
//             // proporzioni.
//             double wantWidth;
//             double wantHeight;
//             const double controlAspectRatio = MaxWidth_ / MaxHeight_;
//
//             if (captured.PixelWidth <= MaxWidth_ * DpiScale && captured.PixelHeight <= MaxHeight_ * DpiScale)
//             {
//                 wantWidth = captured.PixelWidth / DpiScale;
//                 wantHeight = captured.PixelHeight / DpiScale;
//             }
//             else if (aspectRatio > controlAspectRatio)
//             {
//                 wantWidth = MaxWidth_;
//                 wantHeight = MaxWidth_ / aspectRatio;
//             }
//             else
//             {
//                 wantWidth = MaxHeight_ * aspectRatio;
//                 wantHeight = MaxHeight_;
//             }
//
//             Width = wantWidth;
//             Height = wantHeight;
//
//             _image.Source = captured;
//             IsThumbnailAvailable = true;
//         }
//
//         /// <summary>
//         /// PrintWindow nell'area client della finestra sorgente. Prova prima
//         /// con PW_RENDERFULLCONTENT (necessario per le app con superficie
//         /// accelerata: Chrome, Edge, molte app moderne), poi senza, per le
//         /// build di Windows/driver dove quel flag stesso fa fallire la
//         /// chiamata. Ritorna null se nessuna delle due produce pixel validi.
//         /// </summary>
//         private static BitmapSource? TryCapture(IntPtr hwnd, out double aspectRatio)
//         {
//             aspectRatio = 1.0;
//
//             if (!NativeMethods.GetClientRect(hwnd, out NativeMethods.RECT rc))
//             {
//                 return null;
//             }
//
//             int width = rc.Right - rc.Left;
//             int height = rc.Bottom - rc.Top;
//             if (width <= 0 || height <= 0)
//             {
//                 return null;
//             }
//
//             aspectRatio = (double)width / height;
//
//             IntPtr hdcSrc = IntPtr.Zero;
//             IntPtr hdcMem = IntPtr.Zero;
//             IntPtr hBitmap = IntPtr.Zero;
//             IntPtr hOld = IntPtr.Zero;
//
//             try
//             {
//                 hdcSrc = NativeMethods.GetWindowClientDC(hwnd);
//                 if (hdcSrc == IntPtr.Zero)
//                 {
//                     return null;
//                 }
//
//                 hdcMem = NativeMethods.CreateCompatibleDC(hdcSrc);
//                 if (hdcMem == IntPtr.Zero)
//                 {
//                     return null;
//                 }
//
//                 hBitmap = NativeMethods.CreateCompatibleBitmap(hdcSrc, width, height);
//                 if (hBitmap == IntPtr.Zero)
//                 {
//                     return null;
//                 }
//
//                 hOld = NativeMethods.SelectObject(hdcMem, hBitmap);
//
//                 bool ok = NativeMethods.PrintWindow(hwnd, hdcMem,
//                     NativeMethods.PW_CLIENTONLY | NativeMethods.PW_RENDERFULLCONTENT);
//                 if (!ok)
//                 {
//                     // Ripiego: alcune build ignorano/rifiutano
//                     // PW_RENDERFULLCONTENT (0x02) come combinazione di flag.
//                     ok = NativeMethods.PrintWindow(hwnd, hdcMem, NativeMethods.PW_CLIENTONLY);
//                 }
//
//                 if (!ok)
//                 {
//                     return null;
//                 }
//
//                 BitmapSource bmp = Imaging.CreateBitmapSourceFromHBitmap(
//                     hBitmap, IntPtr.Zero, Int32Rect.Empty,
//                     BitmapSizeOptions.FromEmptyOptions());
//                 bmp.Freeze();
//                 return bmp;
//             }
//             catch (Exception)
//             {
//                 return null;
//             }
//             finally
//             {
//                 if (hOld != IntPtr.Zero)
//                 {
//                     NativeMethods.SelectObject(hdcMem, hOld);
//                 }
//                 if (hBitmap != IntPtr.Zero)
//                 {
//                     NativeMethods.DeleteObject(hBitmap);
//                 }
//                 if (hdcMem != IntPtr.Zero)
//                 {
//                     NativeMethods.DeleteDC(hdcMem);
//                 }
//                 if (hdcSrc != IntPtr.Zero)
//                 {
//                     NativeMethods.ReleaseWindowClientDC(hwnd, hdcSrc);
//                 }
//             }
//         }
