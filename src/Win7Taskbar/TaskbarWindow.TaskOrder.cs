// Win7Taskbar - reordering of the taskbar icons (drag & drop)
// Copyright (c) 2026 Win7Taskbar contributors
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
// v1.21.7 - Moving the icons of OUR taskbar.
//
// WHAT IT TOUCHES (SPOILER: ONLY US)
//
//   the pressed button            -> mouse capture on the element
//   the group model               -> Groups.Move (TaskbarViewModel)
//   the program configuration     -> settings.json (list of keys)
//   the Windows taskbar           -> NOTHING. Not a read, not a write:
//                                    no Explorer pin, no registry key, no
//                                    shortcut of the real taskbar.
//
// The mechanism is the one the notification area already uses to move its
// icons (proven in the field, so no second system is invented here): mouse
// capture on the element, system drag threshold, ghost icon following the
// cursor, manual hit-testing and an insertion caret drawn at the edge of the
// target button. No OLE loop and no WPF DragDrop: the drag stays on the UI
// thread exactly like the tray one.
//
// WHY NOT A WPF DRAGDROP
//
// The Superbar buttons already have DragOver/Drop for FILES dragged from File
// Explorer (opening the file with that application). A second, OLE-based drag
// on the same surface would mix the two gestures up: reordering takes the
// LEFT click with mouse capture - like the tray - and leaves the file paths
// untouched.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using Win7Taskbar.Interop;
using Win7Taskbar.Models;

namespace Win7Taskbar
{
    public partial class TaskbarWindow
    {
        // ---------------------------------------------------------------
        //  Drag state
        // ---------------------------------------------------------------

        private Point _orderDragStart;                       // press point (DIP)
        private TaskGroup? _orderDragCandidate;              // item being dragged
        private FrameworkElement? _orderDragElement;         // captured container
        private bool _orderDragging;                         // threshold passed
        private bool _orderDragEnding;                       // re-entrancy guard
        private int _orderDropSlot = -1;                     // insertion slot
        private Window? _orderDragGhost;                     // icon following the cursor
        private Window? _orderDragHint;                      // localized instruction
        private bool _orderDragLogged;                       // one log line per gesture
        private bool _taskOrderConsumedClick;                // a reorder release is not a click
        private Window? _orderDragCaret;                     // insertion caret

        /// <summary>
        /// v1.21.7: true while a button is being dragged. It silences previews
        /// and tooltips (exactly like the Jump List gesture): during a reorder
        /// no popup may appear over the bar, and the release must not activate
        /// the application.
        /// </summary>
        internal bool IsTaskOrderDragActive() => _orderDragging;

        /* Insertion caret: the same blue as the tray icon drag, so the two
         * gestures look alike. */
        private Brush? _taskOrderDropBrush;
        private Brush OrderDropBrush =>
            _taskOrderDropBrush ??
            (_taskOrderDropBrush = new SolidColorBrush(Color.FromRgb(0x33, 0x99, 0xFF)));

        // ---------------------------------------------------------------
        //  Arming (called from the button PreviewMouseDown)
        // ---------------------------------------------------------------

        /// <summary>
        /// v1.21.7: arms the possible drag. Nothing happens until the pointer
        /// moves past the system threshold: a normal click stays a normal
        /// click (activation, menu, window picker), because the capture
        /// without movement is released at once and the button event goes on
        /// as always.
        /// </summary>
        private void BeginPotentialTaskOrderDrag(FrameworkElement element, MouseButtonEventArgs e)
        {
            if (!TaskOrderDragEnabled)
            {
                return;
            }

            // One gesture at a time: if the Jump List (when it is re-armed) or
            // another drag owns the pointer, this one does not start.
            if (_orderDragging || _orderDragElement != null || IsJumpListGestureActive())
            {
                return;
            }

            if (element.DataContext is not TaskGroup)
            {
                return;
            }

            try
            {
                _orderDragStart = e.GetPosition(this);
                _orderDragCandidate = (TaskGroup)element.DataContext;
                _orderDragElement = element;
                _orderDragLogged = false;
                _orderDropSlot = -1;
                // A new press is a new click: whatever the previous gesture
                // consumed does not carry over.
                _taskOrderConsumedClick = false;

                element.CaptureMouse();
                PreviewMouseMove += TaskOrder_CapturedMouseMove;
                PreviewMouseLeftButtonUp += TaskOrder_CapturedMouseUp;
                element.LostMouseCapture += TaskOrder_LostCapture;
            }
            catch (Exception ex)
            {
                _bridge.Log($"ordine icone: cattura non riuscita: {ex.Message}");
                ResetTaskOrderDragState();
            }
        }

        /// <summary>
        /// v1.21.7: single switch of the reorder gesture. Always on today: the
        /// bar is ours and the gesture has no effect outside the program. It
        /// stays a property so it can be turned off without touching code.
        /// </summary>
        private static bool TaskOrderDragEnabled => true;

        // ---------------------------------------------------------------
        //  Dragging
        // ---------------------------------------------------------------

        private void TaskOrder_CapturedMouseMove(object sender, MouseEventArgs e)
        {
            if (e.LeftButton != MouseButtonState.Pressed || _orderDragCandidate == null)
            {
                return;
            }

            if (!_orderDragging)
            {
                Vector moved = e.GetPosition(this) - _orderDragStart;
                if (Math.Abs(moved.X) < SystemParameters.MinimumHorizontalDragDistance &&
                    Math.Abs(moved.Y) < SystemParameters.MinimumVerticalDragDistance)
                {
                    return;
                }

                _orderDragging = true;
                CloseTaskPreview();          // no previews over a reorder
                CloseOpenButtonToolTip();
                ShowTaskOrderGhost();
                if (!_orderDragLogged)
                {
                    _orderDragLogged = true;
                    _bridge.Log("ordine icone: inizio trascinamento del pulsante");
                }
            }

            /* Direct conversion (not PointToScreenSafe: that helper keeps the
             * last point for the tray drag and must not be polluted with the
             * coordinates of another gesture). */
            Point screenPt = PointToScreen(e.GetPosition(this));
            MoveTaskOrderGhost(screenPt);
            UpdateTaskOrderHitTest(screenPt);
        }

        private void TaskOrder_CapturedMouseUp(object sender, MouseButtonEventArgs e)
        {
            EndTaskOrderDrag(commit: true);
        }

        /// <summary>The capture ended by itself (alt-tab, window deactivated):
        /// the drag is cancelled and the state is not left behind - otherwise
        /// the bar would never be reorderable again.</summary>
        private void TaskOrder_LostCapture(object sender, MouseEventArgs e)
        {
            EndTaskOrderDrag(commit: false);
        }

        private void EndTaskOrderDrag(bool commit)
        {
            if (_orderDragElement == null && _orderDragCandidate == null)
            {
                return;   // already finished
            }

            /* Releasing the capture raises LostMouseCapture, which calls this
             * function again: without the guard the second pass would clear the
             * state and the reorder would never be applied. */
            if (_orderDragEnding)
            {
                return;
            }
            _orderDragEnding = true;
            try
            {
                try
                {
                    _orderDragElement?.ReleaseMouseCapture();
                    ReleaseMouseCapture();
                }
                catch (Exception) { /* ignore */ }

                PreviewMouseMove -= TaskOrder_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= TaskOrder_CapturedMouseUp;
                if (_orderDragElement != null)
                {
                    _orderDragElement.LostMouseCapture -= TaskOrder_LostCapture;
                }

                bool wasDragging = _orderDragging;

                if (wasDragging)
                {
                    /* A real drag (not a click) consumes the release: moving an
                     * icon must not activate the application. */
                    _taskOrderConsumedClick = true;
                }

                if (commit && wasDragging)
                {
                    CommitTaskOrderDrag();
                }

                ResetTaskOrderDragState();
                HideTaskOrderGhost();
                ClearTaskOrderIndicator();
            }
            finally
            {
                _orderDragEnding = false;
            }
        }

        private void ResetTaskOrderDragState()
        {
            _orderDragging = false;
            _orderDragCandidate = null;
            _orderDragElement = null;
            _orderDropSlot = -1;
            _orderDragLogged = false;
        }

        // ---------------------------------------------------------------
        //  Insertion position and caret
        // ---------------------------------------------------------------

        /// <summary>
        /// v1.21.7: where the dragged button would land if it were released
        /// now. It looks at the button under the cursor and at its half:
        /// before the half the insertion is on the left, after it on the
        /// right, exactly like the real bar. The value is a "slot" (0..count)
        /// in the shown list.
        /// </summary>
        private void UpdateTaskOrderHitTest(Point screenPt)
        {
            HideTaskOrderCaret();

            List<FrameworkElement> buttons = EnumerateTaskButtonElements();
            if (buttons.Count == 0)
            {
                _orderDropSlot = -1;
                return;
            }

            for (int i = 0; i < buttons.Count; i++)
            {
                FrameworkElement element = buttons[i];
                if (!IsPointOverElement(element, screenPt))
                {
                    continue;
                }

                Point topLeft = element.PointToScreen(new Point(0, 0));

                // The candidate never draws a caret on itself.
                if (ReferenceEquals(element.DataContext, _orderDragCandidate))
                {
                    _orderDropSlot = -1;
                    return;
                }

                bool after = screenPt.X > topLeft.X + element.ActualWidth / 2;
                _orderDropSlot = after ? i + 1 : i;
                ShowTaskOrderCaret(element, after);
                return;
            }

            // Outside the buttons: inside the list it means "at the end",
            // outside the list the release reorders nothing.
            if (IsPointOverElement(TaskList, screenPt))
            {
                _orderDropSlot = buttons.Count;
                ShowTaskOrderCaret(buttons[buttons.Count - 1], after: true);
                return;
            }

            _orderDropSlot = -1;
        }

        /// <summary>
        /// Insertion indicator: a 2 px caret at the edge of the button under
        /// the cursor (left or right, according to the half).
        ///
        /// It is a small window - not a border of the button - because the
        /// theme draws the Superbar buttons with its own images and frames: a
        /// BorderThickness set from the outside may not be visible at all.
        /// This way the caret is always there, with any skin, and not one
        /// pixel of the button changes (no theme property is touched).
        /// </summary>
        private void ShowTaskOrderCaret(FrameworkElement element, bool after)
        {
            try
            {
                Point topLeft = element.PointToScreen(new Point(0, 0));
                double height = Math.Max(8, element.ActualHeight);

                if (_orderDragCaret == null)
                {
                    _orderDragCaret = new Window
                    {
                        WindowStyle = WindowStyle.None,
                        AllowsTransparency = true,
                        Background = null,
                        ShowInTaskbar = false,
                        ShowActivated = false,
                        Topmost = true,
                        Width = 2,
                        Height = height,
                        IsHitTestVisible = false,
                        Content = new Border
                        {
                            Background = OrderDropBrush,
                            CornerRadius = new CornerRadius(1),
                        },
                    };
                    _orderDragCaret.Show();
                }

                _orderDragCaret.Height = height;
                _orderDragCaret.Left = after
                    ? topLeft.X + element.ActualWidth - 2
                    : topLeft.X;
                _orderDragCaret.Top = topLeft.Y + (element.ActualHeight - height) / 2;
                ReassertTaskOrderGhostOnTop(_orderDragCaret);
            }
            catch (Exception)
            {
                // cosmetic: without the caret the reorder still works
                _orderDragCaret = null;
            }
        }

        /// <summary>Hides the caret without destroying its window: during a
        /// drag it is switched on and off at every position change.</summary>
        private void HideTaskOrderCaret()
        {
            try
            {
                if (_orderDragCaret != null && _orderDragCaret.IsVisible)
                {
                    _orderDragCaret.Hide();
                }
            }
            catch (Exception) { /* ignore */ }
        }

        private void ClearTaskOrderIndicator()
        {
            try { _orderDragCaret?.Close(); } catch (Exception) { /* ignore */ }
            _orderDragCaret = null;
        }

        /// <summary>
        /// Buttons of the list, in the shown order (it is the order of the
        /// groups, not of the windows). The container of an ItemsControl with
        /// a DataTemplate is a ContentPresenter: here we walk up to the real
        /// themed button, which is the element with the rectangle to use for
        /// hit-testing and for the caret.
        /// </summary>
        private List<FrameworkElement> EnumerateTaskButtonElements()
        {
            var elements = new List<FrameworkElement>();
            if (TaskList == null)
            {
                return elements;
            }

            for (int i = 0; i < TaskList.Items.Count; i++)
            {
                if (TaskList.ItemContainerGenerator.ContainerFromIndex(i)
                    is not FrameworkElement container)
                {
                    continue;
                }

                elements.Add(FindVisualButton(container) ?? container);
            }
            return elements;
        }

        private static FrameworkElement? FindVisualButton(DependencyObject root)
        {
            if (root is Button button)
            {
                return button;
            }

            int children = VisualTreeHelper.GetChildrenCount(root);
            for (int i = 0; i < children; i++)
            {
                FrameworkElement? found = FindVisualButton(VisualTreeHelper.GetChild(root, i));
                if (found != null)
                {
                    return found;
                }
            }
            return null;
        }

        // ---------------------------------------------------------------
        //  Applying the order
        // ---------------------------------------------------------------

        private void CommitTaskOrderDrag()
        {
            if (_orderDragCandidate == null || _viewModel == null)
            {
                return;
            }

            int from = _viewModel.Groups.IndexOf(_orderDragCandidate);
            int slot = _orderDropSlot;
            if (from < 0 || slot < 0)
            {
                // Released outside the buttons (or on the item itself):
                // no reorder, nothing written to the configuration.
                return;
            }

            // Explicit target list: the dragged item lands in the chosen
            // position, the others keep their relative order.
            var others = _viewModel.Groups
                .Where(g => !ReferenceEquals(g, _orderDragCandidate))
                .ToList();

            int insertAt = slot > from ? slot - 1 : slot;
            insertAt = Math.Max(0, Math.Min(others.Count, insertAt));
            others.Insert(insertAt, _orderDragCandidate);

            if (others.SequenceEqual(_viewModel.Groups))
            {
                // Left where it was: nothing to save.
                return;
            }

            bool saved = _viewModel.ApplyUserTaskbarOrder(others);
            _bridge.Log(saved
                ? $"ordine icone: spostato da {from} a {insertAt} ({others.Count} elementi)"
                : "ordine icone: spostamento non applicato");
        }

        // ---------------------------------------------------------------
        //  Ghost icon and instruction (visual only, never critical)
        // ---------------------------------------------------------------

        private void ShowTaskOrderGhost()
        {
            if (_orderDragGhost != null || _orderDragCandidate == null)
            {
                return;
            }

            try
            {
                _orderDragGhost = new Window
                {
                    WindowStyle = WindowStyle.None,
                    AllowsTransparency = true,
                    Background = null,
                    ShowInTaskbar = false,
                    ShowActivated = false,
                    Topmost = true,
                    Width = 36,
                    Height = 36,
                    IsHitTestVisible = false,
                    Content = new Image
                    {
                        Source = _orderDragCandidate.Icon,
                        Width = 32,
                        Height = 32,
                        Opacity = 0.85,
                        HorizontalAlignment = HorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center,
                    },
                };
                _orderDragGhost.Show();
                ShowTaskOrderHint();
            }
            catch (Exception ex)
            {
                // cosmetic: the ghost must never break the reorder
                _orderDragGhost = null;
                _bridge.Log($"ordine icone: fantasma non mostrato: {ex.Message}");
            }
        }

        /// <summary>
        /// v1.21.7: localized instruction next to the dragged icon ("drop to
        /// change the order"). The text comes from the Languages/*.xaml
        /// dictionaries: no string is written in code.
        /// </summary>
        private void ShowTaskOrderHint()
        {
            string text = L("lang_icon_order_drag_hint", "Drop to change the order");
            if (string.IsNullOrEmpty(text))
            {
                return;
            }

            try
            {
                _orderDragHint = new Window
                {
                    WindowStyle = WindowStyle.None,
                    AllowsTransparency = true,
                    Background = Brushes.Transparent,
                    ShowInTaskbar = false,
                    ShowActivated = false,
                    Topmost = true,
                    SizeToContent = SizeToContent.WidthAndHeight,
                    IsHitTestVisible = false,
                    Content = new Border
                    {
                        Background = new SolidColorBrush(Color.FromArgb(0xF0, 0xFF, 0xFF, 0xFF)),
                        BorderBrush = new SolidColorBrush(Color.FromRgb(0x86, 0x86, 0x86)),
                        BorderThickness = new Thickness(1),
                        CornerRadius = new CornerRadius(3),
                        Padding = new Thickness(8, 4, 8, 4),
                        Child = new TextBlock
                        {
                            Text = text,
                            FontSize = 12,
                            Foreground = new SolidColorBrush(Color.FromRgb(0x1A, 0x1A, 0x1A)),
                        },
                    },
                };
                _orderDragHint.Show();
            }
            catch (Exception)
            {
                _orderDragHint = null;
            }
        }

        private void MoveTaskOrderGhost(Point screenPt)
        {
            try
            {
                if (_orderDragGhost != null)
                {
                    _orderDragGhost.Left = screenPt.X + 10;
                    _orderDragGhost.Top = screenPt.Y + 10;
                    ReassertTaskOrderGhostOnTop(_orderDragGhost);
                }

                if (_orderDragHint != null)
                {
                    _orderDragHint.Left = screenPt.X + 10;
                    _orderDragHint.Top = screenPt.Y + 50;
                    ReassertTaskOrderGhostOnTop(_orderDragHint);
                }
            }
            catch (Exception) { /* cosmetic */ }
        }

        /* Same precaution as the tray ghost: the native panels (overflow,
         * flyout) are Topmost and may open during the drag; bringing the
         * window back to the front of the topmost band without moving or
         * resizing it keeps the dragged icon visible. */
        private static void ReassertTaskOrderGhostOnTop(Window window)
        {
            try
            {
                IntPtr hwnd = new WindowInteropHelper(window).Handle;
                if (hwnd != IntPtr.Zero)
                {
                    NativeMethods.SetWindowPos(hwnd, NativeMethods.HWND_TOPMOST,
                                               0, 0, 0, 0,
                                               NativeMethods.SWP_NOMOVE |
                                               NativeMethods.SWP_NOSIZE |
                                               NativeMethods.SWP_NOACTIVATE);
                }
            }
            catch (Exception) { /* cosmetic */ }
        }

        private void HideTaskOrderGhost()
        {
            try { _orderDragGhost?.Close(); } catch (Exception) { /* ignore */ }
            _orderDragGhost = null;

            try { _orderDragHint?.Close(); } catch (Exception) { /* ignore */ }
            _orderDragHint = null;
        }

        /// <summary>Closes the on-screen text tooltip, if any (v2.53 keeps it
        /// in <c>_openButtonTip</c>): during a reorder no box may stay hanging
        /// over the bar.</summary>
        private void CloseOpenButtonToolTip()
        {
            try
            {
                if (_openButtonTip is { IsOpen: true })
                {
                    _openButtonTip.IsOpen = false;
                }
            }
            catch (Exception) { /* ignore */ }
        }
    }
}
