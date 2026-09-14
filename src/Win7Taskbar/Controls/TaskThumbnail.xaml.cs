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
    /// Direct DWM thumbnail control. This is RetroBar's TaskThumbnail
    /// technique, adapted only to this project's interop type names. The
    /// surrounding frame, three-state close X, and window navigation stay
    /// in TaskbarWindow.xaml.
    /// </summary>
    public partial class TaskThumbnail : UserControl
    {
        private const double RetroWidth = 180;
        private const double RetroHeight = 120;

        public double DpiScale = 1.0;

        private readonly DispatcherTimer _toolTipTimer;
        private EventHandler? _renderingHandler;
        private IntPtr _thumbHandle;

        public TaskThumbnail()
        {
            InitializeComponent();

            _toolTipTimer = new DispatcherTimer();
            _toolTipTimer.Tick += ToolTipTimer_Tick;
            _toolTipTimer.Interval = new TimeSpan(
                0, 0, 0, 0, ToolTipService.GetInitialShowDelay(this));
        }

        public IntPtr Handle
        {
            get
            {
                HwndSource? source =
                    (HwndSource?)PresentationSource.FromVisual(this);

                if (source == null)
                {
                    return IntPtr.Zero;
                }

                IntPtr handle = source.Handle;
                return handle;
            }
        }

        public static readonly DependencyProperty SourceWindowHandleProperty =
            DependencyProperty.Register(nameof(SourceWindowHandle),
                typeof(IntPtr), typeof(TaskThumbnail),
                new PropertyMetadata(new IntPtr()));

        public IntPtr SourceWindowHandle
        {
            get
            {
                return (IntPtr)GetValue(SourceWindowHandleProperty);
            }
            set
            {
                SetValue(SourceWindowHandleProperty, value);
            }
        }

        public static readonly DependencyProperty TitleProperty =
            DependencyProperty.Register(nameof(Title), typeof(string),
                typeof(TaskThumbnail), new PropertyMetadata(""));

        public string Title
        {
            get
            {
                return (string)GetValue(TitleProperty);
            }
            set
            {
                SetValue(TitleProperty, value);
            }
        }

        public NativeMethods.RECT Rect
        {
            get
            {
                try
                {
                    var generalTransform =
                        TransformToAncestor((Visual)Parent);
                    Point leftTopPoint =
                        generalTransform.Transform(new Point(0, 0));
                    return new NativeMethods.RECT
                    {
                        Left = (int)(leftTopPoint.X * DpiScale),
                        Top = (int)(leftTopPoint.Y * DpiScale),
                        Right = (int)(leftTopPoint.X * DpiScale) +
                                (int)(RetroWidth * DpiScale),
                        Bottom = (int)(leftTopPoint.Y * DpiScale) +
                                 (int)(RetroHeight * DpiScale)
                    };
                }
                catch
                {
                    return new NativeMethods.RECT();
                }
            }
        }

        public void Refresh()
        {
            if (_thumbHandle == IntPtr.Zero)
                return;

            var clientAreaProps =
                new NativeMethods.DWM_THUMBNAIL_PROPERTIES
                {
                    dwFlags = NativeMethods.DWM_TNP_SOURCECLIENTAREAONLY,
                    fSourceClientAreaOnly = true
                };
            NativeMethods.DwmUpdateThumbnailProperties(
                _thumbHandle, ref clientAreaProps);

            NativeMethods.DwmQueryThumbnailSourceSize(
                _thumbHandle, out NativeMethods.SIZE size);
            double aspectRatio = (double)size.cx / size.cy;

            var props = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
            {
                fVisible = true,
                dwFlags = NativeMethods.DWM_TNP_VISIBLE |
                          NativeMethods.DWM_TNP_RECTDESTINATION,
                rcDestination = Rect
            };

            if (size.cx <= RetroWidth * DpiScale &&
                size.cy <= RetroHeight * DpiScale)
            {
                // Small windows are not scaled.
                Width = size.cx / DpiScale;
                Height = size.cy / DpiScale;
                props.rcDestination.Right =
                    props.rcDestination.Left + size.cx;
                props.rcDestination.Bottom =
                    props.rcDestination.Top + size.cy;
            }
            else
            {
                // Large windows are scaled while preserving aspect ratio.
                double controlAspectRatio = RetroWidth / RetroHeight;

                if (aspectRatio > controlAspectRatio)
                {
                    int height = (int)(RetroWidth / aspectRatio);
                    Width = RetroWidth;
                    Height = height;
                    props.rcDestination.Bottom =
                        props.rcDestination.Top +
                        (int)(height * DpiScale);
                }
                else if (aspectRatio < controlAspectRatio)
                {
                    int width = (int)(RetroHeight * aspectRatio);
                    Width = width;
                    Height = RetroHeight;
                    props.rcDestination.Right =
                        props.rcDestination.Left +
                        (int)(width * DpiScale);
                }
            }

            NativeMethods.DwmUpdateThumbnailProperties(
                _thumbHandle, ref props);
        }

        private void UserControl_Unloaded(object sender, RoutedEventArgs e)
        {
            if (_renderingHandler != null)
            {
                CompositionTarget.Rendering -= _renderingHandler;
                _renderingHandler = null;
            }

            if (_thumbHandle != IntPtr.Zero)
            {
                NativeMethods.DwmUnregisterThumbnail(_thumbHandle);
                _thumbHandle = IntPtr.Zero;
            }

            _toolTipTimer.Stop();
            if (ToolTip is ToolTip tip)
            {
                tip.IsOpen = false;
            }
        }

        private void UserControl_Loaded(object sender, RoutedEventArgs e)
        {
            DpiScale = PresentationSource.FromVisual(this)
                .CompositionTarget.TransformToDevice.M11;

            if (NativeMethods.IsCompositionEnabled() &&
                SourceWindowHandle != IntPtr.Zero &&
                Handle != IntPtr.Zero &&
                NativeMethods.DwmRegisterThumbnail(Handle,
                    SourceWindowHandle, out _thumbHandle) == 0)
            {
                Refresh();
                // Once loaded, refresh the DWM destination after rendering.
                _renderingHandler = (s, a) =>
                    Dispatcher.BeginInvoke(DispatcherPriority.Render,
                        new Action(Refresh));
                CompositionTarget.Rendering += _renderingHandler;
            }

            _toolTipTimer.Start();
        }

        private void ToolTipTimer_Tick(object? sender, EventArgs e)
        {
            if (ToolTip is ToolTip tip)
            {
                tip.PlacementTarget = this;
                tip.IsOpen = true;
            }
        }
    }
}
