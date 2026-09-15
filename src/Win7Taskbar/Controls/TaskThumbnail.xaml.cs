using RetroBar.Utilities;
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32.SafeHandles;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// RetroBar-style live DWM thumbnail with a delayed, validated screen
    /// capture fallback. The surrounding frame, close X and navigation remain
    /// owned by TaskbarWindow.xaml.
    /// </summary>
    public partial class TaskThumbnail : UserControl
    {
        private const double RetroWidth = 202;
        private const double RetroHeight = 109;

        // A 65% floor is intentionally aesthetic rather than pixel-faithful:
        // tiny source windows otherwise make the fixed 17/19px frame dominate.
        private const double MinScaledWidth = RetroWidth * 0.65;
        private const double MinScaledHeight = RetroHeight * 0.65;
        private const double ExtremeAspectRatioThreshold = 2.5;
        private const double ExtremeLongEdgeCrop = 0.15;
        private static readonly TimeSpan VerificationDelay = TimeSpan.FromMilliseconds(350);
        private const uint GA_ROOT = 2;

        public double DpiScale = 1.0;

        private readonly DispatcherTimer _toolTipTimer;
        private readonly DispatcherTimer _verificationTimer;
        private EventHandler? _renderingHandler;
        private IntPtr _thumbHandle;

        public TaskThumbnail()
        {
            InitializeComponent();

            _toolTipTimer = new DispatcherTimer
            {
                Interval = new TimeSpan(
                    0, 0, 0, 0, ToolTipService.GetInitialShowDelay(this))
            };
            _toolTipTimer.Tick += ToolTipTimer_Tick;

            _verificationTimer = new DispatcherTimer
            {
                Interval = VerificationDelay
            };
            _verificationTimer.Tick += VerificationTimer_Tick;
        }

        public IntPtr Handle
        {
            get
            {
                try
                {
                    return PresentationSource.FromVisual(this) is HwndSource source
                        ? source.Handle : IntPtr.Zero;
                }
                catch
                {
                    return IntPtr.Zero;
                }
            }
        }

        public static readonly DependencyProperty SourceWindowHandleProperty =
            DependencyProperty.Register(nameof(SourceWindowHandle),
                typeof(IntPtr), typeof(TaskThumbnail),
                new PropertyMetadata(IntPtr.Zero));

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

        public static readonly DependencyProperty ApplicationNameProperty =
            DependencyProperty.Register(nameof(ApplicationName), typeof(string),
                typeof(TaskThumbnail), new PropertyMetadata(string.Empty));

        /// <summary>Friendly executable name for the tooltip. Title remains
        /// the real per-window caption used by the visible preview band.</summary>
        public string ApplicationName
        {
            get => (string)GetValue(ApplicationNameProperty);
            set => SetValue(ApplicationNameProperty, value);
        }

        private NativeMethods.RECT Rect
        {
            get
            {
                try
                {
                    // Destination coordinates belong to the shared popup HWND,
                    // not to each ContentPresenter (which starts at 0,0).
                    if (PresentationSource.FromVisual(this) is not HwndSource source ||
                        source.RootVisual is not Visual root)
                    {
                        return new NativeMethods.RECT();
                    }

                    Point origin = TransformToAncestor(root).Transform(new Point(0, 0));
                    return new NativeMethods.RECT
                    {
                        Left = (int)Math.Round(origin.X * DpiScale),
                        Top = (int)Math.Round(origin.Y * DpiScale),
                        Right = (int)Math.Round((origin.X + ActualWidth) * DpiScale),
                        Bottom = (int)Math.Round((origin.Y + ActualHeight) * DpiScale)
                    };
                }
                catch
                {
                    return new NativeMethods.RECT();
                }
            }
        }

        private static void ApplyExtremeAspectCrop(ref NativeMethods.RECT rect)
        {
            int width = rect.Right - rect.Left;
            int height = rect.Bottom - rect.Top;
            if (width <= 0 || height <= 0)
            {
                return;
            }

            double ratio = (double)width / height;
            if (ratio > ExtremeAspectRatioThreshold)
            {
                // A bounded 15% centre crop makes an exceptionally wide
                // preview more substantial without non-uniform stretching.
                // The destination remains wholly inside the DWM aperture.
                int crop = Math.Max(0,
                    (int)Math.Round(width * ExtremeLongEdgeCrop / 2.0));
                rect.Left += crop;
                rect.Right -= crop;
            }
            else if (ratio < 1.0 / ExtremeAspectRatioThreshold)
            {
                // Same compromise for a tall/narrow source. Cropping source
                // pixels is preferable to letting an oversized destination
                // paint over the fixed Aero frame or close button.
                int crop = Math.Max(0,
                    (int)Math.Round(height * ExtremeLongEdgeCrop / 2.0));
                rect.Top += crop;
                rect.Bottom -= crop;
            }
        }

        public void Refresh()
        {
            try
            {
                if (_thumbHandle == IntPtr.Zero)
                {
                    return;
                }

                var clientOnly = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
                {
                    dwFlags = NativeMethods.DWM_TNP_SOURCECLIENTAREAONLY,
                    fSourceClientAreaOnly = true
                };
                if (NativeMethods.DwmUpdateThumbnailProperties(
                        _thumbHandle, ref clientOnly) < 0 ||
                    NativeMethods.DwmQueryThumbnailSourceSize(
                        _thumbHandle, out NativeMethods.SIZE size) < 0 ||
                    size.cx <= 0 || size.cy <= 0 || DpiScale <= 0)
                {
                    // v3.9: fallimento DWM = finestra sorgente non esiste piu'
                    // Evita riquadro vuoto persistente: mostra fallback identita'
                    StopDwmThumbnail();
                    ShowIdentityFallback();
                    return;
                }

                var sourceRect = new NativeMethods.RECT
                {
                    Left = 0,
                    Top = 0,
                    Right = size.cx,
                    Bottom = size.cy
                };
                ApplyExtremeAspectCrop(ref sourceRect);

                // v3.9: fix letterboxing - la preview riempie sempre l'apertura
                // fissa 202x109. Invece di ridimensionare il riquadro esterno in
                // base all'aspect ratio, si adatta la sorgente con crop centrale
                // per coprire (cover) l'apertura, preservando l'aspect senza
                // bande nere. Mantiene ApplyExtremeAspectCrop e adatta il floor
                // minimo al comportamento "sempre pieno".
                int srcW = sourceRect.Right - sourceRect.Left;
                int srcH = sourceRect.Bottom - sourceRect.Top;
                if (srcW > 0 && srcH > 0)
                {
                    double apertureAspect = RetroWidth / RetroHeight;
                    double srcAspect = (double)srcW / srcH;
                    if (srcAspect > apertureAspect)
                    {
                        // Sorgente piu' larga: crop orizzontale centrale
                        int desiredW = (int)Math.Round(srcH * apertureAspect);
                        desiredW = Math.Max(1, Math.Min(desiredW, srcW));
                        int crop = (srcW - desiredW) / 2;
                        sourceRect.Left += crop;
                        sourceRect.Right = sourceRect.Left + desiredW;
                    }
                    else if (srcAspect < apertureAspect)
                    {
                        // Sorgente piu' alta: crop verticale centrale
                        int desiredH = (int)Math.Round(srcW / apertureAspect);
                        desiredH = Math.Max(1, Math.Min(desiredH, srcH));
                        int crop = (srcH - desiredH) / 2;
                        sourceRect.Top += crop;
                        sourceRect.Bottom = sourceRect.Top + desiredH;
                    }
                }

                // Il controllo riempie sempre l'area "*" del template (Stretch),
                // dimensione guidata dal layout (202x109 fissi). Non si imposta
                // piu' Width/Height variabili calcolati qui per evitare
                // letterboxing esterno; la superficie DWM riempie sempre
                // l'apertura fissa.
                Width = RetroWidth;
                Height = RetroHeight;

                NativeMethods.RECT destination = Rect;
                // Se ActualWidth/Height non ancora misurati, usa apertura fissa
                if (destination.Right - destination.Left < 1 ||
                    destination.Bottom - destination.Top < 1)
                {
                    destination.Right = destination.Left +
                        Math.Max(1, (int)Math.Round(RetroWidth * DpiScale));
                    destination.Bottom = destination.Top +
                        Math.Max(1, (int)Math.Round(RetroHeight * DpiScale));
                }

                var props = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
                {
                    fVisible = true,
                    dwFlags = NativeMethods.DWM_TNP_VISIBLE |
                              NativeMethods.DWM_TNP_RECTDESTINATION |
                              NativeMethods.DWM_TNP_RECTSOURCE,
                    rcDestination = destination,
                    rcSource = sourceRect
                };
                if (NativeMethods.DwmUpdateThumbnailProperties(
                        _thumbHandle, ref props) < 0)
                {
                    // v3.9: se anche l'update finale fallisce, tratta come
                    // finestra chiusa: evita riquadro vuoto persistente
                    StopDwmThumbnail();
                    ShowIdentityFallback();
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail.Refresh: {ex.Message}");
                StopDwmThumbnail();
                ShowIdentityFallback();
            }
        }

        private void UserControl_Loaded(object sender, RoutedEventArgs e)
        {
            try
            {
                DpiScale = PresentationSource.FromVisual(this)?
                    .CompositionTarget?.TransformToDevice.M11 ?? 1.0;

                bool registered = NativeMethods.IsCompositionEnabled() &&
                    SourceWindowHandle != IntPtr.Zero && Handle != IntPtr.Zero &&
                    NativeMethods.DwmRegisterThumbnail(Handle,
                        SourceWindowHandle, out _thumbHandle) == 0;

                if (registered)
                {
                    Refresh();
                    _renderingHandler = (s, a) =>
                        Dispatcher.BeginInvoke(DispatcherPriority.Render,
                            new Action(Refresh));
                    CompositionTarget.Rendering += _renderingHandler;

                    // Registration success doesn't prove composition. Probe
                    // once after DWM has had several frames to draw.
                    _verificationTimer.Stop();
                    _verificationTimer.Start();
                }
                else if (Settings.Instance.UseThumbnailCaptureFallback)
                {
                    ShowValidatedFallbackOrIdentity();
                }
                else
                {
                    ShowIdentityFallback();
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail.Loaded: {ex.Message}");
                StopDwmThumbnail();
                ShowIdentityFallback();
            }

            _toolTipTimer.Start();
        }

        private void VerificationTimer_Tick(object? sender, EventArgs e)
        {
            _verificationTimer.Stop();
            try
            {
                if (_thumbHandle != IntPtr.Zero && DwmDestinationLooksComposed())
                {
                    return;
                }

                if (Settings.Instance.UseThumbnailCaptureFallback)
                {
                    ShowValidatedFallbackOrIdentity();
                }
                else
                {
                    // The switch disables capture, not the guarantee that a
                    // failed preview never remains an anonymous empty box.
                    StopDwmThumbnail();
                    ShowIdentityFallback();
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail verification: {ex.Message}");
                StopDwmThumbnail();
                ShowIdentityFallback();
            }
        }

        private bool DwmDestinationLooksComposed()
        {
            NativeMethods.RECT destination = Rect;
            int width = destination.Right - destination.Left;
            int height = destination.Bottom - destination.Top;
            if (Handle == IntPtr.Zero || width < 32 || height < 32)
            {
                return false;
            }

            var popupOrigin = new NativeMethods.POINT();
            if (!NativeMethods.ClientToScreen(Handle, ref popupOrigin))
            {
                return false;
            }

            var screenRect = new NativeMethods.RECT
            {
                Left = popupOrigin.x + destination.Left,
                Top = popupOrigin.y + destination.Top,
                Right = popupOrigin.x + destination.Right,
                Bottom = popupOrigin.y + destination.Bottom
            };
            BitmapSource? probe = CaptureScreenRect(screenRect, out uint[] pixels);
            return probe != null && CaptureLooksComposed(pixels, width, height) &&
                   !HasBlackBorderArtifact(pixels, width, height);
        }

        private void ShowValidatedFallbackOrIdentity()
        {
            try
            {
                if (!TryGetSourceClientScreenRect(out NativeMethods.RECT sourceRect) ||
                    !IsWindowUnoccludedAt(SourceWindowHandle, sourceRect))
                {
                    StopDwmThumbnail();
                    ShowIdentityFallback();
                    return;
                }

                int width = sourceRect.Right - sourceRect.Left;
                int height = sourceRect.Bottom - sourceRect.Top;
                FitFallbackToAperture(width, height);
                BitmapSource? capture = CaptureScreenRect(sourceRect, out uint[] pixels);

                // All three gates are mandatory. A rejected scrape is never
                // displayed, because a wrong/stale rectangle is less honest
                // than the application's icon and current title.
                if (capture == null ||
                    HasBlackBorderArtifact(pixels, width, height) ||
                    !CaptureLooksComposed(pixels, width, height))
                {
                    StopDwmThumbnail();
                    ShowIdentityFallback();
                    return;
                }

                StopDwmThumbnail();
                IdentityFallback.Visibility = Visibility.Collapsed;
                CaptureFallbackImage.Source = capture;
                CaptureFallbackImage.Visibility = Visibility.Visible;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail fallback: {ex.Message}");
                StopDwmThumbnail();
                ShowIdentityFallback();
            }
        }

        private void ShowIdentityFallback()
        {
            CaptureFallbackImage.Source = null;
            CaptureFallbackImage.Visibility = Visibility.Collapsed;
            IdentityFallback.Visibility = Visibility.Visible;
        }

        private void FitFallbackToAperture(int pixelWidth, int pixelHeight)
        {
            if (pixelWidth <= 0 || pixelHeight <= 0 || DpiScale <= 0)
            {
                return;
            }

            // v3.9: anche il fallback riempie sempre l'apertura fissa
            // 202x109 (comportamento "sempre pieno"). Il floor minimo
            // viene adattato: finestre piccole vengono ingrandite per
            // riempire l'intera area, invece di restare al 65%.
            Width = RetroWidth;
            Height = RetroHeight;
        }

        private bool TryGetSourceClientScreenRect(out NativeMethods.RECT rect)
        {
            rect = new NativeMethods.RECT();
            if (SourceWindowHandle == IntPtr.Zero ||
                !NativeMethods.GetClientRect(SourceWindowHandle, out NativeMethods.RECT client))
            {
                return false;
            }

            var origin = new NativeMethods.POINT { x = client.Left, y = client.Top };
            if (!NativeMethods.ClientToScreen(SourceWindowHandle, ref origin))
            {
                return false;
            }

            int width = client.Right - client.Left;
            int height = client.Bottom - client.Top;
            if (width < 32 || height < 32)
            {
                return false;
            }

            rect = new NativeMethods.RECT
            {
                Left = origin.x,
                Top = origin.y,
                Right = origin.x + width,
                Bottom = origin.y + height
            };
            // Keep fallback framing identical to the DWM source crop.
            ApplyExtremeAspectCrop(ref rect);
            return rect.Right - rect.Left >= 32 && rect.Bottom - rect.Top >= 32;
        }

        private static bool IsWindowUnoccludedAt(
            IntPtr window, NativeMethods.RECT rect)
        {
            IntPtr root = NativeMethods.GetAncestor(window, GA_ROOT);
            if (root == IntPtr.Zero)
            {
                root = window;
            }

            int width = rect.Right - rect.Left;
            int height = rect.Bottom - rect.Top;
            if (root == IntPtr.Zero || width <= 0 || height <= 0)
            {
                return false;
            }

            double[,] points =
            {
                { 0.50, 0.50 }, { 0.25, 0.25 }, { 0.75, 0.25 },
                { 0.25, 0.75 }, { 0.75, 0.75 }
            };
            for (int index = 0; index < points.GetLength(0); index++)
            {
                var point = new NativeMethods.POINT
                {
                    x = rect.Left + (int)(width * points[index, 0]),
                    y = rect.Top + (int)(height * points[index, 1])
                };
                IntPtr hit = NativeMethods.WindowFromPoint(point);
                if (hit == IntPtr.Zero)
                {
                    return false;
                }
                IntPtr hitRoot = NativeMethods.GetAncestor(hit, GA_ROOT);
                if (hitRoot == IntPtr.Zero)
                {
                    hitRoot = hit;
                }
                if (hitRoot != root)
                {
                    return false;
                }
            }
            return true;
        }

        private static bool HasBlackBorderArtifact(uint[] pixels, int width, int height)
        {
            if (pixels.Length < width * height || width <= 4 || height <= 4)
            {
                return false;
            }

            bool IsBlack(int x, int y) =>
                (pixels[y * width + x] & 0x00FFFFFFu) == 0;
            int LongestRun(int length, Func<int, bool> at)
            {
                int longest = 0;
                int run = 0;
                for (int i = 0; i < length; i++)
                {
                    run = at(i) ? run + 1 : 0;
                    longest = Math.Max(longest, run);
                }
                return longest;
            }

            int top = LongestRun(width, x => IsBlack(x, 0) && IsBlack(x, 1));
            int bottom = LongestRun(width,
                x => IsBlack(x, height - 1) && IsBlack(x, height - 2));
            int left = LongestRun(height, y => IsBlack(0, y) && IsBlack(1, y));
            int right = LongestRun(height,
                y => IsBlack(width - 1, y) && IsBlack(width - 2, y));
            return top > width / 2 || bottom > width / 2 ||
                   left > height / 2 || right > height / 2;
        }

        private static bool CaptureLooksComposed(uint[] pixels, int width, int height)
        {
            if (pixels.Length < width * height || width < 32 || height < 32)
            {
                return false;
            }

            int stepX = Math.Max(1, width / 64);
            int stepY = Math.Max(1, height / 64);
            long samples = 0;
            long lit = 0;
            for (int x = 0; x < width; x += stepX)
            {
                samples += 2;
                if ((pixels[x] & 0x00FFFFFFu) != 0) lit++;
                if ((pixels[(height - 1) * width + x] & 0x00FFFFFFu) != 0) lit++;
            }
            for (int y = 0; y < height; y += stepY)
            {
                samples += 2;
                if ((pixels[y * width] & 0x00FFFFFFu) != 0) lit++;
                if ((pixels[y * width + width - 1] & 0x00FFFFFFu) != 0) lit++;
            }
            return samples > 0 && lit * 16 >= samples;
        }

        private static BitmapSource? CaptureScreenRect(
            NativeMethods.RECT rect, out uint[] pixels)
        {
            pixels = Array.Empty<uint>();
            int width = rect.Right - rect.Left;
            int height = rect.Bottom - rect.Top;
            long pixelCount = (long)width * height;
            if (width <= 0 || height <= 0 || pixelCount > 64L * 1024 * 1024)
            {
                // Screen scraping is only a fallback. Refuse pathological
                // virtual-window sizes rather than risking a huge allocation.
                return null;
            }

            using var screen = new SafeScreenDc(NativeMethods.GetWindowClientDC(IntPtr.Zero));
            if (screen.IsInvalid)
            {
                return null;
            }
            using var memory = new SafeMemoryDc(
                NativeMethods.CreateCompatibleDC(screen.DangerousGetHandle()));
            using var bitmap = new SafeGdiBitmap(
                NativeMethods.CreateCompatibleBitmap(
                    screen.DangerousGetHandle(), width, height));
            if (memory.IsInvalid || bitmap.IsInvalid)
            {
                return null;
            }

            using (var selection = new SelectedGdiObject(
                memory.DangerousGetHandle(), bitmap.DangerousGetHandle()))
            {
                if (!NativeMethods.BitBlt(
                        memory.DangerousGetHandle(), 0, 0, width, height,
                        screen.DangerousGetHandle(), rect.Left, rect.Top,
                        NativeMethods.SRCCOPY))
                {
                    return null;
                }
            }

            var source = Imaging.CreateBitmapSourceFromHBitmap(
                bitmap.DangerousGetHandle(), IntPtr.Zero, Int32Rect.Empty,
                BitmapSizeOptions.FromEmptyOptions());
            var converted = new FormatConvertedBitmap(
                source, PixelFormats.Bgr32, null, 0);
            pixels = new uint[checked(width * height)];
            converted.CopyPixels(pixels, checked(width * 4), 0);
            converted.Freeze();
            return converted;
        }

        private void StopDwmThumbnail()
        {
            _verificationTimer.Stop();
            if (_renderingHandler != null)
            {
                CompositionTarget.Rendering -= _renderingHandler;
                _renderingHandler = null;
            }
            if (_thumbHandle != IntPtr.Zero)
            {
                _ = NativeMethods.DwmUnregisterThumbnail(_thumbHandle);
                _thumbHandle = IntPtr.Zero;
            }
        }

        private void UserControl_Unloaded(object sender, RoutedEventArgs e)
        {
            try
            {
                StopDwmThumbnail();
                _toolTipTimer.Stop();
                CaptureFallbackImage.Source = null;
                if (ToolTip is ToolTip tip)
                {
                    tip.IsOpen = false;
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail.Unloaded: {ex.Message}");
            }
        }

        private void ToolTipTimer_Tick(object? sender, EventArgs e)
        {
            if (ToolTip is ToolTip tip)
            {
                tip.PlacementTarget = this;
                tip.IsOpen = true;
            }
        }

        private sealed class SafeScreenDc : SafeHandleZeroOrMinusOneIsInvalid
        {
            internal SafeScreenDc(IntPtr handle) : base(true) => SetHandle(handle);
            protected override bool ReleaseHandle()
                => NativeMethods.ReleaseWindowClientDC(IntPtr.Zero, handle) != 0;
        }

        private sealed class SafeMemoryDc : SafeHandleZeroOrMinusOneIsInvalid
        {
            internal SafeMemoryDc(IntPtr handle) : base(true) => SetHandle(handle);
            protected override bool ReleaseHandle() => NativeMethods.DeleteDC(handle);
        }

        private sealed class SafeGdiBitmap : SafeHandleZeroOrMinusOneIsInvalid
        {
            internal SafeGdiBitmap(IntPtr handle) : base(true) => SetHandle(handle);
            protected override bool ReleaseHandle() => NativeMethods.DeleteObject(handle);
        }

        private sealed class SelectedGdiObject : IDisposable
        {
            private IntPtr _dc;
            private readonly IntPtr _previous;

            internal SelectedGdiObject(IntPtr dc, IntPtr value)
            {
                _dc = dc;
                _previous = NativeMethods.SelectObject(dc, value);
                if (_previous == IntPtr.Zero || _previous == new IntPtr(-1))
                {
                    _dc = IntPtr.Zero;
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
            }

            public void Dispose()
            {
                if (_dc != IntPtr.Zero)
                {
                    _ = NativeMethods.SelectObject(_dc, _previous);
                    _dc = IntPtr.Zero;
                }
            }
        }
    }
}
