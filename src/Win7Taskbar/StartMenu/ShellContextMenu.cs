// Win7Taskbar - documented IContextMenu for a filesystem path
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. Public shell APIs only. No Open-Shell source copied.

using System;
using System.Runtime.InteropServices;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    /// <summary>
    /// Real shell context menu (IContextMenu + TrackPopupMenu) for a
    /// file, shortcut or folder. Returns true when a verb ran.
    /// </summary>
    internal static class ShellContextMenu
    {
        private const uint CMF_NORMAL = 0x00000000;
        private const uint CMF_EXTENDEDVERBS = 0x00000100;
        private const uint TPM_LEFTALIGN = 0x0000;
        private const uint TPM_RIGHTBUTTON = 0x0002;
        private const uint TPM_RETURNCMD = 0x0100;
        private const uint MF_BYPOSITION = 0x0400;
        private const uint CMIC_MASK_UNICODE = 0x00004000;
        private const uint CMIC_MASK_PTINVOKE = 0x20000000;
        private const int SW_SHOWNORMAL = 1;
        private const uint idCmdFirst = 1;
        private const uint idCmdLast = 0x7FFF;

        public static bool TryShow(string path, int screenX, int screenY)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }

            IntPtr pidl = IntPtr.Zero;
            IntPtr menu = IntPtr.Zero;
            object? unk = null;
            try
            {
                int hr = NativeMethods.SHParseDisplayName(path, IntPtr.Zero,
                    out pidl, 0, IntPtr.Zero);
                if (hr != 0 || pidl == IntPtr.Zero)
                {
                    return false;
                }

                Guid iidFolder = new("000214E6-0000-0000-C000-000000000046");
                hr = SHBindToParent(pidl, ref iidFolder, out IntPtr folderPtr,
                    out IntPtr child);
                if (hr != 0 || folderPtr == IntPtr.Zero || child == IntPtr.Zero)
                {
                    return false;
                }

                try
                {
                    var folder = (IShellFolder)Marshal.GetObjectForIUnknown(folderPtr);
                    Guid iidMenu = new("000214E4-0000-0000-C000-000000000046");
                    IntPtr[] apidl = { child };
                    hr = folder.GetUIObjectOf(IntPtr.Zero, 1, apidl, ref iidMenu,
                        IntPtr.Zero, out unk);
                    if (hr != 0 || unk == null)
                    {
                        return false;
                    }

                    var ctx = (IContextMenu)unk;
                    menu = CreatePopupMenu();
                    if (menu == IntPtr.Zero)
                    {
                        return false;
                    }

                    uint flags = CMF_NORMAL;
                    try
                    {
                        if ((GetKeyState(0x10) & 0x8000) != 0)
                        {
                            flags |= CMF_EXTENDEDVERBS;
                        }
                    }
                    catch (Exception)
                    {
                    }

                    hr = ctx.QueryContextMenu(menu, 0, idCmdFirst, idCmdLast, flags);
                    if (hr < 0 || GetMenuItemCount(menu) <= 0)
                    {
                        return false;
                    }

                    uint cmd = TrackPopupMenuEx(menu,
                        TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
                        screenX, screenY, GetDesktopWindow(), IntPtr.Zero);
                    if (cmd == 0)
                    {
                        return false;
                    }

                    var info = new CMINVOKECOMMANDINFOEX
                    {
                        cbSize = Marshal.SizeOf<CMINVOKECOMMANDINFOEX>(),
                        fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE,
                        hwnd = IntPtr.Zero,
                        lpVerb = (IntPtr)(cmd - idCmdFirst),
                        lpVerbW = (IntPtr)(cmd - idCmdFirst),
                        nShow = SW_SHOWNORMAL,
                        ptInvokeX = screenX,
                        ptInvokeY = screenY
                    };
                    hr = ctx.InvokeCommand(ref info);
                    return hr >= 0;
                }
                finally
                {
                    Marshal.Release(folderPtr);
                }
            }
            catch (Exception)
            {
                return false;
            }
            finally
            {
                if (menu != IntPtr.Zero)
                {
                    DestroyMenu(menu);
                }
                if (unk != null)
                {
                    try { Marshal.ReleaseComObject(unk); } catch (Exception) { }
                }
                if (pidl != IntPtr.Zero)
                {
                    NativeMethods.ILFree(pidl);
                }
            }
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214E6-0000-0000-C000-000000000046")]
        private interface IShellFolder
        {
            void ParseDisplayName(IntPtr hwnd, IntPtr pbc, [MarshalAs(UnmanagedType.LPWStr)] string pszDisplayName,
                out uint pchEaten, out IntPtr ppidl, ref uint pdwAttributes);
            void EnumObjects(IntPtr hwnd, uint grfFlags, out IntPtr ppenumIDList);
            void BindToObject(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            void BindToStorage(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int CompareIDs(IntPtr lParam, IntPtr pidl1, IntPtr pidl2);
            void CreateViewObject(IntPtr hwndOwner, ref Guid riid, out IntPtr ppv);
            void GetAttributesOf(uint cidl, IntPtr apidl, ref uint rgfInOut);
            [PreserveSig]
            int GetUIObjectOf(IntPtr hwndOwner, uint cidl,
                [In, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 1)] IntPtr[] apidl,
                ref Guid riid, IntPtr rgfReserved,
                [MarshalAs(UnmanagedType.Interface)] out object ppv);
            void GetDisplayNameOf(IntPtr pidl, uint uFlags, IntPtr pName);
            void SetNameOf(IntPtr hwnd, IntPtr pidl, [MarshalAs(UnmanagedType.LPWStr)] string pszName,
                uint uFlags, out IntPtr ppidlOut);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214E4-0000-0000-C000-000000000046")]
        private interface IContextMenu
        {
            [PreserveSig]
            int QueryContextMenu(IntPtr hmenu, uint indexMenu, uint idCmdFirst,
                uint idCmdLast, uint uFlags);
            [PreserveSig]
            int InvokeCommand(ref CMINVOKECOMMANDINFOEX pici);
            [PreserveSig]
            int GetCommandString(UIntPtr idCmd, uint uType, IntPtr pReserved,
                IntPtr pszName, uint cchMax);
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct CMINVOKECOMMANDINFOEX
        {
            public int cbSize;
            public uint fMask;
            public IntPtr hwnd;
            public IntPtr lpVerb;
            public IntPtr lpParameters;
            public IntPtr lpDirectory;
            public int nShow;
            public uint dwHotKey;
            public IntPtr hIcon;
            public IntPtr lpTitle;
            public IntPtr lpVerbW;
            public IntPtr lpParametersW;
            public IntPtr lpDirectoryW;
            public IntPtr lpTitleW;
            public int ptInvokeX;
            public int ptInvokeY;
        }

        [DllImport("shell32.dll")]
        private static extern int SHBindToParent(IntPtr pidl, ref Guid riid,
            out IntPtr ppv, out IntPtr ppidlLast);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr CreatePopupMenu();

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyMenu(IntPtr hMenu);

        [DllImport("user32.dll")]
        private static extern int GetMenuItemCount(IntPtr hMenu);

        [DllImport("user32.dll")]
        private static extern uint TrackPopupMenuEx(IntPtr hMenu, uint uFlags,
            int x, int y, IntPtr hwnd, IntPtr lptpm);

        [DllImport("user32.dll")]
        private static extern IntPtr GetDesktopWindow();

        [DllImport("user32.dll")]
        private static extern short GetKeyState(int nVirtKey);
    }
}
