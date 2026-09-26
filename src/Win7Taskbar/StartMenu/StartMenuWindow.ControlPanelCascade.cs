// Win7Taskbar - Start Menu: Control Panel cascade wiring (hover, click, close)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Behaviour follows Open-Shell's submenu handling (Open-Shell-Menu, MIT,
// Src/StartMenu/StartMenuDLL/MenuContainer.cpp OnMouseMove/OnTimer and
// MenuCommands.cpp ActivateItem/CloseSubMenus):
//   * hovering a cascading row arms a timer with the system menu delay
//     (SPI_GETMENUSHOWDELAY, Open-Shell's "MenuDelay" default); leaving the
//     row before it fires cancels it;
//   * hovering a non-cascading row while a cascade is open closes the
//     cascade after the same delay; moving into the cascade cancels that;
//   * a click on the cascading row opens the cascade immediately;
//   * the parent row stays highlighted while its cascade is open;
//   * Escape closes the cascade first, then the menu.
// The cascade window is owned and non-activating, so the existing
// deactivate/click-away dismissal of the Start Menu is not disturbed.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    public partial class StartMenuWindow
    {
        private ControlPanelCascade? _cascade;
        private DispatcherTimer? _cascadeTimer;
        private ListBoxItem? _cascadePendingHost;
        private StartMenuItem? _cascadePendingItem;
        private bool _cascadePendingClose;
        private StartMenuItem? _cascadeOpenItem;
        private Task<List<ControlPanelItem>>? _cascadeFetch;

        /// <summary>
        /// Started when the menu is presented so the list is usually ready
        /// by the time the user hovers the row. Open-Shell enumerates on
        /// every submenu open; a fresh list per Start Menu presentation
        /// gives the same "always current" result with less latency.
        /// </summary>
        private void PrefetchControlPanelCascade()
        {
            try
            {
                _cascadeFetch = Task.Run(ControlPanelItems.Enumerate);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: prefetch: {ex.Message}");
                _cascadeFetch = null;
            }
        }

        private static TimeSpan CascadeDelay()
        {
            try
            {
                int ms = SystemParameters.MenuShowDelay;
                if (ms < 0 || ms > 5000)
                {
                    ms = 400;
                }
                return TimeSpan.FromMilliseconds(ms);
            }
            catch (Exception)
            {
                return TimeSpan.FromMilliseconds(400);
            }
        }

        private void OnCascadeRowHover(ListBoxItem? host, StartMenuItem item)
        {
            try
            {
                if (item.HasCascade)
                {
                    if (_cascade is { IsOpen: true } && ReferenceEquals(_cascadeOpenItem, item))
                    {
                        /* Back on the parent row: keep it open. */
                        StopCascadeTimer();
                        return;
                    }
                    ArmCascadeTimer(host, item, close: false);
                }
                else if (_cascade is { IsOpen: true })
                {
                    ArmCascadeTimer(null, null, close: true);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: hover: {ex.Message}");
            }
        }

        private void OnCascadeRowLeave(ListBoxItem? host)
        {
            /* Leaving the row before the delay elapsed: no submenu
             * (Open-Shell kills TIMER_HOVER on WM_MOUSELEAVE). A pending
             * close keeps running: the pointer may be heading elsewhere. */
            if (!_cascadePendingClose && host != null &&
                ReferenceEquals(_cascadePendingHost, host))
            {
                StopCascadeTimer();
            }
        }

        private void ArmCascadeTimer(ListBoxItem? host, StartMenuItem? item, bool close)
        {
            StopCascadeTimer();
            _cascadePendingHost = host;
            _cascadePendingItem = item;
            _cascadePendingClose = close;
            _cascadeTimer = new DispatcherTimer(DispatcherPriority.Input, Dispatcher)
            {
                Interval = CascadeDelay()
            };
            _cascadeTimer.Tick += OnCascadeTimerTick;
            _cascadeTimer.Start();
        }

        private void StopCascadeTimer()
        {
            if (_cascadeTimer != null)
            {
                _cascadeTimer.Stop();
                _cascadeTimer.Tick -= OnCascadeTimerTick;
                _cascadeTimer = null;
            }
            _cascadePendingHost = null;
            _cascadePendingItem = null;
            _cascadePendingClose = false;
        }

        private void OnCascadeTimerTick(object? sender, EventArgs e)
        {
            bool close = _cascadePendingClose;
            ListBoxItem? host = _cascadePendingHost;
            StartMenuItem? item = _cascadePendingItem;
            StopCascadeTimer();
            if (close)
            {
                CloseControlPanelCascade();
                return;
            }
            if (host != null && item != null && host.IsMouseOver)
            {
                OpenControlPanelCascade(host, item);
            }
        }

        /// <summary>
        /// Opens (or re-opens) the cascade next to <paramref name="host"/>.
        /// Waits for the background enumeration when it is still running.
        /// </summary>
        private void OpenControlPanelCascade(ListBoxItem? host, StartMenuItem item)
        {
            StopCascadeTimer();
            if (host == null || !IsMenuVisible)
            {
                return;
            }
            Task<List<ControlPanelItem>>? fetch = _cascadeFetch;
            if (fetch == null)
            {
                PrefetchControlPanelCascade();
                fetch = _cascadeFetch;
                if (fetch == null)
                {
                    return;
                }
            }
            if (!fetch.IsCompleted)
            {
                fetch.ContinueWith(_ =>
                {
                    try
                    {
                        Dispatcher.BeginInvoke(DispatcherPriority.Input, new Action(() =>
                        {
                            if ((IsMenuVisible && host.IsMouseOver) ||
                                (_cascade is { IsOpen: true } && ReferenceEquals(_cascadeOpenItem, item)))
                            {
                                OpenControlPanelCascade(host, item);
                            }
                        }));
                    }
                    catch (Exception)
                    {
                    }
                }, TaskScheduler.Default);
                return;
            }

            List<ControlPanelItem> items;
            try
            {
                items = fetch.IsCompletedSuccessfully ? fetch.Result : new List<ControlPanelItem>();
            }
            catch (Exception)
            {
                items = new List<ControlPanelItem>();
            }

            try
            {
                if (_cascade == null)
                {
                    _cascade = new ControlPanelCascade(this);
                    _cascade.ItemActivated += OnCascadeItemActivated;
                    _cascade.ItemContextRequested += OnCascadeItemContext;
                    _cascade.PointerEntered += () =>
                    {
                        if (_cascadePendingClose)
                        {
                            StopCascadeTimer();
                        }
                    };
                }

                double scale = 1.0;
                try
                {
                    scale = PresentationSource.FromVisual(this)?.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
                }
                catch (Exception)
                {
                }

                Point tl = host.PointToScreen(new Point(0, 0));
                Point br = host.PointToScreen(new Point(host.ActualWidth, host.ActualHeight));
                var anchor = new NativeMethods.RECT
                {
                    Left = (int)Math.Round(tl.X),
                    Top = (int)Math.Round(tl.Y),
                    Right = (int)Math.Round(br.X),
                    Bottom = (int)Math.Round(br.Y)
                };
                NativeMethods.RECT limits = CascadeLimits(anchor);

                if (_cascadeOpenItem != null && !ReferenceEquals(_cascadeOpenItem, item))
                {
                    _cascadeOpenItem.IsExpanded = false;
                }
                _cascadeOpenItem = item;
                item.IsExpanded = true;
                _cascade.Open(items, anchor, limits, scale);
                if (!_cascade.IsOpen)
                {
                    item.IsExpanded = false;
                    _cascadeOpenItem = null;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: open: {ex.Message}");
                CloseControlPanelCascade();
            }
        }

        /// <summary>
        /// Open-Shell's s_MenuLimits: the work area of the monitor that
        /// hosts the menu, extended to the monitor edge on the taskbar side
        /// so a tall cascade may run over the bar instead of being cut.
        /// </summary>
        private static NativeMethods.RECT CascadeLimits(NativeMethods.RECT anchor)
        {
            var limits = new NativeMethods.RECT
            {
                Left = anchor.Left - 4000,
                Top = anchor.Top - 4000,
                Right = anchor.Right + 4000,
                Bottom = anchor.Bottom + 4000
            };
            try
            {
                var pt = new NativeMethods.POINT
                {
                    x = (anchor.Left + anchor.Right) / 2,
                    y = (anchor.Top + anchor.Bottom) / 2
                };
                IntPtr monitor = NativeMethods.MonitorFromPoint(pt, NativeMethods.MONITOR_DEFAULTTONEAREST);
                if (monitor == IntPtr.Zero)
                {
                    return limits;
                }
                var info = new NativeMethods.MONITORINFO
                {
                    cbSize = System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.MONITORINFO>()
                };
                if (!NativeMethods.GetMonitorInfoW(monitor, ref info))
                {
                    return limits;
                }
                limits = info.rcWork;
                bool taskbarAtTop = false;
                try
                {
                    taskbarAtTop = RetroBar.Utilities.Settings.Instance.TaskbarPosition == 1;
                }
                catch (Exception)
                {
                }
                if (taskbarAtTop)
                {
                    limits.Top = info.rcMonitor.Top;
                }
                else
                {
                    limits.Bottom = info.rcMonitor.Bottom;
                }
            }
            catch (Exception)
            {
            }
            return limits;
        }

        /// <summary>Closes the cascade; true when one was open.</summary>
        private bool CloseControlPanelCascade()
        {
            StopCascadeTimer();
            bool wasOpen = false;
            try
            {
                if (_cascade != null && (_cascade.IsOpen || _cascade.IsVisible))
                {
                    wasOpen = true;
                    _cascade.Dismiss();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: close: {ex.Message}");
            }
            if (_cascadeOpenItem != null)
            {
                _cascadeOpenItem.IsExpanded = false;
                _cascadeOpenItem = null;
            }
            return wasOpen;
        }

        private void OnCascadeItemActivated(ControlPanelItem item)
        {
            bool runAs = false;
            try
            {
                runAs = (Keyboard.Modifiers & (ModifierKeys.Control | ModifierKeys.Shift))
                    == (ModifierKeys.Control | ModifierKeys.Shift);
            }
            catch (Exception)
            {
            }
            IntPtr owner = IntPtr.Zero;
            try { owner = new WindowInteropHelper(this).Handle; } catch (Exception) { }

            bool launched = false;
            try
            {
                launched = _vm.LaunchControlPanelItem(item, owner, runAs);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: launch: {ex.Message}");
            }
            if (launched)
            {
                Dismiss();
            }
        }

        private void OnCascadeItemContext(ControlPanelItem item, int x, int y)
        {
            /* Same pattern as RunItemMenu: the shell menu is modal and takes
             * the activation; the Start Menu must not dismiss meanwhile. */
            _suppressDeactivate = true;
            Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle, new Action(() =>
            {
                bool ran = false;
                try
                {
                    IntPtr owner = new WindowInteropHelper(this).Handle;
                    ran = ShellContextMenu.TryShow(item.ParsingName, x, y, owner, null, null, out _);
                }
                catch (Exception)
                {
                }
                finally
                {
                    _suppressDeactivate = false;
                }
                if (ran)
                {
                    Dismiss();
                }
            }));
        }
    }
}
