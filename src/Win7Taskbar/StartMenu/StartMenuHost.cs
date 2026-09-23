// Win7Taskbar - Start Menu host (own STA thread + WPF Dispatcher)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch.
//
// Two processes only: this host lives inside Win7Taskbar.exe. The helper
// (Win7StartHelper.exe) owns WH_KEYBOARD_LL. IPC is the two named events
// Local\Win7Taskbar_WindowsKey and Local\Win7Taskbar_StartMenuHeartbeat.

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    internal static class StartMenuHost
    {
        internal const string WindowTitle = "Win7Taskbar Start Menu";
        private const string WinKeyEventName = @"Local\Win7Taskbar_WindowsKey";
        private const string HeartbeatEventName = @"Local\Win7Taskbar_StartMenuHeartbeat";

        private static Thread? _thread;
        private static Dispatcher? _dispatcher;
        private static StartMenuWindow? _window;
        private static NativeBridge? _bridge;
        private static EventWaitHandle? _winKeyEvent;
        private static EventWaitHandle? _heartbeatEvent;
        private static DispatcherTimer? _heartbeatTimer;
        private static Thread? _winKeyWaiter;
        private static Process? _helper;
        private static int _started;
        private static readonly object AnchorLock = new();
        private static Rect _taskbarRect;
        private static Rect _orbRect;
        private static int _anchorValid;

        public static bool IsVisible
        {
            get
            {
                StartMenuWindow? w = _window;
                Dispatcher? d = _dispatcher;
                if (w == null || d == null)
                {
                    return false;
                }
                try
                {
                    return d.Invoke(() => w.IsMenuVisible);
                }
                catch (Exception)
                {
                    return false;
                }
            }
        }

        public static void Start(NativeBridge bridge)
        {
            if (Interlocked.Exchange(ref _started, 1) == 1)
            {
                return;
            }
            _bridge = bridge ?? throw new ArgumentNullException(nameof(bridge));

            var ready = new ManualResetEventSlim(false);
            _thread = new Thread(() =>
            {
                try
                {
                    _dispatcher = Dispatcher.CurrentDispatcher;
                    _window = new StartMenuWindow(bridge);
                    _window.SourceInitialized += (_, _) =>
                    {
                        /* catalog after HWND exists so SHGetFileInfo is safe */
                    };
                    try
                    {
                        _winKeyEvent = new EventWaitHandle(false, EventResetMode.AutoReset, WinKeyEventName);
                        _heartbeatEvent = new EventWaitHandle(false, EventResetMode.AutoReset, HeartbeatEventName);
                    }
                    catch (Exception)
                    {
                        /* events missing: Win-key helper cannot signal us */
                    }

                    _heartbeatTimer = new DispatcherTimer(DispatcherPriority.Background)
                    {
                        Interval = TimeSpan.FromMilliseconds(500)
                    };
                    _heartbeatTimer.Tick += (_, _) =>
                    {
                        try { _heartbeatEvent?.Set(); } catch (Exception) { }
                    };
                    _heartbeatTimer.Start();

                    _winKeyWaiter = new Thread(WinKeyWaitLoop)
                    {
                        IsBackground = true,
                        Name = "Win7Taskbar.WinKeyWait"
                    };
                    _winKeyWaiter.Start();

                    StartHelperProcess();
                    _window.PrepareCatalog();
                    ready.Set();
                    Dispatcher.Run();
                }
                catch (Exception)
                {
                    ready.Set();
                }
            })
            {
                IsBackground = true,
                Name = "Win7Taskbar.StartMenu"
            };
            _thread.SetApartmentState(ApartmentState.STA);
            _thread.Start();
            ready.Wait(TimeSpan.FromSeconds(8));
        }

        public static void Stop()
        {
            try { _heartbeatTimer?.Stop(); } catch (Exception) { }
            try { _winKeyEvent?.Set(); } catch (Exception) { }
            try { _dispatcher?.InvokeShutdown(); } catch (Exception) { }
            try { _helper?.Close(); } catch (Exception) { }
            _helper = null;
            Interlocked.Exchange(ref _started, 0);
        }

        public static void SetAnchor(Rect taskbarScreenDip, Rect orbScreenDip)
        {
            lock (AnchorLock)
            {
                _taskbarRect = taskbarScreenDip;
                _orbRect = orbScreenDip;
                _anchorValid = 1;
            }
        }

        public static void Toggle()
        {
            Dispatcher? d = _dispatcher;
            StartMenuWindow? w = _window;
            if (d == null || w == null)
            {
                return;
            }
            d.BeginInvoke(new Action(() =>
            {
                if (w.IsMenuVisible)
                {
                    w.Dismiss();
                }
                else
                {
                    ShowCore(w);
                }
            }));
        }

        public static void Show()
        {
            Dispatcher? d = _dispatcher;
            StartMenuWindow? w = _window;
            if (d == null || w == null)
            {
                return;
            }
            d.BeginInvoke(new Action(() => ShowCore(w)));
        }

        public static void Hide()
        {
            Dispatcher? d = _dispatcher;
            StartMenuWindow? w = _window;
            if (d == null || w == null)
            {
                return;
            }
            d.BeginInvoke(new Action(() => w.Dismiss()));
        }

        private static void ShowCore(StartMenuWindow w)
        {
            try
            {
                AllowSetForegroundWindow(NativeMethods.GetCurrentProcessId());
            }
            catch (Exception)
            {
            }
            Rect bar;
            Rect orb;
            bool haveAnchor;
            lock (AnchorLock)
            {
                haveAnchor = _anchorValid != 0;
                bar = _taskbarRect;
                orb = _orbRect;
            }
            if (!haveAnchor || bar.Width < 1 || bar.Height < 1)
            {
                Rect work = SystemParameters.WorkArea;
                bar = new Rect(work.Left, work.Bottom, work.Width,
                    Math.Max(40, SystemParameters.PrimaryScreenHeight - work.Bottom));
                orb = new Rect(bar.Left, bar.Top, 54, bar.Height);
            }
            w.PresentAbove(bar, orb);
            w.FocusSearch();
        }

        private static void WinKeyWaitLoop()
        {
            EventWaitHandle? ev = _winKeyEvent;
            if (ev == null)
            {
                return;
            }
            while (Volatile.Read(ref _started) == 1)
            {
                try
                {
                    if (!ev.WaitOne(500))
                    {
                        continue;
                    }
                    if (Volatile.Read(ref _started) != 1)
                    {
                        break;
                    }
                    Toggle();
                }
                catch (ObjectDisposedException)
                {
                    break;
                }
                catch (Exception)
                {
                    Thread.Sleep(200);
                }
            }
        }

        private static void StartHelperProcess()
        {
            try
            {
                string exe = Path.Combine(AppContext.BaseDirectory, "Win7StartHelper.exe");
                if (!File.Exists(exe))
                {
                    return;
                }
                _helper = Process.Start(new ProcessStartInfo
                {
                    FileName = exe,
                    Arguments = "--pid " + Process.GetCurrentProcess().Id,
                    UseShellExecute = false,
                    CreateNoWindow = true
                });
            }
            catch (Exception)
            {
                _helper = null;
            }
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool AllowSetForegroundWindow(uint dwProcessId);
    }
}
