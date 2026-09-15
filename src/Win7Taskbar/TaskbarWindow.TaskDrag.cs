// Win7Taskbar - Task button drag-and-drop reorder subsystem
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Allows the user to reorder pinned and running task buttons by pressing
// the left mouse button on a task button and dragging it horizontally.
//
// IMPORTANT: this subsystem does NOT use Mouse.Capture(). Capturing the
// mouse in PreviewMouseDown prevents the WPF Button from receiving its
// own MouseDown/MouseUp and generating the Click event — which breaks
// normal task button clicks (programs no longer open). Instead, the
// drag is tracked entirely via PreviewMouseMove / PreviewMouseLeftButtonUp
// on the window (tunneling events that fire regardless of capture).
// The Button keeps its own capture and Click works normally. If a drag
// crosses the threshold, the subsequent Click is suppressed by the
// _taskDragSuppressClick flag checked in TaskButton_Click.
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
        /// detection. No mouse capture — the WPF Button must keep its
        /// own capture so Click fires normally.</summary>
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
            _taskDragActive = false;

            try
            {
                _taskDragStartScreen = element.PointToScreen(
                    e.GetPosition(element));
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKDRAG", ex,
                    "start position");
                ClearTaskDragState();
                return;
            }

            // Attach handlers on the window (tunneling events). No mouse
            // capture: the Button needs its own capture for Click to work.
            try
            {
                PreviewMouseMove -= TaskDrag_PreviewMouseMove;
                PreviewMouseLeftButtonUp -= TaskDrag_PreviewMouseUp;

                PreviewMouseMove += TaskDrag_PreviewMouseMove;
                PreviewMouseLeftButtonUp += TaskDrag_PreviewMouseUp;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKDRAG", ex, "attach");
                ClearTaskDragState();
            }
        }

        // ---------------------------------------------------------------
        //  Movement
        // ---------------------------------------------------------------

        private void TaskDrag_PreviewMouseMove(object sender, MouseEventArgs e)
        {
            if (_taskDragSource == null)
            {
                return;
            }

            if (e.LeftButton != MouseButtonState.Pressed)
            {
                // The button went up without our seeing it: clean up.
                CancelTaskDrag("left button no longer pressed");
                return;
            }

            try
            {
                Point screenNow;
                try
                {
                    screenNow = _taskDragSource.PointToScreen(
                        e.GetPosition(_taskDragSource));
                }
                catch (InvalidOperationException)
                {
                    CancelTaskDrag("source disconnected");
                    return;
                }

                if (!_taskDragActive)
                {
                    double dx = screenNow.X - _taskDragStartScreen.X;
                    double dy = screenNow.Y - _taskDragStartScreen.Y;
                    double threshold = SystemParameters.MinimumHorizontalDragDistance;
                    if (dx * dx + dy * dy < threshold * threshold)
                    {
                        return;
                    }

                    _taskDragActive = true;
                    DiagnosticLogger.Write("TASKDRAG", "drag threshold reached");

                    try { CloseTaskPreview(); }
                    catch (Exception ex)
                    {
                        DiagnosticLogger.WriteException("TASKDRAG", ex,
                            "close preview on drag start");
                    }
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

        private void TaskDrag_PreviewMouseUp(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left || _taskDragSource == null)
            {
                return;
            }

            bool wasActive = _taskDragActive;

            if (wasActive)
            {
                // A consumed drag: suppress the Click that the Button
                // will fire after this event. The flag is checked in
                // TaskButton_Click.
                _taskDragSuppressClick = true;
                try
                {
                    Dispatcher.BeginInvoke(
                        System.Windows.Threading.DispatcherPriority.Input,
                        new Action(() =>
                        {
                            if (!_shuttingDown)
                            {
                                _taskDragSuppressClick = false;
                            }
                        }));
                }
                catch (Exception ex)
                {
                    DiagnosticLogger.WriteException("TASKDRAG", ex,
                        "suppress-click deferred clear");
                    _taskDragSuppressClick = false;
                }
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

        private void ReorderTaskButtonsAtCursor(Point screenPoint)
        {
            if (_taskDragGroup == null || TaskList == null || _viewModel == null)
            {
                return;
            }

            var groups = _viewModel.Groups;
            int from = groups.IndexOf(_taskDragGroup);
            if (from < 0)
            {
                return;
            }

            int insertAt = groups.Count;
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

            int to = insertAt > from ? insertAt - 1 : insertAt;
            if (to != from && to >= 0 && to < groups.Count)
            {
                try
                {
                    _viewModel.ReorderGroups(from, to);
                }
                catch (Exception ex)
                {
                    DiagnosticLogger.WriteException("TASKDRAG", ex,
                        $"reorder from={from} to={to}");
                }
            }
        }

        // ---------------------------------------------------------------
        //  Teardown
        // ---------------------------------------------------------------

        /// <summary>Detach handlers and clear state. No capture to
        /// release — we never captured.</summary>
        private void CancelTaskDrag(string reason)
        {
            if (_taskDragSource == null && !_taskDragActive)
            {
                return;
            }

            try
            {
                PreviewMouseMove -= TaskDrag_PreviewMouseMove;
                PreviewMouseLeftButtonUp -= TaskDrag_PreviewMouseUp;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKDRAG", ex, "detach");
            }
            finally
            {
                ClearTaskDragState();
            }
        }

        /// <summary>Clears all drag state.</summary>
        private void ClearTaskDragState()
        {
            _taskDragSource = null;
            _taskDragGroup = null;
            _taskDragActive = false;
        }
    }
}
