// Win7Taskbar - click-away for the Start Menu (own STA)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. Public Win32 only.
//
// WH_MOUSE_LL is installed on the Start Menu dispatcher thread so the
// callback is posted there (the Superbar hook uses Application.Current
// and cannot dismiss this window). The callback itself only copies the
// point and queues work; UI and Win32 queries run on the dispatcher.

using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    internal sealed class StartMenuClickAway : IDisposable
    {
        private const int WhMouseLl = 14;
        private const int WmLButtonDown = 0x0201;
        private const int WmRButtonDown = 0x0204;
        private const int WmMButtonDown = 0x0207;
        private const uint GaRoot = 2;

        private readonly Dispatcher _dispatcher;
        private readonly Func<IntPtr> _hwnd;
        private readonly Action _dismiss;
        private readonly LowLevelMouseProc _proc;
        private IntPtr _hook;
        private bool _disposed;

        public StartMenuClickAway(Dispatcher dispatcher, Func<IntPtr> hwnd, Action dismiss)
        {
            _dispatcher = dispatcher ?? throw new ArgumentNullException(nameof(dispatcher));
            _hwnd = hwnd ?? throw new ArgumentNullException(nameof(hwnd));
            _dismiss = dismiss ?? throw new ArgumentNullException(nameof(dismiss));
            _proc = HookCallback;
        }

        public void Start()
        {
            if (_hook != IntPtr.Zero)
            {
                return;
            }
            try
            {
                using Process proc = Process.GetCurrentProcess();
                using ProcessModule? mod = proc.MainModule;
                IntPtr module = mod != null
                    ? GetModuleHandle(mod.ModuleName)
                    : IntPtr.Zero;
                _hook = SetWindowsHookEx(WhMouseLl, _proc, module, 0);
            }
            catch (Exception)
            {
                _hook = IntPtr.Zero;
            }
        }

        public void Stop()
        {
            if (_hook == IntPtr.Zero)
            {
                return;
            }
            try
            {
                UnhookWindowsHookEx(_hook);
            }
            catch (Exception)
            {
            }
            _hook = IntPtr.Zero;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            Stop();
            GC.SuppressFinalize(this);
        }

        private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0)
            {
                int msg = wParam.ToInt32();
                if (msg == WmLButtonDown || msg == WmRButtonDown || msg == WmMButtonDown)
                {
                    MSLLHOOKSTRUCT info = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                    int x = info.pt.x;
                    int y = info.pt.y;
                    try
                    {
                        _dispatcher.BeginInvoke(DispatcherPriority.Input,
                            new Action(() => OnClick(x, y)));
                    }
                    catch (Exception)
                    {
                    }
                }
            }
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        private void OnClick(int x, int y)
        {
            try
            {
                IntPtr ours = _hwnd();
                if (ours == IntPtr.Zero)
                {
                    return;
                }
                var pt = new NativeMethods.POINT { x = x, y = y };
                IntPtr hit = NativeMethods.WindowFromPoint(pt);
                if (hit != IntPtr.Zero)
                {
                    IntPtr root = NativeMethods.GetAncestor(hit, GaRoot);
                    if (root == ours || hit == ours)
                    {
                        return;
                    }
                    var name = new StringBuilder(32);
                    if (GetClassNameW(hit, name, name.Capacity) > 0 &&
                        name.ToString() == "#32768")
                    {
                        return;
                    }
                    if (root != IntPtr.Zero &&
                        GetClassNameW(root, name, name.Capacity) > 0 &&
                        name.ToString() == "#32768")
                    {
                        return;
                    }
                }
                if (NativeMethods.GetWindowRect(ours, out NativeMethods.RECT rc) &&
                    x >= rc.Left && x < rc.Right && y >= rc.Top && y < rc.Bottom)
                {
                    return;
                }
                _dismiss();
            }
            catch (Exception)
            {
            }
        }

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

        [DllImport("user32.dll")]
        private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelMouseProc lpfn,
            IntPtr hMod, uint dwThreadId);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);

        [DllImport("user32.dll")]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode,
            IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string? lpModuleName);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName,
            int nMaxCount);
    }
}
