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
                SendMessage(_hwnd, TTM_TRACKPOSITION, IntPtr.Zero,
                    (IntPtr)((screenY << 16) | (screenX & 0xFFFF)));
                SendMessage(_hwnd, TTM_TRACKACTIVATE, (IntPtr)1, ref ti);
                _shown = true;
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
            SendMessage(_hwnd, TTM_SETDELAYTIME, (IntPtr)TTDT_AUTOPOP, (IntPtr)10000);
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
    }
}
