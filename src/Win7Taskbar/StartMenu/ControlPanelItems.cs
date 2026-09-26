// Win7Taskbar - All Control Panel Items (shell namespace, not .cpl enum)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// CLSID_ControlPanel {21EC2020-3AEA-1069-A2DD-08002B30309D} via
// SHGetDesktopFolder → BindToObject → IShellFolder::EnumObjects, as used
// by Explorer and Open-Shell. Names and icons come from the shell so the
// list is localized and identical on Windows 7, 10 and 11.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    internal sealed class ControlPanelItem
    {
        public string Name { get; init; } = string.Empty;
        public string ParsingName { get; init; } = string.Empty;
        public ImageSource? Icon { get; init; }
        public IntPtr Pidl { get; init; }
    }

    internal static class ControlPanelItems
    {
        /* "All Control Panel Items" — stable from XP through Windows 11. */
        private const string kAllItems =
            "shell:::{21EC2020-3AEA-1069-A2DD-08002B30309D}";

        private const uint ShcontfFolders = 0x0020;
        private const uint ShcontfNonfolders = 0x0040;
        private const uint ShcontfIncludeHidden = 0x0080;
        private const uint ShgdnNormal = 0x0000;
        private const uint ShgdnForParsing = 0x8000;
        private const uint ShgfiPidl = 0x000000008;
        private const uint ShgfiIcon = 0x000000100;
        private const uint ShgfiSmallIcon = 0x000000001;
        private const uint ShgfiDisplayName = 0x000000200;
        private const uint SeeMaskIdList = 0x00000004;
        private const uint SeeMaskInvokeIdList = 0x0000000C;

        private static readonly Guid IidShellFolder =
            new("000214E6-0000-0000-C000-000000000046");

        public static List<ControlPanelItem> Enumerate()
        {
            var list = new List<ControlPanelItem>();
            IntPtr desktopPtr = IntPtr.Zero;
            IntPtr folderPidl = IntPtr.Zero;
            IntPtr folderPtr = IntPtr.Zero;
            IntPtr enumPtr = IntPtr.Zero;
            try
            {
                if (SHGetDesktopFolder(out desktopPtr) != 0 || desktopPtr == IntPtr.Zero)
                {
                    return list;
                }
                var desktop = (IShellFolder)Marshal.GetObjectForIUnknown(desktopPtr);
                if (NativeMethods.SHParseDisplayName(kAllItems, IntPtr.Zero,
                        out folderPidl, 0, IntPtr.Zero) != 0 ||
                    folderPidl == IntPtr.Zero)
                {
                    return list;
                }
                Guid iid = IidShellFolder;
                desktop.BindToObject(folderPidl, IntPtr.Zero, ref iid, out folderPtr);
                if (folderPtr == IntPtr.Zero)
                {
                    return list;
                }
                var folder = (IShellFolder)Marshal.GetObjectForIUnknown(folderPtr);
                folder.EnumObjects(IntPtr.Zero,
                    ShcontfFolders | ShcontfNonfolders | ShcontfIncludeHidden,
                    out enumPtr);
                if (enumPtr == IntPtr.Zero)
                {
                    return list;
                }
                var enumerator = (IEnumIDList)Marshal.GetObjectForIUnknown(enumPtr);
                while (list.Count < 256)
                {
                    IntPtr child = IntPtr.Zero;
                    uint fetched = 0;
                    if (enumerator.Next(1, out child, out fetched) != 0 ||
                        fetched == 0 || child == IntPtr.Zero)
                    {
                        break;
                    }
                    try
                    {
                        ControlPanelItem? item = FromChild(folder, folderPidl, child);
                        if (item != null)
                        {
                            list.Add(item);
                        }
                    }
                    finally
                    {
                        NativeMethods.ILFree(child);
                    }
                }

                list.Sort((a, b) => StrCmpLogicalW(a.Name, b.Name));
            }
            catch (Exception)
            {
            }
            finally
            {
                if (enumPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(enumPtr); } catch (Exception) { }
                }
                if (folderPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(folderPtr); } catch (Exception) { }
                }
                if (folderPidl != IntPtr.Zero)
                {
                    NativeMethods.ILFree(folderPidl);
                }
                if (desktopPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(desktopPtr); } catch (Exception) { }
                }
            }
            return list;
        }

        public static bool Launch(ControlPanelItem item)
        {
            if (item == null)
            {
                return false;
            }
            try
            {
                if (item.Pidl != IntPtr.Zero)
                {
                    var info = new NativeMethods.SHELLEXECUTEINFO
                    {
                        cbSize = Marshal.SizeOf<NativeMethods.SHELLEXECUTEINFO>(),
                        fMask = SeeMaskIdList | SeeMaskInvokeIdList,
                        lpIDList = item.Pidl,
                        nShow = NativeMethods.SW_SHOWNORMAL
                    };
                    if (NativeMethods.ShellExecuteExW(ref info))
                    {
                        return true;
                    }
                }
                if (!string.IsNullOrWhiteSpace(item.ParsingName))
                {
                    var psi = new System.Diagnostics.ProcessStartInfo
                    {
                        FileName = item.ParsingName,
                        UseShellExecute = true
                    };
                    System.Diagnostics.Process.Start(psi);
                    return true;
                }
            }
            catch (Exception)
            {
            }
            return false;
        }

        public static void Free(IEnumerable<ControlPanelItem> items)
        {
            if (items == null)
            {
                return;
            }
            foreach (ControlPanelItem item in items)
            {
                if (item.Pidl != IntPtr.Zero)
                {
                    NativeMethods.ILFree(item.Pidl);
                }
            }
        }

        private static ControlPanelItem? FromChild(IShellFolder folder,
            IntPtr folderPidl, IntPtr child)
        {
            string name = DisplayName(folder, child, ShgdnNormal);
            if (string.IsNullOrWhiteSpace(name))
            {
                name = DisplayName(folder, child, ShgdnForParsing);
            }
            if (string.IsNullOrWhiteSpace(name))
            {
                return null;
            }
            IntPtr full = ILCombine(folderPidl, child);
            string parse = DisplayName(folder, child, ShgdnForParsing);
            if (string.IsNullOrWhiteSpace(parse) && full != IntPtr.Zero)
            {
                parse = kAllItems + "\\" + name;
            }
            ImageSource? icon = IconFromPidl(full != IntPtr.Zero ? full : child);
            return new ControlPanelItem
            {
                Name = name.Trim(),
                ParsingName = parse ?? string.Empty,
                Icon = icon,
                Pidl = full
            };
        }

        private static string DisplayName(IShellFolder folder, IntPtr pidl, uint flags)
        {
            IntPtr pName = Marshal.AllocHGlobal(520);
            try
            {
                folder.GetDisplayNameOf(pidl, flags, pName);
                var buf = new StringBuilder(260);
                if (StrRetToBufW(pName, pidl, buf, (uint)buf.Capacity) == 0)
                {
                    return buf.ToString();
                }
            }
            catch (Exception)
            {
            }
            finally
            {
                Marshal.FreeHGlobal(pName);
            }
            return string.Empty;
        }

        private static ImageSource? IconFromPidl(IntPtr pidl)
        {
            if (pidl == IntPtr.Zero)
            {
                return null;
            }
            var info = new NativeMethods.SHFILEINFOW();
            IntPtr result = NativeMethods.SHGetFileInfoPidl(pidl, 0, ref info,
                (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                ShgfiPidl | ShgfiIcon | ShgfiSmallIcon | ShgfiDisplayName);
            if (result == IntPtr.Zero || info.hIcon == IntPtr.Zero)
            {
                return StartMenuIcons.FromDll("imageres.dll", 22, 16);
            }
            try
            {
                ImageSource src = Imaging.CreateBitmapSourceFromHIcon(
                    info.hIcon, Int32Rect.Empty, BitmapSizeOptions.FromEmptyOptions());
                src.Freeze();
                return src;
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                DestroyIcon(info.hIcon);
            }
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214E6-0000-0000-C000-000000000046")]
        private interface IShellFolder
        {
            void ParseDisplayName(IntPtr hwnd, IntPtr pbc,
                [MarshalAs(UnmanagedType.LPWStr)] string pszDisplayName,
                out uint pchEaten, out IntPtr ppidl, ref uint pdwAttributes);
            void EnumObjects(IntPtr hwnd, uint grfFlags, out IntPtr ppenumIDList);
            void BindToObject(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            void BindToStorage(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int CompareIDs(IntPtr lParam, IntPtr pidl1, IntPtr pidl2);
            void CreateViewObject(IntPtr hwndOwner, ref Guid riid, out IntPtr ppv);
            void GetAttributesOf(uint cidl, IntPtr apidl, ref uint rgfInOut);
            void GetUIObjectOf(IntPtr hwndOwner, uint cidl, IntPtr apidl,
                ref Guid riid, IntPtr rgfReserved, out IntPtr ppv);
            void GetDisplayNameOf(IntPtr pidl, uint uFlags, IntPtr pName);
            void SetNameOf(IntPtr hwnd, IntPtr pidl,
                [MarshalAs(UnmanagedType.LPWStr)] string pszName,
                uint uFlags, out IntPtr ppidlOut);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214F2-0000-0000-C000-000000000046")]
        private interface IEnumIDList
        {
            [PreserveSig] int Next(uint celt, out IntPtr rgelt, out uint pceltFetched);
            [PreserveSig] int Skip(uint celt);
            [PreserveSig] int Reset();
            [PreserveSig] int Clone(out IntPtr ppenum);
        }

        [DllImport("shell32.dll")]
        private static extern int SHGetDesktopFolder(out IntPtr ppshf);

        [DllImport("shell32.dll")]
        private static extern IntPtr ILCombine(IntPtr pidl1, IntPtr pidl2);

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int StrRetToBufW(IntPtr pstr, IntPtr pidl,
            StringBuilder pszBuf, uint cchBuf);

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int StrCmpLogicalW(string psz1, string psz2);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyIcon(IntPtr hIcon);
    }
}
