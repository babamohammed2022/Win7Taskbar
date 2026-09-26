// Win7Taskbar - low-level mouse hook for unlocked taskbar drag (RetroBar)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// WH_MOUSE_LL used while the user drags an unlocked taskbar. WPF mouse
    /// capture is unreliable on a WS_EX_NOACTIVATE bar once the pointer
    /// leaves it; RetroBar uses the same hook for that reason.
    /// </summary>
    internal sealed class TaskbarDragHook : IDisposable
    {
        private const int WhMouseLl = 14;
        private const int WmMouseMove = 0x0200;
        private const int WmLButtonUp = 0x0202;
        private const int WmRButtonDown = 0x0204;
        private const int WmMButtonDown = 0x0207;

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
        private struct Point
        {
            public int X;
            public int Y;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MsllHookStruct
        {
            public Point Pt;
            public uint MouseData;
            public uint Flags;
            public uint Time;
            public IntPtr ExtraInfo;
        }

        private readonly LowLevelMouseProc _proc;
        private IntPtr _hookId = IntPtr.Zero;

        public event Action<int, int>? Moved;
        public event Action? Released;

        public TaskbarDragHook()
        {
            _proc = HookCallback;
        }

        public bool IsActive => _hookId != IntPtr.Zero;

        public void Start()
        {
            if (_hookId != IntPtr.Zero)
            {
                return;
            }
            using Process process = Process.GetCurrentProcess();
            using ProcessModule module = process.MainModule!;
            _hookId = SetWindowsHookEx(WhMouseLl, _proc, GetModuleHandle(module.ModuleName), 0);
        }

        public void Stop()
        {
            if (_hookId == IntPtr.Zero)
            {
                return;
            }
            UnhookWindowsHookEx(_hookId);
            _hookId = IntPtr.Zero;
        }

        private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0)
            {
                int msg = wParam.ToInt32();
                if (msg == WmMouseMove)
                {
                    MsllHookStruct data = Marshal.PtrToStructure<MsllHookStruct>(lParam);
                    Moved?.Invoke(data.Pt.X, data.Pt.Y);
                }
                else if (msg == WmLButtonUp || msg == WmRButtonDown || msg == WmMButtonDown)
                {
                    Released?.Invoke();
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
