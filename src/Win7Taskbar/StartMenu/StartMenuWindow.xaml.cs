// Win7Taskbar - Start Menu window (created at startup, shown on Win/orb)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. Gradients only — no Microsoft bitmaps.
//
// Aero glass reuses the overflow recipe in native/src/AeroGlass.h:
// SetWindowCompositionAttribute + ACCENT_ENABLE_BLURBEHIND. That API is
// undocumented; the same call is already used for the tray overflow.

using System;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    internal sealed class InverseBooleanToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => value is true ? Visibility.Collapsed : Visibility.Visible;

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => value is Visibility.Collapsed;
    }

    internal sealed class NullToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => value == null ? Visibility.Collapsed : Visibility.Visible;

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    internal sealed class NonEmptyStringToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
            => !string.IsNullOrWhiteSpace(value as string)
                ? Visibility.Visible
                : Visibility.Collapsed;

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    public partial class StartMenuWindow : Window
    {
        private readonly NativeBridge _bridge;
        private readonly StartMenuViewModel _vm;
        private readonly StartMenuClickAway _clickAway;
        private readonly StartMenuWin32Tooltip _infotip = new();
        private bool _suppressDeactivate;
        private bool _glassApplied;
        private DispatcherTimer? _crossfadeTimer;
        private bool _showingUserPhoto = true;
        private Point _dragOrigin;
        private bool _dragArmed;

        internal StartMenuWindow(NativeBridge bridge)
        {
            InitializeComponent();
            _bridge = bridge;
            _vm = new StartMenuViewModel(bridge, Dispatcher);
            DataContext = _vm;
            Visibility = Visibility.Hidden;
            _clickAway = new StartMenuClickAway(Dispatcher,
                () => new WindowInteropHelper(this).Handle,
                () =>
                {
                    if (!_suppressDeactivate)
                    {
                        Dismiss();
                    }
                });
        }

        internal bool IsMenuVisible => IsVisible && Visibility == Visibility.Visible;

        internal void PrepareCatalog()
        {
            _vm.RefreshCatalog();
        }

        internal void PresentAbove(Rect taskbarScreen, Rect orbScreen)
        {
            _suppressDeactivate = true;
            try
            {
                try { _bridge.AppSearchHide(); } catch (Exception) { }
                _vm.ShowDefaultList();
                ResetUserPhoto(animate: false);
                if (ActualHeight < 1)
                {
                    UpdateLayout();
                }
                double height = ActualHeight > 1 ? ActualHeight : Height;
                double width = ActualWidth > 1 ? ActualWidth : Width;
                bool vertical = taskbarScreen.Height > taskbarScreen.Width * 1.5;
                double left;
                double top;
                if (vertical)
                {
                    bool leftEdge = taskbarScreen.Left < 80;
                    left = leftEdge ? taskbarScreen.Right : taskbarScreen.Left - width;
                    top = Math.Max(0, SystemParameters.PrimaryScreenHeight - height);
                }
                else
                {
                    /* Bottom (or top) bar: sit above the Start button.
                     * Outer chrome has a 10 DIP shadow margin — pull left
                     * so the visible frame is flush with the orb. Frame
                     * 476 + photo overhang 25; window Height 511 includes
                     * a 10 DIP gap that used to float the chrome. */
                    left = (orbScreen.Width > 0 ? orbScreen.Left : taskbarScreen.Left) - 10;
                    bool topEdge = taskbarScreen.Top < 80;
                    const double photoOverhangDip = 25;
                    const double frameHeightDip = 476;
                    top = topEdge
                        ? taskbarScreen.Bottom
                        : taskbarScreen.Top - photoOverhangDip - frameHeightDip;
                }
                Left = left;
                Top = top;
                Topmost = true;
                Show();
                Activate();
                try { _clickAway.Start(); } catch (Exception) { }
                SearchBox.Focus();
                Keyboard.Focus(SearchBox);
            }
            catch (Exception)
            {
                /* keep the last known placement */
            }
            finally
            {
                Dispatcher.BeginInvoke(new Action(() => _suppressDeactivate = false),
                    System.Windows.Threading.DispatcherPriority.Input);
            }
        }

        internal void Dismiss()
        {
            try { _clickAway.Stop(); } catch (Exception) { }
            _vm.SearchText = string.Empty;
            Topmost = false;
            Hide();
        }

        internal void FocusSearch()
        {
            SearchBox.Focus();
            Keyboard.Focus(SearchBox);
        }

        private void OnSourceInitialized(object? sender, EventArgs e)
        {
            IntPtr hwnd = new WindowInteropHelper(this).Handle;
            if (hwnd == IntPtr.Zero)
            {
                return;
            }
            try
            {
                IntPtr ex = NativeMethods.GetWindowLongPtr(hwnd, NativeMethods.GWL_EXSTYLE);
                long next = ex.ToInt64() | NativeMethods.WS_EX_TOOLWINDOW;
                next &= ~0x00040000L; /* WS_EX_APPWINDOW, if a host set it */
                NativeMethods.SetWindowLongPtr(hwnd, NativeMethods.GWL_EXSTYLE, new IntPtr(next));
            }
            catch (Exception)
            {
            }
            TryEnableAeroGlass(hwnd);
        }

        /// <summary>
        /// Undocumented user32!SetWindowCompositionAttribute (WCA_ACCENT_POLICY
        /// / ACCENT_ENABLE_BLURBEHIND), the same path as AeroGlass::EnableGlass
        /// on the overflow flyout. Flagged because it is not a public API.
        /// </summary>
        private void TryEnableAeroGlass(IntPtr hwnd)
        {
            if (_glassApplied || hwnd == IntPtr.Zero)
            {
                return;
            }
            try
            {
                var margins = new MARGINS { cxLeftWidth = -1, cxRightWidth = -1, cyTopHeight = -1, cyBottomHeight = -1 };
                DwmExtendFrameIntoClientArea(hwnd, ref margins);

                uint colorization = 0x00A8C8E0;
                bool opaque = false;
                DwmGetColorizationColor(out colorization, out opaque);
                /* Win7 Start Menu used the system color but stayed readable.
                 * 0x2C (~17%) washed the right pane out. 0xB4 (~70%) matches
                 * AeroStyle::kReadableAlpha-and-then-some. Undocumented
                 * SetWindowCompositionAttribute — flagged. */
                const uint gradientAlpha = 0xB4;
                uint gradientColor = (gradientAlpha << 24) |
                    ((colorization & 0xFFu) << 16) |
                    (colorization & 0xFF00u) |
                    ((colorization >> 16) & 0xFFu);

                byte r = (byte)((colorization >> 16) & 0xFFu);
                byte g = (byte)((colorization >> 8) & 0xFFu);
                byte b = (byte)(colorization & 0xFFu);
                try
                {
                    Chrome.Background = new LinearGradientBrush(
                        Color.FromArgb(0xE0, r, g, b),
                        Color.FromArgb(0xC8, (byte)(r / 2), (byte)(g / 2), (byte)(b / 2)),
                        90);
                }
                catch (Exception)
                {
                }

                var accent = new ACCENT_POLICY
                {
                    AccentState = 3, /* ACCENT_ENABLE_BLURBEHIND */
                    AccentFlags = 0,
                    GradientColor = gradientColor,
                    AnimationId = 0
                };
                int size = Marshal.SizeOf<ACCENT_POLICY>();
                IntPtr accentPtr = Marshal.AllocHGlobal(size);
                try
                {
                    Marshal.StructureToPtr(accent, accentPtr, false);
                    var data = new WINDOWCOMPOSITIONATTRIBDATA
                    {
                        Attrib = 19, /* WCA_ACCENT_POLICY */
                        pvData = accentPtr,
                        cbData = size
                    };
                    SetWindowCompositionAttribute(hwnd, ref data);
                    _glassApplied = true;
                }
                finally
                {
                    Marshal.FreeHGlobal(accentPtr);
                }
            }
            catch (Exception)
            {
                /* glass is optional; the drawn Aero chrome still applies */
            }
        }

        private void OnDeactivated(object sender, EventArgs e)
        {
            if (_suppressDeactivate)
            {
                return;
            }
            Dismiss();
        }

        private void OnPreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                Dismiss();
                e.Handled = true;
                return;
            }
            if (e.Key == Key.Enter)
            {
                ActivateCurrent();
                e.Handled = true;
            }
        }

        private void OnPreviewTextInput(object sender, TextCompositionEventArgs e)
        {
            if (SearchBox.IsKeyboardFocusWithin)
            {
                return;
            }
            SearchBox.Focus();
        }

        private void OnLeftItemClick(object sender, MouseButtonEventArgs e)
        {
            if (sender is ListBoxItem { DataContext: StartMenuItem item })
            {
                LaunchItem(item);
                e.Handled = true;
            }
        }

        private void OnLeftItemContext(object sender, MouseButtonEventArgs e)
        {
            if (sender is ListBoxItem { DataContext: StartMenuItem item } &&
                !item.IsSeparator)
            {
                RunItemMenu(item);
                e.Handled = true;
            }
        }

        private void OnLeftListContext(object sender, MouseButtonEventArgs e)
        {
            if (e.Handled)
            {
                return;
            }
            Point pt = CursorScreenPoint();
            _suppressDeactivate = true;
            Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, new Action(() =>
            {
                try
                {
                    _vm.ShowEmptyLeftContextMenu((int)Math.Round(pt.X), (int)Math.Round(pt.Y));
                }
                catch (Exception)
                {
                }
                finally
                {
                    _suppressDeactivate = false;
                }
            }));
            e.Handled = true;
        }

        private void OnRightItemClick(object sender, MouseButtonEventArgs e)
        {
            if (sender is ListBoxItem { DataContext: StartMenuItem item } &&
                !item.IsSeparator)
            {
                _vm.OpenRightLink(item);
                Dismiss();
                e.Handled = true;
            }
        }

        private void OnRightItemContext(object sender, MouseButtonEventArgs e)
        {
            if (sender is ListBoxItem { DataContext: StartMenuItem item } &&
                !item.IsSeparator)
            {
                RunItemMenu(item);
                e.Handled = true;
            }
        }

        private void RunItemMenu(StartMenuItem item)
        {
            Point pt = CursorScreenPoint();
            _suppressDeactivate = true;
            Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, new Action(() =>
            {
                bool dismiss = false;
                try
                {
                    IntPtr hwnd = new WindowInteropHelper(this).Handle;
                    dismiss = _vm.ShowItemContextMenu(item, (int)Math.Round(pt.X),
                        (int)Math.Round(pt.Y), hwnd);
                }
                catch (Exception)
                {
                }
                finally
                {
                    _suppressDeactivate = false;
                }
                if (dismiss)
                {
                    Dismiss();
                }
            }));
        }

        private Point CursorScreenPoint()
        {
            try
            {
                if (NativeMethods.GetCursorPos(out NativeMethods.POINT p))
                {
                    return new Point(p.x, p.y);
                }
            }
            catch (Exception)
            {
            }
            try
            {
                return PointToScreen(Mouse.GetPosition(this));
            }
            catch (InvalidOperationException)
            {
                return new Point(Left, Top);
            }
        }

        private void OnRightItemMouseEnter(object sender, MouseEventArgs e)
        {
            if (sender is ListBoxItem { DataContext: StartMenuItem item } &&
                !item.IsSeparator)
            {
                ShowLinkIcon(item.Icon);
            }
        }

        private void OnRightListMouseLeave(object sender, MouseEventArgs e)
        {
            ResetUserPhoto(animate: true);
        }

        /// <summary>
        /// Two stacked Images, 150 ms cross-fade. The profile frame is
        /// hidden with the photo so the hovered item icon can show.
        /// </summary>
        public void CrossfadeIcon(ImageSource? newIcon, TimeSpan duration)
        {
            IconNew.BeginAnimation(OpacityProperty, null);
            IconOld.BeginAnimation(OpacityProperty, null);
            IconNew.Source = newIcon;
            IconNew.Opacity = 0;
            var fadeOut = new DoubleAnimation(1, 0, duration);
            var fadeIn = new DoubleAnimation(0, 1, duration);
            IconOld.BeginAnimation(OpacityProperty, fadeOut);
            IconNew.BeginAnimation(OpacityProperty, fadeIn);
            if (_crossfadeTimer != null)
            {
                _crossfadeTimer.Stop();
                _crossfadeTimer.Tick -= OnCrossfadeDone;
            }
            _crossfadeTimer = new DispatcherTimer { Interval = duration };
            _crossfadeTimer.Tick += OnCrossfadeDone;
            _crossfadeTimer.Start();
        }

        private void OnCrossfadeDone(object? sender, EventArgs e)
        {
            if (_crossfadeTimer != null)
            {
                _crossfadeTimer.Stop();
                _crossfadeTimer.Tick -= OnCrossfadeDone;
            }
            IconOld.BeginAnimation(OpacityProperty, null);
            IconNew.BeginAnimation(OpacityProperty, null);
            IconOld.Source = IconNew.Source;
            IconOld.Opacity = 1;
            IconNew.Opacity = 0;
        }

        private void ShowLinkIcon(ImageSource? icon)
        {
            PhotoFrame.Visibility = Visibility.Collapsed;
            _showingUserPhoto = false;
            CrossfadeIcon(icon, TimeSpan.FromMilliseconds(150));
        }

        private void ResetUserPhoto(bool animate)
        {
            PhotoFrame.Visibility = Visibility.Visible;
            if (_showingUserPhoto && IconOld.Source == _vm.UserPicture && animate)
            {
                return;
            }
            _showingUserPhoto = true;
            if (animate)
            {
                CrossfadeIcon(_vm.UserPicture, TimeSpan.FromMilliseconds(150));
            }
            else
            {
                IconOld.BeginAnimation(OpacityProperty, null);
                IconNew.BeginAnimation(OpacityProperty, null);
                _crossfadeTimer?.Stop();
                IconOld.Source = _vm.UserPicture;
                IconOld.Opacity = 1;
                IconNew.Source = null;
                IconNew.Opacity = 0;
            }
        }

        private void OnAllProgramsFooter(object sender, MouseButtonEventArgs e)
        {
            _vm.ToggleAllPrograms();
            e.Handled = true;
        }

        private void OnAllProgramsContext(object sender, MouseButtonEventArgs e)
        {
            RunItemMenu(new StartMenuItem { IsAllPrograms = true, Name = "All Programs" });
            e.Handled = true;
        }

        private void OnUserPictureClick(object sender, MouseButtonEventArgs e)
        {
            _vm.OpenUserAccounts();
            Dismiss();
            e.Handled = true;
        }

        private void OnLeftKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter)
            {
                ActivateCurrent();
                e.Handled = true;
            }
        }

        private void ActivateCurrent()
        {
            if (_vm.IsSearching)
            {
                LaunchSelected(SearchList);
            }
            else
            {
                LaunchSelected(LeftList);
            }
        }

        private void LaunchSelected(ListBox list)
        {
            if (list.SelectedItem is not StartMenuItem item)
            {
                if (list.Items.Count > 0)
                {
                    item = (StartMenuItem)list.Items[0];
                }
                else
                {
                    return;
                }
            }
            LaunchItem(item);
        }

        private void LaunchItem(StartMenuItem item)
        {
            if (item.IsSeparator)
            {
                return;
            }
            if (item.IsAllPrograms)
            {
                _vm.ToggleAllPrograms();
                return;
            }
            if (item.IsFolder)
            {
                _vm.ToggleFolder(item);
                return;
            }
            _vm.Launch(item);
            Dismiss();
        }

        private void OnShutdown(object sender, MouseButtonEventArgs e)
        {
            _vm.Power(0);
            Dismiss();
            e.Handled = true;
        }

        private void OnShutdownArrow(object sender, MouseButtonEventArgs e)
        {
            Point origin;
            try
            {
                origin = ShutdownArrow.PointToScreen(Mouse.GetPosition(ShutdownArrow));
            }
            catch (Exception)
            {
                origin = new Point(Left + Width - 40, Top + Height - 40);
            }
            e.Handled = true;
            _suppressDeactivate = true;
            Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, new Action(() =>
            {
                int choice = 0;
                try
                {
                    choice = _vm.ShowPowerMenu(
                        (int)Math.Round(origin.X),
                        (int)Math.Round(origin.Y));
                }
                catch (Exception)
                {
                }
                finally
                {
                    _suppressDeactivate = false;
                }
                if (choice > 0)
                {
                    try { _vm.ApplyPowerChoice(choice); } catch (Exception) { }
                    Dismiss();
                }
            }));
        }

        private void ClipVisibleChrome()
        {
            /* DWM blur-behind applies to the whole HWND. The 25 DIP photo
             * overhang is mostly empty; without a region it frosts desktop
             * icons above the menu. Clip to chrome + photo glass. */
            try
            {
                IntPtr hwnd = new WindowInteropHelper(this).Handle;
                if (hwnd == IntPtr.Zero)
                {
                    return;
                }
                double scale = 1;
                try
                {
                    PresentationSource? src = PresentationSource.FromVisual(this);
                    if (src?.CompositionTarget != null)
                    {
                        scale = src.CompositionTarget.TransformToDevice.M11;
                    }
                }
                catch (Exception)
                {
                }
                int Dip(double v) => (int)Math.Round(v * scale);
                IntPtr chrome = CreateRectRgn(Dip(10), Dip(25), Dip(10 + 411), Dip(25 + 476));
                IntPtr photo = CreateRectRgn(Dip(310), Dip(0), Dip(310 + 55), Dip(57));
                IntPtr combined = CreateRectRgn(0, 0, 0, 0);
                CombineRgn(combined, chrome, photo, RGN_OR);
                DeleteObject(chrome);
                DeleteObject(photo);
                if (SetWindowRgn(hwnd, combined, true) == 0)
                {
                    DeleteObject(combined);
                }
            }
            catch (Exception)
            {
            }
        }

        private void OnLeftPreviewMouseMove(object sender, MouseEventArgs e)
        {
            if (e.LeftButton != MouseButtonState.Pressed)
            {
                _dragArmed = false;
                return;
            }
            try
            {
                Point now = e.GetPosition(this);
                if (!_dragArmed)
                {
                    _dragOrigin = now;
                    _dragArmed = true;
                    return;
                }
                Vector delta = now - _dragOrigin;
                if (Math.Abs(delta.X) < SystemParameters.MinimumHorizontalDragDistance &&
                    Math.Abs(delta.Y) < SystemParameters.MinimumVerticalDragDistance)
                {
                    return;
                }
                if (LeftList.SelectedItem is not StartMenuItem item ||
                    item.IsSeparator || item.IsFolder)
                {
                    return;
                }
                string path = !string.IsNullOrEmpty(item.Path) ? item.Path : item.Target;
                if (string.IsNullOrEmpty(path) ||
                    (!File.Exists(path) && !Directory.Exists(path)))
                {
                    return;
                }
                _dragArmed = false;
                DragDrop.DoDragDrop(LeftList, new DataObject(DataFormats.FileDrop, new[] { path }),
                    DragDropEffects.Copy);
            }
            catch (Exception)
            {
            }
        }

        private void OnLeftDragOver(object sender, DragEventArgs e)
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
                ? DragDropEffects.Copy
                : DragDropEffects.None;
            e.Handled = true;
        }

        private void OnLeftDrop(object sender, DragEventArgs e)
        {
            e.Handled = true;
            try
            {
                if (!e.Data.GetDataPresent(DataFormats.FileDrop) ||
                    e.Data.GetData(DataFormats.FileDrop) is not string[] files)
                {
                    return;
                }
                foreach (string file in files)
                {
                    if (!string.IsNullOrWhiteSpace(file))
                    {
                        StartMenuStore.PinShortcut(file);
                    }
                }
                _vm.ShowDefaultList();
            }
            catch (Exception)
            {
            }
        }

        private const int RGN_OR = 2;

        [DllImport("gdi32.dll")]
        private static extern IntPtr CreateRectRgn(int x1, int y1, int x2, int y2);

        [DllImport("gdi32.dll")]
        private static extern int CombineRgn(IntPtr hrgnDest, IntPtr hrgnSrc1, IntPtr hrgnSrc2, int nCombineMode);

        [DllImport("gdi32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeleteObject(IntPtr ho);

        [DllImport("user32.dll")]
        private static extern int SetWindowRgn(IntPtr hWnd, IntPtr hRgn, [MarshalAs(UnmanagedType.Bool)] bool bRedraw);

        [StructLayout(LayoutKind.Sequential)]
        private struct MARGINS
        {
            public int cxLeftWidth;
            public int cxRightWidth;
            public int cyTopHeight;
            public int cyBottomHeight;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ACCENT_POLICY
        {
            public int AccentState;
            public int AccentFlags;
            public uint GradientColor;
            public int AnimationId;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct WINDOWCOMPOSITIONATTRIBDATA
        {
            public int Attrib;
            public IntPtr pvData;
            public int cbData;
        }

        [DllImport("dwmapi.dll")]
        private static extern int DwmExtendFrameIntoClientArea(IntPtr hwnd, ref MARGINS pMarInset);

        [DllImport("dwmapi.dll")]
        private static extern int DwmGetColorizationColor(out uint pcrColorization, [MarshalAs(UnmanagedType.Bool)] out bool pfOpaqueBlend);

        /* Undocumented. Same export as native/src/AeroGlass.h. */
        [DllImport("user32.dll")]
        private static extern int SetWindowCompositionAttribute(IntPtr hwnd, ref WINDOWCOMPOSITIONATTRIBDATA data);
    }
}
