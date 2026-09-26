// Win7Taskbar - shared task model: ghost/hung window handling
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// Ghost/hung window handling for task identity.
    ///
    /// When an application hangs, Windows shows a "ghost" of its window: the
    /// handle the system reports belongs to the ghost, not to the hung window.
    /// The ghost handle stays the identity of the task (it is what activation
    /// and matching must use), while queries (title, icon, process) go through
    /// the real window behind it.
    ///
    /// IsHungAppWindow is a documented API. HungWindowFromGhostWindow is an
    /// optional user32 export and is resolved dynamically: when it is absent
    /// this helper degrades to detection only (the ghost handle keeps the
    /// identity and hung windows are simply not queried).
    /// </summary>
    internal static class GhostHandle
    {
        /// <summary>Window class Windows uses for hung-window ghosts.</summary>
        private const string GhostWindowClass = "Ghost";

        private delegate nint HungFromGhostProc(nint hwnd);

        private static readonly HungFromGhostProc? s_hungFromGhost;

        static GhostHandle()
        {
            try
            {
                nint user32 = GetModuleHandleW("user32.dll");
                if (user32 != 0)
                {
                    nint proc = GetProcAddress(user32, "HungWindowFromGhostWindow");
                    if (proc != 0)
                    {
                        s_hungFromGhost =
                            Marshal.GetDelegateForFunctionPointer<HungFromGhostProc>(proc);
                    }
                }
            }
            catch
            {
                // Detection below keeps working without the export.
                s_hungFromGhost = null;
            }
        }

        /// <summary>True when the optional export is available on this system.</summary>
        public static bool CanResolveGhosts => s_hungFromGhost != null;

        /// <summary>
        /// True when the handle is a hung-window ghost (observable window class).
        /// </summary>
        public static bool IsGhostWindow(ulong hwnd)
        {
            if (hwnd == 0)
            {
                return false;
            }

            try
            {
                var name = new StringBuilder(32);
                int len = GetClassNameW(unchecked((nint)hwnd), name, name.Capacity);
                return len == GhostWindowClass.Length &&
                       string.Equals(name.ToString(), GhostWindowClass,
                                     StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Resolves the hung window behind a ghost handle. False when the
        /// handle is not a ghost, the export is missing or the lookup failed:
        /// the caller keeps the original handle as the query handle and skips
        /// the queries that would block.
        /// </summary>
        public static bool TryResolve(ulong hwnd, out ulong realHwnd)
        {
            realHwnd = 0;
            if (hwnd == 0 || s_hungFromGhost == null)
            {
                return false;
            }

            try
            {
                nint real = s_hungFromGhost(unchecked((nint)hwnd));
                if (real == 0 || real == unchecked((nint)hwnd))
                {
                    return false;
                }

                realHwnd = unchecked((ulong)real);
                return true;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>True when the window does not answer messages (documented API).</summary>
        public static bool IsHung(ulong hwnd)
        {
            if (hwnd == 0)
            {
                return false;
            }

            try
            {
                return IsHungAppWindow(unchecked((nint)hwnd));
            }
            catch
            {
                return false;
            }
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode, BestFitMapping = false)]
        private static extern int GetClassNameW(nint hWnd, StringBuilder lpClassName,
                                                int nMaxCount);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsHungAppWindow(nint hwnd);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, BestFitMapping = false)]
        private static extern nint GetModuleHandleW(string? lpModuleName);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, BestFitMapping = false,
                   EntryPoint = "GetProcAddress")]
        private static extern nint GetProcAddress(nint hModule, string procName);
    }
}
