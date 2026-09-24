// Win7Taskbar - Win32 TOOLTIPS_CLASS infotip (connection-flyout style)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Documented commctrl TOOLTIPS_CLASS / TTM_ADDTOOL / TTM_TRACKACTIVATE /
// TTM_SETTITLE. Same control the Win7 network connection flyout uses
// (native/src/Win7NetworkFlyout.cpp). No WPF ToolTip. No Open-Shell source.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Win7Taskbar.StartMenu
{
    internal sealed class StartMenuWin32Tooltip : IDisposable
    {
        private const int WS_POPUP = unchecked((int)0x80000000);
        private const int WS_EX_TOPMOST = 0x00000008;
        private const int WS_EX_LAYERED = 0x00080000;
        private const int WS_EX_TRANSPARENT = 0x00000020;
        private const int WS_EX_TOOLWINDOW = 0x00000080;
        private const int WS_EX_NOACTIVATE = unchecked((int)0x08000000);
        private const int SWP_NOSIZE = 0x0001;
        private const int SWP_NOZORDER = 0x0004;
        private const int SWP_NOACTIVATE = 0x0010;
        private const int MONITOR_DEFAULTTONEAREST = 2;
        private const int SM_CXCURSOR = 13;
        private const int SM_CYCURSOR = 14;
        private const int TTS_ALWAYSTIP = 0x01;
        private const int TTS_NOPREFIX = 0x02;
        private const int TTF_IDISHWND = 0x0001;
        private const int TTF_TRACK = 0x0020;
        private const int TTF_ABSOLUTE = 0x0080;
        private const int TTF_TRANSPARENT = 0x0100;
        private const uint WM_USER = 0x0400;
        private const uint TTM_ADDTOOLW = WM_USER + 50;
        private const uint TTM_DELTOOLW = WM_USER + 51;
        private const uint TTM_TRACKACTIVATE = WM_USER + 17;
        private const uint TTM_TRACKPOSITION = WM_USER + 18;
        private const uint TTM_SETMAXTIPWIDTH = WM_USER + 24;
        private const uint TTM_SETTITLEW = WM_USER + 33;
        private const uint TTM_SETDELAYTIME = WM_USER + 3;
        private const int TTDT_INITIAL = 3;
        private const int TTDT_AUTOPOP = 2;
        private const int TTDT_RESHOW = 1;
        private const uint ICC_WIN95_CLASSES = 0x000000FF;

        private IntPtr _hwnd;
        private IntPtr _owner;
        private bool _shown;
        private bool _disposed;

        public void Show(IntPtr owner, string? title, string? text, int screenX, int screenY)
        {
            if (_disposed || string.IsNullOrWhiteSpace(text) || owner == IntPtr.Zero)
            {
                Hide();
                return;
            }
            try
            {
                EnsureWindow(owner);
                if (_hwnd == IntPtr.Zero)
                {
                    return;
                }
                Hide();
                string body = text.Trim();
                string head = string.IsNullOrWhiteSpace(title) ? string.Empty : title.Trim();
                var ti = MakeInfo(body);
                if (SendMessage(_hwnd, TTM_ADDTOOLW, IntPtr.Zero, ref ti) == IntPtr.Zero)
                {
                    return;
                }
                if (!string.IsNullOrEmpty(head))
                {
                    SendMessage(_hwnd, TTM_SETTITLEW, (IntPtr)0, head);
                }
                Clearance(out int padX, out int padY);
                int x = screenX + padX;
                int y = screenY + padY;
                SendMessage(_hwnd, TTM_TRACKPOSITION, IntPtr.Zero, PackPoint(x, y));
                SendMessage(_hwnd, TTM_TRACKACTIVATE, (IntPtr)1, ref ti);
                _shown = true;
                NudgeOffCursor(screenX, screenY);
            }
            catch (Exception)
            {
                Hide();
            }
        }

        public void Hide()
        {
            if (_hwnd == IntPtr.Zero || !_shown)
            {
                _shown = false;
                return;
            }
            try
            {
                var ti = MakeInfo(string.Empty);
                SendMessage(_hwnd, TTM_TRACKACTIVATE, IntPtr.Zero, ref ti);
                SendMessage(_hwnd, TTM_DELTOOLW, IntPtr.Zero, ref ti);
            }
            catch (Exception)
            {
            }
            _shown = false;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            Hide();
            if (_hwnd != IntPtr.Zero)
            {
                try { DestroyWindow(_hwnd); } catch (Exception) { }
                _hwnd = IntPtr.Zero;
            }
        }

        private void EnsureWindow(IntPtr owner)
        {
            if (_hwnd != IntPtr.Zero && IsWindow(_hwnd))
            {
                return;
            }
            _hwnd = IntPtr.Zero;
            _owner = owner;
            var icc = new INITCOMMONCONTROLSEX
            {
                dwSize = Marshal.SizeOf<INITCOMMONCONTROLSEX>(),
                dwICC = ICC_WIN95_CLASSES
            };
            InitCommonControlsEx(ref icc);
            _hwnd = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_LAYERED,
                "tooltips_class32",
                string.Empty,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                0, 0, 0, 0,
                owner, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
            if (_hwnd == IntPtr.Zero)
            {
                return;
            }
            SendMessage(_hwnd, TTM_SETMAXTIPWIDTH, IntPtr.Zero, (IntPtr)300);
            SendMessage(_hwnd, TTM_SETDELAYTIME, (IntPtr)TTDT_INITIAL, (IntPtr)500);
            SendMessage(_hwnd, TTM_SETDELAYTIME, (IntPtr)TTDT_AUTOPOP, (IntPtr)32767);
            SendMessage(_hwnd, TTM_SETDELAYTIME, (IntPtr)TTDT_RESHOW, (IntPtr)100);
        }

        private TOOLINFOW MakeInfo(string text)
        {
            return new TOOLINFOW
            {
                cbSize = Marshal.SizeOf<TOOLINFOW>(),
                uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT | TTF_IDISHWND,
                hwnd = _owner,
                uId = _owner,
                lpszText = text ?? string.Empty
            };
        }

        private static IntPtr PackPoint(int x, int y)
        {
            return (IntPtr)(unchecked((uint)(ushort)x) | (unchecked((uint)(ushort)y) << 16));
        }

        /* The cursor glyph (typically 32 px) plus a gap so the tip never
         * sits on the hotspot. Sitting on the hotspot is what tilts the
         * flyout: the mouse then leaves the row, the tip fights the
         * pointer, and it looks like it is leaning. */
        private static void Clearance(out int padX, out int padY)
        {
            int cx = 32;
            int cy = 32;
            try
            {
                int mx = GetSystemMetrics(SM_CXCURSOR);
                int my = GetSystemMetrics(SM_CYCURSOR);
                if (mx > 0) cx = mx;
                if (my > 0) cy = my;
            }
            catch (Exception)
            {
            }
            padX = cx + 20;
            padY = cy + 24;
        }

        private void NudgeOffCursor(int cursorX, int cursorY)
        {
            if (_hwnd == IntPtr.Zero)
            {
                return;
            }
            try
            {
                if (!GetWindowRect(_hwnd, out RECT tip) ||
                    tip.right <= tip.left || tip.bottom <= tip.top)
                {
                    return;
                }
                int w = tip.right - tip.left;
                int h = tip.bottom - tip.top;
                int x = tip.left;
                int y = tip.top;
                if (cursorX >= x && cursorX < x + w &&
                    cursorY >= y && cursorY < y + h)
                {
                    Clearance(out int padX, out int padY);
                    x = cursorX + padX;
                    y = cursorY + padY;
                }
                var pt = new POINT { x = cursorX, y = cursorY };
                IntPtr mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
                var mi = new MONITORINFO { cbSize = Marshal.SizeOf<MONITORINFO>() };
                if (mon != IntPtr.Zero && GetMonitorInfoW(mon, ref mi))
                {
                    RECT wa = mi.rcWork;
                    if (x + w > wa.right) x = wa.right - w;
                    if (y + h > wa.bottom) y = wa.bottom - h;
                    if (x < wa.left) x = wa.left;
                    if (y < wa.top) y = wa.top;
                    if (cursorX >= x && cursorX < x + w &&
                        cursorY >= y && cursorY < y + h)
                    {
                        if (cursorX - wa.left > wa.right - cursorX)
                        {
                            x = cursorX - w - 8;
                        }
                        else
                        {
                            x = cursorX + 8;
                        }
                        if (x + w > wa.right) x = wa.right - w;
                        if (x < wa.left) x = wa.left;
                    }
                }
                if (x != tip.left || y != tip.top)
                {
                    SetWindowPos(_hwnd, IntPtr.Zero, x, y, 0, 0,
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            catch (Exception)
            {
            }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct INITCOMMONCONTROLSEX
        {
            public int dwSize;
            public uint dwICC;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct TOOLINFOW
        {
            public int cbSize;
            public int uFlags;
            public IntPtr hwnd;
            public IntPtr uId;
            public RECT rect;
            public IntPtr hinst;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string lpszText;
            public IntPtr lParam;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT
        {
            public int left, top, right, bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT
        {
            public int x, y;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct MONITORINFO
        {
            public int cbSize;
            public RECT rcMonitor;
            public RECT rcWork;
            public int dwFlags;
        }

        [DllImport("comctl32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool InitCommonControlsEx(ref INITCOMMONCONTROLSEX picce);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateWindowExW(
            int dwExStyle, string lpClassName, string lpWindowName, int dwStyle,
            int x, int y, int nWidth, int nHeight,
            IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWindow(IntPtr hWnd);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, ref TOOLINFOW lParam);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, string lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string? lpModuleName);

        [DllImport("user32.dll")]
        private static extern int GetSystemMetrics(int nIndex);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetWindowPos(
            IntPtr hWnd, IntPtr hWndInsertAfter, int x, int y, int cx, int cy, uint uFlags);

        [DllImport("user32.dll")]
        private static extern IntPtr MonitorFromPoint(POINT pt, int dwFlags);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetMonitorInfoW(IntPtr hMonitor, ref MONITORINFO lpmi);
    }
}
