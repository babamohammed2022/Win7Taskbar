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


        private readonly DispatcherTimer _toolTipTimer;
        private readonly DispatcherTimer _verificationTimer;
        private EventHandler? _renderingHandler;
        private IntPtr _thumbHandle;
        private int _sourceRegistrationGeneration;

        /* v1.21.8: ultimo rettangolo consegnato a DWM, per non ripetere la
         * stessa chiamata a ogni fotogramma. */
        private bool _hasDwmUpdate;
        private NativeMethods.RECT _lastDestination;
        private NativeMethods.RECT _lastSource;

        /* v1.21.8: Refresh() non tocca piu' Width/Height, quindi agganciare
         * il ricalcolo al layout e' sicuro (nessuna ricorsione) e serve: ogni
         * spostamento o ridimensionamento del popup cambia il rettangolo di
         * destinazione. L'aggancio avviene una volta sola per istanza. */
        private bool _layoutRefreshHooked;

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
                new PropertyMetadata(IntPtr.Zero, OnSourceWindowHandleChanged));

        public IntPtr SourceWindowHandle
        {
            get => (IntPtr)GetValue(SourceWindowHandleProperty);
            set => SetValue(SourceWindowHandleProperty, value);
        }

        /// <summary>
        /// Segnale interno al contenitore: quando la relazione DWM viene
        /// registrata di nuovo o cambia il rettangolo corrente, il bordo puo'
        /// essere ridisegnato usando le dimensioni attuali del frame.
        /// </summary>
        public event EventHandler? DwmGeometryChanged;

        private static void OnSourceWindowHandleChanged(
            DependencyObject dependencyObject, DependencyPropertyChangedEventArgs e)
        {
            if (dependencyObject is TaskThumbnail thumbnail)
            {
                thumbnail.RestartDwmThumbnailForNewSource();
            }
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

        /// <summary>
        /// v1.21.18: destination rectangle of the live thumbnail, in the CLIENT
        /// coordinates of the window that hosts this control.
        ///
        /// DWM_THUMBNAIL_PROPERTIES.rcDestination is documented as "the area in
        /// the destination window where the thumbnail will be rendered", i.e.
        /// client coordinates in device pixels, and DWM stretches rcSource into
        /// it. Two details therefore matter and both are handled here:
        ///
        ///   * the rectangle must come from the CURRENT layout, the CURRENT
        ///     monitor and the CURRENT transform chain. Both corners are taken
        ///     with PointToScreen - device pixels, every transform applied -
        ///     and the popup's own client origin is subtracted. The old code
        ///     multiplied a DIP rectangle by VisualTreeHelper.GetDpi, which
        ///     stops being the painted rectangle as soon as the element's
        ///     physical size is not exactly its DIP size times the monitor
        ///     scale (fractional sizes at 125%/150%, and the popup's own
        ///     normalisation that keeps the previews at their 100%-DPI pixel
        ///     geometry);
        ///   * each edge is rounded on its own to the NEAREST device pixel -
        ///     the same rule the XAML frame's edge snapping follows - so the
        ///     rectangle lands exactly on the painted aperture instead of
        ///     spilling ~1 px over its inner edge at fractional scales (a
        ///     125% field report); exact halves still round outward, keeping
        ///     the v1.1.1 guarantee that no transparent strip is ever left
        ///     inside the border (see the rounding comment below).
        ///
        /// No value is guessed: when the layout is not ready yet the caller
        /// skips the update instead of painting the thumbnail at a wrong place.
        /// </summary>
        private bool TryGetDestinationRect(out NativeMethods.RECT rect)
        {
            rect = new NativeMethods.RECT();
            try
            {
                // Destination coordinates belong to the shared popup HWND,
                // not to each ContentPresenter (which starts at 0,0).
                if (Handle == IntPtr.Zero ||
                    PresentationSource.FromVisual(this) is not HwndSource source ||
                    source.RootVisual is not Visual root ||
                    ActualWidth < 1 || ActualHeight < 1)
                {
                    return false;
                }

                /* v1.21.18: PHYSICAL pixels come from PointToScreen alone.
                 * PointToScreen maps through the whole transform chain -
                 * layout transforms, the DPI of the monitor the popup is on,
                 * layout rounding - and returns device pixels, which is what
                 * DWM_THUMBNAIL_PROPERTIES.rcDestination uses. The previous
                 * version multiplied DIP coordinates by VisualTreeHelper
                 * .GetDpi, which is only correct while the element's own DIP
                 * size times the monitor scale is the size actually painted:
                 * at 125% the fixed 236x166 preview geometry lands on half
                 * pixels (207.5 px tall), so the raw multiplication and the
                 * painted aperture could disagree by up to one pixel and the
                 * live surface sat a fraction of a pixel off its own frame.
                 * Taking both corners in screen space and subtracting the
                 * popup's own client origin removes the guesswork: the two
                 * points share the same transform chain, so the difference is
                 * exactly where the aperture was drawn inside the popup. */
                Point topLeft = PointToScreen(new Point(0, 0));
                Point bottomRight = PointToScreen(new Point(ActualWidth, ActualHeight));
                Point clientOrigin = root.PointToScreen(new Point(0, 0));

                /* Nearest-pixel rounding, exact halves outward (refined after
                 * a 125% field report: the v1.1.1 all-outward rule cured the
                 * empty strip but let the live image spill ~1 px over the
                 * frame's inner edge wherever the edges landed on fractions).
                 * The frame's 17/38/19-DIP border and this rectangle are the
                 * same geometry, but DWM wants whole pixels while the popup is
                 * normalised to the monitor scale (18.7 px borders at 125% +
                 * the 10% enlargement), so an edge can fall on a half pixel.
                 * Plain rounding to nearest could then leave a sub-pixel strip
                 * of popup with nothing painted in it on exact halves, and a
                 * transparent strip in a layered window shows the desktop
                 * through it: that is the "empty edge" this rounding exists to
                 * prevent, which is why halves keep going outward (Left/Top
                 * down, Right/Bottom up). The thumbnail is composed above the
                 * window's own content (Microsoft: the thumbnail is rendered
                 * into the destination window), so the at most one extra pixel
                 * on a half can only cover the innermost, fading row of the
                 * border, never the frame itself. */
                rect = new NativeMethods.RECT
                {
                    Left = (int)Math.Ceiling(topLeft.X - clientOrigin.X - 0.5),
                    Top = (int)Math.Ceiling(topLeft.Y - clientOrigin.Y - 0.5),
                    Right = (int)Math.Floor(bottomRight.X - clientOrigin.X + 0.5),
                    Bottom = (int)Math.Floor(bottomRight.Y - clientOrigin.Y + 0.5)
                };
                return rect.Right > rect.Left && rect.Bottom > rect.Top;
            }
            catch
            {
                return false;
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

                /* v1.21.8: la destinazione si legge PRIMA della sorgente: il
                 * ritaglio della sorgente deve avere il rapporto d'aspetto
                 * della destinazione, altrimenti DWM stira l'immagine e il
                 * contenuto non combacia con l'apertura della cornice. Se il
                 * layout non e' pronto si esce senza disegnare nulla: il
                 * prossimo giro (SizeChanged/LayoutUpdated) sistema tutto. */ 
                if (!TryGetDestinationRect(out NativeMethods.RECT destination))
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
                    size.cx <= 0 || size.cy <= 0)
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
                // della cornice. v1.21.8: il rapporto d'aspetto non e' piu' la
                // costante 202/109 ma quello del rettangolo di destinazione
                // appena calcolato: e' esattamente il rapporto con cui DWM
                // stira la sorgente, quindi cosi' l'immagine riempie l'apertura
                // senza deformazioni e senza sbordare sui bordi.
                int srcW = sourceRect.Right - sourceRect.Left;
                int srcH = sourceRect.Bottom - sourceRect.Top;
                if (srcW > 0 && srcH > 0)
                {
                    int dstW = destination.Right - destination.Left;
                    int dstH = destination.Bottom - destination.Top;
                    double apertureAspect = dstH > 0
                        ? (double)dstW / dstH
                        : RetroWidth / RetroHeight;
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

                /* v1.21.8: niente piu' Width/Height impostati a ogni giro.
                 * Il controllo e' gia' 202x109 dal template e la cornice lo
                 * stira sull'apertura: riscrivere la dimensione da qui faceva
                 * ripartire il layout a ogni fotogramma (e con esso il
                 * rettangolo di destinazione appena letto), che e' una delle
                 * cause della superficie DWM fuori posto. */

                /* Nessuna chiamata quando nulla e' cambiato: il thumbnail e'
                 * vivo di suo, qui si aggiorna solo il ritaglio. */
                if (_hasDwmUpdate &&
                    destination.Left == _lastDestination.Left &&
                    destination.Top == _lastDestination.Top &&
                    destination.Right == _lastDestination.Right &&
                    destination.Bottom == _lastDestination.Bottom &&
                    sourceRect.Left == _lastSource.Left &&
                    sourceRect.Top == _lastSource.Top &&
                    sourceRect.Right == _lastSource.Right &&
                    sourceRect.Bottom == _lastSource.Bottom)
                {
                    return;
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
                    return;
                }

                bool geometryChanged = !_hasDwmUpdate ||
                    destination.Left != _lastDestination.Left ||
                    destination.Top != _lastDestination.Top ||
                    destination.Right != _lastDestination.Right ||
                    destination.Bottom != _lastDestination.Bottom ||
                    sourceRect.Left != _lastSource.Left ||
                    sourceRect.Top != _lastSource.Top ||
                    sourceRect.Right != _lastSource.Right ||
                    sourceRect.Bottom != _lastSource.Bottom;
                _hasDwmUpdate = true;
                _lastDestination = destination;
                _lastSource = sourceRect;

                if (geometryChanged)
                {
                    try
                    {
                        /* Il contenitore puo' essere stato riciclato o il
                         * popup puo' essersi spostato senza produrre un nuovo
                         * SizeChanged sul frame: il genitore ridisegna ora il
                         * bordo sulla geometria corrente. */
                        DwmGeometryChanged?.Invoke(this, EventArgs.Empty);
                    }
                    catch (Exception ex)
                    {
                        System.Diagnostics.Debug.WriteLine(
                            $"TaskThumbnail geometry event: {ex.Message}");
                    }
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

        private void RestartDwmThumbnailForNewSource()
        {
            try
            {
                int generation = unchecked(++_sourceRegistrationGeneration);
                if (!IsLoaded)
                {
                    return;
                }

                /* DwmRegisterThumbnail lega l'handle sorgente alla relazione
                 * corrente: quando il binding passa a un'altra finestra non
                 * basta aggiornare rcSource, bisogna deregistrare la relazione
                 * precedente e crearne una nuova con l'HWND top-level nuovo. */
                StopDwmThumbnail();
                ShowIdentityFallback();
                if (SourceWindowHandle == IntPtr.Zero)
                {
                    return;
                }

                Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
                {
                    try
                    {
                        if (!IsLoaded || generation != _sourceRegistrationGeneration ||
                            SourceWindowHandle == IntPtr.Zero)
                        {
                            return;
                        }
                        RegisterDwmThumbnail();
                    }
                    catch (Exception ex)
                    {
                        System.Diagnostics.Debug.WriteLine(
                            $"TaskThumbnail source change: {ex.Message}");
                        StopDwmThumbnail();
                        ShowIdentityFallback();
                    }
                }));
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail source change: {ex.Message}");
                StopDwmThumbnail();
                ShowIdentityFallback();
            }
        }

        private void RegisterDwmThumbnail()
        {
            try
            {
                if (!IsLoaded || _thumbHandle != IntPtr.Zero)
                {
                    return;
                }

                if (!NativeMethods.IsCompositionEnabled() ||
                    SourceWindowHandle == IntPtr.Zero || Handle == IntPtr.Zero)
                {
                    ShowValidatedFallbackOrIdentity();
                    return;
                }

                int hr = NativeMethods.DwmRegisterThumbnail(
                    Handle, SourceWindowHandle, out _thumbHandle);
                if (hr < 0 || _thumbHandle == IntPtr.Zero)
                {
                    StopDwmThumbnail();
                    ShowValidatedFallbackOrIdentity();
                    return;
                }

                if (!_layoutRefreshHooked)
                {
                    _layoutRefreshHooked = true;
                    SizeChanged += OnLayoutRefresh;
                    LayoutUpdated += OnLayoutRefresh;
                }

                CaptureFallbackImage.Source = null;
                CaptureFallbackImage.Visibility = Visibility.Collapsed;
                IdentityFallback.Visibility = Visibility.Collapsed;

                Refresh();
                if (_thumbHandle == IntPtr.Zero)
                {
                    return;
                }

                _renderingHandler = (s, a) =>
                    Dispatcher.BeginInvoke(DispatcherPriority.Render,
                        new Action(Refresh));
                CompositionTarget.Rendering += _renderingHandler;

                /* Registration success does not prove composition. Probe once
                 * after DWM has had several frames to draw. */
                _verificationTimer.Stop();
                _verificationTimer.Start();
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"TaskThumbnail register: {ex.Message}");
                StopDwmThumbnail();
                ShowIdentityFallback();
            }
        }

        private void UserControl_Loaded(object sender, RoutedEventArgs e)
        {
            try
            {
                /* v1.21.8: il fattore di scala non si memorizza piu': si
                 * legge a ogni aggiornamento con VisualTreeHelper.GetDpi
                 * (segue il monitor su cui si apre il popup). */
                RegisterDwmThumbnail();
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
            /* v1.21.8: same measured rectangle DWM was given (see
             * TryGetDestinationRect); when the layout is not ready yet there
             * is nothing to probe. */
            if (!TryGetDestinationRect(out NativeMethods.RECT destination))
            {
                return false;
            }

            int width = destination.Right - destination.Left;
            int height = destination.Bottom - destination.Top;
            if (width < 32 || height < 32)
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
            if (pixelWidth <= 0 || pixelHeight <= 0)
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

        private void OnLayoutRefresh(object? sender, EventArgs e)
        {
            Refresh();
        }

        private void StopDwmThumbnail()
        {
            _verificationTimer.Stop();
            if (_layoutRefreshHooked)
            {
                _layoutRefreshHooked = false;
                SizeChanged -= OnLayoutRefresh;
                LayoutUpdated -= OnLayoutRefresh;
            }
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
            _hasDwmUpdate = false;
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
