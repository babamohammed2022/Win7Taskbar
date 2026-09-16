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
        private const int WM_LBUTTONDOWN = 0x0201;
        private const int WM_RBUTTONDOWN = 0x0204;
        private const int WM_MBUTTONDOWN = 0x0207;

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
