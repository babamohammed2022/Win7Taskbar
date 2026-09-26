// Win7Taskbar - hide Explorer's taskbar before Win7TaskbarCore.dll loads
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// RetroBar's ManagedShell hides the native bar from the App constructor,
// before theme/window. We cannot wait for OnLoaded: a clean first start
// spends seconds in ThemeLoader + Defender's first scan of Core.dll while
// Shell_TrayWnd is still on screen. This helper uses only user32/shell32
// (already mapped) so the hide does not wait for the native core.

using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace Win7Taskbar.Interop
{
    internal static class EarlyNativeTaskbarHide
    {
        private const uint SwpHideWindow = 0x0080;
        private const uint SwpShowWindow = 0x0040;
        private const uint SwpNoMove = 0x0002;
        private const uint SwpNoSize = 0x0001;
        private const uint SwpNoActivate = 0x0010;
        private static readonly IntPtr HwndBottom = new(1);

        private const uint AbmSetState = 0x0000000A;
        private const int AbsAutohide = 0x00000001;
        private const int AbsAlwaysOnTop = 0x00000002;

        private static volatile bool s_watch;
        private static Thread? s_thread;
        private static readonly object s_sync = new();

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after,
            string? className, string? windowName);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "FindWindowExW",
            SetLastError = true)]
        private static extern IntPtr FindWindowExAtom(IntPtr parent, IntPtr after,
            IntPtr classAtom, string? windowName);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);

        [DllImport("shell32.dll")]
        private static extern IntPtr SHAppBarMessage(uint dwMessage, ref AppBarData data);

        [StructLayout(LayoutKind.Sequential)]
        private struct Rect
        {
            public int Left, Top, Right, Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct AppBarData
        {
            public int cbSize;
            public IntPtr hWnd;
            public uint uCallbackMessage;
            public uint uEdge;
            public Rect rc;
            public IntPtr lParam;
        }

        /// <summary>
        /// Hide every foreign Shell_TrayWnd now, then re-hide every 100 ms
        /// until <see cref="Stop"/> (native HideWatcher takes over).
        /// </summary>
        public static void HideAndWatch()
        {
            long t0 = Environment.TickCount64;
            ApplyAll(hide: true, setAutohide: true);
            StartupGuard.Note(
                "early-hide user32 dt=" + (Environment.TickCount64 - t0) + "ms");

            lock (s_sync)
            {
                if (s_thread != null)
                {
                    return;
                }
                s_watch = true;
                s_thread = new Thread(WatchLoop)
                {
                    IsBackground = true,
                    Name = "early-native-hide"
                };
                s_thread.Start();
            }
        }

        /// <summary>Native watcher owns the hide; stop the bootstrap thread.</summary>
        public static void Stop()
        {
            s_watch = false;
        }

        /// <summary>Show Explorer's bar again (startup failure / close).</summary>
        public static void StopAndShow()
        {
            s_watch = false;
            ApplyAll(hide: false, setAutohide: false);
        }

        private static void WatchLoop()
        {
            while (s_watch)
            {
                Thread.Sleep(100);
                if (!s_watch)
                {
                    break;
                }
                ApplyAll(hide: true, setAutohide: false);
            }
        }

        private static void ApplyAll(bool hide, bool setAutohide)
        {
            try
            {
                uint ourPid = (uint)Environment.ProcessId;
                IntPtr tray = IntPtr.Zero;
                while ((tray = FindWindowExW(IntPtr.Zero, tray, "Shell_TrayWnd", null))
                       != IntPtr.Zero)
                {
                    GetWindowThreadProcessId(tray, out uint pid);
                    if (pid == 0 || pid == ourPid)
                    {
                        continue;
                    }
                    if (setAutohide)
                    {
                        SetState(tray, hide ? AbsAutohide : AbsAlwaysOnTop);
                    }
                    else if (!hide)
                    {
                        SetState(tray, AbsAlwaysOnTop);
                    }
                    Apply(tray, hide);
                }

                IntPtr secondary = IntPtr.Zero;
                while ((secondary = FindWindowExW(IntPtr.Zero, secondary,
                           "Shell_SecondaryTrayWnd", null)) != IntPtr.Zero)
                {
                    Apply(secondary, hide);
                }

                IntPtr start = FindWindowExAtom(IntPtr.Zero, IntPtr.Zero,
                    new IntPtr(0xC017), null);
                if (start != IntPtr.Zero)
                {
                    Apply(start, hide);
                }
            }
            catch (Exception)
            {
            }
        }

        private static void SetState(IntPtr tray, int state)
        {
            try
            {
                var abd = new AppBarData
                {
                    cbSize = Marshal.SizeOf<AppBarData>(),
                    hWnd = tray,
                    lParam = new IntPtr(state)
                };
                SHAppBarMessage(AbmSetState, ref abd);
            }
            catch (Exception)
            {
            }
        }

        private static void Apply(IntPtr hwnd, bool hide)
        {
            if (hwnd == IntPtr.Zero)
            {
                return;
            }
            uint swp = hide ? SwpHideWindow : SwpShowWindow;
            NativeMethods.SetWindowPos(hwnd, HwndBottom, 0, 0, 0, 0,
                swp | SwpNoMove | SwpNoSize | SwpNoActivate);
        }
    }
}
