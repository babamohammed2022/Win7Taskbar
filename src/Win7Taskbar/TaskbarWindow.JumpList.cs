// Win7Taskbar - Windows 7 Jump List gesture subsystem
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// The Windows 7 Superbar opened a taskbar button's Jump List from the
// left-button press + drag-up gesture (the right-click stays the plain
// Windows 7 context menu, handled in TaskbarWindow.xaml.cs and not touched
// here). This file is the whole interaction: a small state machine
//
//     Idle -> PotentialDrag -> Opening -> Open
//
// armed on mouse-down over a task button. A movement smaller than the
// SYSTEM drag threshold (SystemParameters.MinimumVerticalDragDistance: the
// DPI-aware SM_CYDRAG value, converted to DIPs by WPF - no hard-coded
// pixel) never consumes the click; a normal left click runs through the
// button handlers exactly as before. Only an upward drag beyond the
// threshold takes the gesture over:
//
//   * the popup is built and shown by the native subsystem from the
//     application's REAL Shell jump list data (identity resolution and the
//     list read both live in native/src/JumpListWindow.cpp);
//   * while open, the button keeps mouse capture (the same mechanism the
//     tray drag uses: element capture + window-level tunneling handlers +
//     manual hit-testing). The popup window never activates and never
//     touches the input queue itself;
//   * the cursor may travel through the gap between button and popup; if
//     it leaves the interaction area the gesture cancels cleanly;
//   * releasing the left button activates the row under the cursor
//     (document, application row or pin toggle) - the native side
//     hit-tests the screen point it is given;
//   * every failure (Shell/COM, popup creation, marshal) is logged through
//     the project's DiagnosticLogger and ends the gesture in a controlled
//     way: capture released, handlers detached, popup hidden. A failure
//     here can never take the taskbar down.
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
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Win7Taskbar.Interop;
using Win7Taskbar.Models;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    public partial class TaskbarWindow
    {
        /// <summary>Gesture stages (the state machine of the subsystem).</summary>
        private enum JumpListStage
        {
            Idle,
            PotentialDrag,
            Opening,
            Open
        }

        private JumpListStage _jumpStage = JumpListStage.Idle;
        private FrameworkElement? _jumpButton;
        private TaskGroup? _jumpGroup;
        private Point _jumpStartDip;          // window client DIPs; threshold only
        private bool _jumpSuppressNextClick;  // a consumed drag must not activate
        private bool _jumpEnding;             // re-entrancy guard (like tray drag)
        // Generation token: the deferred teardown of a quiet release must
        // never detach a subscription belonging to a LATER press on the
        // same element (rapid re-clicks are legal). Every new press bumps it.
        private int _jumpGeneration;

        /// <summary>True while the gesture owns the pointer: the hover
        /// preview and the button tooltip stay away while a jump list is
        /// on screen (two stacked popups are not the Windows 7 way).</summary>
        private bool IsJumpListGestureActive() => _jumpStage != JumpListStage.Idle;

        // ---------------------------------------------------------------
        //  Entry points called from the button's existing handlers
        //  (TaskbarWindow.xaml.cs) - the XAML wiring is unchanged.
        // ---------------------------------------------------------------

        /// <summary>Left button pressed on a task button: arm the drag
        /// detection. Nothing opens yet - Windows 7 never opens the jump
        /// list on mouse-down.</summary>
        private void BeginPotentialJumpListDrag(FrameworkElement element,
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

            // A stale machine must never survive into a new press.
            if (_jumpStage != JumpListStage.Idle)
            {
                CancelJumpList("stale gesture before a new press");
            }

            // Own the generation first: a deferred teardown queued by an
            // earlier quiet release becomes inert the moment it notices the
            // bump, so it cannot detach the subscriptions set up below.
            _jumpGeneration++;

            // If the previous press ended on a DIFFERENT button, drop its
            // leftover capture handler (same-element re-arms are handled by
            // the detach-then-attach below; the idempotent "-=" on the same
            // element+handler would otherwise double-subscribe).
            if (_jumpButton != null && !ReferenceEquals(_jumpButton, element))
            {
                _jumpButton.LostMouseCapture -= JumpList_LostCapture;
            }

            _jumpButton = element;
            _jumpGroup = group;
            _jumpStartDip = e.GetPosition(this);   // window client DIPs
            _jumpStage = JumpListStage.PotentialDrag;
            DiagnosticLogger.Write("JUMPLIST", "left-button gesture started");

            // Capture NOW: with the button element captured, the window's
            // tunneling Preview handlers keep seeing moves and the release
            // even when the cursor is far above the bar - the same
            // mechanism the tray drag uses (manual hit-test, no hooks).
            // Each += is preceded by -=: re-arming twice in a row (a missed
            // teardown after an exceptional release) must not stack
            // duplicate handlers on the window.
            try
            {
                element.LostMouseCapture -= JumpList_LostCapture;
                PreviewMouseMove -= JumpList_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= JumpList_CapturedMouseUp;
                PreviewKeyDown -= JumpList_PreviewKeyDown;

                element.CaptureMouse();
                PreviewMouseMove += JumpList_CapturedMouseMove;
                PreviewMouseLeftButtonUp += JumpList_CapturedMouseUp;
                PreviewKeyDown += JumpList_PreviewKeyDown;
                element.LostMouseCapture += JumpList_LostCapture;
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "capture");
                CancelJumpList("capture failed");
            }
        }

        /// <summary>Click fired by the button after a consumed drag must
        /// not activate the group. Consumed once.</summary>
        private bool ShouldSuppressClickAfterJumpList()
        {
            if (!_jumpSuppressNextClick)
            {
                return false;
            }
            _jumpSuppressNextClick = false;
            return true;
        }

        // ---------------------------------------------------------------
        //  The gesture itself
        // ---------------------------------------------------------------

        private void JumpList_CapturedMouseMove(object sender, MouseEventArgs e)
        {
            if (_jumpStage == JumpListStage.Idle || _jumpButton == null)
            {
                return;
            }

            if (e.LeftButton != MouseButtonState.Pressed)
            {
                // The button went up without a preview-up (e.g. it was
                // grabbed elsewhere): end the gesture, do not linger.
                CancelJumpList("left button released elsewhere");
                return;
            }

            if (_jumpStage == JumpListStage.PotentialDrag)
            {
                if (!ShouldOpenJumpList(e.GetPosition(this)))
                {
                    return;
                }

                _jumpStage = JumpListStage.Opening;
                DiagnosticLogger.Write("JUMPLIST", "drag threshold reached");

                if (!OpenJumpList())
                {
                    // Controlled failure (or no data worth showing): the
                    // drag was consumed, the click must not fire, but no
                    // popup exists - return to Idle quietly.
                    _jumpStage = JumpListStage.Idle;
                    ConsumeClickOnce();
                    TearDownJumpListCapture();
                    return;
                }

                _jumpStage = JumpListStage.Open;
                return;
            }

            if (_jumpStage == JumpListStage.Open)
            {
                UpdateJumpListHover(e.GetPosition(this));
            }
        }

        /// <summary>Drag threshold rule: vertical movement beyond the
        /// SYSTEM drag distance, clearly upward-dominant (the threshold is
        /// the DPI-aware SM_CYDRAG in DIPs; positions here are window
        /// client DIPs, so the comparison is space-consistent).</summary>
        private bool ShouldOpenJumpList(Point currentDip)
        {
            Vector moved = currentDip - _jumpStartDip;
            return moved.Y < -SystemParameters.MinimumVerticalDragDistance &&
                   Math.Abs(moved.Y) > Math.Abs(moved.X);
        }

        private void UpdateJumpListHover(Point windowDip)
        {
            try
            {
                // window DIP -> screen physical pixels (PointToScreenSafe;
                // no further scaling, see the note at the top of the file)
                Point screen = PointToScreenSafe(windowDip);
                bool inside = _bridge.JumpListSetHover(
                    (int)Math.Round(screen.X), (int)Math.Round(screen.Y));
                if (!inside)
                {
                    // Cancelled by leaving the area; the click stays
                    // consumed for this press (a drag was performed).
                    CancelJumpList("pointer left the interaction area");
                }
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "hover update");
                CancelJumpList("hover failed");
            }
        }

        private void JumpList_CapturedMouseUp(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left || _jumpStage == JumpListStage.Idle)
            {
                return;
            }

            if (_jumpStage == JumpListStage.Open)
            {
                HandleJumpListSelection(e.GetPosition(this));
                return;
            }

            if (_jumpStage == JumpListStage.PotentialDrag)
            {
                // A click that never crossed the drag threshold: disarm
                // silently. No "cancelled" line - the button handles the
                // click itself and log noise on every click is forbidden.
                //
                // CRITICAL: the capture is NOT released here. ButtonBase is
                // mid-release (its own up handler runs after this tunneling
                // one), and yanking the capture before it sees the up would
                // change normal-click behavior. The teardown is deferred
                // to after this input event completes.
                _jumpStage = JumpListStage.Idle;
                PreviewMouseMove -= JumpList_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= JumpList_CapturedMouseUp;
                PreviewKeyDown -= JumpList_PreviewKeyDown;
                int releaseGen = _jumpGeneration;
                FrameworkElement? closing = _jumpButton;
                Dispatcher.BeginInvoke(DispatcherPriority.Input, new Action(() =>
                {
                    // By now the button's own up processing is done. Run
                    // only while this release is still the current gesture
                    // (a re-arm bumps the generation and owns the state).
                    if (releaseGen != _jumpGeneration) return;
                    if (!ReferenceEquals(_jumpButton, closing)) return;
                    if (closing != null)
                    {
                        closing.LostMouseCapture -= JumpList_LostCapture;
                        if (closing.IsMouseCaptured)
                        {
                            // Let go of a capture nothing else released.
                            closing.ReleaseMouseCapture();
                        }
                    }
                    _jumpButton = null;
                    _jumpGroup = null;
                }));
                return;
            }

            // Opening failed mid-way: end quietly, the failure was already
            // logged at its source.
            CancelJumpList("gesture released without a list");
        }

        /// <summary>Release over the open popup: activate the row under the
        /// cursor. The native side hit-tests the screen point; a release
        /// over empty popup space just closes (like Windows 7).</summary>
        private void HandleJumpListSelection(Point windowDip)
        {
            ConsumeClickOnce();
            try
            {
                Point screen = PointToScreenSafe(windowDip);
                bool handled = _bridge.JumpListActivateAt(
                    (int)Math.Round(screen.X), (int)Math.Round(screen.Y),
                    out int bits);
                if (handled)
                {
                    if (bits != 0)
                    {
                        DiagnosticLogger.Write("JUMPLIST",
                            $"item activated (bits={bits}: 1=document," +
                            " 2=app row, 4=pin toggled)");
                        if ((bits & 4) != 0)
                        {
                            // The native side wrote/deleted the real .lnk;
                            // refresh the model without waiting for the
                            // folder watcher's next pass.
                            _viewModel.InvalidatePins();
                            _bridge.PinnedRefresh();
                        }
                    }
                    else
                    {
                        DiagnosticLogger.Write("JUMPLIST",
                            "popup closed without a selection");
                    }
                }
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "activation");
            }
            finally
            {
                // Whatever happened above: no stuck capture, popup gone.
                _jumpStage = JumpListStage.Idle;
                _bridge.JumpListHide();
                TearDownJumpListCapture();
            }
        }

        private void JumpList_LostCapture(object sender, MouseEventArgs e)
        {
            // Capture ended by itself (alt-tab, a window taking focus...):
            // the gesture cannot continue; leave a clean state behind.
            if (_jumpStage != JumpListStage.Idle)
            {
                CancelJumpList("mouse capture lost");
            }
        }

        private void JumpList_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (_jumpStage == JumpListStage.Open && e.Key == Key.Escape)
            {
                e.Handled = true;
                CancelJumpList("escape");
            }
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
        /// resolved (button not connected yet).</summary>
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
                    // the managed diagnostics and stop the gesture.
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

                // A jump list on screen replaces the hover previews of the
                // button it belongs to (v2.53 rule: never two popups).
                if (_openButtonTip is { IsOpen: true })
                {
                    try { _openButtonTip.IsOpen = false; } catch { }
                }
                CloseTaskPreview();

                return true;
            }
            catch (Exception ex)
            {
                LogJumpListFailure(ex, "open");
                return false;
            }
        }

        // ---------------------------------------------------------------
        //  Teardown
        // ---------------------------------------------------------------

        /// <summary>Ends the gesture without activating anything: hide the
        /// popup, release the capture, detach the handlers. Safe to call
        /// twice (re-entrancy guard like the tray drag).</summary>
        private void CancelJumpList(string reason)
        {
            if (_jumpStage == JumpListStage.Idle && _jumpButton == null)
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
                DiagnosticLogger.Write("JUMPLIST", $"popup cancelled ({reason})");
                if (_jumpStage == JumpListStage.Open)
                {
                    ConsumeClickOnce();
                }
                _jumpStage = JumpListStage.Idle;
                try { _bridge.JumpListHide(); } catch { }
                TearDownJumpListCapture();
            }
            finally
            {
                _jumpEnding = false;
            }
        }

        /// <summary>The drag consumed this press: the Click that the button
        /// fires on release (with the capture this subsystem holds the
        /// press and the release are the same element) must not activate
        /// the group. Cleared by the next press as well, so a consumed flag
        /// can never eat a later, genuine click.</summary>
        private void ConsumeClickOnce()
        {
            _jumpSuppressNextClick = true;
            Dispatcher.BeginInvoke(DispatcherPriority.Input, new Action(() =>
            {
                // Belt and braces: if no Click ever arrives (the release
                // landed outside the button and capture semantics differed
                // from expectations), the flag must not survive the frame.
                if (_jumpStage == JumpListStage.Idle)
                {
                    _jumpSuppressNextClick = false;
                }
            }));
        }

        /// <summary>Detaches everything BeginPotentialJumpListDrag wired.
        /// The capture release is unconditional: it is the one state a
        /// failed gesture must never leave behind.</summary>
        private void TearDownJumpListCapture()
        {
            try
            {
                PreviewMouseMove -= JumpList_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= JumpList_CapturedMouseUp;
                PreviewKeyDown -= JumpList_PreviewKeyDown;
                if (_jumpButton != null)
                {
                    _jumpButton.LostMouseCapture -= JumpList_LostCapture;
                    if (_jumpButton.IsMouseCaptured)
                    {
                        _jumpButton.ReleaseMouseCapture();
                    }
                }
                else
                {
                    ReleaseMouseCapture();
                }
            }
            catch (Exception ex)
            {
                // Tearing down must not throw into the input pipeline;
                // a capture that refuses to die is logged, not hidden.
                DiagnosticLogger.WriteException("JUMPLIST", ex,
                    "capture teardown (mouse capture may need the next press)");
            }
            finally
            {
                _jumpButton = null;
                _jumpGroup = null;
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
                // WPF/marshal layer of the gesture, so label it truthfully.
                _bridge.Log($"JumpList: gesture failure in {where}: " +
                            $"{ex.GetType().Name}: {ex.Message}");
            }
            catch { /* logging is best effort */ }
        }

        // ---------------------------------------------------------------
        //  Icon transport for the popup's application row (kept from the
        //  v2.38 helper; it converts the group's live icon, packaged apps
        //  included - never a placeholder invented here).
        // ---------------------------------------------------------------

        /// <summary>v2.38: converts an ImageSource into straight (not
        /// premultiplied) top-down BGRA32 pixels, the format the native
        /// MakeHBitmapFromArgb expects. Null when no icon is available.</summary>
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
