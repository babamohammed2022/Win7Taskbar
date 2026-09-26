// Win7Taskbar - hover cascade of All Control Panel Items
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    internal sealed class ControlPanelCascadeWindow : Window
    {
        private const double RowHeight = 22;
        private const double ColumnWidth = 248;
        private const double IconBox = 16;

        private readonly List<ControlPanelItem> _items;
        private readonly Action _onLaunch;
        private readonly DispatcherTimer _leaveTimer;
        private readonly StartMenuWin32Tooltip _infotip = new();
        private DispatcherTimer? _tipTimer;
        private ControlPanelItem? _tipItem;
        internal bool PointerInside { get; private set; }

        internal ControlPanelCascadeWindow(List<ControlPanelItem> items, Action onLaunch)
        {
            _items = items ?? new List<ControlPanelItem>();
            _onLaunch = onLaunch;
            _leaveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(280) };
            _leaveTimer.Tick += (_, _) =>
            {
                _leaveTimer.Stop();
                if (!PointerInside)
                {
                    Close();
                }
            };

            ShowActivated = false;
            ShowInTaskbar = false;
            WindowStyle = WindowStyle.None;
            ResizeMode = ResizeMode.NoResize;
            AllowsTransparency = false;
            Background = Brushes.White;
            Topmost = true;
            SnapsToDevicePixels = true;

            MouseEnter += (_, _) =>
            {
                PointerInside = true;
                _leaveTimer.Stop();
            };
            MouseLeave += (_, _) =>
            {
                PointerInside = false;
                _leaveTimer.Stop();
                _leaveTimer.Start();
            };
            Closed += (_, _) =>
            {
                _leaveTimer.Stop();
                CancelInfotip();
                try { _infotip.Dispose(); } catch (Exception) { }
                ControlPanelItems.Free(_items);
            };

            Content = BuildBody();
        }

        internal void PlaceNextTo(Rect hostScreen, Rect rowScreen)
        {
            double maxH = SystemParameters.WorkArea.Height - 16;
            if (maxH < RowHeight * 4)
            {
                maxH = RowHeight * 8;
            }
            int perCol = Math.Max(1, (int)Math.Floor(maxH / RowHeight));
            int cols = Math.Max(1, (int)Math.Ceiling(_items.Count / (double)perCol));
            Width = cols * ColumnWidth + 6;
            Height = Math.Min(maxH, Math.Min(_items.Count, perCol) * RowHeight + 6);

            double left = hostScreen.Right - 4;
            double top = rowScreen.Top - 2;
            Rect work = SystemParameters.WorkArea;
            if (left + Width > work.Right)
            {
                left = hostScreen.Left - Width + 4;
            }
            if (top + Height > work.Bottom)
            {
                top = Math.Max(work.Top, work.Bottom - Height);
            }
            if (top < work.Top)
            {
                top = work.Top;
            }
            Left = left;
            Top = top;
        }

        internal static bool ContainsScreenPoint(int x, int y)
        {
            foreach (Window w in Application.Current.Windows)
            {
                if (w is ControlPanelCascadeWindow)
                {
                    try
                    {
                        IntPtr hwnd = new WindowInteropHelper(w).Handle;
                        if (hwnd != IntPtr.Zero &&
                            NativeMethods.GetWindowRect(hwnd, out NativeMethods.RECT rc) &&
                            x >= rc.Left && x < rc.Right && y >= rc.Top && y < rc.Bottom)
                        {
                            return true;
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }
            return false;
        }

        internal void KeepAlive()
        {
            PointerInside = true;
            _leaveTimer.Stop();
        }

        internal void ScheduleClose()
        {
            PointerInside = false;
            _leaveTimer.Stop();
            _leaveTimer.Start();
        }

        private UIElement BuildBody()
        {
            double maxH = SystemParameters.WorkArea.Height - 16;
            int perCol = Math.Max(1, (int)Math.Floor(maxH / RowHeight));
            int cols = Math.Max(1, (int)Math.Ceiling(_items.Count / (double)perCol));

            var border = new Border
            {
                BorderBrush = new SolidColorBrush(Color.FromRgb(0xA0, 0xA0, 0xA0)),
                BorderThickness = new Thickness(1),
                Background = Brushes.White,
                Padding = new Thickness(2)
            };
            var grid = new Grid();
            for (int c = 0; c < cols; c++)
            {
                grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(ColumnWidth) });
            }
            for (int r = 0; r < Math.Min(_items.Count, perCol); r++)
            {
                grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(RowHeight) });
            }

            for (int i = 0; i < _items.Count; i++)
            {
                int col = i / perCol;
                int row = i % perCol;
                ControlPanelItem item = _items[i];
                var rowGrid = new Grid { Background = Brushes.Transparent, Cursor = Cursors.Hand };
                rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(22) });
                rowGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                var img = new Image
                {
                    Source = item.Icon,
                    Width = IconBox,
                    Height = IconBox,
                    Margin = new Thickness(3, 0, 0, 0),
                    VerticalAlignment = VerticalAlignment.Center,
                    HorizontalAlignment = HorizontalAlignment.Left
                };
                RenderOptions.SetBitmapScalingMode(img, BitmapScalingMode.HighQuality);
                var text = new TextBlock
                {
                    Text = item.Name,
                    FontSize = 12,
                    Foreground = new SolidColorBrush(Color.FromRgb(0x1A, 0x1A, 0x1A)),
                    VerticalAlignment = VerticalAlignment.Center,
                    TextTrimming = TextTrimming.CharacterEllipsis,
                    Margin = new Thickness(4, 0, 6, 0)
                };
                Grid.SetColumn(img, 0);
                Grid.SetColumn(text, 1);
                rowGrid.Children.Add(img);
                rowGrid.Children.Add(text);
                rowGrid.Background = Brushes.Transparent;
                ControlPanelItem captured = item;
                rowGrid.MouseEnter += (_, _) =>
                {
                    rowGrid.Background = new SolidColorBrush(Color.FromRgb(0xCE, 0xE4, 0xF7));
                    ScheduleInfotip(captured);
                };
                rowGrid.MouseLeave += (_, _) =>
                {
                    rowGrid.Background = Brushes.Transparent;
                    CancelInfotip();
                };
                void LaunchRow(MouseButtonEventArgs e)
                {
                    CancelInfotip();
                    ControlPanelItems.Launch(captured);
                    _onLaunch();
                    e.Handled = true;
                }
                rowGrid.PreviewMouseLeftButtonDown += (_, e) => LaunchRow(e);
                Grid.SetColumn(rowGrid, col);
                Grid.SetRow(rowGrid, row);
                grid.Children.Add(rowGrid);
            }

            border.Child = grid;
            return border;
        }

        private void ScheduleInfotip(ControlPanelItem item)
        {
            CancelInfotip();
            _tipItem = item;
            int delay = 500;
            try
            {
                delay = SystemParameters.MouseHoverTime;
                if (delay < 400)
                {
                    delay = 400;
                }
                if (delay > 800)
                {
                    delay = 800;
                }
            }
            catch (Exception)
            {
            }
            _tipTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(delay) };
            _tipTimer.Tick += OnInfotipDelay;
            _tipTimer.Start();
        }

        private void OnInfotipDelay(object? sender, EventArgs e)
        {
            if (_tipTimer != null)
            {
                _tipTimer.Stop();
                _tipTimer.Tick -= OnInfotipDelay;
                _tipTimer = null;
            }
            ControlPanelItem? item = _tipItem;
            if (item == null)
            {
                return;
            }
            string? tip = null;
            try
            {
                tip = ControlPanelItems.GetInfoTip(item);
            }
            catch (Exception)
            {
            }
            if (string.IsNullOrWhiteSpace(tip))
            {
                return;
            }
            try
            {
                IntPtr owner = new WindowInteropHelper(this).Handle;
                if (owner == IntPtr.Zero)
                {
                    return;
                }
                int x = 0, y = 0;
                if (NativeMethods.GetCursorPos(out NativeMethods.POINT cursor))
                {
                    x = cursor.x;
                    y = cursor.y;
                }
                _infotip.Show(owner, item.Name, tip, x, y);
            }
            catch (Exception)
            {
            }
        }

        private void CancelInfotip()
        {
            if (_tipTimer != null)
            {
                try
                {
                    _tipTimer.Stop();
                    _tipTimer.Tick -= OnInfotipDelay;
                }
                catch (Exception)
                {
                }
                _tipTimer = null;
            }
            _tipItem = null;
            try { _infotip.Hide(); } catch (Exception) { }
        }
    }
}
