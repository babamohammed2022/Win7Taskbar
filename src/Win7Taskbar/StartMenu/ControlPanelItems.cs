// Win7Taskbar - Control Panel applet enumeration for the Start Menu cascade
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Written from scratch on public shell APIs (IShellItem, IEnumShellItems,
// SHGetKnownFolderItem). The BEHAVIOUR mirrors Open-Shell's Control Panel
// submenu (Open-Shell-Menu, MIT: Src/StartMenu/StartMenuDLL/
// MenuContainer.cpp, CMenuContainer::AddFirstFolder with the
// CONTAINER_CONTROLPANEL option, and LoadItemOrder for the sort):
//   * the folder is FOLDERID_ControlPanelFolder (the flat "All Control
//     Panel Items" view, parsing name ::{26EE0668-...}\0), so every applet
//     registered with the shell appears: built-in CLSID applets, .cpl
//     files, legacy items restored by third-party tools, vendor applets;
//   * items with SFGAO_HIDDEN are skipped;
//   * items with an empty display name, or whose display name starts
//     with the Control Panel GUID ("something's wrong, like the Intel
//     crap" in the Open-Shell comment), are skipped;
//   * no item cascades further except Administrative Tools
//     (::{D20EA4E1-3957-11D2-A40B-0C5020524153}), which Open-Shell flags
//     as a folder after sorting; here it opens as a folder;
//   * the list is sorted with StrCmpLogicalW ("NumericSort" default),
//     CurrentCultureIgnoreCase as a fallback;
//   * an empty result is reported as such so the UI can show "(Empty)".
// No Open-Shell code is copied. The mapping is documented in
// docs/CONTROL-PANEL-CASCADE-ANALYSIS.md.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    /// <summary>One Control Panel applet as the shell reports it.</summary>
    internal sealed class ControlPanelItem
    {
        /// <summary>Localized display name (SIGDN_NORMALDISPLAY).</summary>
        public string Name { get; init; } = string.Empty;

        /// <summary>
        /// Desktop-absolute parsing name: "::{26EE0668-...}\0\::{CLSID}" for
        /// CLSID applets, a full file path for .cpl based items. Usable with
        /// SHParseDisplayName / SHCreateItemFromParsingName and therefore
        /// with <see cref="ShellContextMenu.TryInvokeDefault"/> and the icon
        /// helpers.
        /// </summary>
        public string ParsingName { get; init; } = string.Empty;

        /// <summary>
        /// True for the one entry Open-Shell treats as a folder in this
        /// cascade (Administrative Tools). Everything else launches.
        /// </summary>
        public bool IsFolder { get; init; }
    }

    /// <summary>
    /// Enumerates the applets of the Control Panel folder in the same
    /// order and with the same exclusions as Open-Shell's cascade.
    /// Everything is best-effort: any COM failure yields an empty list,
    /// never an exception.
    /// </summary>
    internal static class ControlPanelItems
    {
        /// <summary>Parsing name of FOLDERID_ControlPanelFolder.</summary>
        public const string ControlPanelFolderParsingName =
            "::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0";

        /// <summary>Administrative Tools, the only cascading child.</summary>
        public const string AdminToolsClsid = "::{D20EA4E1-3957-11D2-A40B-0C5020524153}";

        /// <summary>Open-Shell's MAX_MENU_ITEMS cap (MenuContainer.h).</summary>
        private const int MaxItems = 2000;

        private static readonly Guid FolderIdControlPanel =
            new("82A74AEB-AEB4-465C-A014-D097EE346D63");
        private static readonly Guid IidShellItem =
            new("43826d1e-e718-42ee-bc55-a1e261c37bfe");
        private static readonly Guid IidEnumShellItems =
            new("70629033-e363-4a28-a567-0db78006e6d7");
        private static readonly Guid BhidEnumItems =
            new("94f60519-2850-4924-aa5a-d15e84868039");

        private const uint SIGDN_NORMALDISPLAY = 0x00000000;
        private const uint SIGDN_DESKTOPABSOLUTEPARSING = 0x80028000;
        private const uint SFGAO_HIDDEN = 0x00080000;
        private const uint SFGAO_FOLDER = 0x20000000;

        /// <summary>
        /// Reads the current applet list. Safe to call from a thread-pool
        /// thread (same pattern as StartMenuShellSearch, which already
        /// enumerates this folder in the background). Returns an empty
        /// list on any failure.
        /// </summary>
        public static List<ControlPanelItem> Enumerate()
        {
            var result = new List<ControlPanelItem>();
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                IShellItem? folder = OpenControlPanelFolder();
                if (folder == null)
                {
                    Debug.WriteLine("[Win7Taskbar] control panel cascade: folder not available");
                    return result;
                }
                try
                {
                    EnumerateInto(folder, result, seen);
                }
                finally
                {
                    try { Marshal.ReleaseComObject(folder); } catch (Exception) { }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel cascade: enumeration failed: {ex.Message}");
                result.Clear();
                return result;
            }

            Sort(result);
            return result;
        }

        private static IShellItem? OpenControlPanelFolder()
        {
            Guid iid = IidShellItem;
            try
            {
                Guid folderId = FolderIdControlPanel;
                int hr = SHGetKnownFolderItem(ref folderId, 0, IntPtr.Zero,
                    ref iid, out IShellItem? known);
                if (hr == 0 && known != null)
                {
                    return known;
                }
            }
            catch (Exception)
            {
            }
            try
            {
                int hr = SHCreateItemFromParsingName(ControlPanelFolderParsingName,
                    IntPtr.Zero, ref iid, out IShellItem? parsed);
                if (hr == 0 && parsed != null)
                {
                    return parsed;
                }
            }
            catch (Exception)
            {
            }
            return null;
        }

        private static void EnumerateInto(IShellItem folder, List<ControlPanelItem> list,
            HashSet<string> seen)
        {
            IntPtr enumPtr = IntPtr.Zero;
            Guid bhid = BhidEnumItems;
            Guid iidEnum = IidEnumShellItems;
            int bind = folder.BindToHandler(IntPtr.Zero, ref bhid, ref iidEnum, out enumPtr);
            if (bind != 0 || enumPtr == IntPtr.Zero)
            {
                return;
            }
            object enumObj = Marshal.GetObjectForIUnknown(enumPtr);
            try
            {
                var enumerator = (IEnumShellItems)enumObj;
                while (list.Count < MaxItems)
                {
                    IntPtr itemPtr = IntPtr.Zero;
                    uint fetched = 0;
                    int next = enumerator.Next(1, out itemPtr, out fetched);
                    if (next != 0 || fetched == 0 || itemPtr == IntPtr.Zero)
                    {
                        break;
                    }
                    object itemObj = Marshal.GetObjectForIUnknown(itemPtr);
                    try
                    {
                        var item = (IShellItem)itemObj;
                        ControlPanelItem? entry = Describe(item);
                        if (entry != null && seen.Add(entry.ParsingName))
                        {
                            list.Add(entry);
                        }
                    }
                    catch (Exception)
                    {
                        /* One broken applet must not hide the rest. */
                    }
                    finally
                    {
                        try { Marshal.ReleaseComObject(itemObj); } catch (Exception) { }
                        Marshal.Release(itemPtr);
                    }
                }
            }
            finally
            {
                try { Marshal.ReleaseComObject(enumObj); } catch (Exception) { }
                Marshal.Release(enumPtr);
            }
        }

        private static ControlPanelItem? Describe(IShellItem item)
        {
            uint attrs = 0;
            try
            {
                item.GetAttributes(SFGAO_HIDDEN | SFGAO_FOLDER, out attrs);
            }
            catch (Exception)
            {
                attrs = 0;
            }
            if ((attrs & SFGAO_HIDDEN) != 0)
            {
                return null;
            }

            string name = ReadName(item, SIGDN_NORMALDISPLAY).Trim();
            if (name.Length == 0 ||
                name.StartsWith(ControlPanelFolderParsingName, StringComparison.OrdinalIgnoreCase))
            {
                return null;
            }
            string parsing = ReadName(item, SIGDN_DESKTOPABSOLUTEPARSING);
            if (parsing.Length == 0)
            {
                return null;
            }

            bool isAdminTools = parsing.EndsWith(AdminToolsClsid, StringComparison.OrdinalIgnoreCase);
            return new ControlPanelItem
            {
                Name = name,
                ParsingName = parsing,
                IsFolder = isAdminTools
            };
        }

        private static void Sort(List<ControlPanelItem> list)
        {
            /* Open-Shell LoadItemOrder: folders first, then names with
             * StrCmpLogicalW ("NumericSort" default on), otherwise the
             * linguistic case-insensitive compare. */
            Comparison<ControlPanelItem> byName = (a, b) =>
            {
                if (a.IsFolder != b.IsFolder)
                {
                    return a.IsFolder ? -1 : 1;
                }
                try
                {
                    return StrCmpLogicalW(a.Name, b.Name);
                }
                catch (Exception)
                {
                    return string.Compare(a.Name, b.Name, StringComparison.CurrentCultureIgnoreCase);
                }
            };
            try
            {
                list.Sort(byName);
            }
            catch (Exception)
            {
                /* A misbehaving comparer must not lose the list. */
            }
        }

        private static string ReadName(IShellItem item, uint sigdn)
        {
            IntPtr p = IntPtr.Zero;
            try
            {
                item.GetDisplayName(sigdn, out p);
                return p == IntPtr.Zero ? string.Empty : (Marshal.PtrToStringUni(p) ?? string.Empty);
            }
            catch (Exception)
            {
                return string.Empty;
            }
            finally
            {
                if (p != IntPtr.Zero)
                {
                    Marshal.FreeCoTaskMem(p);
                }
            }
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
        private interface IShellItem
        {
            [PreserveSig]
            int BindToHandler(IntPtr pbc, [In] ref Guid bhid, [In] ref Guid riid, out IntPtr ppv);
            void GetParent(out IShellItem ppsi);
            void GetDisplayName(uint sigdnName, out IntPtr ppszName);
            void GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
            void Compare(IShellItem psi, uint hint, out int piOrder);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("70629033-e363-4a28-a567-0db78006e6d7")]
        private interface IEnumShellItems
        {
            [PreserveSig]
            int Next(uint celt, out IntPtr rgelt, out uint pceltFetched);
            [PreserveSig]
            int Skip(uint celt);
            [PreserveSig]
            int Reset();
            [PreserveSig]
            int Clone(out IEnumShellItems ppenum);
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
        private static extern int SHCreateItemFromParsingName(
            string pszPath, IntPtr pbc, [In] ref Guid riid,
            [MarshalAs(UnmanagedType.Interface)] out IShellItem? ppv);

        [DllImport("shell32.dll", PreserveSig = true)]
        private static extern int SHGetKnownFolderItem(
            [In] ref Guid rfid, uint flags, IntPtr hToken, [In] ref Guid riid,
            [MarshalAs(UnmanagedType.Interface)] out IShellItem? ppv);

        [DllImport("shlwapi.dll", CharSet = CharSet.Unicode)]
        private static extern int StrCmpLogicalW(string psz1, string psz2);
    }
}
