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
            IntPtr chrome = IntPtr.Zero, photo = IntPtr.Zero;
            IntPtr windowRgn = IntPtr.Zero;
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

                chrome = CreateRectRgn(Dip(10), Dip(25), Dip(10 + 411), Dip(25 + 476));
                photo = CreateRectRgn(Dip(310), Dip(0), Dip(310 + 55), Dip(57));
                if (chrome == IntPtr.Zero || photo == IntPtr.Zero)
                {
                    return;
                }

                windowRgn = CreateRectRgn(0, 0, 0, 0);
                CombineRgn(windowRgn, chrome, photo, RGN_OR);
                if (SetWindowRgn(hwnd, windowRgn, true) != 0)
                {
                    windowRgn = IntPtr.Zero;
                }

                ApplySimulatedGlass();
            }
            catch (Exception)
            {
                /* glass opzionale: il frame disegnato resta valido */
            }
            finally
            {
                SafeDeleteGdi(ref chrome);
                SafeDeleteGdi(ref photo);
                SafeDeleteGdi(ref windowRgn);
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
        private const byte SimTopAlpha = 0xB4;
        private const byte SimBottomAlpha = 0x9C;

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

        private static void SafeDeleteGdi(ref IntPtr handle)
        {
            if (handle == IntPtr.Zero)
            {
                return;
            }
            try { DeleteObject(handle); } catch (Exception) { }
            handle = IntPtr.Zero;
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
