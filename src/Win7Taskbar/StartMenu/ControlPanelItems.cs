// Win7Taskbar - All Control Panel Items (shell namespace, not .cpl enum)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// CLSID_ControlPanel {21EC2020-3AEA-1069-A2DD-08002B30309D} via
// SHGetDesktopFolder → BindToObject → IShellFolder::EnumObjects, as used
// by Explorer and Open-Shell. Many applets are SFGAO_FOLDER (BitLocker,
// Sync Center, RemoteApp, …): they must stay in the list. Names that
// resolve only to ::{GUID} are recovered via IShellFolder2 / IShellItem
// / HKCR\CLSID, or dropped rather than shown raw.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
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
        private const string kAllItems =
            "shell:::{21EC2020-3AEA-1069-A2DD-08002B30309D}";
        /* Category-view "All Control Panel Items" (::{26EE0668}\0) and the
         * known-folder alias fill applets that Win10/11 omit from 21EC2020
         * alone. Deduped by display name. */
        private static readonly string[] kNamespaces =
        {
            "shell:::{21EC2020-3AEA-1069-A2DD-08002B30309D}",
            "shell:ControlPanelFolder",
            @"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}\0"
        };

        private const uint ShcontfFolders = 0x0020;
        private const uint ShcontfNonfolders = 0x0040;
        private const uint ShcontfIncludeHidden = 0x0080;
        private const uint ShcontfIncludeSuperHidden = 0x00010000;
        private const uint ShgdnNormal = 0x0000;
        private const uint ShgdnInFolder = 0x0001;
        private const uint ShgdnForParsing = 0x8000;
        private const uint ShgfiPidl = 0x000000008;
        private const uint ShgfiIcon = 0x000000100;
        private const uint ShgfiSmallIcon = 0x000000001;
        private const uint ShgfiDisplayName = 0x000000200;
        private const uint SeeMaskIdList = 0x00000004;
        private const uint SeeMaskInvokeIdList = 0x0000000C;
        private const uint SigdnNormalDisplay = 0;

        private static readonly Guid IidShellFolder =
            new("000214E6-0000-0000-C000-000000000046");
        private static readonly Guid IidShellFolder2 =
            new("93F2F68C-1D1B-11D3-A30E-00C04F79ABD1");
        private static readonly Guid IidShellItem =
            new("43826d1e-e718-42ee-bc55-a1e261c37bfe");
        private static readonly PROPERTYKEY PkeyItemNameDisplay = new()
        {
            fmtid = new Guid("B725F130-47EF-101A-A5F1-02608C9EEBAC"),
            pid = 10
        };
        private static readonly Regex GuidInText = new(
            @"\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}",
            RegexOptions.CultureInvariant | RegexOptions.Compiled);

        public static List<ControlPanelItem> Enumerate()
        {
            var list = new List<ControlPanelItem>();
            var seen = new HashSet<string>(StringComparer.CurrentCultureIgnoreCase);
            foreach (string ns in kNamespaces)
            {
                try
                {
                    EnumerateNamespace(ns, list, seen);
                }
                catch (Exception)
                {
                }
            }
            list.Sort((a, b) => StrCmpLogicalW(a.Name, b.Name));
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

        private static void EnumerateNamespace(string parsingName,
            List<ControlPanelItem> list, HashSet<string> seen)
        {
            IntPtr desktopPtr = IntPtr.Zero;
            IntPtr folderPidl = IntPtr.Zero;
            IntPtr folderPtr = IntPtr.Zero;
            IntPtr folder2Ptr = IntPtr.Zero;
            IntPtr enumPtr = IntPtr.Zero;
            try
            {
                if (SHGetDesktopFolder(out desktopPtr) != 0 || desktopPtr == IntPtr.Zero)
                {
                    return;
                }
                var desktop = (IShellFolder)Marshal.GetObjectForIUnknown(desktopPtr);
                if (NativeMethods.SHParseDisplayName(parsingName, IntPtr.Zero,
                        out folderPidl, 0, IntPtr.Zero) != 0 ||
                    folderPidl == IntPtr.Zero)
                {
                    return;
                }
                Guid iid = IidShellFolder;
                if (desktop.BindToObject(folderPidl, IntPtr.Zero, ref iid, out folderPtr) != 0 ||
                    folderPtr == IntPtr.Zero)
                {
                    return;
                }
                var folder = (IShellFolder)Marshal.GetObjectForIUnknown(folderPtr);
                IShellFolder2? folder2 = null;
                Guid iid2 = IidShellFolder2;
                if (Marshal.QueryInterface(folderPtr, ref iid2, out folder2Ptr) == 0 &&
                    folder2Ptr != IntPtr.Zero)
                {
                    folder2 = (IShellFolder2)Marshal.GetObjectForIUnknown(folder2Ptr);
                }

                if (folder.EnumObjects(IntPtr.Zero,
                        ShcontfFolders | ShcontfNonfolders |
                        ShcontfIncludeHidden | ShcontfIncludeSuperHidden,
                        out enumPtr) != 0 ||
                    enumPtr == IntPtr.Zero)
                {
                    return;
                }
                var enumerator = (IEnumIDList)Marshal.GetObjectForIUnknown(enumPtr);
                while (list.Count < 256)
                {
                    IntPtr child = IntPtr.Zero;
                    uint fetched = 0;
                    int next = enumerator.Next(1, out child, out fetched);
                    if (next != 0 || fetched == 0 || child == IntPtr.Zero)
                    {
                        break;
                    }
                    try
                    {
                        ControlPanelItem? item = FromChild(folder, folder2, folderPidl, child);
                        if (item != null && seen.Add(item.Name))
                        {
                            list.Add(item);
                        }
                        else if (item != null)
                        {
                            if (item.Pidl != IntPtr.Zero)
                            {
                                NativeMethods.ILFree(item.Pidl);
                            }
                        }
                    }
                    finally
                    {
                        NativeMethods.ILFree(child);
                    }
                }
            }
            finally
            {
                if (enumPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(enumPtr); } catch (Exception) { }
                }
                if (folder2Ptr != IntPtr.Zero)
                {
                    try { Marshal.Release(folder2Ptr); } catch (Exception) { }
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
        }

        private static ControlPanelItem? FromChild(IShellFolder folder,
            IShellFolder2? folder2, IntPtr folderPidl, IntPtr child)
        {
            /* Nested Control Panel applets (BitLocker, Sync Center, RemoteApp,
             * Windows To Go, …) are SFGAO_FOLDER. They are real items in
             * "All Control Panel Items" — never skip them. */
            IntPtr full = ILCombine(folderPidl, child);
            string parse = DisplayNameOf(folder, child, ShgdnForParsing);
            string name = ResolveDisplayName(folder, folder2, folderPidl, child, full, parse);
            if (string.IsNullOrWhiteSpace(name) || LooksLikeGuid(name))
            {
                if (full != IntPtr.Zero)
                {
                    NativeMethods.ILFree(full);
                }
                return null;
            }
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

        private static string ResolveDisplayName(IShellFolder folder, IShellFolder2? folder2,
            IntPtr folderPidl, IntPtr child, IntPtr full, string parse)
        {
            string name = DetailsExName(folder2, child);
            if (IsUsableName(name))
            {
                return name;
            }
            name = ShellItemName(folder, folderPidl, child);
            if (IsUsableName(name))
            {
                return name;
            }
            name = DisplayNameOf(folder, child, ShgdnNormal);
            if (IsUsableName(name))
            {
                return name;
            }
            name = DisplayNameOf(folder, child, ShgdnInFolder);
            if (IsUsableName(name))
            {
                return name;
            }
            name = FileInfoDisplayName(full != IntPtr.Zero ? full : child);
            if (IsUsableName(name))
            {
                return name;
            }
            name = RegistryClsidName(parse);
            if (IsUsableName(name))
            {
                return name;
            }
            name = RegistryClsidName(DisplayNameOf(folder, child, ShgdnForParsing));
            if (IsUsableName(name))
            {
                return name;
            }
            return string.Empty;
        }

        private static bool IsUsableName(string? name)
            => !string.IsNullOrWhiteSpace(name) && !LooksLikeGuid(name);

        private static bool LooksLikeGuid(string name)
        {
            string t = name.Trim();
            if (t.StartsWith("::", StringComparison.Ordinal) ||
                t.StartsWith("shell:::", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
            return t.Length >= 38 && t[0] == '{' && GuidInText.IsMatch(t) &&
                   t.TrimStart('{').IndexOf(' ') < 0;
        }

        private static string DetailsExName(IShellFolder2? folder2, IntPtr child)
        {
            if (folder2 == null)
            {
                return string.Empty;
            }
            try
            {
                PROPERTYKEY key = PkeyItemNameDisplay;
                if (folder2.GetDetailsEx(child, ref key, out object pv) != 0 || pv == null)
                {
                    return string.Empty;
                }
                if (pv is string s)
                {
                    return s.Trim();
                }
                return Convert.ToString(pv)?.Trim() ?? string.Empty;
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }

        private static string ShellItemName(IShellFolder folder, IntPtr folderPidl, IntPtr child)
        {
            IntPtr itemPtr = IntPtr.Zero;
            try
            {
                Guid iid = IidShellItem;
                if (SHCreateItemWithParent(folderPidl, null, child, ref iid, out itemPtr) != 0 ||
                    itemPtr == IntPtr.Zero)
                {
                    /* psfParent form: some namespaces reject the absolute parent. */
                    if (SHCreateItemWithParent(IntPtr.Zero, folder, child, ref iid, out itemPtr) != 0 ||
                        itemPtr == IntPtr.Zero)
                    {
                        return string.Empty;
                    }
                }
                var item = (IShellItem)Marshal.GetObjectForIUnknown(itemPtr);
                if (item.GetDisplayName(SigdnNormalDisplay, out IntPtr pName) != 0 ||
                    pName == IntPtr.Zero)
                {
                    return string.Empty;
                }
                try
                {
                    return (Marshal.PtrToStringUni(pName) ?? string.Empty).Trim();
                }
                finally
                {
                    Marshal.FreeCoTaskMem(pName);
                }
            }
            catch (Exception)
            {
                return string.Empty;
            }
            finally
            {
                if (itemPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(itemPtr); } catch (Exception) { }
                }
            }
        }

        private static string DisplayNameOf(IShellFolder folder, IntPtr pidl, uint flags)
        {
            IntPtr pName = Marshal.AllocHGlobal(520);
            try
            {
                if (folder.GetDisplayNameOf(pidl, flags, pName) != 0)
                {
                    return string.Empty;
                }
                var buf = new StringBuilder(260);
                if (StrRetToBufW(pName, pidl, buf, (uint)buf.Capacity) == 0)
                {
                    return buf.ToString().Trim();
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

        private static string FileInfoDisplayName(IntPtr pidl)
        {
            if (pidl == IntPtr.Zero)
            {
                return string.Empty;
            }
            var info = new NativeMethods.SHFILEINFOW();
            IntPtr result = NativeMethods.SHGetFileInfoPidl(pidl, 0, ref info,
                (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                ShgfiPidl | ShgfiDisplayName);
            if (result == IntPtr.Zero)
            {
                return string.Empty;
            }
            return info.szDisplayName?.Trim() ?? string.Empty;
        }

        private static string RegistryClsidName(string? parsing)
        {
            if (string.IsNullOrWhiteSpace(parsing))
            {
                return string.Empty;
            }
            Match m = GuidInText.Match(parsing);
            if (!m.Success)
            {
                return string.Empty;
            }
            try
            {
                using RegistryKey? key = Registry.ClassesRoot.OpenSubKey(@"CLSID\" + m.Value);
                if (key == null)
                {
                    return string.Empty;
                }
                string? loc = key.GetValue("LocalizedString") as string;
                string loaded = LoadIndirect(loc);
                if (IsUsableName(loaded))
                {
                    return loaded;
                }
                string? def = key.GetValue(null) as string;
                loaded = LoadIndirect(def);
                if (IsUsableName(loaded))
                {
                    return loaded;
                }
            }
            catch (Exception)
            {
            }
            return string.Empty;
        }

        private static string LoadIndirect(string? value)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return string.Empty;
            }
            if (!value.StartsWith("@", StringComparison.Ordinal))
            {
                return value.Trim();
            }
            var buf = new StringBuilder(512);
            if (SHLoadIndirectString(value, buf, (uint)buf.Capacity, IntPtr.Zero) == 0)
            {
                return buf.ToString().Trim();
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

        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct PROPERTYKEY
        {
            public Guid fmtid;
            public uint pid;
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214E6-0000-0000-C000-000000000046")]
        private interface IShellFolder
        {
            [PreserveSig] int ParseDisplayName(IntPtr hwnd, IntPtr pbc,
                [MarshalAs(UnmanagedType.LPWStr)] string pszDisplayName,
                out uint pchEaten, out IntPtr ppidl, ref uint pdwAttributes);
            [PreserveSig] int EnumObjects(IntPtr hwnd, uint grfFlags, out IntPtr ppenumIDList);
            [PreserveSig] int BindToObject(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int BindToStorage(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int CompareIDs(IntPtr lParam, IntPtr pidl1, IntPtr pidl2);
            [PreserveSig] int CreateViewObject(IntPtr hwndOwner, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int GetAttributesOf(uint cidl, IntPtr apidl, ref uint rgfInOut);
            [PreserveSig] int GetUIObjectOf(IntPtr hwndOwner, uint cidl, IntPtr apidl,
                ref Guid riid, IntPtr rgfReserved, out IntPtr ppv);
            [PreserveSig] int GetDisplayNameOf(IntPtr pidl, uint uFlags, IntPtr pName);
            [PreserveSig] int SetNameOf(IntPtr hwnd, IntPtr pidl,
                [MarshalAs(UnmanagedType.LPWStr)] string pszName,
                uint uFlags, out IntPtr ppidlOut);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("93F2F68C-1D1B-11D3-A30E-00C04F79ABD1")]
        private interface IShellFolder2
        {
            [PreserveSig] int ParseDisplayName(IntPtr hwnd, IntPtr pbc,
                [MarshalAs(UnmanagedType.LPWStr)] string pszDisplayName,
                out uint pchEaten, out IntPtr ppidl, ref uint pdwAttributes);
            [PreserveSig] int EnumObjects(IntPtr hwnd, uint grfFlags, out IntPtr ppenumIDList);
            [PreserveSig] int BindToObject(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int BindToStorage(IntPtr pidl, IntPtr pbc, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int CompareIDs(IntPtr lParam, IntPtr pidl1, IntPtr pidl2);
            [PreserveSig] int CreateViewObject(IntPtr hwndOwner, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int GetAttributesOf(uint cidl, IntPtr apidl, ref uint rgfInOut);
            [PreserveSig] int GetUIObjectOf(IntPtr hwndOwner, uint cidl, IntPtr apidl,
                ref Guid riid, IntPtr rgfReserved, out IntPtr ppv);
            [PreserveSig] int GetDisplayNameOf(IntPtr pidl, uint uFlags, IntPtr pName);
            [PreserveSig] int SetNameOf(IntPtr hwnd, IntPtr pidl,
                [MarshalAs(UnmanagedType.LPWStr)] string pszName,
                uint uFlags, out IntPtr ppidlOut);
            [PreserveSig] int GetDefaultSearchGUID(out Guid pguid);
            [PreserveSig] int EnumSearches(out IntPtr ppenum);
            [PreserveSig] int GetDefaultColumn(uint dwRes, out uint pSort, out uint pDisplay);
            [PreserveSig] int GetDefaultColumnState(uint iColumn, out uint pcsFlags);
            [PreserveSig] int GetDetailsEx(IntPtr pidl, ref PROPERTYKEY pscid,
                [MarshalAs(UnmanagedType.Struct)] out object pv);
            [PreserveSig] int GetDetailsOf(IntPtr pidl, uint iColumn, IntPtr psd);
            [PreserveSig] int MapColumnToSCID(uint iColumn, out PROPERTYKEY pscid);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
        private interface IShellItem
        {
            [PreserveSig] int BindToHandler(IntPtr pbc, ref Guid bhid, ref Guid riid, out IntPtr ppv);
            [PreserveSig] int GetParent(out IntPtr ppsi);
            [PreserveSig] int GetDisplayName(uint sigdnName, out IntPtr ppszName);
            [PreserveSig] int GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
            [PreserveSig] int Compare(IntPtr psi, uint hint, out int piOrder);
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

        [DllImport("shell32.dll", PreserveSig = true)]
        private static extern int SHCreateItemWithParent(IntPtr pidlParent,
            IShellFolder? psfParent, IntPtr pidl, ref Guid riid, out IntPtr ppvItem);

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int StrRetToBufW(IntPtr pstr, IntPtr pidl,
            StringBuilder pszBuf, uint cchBuf);

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int StrCmpLogicalW(string psz1, string psz2);

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int SHLoadIndirectString(string pszSource,
            StringBuilder pszOutBuf, uint cchOutBuf, IntPtr ppvReserved);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyIcon(IntPtr hIcon);
    }
}
