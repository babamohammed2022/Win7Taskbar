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
        /* Absolute PIDL (parent + relative, ILCombine). */
        public IntPtr Pidl { get; init; }
        public IntPtr RelativePidl { get; init; }
        public IntPtr ParentPidl { get; init; }
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
        private const uint SeeMaskFlagDdeWait = 0x00000100;
        private const uint SeeMaskNoAsync = 0x00100000;
        private const uint SigdnNormalDisplay = 0;

        private static readonly Guid IidShellFolder =
            new("000214E6-0000-0000-C000-000000000046");
        private static readonly Guid IidShellFolder2 =
            new("93F2F68C-1D1B-11D3-A30E-00C04F79ABD1");
        private static readonly Guid IidShellItem =
            new("43826d1e-e718-42ee-bc55-a1e261c37bfe");
        private static readonly Guid IidContextMenu =
            new("000214E4-0000-0000-C000-000000000046");
        private static readonly Guid IidQueryInfo =
            new("00021500-0000-0000-C000-000000000046");
        private static readonly Guid ClsidAllItems =
            new("21EC2020-3AEA-1069-A2DD-08002B30309D");
        private static readonly Guid ClsidCategoryView =
            new("26EE0668-A00A-44D7-9371-BEB064C98683");
        private static readonly Guid ClsidControlPanelRoot =
            new("5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0");
        private static readonly PROPERTYKEY PkeyItemNameDisplay = new()
        {
            fmtid = new Guid("B725F130-47EF-101A-A5F1-02608C9EEBAC"),
            pid = 10
        };
        /* PKEY_InfoTip {C9944A21-A406-48FE-8225-AEC7E24C211B}, 4 */
        private static readonly PROPERTYKEY PkeyInfoTip = new()
        {
            fmtid = new Guid("C9944A21-A406-48FE-8225-AEC7E24C211B"),
            pid = 4
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
            try
            {
                AppendRegistryNamespaces(list, seen);
            }
            catch (Exception)
            {
            }
            try
            {
                AppendRestoredApplets(list, seen);
            }
            catch (Exception)
            {
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
                if (InvokeDefaultVerb(item))
                {
                    LogLaunch(item, "IContextMenu", true, 0, 0);
                    return true;
                }
                if (item.Pidl != IntPtr.Zero &&
                    ShellExecutePidl(item.Pidl, out int seeErr, out int hInst))
                {
                    LogLaunch(item, "ShellExecuteEx pidl", true, seeErr, hInst);
                    return true;
                }
                string raw = item.ParsingName ?? string.Empty;
                string uri = ToShellUri(raw);
                if (uri.StartsWith("shell:", StringComparison.OrdinalIgnoreCase) ||
                    uri.StartsWith("ms-settings:", StringComparison.OrdinalIgnoreCase))
                {
                    if (ShellExecuteFile(uri, out seeErr, out hInst))
                    {
                        LogLaunch(item, "ShellExecuteEx file", true, seeErr, hInst);
                        return true;
                    }
                    if (StartExplorer(uri))
                    {
                        LogLaunch(item, "explorer.exe", true, 0, 0);
                        return true;
                    }
                }
                else if (!string.IsNullOrWhiteSpace(raw) && LaunchCommand(raw))
                {
                    LogLaunch(item, "command", true, 0, 0);
                    return true;
                }
                LogLaunch(item, "all-paths-failed", false,
                    Marshal.GetLastWin32Error(), 0);
            }
            catch (Exception ex)
            {
                try
                {
                    Win7Taskbar.Utilities.DiagnosticLogger.WriteException(
                        "CPL", ex, "launch " + item.Name);
                }
                catch (Exception)
                {
                }
            }
            return false;
        }

        private static void LogLaunch(ControlPanelItem item, string how,
            bool ok, int win32, int hInst)
        {
            try
            {
                Win7Taskbar.Utilities.DiagnosticLogger.Write("CPL",
                    (ok ? "ok " : "FAIL ") + how +
                    " name=\"" + item.Name + "\"" +
                    " parse=" + (item.ParsingName ?? "") +
                    " pidl=" + (item.Pidl != IntPtr.Zero ? "yes" : "no") +
                    " rel=" + (item.RelativePidl != IntPtr.Zero ? "yes" : "no") +
                    " win32=" + win32 +
                    " hInst=" + hInst);
            }
            catch (Exception)
            {
            }
        }

        private static bool LaunchCommand(string command)
        {
            try
            {
                string file = command.Trim();
                string? args = null;
                if (file.StartsWith("\"", StringComparison.Ordinal))
                {
                    int end = file.IndexOf('"', 1);
                    if (end > 0)
                    {
                        args = file.Substring(end + 1).Trim();
                        file = file.Substring(1, end - 1);
                    }
                }
                else
                {
                    int sp = file.IndexOf(' ');
                    if (sp > 0 && file.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) == false)
                    {
                        /* rundll32.exe cscui.dll,... */
                        string first = file.Substring(0, sp);
                        if (first.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                        {
                            args = file.Substring(sp + 1).Trim();
                            file = first;
                        }
                    }
                }
                var psi = new System.Diagnostics.ProcessStartInfo
                {
                    FileName = file,
                    UseShellExecute = true
                };
                if (!string.IsNullOrEmpty(args))
                {
                    psi.Arguments = args;
                }
                using System.Diagnostics.Process? p = System.Diagnostics.Process.Start(psi);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static bool ShellExecutePidl(IntPtr pidl, out int win32, out int hInst)
        {
            win32 = 0;
            hInst = 0;
            try
            {
                var info = new NativeMethods.SHELLEXECUTEINFO
                {
                    cbSize = Marshal.SizeOf<NativeMethods.SHELLEXECUTEINFO>(),
                    fMask = SeeMaskInvokeIdList | SeeMaskIdList | SeeMaskFlagDdeWait,
                    lpVerb = null,
                    lpIDList = pidl,
                    nShow = NativeMethods.SW_SHOWNORMAL
                };
                bool ok = NativeMethods.ShellExecuteExW(ref info);
                win32 = Marshal.GetLastWin32Error();
                hInst = unchecked((int)info.hInstApp.ToInt64());
                return ok && (long)info.hInstApp > 32;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static bool ShellExecuteFile(string file, out int win32, out int hInst)
        {
            win32 = 0;
            hInst = 0;
            try
            {
                var info = new NativeMethods.SHELLEXECUTEINFO
                {
                    cbSize = Marshal.SizeOf<NativeMethods.SHELLEXECUTEINFO>(),
                    fMask = SeeMaskFlagDdeWait,
                    lpVerb = null,
                    lpFile = file,
                    nShow = NativeMethods.SW_SHOWNORMAL
                };
                bool ok = NativeMethods.ShellExecuteExW(ref info);
                win32 = Marshal.GetLastWin32Error();
                hInst = unchecked((int)info.hInstApp.ToInt64());
                return ok && (long)info.hInstApp > 32;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static bool InvokeDefaultVerb(ControlPanelItem item)
        {
            IntPtr folderPtr = IntPtr.Zero;
            IntPtr menuPtr = IntPtr.Zero;
            IntPtr pidlArray = IntPtr.Zero;
            IntPtr hmenu = IntPtr.Zero;
            IntPtr relative = item.RelativePidl;
            try
            {
                if (item.Pidl == IntPtr.Zero && relative == IntPtr.Zero)
                {
                    return false;
                }
                Guid iidFolder = IidShellFolder;
                IntPtr last = IntPtr.Zero;
                if (item.ParentPidl != IntPtr.Zero && relative != IntPtr.Zero)
                {
                    if (SHGetDesktopFolder(out IntPtr desktopPtr) != 0 ||
                        desktopPtr == IntPtr.Zero)
                    {
                        return false;
                    }
                    try
                    {
                        var desktop = (IShellFolder)Marshal.GetObjectForIUnknown(desktopPtr);
                        if (desktop.BindToObject(item.ParentPidl, IntPtr.Zero,
                                ref iidFolder, out folderPtr) != 0 ||
                            folderPtr == IntPtr.Zero)
                        {
                            return false;
                        }
                    }
                    finally
                    {
                        try { Marshal.Release(desktopPtr); } catch (Exception) { }
                    }
                }
                else
                {
                    if (SHBindToParent(item.Pidl, ref iidFolder, out folderPtr, out last) != 0 ||
                        folderPtr == IntPtr.Zero)
                    {
                        return false;
                    }
                    relative = last;
                }
                if (relative == IntPtr.Zero)
                {
                    return false;
                }
                var folder = (IShellFolder)Marshal.GetObjectForIUnknown(folderPtr);
                pidlArray = Marshal.AllocHGlobal(IntPtr.Size);
                Marshal.WriteIntPtr(pidlArray, relative);
                Guid iidMenu = IidContextMenu;
                if (folder.GetUIObjectOf(IntPtr.Zero, 1, pidlArray, ref iidMenu,
                        IntPtr.Zero, out menuPtr) != 0 ||
                    menuPtr == IntPtr.Zero)
                {
                    return false;
                }
                var menu = (IContextMenu)Marshal.GetObjectForIUnknown(menuPtr);
                hmenu = CreatePopupMenu();
                if (hmenu == IntPtr.Zero)
                {
                    return false;
                }
                const uint CmfDefaultOnly = 0x00000001;
                int qhr = menu.QueryContextMenu(hmenu, 0, 1, 0x7FFF, CmfDefaultOnly);
                if (qhr < 0)
                {
                    return false;
                }
                var cmd = new CMINVOKECOMMANDINFO
                {
                    cbSize = Marshal.SizeOf<CMINVOKECOMMANDINFO>(),
                    fMask = 0,
                    hwnd = IntPtr.Zero,
                    lpVerb = IntPtr.Zero, /* MAKEINTRESOURCEA(0) = default */
                    nShow = NativeMethods.SW_SHOWNORMAL
                };
                int ihr = menu.InvokeCommand(ref cmd);
                return ihr >= 0;
            }
            catch (Exception)
            {
                return false;
            }
            finally
            {
                if (hmenu != IntPtr.Zero)
                {
                    try { DestroyMenu(hmenu); } catch (Exception) { }
                }
                if (pidlArray != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(pidlArray);
                }
                if (menuPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(menuPtr); } catch (Exception) { }
                }
                if (folderPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(folderPtr); } catch (Exception) { }
                }
            }
        }

        public static string? GetInfoTip(ControlPanelItem item)
        {
            if (item == null)
            {
                return null;
            }
            IntPtr folderPtr = IntPtr.Zero;
            IntPtr qiPtr = IntPtr.Zero;
            IntPtr pidlArray = IntPtr.Zero;
            IntPtr relative = item.RelativePidl;
            try
            {
                Guid iidFolder = IidShellFolder;
                IntPtr last = IntPtr.Zero;
                if (item.ParentPidl != IntPtr.Zero && relative != IntPtr.Zero)
                {
                    if (SHGetDesktopFolder(out IntPtr desktopPtr) != 0 ||
                        desktopPtr == IntPtr.Zero)
                    {
                        return GetInfoTipDetailsEx(item);
                    }
                    try
                    {
                        var desktop = (IShellFolder)Marshal.GetObjectForIUnknown(desktopPtr);
                        if (desktop.BindToObject(item.ParentPidl, IntPtr.Zero,
                                ref iidFolder, out folderPtr) != 0)
                        {
                            folderPtr = IntPtr.Zero;
                        }
                    }
                    finally
                    {
                        try { Marshal.Release(desktopPtr); } catch (Exception) { }
                    }
                }
                else if (item.Pidl != IntPtr.Zero)
                {
                    if (SHBindToParent(item.Pidl, ref iidFolder, out folderPtr, out last) == 0)
                    {
                        relative = last;
                    }
                }
                if (folderPtr == IntPtr.Zero || relative == IntPtr.Zero)
                {
                    return GetInfoTipDetailsEx(item);
                }
                var folder = (IShellFolder)Marshal.GetObjectForIUnknown(folderPtr);
                pidlArray = Marshal.AllocHGlobal(IntPtr.Size);
                Marshal.WriteIntPtr(pidlArray, relative);
                Guid iidQi = IidQueryInfo;
                if (folder.GetUIObjectOf(IntPtr.Zero, 1, pidlArray, ref iidQi,
                        IntPtr.Zero, out qiPtr) == 0 &&
                    qiPtr != IntPtr.Zero)
                {
                    var qi = (IQueryInfo)Marshal.GetObjectForIUnknown(qiPtr);
                    if (qi.GetInfoTip(0, out IntPtr tip) == 0 && tip != IntPtr.Zero)
                    {
                        try
                        {
                            string? text = Marshal.PtrToStringUni(tip);
                            if (!string.IsNullOrWhiteSpace(text))
                            {
                                return text.Trim();
                            }
                        }
                        finally
                        {
                            Marshal.FreeCoTaskMem(tip);
                        }
                    }
                }
                return DetailsExTip(folder as IShellFolder2, relative)
                    ?? GetInfoTipDetailsEx(item);
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (pidlArray != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(pidlArray);
                }
                if (qiPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(qiPtr); } catch (Exception) { }
                }
                if (folderPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(folderPtr); } catch (Exception) { }
                }
            }
        }

        private static string? GetInfoTipDetailsEx(ControlPanelItem item)
        {
            if (item.Pidl == IntPtr.Zero)
            {
                return null;
            }
            IntPtr folderPtr = IntPtr.Zero;
            try
            {
                Guid iid = IidShellFolder2;
                if (SHBindToParent(item.Pidl, ref iid, out folderPtr, out IntPtr last) != 0 ||
                    folderPtr == IntPtr.Zero || last == IntPtr.Zero)
                {
                    return null;
                }
                var folder2 = (IShellFolder2)Marshal.GetObjectForIUnknown(folderPtr);
                return DetailsExTip(folder2, last);
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (folderPtr != IntPtr.Zero)
                {
                    try { Marshal.Release(folderPtr); } catch (Exception) { }
                }
            }
        }

        private static string? DetailsExTip(IShellFolder2? folder2, IntPtr child)
        {
            if (folder2 == null || child == IntPtr.Zero)
            {
                return null;
            }
            try
            {
                PROPERTYKEY key = PkeyInfoTip;
                if (folder2.GetDetailsEx(child, ref key, out object pv) != 0 || pv == null)
                {
                    return null;
                }
                string? s = pv as string ?? Convert.ToString(pv);
                return string.IsNullOrWhiteSpace(s) ? null : s.Trim();
            }
            catch (Exception)
            {
                return null;
            }
        }

        internal static string ToShellUri(string? parsing)
        {
            if (string.IsNullOrWhiteSpace(parsing))
            {
                return string.Empty;
            }
            string t = parsing.Trim();
            if (t.StartsWith("shell:", StringComparison.OrdinalIgnoreCase) ||
                t.StartsWith("ms-settings:", StringComparison.OrdinalIgnoreCase) ||
                t.StartsWith("control.exe", StringComparison.OrdinalIgnoreCase))
            {
                return t;
            }
            if (t.StartsWith("::{", StringComparison.Ordinal) ||
                t.StartsWith("{", StringComparison.Ordinal))
            {
                if (t[0] == '{')
                {
                    t = "::" + t;
                }
                return "shell:" + t;
            }
            return t;
        }

        private static bool StartExplorer(string uri)
        {
            try
            {
                var psi = new System.Diagnostics.ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    Arguments = uri,
                    UseShellExecute = true
                };
                using System.Diagnostics.Process? p = System.Diagnostics.Process.Start(psi);
                return p != null || true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static void AppendRegistryNamespaces(List<ControlPanelItem> list,
            HashSet<string> seen)
        {
            const string ns =
                @"SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\ControlPanel\NameSpace";
            AppendNameSpaceHive(Registry.LocalMachine, ns, list, seen);
            AppendNameSpaceHive(Registry.CurrentUser, ns, list, seen);
        }

        private static void AppendNameSpaceHive(RegistryKey hive, string path,
            List<ControlPanelItem> list, HashSet<string> seen)
        {
            try
            {
                using RegistryKey? key = hive.OpenSubKey(path);
                if (key == null)
                {
                    return;
                }
                foreach (string name in key.GetSubKeyNames())
                {
                    if (!Guid.TryParse(name.Trim('{', '}'), out Guid guid))
                    {
                        continue;
                    }
                    TryAddParsing("shell:::{" + guid.ToString("D") + "}", list, seen);
                }
            }
            catch (Exception)
            {
            }
        }

        /* CLSIDs restored in-process by Windhawk mods that hook explorer.exe
         * / control.exe only — our IShellFolder enum never sees them. Add
         * the same parsing names Explorer uses (and file-based applets). */
        private static readonly string[] RestoredClsid =
        {
            "{ED834ED6-4B5A-4bfe-8F11-A626DCB6A921}", /* Personalization */
            "{05d7b0f4-2121-4eff-bf6b-ed3f69b894d9}", /* Notification Area Icons */
            "{7007ACC7-3202-11D1-AAD2-00805FC1270E}", /* Network Connections */
            "{992CFFA0-F557-101A-88EC-00DD010CCC48}", /* Network Connections (alt) */
            "{2227A280-3AEA-1069-A2DE-08002B30309D}", /* Printers */
            "{67CA7650-96E6-4FDD-BB43-A8E774F73A57}", /* HomeGroup */
            "{B4FB3F98-C1EA-428d-A78A-D1F5659CBA93}", /* HomeGroup (page) */
            "{D9EF8727-CAC2-4e60-809E-86F80A666C91}", /* BitLocker */
            "{80F3F1D5-FECA-45F3-BC32-752C152E456E}", /* Tablet PC Settings */
            "{D17D1D6D-CC3F-4815-8FE3-607E7D5D10B3}", /* Text to Speech */
            "{78F3955E-3B90-4184-BD14-5397C15F1EFC}", /* Performance Information */
            "{60632754-c523-4b62-b45c-4172da012619}", /* User Accounts */
            "{7A4D8BD7-9B32-48d0-8A2D-3C208B1A1E22}", /* User Accounts (alt) */
            "{BB06C0E4-D293-4f75-8A90-CB05B6477EEE}"  /* System */
        };

        private static void AppendRestoredApplets(List<ControlPanelItem> list,
            HashSet<string> seen)
        {
            foreach (string clsid in RestoredClsid)
            {
                TryAddParsing("shell:::" + clsid, list, seen);
            }
            TryAddFileApplet("iscsicpl.exe", "iSCSI Initiator", list, seen);
            TryAddFileApplet("joy.cpl", "Game Controllers", list, seen);
            string sys = Environment.GetFolderPath(Environment.SpecialFolder.System);
            string cscui = System.IO.Path.Combine(sys, "cscui.dll");
            if (System.IO.File.Exists(cscui))
            {
                TryAddCommand("rundll32.exe", "cscui.dll,OfflineFilesCpl",
                    "Offline Files", list, seen);
            }
        }

        private static void TryAddParsing(string parsing,
            List<ControlPanelItem> list, HashSet<string> seen)
        {
            IntPtr pidl = IntPtr.Zero;
            try
            {
                if (IsControlPanelViewParsing(parsing))
                {
                    return;
                }
                if (NativeMethods.SHParseDisplayName(parsing, IntPtr.Zero,
                        out pidl, 0, IntPtr.Zero) != 0 ||
                    pidl == IntPtr.Zero)
                {
                    string fallback = RegistryClsidName(parsing);
                    if (!IsUsableName(fallback) && WindhawkPresent())
                    {
                        fallback = HardcodedRestoredName(parsing);
                    }
                    if (!IsUsableName(fallback) || !seen.Add(fallback))
                    {
                        return;
                    }
                    list.Add(new ControlPanelItem
                    {
                        Name = fallback.Trim(),
                        ParsingName = parsing,
                        Icon = null,
                        Pidl = IntPtr.Zero
                    });
                    return;
                }
                string name = FileInfoDisplayName(pidl);
                if (!IsUsableName(name))
                {
                    name = RegistryClsidName(parsing);
                }
                if (IsUsableName(name) && IsControlPanelViewName(name))
                {
                    NativeMethods.ILFree(pidl);
                    return;
                }
                if (!IsUsableName(name) || !seen.Add(name))
                {
                    NativeMethods.ILFree(pidl);
                    return;
                }
                list.Add(new ControlPanelItem
                {
                    Name = name.Trim(),
                    ParsingName = parsing,
                    Icon = IconFromPidl(pidl),
                    Pidl = pidl
                });
                pidl = IntPtr.Zero;
            }
            catch (Exception)
            {
                if (pidl != IntPtr.Zero)
                {
                    NativeMethods.ILFree(pidl);
                }
            }
        }

        private static string HardcodedRestoredName(string parsing)
        {
            Match m = GuidInText.Match(parsing ?? string.Empty);
            if (!m.Success)
            {
                return string.Empty;
            }
            string g = m.Value;
            if (g.Equals("{78F3955E-3B90-4184-BD14-5397C15F1EFC}", StringComparison.OrdinalIgnoreCase))
            {
                return "Performance Information and Tools";
            }
            if (g.Equals("{ED834ED6-4B5A-4bfe-8F11-A626DCB6A921}", StringComparison.OrdinalIgnoreCase))
            {
                return "Personalization";
            }
            if (g.Equals("{05d7b0f4-2121-4eff-bf6b-ed3f69b894d9}", StringComparison.OrdinalIgnoreCase))
            {
                return "Notification Area Icons";
            }
            if (g.Equals("{60632754-c523-4b62-b45c-4172da012619}", StringComparison.OrdinalIgnoreCase) ||
                g.Equals("{7A4D8BD7-9B32-48d0-8A2D-3C208B1A1E22}", StringComparison.OrdinalIgnoreCase))
            {
                return "User Accounts";
            }
            return string.Empty;
        }

        private static bool WindhawkPresent()
        {
            try
            {
                return System.Diagnostics.Process.GetProcessesByName("Windhawk").Length > 0;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static void TryAddFileApplet(string file, string fallbackName,
            List<ControlPanelItem> list, HashSet<string> seen)
        {
            string sys = Environment.GetFolderPath(Environment.SpecialFolder.System);
            string full = System.IO.Path.Combine(sys, file);
            if (!System.IO.File.Exists(full))
            {
                return;
            }
            TryAddCommand(full, null, fallbackName, list, seen);
        }

        private static void TryAddCommand(string file, string? args, string name,
            List<ControlPanelItem> list, HashSet<string> seen)
        {
            if (!seen.Add(name))
            {
                return;
            }
            string parsing = string.IsNullOrEmpty(args) ? file : file + " " + args;
            list.Add(new ControlPanelItem
            {
                Name = name,
                ParsingName = parsing,
                Icon = IconFromPidl(IntPtr.Zero),
                Pidl = IntPtr.Zero
            });
        }

        public static void Free(IEnumerable<ControlPanelItem> items)
        {
            if (items == null)
            {
                return;
            }
            foreach (ControlPanelItem item in items)
            {
                FreeItem(item);
            }
        }

        private static void FreeItem(ControlPanelItem item)
        {
            if (item.Pidl != IntPtr.Zero)
            {
                NativeMethods.ILFree(item.Pidl);
            }
            if (item.RelativePidl != IntPtr.Zero)
            {
                NativeMethods.ILFree(item.RelativePidl);
            }
            if (item.ParentPidl != IntPtr.Zero)
            {
                NativeMethods.ILFree(item.ParentPidl);
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
                            FreeItem(item);
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
            if (IsControlPanelViewItem(folder, child))
            {
                return null;
            }
            IntPtr full = ILCombine(folderPidl, child);
            string parse = DisplayNameOf(folder, child, ShgdnForParsing);
            if (IsControlPanelViewParsing(parse))
            {
                if (full != IntPtr.Zero)
                {
                    NativeMethods.ILFree(full);
                }
                return null;
            }
            string name = ResolveDisplayName(folder, folder2, folderPidl, child, full, parse);
            if (string.IsNullOrWhiteSpace(name) || LooksLikeGuid(name) ||
                IsControlPanelViewName(name))
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
                Pidl = full,
                RelativePidl = ILClone(child),
                ParentPidl = ILClone(folderPidl)
            };
        }

        private static bool IsControlPanelViewItem(IShellFolder folder, IntPtr child)
        {
            try
            {
                int size = Marshal.SizeOf<SHDESCRIPTIONID>();
                IntPtr buf = Marshal.AllocHGlobal(size);
                try
                {
                    if (SHGetDataFromIDListW(folder, child, 3 /* SHGDFIL_DESCRIPTIONID */,
                            buf, size) != 0)
                    {
                        return false;
                    }
                    var desc = Marshal.PtrToStructure<SHDESCRIPTIONID>(buf);
                    return IsControlPanelViewClsid(desc.clsid);
                }
                finally
                {
                    Marshal.FreeHGlobal(buf);
                }
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static bool IsControlPanelViewClsid(Guid clsid)
            => clsid == ClsidAllItems ||
               clsid == ClsidCategoryView ||
               clsid == ClsidControlPanelRoot;

        private static bool IsControlPanelViewParsing(string parse)
        {
            if (string.IsNullOrWhiteSpace(parse))
            {
                return false;
            }
            var found = new List<Guid>();
            foreach (Match m in GuidInText.Matches(parse))
            {
                if (Guid.TryParse(m.Value, out Guid g))
                {
                    found.Add(g);
                }
            }
            if (found.Count == 0)
            {
                return false;
            }
            foreach (Guid g in found)
            {
                if (!IsControlPanelViewClsid(g))
                {
                    return false;
                }
            }
            return true;
        }

        private static bool IsControlPanelViewName(string name)
        {
            string n = name.Trim();
            if (n.Equals("Control Panel", StringComparison.CurrentCultureIgnoreCase) ||
                n.Equals("Pannello di controllo", StringComparison.CurrentCultureIgnoreCase))
            {
                return true;
            }
            if (n.StartsWith("All Control Panel Items", StringComparison.CurrentCultureIgnoreCase) ||
                n.StartsWith("Tutti gli elementi del Pannello", StringComparison.CurrentCultureIgnoreCase))
            {
                return true;
            }
            return false;
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

        [DllImport("shell32.dll")]
        private static extern IntPtr ILClone(IntPtr pidl);

        [DllImport("shell32.dll")]
        private static extern int SHBindToParent(IntPtr pidl, ref Guid riid,
            out IntPtr ppv, out IntPtr ppidlLast);

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        private static extern int SHGetDataFromIDListW(IShellFolder psf, IntPtr pidl,
            int nFormat, IntPtr pv, int cb);

        [StructLayout(LayoutKind.Sequential)]
        private struct SHDESCRIPTIONID
        {
            public uint dwDescriptionId;
            public Guid clsid;
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("000214E4-0000-0000-C000-000000000046")]
        private interface IContextMenu
        {
            [PreserveSig] int QueryContextMenu(IntPtr hmenu, uint indexMenu,
                uint idCmdFirst, uint idCmdLast, uint uFlags);
            [PreserveSig] int InvokeCommand(ref CMINVOKECOMMANDINFO pici);
            [PreserveSig] int GetCommandString(UIntPtr idCmd, uint uType,
                IntPtr pReserved, IntPtr pszName, uint cchMax);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("00021500-0000-0000-C000-000000000046")]
        private interface IQueryInfo
        {
            [PreserveSig] int GetInfoTip(uint dwFlags, out IntPtr ppwszTip);
            [PreserveSig] int GetInfoFlags(out uint pdwFlags);
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct CMINVOKECOMMANDINFO
        {
            public int cbSize;
            public int fMask;
            public IntPtr hwnd;
            public IntPtr lpVerb;
            public IntPtr lpParameters;
            public IntPtr lpDirectory;
            public int nShow;
            public int dwHotKey;
            public IntPtr hIcon;
        }

        [DllImport("user32.dll")]
        private static extern IntPtr CreatePopupMenu();

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyMenu(IntPtr hMenu);

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
