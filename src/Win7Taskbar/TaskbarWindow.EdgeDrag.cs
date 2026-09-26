// Win7Taskbar - unlocked taskbar: drag between the top and bottom edges
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Behavioural reference: RetroBar (Apache License 2.0),
// RetroBar/Taskbar.xaml.cs "Unlocked taskbar drag hook":
//   * a left button press on the bar itself (not on a button) while the
//     bar is unlocked starts a low-level mouse hook;
//   * every WM_MOUSEMOVE beyond the system drag threshold maps the cursor
//     to a screen edge by splitting the screen into quadrants and each
//     quadrant along its diagonal (DragCoordsToScreenEdge);
//   * when the edge differs from the current one the setting is changed
//     at once, so the bar follows the pointer during the drag;
//   * any button press or release stops the hook.
// Original C# code; only the algorithm is taken. Win7Taskbar limits the
// result to the two horizontal edges: a Left/Right outcome is ignored,
// the bar stays where it is (no vertical docking in this project).

using System;
using System.Diagnostics;
using System.Windows;
using Win7Taskbar.Interop;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    public partial class TaskbarWindow
    {
        private GlobalMouseHook? _edgeDragHook;
        private Point? _edgeDragStart;
        private NativeMethods.RECT _edgeDragScreen;

        /// <summary>
        /// Called from the bar's own MouseLeftButtonDown (bubbling: child
        /// controls that handled the click never get here). Starts the
        /// edge drag when the bar is unlocked.
        /// </summary>
        private void BeginEdgeDragIfUnlocked()
        {
            try
            {
                if (_taskbarLocked || _edgeDragHook != null)
                {
                    return;
                }
                if (!NativeMethods.GetCursorPos(out NativeMethods.POINT start))
                {
                    return;
                }
                _edgeDragScreen = EdgeDragScreenBounds(start);
                _edgeDragStart = new Point(start.x, start.y);

                _edgeDragHook = new GlobalMouseHook();
                _edgeDragHook.MouseMove += OnEdgeDragMove;
                _edgeDragHook.MouseButtonChanged += OnEdgeDragButton;
                _edgeDragHook.Start();
                _bridge.Log("taskbar edge drag: started");
            }
            catch (Exception ex)
            {
                _bridge.Log($"taskbar edge drag: start failed: {ex.Message}");
                EndEdgeDrag();
            }
        }

        private void EndEdgeDrag()
        {
            GlobalMouseHook? hook = _edgeDragHook;
            _edgeDragHook = null;
            _edgeDragStart = null;
            if (hook == null)
            {
                return;
            }
            try
            {
                hook.MouseMove -= OnEdgeDragMove;
                hook.MouseButtonChanged -= OnEdgeDragButton;
                hook.Dispose();
                _bridge.Log("taskbar edge drag: ended");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] taskbar edge drag: stop: {ex.Message}");
            }
        }

        private void OnEdgeDragButton(object? sender, Point pt)
        {
            EndEdgeDrag();
        }

        private void OnEdgeDragMove(object? sender, Point pt)
        {
            try
            {
                if (_edgeDragStart is not Point start)
                {
                    return;
                }
                /* The bar was locked meanwhile (context menu, Properties):
                 * stop without moving anything. */
                if (_taskbarLocked)
                {
                    EndEdgeDrag();
                    return;
                }
                if (Math.Abs(pt.X - start.X) <= SystemParameters.MinimumHorizontalDragDistance &&
                    Math.Abs(pt.Y - start.Y) <= SystemParameters.MinimumVerticalDragDistance)
                {
                    return;
                }

                int? position = EdgeDragPositionFromPoint(pt, _edgeDragScreen);
                if (position == null)
                {
                    return; /* Left/Right: not supported, keep the bar */
                }
                var st = RetroBar.Utilities.Settings.Instance;
                if (st.TaskbarPosition == position.Value)
                {
                    return;
                }
                /* Same path as the Properties dialog: the setting persists
                 * itself and ApplyTaskbarGeometry re-lays the window and
                 * the AppBar reservation. */
                st.TaskbarPosition = position.Value;
                ApplyTaskbarGeometry();
                _bridge.Log(position.Value == 1
                    ? "taskbar edge drag: moved to top"
                    : "taskbar edge drag: moved to bottom");
            }
            catch (Exception ex)
            {
                _bridge.Log($"taskbar edge drag: move failed: {ex.Message}");
                EndEdgeDrag();
            }
        }

        /// <summary>
        /// RetroBar's DragCoordsToScreenEdge, reduced to the two edges this
        /// project supports. The screen is split into four quadrants; each
        /// quadrant is split along its diagonal so the triangle touching the
        /// top or bottom edge selects that edge and the triangle touching a
        /// side edge selects Left/Right (returned as null here).
        /// Returns 1 for Top, 0 for Bottom, null for Left/Right.
        /// </summary>
        internal static int? EdgeDragPositionFromPoint(Point pt, NativeMethods.RECT screen)
        {
            double width = screen.Right - screen.Left;
            double height = screen.Bottom - screen.Top;
            if (width <= 0 || height <= 0)
            {
                return null;
            }
            double relativeX = (pt.X - screen.Left) / width;
            double relativeY = (pt.Y - screen.Top) / height;

            if (relativeX < 0.5 && relativeY < 0.5)
            {
                /* top-left quadrant */
                return relativeX >= relativeY ? 1 : null;
            }
            if (relativeX >= 0.5 && relativeY < 0.5)
            {
                /* top-right quadrant */
                relativeX -= 0.5;
                return relativeX + relativeY < 0.5 ? 1 : null;
            }
            if (relativeX < 0.5 && relativeY >= 0.5)
            {
                /* bottom-left quadrant */
                relativeY -= 0.5;
                return relativeX + relativeY < 0.5 ? null : 0;
            }
            /* bottom-right quadrant */
            return relativeX >= relativeY ? null : 0;
        }

        private static NativeMethods.RECT EdgeDragScreenBounds(NativeMethods.POINT near)
        {
            try
            {
                IntPtr monitor = NativeMethods.MonitorFromPoint(near, NativeMethods.MONITOR_DEFAULTTONEAREST);
                if (monitor != IntPtr.Zero)
                {
                    var info = new NativeMethods.MONITORINFO
                    {
                        cbSize = System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.MONITORINFO>()
                    };
                    if (NativeMethods.GetMonitorInfoW(monitor, ref info))
                    {
                        return info.rcMonitor;
                    }
                }
            }
            catch (Exception)
            {
            }
            return new NativeMethods.RECT
            {
                Left = 0,
                Top = 0,
                Right = (int)SystemParameters.PrimaryScreenWidth,
                Bottom = (int)SystemParameters.PrimaryScreenHeight
            };
        }
    }
}
