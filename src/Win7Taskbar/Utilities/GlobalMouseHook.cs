// Win7Taskbar - Global mouse hook for click-outside detection
// English: Detects clicks outside flyouts to close them (clock, calendar)
// Italiano: Rileva click fuori dai flyout per chiuderli
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Low-level mouse hook to detect clicks anywhere on screen.
    /// Used to close clock flyout when clicking outside.
    /// </summary>
    public class GlobalMouseHook : IDisposable
    {
        private const int WH_MOUSE_LL = 14;
        private const int WM_MOUSEMOVE = 0x0200;
        private const int WM_LBUTTONDOWN = 0x0201;
        private const int WM_LBUTTONUP = 0x0202;
        private const int WM_RBUTTONDOWN = 0x0204;
        private const int WM_RBUTTONUP = 0x0205;
        private const int WM_MBUTTONDOWN = 0x0207;
        private const int WM_MBUTTONUP = 0x0208;
        private const int WM_XBUTTONDOWN = 0x020B;
        private const int WM_XBUTTONUP = 0x020C;

        [DllImport("user32.dll")]
        private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelMouseProc lpfn, IntPtr hMod, uint dwThreadId);

        [DllImport("user32.dll")]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);

        [DllImport("user32.dll")]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetModuleHandle(string lpModuleName);

        private delegate IntPtr LowLevelMouseProc(int nCode, IntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT
        {
            public int x;
            public int y;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MSLLHOOKSTRUCT
        {
            public POINT pt;
            public uint mouseData;
            public uint flags;
            public uint time;
            public IntPtr dwExtraInfo;
        }

        private IntPtr _hookId = IntPtr.Zero;
        private LowLevelMouseProc _proc;

        public event EventHandler<Point>? MouseDownOutside;

        /// <summary>
        /// Pointer movement anywhere on screen (screen pixels). Only
        /// marshalled to the UI thread while somebody listens, so the
        /// existing click-outside users pay nothing for it. Used by the
        /// unlocked-taskbar edge drag (RetroBar's drag hook pattern).
        /// </summary>
        public event EventHandler<Point>? MouseMove;

        /// <summary>Any mouse button released or pressed (screen pixels);
        /// the edge drag ends on the first of these.</summary>
        public event EventHandler<Point>? MouseButtonChanged;

        // Rect to exclude (flyout bounds)
        public Rect ExcludeRect { get; set; }
        public Rect ExcludeRect2 { get; set; } // e.g., clock host

        public GlobalMouseHook()
        {
            _proc = HookCallback;
        }

        public void Start()
        {
            if (_hookId != IntPtr.Zero) return;
            using (Process curProcess = Process.GetCurrentProcess())
            using (ProcessModule curModule = curProcess.MainModule!)
            {
                _hookId = SetWindowsHookEx(WH_MOUSE_LL, _proc, GetModuleHandle(curModule.ModuleName), 0);
            }
        }

        public void Stop()
        {
            if (_hookId != IntPtr.Zero)
            {
                UnhookWindowsHookEx(_hookId);
                _hookId = IntPtr.Zero;
            }
        }

        private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0)
            {
                int msg = wParam.ToInt32();
                try
                {
                    if (msg == WM_MOUSEMOVE && MouseMove != null)
                    {
                        MSLLHOOKSTRUCT move = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                        Point movePt = new Point(move.pt.x, move.pt.y);
                        Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
                        {
                            MouseMove?.Invoke(this, movePt);
                        }));
                    }
                    else if (MouseButtonChanged != null &&
                             (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP ||
                              msg == WM_XBUTTONUP || msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                              msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN))
                    {
                        MSLLHOOKSTRUCT btn = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                        Point btnPt = new Point(btn.pt.x, btn.pt.y);
                        Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
                        {
                            MouseButtonChanged?.Invoke(this, btnPt);
                        }));
                    }
                }
                catch (Exception ex)
                {
                    /* A hook callback must never throw back into user32. */
                    Debug.WriteLine($"[Win7Taskbar] mouse hook: {ex.Message}");
                }
                if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
                {
                    MSLLHOOKSTRUCT hookStruct = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                    Point pt = new Point(hookStruct.pt.x, hookStruct.pt.y);

                    bool insideExclude = false;
                    if (!ExcludeRect.IsEmpty && ExcludeRect.Contains(pt))
                        insideExclude = true;
                    if (!ExcludeRect2.IsEmpty && ExcludeRect2.Contains(pt))
                        insideExclude = true;

                    if (!insideExclude)
                    {
                        // Raise on UI thread
                        Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
                        {
                            MouseDownOutside?.Invoke(this, pt);
                        }));
                    }
                }
            }
            return CallNextHookEx(_hookId, nCode, wParam, lParam);
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
