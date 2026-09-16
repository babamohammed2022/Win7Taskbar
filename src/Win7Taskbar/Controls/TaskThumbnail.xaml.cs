using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Windows 7-style live window preview using the native DWM thumbnail API.
    /// The preview UI, close button and window navigation are handled by the
    /// parent TaskbarWindow.
    /// </summary>
    public partial class TaskThumbnail : UserControl
    {
        private const double MaxWidth = 180;
        private const double MaxHeight = 120;

        private IntPtr _thumbnailHandle;
        private EventHandler? _renderingHandler;
        private readonly DispatcherTimer _toolTipTimer;

        public double DpiScale { get; private set; } = 1.0;

        public TaskThumbnail()
        {
            InitializeComponent();

            _toolTipTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(
                    ToolTipService.GetInitialShowDelay(this))
            };

            _toolTipTimer.Tick += ToolTipTimer_Tick;
        }

        public static readonly DependencyProperty SourceWindowHandleProperty =
            DependencyProperty.Register(
                nameof(SourceWindowHandle),
                typeof(IntPtr),
                typeof(TaskThumbnail),
                new PropertyMetadata(IntPtr.Zero));

        public IntPtr SourceWindowHandle
        {
            get => (IntPtr)GetValue(SourceWindowHandleProperty);
            set => SetValue(SourceWindowHandleProperty, value);
        }

        public static readonly DependencyProperty TitleProperty =
            DependencyProperty.Register(
                nameof(Title),
                typeof(string),
                typeof(TaskThumbnail),
                new PropertyMetadata(string.Empty));

        public string Title
        {
            get => (string)GetValue(TitleProperty);
            set => SetValue(TitleProperty, value);
        }

        private IntPtr HostHandle
        {
            get
            {
                try
                {
                    if (PresentationSource.FromVisual(this) is HwndSource source)
                        return source.Handle;
                }
                catch
                {
                    // The control may be unloading.
                }

                return IntPtr.Zero;
            }
        }

        /// <summary>
        /// Destination rectangle for the DWM thumbnail.
        /// Coordinates are relative to the WPF host window.
        /// </summary>
        private NativeMethods.RECT DestinationRect
        {
            get
            {
                try
                {
                    if (PresentationSource.FromVisual(this)?.RootVisual is not Visual root)
                        return default;

                    double width = ActualWidth > 0 ? ActualWidth : Width;
                    double height = ActualHeight > 0 ? ActualHeight : Height;

                    if (width <= 0 || height <= 0)
                        return default;

                    GeneralTransform transform = TransformToAncestor(root);

                    Point topLeft = transform.Transform(new Point(0, 0));
                    Point bottomRight = transform.Transform(
                        new Point(width, height));

                    int left = (int)Math.Round(topLeft.X * DpiScale);
                    int top = (int)Math.Round(topLeft.Y * DpiScale);
                    int right = (int)Math.Round(bottomRight.X * DpiScale);
                    int bottom = (int)Math.Round(bottomRight.Y * DpiScale);

                    return new NativeMethods.RECT
                    {
                        Left = left,
                        Top = top,
                        Right = right,
                        Bottom = bottom
                    };
                }
                catch
                {
                    return default;
                }
            }
        }

        public void Refresh()
        {
            try
            {
                if (_thumbnailHandle == IntPtr.Zero)
                    return;

                // Display only the client area of the source window.
                var clientAreaProperties =
                    new NativeMethods.DWM_THUMBNAIL_PROPERTIES
                    {
                        dwFlags = NativeMethods.DWM_TNP_SOURCECLIENTAREAONLY,
                        fSourceClientAreaOnly = true
                    };

                NativeMethods.DwmUpdateThumbnailProperties(
                    _thumbnailHandle,
                    ref clientAreaProperties);

                // Ask DWM for the real source size.
                if (NativeMethods.DwmQueryThumbnailSourceSize(
                        _thumbnailHandle,
                        out NativeMethods.SIZE size) != 0)
                {
                    return;
                }

                if (size.cx <= 0 || size.cy <= 0)
                    return;

                double aspectRatio = (double)size.cx / size.cy;
                double controlAspectRatio = MaxWidth / MaxHeight;

                double width;
                double height;

                // Small windows are displayed at 1:1.
                if (size.cx <= MaxWidth * DpiScale &&
                    size.cy <= MaxHeight * DpiScale)
                {
                    width = size.cx / DpiScale;
                    height = size.cy / DpiScale;
                }
                else if (aspectRatio > controlAspectRatio)
                {
                    // Wide window.
                    width = MaxWidth;
                    height = MaxWidth / aspectRatio;
                }
                else
                {
                    // Tall or square window.
                    width = MaxHeight * aspectRatio;
                    height = MaxHeight;
                }

                Width = width;
                Height = height;

                // WPF has to process the new size before the destination
                // rectangle is calculated.
                UpdateLayout();

                NativeMethods.RECT destination = DestinationRect;

                if (destination.Right <= destination.Left ||
                    destination.Bottom <= destination.Top)
                {
                    return;
                }

                var properties =
                    new NativeMethods.DWM_THUMBNAIL_PROPERTIES
                    {
                        fVisible = true,
                        dwFlags =
                            NativeMethods.DWM_TNP_VISIBLE |
                            NativeMethods.DWM_TNP_RECTDESTINATION,
                        rcDestination = destination
                    };

                NativeMethods.DwmUpdateThumbnailProperties(
                    _thumbnailHandle,
                    ref properties);
            }
            catch
            {
                // A preview failure must never break the taskbar.
            }
        }

        private void UserControl_Loaded(object sender, RoutedEventArgs e)
        {
            try
            {
                if (PresentationSource.FromVisual(this)?.CompositionTarget
                    is CompositionTarget target)
                {
                    DpiScale = target.TransformToDevice.M11;
                }

                if (!NativeMethods.IsCompositionEnabled())
                    return;

                IntPtr hostHandle = HostHandle;

                if (hostHandle == IntPtr.Zero ||
                    SourceWindowHandle == IntPtr.Zero)
                {
                    return;
                }

                if (NativeMethods.DwmRegisterThumbnail(
                        hostHandle,
                        SourceWindowHandle,
                        out _thumbnailHandle) != 0)
                {
                    _thumbnailHandle = IntPtr.Zero;
                    return;
                }

                Refresh();

                // The popup can move while it is open. DWM does not follow
                // WPF layout automatically, so update the destination
                // rectangle during rendering.
                _renderingHandler = (senderObject, args) =>
                {
                    try
                    {
                        Dispatcher.BeginInvoke(
                            DispatcherPriority.Render,
                            new Action(Refresh));
                    }
                    catch
                    {
                        // Ignore dispatcher failures during shutdown.
                    }
                };

                CompositionTarget.Rendering += _renderingHandler;

                _toolTipTimer.Start();
            }
            catch
            {
                CleanupThumbnail();
            }
        }

        private void UserControl_Unloaded(object sender, RoutedEventArgs e)
        {
            try
            {
                CleanupThumbnail();

                _toolTipTimer.Stop();

                if (ToolTip is ToolTip tip)
                    tip.IsOpen = false;
            }
            catch
            {
                // Nothing should escape from Unloaded.
            }
        }

        private void CleanupThumbnail()
        {
            try
            {
                if (_renderingHandler != null)
                {
                    CompositionTarget.Rendering -= _renderingHandler;
                    _renderingHandler = null;
                }

                if (_thumbnailHandle != IntPtr.Zero)
                {
                    NativeMethods.DwmUnregisterThumbnail(
                        _thumbnailHandle);

                    _thumbnailHandle = IntPtr.Zero;
                }
            }
            catch
            {
                // Best-effort cleanup.
                _thumbnailHandle = IntPtr.Zero;
                _renderingHandler = null;
            }
        }

        private void ToolTipTimer_Tick(object? sender, EventArgs e)
        {
            try
            {
                _toolTipTimer.Stop();

                if (ToolTip is ToolTip tip)
                {
                    tip.PlacementTarget = this;
                    tip.IsOpen = true;
                }
            }
            catch
            {
                // Ignore tooltip failures.
            }
        }
    }
}
