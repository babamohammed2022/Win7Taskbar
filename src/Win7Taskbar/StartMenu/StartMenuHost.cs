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
        private static int _scanReady;
        private static readonly object LifecycleLock = new();
        private static readonly object AnchorLock = new();
        private static Rect _taskbarRect;
        private static Rect _orbRect;
        private static int _anchorValid;

        /// <summary>
        /// Fired on the Start Menu STA after show/hide. The taskbar uses this
        /// to keep the orb pressed while OUR menu is open (the native Start
        /// monitor must not steal that state).
        /// </summary>
        internal static event Action<bool>? MenuVisibilityChanged;

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
            if (bridge == null)
            {
                throw new ArgumentNullException(nameof(bridge));
            }
            if (Interlocked.Exchange(ref _started, 1) == 1)
            {
                return;
            }
            _bridge = bridge;

            try
            {
                var scanner = new Thread(() =>
                {
                    try { bridge.StartMenuScan(); } catch (Exception) { }
                    Interlocked.Exchange(ref _scanReady, 1);
                    try
                    {
                        Dispatcher? d = _dispatcher;
                        StartMenuWindow? w = _window;
                        if (d != null && w != null)
                        {
                            d.BeginInvoke(new Action(() =>
                            {
                                try { w.LoadCachedCatalog(); } catch (Exception) { }
                            }));
                        }
                    }
                    catch (Exception)
                    {
                    }
                })
                {
                    IsBackground = true,
                    Name = "Win7Taskbar.StartMenuScan"
                };
                scanner.Start();
            }
            catch (Exception)
            {
            }

            using var ready = new ManualResetEventSlim(false);
            void SignalReady()
            {
                try { ready.Set(); }
                catch (ObjectDisposedException) { }
                catch (Exception ex)
                {
                    Debug.WriteLine($"StartMenu ready signal: {ex.Message}");
                }
            }

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
                    SignalReady();
                    try
                    {
                        if (Volatile.Read(ref _scanReady) != 0)
                        {
                            _window?.LoadCachedCatalog();
                        }
                    }
                    catch (Exception)
                    {
                    }
                    Dispatcher.Run();
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"StartMenu STA: {ex}");
                    SignalReady();
                }
            })
            {
                IsBackground = true,
                Name = "Win7Taskbar.StartMenu"
            };
            try
            {
                _thread.SetApartmentState(ApartmentState.STA);
                _thread.Start();
                ready.Wait(TimeSpan.FromSeconds(8));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartMenu thread start: {ex}");
                Stop();
            }
        }

        public static void Stop()
        {
            if (Interlocked.Exchange(ref _started, 0) == 0)
            {
                return;
            }

            Thread? waiter;
            Thread? host;
            Dispatcher? dispatcher;
            lock (LifecycleLock)
            {
                waiter = _winKeyWaiter;
                host = _thread;
                dispatcher = _dispatcher;
            }

            try
            {
                /* Il dispatcher del menu possiede il timer e la Window: lo
                 * smontaggio avviene sul suo STA prima del BeginInvokeShutdown.
                 * L'evento del waiter viene segnalato dopo aver chiuso il
                 * produttore, così non può riaprire il menu durante lo stop. */
                if (dispatcher != null)
                {
                    try
                    {
                        dispatcher.BeginInvoke(new Action(() =>
                        {
                            try { _heartbeatTimer?.Stop(); }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"StartMenu timer cleanup: {ex.Message}");
                            }
                            try { _window?.Dismiss(); }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"StartMenu window cleanup: {ex.Message}");
                            }
                            try
                            {
                                dispatcher.BeginInvokeShutdown(DispatcherPriority.Send);
                            }
                            catch (Exception ex)
                            {
                                Debug.WriteLine($"StartMenu dispatcher shutdown: {ex.Message}");
                            }
                        }), DispatcherPriority.Send);
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"StartMenu dispatcher cleanup: {ex.Message}");
                        try { dispatcher.BeginInvokeShutdown(DispatcherPriority.Send); }
                        catch (Exception shutdownEx)
                        {
                            Debug.WriteLine($"StartMenu dispatcher force stop: {shutdownEx.Message}");
                        }
                    }
                }

                try { _winKeyEvent?.Set(); }
                catch (Exception ex)
                {
                    Debug.WriteLine($"StartMenu wait event signal: {ex.Message}");
                }

                JoinThread(waiter, "WinKeyWait", 1500);
                JoinThread(host, "StartMenu", 3000);
            }
            finally
            {
                /* RAII: nessun handle/evento/timer resta radicato dopo la
                 * chiusura del secondo STA, anche se una fase precedente ha
                 * fallito o ha restituito prima del previsto. */
                try { _heartbeatTimer?.Stop(); } catch (Exception) { }
                try { _heartbeatTimer = null; } catch (Exception) { }
                try { _winKeyEvent?.Dispose(); } catch (Exception) { }
                try { _heartbeatEvent?.Dispose(); } catch (Exception) { }
                try { _helper?.Close(); } catch (Exception) { }
                try { _helper?.Dispose(); } catch (Exception) { }

                lock (LifecycleLock)
                {
                    _winKeyEvent = null;
                    _heartbeatEvent = null;
                    _heartbeatTimer = null;
                    _winKeyWaiter = null;
                    _thread = null;
                    _dispatcher = null;
                    _window = null;
                    _helper = null;
                    _bridge = null;
                    Interlocked.Exchange(ref _scanReady, 0);
                }
            }
        }

        private static void JoinThread(Thread? thread, string name, int timeoutMs)
        {
            if (thread == null || thread == Thread.CurrentThread)
            {
                return;
            }
            try
            {
                if (thread.IsAlive && !thread.Join(timeoutMs))
                {
                    Debug.WriteLine($"StartMenu cleanup: thread {name} non terminato");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartMenu cleanup {name}: {ex.Message}");
            }
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
            try
            {
                d.BeginInvoke(new Action(() =>
                {
                    try
                    {
                        if (w.IsMenuVisible)
                        {
                            w.Dismiss();
                        }
                        else
                        {
                            ShowCore(w);
                        }
                    }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"StartMenu toggle: {ex}");
                    }
                }));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartMenu toggle dispatch: {ex}");
            }
        }

        public static void Show()
        {
            Dispatcher? d = _dispatcher;
            StartMenuWindow? w = _window;
            if (d == null || w == null)
            {
                return;
            }
            try
            {
                d.BeginInvoke(new Action(() =>
                {
                    try { ShowCore(w); }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"StartMenu show: {ex}");
                    }
                }));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartMenu show dispatch: {ex}");
            }
        }

        public static void Hide()
        {
            Dispatcher? d = _dispatcher;
            StartMenuWindow? w = _window;
            if (d == null || w == null)
            {
                return;
            }
            try
            {
                d.BeginInvoke(new Action(() =>
                {
                    try { w.Dismiss(); }
                    catch (Exception ex)
                    {
                        Debug.WriteLine($"StartMenu hide: {ex}");
                    }
                }));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartMenu hide dispatch: {ex}");
            }
        }

        private static void ShowCore(StartMenuWindow w)
        {
            try
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
            catch (Exception)
            {
            }
        }

        internal static void NotifyVisible(bool visible)
        {
            try
            {
                MenuVisibilityChanged?.Invoke(visible);
            }
            catch (Exception)
            {
            }
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
