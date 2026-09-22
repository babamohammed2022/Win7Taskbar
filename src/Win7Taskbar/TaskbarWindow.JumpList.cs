// Win7Taskbar - Windows 7 Jump List subsystem (drag-up trigger + arrow)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// The Windows 7 Superbar opens a taskbar button's Jump List when the user
// presses the button with the LEFT button and drags AWAY from the bar
// (up for a bottom bar) - the press + release still on the button is an
// ordinary activation, and a press + drag ALONG the bar is the icon
// reorder. This file is the whole managed side of both Jump List triggers;
// the popup is the native window of native/src/JumpListWindow.cpp.
//
// The right-click of a taskbar button is NEVER involved: it keeps the plain
// Windows 7 context menu handled in TaskbarWindow.xaml.cs.
//
// Trigger 1 - the Windows 7 drag (the main one, since v2.62):
//
//   TaskButton_PreviewMouseDown (TaskbarWindow.xaml.cs)
//        -> the press arms the jump-list candidate (and the reorder
//           candidate, as before): nothing happens yet, a plain click is
//           never touched.
//   TaskButton_PreviewMouseMove (TaskbarWindow.xaml.cs)
//        -> threshold arbitration: the first axis to cross its threshold
//           OWNS the press - movement away from the bar past
//           JumpDragAwayThreshold starts the jump-list drag, movement
//           along the bar past the system drag threshold starts the
//           reorder. A diagonal drag is decided by the dominant axis, so
//           the two gestures can never fight for the same press.
//   while the drag is active (the button holds the mouse capture)
//        -> every move calls the native DragMove: the popup re-anchors on
//           the cursor (the cursor-position rule of the GPL-3.0 Windhawk
//           mod "taskbar-jump-list-on-cursor-pos" by m417z, applied live)
//           and the row under the cursor is highlighted.
//   TaskButton_PreviewMouseLeftButtonUp
//        -> released ON A ROW -> the row activates and the list closes;
//           released over the list or the button -> the list stays open,
//           persistent, and takes ordinary input (row clicks, Escape,
//           click-outside) - the drag-up becomes a click list;
//           released outside the interaction area -> plain cancel.
//        The release ALWAYS consumes the click of the press that dragged.
//
// Trigger 2 - the hover arrow (the secondary affordance):
//
//   TaskButton_PreviewMouseDown
//        -> TryBeginJumpListArrowPress: the press is inside the arrow slot
//           of the hovered button -> the press is CONSUMED (e.Handled);
//   the arrow slot release opens the list directly, persistent, through
//   the same OpenJumpListPopup the drag uses.
//
// Every failure (Shell/COM, popup creation, marshal, a core whose dist/ DLL
// predates the interactive handoff or the drag exports) is logged through
// the project's DiagnosticLogger under the JUMPLIST tag and ends in a
// controlled state: capture released, hook stopped, popup hidden. A
// failure here can never take the taskbar down.
//
// Coordinate spaces (this is where the DPI bugs live, so it is spelled
// out): WPF element/window points are DEVICE-INDEPENDENT units.
// PointToScreenSafe (on a Per-Monitor-V2 process, see app.manifest)
// converts a window-DIP point to SCREEN PHYSICAL PIXELS - and only that;
// the native side receives physical pixels everywhere and scales its own
// geometry with the DPI of the monitor under the button. The button rect
// is therefore produced by converting BOTH corners through PointToScreen
// - sizes are never multiplied by a scale a second time.

using System;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Win7Taskbar.Converters;
using Win7Taskbar.Interop;
using Win7Taskbar.Models;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    public partial class TaskbarWindow
    {
        /// <summary>Name of the arrow element in TaskButtonContentTemplate
        /// (Themes/Overrides.xaml). The press detection hit-tests the
        /// pointer against the live layout slot of this element, so no
        /// coordinate of the arrow is duplicated in C#.</summary>
        private const string JumpListArrowName = "JumpListArrow";

        private FrameworkElement? _jumpButton;      // button the open list belongs to
        private TaskGroup? _jumpGroup;
        private FrameworkElement? _jumpArrowPress;  // button holding an un-released arrow press

        // v2.62: the Windows 7 drag trigger. The press of a task button is
        // armed as a candidate (alongside the reorder candidate, on the
        // SAME press); it becomes the real drag only when the pointer
        // moves away from the bar past the threshold, and only then the
        // button captures the mouse. _jumpButton/_jumpGroup are reused as
        // the anchor of the list the drag opens.
        private FrameworkElement? _jumpDragButton;  // armed candidate / active drag
        private Point _jumpDragPressPt;             // its press point (window DIPs)
        private bool _jumpDragActive;               // popup on screen, drag owns the pointer
        private bool _jumpDragExportsMissing;       // core without the v2.62 drag exports

        /// <summary>DIP movement away from the taskbar edge that opens the
        /// list. Above the system drag threshold (4 DIP) on purpose: a
        /// click that wobbles a few pixels must stay a click, and the
        /// along-bar reorder keeps the system threshold for its axis.</summary>
        private const double JumpDragAwayThreshold = 6.0;
        private GlobalMouseHook? _jumpDismissHook;  // click-outside on an older core
        private bool _jumpDismissArmed;             // the hook is installed
        private int _jumpOpenGen;                   // bumped by every open
        private int _jumpDismissGen;                // gen the armed hook belongs to
        private bool _jumpEnding;                   // re-entrancy guard (like tray drag)

        /// <summary>The dismissal message the native popup posts to itself
        /// when its low-level hook sees a click outside (WM_APP + 0x177):
        /// keep in sync with kDismissOutsideMessage in
        /// native/src/JumpListWindow.cpp.</summary>
        private const uint JumpDismissMessage = 0x8000 + 0x177;

        /// <summary>PM_REMOVE.</summary>
        private const uint PeekRemove = 1;

        /// <summary>True while the subsystem owns the pointer (an arrow
        /// press or a drag-up candidate not released yet) or its popup is
        /// on screen: the hover preview, the button tooltip and the icon
        /// reorder stay away while a jump list is open (two stacked
        /// flyovers are not the Windows 7 way). Existing call sites keep
        /// their meaning unchanged.</summary>
        private bool IsJumpListGestureActive()
        {
            if (_jumpArrowPress != null || _jumpDragButton != null ||
                _jumpDragActive)
            {
                return true;
            }
            bool up = IsJumpListUp();
            if (!up && _jumpButton != null)
            {
                // The popup went away inside the native core (row
                // activated, Escape, deactivation): drop the anchor so a
                // group that later leaves the bar is not kept alive by a
                // stale reference, and the next open starts clean.
                _jumpButton = null;
                _jumpGroup = null;
            }
            return up;
        }

        /// <summary>The native popup is on screen (probe in NativeBridge:
        /// window class + visibility + process id, no native export).</summary>
        private bool IsJumpListUp()
        {
            try
            {
                return _bridge.IsJumpListPopupVisible();
            }
            catch (Exception ex)
            {
                // A broken probe must read as "no list", never throw into
                // the hover/tooltip path that calls this on every enter.
                System.Diagnostics.Debug.WriteLine(
                    $"jump list visibility probe: {ex.Message}");
                return false;
            }
        }

        // ---------------------------------------------------------------
        //  The arrow trigger
        // ---------------------------------------------------------------

        /// <summary>Left button pressed on a task button: when the press
        /// lands on the hovered button's jump list arrow, consume it and
        /// arm the release. Returns true when the press was taken (the
        /// caller must stop: no activation, no reorder, no other gesture).
        /// Nothing opens here - Windows 7 opens the list on the click, and
        /// a click is a press AND a release on the same target.</summary>
        private bool TryBeginJumpListArrowPress(FrameworkElement element,
                                                MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left)
            {
                return false;
            }

            Point press;
            try
            {
                press = e.GetPosition(element);
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "arrow press position");
                return false;
            }

            // Cheap rejection before walking the visual tree: the arrow
            // lives in the last ~14 DIP of the button's right edge.
            if (element.ActualWidth <= 0 ||
                press.X < element.ActualWidth - 30)
            {
                return false;
            }

            if (!IsJumpListArrowHit(element, press))
            {
                return false;
            }

            if (element.DataContext is not TaskGroup)
            {
                return false;
            }

            e.Handled = true;

            // The arrow of the button whose list is already open closes it
            // (the Windows 7 list is a toggle of the same affordance).
            if (IsJumpListUp() && ReferenceEquals(_jumpButton, element))
            {
                HideJumpList("arrow pressed again on the same button");
                return true;
            }

            // A stale press on another button must not survive.
            if (_jumpArrowPress != null &&
                !ReferenceEquals(_jumpArrowPress, element))
            {
                EndJumpListArrowPress();
            }

            _jumpArrowPress = element;
            DiagnosticLogger.Write("JUMPLIST", "jump list arrow pressed");

            // Capture so the release comes back to this button even when
            // the cursor travels off it: the same mechanism the tray drag
            // and the old gesture used (manual hit-test, no hooks).
            try
            {
                element.LostMouseCapture -= JumpArrow_LostCapture;
                PreviewMouseLeftButtonUp -= JumpArrow_Release;

                element.CaptureMouse();
                PreviewMouseLeftButtonUp += JumpArrow_Release;
                element.LostMouseCapture += JumpArrow_LostCapture;
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "arrow capture");
                EndJumpListArrowPress();
            }
            return true;
        }

        /// <summary>The release that completes the arrow click opens the
        /// list; a release outside the arrow slot is a plain cancelled
        /// click (the press was consumed, nothing else fires).</summary>
        private void JumpArrow_Release(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left || _jumpArrowPress == null)
            {
                return;
            }

            FrameworkElement pressed = _jumpArrowPress;
            EndJumpListArrowPress();

            // The press was consumed: whatever this release is, it is not a
            // click on the button underneath.
            e.Handled = true;

            Point release;
            try
            {
                release = e.GetPosition(pressed);
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "arrow release position");
                return;
            }

            if (!IsJumpListArrowHit(pressed, release))
            {
                DiagnosticLogger.Write("JUMPLIST",
                    "arrow press released off the arrow - cancelled");
                return;
            }

            OpenJumpListForButton(pressed);
        }

        /// <summary>Capture ended by itself (alt-tab, a window taking
        /// focus...): the armed press cannot complete; leave a clean state
        /// behind.</summary>
        private void JumpArrow_LostCapture(object sender, MouseEventArgs e)
        {
            if (_jumpArrowPress != null)
            {
                DiagnosticLogger.Write("JUMPLIST",
                    "arrow press lost the mouse capture - cancelled");
                EndJumpListArrowPress();
            }
        }

        /// <summary>Detaches what TryBeginJumpListArrowPress wired. The
        /// capture release is unconditional: it is the one state a failed
        /// press must never leave behind.</summary>
        private void EndJumpListArrowPress()
        {
            FrameworkElement? pressed = _jumpArrowPress;
            _jumpArrowPress = null;
            try
            {
                PreviewMouseLeftButtonUp -= JumpArrow_Release;
                if (pressed != null)
                {
                    pressed.LostMouseCapture -= JumpArrow_LostCapture;
                    if (pressed.IsMouseCaptured)
                    {
                        pressed.ReleaseMouseCapture();
                    }
                }
            }
            catch (Exception ex)
            {
                // Tearing down must not throw into the input pipeline; a
                // capture that refuses to die is logged, not hidden.
                DiagnosticLogger.WriteException("JUMPLIST", ex,
                    "arrow capture teardown (the capture may need the next press)");
            }
        }

        // ---------------------------------------------------------------
        //  Arrow geometry: the live layout slot of the template element
        // ---------------------------------------------------------------

        /// <summary>The arrow element of a button, wherever the content
        /// template put it (a visual-tree walk by name: the template is
        /// shared by the four button states and re-created with them, so a
        /// cached reference would go stale).</summary>
        private static FrameworkElement? FindJumpListArrow(DependencyObject root)
        {
            if (root is FrameworkElement self && self.Name == JumpListArrowName)
            {
                return self;
            }

            int children = VisualTreeHelper.GetChildrenCount(root);
            for (int i = 0; i < children; i++)
            {
                FrameworkElement? found =
                    FindJumpListArrow(VisualTreeHelper.GetChild(root, i));
                if (found != null)
                {
                    return found;
                }
            }
            return null;
        }

        /// <summary>True when the point (in the button's own DIP space) is
        /// inside the arrow's current layout slot. TransformToDescendant
        /// follows whatever layout the template produced: no hard-coded
        /// coordinate, and a button that moved or resized since the last
        /// open is hit-tested where it is NOW.</summary>
        private static bool IsJumpListArrowHit(FrameworkElement button,
                                               Point pointInButton)
        {
            FrameworkElement? arrow = FindJumpListArrow(button);
            if (arrow == null ||
                arrow.Visibility != Visibility.Visible ||
                arrow.ActualWidth <= 0 || arrow.ActualHeight <= 0)
            {
                return false;
            }

            try
            {
                Point p = button.TransformToDescendant(arrow)
                                .Transform(pointInButton);
                return p.X >= 0 && p.Y >= 0 &&
                       p.X <= arrow.ActualWidth && p.Y <= arrow.ActualHeight;
            }
            catch (Exception ex)
            {
                // A disconnected visual tree (template swap mid-press) is a
                // "not on the arrow" answer, not an exception in the input
                // pipeline.
                System.Diagnostics.Debug.WriteLine(
                    $"jump list arrow hit-test: {ex.Message}");
                return false;
            }
        }

        // ---------------------------------------------------------------
        //  Opening - shared by the two triggers
        // ---------------------------------------------------------------

        /// <summary>The shared open: remembers the anchor, clears the
        /// other flyover of the same button (text tooltip and hover
        /// preview), then asks the native side for the popup. Returns
        /// false when nothing is on screen (failure or nothing worth
        /// showing, already logged at the source). The caller decides
        /// what happens next: the arrow hands over ordinary input at
        /// once, the drag keeps the pointer until the release.</summary>
        private bool OpenJumpListPopup(FrameworkElement element)
        {
            _jumpButton = element;
            _jumpGroup = element.DataContext as TaskGroup;
            if (_jumpGroup == null)
            {
                _jumpButton = null;
                return false;
            }

            // A new open supersedes every dismissal decision taken for the
            // list that was on screen a moment ago (see the purge below and
            // the generation check of the click-outside hook).
            _jumpOpenGen++;

            if (_openButtonTip is { IsOpen: true })
            {
                try { _openButtonTip.IsOpen = false; } catch { }
            }
            _previewShowTimer?.Stop();
            CloseTaskPreview();

            if (!OpenJumpList())
            {
                // Controlled failure (or no data worth showing): logged at
                // its source; nothing is left open and nothing is armed.
                _jumpButton = null;
                _jumpGroup = null;
                return false;
            }

            PurgeStaleJumpDismissals();
            return true;
        }

        /// <summary>Arrow trigger (trigger 2): opens the list of one
        /// button and hands the popup its ordinary input at once - the
        /// Windows 7 list persists until a row click, Escape or a click
        /// outside, it is never a drag modal.</summary>
        private void OpenJumpListForButton(FrameworkElement element)
        {
            if (!OpenJumpListPopup(element))
            {
                return;
            }
            HandOverJumpListInput();
        }

        // ---------------------------------------------------------------
        //  The drag trigger (trigger 1): press + drag away from the bar
        // ---------------------------------------------------------------

        /// <summary>Arms the jump-list candidate of one press (called
        /// from TaskButton_PreviewMouseDown for every left press on a
        /// task button, next to the reorder candidate - SAME press).
        /// Nothing happens until TaskButton_PreviewMouseMove sees the
        /// pointer move away from the bar past the threshold: a plain
        /// click is never consumed by this subsystem, and the reorder
        /// keeps the press when its axis crosses first.</summary>
        private void ArmJumpDragCandidate(FrameworkElement button,
                                          MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left)
            {
                return;
            }
            if (button.DataContext is not TaskGroup)
            {
                return;
            }
            // One gesture at a time: an active drag, an arrow press or a
            // reorder already owns the pointer.
            if (_jumpDragActive || _jumpArrowPress != null ||
                _reorderDragging != null)
            {
                return;
            }
            _jumpDragButton = button;
            _jumpDragPressPt = e.GetPosition(this);
        }

        /// <summary>Drops the armed candidate of a press that will not
        /// become a drag (plain click, the pointer left, the other
        /// gesture won the arbitration). Safe when nothing is armed: no
        /// capture exists yet (the capture is taken only when the
        /// threshold is crossed).</summary>
        private void CancelJumpDragCandidate()
        {
            _jumpDragButton = null;
        }

        /// <summary>The arbitration of the shared press, evaluated on
        /// every move: true when the movement away from the bar crossed
        /// the jump threshold and the movement along the bar did not win
        /// the drag. The away axis depends on the edge the bar sits on
        /// (up for the bottom bar, down for a top bar, right for a left
        /// bar, left for a right bar), so the gesture works however the
        /// taskbar is positioned. A diagonal drag is decided by the
        /// dominant axis: that is what keeps the jump list and the icon
        /// movement from ever fighting for one press.</summary>
        private bool JumpDragShouldTakeGesture(Vector delta)
        {
            int edge = GetThumbnailEdge(this);
            double away;
            double along;
            double alongThreshold;
            switch (edge)
            {
                case (int)TaskbarEdge.Top:
                    away = delta.Y;
                    along = delta.X;
                    alongThreshold =
                        SystemParameters.MinimumHorizontalDragDistance;
                    break;
                case (int)TaskbarEdge.Left:
                    away = delta.X;
                    along = delta.Y;
                    alongThreshold =
                        SystemParameters.MinimumVerticalDragDistance;
                    break;
                case (int)TaskbarEdge.Right:
                    away = -delta.X;
                    along = delta.Y;
                    alongThreshold =
                        SystemParameters.MinimumVerticalDragDistance;
                    break;
                case (int)TaskbarEdge.Bottom:
                default:
                    away = -delta.Y;
                    along = delta.X;
                    alongThreshold =
                        SystemParameters.MinimumHorizontalDragDistance;
                    break;
            }

            if (away < JumpDragAwayThreshold)
            {
                return false;
            }

            double alongDistance = Math.Abs(along);
            if (alongDistance >= alongThreshold && alongDistance > away)
            {
                return false;   // the icon reorder wins the diagonal
            }
            return true;
        }

        /// <summary>True while the along-bar movement is below the
        /// reorder threshold of the bar orientation: the press is not a
        /// reorder yet (and the jump list has not taken it either), so
        /// the next move decides - or the release ends it as a click.</summary>
        private bool ReorderBelowThreshold(Vector delta)
        {
            int edge = GetThumbnailEdge(this);
            if (edge == (int)TaskbarEdge.Left ||
                edge == (int)TaskbarEdge.Right)
            {
                return Math.Abs(delta.Y) <
                       SystemParameters.MinimumVerticalDragDistance;
            }
            return Math.Abs(delta.X) <
                   SystemParameters.MinimumHorizontalDragDistance;
        }

        /// <summary>The away threshold was crossed: this press is now the
        /// Windows 7 jump-list drag. The button captures the mouse (the
        /// moves and the release come back to it from anywhere on the
        /// screen), the popup opens and the first DragMove anchors it on
        /// the cursor. Any failure degrades the press to a plain click:
        /// the capture is released and the button keeps its ordinary
        /// activation on the release.</summary>
        private void BeginJumpDrag(FrameworkElement button,
                                   MouseButtonEventArgs e)
        {
            DiagnosticLogger.Write("JUMPLIST",
                "drag away from the bar crossed the threshold - opening" +
                " the jump list");

            try
            {
                button.LostMouseCapture -= JumpDrag_LostCapture;
                button.LostMouseCapture += JumpDrag_LostCapture;
                button.CaptureMouse();
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "drag capture");
                return;
            }

            if (!OpenJumpListPopup(button))
            {
                EndJumpDrag();
                return;
            }

            // The cursor-position anchoring (the GPL-3.0 Windhawk mod
            // "taskbar-jump-list-on-cursor-pos" rule): the popup centers
            // on the cursor along the bar axis right away, instead of
            // staying glued to the button's left edge like a plain menu.
            try
            {
                Point screen = PointToScreen(e.GetPosition(this));
                DragAt((int)Math.Round(screen.X), (int)Math.Round(screen.Y));
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "drag open");
            }

            _jumpDragActive = true;
        }

        /// <summary>One move of the active drag: the popup follows the
        /// cursor and the hover row updates (native DragMove; a core
        /// without the export keeps the open position and only updates
        /// the hover through SetHover).</summary>
        private void JumpDrag_OnMove(FrameworkElement button, MouseEventArgs e)
        {
            try
            {
                Point screen = PointToScreen(e.GetPosition(this));
                DragAt((int)Math.Round(screen.X), (int)Math.Round(screen.Y));
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "drag move");
            }
        }

        /// <summary>The drag move against the core, with the older-core
        /// fallback: without W7T_JumpListDrag the popup keeps the
        /// position it opened with and the hover row still updates.</summary>
        private bool DragAt(int screenX, int screenY)
        {
            if (_jumpDragExportsMissing)
            {
                return _bridge.JumpListSetHover(screenX, screenY);
            }
            try
            {
                return _bridge.JumpListDrag(screenX, screenY);
            }
            catch (EntryPointNotFoundException)
            {
                _jumpDragExportsMissing = true;
                return _bridge.JumpListSetHover(screenX, screenY);
            }
        }

        /// <summary>Row under the screen point: >=0 the row index, -1
        /// when the point is over empty popup space, -2 when the core
        /// has no W7T_JumpListHitRow export (the release then decides
        /// with the popup's rectangle).</summary>
        private int HitRowAtSafe(int screenX, int screenY)
        {
            if (_jumpDragExportsMissing)
            {
                return -2;
            }
            try
            {
                return _bridge.JumpListHitRow(screenX, screenY);
            }
            catch (EntryPointNotFoundException)
            {
                _jumpDragExportsMissing = true;
                return -2;
            }
        }

        /// <summary>The release that ends the drag (XAML-wired per
        /// button, tunneling - it runs before the button's own click
        /// handling, and e.Handled keeps that press from activating the
        /// window: the press became a gesture). Row under the cursor ->
        /// activate; over the list or the button -> the list persists
        /// with ordinary input (the drag-up became a click list);
        /// outside the interaction area -> plain cancel.</summary>
        private void JumpDrag_OnRelease(FrameworkElement button,
                                        MouseButtonEventArgs e)
        {
            EndJumpDrag();

            int x;
            int y;
            try
            {
                Point screen = PointToScreen(e.GetPosition(this));
                x = (int)Math.Round(screen.X);
                y = (int)Math.Round(screen.Y);
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "drag release");
                _bridge.JumpListHide();
                return;
            }

            int row = HitRowAtSafe(x, y);
            bool inside = DragAt(x, y);

            if (row == -2)
            {
                // A core without the hit-row export: the popup's rectangle
                // plays the hit-test (the native ActivateRow then hits the
                // row under the point and closes the list either way).
                bool overPopup = false;
                if (_bridge.TryGetJumpListPopupRect(out NativeMethods.RECT pr))
                {
                    overPopup = x >= pr.Left && x < pr.Right &&
                                y >= pr.Top && y < pr.Bottom;
                }
                if (overPopup)
                {
                    ActivateJumpListRowAt(x, y);
                }
                else if (inside)
                {
                    DiagnosticLogger.Write("JUMPLIST",
                        "drag released over the list/button - the list" +
                        " stays open");
                    HandOverJumpListInput();
                }
                else
                {
                    DiagnosticLogger.Write("JUMPLIST",
                        "drag released outside the interaction area -" +
                        " cancelled");
                    _bridge.JumpListHide();
                }
                return;
            }

            if (row >= 0)
            {
                ActivateJumpListRowAt(x, y);
                return;
            }

            if (inside)
            {
                DiagnosticLogger.Write("JUMPLIST",
                    "drag released over the list/button - the list" +
                    " stays open");
                HandOverJumpListInput();
                return;
            }

            DiagnosticLogger.Write("JUMPLIST",
                "drag released outside the interaction area - cancelled");
            _bridge.JumpListHide();
        }

        /// <summary>Activates the row under the point through the native
        /// popup (which closes itself afterwards). A pin toggle refreshes
        /// the model so the bar drops or shows the button at once.</summary>
        private void ActivateJumpListRowAt(int screenX, int screenY)
        {
            try
            {
                int bits = 0;
                _bridge.JumpListActivateAt(screenX, screenY, out bits);
                DiagnosticLogger.Write("JUMPLIST",
                    $"drag released on a row - action bits = {bits}");
                if ((bits & 4) != 0)
                {
                    try
                    {
                        _viewModel.InvalidatePins();
                    }
                    catch (Exception pinEx)
                    {
                        // The pin was toggled on disk; the model refresh is
                        // best effort and the native watcher will catch up.
                        DiagnosticLogger.WriteException("JUMPLIST", pinEx,
                            "pin refresh after the jump list row");
                    }
                }
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "drag activate");
                _bridge.JumpListHide();
            }
        }

        /// <summary>The capture ended by itself (alt-tab, a window taking
        /// focus, the bar deactivated): the drag cannot complete; the list
        /// goes away with it - a list with no pointer under it is a ghost
        /// no one can reach.</summary>
        private void JumpDrag_LostCapture(object sender, MouseEventArgs e)
        {
            if (!_jumpDragActive)
            {
                return;
            }
            DiagnosticLogger.Write("JUMPLIST",
                "drag lost the mouse capture - cancelled");
            EndJumpDrag();
            _bridge.JumpListHide();
        }

        /// <summary>Detaches what BeginJumpDrag wired: the capture (always
        /// released, even when the open failed - the one state a failed
        /// press must never leave behind) and the drag state. The open
        /// list is NOT hidden here: the release decides (activate or
        /// persist) and the cancellation paths hide it themselves.</summary>
        private void EndJumpDrag()
        {
            _jumpDragActive = false;
            FrameworkElement? button = _jumpDragButton;
            _jumpDragButton = null;
            if (button == null)
            {
                return;
            }
            try
            {
                button.LostMouseCapture -= JumpDrag_LostCapture;
                if (button.IsMouseCaptured)
                {
                    button.ReleaseMouseCapture();
                }
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("JUMPLIST", ex,
                    "drag capture teardown (the capture may need the next" +
                    " press)");
            }
        }

        /// <summary>XAML-wired (per button, tunneling): the left release.
        /// It either completes an active drag (the press is consumed and
        /// never activates the window) or simply drops the armed
        /// candidate of a plain click (not consumed: the button keeps its
        /// ordinary activation).</summary>
        private void TaskButton_PreviewMouseLeftButtonUp(
            object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left || e.Handled)
            {
                return;
            }
            if (_jumpDragActive && ReferenceEquals(_jumpDragButton, sender))
            {
                e.Handled = true;   // the gesture owns the press: no click
                JumpDrag_OnRelease((FrameworkElement)sender, e);
                return;
            }
            if (ReferenceEquals(_jumpDragButton, sender))
            {
                CancelJumpDragCandidate();
            }
        }

        /// <summary>The native popup window is a singleton the core reuses
        /// for every open, and its click-outside hook posts the dismissal to
        /// that window: a press that started on another button while the
        /// previous list was up queued a dismissal for a list that no longer
        /// exists, and the message would land on the list that opens now.
        /// Peeling the filtered messages off THIS thread's queue (the popup
        /// lives on the UI thread, the same one that opens it) right after
        /// the open - and before the new hook is installed - removes exactly
        /// those stale posts and nothing else.</summary>
        private void PurgeStaleJumpDismissals()
        {
            try
            {
                IntPtr hwnd = _bridge.FindJumpListPopupWindow();
                if (hwnd == IntPtr.Zero)
                {
                    return;
                }
                while (NativeMethods.PeekMessageW(out _, hwnd,
                           JumpDismissMessage, JumpDismissMessage, PeekRemove))
                {
                    DiagnosticLogger.Write("JUMPLIST",
                        "dropped a dismissal posted for the previous list");
                }
            }
            catch (Exception ex)
            {
                // A failed purge is not a failure of the open: worst case
                // the new list closes at once and the next open is clean.
                LogJumpListFailure(ex, "dismissal purge");
            }
        }

        /// <summary>The opened list is persistent, like Windows 7: it waits
        /// for a row click, Escape or a click outside. The native core does
        /// all three once it owns input (MakeInteractive); a core whose
        /// dist/ DLL predates that export keeps rows and hover (its WndProc
        /// answers them without the interactive bit) and gets the dismissal
        /// from the same hook utility the clock flyout uses.</summary>
        private void HandOverJumpListInput()
        {
            try
            {
                _bridge.JumpListMakeInteractive();
                DisarmJumpDismissFallback();
                DiagnosticLogger.Write("JUMPLIST",
                    "jump list open; the popup owns ordinary input");
            }
            catch (EntryPointNotFoundException)
            {
                DiagnosticLogger.Write("JUMPLIST",
                    "core without W7T_JumpListMakeInteractive (dist/ older" +
                    " than the sources): managed click-outside dismissal" +
                    " armed instead");
                ArmJumpDismissFallback();
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "input handoff");
                HideJumpList("input handoff failed");
            }
        }

        // ---------------------------------------------------------------
        //  Click-outside fallback for a core older than the handoff
        // ---------------------------------------------------------------

        private void ArmJumpDismissFallback()
        {
            try
            {
                if (!_bridge.TryGetJumpListPopupRect(out NativeMethods.RECT r))
                {
                    return;   // nothing on screen: nothing to protect
                }

                _jumpDismissHook ??= new GlobalMouseHook();
                _jumpDismissHook.MouseDownOutside -= JumpDismiss_OutsideClick;
                _jumpDismissHook.MouseDownOutside += JumpDismiss_OutsideClick;
                // A dismissal decided for THIS list only: an open that
                // happens later bumps _jumpOpenGen and makes a callback
                // still queued for the previous list inert.
                _jumpDismissGen = _jumpOpenGen;

                // Clicks on the popup and on the button that owns it are
                // not "outside": the first activates rows (native WndProc),
                // the second must stay able to toggle the list closed.
                _jumpDismissHook.ExcludeRect = new Rect(
                    r.Left, r.Top,
                    Math.Max(0, r.Right - r.Left),
                    Math.Max(0, r.Bottom - r.Top));
                _jumpDismissHook.ExcludeRect2 = new Rect(
                    _jumpButtonRectPx.Left, _jumpButtonRectPx.Top,
                    Math.Max(0, _jumpButtonRectPx.Right - _jumpButtonRectPx.Left),
                    Math.Max(0, _jumpButtonRectPx.Bottom - _jumpButtonRectPx.Top));

                _jumpDismissHook.Stop();
                _jumpDismissHook.Start();
                _jumpDismissArmed = true;
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "dismiss fallback");
            }
        }

        private void DisarmJumpDismissFallback()
        {
            try
            {
                if (_jumpDismissHook == null || !_jumpDismissArmed)
                {
                    return;
                }
                _jumpDismissArmed = false;
                _jumpDismissHook.MouseDownOutside -= JumpDismiss_OutsideClick;
                _jumpDismissHook.Stop();
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "dismiss fallback teardown");
            }
        }

        private void JumpDismiss_OutsideClick(object? sender, Point screenPoint)
        {
            // The hook already marshals to the UI thread. An outside click
            // while the popup is still up is a dismissal; when the native
            // handoff is present this never runs (the native hook and the
            // deactivation hide the popup first, and the probe below reads
            // it from the window itself).
            if (_jumpDismissGen != _jumpOpenGen)
            {
                return;   // decided for a list a newer open replaced
            }
            if (IsJumpListUp())
            {
                HideJumpList("click outside the list");
            }
        }

        // ---------------------------------------------------------------
        //  Teardown
        // ---------------------------------------------------------------

        /// <summary>Ends everything the subsystem may own: an armed press,
        /// the dismissal hook, the native popup. Safe to call twice and
        /// safe to call when nothing is open (the callers sit on group
        /// removal, theme swap, reorder start and window close).</summary>
        private void HideJumpList(string reason)
        {
            if (_jumpArrowPress == null && !_jumpDismissArmed &&
                !_jumpDragActive && _jumpDragButton == null &&
                !IsJumpListUp())
            {
                return;   /* already finished */
            }
            if (_jumpEnding)
            {
                return;
            }
            _jumpEnding = true;
            try
            {
                DiagnosticLogger.Write("JUMPLIST", $"jump list closed ({reason})");
                EndJumpListArrowPress();
                EndJumpDrag();
                DisarmJumpDismissFallback();
                _bridge.JumpListHide();
                _jumpButton = null;
                _jumpGroup = null;
            }
            finally
            {
                _jumpEnding = false;
            }
        }

        private void LogJumpListFailure(Exception ex, string where)
        {
            DiagnosticLogger.WriteException("JUMPLIST", ex, $"failure in {where}");
            try
            {
                // The native side writes its own "Shell/COM failure" lines
                // with the HRESULT; a managed exception here is about the
                // WPF/marshal layer of the interaction, so label it
                // truthfully.
                _bridge.Log($"JumpList: failure in {where}: " +
                            $"{ex.GetType().Name}: {ex.Message}");
            }
            catch { /* logging is best effort */ }
        }

        // ---------------------------------------------------------------
        //  Data preparation: identity resolution lives in the native
        //  subsystem (it needs the Shell property stores); the managed side
        //  only converts geometry and hands over what the group knows.
        // ---------------------------------------------------------------

        /// <summary>Converts the button's bounds to SCREEN PHYSICAL PIXELS
        /// by mapping BOTH corners through PointToScreen (a PMv2 WPF window
        /// needs no second scale multiply - multiplying TransformToDevice
        /// on top of PointToScreen was the old double-scaling bug this
        /// subsystem removed). Returns false when the geometry cannot be
        /// resolved (button not connected yet). The rectangle is also what
        /// the popup is anchored to, so it is read at open time: a button
        /// reordered since the last open anchors the list where it is
        /// NOW.</summary>
        private bool TryGetButtonScreenRect(FrameworkElement element,
                                            out NativeMethods.RECT rectPx)
        {
            rectPx = default;
            try
            {
                Point topLeft = element.PointToScreen(new Point(0, 0));
                Point bottomRight = element.PointToScreen(new Point(
                    element.ActualWidth, element.ActualHeight));

                int left = (int)Math.Round(Math.Min(topLeft.X, bottomRight.X));
                int right = (int)Math.Round(Math.Max(topLeft.X, bottomRight.X));
                int top = (int)Math.Round(Math.Min(topLeft.Y, bottomRight.Y));
                int bottom = (int)Math.Round(Math.Max(topLeft.Y, bottomRight.Y));

                if (right <= left || bottom <= top)
                {
                    return false;
                }

                rectPx = new NativeMethods.RECT
                {
                    Left = left,
                    Top = top,
                    Right = right,
                    Bottom = bottom
                };
                return true;
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "button geometry");
                return false;
            }
        }

        /// <summary>Opens the popup for the armed group. The representative
        /// window of the group (active window, else the first one - the
        /// same rule the wheel handler uses) is what the native side asks
        /// the Shell about; an idle pinned group passes 0 and is identified
        /// by its .lnk metadata. Returns false when the popup must not
        /// show (failure or nothing worth showing).</summary>
        private bool OpenJumpList()
        {
            if (_jumpButton == null || _jumpGroup == null)
            {
                return false;
            }

            try
            {
                if (!TryGetButtonScreenRect(_jumpButton, out var rectPx))
                {
                    return false;
                }
                _jumpButtonRectPx = rectPx;

                TaskGroup group = _jumpGroup;

                // Application identity input: a live window when the app
                // runs, the pinned shortcut otherwise - never the visible
                // button text.
                ulong hwnd = 0;
                TaskWindow? rep = group.Windows.FirstOrDefault(w => w.IsActive)
                                   ?? group.Windows.FirstOrDefault();
                if (rep != null)
                {
                    hwnd = rep.Hwnd;
                }

                // The Windows 7 popup shows the application name without
                // the extension, taken from the executable, not from the
                // button caption.
                string title = group.AppId;
                if (!string.IsNullOrEmpty(group.ExePath))
                {
                    try
                    {
                        title = System.IO.Path.GetFileNameWithoutExtension(
                            group.ExePath);
                    }
                    catch (ArgumentException)
                    {
                        title = group.DisplayTitle;
                    }
                }
                if (string.IsNullOrWhiteSpace(title))
                {
                    title = group.DisplayTitle;
                }

                string launchPath = !string.IsNullOrEmpty(group.LaunchPath)
                    ? group.LaunchPath
                    : (group.ExePath ?? string.Empty);
                string pinnedLnk = group.LaunchPath ?? string.Empty;

                uint[]? icon = ExtractBgra32(group.Icon, out int iconW, out int iconH);

                int lang = Math.Max(0, Array.IndexOf(kLangCodes,
                    RetroBar.Utilities.Settings.Instance.Language
                    ?? RetroBar.Utilities.Settings.DefaultLanguageCode));

                int edge = GetThumbnailEdge(this);

                int entries = _bridge.JumpListOpen(rectPx, edge, title,
                    launchPath, pinnedLnk, group.IsPinned, hwnd,
                    group.ExePath ?? string.Empty, icon, iconW, iconH,
                    lang, out string appId);

                if (entries < 0)
                {
                    // The native side already logged the Shell/COM detail
                    // ([JUMPLIST] lines in log-core.txt); mirror it into
                    // the managed diagnostics and stop here.
                    DiagnosticLogger.Write("JUMPLIST",
                        $"Shell/COM failure opening the jump list (code {entries})");
                    _bridge.Log(
                        $"JumpList: Shell/COM failure = code {entries}");
                    return false;
                }

                if (!string.IsNullOrEmpty(appId))
                {
                    DiagnosticLogger.Write("JUMPLIST",
                        $"application identity resolved; AppUserModelID = {appId}");
                }
                else
                {
                    DiagnosticLogger.Write("JUMPLIST",
                        "application identity resolved; the Shell exposes" +
                        " no explicit AppUserModelID (implicit default used)");
                }
                DiagnosticLogger.Write("JUMPLIST", $"entries loaded = {entries}");
                DiagnosticLogger.Write("JUMPLIST", "popup opened (coordinates" +
                    " in log-core.txt are screen physical pixels)");

                return true;
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "open");
                return false;
            }
        }

        // ---------------------------------------------------------------
        //  Icon transport for the popup's application row: converts the
        //  group's live icon (packaged apps included - never a placeholder
        //  invented here) into the pixels the native row paints.
        // ---------------------------------------------------------------

        /// <summary>Converts an ImageSource into straight (not
        /// premultiplied) top-down BGRA32 pixels, the format the native
        /// MakeHBitmapFromArgb expects. Null when no icon is
        /// available.</summary>
        private static uint[]? ExtractBgra32(ImageSource? source,
            out int width, out int height)
        {
            width = 0;
            height = 0;
            if (source == null) return null;
            try
            {
                if (source is not BitmapSource bmp)
                {
                    return null;
                }
                if (bmp.Format != PixelFormats.Bgra32)
                {
                    bmp = new FormatConvertedBitmap(bmp, PixelFormats.Bgra32, null, 0);
                }
                width = bmp.PixelWidth;
                height = bmp.PixelHeight;
                if (width <= 0 || height <= 0)
                {
                    width = 0;
                    height = 0;
                    return null;
                }
                var bytes = new byte[width * height * 4];
                bmp.CopyPixels(bytes, width * 4, 0);
                var pixels = new uint[width * height];
                Buffer.BlockCopy(bytes, 0, pixels, 0, bytes.Length);
                return pixels;
            }
            catch (Exception ex)
            {
                // A broken icon must not break the jump list: no icon row,
                // but the popup and its data are still correct.
                DiagnosticLogger.WriteException("JUMPLIST", ex, "icon extract");
                width = 0;
                height = 0;
                return null;
            }
        }
    }
}
