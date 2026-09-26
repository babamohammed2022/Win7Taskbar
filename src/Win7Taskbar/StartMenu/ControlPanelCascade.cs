// Win7Taskbar - Control Panel cascade (submenu) of the Start Menu right column
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Written from scratch in WPF. Layout, placement and metrics follow
// Open-Shell's submenu (Open-Shell-Menu, MIT):
//   * CMenuContainer::InitWindowInternal (MenuContainer.cpp) - multi-column
//     packing when "ScrollType" is NoScroll (the default): items are laid
//     top to bottom and a new column starts when the next item would cross
//     the maximum height; all columns share the widest column's width
//     ("SameSizeColumns" default). The column count is therefore a RESULT
//     of the available height, not a fixed threshold;
//   * CMenuContainer::ActivateItem (MenuCommands.cpp) - the submenu is
//     placed to the right of the parent item (left when it does not fit),
//     top-aligned with the item minus the submenu padding, bottom-aligned
//     when it would cross the bottom limit, clamped otherwise;
//   * Win7Aero7 skin (Src/Skins/Win7Aero7/SkinDescription.txt) and the
//     skin defaults in SkinManager.cpp - 2px submenu padding inside a 1px
//     frame, 22px rows (16px icon + 3px padding each side), text padding
//     {1,2,8,2}, arrow area 5+7+4px, 4px column gap, white background,
//     Segoe UI 9pt, light-blue rounded selection.
// No Open-Shell code or bitmaps are copied.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    /// <summary>
    /// The Windows 7 / Open-Shell style "Control Panel" submenu. Owned by
    /// the Start Menu window, never activated (WS_EX_NOACTIVATE), so the
    /// Start Menu keeps focus and its deactivate-to-dismiss logic stays
    /// untouched. The parent decides WHEN to open/close (hover timers);
    /// this class decides WHERE and HOW.
    /// </summary>
    internal sealed class ControlPanelCascade : Window
    {
        /* Metrics in DIP (96 DPI pixels of the Win7Aero7 skin). */
        private const double FrameDip = 1;
        private const double PadDip = 2;
        private const double ItemHeight = 22;
        private const double IconSize = 16;
        private const double IconPad = 3;
        private const double TextPadLeft = 1;
        private const double TextPadRight = 8;
        private const double ArrowArea = 5 + 7 + 4;
        private const double ColumnGap = 4;
        private const double FontSizeDip = 12; /* Segoe UI 9pt */

        private static readonly Brush TextBrush = new SolidColorBrush(Color.FromRgb(0, 0, 0));
        private static readonly Brush DisabledBrush = new SolidColorBrush(Color.FromRgb(0x7F, 0x7F, 0x7F));
        private static readonly Brush HoverBorder = new SolidColorBrush(Color.FromRgb(0xA8, 0xD8, 0xEB));
        private static readonly Brush HoverFill = new LinearGradientBrush(
            Color.FromRgb(0xE9, 0xF5, 0xFC), Color.FromRgb(0xC7, 0xE4, 0xF7), 90);
        private static readonly FontFamily MenuFont = new("Segoe UI");

        static ControlPanelCascade()
        {
            TextBrush.Freeze();
            DisabledBrush.Freeze();
            HoverBorder.Freeze();
            HoverFill.Freeze();
        }

        private readonly Border _root;
        private readonly StackPanel _columns;
        private readonly List<ControlPanelItem> _items = new();
        private readonly Dictionary<Border, ControlPanelItem> _rows = new();
        private Border? _hot;
        private int _generation;

        /// <summary>Raised when an applet must be launched (default verb).</summary>
        public event Action<ControlPanelItem>? ItemActivated;

        /// <summary>Raised on right click; the parent shows the shell menu.</summary>
        public event Action<ControlPanelItem, int, int>? ItemContextRequested;

        /// <summary>Raised when the pointer enters the cascade.</summary>
        public event Action? PointerEntered;

        public ControlPanelCascade(Window owner)
        {
            Owner = owner;
            WindowStyle = WindowStyle.None;
            ResizeMode = ResizeMode.NoResize;
            ShowInTaskbar = false;
            ShowActivated = false;
            Topmost = true;
            AllowsTransparency = false;
            SizeToContent = SizeToContent.Manual;
            Background = Brushes.White;
            UseLayoutRounding = true;
            SnapsToDevicePixels = true;
            TextOptions.SetTextFormattingMode(this, TextFormattingMode.Display);

            _columns = new StackPanel { Orientation = Orientation.Horizontal };
            _root = new Border
            {
                Background = Brushes.White,
                BorderBrush = new SolidColorBrush(Color.FromRgb(0x97, 0x97, 0x97)),
                BorderThickness = new Thickness(FrameDip),
                Padding = new Thickness(PadDip),
                Child = _columns
            };
            Content = _root;

            MouseEnter += (_, _) => PointerEntered?.Invoke();
        }

        /// <summary>True while the cascade is on screen.</summary>
        public bool IsOpen { get; private set; }

        protected override void OnSourceInitialized(EventArgs e)
        {
            base.OnSourceInitialized(e);
            try
            {
                IntPtr hwnd = new WindowInteropHelper(this).Handle;
                if (hwnd != IntPtr.Zero)
                {
                    IntPtr ex = NativeMethods.GetWindowLongPtr(hwnd, NativeMethods.GWL_EXSTYLE);
                    long next = ex.ToInt64()
                        | NativeMethods.WS_EX_TOOLWINDOW
                        | NativeMethods.WS_EX_NOACTIVATE;
                    NativeMethods.SetWindowLongPtr(hwnd, NativeMethods.GWL_EXSTYLE, new IntPtr(next));
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: ex-style: {ex.Message}");
            }
        }

        /// <summary>
        /// Lays the items out for the given limits and shows the window
        /// next to <paramref name="anchorPx"/> (screen pixels of the parent
        /// row). <paramref name="limitsPx"/> is the rectangle the menu may
        /// occupy (Open-Shell's s_MenuLimits). <paramref name="scale"/> is
        /// the DPI scale of the owner window.
        /// </summary>
        public void Open(IReadOnlyList<ControlPanelItem> items, NativeMethods.RECT anchorPx,
            NativeMethods.RECT limitsPx, double scale)
        {
            if (scale <= 0)
            {
                scale = 1.0;
            }
            int generation = ++_generation;
            _items.Clear();
            _items.AddRange(items);
            _rows.Clear();
            _hot = null;
            _columns.Children.Clear();

            double limitsWidth = (limitsPx.Right - limitsPx.Left) / scale;
            double limitsHeight = (limitsPx.Bottom - limitsPx.Top) / scale;
            double chrome = 2 * (FrameDip + PadDip);
            double maxHeight = Math.Max(ItemHeight, limitsHeight - chrome);

            /* ---- measure (InitWindowInternal: widest text decides) ---- */
            double textMax = 0;
            var labels = new List<string>(_items.Count);
            if (_items.Count == 0)
            {
                labels.Add(StartMenuViewModel.T("lang_sm_empty", "(Empty)"));
            }
            else
            {
                foreach (ControlPanelItem item in _items)
                {
                    labels.Add(item.Name);
                }
            }
            foreach (string label in labels)
            {
                textMax = Math.Max(textMax, MeasureText(label));
            }
            double itemWidth = IconPad + IconSize + IconPad + TextPadLeft
                + Math.Ceiling(textMax) + TextPadRight + ArrowArea;
            /* Auto width guard: never wider than the limits themselves. */
            itemWidth = Math.Min(itemWidth, Math.Max(80, limitsWidth - chrome));

            /* ---- pack columns greedily by height ---- */
            var columns = new List<List<int>> { new List<int>() };
            double y = 0;
            for (int i = 0; i < labels.Count; i++)
            {
                if (y > 0 && y + ItemHeight > maxHeight)
                {
                    columns.Add(new List<int>());
                    y = 0;
                }
                columns[^1].Add(i);
                y += ItemHeight;
            }
            int columnCount = columns.Count;
            double tallest = 0;
            foreach (List<int> column in columns)
            {
                tallest = Math.Max(tallest, column.Count * ItemHeight);
            }

            /* ---- build visuals ---- */
            for (int c = 0; c < columnCount; c++)
            {
                var panel = new StackPanel
                {
                    Orientation = Orientation.Vertical,
                    Width = itemWidth,
                    Margin = new Thickness(c == 0 ? 0 : ColumnGap, 0, 0, 0)
                };
                foreach (int index in columns[c])
                {
                    ControlPanelItem? item = _items.Count == 0 ? null : _items[index];
                    panel.Children.Add(BuildRow(item, labels[index], itemWidth));
                }
                _columns.Children.Add(panel);
            }

            double totalWidth = columnCount * itemWidth + (columnCount - 1) * ColumnGap + chrome;
            double totalHeight = Math.Min(tallest, maxHeight) + chrome;
            Width = totalWidth;
            Height = totalHeight;

            /* ---- place (ActivateItem) ---- */
            int widthPx = (int)Math.Ceiling(totalWidth * scale);
            int heightPx = (int)Math.Ceiling(totalHeight * scale);
            int inset = (int)Math.Round((FrameDip + PadDip) * scale);

            int x = anchorPx.Right - inset;
            if (x + widthPx > limitsPx.Right)
            {
                x = anchorPx.Left - widthPx + inset;
                if (x < limitsPx.Left)
                {
                    x = Math.Max(limitsPx.Left, limitsPx.Right - widthPx);
                }
            }
            int top = anchorPx.Top - inset;
            if (top + heightPx > limitsPx.Bottom)
            {
                top = anchorPx.Bottom - heightPx + inset;
                if (top < limitsPx.Top)
                {
                    top = Math.Max(limitsPx.Top, limitsPx.Bottom - heightPx);
                }
            }

            Left = x / scale;
            Top = top / scale;
            try
            {
                if (!IsVisible)
                {
                    Show();
                }
                IntPtr hwnd = new WindowInteropHelper(this).Handle;
                if (hwnd != IntPtr.Zero)
                {
                    NativeMethods.SetWindowPos(hwnd, NativeMethods.HWND_TOPMOST,
                        x, top, widthPx, heightPx,
                        NativeMethods.SWP_NOACTIVATE | NativeMethods.SWP_SHOWWINDOW);
                }
                IsOpen = true;
                AnimateIn();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: show failed: {ex.Message}");
                IsOpen = false;
                return;
            }

            LoadIconsAsync(generation, scale);
        }

        /// <summary>Hides the cascade (kept alive for reuse).</summary>
        public void Dismiss()
        {
            if (!IsOpen && !IsVisible)
            {
                return;
            }
            IsOpen = false;
            ++_generation;
            try
            {
                _root.BeginAnimation(UIElement.OpacityProperty, null);
                Hide();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: hide failed: {ex.Message}");
            }
        }

        /// <summary>Screen rectangle in pixels, empty when hidden.</summary>
        public bool TryGetScreenRect(out NativeMethods.RECT rect)
        {
            rect = default;
            try
            {
                IntPtr hwnd = new WindowInteropHelper(this).Handle;
                return hwnd != IntPtr.Zero && IsOpen && NativeMethods.GetWindowRect(hwnd, out rect);
            }
            catch (Exception)
            {
                return false;
            }
        }

        private Border BuildRow(ControlPanelItem? item, string label, double width)
        {
            var image = new Image
            {
                Width = IconSize,
                Height = IconSize,
                Margin = new Thickness(IconPad, 0, IconPad, 0),
                VerticalAlignment = VerticalAlignment.Center,
                SnapsToDevicePixels = true
            };
            RenderOptions.SetBitmapScalingMode(image, BitmapScalingMode.HighQuality);

            var text = new TextBlock
            {
                Text = label,
                FontFamily = MenuFont,
                FontSize = FontSizeDip,
                Foreground = item == null ? DisabledBrush : TextBrush,
                VerticalAlignment = VerticalAlignment.Center,
                Margin = new Thickness(TextPadLeft, 0, TextPadRight, 0),
                TextTrimming = TextTrimming.CharacterEllipsis
            };

            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(ArrowArea) });
            Grid.SetColumn(image, 0);
            Grid.SetColumn(text, 1);
            grid.Children.Add(image);
            grid.Children.Add(text);
            if (item is { IsFolder: true })
            {
                var arrow = new Path
                {
                    Data = Geometry.Parse("M0,0 L4,3.5 L0,7 Z"),
                    Fill = TextBrush,
                    Width = 4,
                    Height = 7,
                    HorizontalAlignment = HorizontalAlignment.Right,
                    VerticalAlignment = VerticalAlignment.Center,
                    Margin = new Thickness(0, 0, 7, 0)
                };
                Grid.SetColumn(arrow, 2);
                grid.Children.Add(arrow);
            }

            var row = new Border
            {
                Height = ItemHeight,
                Width = width,
                Background = Brushes.Transparent,
                BorderBrush = Brushes.Transparent,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(3),
                Child = grid,
                Tag = image
            };
            if (item != null)
            {
                _rows[row] = item;
                row.MouseEnter += OnRowEnter;
                row.MouseLeave += OnRowLeave;
                row.MouseLeftButtonUp += OnRowClick;
                row.MouseRightButtonUp += OnRowContext;
            }
            return row;
        }

        private void OnRowEnter(object sender, MouseEventArgs e)
        {
            if (sender is not Border row)
            {
                return;
            }
            SetHot(row);
        }

        private void OnRowLeave(object sender, MouseEventArgs e)
        {
            if (sender is Border row && ReferenceEquals(_hot, row))
            {
                SetHot(null);
            }
        }

        private void SetHot(Border? row)
        {
            if (_hot != null)
            {
                _hot.Background = Brushes.Transparent;
                _hot.BorderBrush = Brushes.Transparent;
            }
            _hot = row;
            if (_hot != null)
            {
                _hot.Background = HoverFill;
                _hot.BorderBrush = HoverBorder;
            }
        }

        private void OnRowClick(object sender, MouseButtonEventArgs e)
        {
            if (sender is Border row && _rows.TryGetValue(row, out ControlPanelItem? item))
            {
                e.Handled = true;
                ItemActivated?.Invoke(item);
            }
        }

        private void OnRowContext(object sender, MouseButtonEventArgs e)
        {
            if (sender is Border row && _rows.TryGetValue(row, out ControlPanelItem? item))
            {
                e.Handled = true;
                if (NativeMethods.GetCursorPos(out NativeMethods.POINT pt))
                {
                    ItemContextRequested?.Invoke(item, pt.x, pt.y);
                }
            }
        }

        private void AnimateIn()
        {
            try
            {
                if (SystemParameters.IsMenuFadeEnabled && SystemParameters.IsMenuAnimationEnabled)
                {
                    var fade = new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(120))
                    {
                        FillBehavior = FillBehavior.Stop
                    };
                    _root.Opacity = 1;
                    _root.BeginAnimation(UIElement.OpacityProperty, fade);
                }
                else
                {
                    _root.BeginAnimation(UIElement.OpacityProperty, null);
                    _root.Opacity = 1;
                }
            }
            catch (Exception)
            {
                _root.Opacity = 1;
            }
        }

        private void LoadIconsAsync(int generation, double scale)
        {
            var targets = new List<KeyValuePair<Image, string>>();
            foreach (KeyValuePair<Border, ControlPanelItem> pair in _rows)
            {
                if (pair.Key.Tag is Image image)
                {
                    targets.Add(new KeyValuePair<Image, string>(image, pair.Value.ParsingName));
                }
            }
            if (targets.Count == 0)
            {
                return;
            }
            int pixelSize = (int)Math.Round(IconSize * scale);
            if (pixelSize < 16)
            {
                pixelSize = 16;
            }
            _ = Task.Run(() =>
            {
                foreach (KeyValuePair<Image, string> target in targets)
                {
                    if (generation != _generation)
                    {
                        return;
                    }
                    ImageSource? icon = null;
                    try
                    {
                        icon = StartMenuIcons.FromParsingName(target.Value, pixelSize);
                    }
                    catch (Exception)
                    {
                        icon = null;
                    }
                    if (icon == null)
                    {
                        continue;
                    }
                    Image image = target.Key;
                    try
                    {
                        Dispatcher.BeginInvoke(new Action(() =>
                        {
                            if (generation == _generation)
                            {
                                image.Source = icon;
                            }
                        }));
                    }
                    catch (Exception)
                    {
                    }
                }
            });
        }

        private static double MeasureText(string text)
        {
            try
            {
                var formatted = new FormattedText(text, CultureInfo.CurrentUICulture,
                    FlowDirection.LeftToRight,
                    new Typeface(MenuFont, FontStyles.Normal, FontWeights.Normal, FontStretches.Normal),
                    FontSizeDip, TextBrush, 1.0);
                return formatted.WidthIncludingTrailingWhitespace;
            }
            catch (Exception)
            {
                return text.Length * 7.0;
            }
        }
    }
}
