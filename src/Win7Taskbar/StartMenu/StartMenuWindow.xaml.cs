// Win7Taskbar - Start Menu window (created at startup, shown on Win/orb)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. Gradients only — no Microsoft bitmaps.
//
// Simulated Aero wash on Chrome: system color + two faint white highlights.
// No DWM blur APIs. SetWindowRgn still clips chrome + photo.

using System;
using System.ComponentModel;
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
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32.SafeHandles;
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
        private DispatcherTimer? _infotipTimer;
        private int _fadeGeneration;
        private bool _showingUserPhoto = true;
        private bool _searchLayoutOpen;
        private Point _dragOrigin;
        private bool _dragArmed;
        private IntPtr _infotipOwner;
        private string? _infotipTitle;
        private string? _infotipText;
        private int _infotipX;
        private int _infotipY;

        internal StartMenuWindow(NativeBridge bridge)
        {
            InitializeComponent();
            _bridge = bridge;
            _vm = new StartMenuViewModel(bridge, Dispatcher);
            DataContext = _vm;
            _vm.PropertyChanged += OnViewModelPropertyChanged;
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
                SnapSearchLayout(false);
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
                ClipVisibleChrome();
                try { _clickAway.Start(); } catch (Exception) { }
                SearchBox.Focus();
                Keyboard.Focus(SearchBox);
                StartMenuHost.NotifyVisible(true);
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
            CancelInfotip();
            _vm.SearchText = string.Empty;
            Topmost = false;
            Hide();
            StartMenuHost.NotifyVisible(false);
        }

        internal void FocusSearch()
        {
            SearchBox.Focus();
            Keyboard.Focus(SearchBox);
        }

        private void OnSourceInitialized(object? sender, EventArgs e)
        {
            IntPtr hwnd = new WindowInteropHelper(this).Handle;
            if (hwnd != IntPtr.Zero)
            {
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
            try
            {
                double scale = 1;
                try { scale = VisualTreeHelper.GetDpi(this).DpiScaleX; }
                catch (Exception) { }
                SearchGlyph.Source = LoadSearchGlyph(16, scale);
            }
            catch (Exception)
            {
            }
        }

        protected override void OnDpiChanged(DpiScale oldDpi, DpiScale newDpi)
        {
            try
            {
                base.OnDpiChanged(oldDpi, newDpi);
            }
            catch (Exception)
            {
            }
            try
            {
                SearchGlyph.Source = LoadSearchGlyph(16, newDpi.DpiScaleX);
                if (IsVisible)
                {
                    ClipVisibleChrome();
                }
            }
            catch (Exception)
            {
            }
        }

        /// <summary>
        /// Simulated Aero wash (no DWM blur APIs). See ApplySimulatedGlass.
        /// </summary>
        private void TryEnableAeroGlass(IntPtr hwnd)
        {
            if (hwnd == IntPtr.Zero)
            {
                return;
            }
            try
            {
                ApplySimulatedGlass();
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
                ShowWin32Infotip(sender as FrameworkElement, item);
            }
        }

        private void OnRightItemMouseLeave(object sender, MouseEventArgs e)
        {
            CancelInfotip();
        }

        private void OnRightListMouseLeave(object sender, MouseEventArgs e)
        {
            CancelInfotip();
            ResetUserPhoto(animate: true);
        }

        private void ShowWin32Infotip(FrameworkElement? host, StartMenuItem item)
        {
            CancelInfotip();
            if (host == null || !item.HasInfotip)
            {
                return;
            }
            try
            {
                if (NativeMethods.GetCursorPos(out NativeMethods.POINT cursor))
                {
                    _infotipX = cursor.x;
                    _infotipY = cursor.y;
                }
                else
                {
                    Point pt = host.PointToScreen(new Point(0, host.ActualHeight));
                    _infotipX = (int)Math.Round(pt.X);
                    _infotipY = (int)Math.Round(pt.Y);
                }
                _infotipOwner = new WindowInteropHelper(this).Handle;
                _infotipTitle = item.Name;
                _infotipText = item.Infotip;
                _infotipTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
                _infotipTimer.Tick += OnInfotipDelayElapsed;
                _infotipTimer.Start();
            }
            catch (Exception)
            {
            }
        }

        private void OnInfotipDelayElapsed(object? sender, EventArgs e)
        {
            try
            {
                if (_infotipTimer != null)
                {
                    _infotipTimer.Stop();
                    _infotipTimer.Tick -= OnInfotipDelayElapsed;
                    _infotipTimer = null;
                }
                if (NativeMethods.GetCursorPos(out NativeMethods.POINT cursor))
                {
                    _infotipX = cursor.x;
                    _infotipY = cursor.y;
                }
                _infotip.Show(_infotipOwner, _infotipTitle, _infotipText, _infotipX, _infotipY);
            }
            catch (Exception)
            {
            }
        }

        private void CancelInfotip()
        {
            try
            {
                if (_infotipTimer != null)
                {
                    _infotipTimer.Stop();
                    _infotipTimer.Tick -= OnInfotipDelayElapsed;
                    _infotipTimer = null;
                }
                _infotip.Hide();
            }
            catch (Exception)
            {
            }
        }

        /// <summary>
        /// Two stacked Images, 200 ms cross-fade. The profile frame is
        /// hidden with the photo so the hovered item icon can show.
        /// </summary>
        public void CrossfadeIcon(ImageSource? newIcon, TimeSpan duration)
        {
            try
            {
                if (ReferenceEquals(IconOld.Source, newIcon) &&
                    IconOld.Opacity >= 0.95 && IconNew.Opacity <= 0.05)
                {
                    return;
                }
                if (newIcon is BitmapSource bitmap && bitmap.CanFreeze && !bitmap.IsFrozen)
                {
                    try { bitmap.Freeze(); } catch (Exception) { }
                }
                IconNew.BeginAnimation(OpacityProperty, null);
                IconOld.BeginAnimation(OpacityProperty, null);
                IconNew.Source = newIcon;
                IconNew.Opacity = 0;
                var fadeOut = new DoubleAnimation(1, 0, duration)
                {
                    FillBehavior = FillBehavior.HoldEnd
                };
                var fadeIn = new DoubleAnimation(0, 1, duration)
                {
                    FillBehavior = FillBehavior.HoldEnd
                };
                int gen = ++_fadeGeneration;
                fadeIn.Completed += (_, _) =>
                {
                    if (gen == _fadeGeneration)
                    {
                        OnCrossfadeDone(null, EventArgs.Empty);
                    }
                };
                IconOld.BeginAnimation(OpacityProperty, fadeOut);
                IconNew.BeginAnimation(OpacityProperty, fadeIn);
                if (_crossfadeTimer != null)
                {
                    _crossfadeTimer.Stop();
                    _crossfadeTimer.Tick -= OnCrossfadeDone;
                    _crossfadeTimer = null;
                }
            }
            catch (Exception)
            {
                try
                {
                    IconOld.Source = newIcon;
                    IconOld.Opacity = 1;
                    IconNew.Opacity = 0;
                }
                catch (Exception)
                {
                }
            }
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
            CrossfadeIcon(icon, TimeSpan.FromMilliseconds(200));
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
                CrossfadeIcon(_vm.UserPicture, TimeSpan.FromMilliseconds(200));
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
            try
            {
                IntPtr hwnd = new WindowInteropHelper(this).Handle;
                if (hwnd == IntPtr.Zero)
                {
                    return;
                }

                using SafeGdiRegion chrome = CreateRectRgn(PxX(10), PxY(25), PxX(10 + 411), PxY(25 + 476));
                using SafeGdiRegion photo = CreateRectRgn(PxX(310), PxY(0), PxX(310 + 55), PxY(57));
                if (chrome is null || photo is null || chrome.IsInvalid || photo.IsInvalid)
                {
                    return;
                }

                using SafeGdiRegion windowRgn = CreateRectRgn(0, 0, 0, 0);
                if (windowRgn is null || windowRgn.IsInvalid)
                {
                    return;
                }

                try
                {
                    CombineRgn(windowRgn.DangerousGetHandle(),
                        chrome.DangerousGetHandle(),
                        photo.DangerousGetHandle(),
                        RGN_OR);
                    if (SetWindowRgn(hwnd, windowRgn.DangerousGetHandle(), true) != 0)
                    {
                        /* SetWindowRgn took ownership; skip DeleteObject. */
                        windowRgn.SetHandleAsInvalid();
                    }
                }
                catch (Exception)
                {
                }

                ApplySimulatedGlass();
            }
            catch (Exception)
            {
                /* glass opzionale: il frame disegnato resta valido */
            }
        }

        private int PxX(double dip)
        {
            try
            {
                return (int)Math.Round(dip * VisualTreeHelper.GetDpi(this).DpiScaleX);
            }
            catch (Exception)
            {
                return (int)Math.Round(dip);
            }
        }

        private int PxY(double dip)
        {
            try
            {
                return (int)Math.Round(dip * VisualTreeHelper.GetDpi(this).DpiScaleY);
            }
            catch (Exception)
            {
                return (int)Math.Round(dip);
            }
        }

        private static ImageSource? LoadSearchGlyph(double dipSize, double dpiScale)
        {
            try
            {
                byte[] bytes = Convert.FromBase64String(SearchGlyphIcoBase64);
                using (var ms = new MemoryStream(bytes, writable: false))
                {
                    var decoder = new IconBitmapDecoder(ms,
                        BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
                    int target = (int)Math.Ceiling(dipSize * dpiScale);
                    BitmapFrame? best = null;
                    foreach (BitmapFrame f in decoder.Frames)
                    {
                        if (best == null)
                        {
                            best = f;
                        }
                        else if (best.PixelWidth < target)
                        {
                            /* ancora sotto il target: prendi un frame piu' grande */
                            if (f.PixelWidth > best.PixelWidth) { best = f; }
                        }
                        else if (f.PixelWidth >= target && f.PixelWidth < best.PixelWidth)
                        {
                            /* gia' sopra il target: prendi il piu' piccolo che basta */
                            best = f;
                        }
                    }
                    if (best == null) { return null; }
                    best.Freeze();
                    return best;
                }
            }
            catch (Exception)
            {
                return null;
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

        private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(StartMenuViewModel.IsSearching))
            {
                AnimateSearchLayout(_vm.IsSearching);
            }
        }

        /// <summary>
        /// Win7: the right links fade out and the white search list covers
        /// the two columns. Outer 431x511 / chrome 411x476 stay put.
        /// Photo + frame hide; shutdown strip fades to white with black label.
        /// </summary>
        private void AnimateSearchLayout(bool searching)
        {
            if (_searchLayoutOpen == searching)
            {
                return;
            }
            _searchLayoutOpen = searching;
            TimeSpan dt = TimeSpan.FromMilliseconds(90);
            try
            {
                SearchHost.BeginAnimation(OpacityProperty, null);
                RightList.BeginAnimation(OpacityProperty, null);
                PhotoHost.BeginAnimation(OpacityProperty, null);
                SearchShutWash.BeginAnimation(OpacityProperty, null);
                ApplySearchShutdownInk(searching);
                if (searching)
                {
                    SearchHost.Visibility = Visibility.Visible;
                    SearchHost.IsHitTestVisible = true;
                    SearchHost.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(0, 1, dt) { FillBehavior = FillBehavior.HoldEnd });
                    RightList.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(1, 0, dt) { FillBehavior = FillBehavior.HoldEnd });
                    RightList.IsHitTestVisible = false;
                    PhotoHost.IsHitTestVisible = false;
                    PhotoHost.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(1, 0, dt) { FillBehavior = FillBehavior.HoldEnd });
                    SearchShutWash.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(0, 1, dt) { FillBehavior = FillBehavior.HoldEnd });
                }
                else
                {
                    var fadeOut = new DoubleAnimation(1, 0, dt) { FillBehavior = FillBehavior.HoldEnd };
                    fadeOut.Completed += (_, _) =>
                    {
                        try
                        {
                            if (!_searchLayoutOpen)
                            {
                                SearchHost.Visibility = Visibility.Collapsed;
                                SearchHost.IsHitTestVisible = false;
                            }
                        }
                        catch (Exception)
                        {
                        }
                    };
                    SearchHost.BeginAnimation(OpacityProperty, fadeOut);
                    SearchHost.IsHitTestVisible = false;
                    RightList.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(0, 1, dt) { FillBehavior = FillBehavior.HoldEnd });
                    RightList.IsHitTestVisible = true;
                    PhotoHost.IsHitTestVisible = true;
                    PhotoHost.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(0, 1, dt) { FillBehavior = FillBehavior.HoldEnd });
                    SearchShutWash.BeginAnimation(OpacityProperty,
                        new DoubleAnimation(1, 0, dt) { FillBehavior = FillBehavior.HoldEnd });
                }
            }
            catch (Exception)
            {
                SnapSearchLayout(searching);
            }
        }

        private void SnapSearchLayout(bool searching)
        {
            try
            {
                _searchLayoutOpen = searching;
                SearchHost.BeginAnimation(OpacityProperty, null);
                RightList.BeginAnimation(OpacityProperty, null);
                PhotoHost.BeginAnimation(OpacityProperty, null);
                SearchShutWash.BeginAnimation(OpacityProperty, null);
                SearchHost.Visibility = searching ? Visibility.Visible : Visibility.Collapsed;
                SearchHost.Opacity = searching ? 1 : 0;
                SearchHost.IsHitTestVisible = searching;
                RightList.Opacity = searching ? 0 : 1;
                RightList.IsHitTestVisible = !searching;
                PhotoHost.Opacity = searching ? 0 : 1;
                PhotoHost.IsHitTestVisible = !searching;
                SearchShutWash.Opacity = searching ? 1 : 0;
                ApplySearchShutdownInk(searching);
            }
            catch (Exception)
            {
            }
        }

        private void ApplySearchShutdownInk(bool searching)
        {
            try
            {
                ShutdownLabel.Foreground = searching ? Brushes.Black : Brushes.White;
                ShutdownArrowGlyph.Fill = searching ? Brushes.Black : Brushes.White;
            }
            catch (Exception)
            {
            }
        }

        /// <summary>
        /// Undocumented user32!SetWindowCompositionAttribute (WCA_ACCENT_POLICY
        /// / ACCENT_ENABLE_TRANSPARENTGRADIENT). Tinted see-through, no blur.
        /// Flagged because it is not a public API. DwmExtend and
        /// DwmEnableBlurBehindWindow DllImports remain; they are not called.
        /// </summary>
        private void ApplyAccentGlass(IntPtr hwnd)
        {
            if (hwnd == IntPtr.Zero)
            {
                return;
            }
            try
            {
                uint colorization = 0x00A8C8E0;
                bool opaque = false;
                try
                {
                    DwmGetColorizationColor(out colorization, out opaque);
                }
                catch (Exception)
                {
                }

                const uint gradientAlpha = 0xB4;
                uint gradientColor = (gradientAlpha << 24) |
                    ((colorization & 0xFFu) << 16) |
                    (colorization & 0xFF00u) |
                    ((colorization >> 16) & 0xFFu);

                var accent = new ACCENT_POLICY
                {
                    AccentState = 2, /* ACCENT_ENABLE_TRANSPARENTGRADIENT */
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
                    if (SetWindowCompositionAttribute(hwnd, ref data) != 0)
                    {
                        _glassApplied = true;
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(accentPtr);
                }
            }
            catch (Exception)
            {
            }
        }

        /* Vetro simulato: nessuna API DWM, nessun blur. Regola gli alpha a occhio:
         * piu' alti = menu piu' opaco e testo piu' leggibile. */
        private const byte SimTopAlpha = 0xCD;
        private const byte SimBottomAlpha = 0xB5;

        private void ApplySimulatedGlass()
        {
            try
            {
                uint c = 0x00A8C8E0;
                bool o = false;
                try { DwmGetColorizationColor(out c, out o); } catch (Exception) { }
                byte r = (byte)((c >> 16) & 0xFFu);
                byte g = (byte)((c >> 8) & 0xFFu);
                byte b = (byte)(c & 0xFFu);

                var group = new DrawingGroup();

                /* Fondo: colore di sistema, scurito verso il basso. */
                var baseFill = new LinearGradientBrush(
                    Color.FromArgb(SimTopAlpha, r, g, b),
                    Color.FromArgb(SimBottomAlpha, (byte)(r / 2), (byte)(g / 2), (byte)(b / 2)),
                    90);
                group.Children.Add(new GeometryDrawing(baseFill, null,
                    new RectangleGeometry(new Rect(0, 0, 411, 476))));

                /* Riflesso 1: triangolo in alto a sinistra. */
                var tri = new StreamGeometry();
                using (StreamGeometryContext ctx = tri.Open())
                {
                    ctx.BeginFigure(new Point(0, 0), true, true);
                    ctx.LineTo(new Point(290, 0), true, false);
                    ctx.LineTo(new Point(0, 200), true, false);
                }
                tri.Freeze();
                group.Children.Add(new GeometryDrawing(
                    new LinearGradientBrush(
                        Color.FromArgb(0x3A, 255, 255, 255),
                        Color.FromArgb(0x00, 255, 255, 255),
                        new Point(0, 0), new Point(0.75, 0.75)),
                    null, tri));

                /* Riflesso 2: banda sottile parallela. */
                var band = new StreamGeometry();
                using (StreamGeometryContext ctx = band.Open())
                {
                    ctx.BeginFigure(new Point(305, 0), true, true);
                    ctx.LineTo(new Point(360, 0), true, false);
                    ctx.LineTo(new Point(0, 245), true, false);
                    ctx.LineTo(new Point(0, 205), true, false);
                }
                band.Freeze();
                group.Children.Add(new GeometryDrawing(
                    new SolidColorBrush(Color.FromArgb(0x16, 255, 255, 255)), null, band));

                group.Freeze();
                var brush = new DrawingBrush(group) { Stretch = Stretch.Fill };
                brush.Freeze();
                Chrome.Background = brush;
            }
            catch (Exception)
            {
                /* se fallisce resta il gradiente XAML */
            }
        }

        private const int RGN_OR = 2;
        private const int DWM_BB_ENABLE = 0x1;
        private const int DWM_BB_BLURREGION = 0x2;

        [StructLayout(LayoutKind.Sequential)]
        private struct DWM_BLURBEHIND
        {
            public int dwFlags;
            [MarshalAs(UnmanagedType.Bool)]
            public bool fEnable;
            public IntPtr hRgnBlur;
            [MarshalAs(UnmanagedType.Bool)]
            public bool fTransitionOnMaximized;
        }

        [DllImport("dwmapi.dll")]
        private static extern int DwmEnableBlurBehindWindow(IntPtr hwnd, ref DWM_BLURBEHIND bb);

        [DllImport("gdi32.dll")]
        private static extern SafeGdiRegion CreateRectRgn(int x1, int y1, int x2, int y2);

        [DllImport("gdi32.dll")]
        private static extern int CombineRgn(IntPtr hrgnDest, IntPtr hrgnSrc1, IntPtr hrgnSrc2, int nCombineMode);

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

        /* From-scratch blue #1473C4 lens ICO (PNG frames 16/20/24/32/40/48/64/256).
         * Handle leans like '/', lens top-right. No Microsoft bitmaps. */
        private const string SearchGlyphIcoBase64 =
            "AAABAAgAEBAAAAEAIAD/AgAAhgAAABQUAAABACAAqAMAAIUDAAAYGAAAAQAgAO8DAAAtBwAAICAAAAEAIAD8BAAAHAsAACgoAAABACAA3QUAAB" +
            "gQAAAwMAAAAQAgAPsGAAD1FQAAQEAAAAEAIADgCAAA8BwAAAAAAAABACAAJSQAANAlAACJUE5HDQoaCgAAAA1JSERSAAAAEAAAABAIAwAAACgt" +
            "D1MAAAAEZ0FNQQAAsY8L/GEFAAAAIGNIUk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAADhUExURQAAABRzxBRzxBRzxB" +
            "RzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRz" +
            "xBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxB" +
            "RzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxAAAANRZLrQAAABJdFJOUwAAAQMFNlc6B0ji/ehTWcNDIj66ZQIT" +
            "9qyd+x8EYiQGGP58kuzbsIQI5qFBUkJVv/QxKe3I3ZS82R7ae9Lp03YLF4URyo8KxZr2zhD7AAAAAWJLR0QAiAUdSAAAAAd0SU1FB+oJGAkdCW" +
            "rXYHIAAADGSURBVBjTJY/ZQkIxDEQ7bVE2RZYAyoXLvioouIEoq2j+/4dISt8ymebMGGMMLBx84uo6CWdlNjqn0pzh7A1cMDjccu4uXyhyCSoQ" +
            "ypUqrCXcP9T0S4Q6NxCTR7PVVodHp2vFIZZe/yIMKsMg2NFYBYcJPyL2MZ54qhChzJ5fEGG+eIWmitB441bu/YM/awipliv+qq+/fzZbkF7YrX" +
            "kPsrIjgiEcjvyLyDuQF7jB6Y//pZRi5Z5A5jyGI4Sal6r5k0TUXXhnzxwSDYazpk4AAAAldEVYdGRhdGU6Y3JlYXRlADIwMjYtMDktMjRUMDk6" +
            "Mjg6NDQrMDA6MDAcCKTbAAAAJXRFWHRkYXRlOm1vZGlmeQAyMDI2LTA5LTI0VDA5OjI4OjQ0KzAwOjAwbVUcZwAAAABJRU5ErkJggolQTkcNCh" +
            "oKAAAADUlIRFIAAAAUAAAAFAgDAAAAulftPwAAAARnQU1BAACxjwv8YQUAAAAgY0hSTQAAeiYAAICEAAD6AAAAgOgAAHUwAADqYAAAOpgAABdw" +
            "nLpRPAAAASxQTFRFAAAAFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEAAAA4NWc7QAAAGJ0Uk5TAAABAwIG" +
            "ICcNE4Pi8qkxLuDssqjZ/WkWgAhK6FAEjXs21gcJ6eYKmEkoplF9MJlFiRjFBXBkvznV/CRCmpM++O5/OjRfzsY38NqdDi/q3h4MWYqSbCbj5R" +
            "/d69Q4r/RDfkaVwzqPAAAAAWJLR0QAiAUdSAAAAAd0SU1FB+oJGAkdCWrXYHIAAAELSURBVBjTXZDnUsMwEIS9khxCDxBqEMXUAKGJ3kIn9Bp6" +
            "3fd/CM6WYRjul+abO20JgnigoA3CTE0WSiv8MtTW1ZMNjU3yTGDMmnNsaW3Ls70D2kODzhy7umHQU2BvumnRx35obS0GOBil50PDI6NKQ6kIYx" +
            "xPoEaRE7CiJzeTnEphidOwCmIIM5xNoMEc5wUqBYcFLv6oL3EZkdUO4crqmveusc58BkY+3eAmfB6HrTK3d3b39g94mEXCLI4qPJaUMienMJ6d" +
            "nfPi8ur65vauGjcTxKx6z4ciYu9JWyqQvI9PfC6JI+uck+KkOvH4wsqrzyP24dO88f1D9NNu04ifha9/TIRMGPfzl30DoeEkJu+M998AAAAldE" +
            "VYdGRhdGU6Y3JlYXRlADIwMjYtMDktMjRUMDk6Mjg6NDQrMDA6MDAcCKTbAAAAJXRFWHRkYXRlOm1vZGlmeQAyMDI2LTA5LTI0VDA5OjI4OjQ0" +
            "KzAwOjAwbVUcZwAAAABJRU5ErkJggolQTkcNChoKAAAADUlIRFIAAAAYAAAAGAgDAAAA16nNygAAAARnQU1BAACxjwv8YQUAAAAgY0hSTQAAei" +
            "YAAICEAAD6AAAAgOgAAHUwAADqYAAAOpgAABdwnLpRPAAAASxQTFRFAAAAFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEAAAA4NWc7QAAAGJ0Uk5TAAABAgMLBwQ1lMri27lwFAmc/utUu/urUTI5btZnkfBNBZo2LPlXBrB+MfUntG3Oy1Bxv13BY5UZOknz" +
            "KM3HEWJ2VQ4gi/ip/aVK+n9750H3/INMRCI38Y4v7Znlo663CAz7uJ0sAAAAAWJLR0QAiAUdSAAAAAd0SU1FB+oJGAkdCWrXYHIAAAFSSURBVC" +
            "jPZVGHVgIxEGSSSCwgKohY4VA4VLB3BRUb9l6xzv9/hMlxdz5130vy3sxmZ2c3EmkFBKQCRFvUXNocH494eHtHZ1cs3p2AEvjB0dNLG31MpqAQ" +
            "FBKI9jM9kBkcGh4hR6EDQmGM2RykyXDyTI/7Pww+UWARrlbKkSixFBAak4zDEbamxhSny+GPCmeghbByArMs+oTAHOcN4etVuOATEotcMgRsks" +
            "QyV0KNVebhGP/W0VqW6yGxwc0tuKZdFTWNVHWgIRBnbdv6UMgUuINA28UuGavvQe4fHPII0p+sgzobZlDJ6kiBPHaFCPCT0wbPzi8Ml66lWtP1" +
            "8Mura96YbQze3t1LKAnr1Mzg4bHBJzjaG4m0DyIWf35psGntCaW19GBTSaL8Sr7ZuuE+bUgkYuS7kBLhPr3QyJEfrpC/8+22P5v5L/zNNxq2Ef" +
            "Uf/wapxy1TALrRnAAAACV0RVh0ZGF0ZTpjcmVhdGUAMjAyNi0wOS0yNFQwOToyODo0NCswMDowMBwIpNsAAAAldEVYdGRhdGU6bW9kaWZ5ADIw" +
            "MjYtMDktMjRUMDk6Mjg6NDQrMDA6MDBtVRxnAAAAAElFTkSuQmCCiVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAABGdBTUEAAL" +
            "GPC/xhBQAAACBjSFJNAAB6JgAAgIQAAPoAAACA6AAAdTAAAOpgAAA6mAAAF3CculE8AAABgFBMVEUAAAAUc8QUc8QUc8QUc8QUc8QUc8QUc8QU" +
            "c8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8" +
            "QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QU" +
            "c8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8" +
            "QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QU" +
            "c8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QUc8QAAAB37HdBAAAAfnRSTlMAAAECBAMNR3iWmH1PExCF6vAaPOL+71FJ+fqxZUNCXqb1/WIq8csuIL" +
            "j7ugcFn9QIRuEVBmFpTLnY2+gWGcquNCe+oBvyx6s33BTV7KhcQMEKu22hhj38shecVY+SKOZ7jH+Bl7V2fh9nmraeb2qKYJRWSqr3I2v2xS/z" +
            "Ek48YIzWAAAAAWJLR0QAiAUdSAAAAAd0SU1FB+oJGAkdCWrXYHIAAAHvSURBVDjLdVNnV9RAFM2dsooFAc0iUVBWBTcS3bXX1UXsBRUx2HvH3t" +
            "v97b5JzmYnB3Y+zEneva/d9yYIvAMFbRRUEMhltPwGpaMUrEZlxcq+VavXrIW2cNySv0H/ugFmZ7BvCHm0Er5+A8OcELI6LAaPIPGxcYSMNm0e" +
            "HduydZw1bhOGF0Bj+46QE5PQSr531hmHY7BehRq7yPoUTGKskfS7yT2NbgRl0ST37nN5letH7Z8gD3gE41wOIsm6zyo+RB72CDBHWB2SApCXpN" +
            "A4ymPHC4LGiUG2nL1TtMVJRqcKgkU75rSk9nKeJptehJkzPNvJ4CIYnGN83qth6gIHLhYMyWUu8fKM3+YV8qrfxSx5rdTmsChzXYji7OaOG+Sc" +
            "RxBbi7wJJUJqk1jMi279JR1wiykXbrsyFCp3OMK7MF1cYt9jGLF6v/mgPTv/kDEfyYC7uMFj1mrZLqRpds/JxqgCT/CEccqnz+J8YdLnk92Nyv" +
            "AXjGO+BF69fvO2vviuLTkLXAn+nmnED0jctDMdrIXq4KLOR9YifnJBtTHWSpuqs/Vw+GeHLyKz5gsD+AJ8iRnxq5JXUXoIxRBGv4koC0YvfUv5" +
            "nL9XBZ9uqOXxwOAHf/JXBT1wWaXff9j6K6ouj7su2/96+7suZHq6J/4fxndfawK+3JwAAAAldEVYdGRhdGU6Y3JlYXRlADIwMjYtMDktMjRUMD" +
            "k6Mjg6NDQrMDA6MDAcCKTbAAAAJXRFWHRkYXRlOm1vZGlmeQAyMDI2LTA5LTI0VDA5OjI4OjQ0KzAwOjAwbVUcZwAAAABJRU5ErkJggolQTkcN" +
            "ChoKAAAADUlIRFIAAAAoAAAAKAgDAAAAuyBIXwAAAARnQU1BAACxjwv8YQUAAAAgY0hSTQAAeiYAAICEAAD6AAAAgOgAAHUwAADqYAAAOpgAAB" +
            "dwnLpRPAAAAapQTFRFAAAAFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEAAAAueh6LQAAAIx0Uk5TAAABBAUDAgklPkg2HE6r5f751Y4tRdP9+vunjPzuTKS7d1NPW4tckSbYRm" +
            "ap4RTXYwau+I1hne8a8yFqZxfwphXsqjy41FiQ6BOJHlWS5xI5aLzR7bQqdV/q6c6+gkF9vdYM4sA6CmTm9jcPydCPckrBhrEQrHo4k9Lyo4Qr" +
            "NCQNlZqDawfNh9qK4yDW58+kAAAAAWJLR0QAiAUdSAAAAAd0SU1FB+oJGAkdCWrXYHIAAAKYSURBVDjLhZTnWxNBEMYzu7cbkQh3WO5OIiD2Qq" +
            "RYUEBQwIYIFozYC4KgoghW7L28f7SzG3zMXgLZD/ck9/xu5p13ZieVSh4SJKQkEin+4Sl+UKrcEURKC0qvqlpdnVHmT1nShiNvTU2tDyCoW7uO" +
            "tCIhynPrNzAURlHMcLgxQ5LfluQVWtRzqCwKJ4ixqYFkMjvn0KIRUYSgaXPzlq3b6uAHwHZDugGFoh0mXM1Oo0uQbtiFyPd3kyI3oKJmxH64hy" +
            "vwpJSepvReRGjJkXYDUnofYrRSmxJC2Mo0taMD+0k6oKQDCHCQvCWX+ako1wIc6nRSC01NwOEjpP9pZ9KjLv642wE19RwFetk38V+2pr5jwHEH" +
            "VNTPyluNwU6BA8CgU4ykE1xKP6li01j4SeDU6WLQo26OWO2aJjw6w8KH3IhnOWJVScRh4NyIq3GUwfMlGi8AFxNV5y4B7cmqO9mKRtdHRWPwOy" +
            "6zyGIf8+zjFRc0ZQfsWVFnJA2NA1dzLsjvr7HKPHlmVk23PWq7zr2+4fbaCL+ZRYhbLM3zpMcjkbvNOe7cdebRjsDIPYRZTNwvTM/k1AN2dnrG" +
            "sdZyDx9xwDgCHs92dT9prEUYoOOpM+HMaZGeYy6ERQt3JouWUffOMKflGJcS4tm8YbIRf4Hp2QXDieJ4Wj9nLosXpF++ev3GRzw+kc8k7rUwFS" +
            "8yF+MtXwQi3ffu/YePjEjt5mWzP1lu2HRQSXtjSPP1Ekmu3nKfyawlswgUr6DEhrId+GK5RbuUUuWP5fJcRYx5rmhZznb+q+UGvgm93DosdHjK" +
            "9ANzk7QCZwZ7JvQ54vcFM4jLcyzwh1lvgz0rcylW2Mp5f2YqcLbLv36P/anIFVogtK7ALfVFVeL+Anm5kD4epRa0AAAAJXRFWHRkYXRlOmNyZW" +
            "F0ZQAyMDI2LTA5LTI0VDA5OjI4OjQ0KzAwOjAwHAik2wAAACV0RVh0ZGF0ZTptb2RpZnkAMjAyNi0wOS0yNFQwOToyODo0NCswMDowMG1VHGcA" +
            "AAAASUVORK5CYIKJUE5HDQoaCgAAAA1JSERSAAAAMAAAADAIAwAAAGDcCbUAAAAEZ0FNQQAAsY8L/GEFAAAAIGNIUk0AAHomAACAhAAA+gAAAI" +
            "DoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAHvUExURQAAABRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRz" +
            "xBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxB" +
            "RzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRz" +
            "xBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxB" +
            "RzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRz" +
            "xBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxB" +
            "RzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxBRzxAAAAFYRuQMAAACjdFJOUwAAAQIEBQMIF1qYv9bl28ShZyIuo/L5t0INlP38+Pr7sR0p1+1HNOvG" +
            "i2NgYYG69P5WJembKBqC8QbH3Tcfw+d8yxWpqO7iG8FAfk7zq9S1iTEnWWToEZCIvZGzpHGvoHDVp3h/U/UegCD2a0Wu411RB9NKK996vjrs3n" +
            "IYX83a78zYKrQTIeCN2XZSZg4WSZMPyYwJmbaflgqOhc9NJOYwHG25PKC6AAAAAWJLR0QAiAUdSAAAAAd0SU1FB+oJGAkdCWrXYHIAAANaSURB" +
            "VEjHjZaHV9RAEMZvdjcBC4qop5jcRVQUsRALCip2RUSxF1TA3jv23gvYC/Y6/6jf5DzUbMDLuxfe7f6+mdmZuVlSqcSHlFLaOK4iwh+tSFGqv0" +
            "cpcg1EpItcir70K8GeBlE8YOCgwSVDhpYOKyPl6r4V2HFdGj5iJOef9KjRpMVVnzyVj/GY/XQmnc2mMwE0YyvIJCvAGxo3nr0g6PUQBD5PqBRF" +
            "QlRif+IkDjyPg6rJ1VOmTptew6HnszcjUYHz0sxZ7AOfXSsxIEdz5tax54VeJWxZQWG/fp7w8xeQ1o4xKIamhoWytKiCtLJOYGgx4uElSwllg1" +
            "wq6JC7jBHVcnLjAqWpYgV7vLKRHKlYvorKWcVeyE3kxgWGVnMQcrPw1OsWwdeu4YDXxgWy1YKNdWTUXwnBskPrsV5TEcsSLG1gRNQMAf1jSNPG" +
            "NPu8KZ5WQ5thaMvWeFFxchrKad4WEyiHtnOGW5G++I6hHdjZaS/vwnIpIkpZptrgYYktaIegI1GwG4I9yhLMhmBvomAfBAvtM+yPHKvYGaQDOm" +
            "BqrZ2lA0jewUPxpoEBMximDsfroOnIUeT1mHTyPw5cakKT8/G4B6S7FYI9Reg59RePiE5g/WS5JTB0in2fF1P9H4W0PJ3mMODJZOICRWfOojfC" +
            "Tqr/PSfwRi5qJ2G1rox0TCDBwlbINZ0YRloCw0AzdO68OLhgp5vE+3L2wzCcW4TQXWNEUz1S+Iuuis+NaGRcusyhZOTK1TJxQdeu38CPJ+Cbc5" +
            "BEZfMTb4HHwX0MsJax7e23D6Lh/YDvHLFmQMTfnYXTRZ9sfjD5WT/ke/eFJ4t/8BCVhotH8/Dy0plMJo3vXDI1mpVx3qXGx8L73EVnuketyY/W" +
            "qidPyZrGEf+sRfiA96MKGN/P2x51vOjqbiD0nkriX77K8a9lBhi3t3Davh5kVb25CBiftxQBcgXJ5DNJF5Dw9T05/h3mVpQ/Ipl7eKesB7zW7T" +
            "l+GfV32+TjQQuV5vgeo//Hwz66+n2OH/TBqn+CA9j/KLzPVeVWPZMEmj7l+M/3C+Dll/8lV9+bjQXx0QQJwD8s6+PeswUYm1ku2VgYn0pp+rqF" +
            "+dv3Qnn8/0E/2n4WFxT/7zqgWyRXBfJR5zlOgfwvUf/fhVv48TEAAAAldEVYdGRhdGU6Y3JlYXRlADIwMjYtMDktMjRUMDk6Mjg6NDQrMDA6MD" +
            "AcCKTbAAAAJXRFWHRkYXRlOm1vZGlmeQAyMDI2LTA5LTI0VDA5OjI4OjQ0KzAwOjAwbVUcZwAAAABJRU5ErkJggolQTkcNChoKAAAADUlIRFIA" +
            "AABAAAAAQAgDAAAAnbeB7AAAAARnQU1BAACxjwv8YQUAAAAgY0hSTQAAeiYAAICEAAD6AAAAgOgAAHUwAADqYAAAOpgAABdwnLpRPAAAAklQTF" +
            "RFAAAAFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFH" +
            "PEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPEFHPE" +
            "FHPEFHPEAAAA+Pz6IwAAAMF0Uk5TAAABAgMEBRQrP0EyGggjaqvZ8v314rl9M0u4+f7PZws6x/z74VwGjhjG6Dwg2uCxhX+AgaXU90nxlTgHJ3" +
            "r4Q8OaGQl08+wm7SXOxD7eCrV2wuso7h1OTR/nibSXXeUS7x5plFenqjsP5N/X3agpFrJHYkJ5fDGERhv007cq1up7NA1uynUiy6bcmDZfmRcu" +
            "WjdKyFUMrHJzZtifcZDmgvqi6Zx4cIsQqbpjfmtbjEWkLL/NERzSE/A1zD3V9lSjh+RUZgMAAAABYktHRACIBR1IAAAAB3RJTUUH6gkYCR0Jat" +
            "dgcgAABMdJREFUWMOdV4lfFVUUfufOvaMRJDqSoGPLPEQNtXIlUdSwqNTcUkIs08rIAiPKhDTIStsw280206hsX8z25fxnnnPuPAq5923392OY" +
            "9+Z93z33nO8sk8kUXABKqUAbowO6ASiMGINWoAJj6JrJEFqHmm6L52C40XQNJ0y8rOLyyiq6DUzRFMBbBgqumFQ9eUqEiFNrrpxWq4umkO0hqJ" +
            "s+A3nFkfzDmVddTbRFMLC1Gq65ljBJEseWJMnSTf0sfqJUQbxWDbMRo8QiaSVsRJTEOL0STAEGxsOcuciYOLH78+JboryukRmgAH7efMwyhv4W" +
            "XH/DjQsXLV6StRQJLl2Wn4HOD5WTGU++a7ppeTP/mEhXrFyAERkVYcsqZsgTQNWwmvFRjGtupo86pGUorq1rb2GbIry1jTbxMSiK32zB423LKO" +
            "5aNEwXTbK6/Q5mSHCd4W89BmhYj+wu3LACwgByP2RphHDnRmbI4ibvIZh5M6Fj3LIVwjGyY3Gq5tXy8K5tEHgINGxHjh+2Q3iJryU+d3fQswR3" +
            "gHESUP5BJxEkuHMcXhhCuEdMuHcXBE6CANpm8P737SYbYTw/bbBONpgFxnkCA3voaYL3u73EOfaAEDzoIdCwV/T3EDnMFSfyZNfDSAm2r8oZRg" +
            "XmEVbQo1WehGETunmLuMdJEMD+JsogfMxjAG0RwloiiLDX+VzDxJiFsoM0kHETGKgTJzzuIWAZZnEhxctNQM7tI4IsPuH8gYEKeVoEwZMeCw4g" +
            "H+Ep7xGI4KAcYY/Hif0DnG7dHqWKDyaJE9s9YWyt50LytK96cxjXiFJq3QQaDknKH/ZkGwnJPMNKGexyExhYKVIeojM4pazJzVwtDoH2WPAsl3" +
            "LccASUM5kMVMsOz0HoPqKC58XJLzjSmZUOjVLjB4660zmXjohTesYVb25YquGY8L9IWncTKDjyEh0hwmPUB8cwMD6El4U+fsVHwGF6lWtagp3N" +
            "/y+KdAPGwCJblocJ7xMK1ZyNlmFuP2iu3wTluk7wcNg2hprjrnqV20jDa1nphTh4gt0eGlnE87q0BdLZG3w6Hz4I9F6UlkpyefOtt21lIf83vh" +
            "MJPsF3/c1RBoPFmA4UPFh0dJ/srejre2/4lP1M/Xkai8yHJyW9L405befRaHfnreXLD8AOXm68gQ9TfGQpaL6IaNGV4RF+dDAvPoSPUzye/uSM" +
            "TDj/GcABODuhAP5T0QmHYAS2fXZGTEnSCQendh/IMyIJfmQUv4lb8+cnvqhJUgu2nBvZTS7yDmmklDBNA7RFkcZUcnbV4d4vh776ur22iyWiwe" +
            "t/7pvfcOgF/62cFHKTbjrrBt6xwvbd7VGc4s9yX1EyVNBkYmjC4XE7z2Ql+O9aMMV/b/HFL8GvH8jhq1VQzDw7Bm9geZMVcBY7TTn4HwZz+B9b" +
            "oQz80XqLT/Cn85cUoiLwGn6ebwWQ4IX9JeJ5iIXjS3L4U/2l4oEO/MtMi49w37wCk7jDAAXNm20CRtjRUzKe+8AQnrb4X38rHc8W/C4BiHHgjz" +
            "LwnMQXUN4Clq5yNbNijrATW5IsJnVl4XkmmfMn7f/X34wvJYFGjxDA+ZP//NtW8F3Ms6STgJTzsvCWgRpXUC4+k77jQ6nv9rQuAhYF3d9/19EN" +
            "AAAAJXRFWHRkYXRlOmNyZWF0ZQAyMDI2LTA5LTI0VDA5OjI4OjQ0KzAwOjAwHAik2wAAACV0RVh0ZGF0ZTptb2RpZnkAMjAyNi0wOS0yNFQwOT" +
            "oyODo0NCswMDowMG1VHGcAAAAASUVORK5CYIKJUE5HDQoaCgAAAA1JSERSAAABAAAAAQAIBgAAAFxyqGYAAAAEZ0FNQQAAsY8L/GEFAAAAIGNI" +
            "Uk0AAHomAACAhAAA+gAAAIDoAAB1MAAA6mAAADqYAAAXcJy6UTwAAAAGYktHRAAAAAAAAPlDu38AAAAHdElNRQfqCRgJHQlq12ByAAAjKUlEQV" +
            "R42u3deZgU1b3/8XdV9TZ798ywDLIKERdQMO6iqBgRVFyCmuiwCK0mrjHL1TyRxBuSG80Tf0bc46CC7c11uSYa9xuimHgT9YmguCEgaxhgprtr" +
            "mLVnurp+f9SM4ZoZZoaZU9U98309j49/MMw51dX15VTVOZ8DQgghhBBCCCGEEEIIIYQQQgghhBBCCCGEEEKIHKR53QEx+BiR0eALEJn/2GnA6H" +
            "3+GwlUAOVAGMgDDMACmoA6oBbYBewAtgFb2///dnzZWc12WzNge32IOUMKgFBCLxyC3dZM2XWvTAamAtOBWTgXuGq7gJeA1cB7wEe1d8+wSae8" +
            "/liyjhQA0Xe6gZ5fSunVvwsBJwHnAlcChV53bR/1QBXwArAmfs9ZSbutBeyM1/3ylBQAcUC0/FLsVAPl3/nTROB84HvAUK/71Qs1wK+A5+IPzF" +
            "lvpxrAavW6T66TAiB6TC8ow25roez6Vw8H5gM3e92nfnQ78DjwSe2vz7AHSzGQAiD2Swvko4WKKL3yv4cClwLLvO6TC24Anog/cF7Cbq4b0LcJ" +
            "UgBEp/Ti4ZRe+YwGHAvchvMAb7B5uf3Y361ddqZNW4vX/el3UgDEP2k6RmQ0kStifpwHeSuAIq+7lQXqgQXAC/F7z26zUw1e96ffSAEQoBkY5W" +
            "OJzF8RxBnmr/C6S1lsAfBk/L7ZKbulnlyfcyAFYDDTNIzy8UTmP+YD5gK/9bpLOeSbwDPx+2an7Za9XvflgEkBGKSMyGgii/5TA04HXsOZcSd6" +
            "xwLOMmPRP1nJbditTV73p9ekAAwyWl4Yo2ws4UvvHY8z1D/Z6z4NAG8BC5KPXr7JqtsJVpvX/ekxKQCDhWbgH3kUJZcsCwL/BvzU6y4NQD8Gfp" +
            "l48PxUpjFBLjwfkAIwCOglFRjhgyiZ++vjgLe97s8gcLwZi76TTmwh218dSgEYyDQD/5hjKPn6nUGc99m3eN2lQeQO4CeJhy5MZRpqydbRgBSA" +
            "AUrLL8U/8iiKz1s6EXgXeZ/vhQbgmOTji9Zb8S1ZudZA97oDov/5Ko7AbtlL8XlLK4FPkYvfK4XAp5F5j1T6yg9GCxV73Z9/ISOAgUQzCHzlVI" +
            "rPWxoE7gcWed0l8YVHgGsSD1+cyuzdRbbcEkgBGCC0QAGBQ06naOYtFcA7OOk6IrvsAI5LrlxYbcU3Q8byuj9SAAYCvWgovqGHUHzB7VOANV73" +
            "R3RrqhmLrk3HN+N1SpE8A8hxRvnBYAQovuD2WcjFnyvWhCurzvaVjUMLFHjaEZn+mcN8IyZh7dlA2bUvzgee8bo/olcqQ0fO2dy6YfX7ttXm2U" +
            "hACkCO8o/+KlZ8K+U3/M/1wG+87o84IBeGjpyTbP3s9bdt2/Jk0pA8A8hB/rEnYJk7KF38X/+GM+EkF60C3gA+AN6J33v2LrutuUcPxvTCIQCU" +
            "Xv27I4GxwJHAacAMrw/qAN1sxqK/tBr2YDcmXG1YCkCOydGLfwfwIM5F/0HtsjObaEvRv6/CNPSiIZRe9ayBk2J0JnA1ufM2xJMiIAUgh/hHH0" +
            "O69nPKvv38DcDdXvenG8/i3Jqsjt8zs8VZKuvuu2+jdAyRK57Qga8BVwEXef2hdOMGMxa9x6rfjd2UdKVBKQA5wjdiEunqjyn/7pvzyd7Enr8A" +
            "PwNei98z07ZbG73uzxeMsnFYia2Uf/fNmcCtwDSv+9SFBWYsutKqq8ZuqVPemBSAHGCUHwxWmsii/zwbJ6gy2ywBlicePL8602xmxQSXrmiBAr" +
            "T8MKWLn6wAFgNLve5TJ2absejLVnI7qouoFIAsl+WTfOYDT8fvm92Sc/l4moZvyCGE5y334cR7rfS6S1/iymQhKQBZTAsUEDpyDgXTr60Adnrd" +
            "n31cDvxX/L7ZmVzOw+vgqzicdM0mym9cdRnwhNf92ccIMxatTtdsUDaqkgKQrTSD/BMXkn/iFUFgI9nxNPv7wP3xB+Y0Ow+pcuhf/O7oPnzDJh" +
            "K+7KE84BqcbcO8tgOYkFy5MGXVbELF5y1TgbNU8PCZWPV7wFnV5/XF/xwwIrH8G3fW3jW92W7KjbirXsmkSVd/RPyemc11T3/nTmAE8LzHvRoJ" +
            "3K/pPvTi4UoakBFAFvJVHEF693rKb3qjEme/Oi+dbcair7rxQCqbGGXjwM4QueKJmcArHndnnhmLxqy6nfT3LZcUgCyj5ZcSGHssRbOWTMQJ8/" +
            "DKs0A0sfzSZKauekDvj9clX5DA2OMpPv8/wsByvJ1HcKgZi65P137er8lCUgCyiWaQf8rV5B97WRBn+2qvknzmA7H4fbPtgfCQr698FZNI7/6E" +
            "8ptWz8O7twX1wJDk44tS1p4N9NctmDwDyCKBCafQtvltgH/Hu4t/shmLPl579wy5+Nulqz9Ez4+w9w9LHgcme9SNIuA2TdPRC8v77ZfKCCBLZE" +
            "F091pgRvKxeQkrsXVwDvm74w8RnDiDopk/LAX+CEz1oBf9GjkuBSAbaAYFp19P3tS5QcCLIPkYsChRdUlbpi6bphtkIU0jeNhMimbd6gMeBSo9" +
            "6EXIjEVT6d3r6eutgNwCZIHAuBNoWfciODv2uO0OYH7ioQvl4u8J2yb18Svsff5HaZxnJV6syPwBgF5Q2udfJCMAj2l5YQITplF01i3jcSb8uG" +
            "mJGYv+LNNQQ6Yx7vVHkXP8B59EZu8uIgtW/ghnEZSbJpix6KZ07aY+7UUoIwCPhSafS9FZt2i4/3RZLv4+avv8fzGKK0iuXPBznAVRbloRrqzS" +
            "jJIRffolUgA8ZJSOoXXL2+Bs0X2Si03fYcaiP8s0xuXi76PWz9/CCI+i7qkbf4a7twMnA6dr/jy0QP4B/xIpAF7RNEJTv05k3qM+4DUXW44BP8" +
            "y07CXTUOP1pzAgtG54A71kOMAPcXfm5mvhyiqfERl9wL9ACoBHfMMOpfHNBwDm4l446xpgUfKxSlse+PWv1Ecv0/jnB22cjIG1LjVr4Hx/Dnjb" +
            "MXkI6AXvXvuVmbFoIr3nM3nPr4I/RP4xl5F/0qJSwM17qwN+LSgjAA/4Kg6j8fVlAJe62OxkMxZNpOOb5eJXpa2FlnXPs/cPSxK4O2PwUjiwUY" +
            "AUALdpOsHDZlL+3Tf9uJftN9+MRT+06nZ6vhXVQJdpqCVTX0PtXdM/xJkn4IYV4coqv1FS0eu/KAXAZcaQCTT99VGAc11q8lkglknV9/tSUtG5" +
            "dPWHhCbPAeeB67MuNXsugBYs7NVfkmcALis44ybypn5dA+pwZ8FPxIxFTbnvd5kvRP5xl5N/4hURwI2g/3qgJPn4Itva81mP/5KMAFykl1TQuu" +
            "nP4Gxc4cbFf7YZi5pWcrtc/G5Lt5Ba/yeSj16eBM52ocUi4FhN08Ef6vFfkgLgosD4UwiMOxHgNheae96MRV/NpBoGVZJPNrHim/GPOYamv614" +
            "FSdWTbXb4J9bp/WEFACXaIF8NF+QvK9eOhSY5UKT3wLI7K32+tAHtdRHr2CnGgC+7UJzs8KVVUP1QAHoPZtaIgXAJb7hh9Gy7g/gzqu/75uxaL" +
            "VVvyerN+kYDOzWRtK1m0g8PLcaJ1VZtV69EpQC4JLgxBmUXfOCDixzobn7Adf2lxP717btPfKOvhjaz4tiy8KVVbpRNKxHPywFwAV6QRmt294D" +
            "ONSF5i43Y9Fmq24nAy66O1dl0qTWr6L27hnNOJuqqOZ8z4xAtz8oBcAFvhGTsZviAPNcaO5pQN75Z5n0rk8pmH4dtJ8fxeYD6Hkl3f6gFADlNP" +
            "xjjqXkknsM4BbFjc03Y9E2Sxb6ZB87Q+rjV6m9a3ob6mcI3hyurDL0grJuf1AKgGJafoT0zg8AJrjQ3DMAzkadItukd39KwanXQPt5Usz5vnVz" +
            "GyAFQDHf0ENo2/kRwPmKm1pixqLNzhp/uffPSpk0bdveI7lyQTPqE4TOB9BC+58aLAVAscC44ym55B5Q/wpoOUCm2fT6kMV+tO1YS2DMcdB+vh" +
            "T6Htjo+fu/DZACoJIvhJ1qwCgaEgF6Pj2r994yY9Fqu61Z3vtnObu1EXSDlo9frQb+orCpoeHK5RFNN0Dr+jKXAqCQUTyM1MY3Qf0GEkvBWYoq" +
            "sl/rprdofnslqE8Sngqg7WdtgBQAhYwhEzrmZate+vtnALu1yetDFj1gJbcTmnwetJ83hbpdIiwFQCH/yCkUzf4JQFRhM8+asWhTJlWPPPzLEZ" +
            "k0Vt1Omv73kSbU5gVcCTZaXrjLH5ACoIrhx25rRg8WFKJ26e9vAOwm0+sjFr3Qtn0NqfWroP38KVIYrlxeqGk6aJ1Hf0gBUETPKyG96xOAsYqb" +
            "ehvAbmv2+pBFL1h1OwkePhPgb4qbGgd0OR9ACoAievFwWjesBjhaYTPbzdhi086kJfAj16RTaEaAdGJbHbBDYUvOg0Bf5w8CpQAoYkRGE5x0Ds" +
            "B0hc38BjRZ9Zej2ravof6FJQAPKWxmOtDl7kFSABTxDRlP/nHzQG34xx8BMpL4k5PS8c3ooRJoP4+KzAIbLVjQ6R9KAVBB08AXxAiP8AG9z2ru" +
            "ufUApFu9PmJxADKNCfyjpkLHeVSjIly53Kfpvk7/UAqACkawY1iuOvjTBOT+P1elWzrmiZiKW3K+h53MCJQCoIAWyMcyd4La6b+rzFjUlsk/uc" +
            "1KbKX5/edsYJXCZpzvYSejACkACujBQqz4ZoC+bd6+f28Akvib46zENlrefxbaz6ciBwFohhQAV2h5xaSdzRkOfN/m7n0AYMtWXznNqt/Tkd/w" +
            "gcJmRgGg+//lD6QAKKCHSjDKxwOMUdjMFgDbavP6cEUf2M0mvmGHQfv5VGQ0dD4C8PX6V4luaXnF+PLDoHYEUANAJu314Yo+sFubMMIjoON8qu" +
            "F8D41/HQFIAVBACxZhFA+D9nsvRZzcL3kDkNNsqxUtPwId51ONkUCnBUBuARTQg4UYpWNB7RwA5+bflhWAOS1jofnzoON8qlEBoMlbAHdogXx0" +
            "Z6/2coXNSPTPQGBn0JxtvFSez3KwO90uTAqACr4g7ZHM3QezHzj5p3+gcCboqDyf4X3a+T+kACigGT50Z+51fl9/lxD9IA9AkwLgkn9+0D3bov" +
            "UAmLHFMgIYKJywDpXns8vvoRQAFf75ZF7ZfV24crnW998isoLzIFfl+ezyeygFQAHbSncEdEpMj8gGTQB2J6+MpQCokE5hORHdpsJWZAQwUDgX" +
            "psrzWbdPO/+HFAAF7NYmMnXVACqD+pU9XxAu0nRsZzMXleezFrRON42RAqBAJtWAldgCUK2wmSDQZdqryBG60RHoGlTYSjWA3cm0cZkKrICdqi" +
            "ddUw9qwx6LgAY0HWyZE5SrNCPgRnjMPwDoZOGYjAAUsJv3kt6zEWC7wma6DHkQucOl8JhtgBQAt2Ra6rBqNwFsVdjMWACtkwUeIndoeWHSu5Xv" +
            "H7EVnLdTXyYFQAG7eS++oYdAR+VV40gAzafy1lGoZhQNRQsVQfv5VMT5HmZkBOCKTKoBo2wcwE6Fzew3713kBqN0NKEjLwC1+0fsBBkBuMalkI" +
            "czw5VVmhYo6PtvEp4xSseQN+VCDThTYTNdhsdIAVDBSrkR8gD7WeUlcoAvRKahBjrOozpdhsfIN0cF23ZmA5o706idCzARAF+gj79GeEEvKKVt" +
            "+xroOI9qVJuxxWm7i+g4KQCKpGs20fTO4wAvK2zmTABdbgNykq9sHJmWOlA7/H8ZNOxU5/HxUgAUsZLbSH34IsBqhc1cDXbH7YbIMb5RUyg656" +
            "cAVylsZjVAVxvISAFQJLN3F4GvTAd4T2EzI8OVy8Oa7pPnALnGF4R0K76yMWE6cvvVeA/ATrd0+ofyrVEk01yHb7jyvHeA44GOYEmRI4ySEaQ+" +
            "eQ3az59CWwCwOt9AVgqAKlYbmj+PTKqxAbVvA64C0Jx9CESO8I+aSnDiDFA7/K83Y4sbbDvTZXq0FACF2naspf6lfweoUtjMReHKqnw9WIREBO" +
            "QI3YdRMoL8kxblAxcpbKkKNOxms+uueP1ZDGRWzcaO97wvKG7qFJBZgbnCiIyiZd0foP28KfQCgJ1q6PIHpAAoZO3dTXDCqQBrFDd1K4BeqHIb" +
            "AtFfAuOnkXfCQoAliptaA2C3tXT5A1IAVEq3oAULsOr3JIE9CluaFq6sqtD8eZ1u/iCyhxYogEya0GFfqwBOVtjUHjO2OGlnrP1uHycFQLHWze" +
            "9Q99QNAHcqbmoxgJ4X9vqQxX74R06hdes70H6+FLoTNDJN8f3+kBQAxdI1G/BXHA7wvOKmloYrq/L0wiHIw8Aspfvwjz6ayPwVecBSxa09B2C3" +
            "NOz3h6QAKGY3JvAddBTABheamwt0rC8XWcY37FAa37wf2s+TYhuBLt//d5ACoJxN29Z3qXvqRgu4Q3FjK8OVVX6jZITXBy2+TNMJHj6T8ptW+4" +
            "GVilu7w4xFrUxjvNsflALggvTOdR3z9VWfeICLAbRQsdeHLfbhG34ojavvhfbzo9hKcGajdkcKgAsyjXECo48G+NSF5p4IV1blOaMAeRaQFXQf" +
            "wYkzKL9xVR7whAstOt+zbob/IAXANan1q4jff24GuMGF5q4BZJVglvCPPprm956G9vOi2A1mLJqx6nf36IelALgkvesTQpPPA3jSheZ+Fa6sqj" +
            "CKhsq8AI9pgQJ85eMpvfKZCuBXLjT5JIDdsrdHPywFwCV2axN2OkXz35/cg9qQkA4PAujFFV4f+qAWPGJWx/OYB11o7mUzFt2TaW3sdBuwzkgB" +
            "cFHrpj/TuvmvALe50NyccGXVTD1YiASHesMoG0fb1nfJP37eTGCOC03eBnSsP+kRKQAuytRVExh/CsC7qA8MBXglXFkVMSKjJDDEbb4QwYlnEL" +
            "niiQjwigst1gPv2nYG9jP3/8vkW+Gylg9fJP7AHBtY4FKTVeHKKk0vkVsBN4WOmEX+iVdoqF0Kvq8FZixqZ+p6txWFFACXWTUbyT9hAcCLLjV5" +
            "EVCpB4tkboBLfBWTaFn3PEAlatf77+tF2P/S385IAXCbnSH16f9Q+/9ObcW9UcDKcGXVJKNkhJNFJ5TRC8vRi4ZQftPqSbgz8Qucf/1brbreJ9" +
            "BLAfBAuvoTCk6/Adx5JdhhXbiyqtRXNk6eB6jiDxGaPIfi85aWAutcbLlXr/72Jd8EL9gWqY9fpXbZ11LAN11seVW4sspvlI7x+hMYeDSdvKlz" +
            "yT9pkR9Y5WLLl5mxaMqq2wnYvf7LUgA8kt79KQWnfhvgGaBnL237bgrwSGTh45ouC4b6VfCI2RSc8i0NeATnc3aDBTwNB/avP0gB8I5t07Lmv0" +
            "k+fkUaOMvFliuBX+ihYpzsANFXga+cRsa5//4FzufrlrPMWDRtJQ98F3opAB6yElsJjDke4HXgLRebvjlcWXWrXlCGXlDm9ceQ0wIHn4xlbqfk" +
            "krtvBW52sem3gNfttuYud/3pCZko7jEruQ0ruY3g+GmrcWehUIczQkfOsVKfrnoTbOy2Zq8/ipwTOPhkrPpdROavuBX1CT9fdoYZiyas5Lb9Zv" +
            "51R9aLZgHni7SHyPxHlwA/dbn5O4AfJh660O7NFNLBLvCV6RTP+bmGM+x3819+gB+bsejSTEMNPQn92B+5BcgCrZv/RmjyOQC/9KD5m4GVpVf/" +
            "zicPBntA0wkeNpPiOT/vSPZx++KH9u9JpjHR98PxoPOiE3pJBUb4IErm/vo44G0PurAGODP5WGXCSvRtWDlg+UMEJ86gaOYPS3Fe9U3xoBfHm7" +
            "HoO+nEll7N+e+KjACyRKauGi1QSN1TN76D+uzAzkwF4pGFsUm+oYfIjMEv0QvLCYw7kaKZP5wExPHm4r/DjEXfyTTG++XiBykAWaV145/xjzse" +
            "4Ce4s1qwM+vClVXzfGXjZO1AO1/F4ZRe/Xut+Lyl83B3ht++6oGf2HaGTENtv/1SuQXIMlp+KYGxx1I0a8lE3MkQ7MqzwOLE8kvNTF314Lwl8I" +
            "UIjDmG4gtuj+Cs6nNrYU9nDjNj0U/TtZ/3KOuvp2QEkGXspgRWcge1d522HpjnYVcuApKli5+c6Rt6yKALFTHKxmEUD6P4gttnAgm8vfjnmbHo" +
            "p1bdzn69+EFGAFkr/6RF+ComERh73FrgKI+78zzwrcTyb1Rn9lb3OG4qF2mBAnwVh1My967hwEO4k+SzP4+YsejiTHMdmb29X+3X7fF6fHCiE/" +
            "5xJ5Ax/0Fk0W9vBm73uj/7+D5wf/yBOc12U5IDWXyStXQfvmETCV/2UB5Oeq8bAZ7d2QFMSK5cmLJqNqHi85YCkGX8o44mXbORsmtfuha41+v+" +
            "dOFy4Lfx+2bbB7oIJZv4hh9G+PKHNZyVmW7k9vfUCDMWrU7XbFA26pICkEV8IyaT3rmO8u/9ZQHwmNf96YH5wJPx+2a35lwh0AyMsrFEFqwIAp" +
            "fgXnhHT001Y9G16fhmSKfUfQxeH6Vw+IZNxKqrpuzal+bSvsQzhywBlicePL8602xm9TMCLVCAlh+mdPGTFThbdLs9h78nZpmx6CtWcjt2a6Pa" +
            "z8PrIxVglI/HTrdQuvjJ2biXFajCW8DS5KOXv5ppqFX+5e0No2wskYUxDWfp9RLgZK/71IUFZiy60qqrxm7pfm+/vpIC4DEjMhotkE+4suo0nG" +
            "XBA8WzwG+AP8bvmWm5Xgw0Hb14OKXRpwLA6cBVePsqryduNGPRZVb9bpyHrOpJAfCQXjICo6SCkovvPh74m9f9UWg77cUA+HvtsjPb+msq6xd0" +
            "Ay1YRNk1L4RwpumeiXPRj/L64HvoZjMW/aXVsAe7Hxb59JQUAI/oRUPxDZtI8fm/OApY63V/PLAKeAP4APhT/N6zG+zWRrB78KrLH0IzApRd+1" +
            "IpMA04EpiOc9HnIk8ufpAC4AmtoIzA6K9SNPvHhwKfeN0f4SnXh/37kgLgMi2vhOBXTqPwaz8YB3zudX+Ep1x94NcZKQAu0oKFhCadS8Fp140A" +
            "/uF1f4SnXHvVtz9SAFyi+fMIHX0xBdOuGgLs8bo/wlOuTPLpCZ/Xn8Sg4At2XPxhYKPX3RGe2QEcl1y5sNqKb86KCVOyHFg1w0/e1K9TMO2qQp" +
            "zYLUnZGJweAcYnHr642qrZmBUXP8gIQC3dIDTlIgpOvSYEvAmM9bpLwhPzzFg0ZtXtPOAdfFSRAqCKphM66kIKT7veD7yEk7knBpd64Njk44vW" +
            "W/Et/R7m0R/kFkAJjdDk8yg84zsG8BTOVFQxuNwBDEk8dOF6a8+GrLz4QUYASgSPOJvCr/1AB5YDF3jdH+G6fo3uVklGAP0sMHEGRWf/SAPuAh" +
            "Z43Z8uePvuaeD6MRBKPHj+O+nd67P+4gcZAfSrwIRTad9eaynu7vPXW6Nw3kasIHuXxeaSt4AFyUcv3+QEd7Z53Z8ek4lA/SSLc/y+7CAzFt1p" +
            "W22gaUTmP3YG8BqyUeyBsHDyBV6P33OW3Zdder0iBaAf5EiOH8DBZiy6OV27yflXStMwyscTmf+YD5gL/NbrDuaQy4Cn4/fNTmfbq73ekALQRz" +
            "mU49f1xhKagVE+lsj8FUHgUpxbA9G5BTg5iCm7pZ5cT0aWAtAHOZTjN8WMRd/vdu65pmNERhO5IuYHzsUpBEVedz4L1ONc+C/G7z271U41eN2f" +
            "fiMF4ADlUI7fCWYs+nZvX0npxcMpvfIZDTgWuA2Y5fWBeODl9mN/t3bZmXYuPNXvLSkAByCHcvxON2PRN6zkNg74AZURwIiMIrJgRRGwEFjm9U" +
            "G54AagKv7Aec12k0muD/P3RwpAL+VQjt85Ziz6Un+uN9cLyrDbmim7/rXDcfYtvMXrg+xHdwArEw9d+HGmuS5rZ+71NykAvZBDOX4Xm7HoM5b5" +
            "D+yUml3GtfxS7FQD5d/500TgfOB7wFCvD7wX9gB3As/FH5iz3k41DJqLfl9SAHooh3L8Fpqx6ApXY6Z8QTTDT9l1r5QAX8V5gBglux4g1uNs8f" +
            "0C8E78nrMa7LbmnoWQDmBSAHogh3L8rjNj0fu8Cpjcl15Yjt3aTNn1r04CjsZJ7Z0FVLjQfDXOA7zVwHvx+2Z/aKdTnqfvZCMpAN3IoRy/W8xY" +
            "9A6roQa7Me51X7qmaehFwym98mkfTqT3aGBM+/8PwikQ5UAJkI8zQ9ECmoA6oBbnAt+Bs9/AVmAbsDq+7Czbbsu92XhekgKwHzmU4/dzMxa9Nd" +
            "MY71iLIESPyGrAruROjt8yYEmmKSkXv+g1KQCdyZ0cvxXATYmHL7Yz9bu97ovIQVIAvix3cvx+DyxOLL80k9lb7XVfRI6SArCv3Mnxex24JPnI" +
            "ZVbGzObnkiLbSQH4Qs7k+K0BZicfq2yzktu97ovIcVIA2uVIjt8W4NTkigUtVmIrA3mOunCHFAByJsdvLzA1+fiiBiu+edDPYBP9Y9AXgBzK8Z" +
            "tgxqKmVbMJ7IzXfREDxKCeCJRrOX7pmo2QSXvdFzGADNoC4B/1VdI1G3Ivx0+IfjQoC8CAyPEToh8MugLgG3YoVt3OgZPjJ0QfDKoCMNBz/ITo" +
            "rUHzFsCIjEYz/JQufvI0svviP92MRd+2ktvk4hfKDYoCoJeMQC8aQriy6niyO8TzHCfEc/uBh3gK0QsDvgDoRUPxDRlPycV3H0V2h3hebMaiL1" +
            "nmP/otxFOI7gzozUG1gjL8I6d05Pit9bo/+7HQjEWfseqqlYV4CtGZAVsAtLwSguOndeT4ZXOI53VmLLrCqt/tXoinEO0GZAHQgoWEDp/VkeOX" +
            "zSGet5ix6H1WQ43nIZ5icBpwBUDz5zuBHk6OXzYvlv+5GYvekWmMZ3eIpxjQBtZDQF+Q0NFzJcdPiB4aOAXA8JM3da7k+AnRCwOjAOgGoSlfp+" +
            "DUb0uOnxC9kPsF4Iscv+skx0+IXsrxAiA5fkL0RU4XAMnxE6JvcrYASI6fEH2XkwVAcvyE6B85lwcgOX5C9J+cKgCS4ydE/8qZAiA5fkL0v5wo" +
            "AJLjJ4QaWV8AJMdPCHWy+i2A5PgJoVbWFgDJ8RNCvawsAJLjJ4Q7si4QRHL8hHBPVhUAyfETwl1ZUwAkx08I92VFAZAcPyG84f1DQMnxE8Iz3h" +
            "YAw0/e0Zd05PitQ3L8hHCVdzMBdYO8YyspmHZlCPgIONjrD6MLvwfmJpZfKlFeYsDx7BlA/omL8I+aCvAe2XvxS46fGNA8uQUwysfT8tFL+A86" +
            "cjZwmNcfQhckx08MeK7fAmjBIifL7/QbRwLZemVtASYnVyxosOKfS5SXGLBcvwUITT4XPTwSsndNv+T4iUHD1VsAvWgoVnI7eUedPwWY4fXBd0" +
            "Fy/MSg4eoIIDB+GnpeGGCJ1wfehYPMWLQmXbMRbMvrvgihnHsFQPdhlIwg75hv5AMXeX3gnTjYjEV3pms3SYinGDRcuwXQC0pp27EW4BCvD7oT" +
            "hzkhnp9LiKcYVNwrAEXDaN30F4CjvD7oL5lixqKfpuObJcRTDDruFYD8CL4RkwAmeH3Q+zjBjEXfTye2SIinGJRcKwCaL4CeHwEo8vqg20mOnx" +
            "j0XCsAdrqVjLN+PhvicyTHTwhcLACZpiTpnR+C90t+JcdPiHbuFYD63QTGTwN438PjlRw/IfbhXgFoTOAfOQXgM4+O1cnx27tLcvyEaGe41pKd" +
            "wYiMJL3rkzb/qKlH4u4qwFvMWPQuq2GP5PgJsQ9X1wK0bvoL6T2fASx1sdn2HL9a7MaEm4crRNZztQBk6vdgREbR/P5za4FVLjS5T45frZuHKk" +
            "ROcD0QpGXdC1iJLQALFTf1NHBTouoSyfETogueZAK6sOPvX4FTko9eblmJrV4cohA5wZNIMKt2E3p+KbV3TnuJ/h8JVAHTEw/PlYtfiG54lwqM" +
            "kpHAOWYs+pIzw08m+QjRHfdeA3bCbkqCDW3b3tsQOnzmcpyVggeSELwKmJ585Jt/txLbZGGPED3k6QhgX3rRULBtSq/+3RScxKCehIY8CyxNLL" +
            "90baahVi58IXopawrAF3QfmhGg7IbX8nHCQ47CWUJchLOQaCPOdOLPau+e0US6FZDgTiGEEEIIIYQQQgghhBBCCCGEEEIIIYQQQgghhBBCCCGE" +
            "EAPY/wd7LdzYtVHcEwAAACV0RVh0ZGF0ZTpjcmVhdGUAMjAyNi0wOS0yNFQwOToyODo0NCswMDowMBwIpNsAAAAldEVYdGRhdGU6bW9kaWZ5AD" +
            "IwMjYtMDktMjRUMDk6Mjg6NDQrMDA6MDBtVRxnAAAAAElFTkSuQmCC";
    }

    internal sealed class SafeGdiRegion : SafeHandleZeroOrMinusOneIsInvalid
    {
        public SafeGdiRegion() : base(true) { }

        protected override bool ReleaseHandle()
        {
            try
            {
                return DeleteObject(handle);
            }
            catch (Exception)
            {
                return false;
            }
        }

        [DllImport("gdi32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DeleteObject(IntPtr ho);
    }
}
