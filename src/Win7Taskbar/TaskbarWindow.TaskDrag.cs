// Win7Taskbar - Task button drag-and-drop reorder subsystem
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Allows the user to reorder pinned and running task buttons by pressing
// the left mouse button on a task button and dragging it horizontally.
// The mechanism mirrors the v3.8 thumbnail reorder pattern: mouse capture,
// system drag threshold, position-based insertion, click suppression after
// a consumed drag.
//
// This subsystem does NOT touch the notification area (system tray) or
// the tray overflow. It only operates on the task button strip (TaskList).

using System;
using System.Windows;
using System.Windows.Input;
using Win7Taskbar.Models;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    public partial class TaskbarWindow
    {
        // ---------------------------------------------------------------
        //  Drag state
        // ---------------------------------------------------------------

        /// <summary>The button element being dragged (null when idle).</summary>
        private FrameworkElement? _taskDragSource;

        /// <summary>The TaskGroup bound to the dragged button.</summary>
        private TaskGroup? _taskDragGroup;

        /// <summary>Screen position where the drag started (threshold only).</summary>
        private Point _taskDragStartScreen;

        /// <summary>True once the movement has crossed the drag threshold.</summary>
        private bool _taskDragActive;

        /// <summary>Set after a consumed drag to suppress the subsequent Click.</summary>
        private bool _taskDragSuppressClick;

        // ---------------------------------------------------------------
        //  Entry point (called from TaskButton_PreviewMouseDown)
        // ---------------------------------------------------------------

        /// <summary>Left button pressed on a task button: arm the drag
        /// detection. Nothing moves yet — the click still works normally
        /// if the user releases without dragging.</summary>
        private void BeginPotentialTaskDrag(FrameworkElement element,
                                             MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left)
            {
                return;
            }

            if (element.DataContext is not TaskGroup group)
            {
                return;
            }

            // A stale drag must never survive into a new press.
            CancelTaskDrag("new press");

            _taskDragSource = element;
            _taskDragGroup = group;
            _taskDragStartScreen = element.PointToScreen(e.GetPosition(element));
            _taskDragActive = false;

            // Attach capture handlers on the window. Each += is preceded
            // by -= so a missed teardown cannot stack duplicates.
            try
            {
                PreviewMouseMove -= TaskDrag_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= TaskDrag_CapturedMouseUp;

                element.CaptureMouse();
                PreviewMouseMove += TaskDrag_CapturedMouseMove;
                PreviewMouseLeftButtonUp += TaskDrag_CapturedMouseUp;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKDRAG", ex, "capture");
                CancelTaskDrag("capture failed");
            }
        }

        // ---------------------------------------------------------------
        //  Movement
        // ---------------------------------------------------------------

        private void TaskDrag_CapturedMouseMove(object sender, MouseEventArgs e)
        {
            if (_taskDragSource == null)
            {
                return;
            }

            if (e.LeftButton != MouseButtonState.Pressed)
            {
                // The button went up without a preview-up: end the drag.
                CancelTaskDrag("left button released elsewhere");
                return;
            }

            try
            {
                Point screenNow = _taskDragSource.PointToScreen(
                    e.GetPosition(_taskDragSource));

                if (!_taskDragActive)
                {
                    // Check whether the movement has crossed the system
                    // drag threshold (DPI-aware).
                    double dx = screenNow.X - _taskDragStartScreen.X;
                    double dy = screenNow.Y - _taskDragStartScreen.Y;
                    double threshold = SystemParameters.MinimumHorizontalDragDistance;
                    if (dx * dx + dy * dy < threshold * threshold)
                    {
                        return;
                    }

                    _taskDragActive = true;
                    DiagnosticLogger.Write("TASKDRAG", "drag threshold reached");

                    // Close any open preview popup — dragging with a tooltip
                    // or thumbnail on screen is not the Windows 7 way.
                    CloseTaskPreview();
                }

                ReorderTaskButtonsAtCursor(screenNow);
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKDRAG", ex, "mouse move");
                CancelTaskDrag("move error");
            }
        }

        // ---------------------------------------------------------------
        //  Release
        // ---------------------------------------------------------------

        private void TaskDrag_CapturedMouseUp(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left || _taskDragSource == null)
            {
                return;
            }

            bool wasActive = _taskDragActive;

            if (wasActive)
            {
                // A consumed drag: suppress the Click that the button
                // will fire on release, then clean up.
                _taskDragSuppressClick = true;
                Dispatcher.BeginInvoke(
                    System.Windows.Threading.DispatcherPriority.Input,
                    new Action(() =>
                    {
                        // Belt and braces: clear the flag at end of frame
                        // if no Click ever arrived.
                        _taskDragSuppressClick = false;
                    }));
                DiagnosticLogger.Write("TASKDRAG", "drag completed");
            }

            CancelTaskDrag(wasActive ? "drag released" : "click (no drag)");
        }

        // ---------------------------------------------------------------
        //  Click suppression (called from TaskButton_Click)
        // ---------------------------------------------------------------

        /// <summary>Returns true and clears the flag when the Click that
        /// just fired was the tail end of a consumed drag.</summary>
        private bool ShouldSuppressClickAfterTaskDrag()
        {
            if (!_taskDragSuppressClick)
            {
                return false;
            }
            _taskDragSuppressClick = false;
            return true;
        }

        // ---------------------------------------------------------------
        //  Reorder logic
        // ---------------------------------------------------------------

        /// <summary>Moves the dragged TaskGroup to the position indicated
        /// by the cursor. The insertion point is the first task button
        /// whose horizontal centre is to the right of the cursor.</summary>
        private void ReorderTaskButtonsAtCursor(Point screenPoint)
        {
            if (_taskDragGroup == null || TaskList == null)
            {
                return;
            }

            var groups = _viewModel.Groups;
            int from = groups.IndexOf(_taskDragGroup);
            if (from < 0)
            {
                return;
            }

            // Find the insertion index by scanning all other buttons.
            int insertAt = groups.Count; // after the last one
            for (int i = 0; i < groups.Count; i++)
            {
                if (ReferenceEquals(groups[i], _taskDragGroup))
                {
                    continue;
                }

                if (TaskList.ItemContainerGenerator
                        .ContainerFromItem(groups[i])
                        is not FrameworkElement container)
                {
                    continue;
                }

                try
                {
                    Point center = container.PointToScreen(new Point(
                        container.ActualWidth / 2.0,
                        container.ActualHeight / 2.0));

                    if (screenPoint.X < center.X)
                    {
                        insertAt = i;
                        break;
                    }
                }
                catch
                {
                    // Container in transition: skip.
                }
            }

            // ObservableCollection.Move wants the destination index in
            // the list AFTER the dragged element has been removed.
            int to = insertAt > from ? insertAt - 1 : insertAt;
            if (to != from && to >= 0 && to < groups.Count)
            {
                _viewModel.ReorderGroups(from, to);
            }
        }

        // ---------------------------------------------------------------
        //  Teardown
        // ---------------------------------------------------------------

        /// <summary>Ends the drag without activating anything: detach
        /// handlers, release capture, clear state.</summary>
        private void CancelTaskDrag(string reason)
        {
            if (_taskDragSource == null && !_taskDragActive)
            {
                return;
            }

            try
            {
                PreviewMouseMove -= TaskDrag_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= TaskDrag_CapturedMouseUp;

                if (_taskDragSource != null)
                {
                    if (_taskDragSource.IsMouseCaptured)
                    {
                        _taskDragSource.ReleaseMouseCapture();
                    }
                }
                else
                {
                    ReleaseMouseCapture();
                }
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKDRAG", ex,
                    "teardown (capture may need next press)");
            }
            finally
            {
                _taskDragSource = null;
                _taskDragGroup = null;
                _taskDragActive = false;
            }
        }
    }
}
