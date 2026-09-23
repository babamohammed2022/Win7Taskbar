// Win7Taskbar - Start Menu view-model
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch.

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Microsoft.Win32;
using Win7Taskbar.Interop;

namespace Win7Taskbar.StartMenu
{
    internal sealed class StartMenuViewModel : INotifyPropertyChanged
    {
        private readonly NativeBridge _bridge;
        private readonly StartMenuStore _store;
        private readonly Dispatcher _dispatcher;
        private readonly List<NativeMethods.W7TStartMenuEntry> _catalog = new();
        private string _searchText = string.Empty;
        private string _userName = Environment.UserName;
        private ImageSource? _userPicture;
        private bool _allProgramsOpen;
        private string _searchHint = "Search programs and files";
        private ImageSource? _hoveredLinkIcon;
        private readonly DispatcherTimer _filePoll;
        private readonly HashSet<string> _expandedFolders =
            new(StringComparer.OrdinalIgnoreCase);
        private ImageSource? _folderIcon;

        public ObservableCollection<StartMenuItem> LeftItems { get; } = new();
        public ObservableCollection<StartMenuItem> SearchHits { get; } = new();
        public ObservableCollection<StartMenuItem> RightLinks { get; } = new();

        public StartMenuViewModel(NativeBridge bridge, Dispatcher dispatcher)
        {
            _bridge = bridge;
            _dispatcher = dispatcher;
            _store = StartMenuStore.Load();
            _filePoll = new DispatcherTimer(DispatcherPriority.Background, dispatcher)
            {
                Interval = TimeSpan.FromMilliseconds(120)
            };
            _filePoll.Tick += OnFilePollTick;
            LoadUser();
            BuildRightLinks();
        }

        public string UserName
        {
            get => _userName;
            private set { _userName = value; OnPropertyChanged(); }
        }

        public ImageSource? UserPicture
        {
            get => _userPicture;
            private set { _userPicture = value; OnPropertyChanged(); }
        }

        /// <summary>Right-pane hover: fade the profile picture to this icon.</summary>
        public ImageSource? HoveredLinkIcon
        {
            get => _hoveredLinkIcon;
            private set { _hoveredLinkIcon = value; OnPropertyChanged(); }
        }

        public string SearchHint
        {
            get => _searchHint;
            private set { _searchHint = value; OnPropertyChanged(); }
        }

        public string SearchText
        {
            get => _searchText;
            set
            {
                if (_searchText == value)
                {
                    return;
                }
                _searchText = value ?? string.Empty;
                OnPropertyChanged();
                OnPropertyChanged(nameof(IsSearching));
                RunSearch();
            }
        }

        public bool IsSearching => !string.IsNullOrWhiteSpace(_searchText);
        public bool AllProgramsOpen
        {
            get => _allProgramsOpen;
            private set { _allProgramsOpen = value; OnPropertyChanged(); }
        }

        public event PropertyChangedEventHandler? PropertyChanged;

        public void RefreshCatalog()
        {
            try
            {
                _bridge.StartMenuScan();
                _catalog.Clear();
                _catalog.AddRange(_bridge.StartMenuGetEntries());
            }
            catch (Exception)
            {
                _catalog.Clear();
            }
            RebuildLeft();
        }

        public void ShowDefaultList()
        {
            AllProgramsOpen = false;
            SearchText = string.Empty;
            RebuildLeft();
        }

        public void ToggleAllPrograms()
        {
            AllProgramsOpen = !AllProgramsOpen;
            if (!AllProgramsOpen)
            {
                _expandedFolders.Clear();
            }
            RebuildLeft();
        }

        public void ToggleFolder(StartMenuItem item)
        {
            if (item == null || !item.IsFolder || string.IsNullOrEmpty(item.Folder))
            {
                return;
            }
            if (!_expandedFolders.Add(item.Folder))
            {
                _expandedFolders.Remove(item.Folder);
            }
            RebuildLeft();
        }

        public void Launch(StartMenuItem item)
        {
            if (item == null || item.IsSeparator)
            {
                return;
            }
            if (item.IsAllPrograms)
            {
                ToggleAllPrograms();
                return;
            }
            if (item.IsFolder)
            {
                ToggleFolder(item);
                return;
            }
            string path = !string.IsNullOrEmpty(item.Path) ? item.Path : item.Target;
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            if (_bridge.StartMenuLaunch(path))
            {
                _store.RecordLaunch(path);
            }
            else
            {
                try
                {
                    Process.Start(new ProcessStartInfo
                    {
                        FileName = path,
                        UseShellExecute = true
                    });
                    _store.RecordLaunch(path);
                }
                catch (Exception)
                {
                    /* launch failed: leave the menu as-is */
                }
            }
        }

        public void Power(int action) => _bridge.StartMenuPower(action);

        /// <summary>
        /// Win32 TrackPopupMenu for Shut down / Log off / Sleep — same
        /// hover as the taskbar context menu, not a WPF Popup.
        /// Returns the 1-based choice, or 0 if cancelled.
        /// </summary>
        public int ShowPowerMenu(int screenX, int screenY)
        {
            const string items =
                "Switch user\nLog off\nLock\n-\nRestart\nSleep\nHibernate";
            return _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                items, anchorAtCursor: true);
        }

        public void ApplyPowerChoice(int choice)
        {
            /* 1 Switch user, 2 Log off, 3 Lock, 4 Restart, 5 Sleep, 6 Hibernate */
            int action = choice switch
            {
                1 => 6,
                2 => 4,
                3 => 5,
                4 => 1,
                5 => 2,
                6 => 3,
                _ => -1
            };
            if (action >= 0)
            {
                Power(action);
            }
        }

        public void OpenShellFolder(Environment.SpecialFolder folder)
        {
            try
            {
                string path = Environment.GetFolderPath(folder);
                if (!string.IsNullOrEmpty(path))
                {
                    Process.Start(new ProcessStartInfo
                    {
                        FileName = path,
                        UseShellExecute = true
                    });
                }
            }
            catch (Exception)
            {
            }
        }

        public void OpenShellUri(string uri)
        {
            StartProcess(uri, null);
        }

        /// <summary>
        /// Same explorer.exe shell::: pattern as the overflow date-time page.
        /// CLSID {60632754-...} is User Accounts.
        /// </summary>
        public void OpenUserAccounts()
        {
            StartProcess("explorer.exe",
                "shell:::{60632754-c523-4b62-b45c-4172da012619}");
        }

        private static void StartProcess(string fileName, string? arguments)
        {
            try
            {
                var info = new ProcessStartInfo
                {
                    FileName = fileName,
                    UseShellExecute = true
                };
                if (!string.IsNullOrEmpty(arguments))
                {
                    info.Arguments = arguments;
                }
                Process.Start(info);
            }
            catch (Exception)
            {
            }
        }

        public void SetHoveredLink(StartMenuItem? item)
        {
            HoveredLinkIcon = item is { IsSeparator: false } ? item.Icon : null;
        }

        private void RebuildLeft()
        {
            LeftItems.Clear();
            if (AllProgramsOpen)
            {
                AppendProgramsLevel(string.Empty, 0);
                return;
            }

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var pinSet = new HashSet<string>(StartMenuStore.ReadPinnedShortcuts(),
                StringComparer.OrdinalIgnoreCase);
            foreach (string pinPath in pinSet)
            {
                StartMenuItem? item = FromExistingShortcut(pinPath);
                if (item == null || !seen.Add(item.Path))
                {
                    continue;
                }
                item.IsPinned = true;
                LeftItems.Add(item);
            }

            bool addedRecent = false;
            foreach (string recent in _store.Recent)
            {
                if (!StartMenuStore.LooksLikePath(recent) || !File.Exists(recent))
                {
                    continue;
                }
                StartMenuItem? item = FromExistingShortcut(recent);
                if (item == null || !seen.Add(item.Path))
                {
                    continue;
                }
                item.IsRecent = true;
                item.IsPinned = pinSet.Contains(item.Path);
                if (!addedRecent)
                {
                    LeftItems.Add(new StartMenuItem { IsSeparator = true });
                    addedRecent = true;
                }
                LeftItems.Add(item);
            }
        }

        private StartMenuItem? FromExistingShortcut(string path)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                return null;
            }
            return new StartMenuItem
            {
                Name = Path.GetFileNameWithoutExtension(path),
                Path = path,
                Target = path,
                Icon = LoadIcon(path, path)
            };
        }

        private StartMenuItem FromEntry(NativeMethods.W7TStartMenuEntry e, int indent = 0)
        {
            var item = new StartMenuItem
            {
                Name = e.Name ?? string.Empty,
                Path = e.Path ?? string.Empty,
                Target = e.Target ?? string.Empty,
                Folder = e.Folder ?? string.Empty,
                IndentLevel = indent
            };
            item.Icon = LoadIcon(item.Path, item.Target);
            return item;
        }

        /// <summary>
        /// Win7 / Open-Shell All Programs tree: merged Programs folders
        /// first, then shortcuts at this level. Folder click expands in
        /// place; the catalog Folder field is a relative Programs path.
        /// Written from scratch (layout looked up, source not copied).
        /// </summary>
        private void AppendProgramsLevel(string parentRelative, int indent)
        {
            var folders = new SortedDictionary<string, string>(
                StringComparer.CurrentCultureIgnoreCase);
            var files = new List<NativeMethods.W7TStartMenuEntry>();
            string prefix = string.IsNullOrEmpty(parentRelative)
                ? string.Empty
                : parentRelative + "\\";

            foreach (NativeMethods.W7TStartMenuEntry e in _catalog)
            {
                string folder = e.Folder ?? string.Empty;
                if (string.IsNullOrEmpty(parentRelative))
                {
                    if (string.IsNullOrEmpty(folder))
                    {
                        files.Add(e);
                    }
                    else
                    {
                        int slash = folder.IndexOf('\\');
                        string first = slash < 0 ? folder : folder.Substring(0, slash);
                        if (!string.IsNullOrEmpty(first) && !folders.ContainsKey(first))
                        {
                            folders[first] = first;
                        }
                    }
                }
                else if (string.Equals(folder, parentRelative, StringComparison.OrdinalIgnoreCase))
                {
                    files.Add(e);
                }
                else if (folder.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                {
                    string rest = folder.Substring(prefix.Length);
                    int slash = rest.IndexOf('\\');
                    string first = slash < 0 ? rest : rest.Substring(0, slash);
                    if (string.IsNullOrEmpty(first))
                    {
                        continue;
                    }
                    string full = parentRelative + "\\" + first;
                    if (!folders.ContainsKey(first))
                    {
                        folders[first] = full;
                    }
                }
            }

            foreach (KeyValuePair<string, string> kv in folders)
            {
                bool expanded = _expandedFolders.Contains(kv.Value);
                LeftItems.Add(MakeFolderItem(kv.Key, kv.Value, indent, expanded));
                if (expanded)
                {
                    AppendProgramsLevel(kv.Value, indent + 1);
                }
            }

            files.Sort((a, b) => string.Compare(a.Name, b.Name,
                StringComparison.CurrentCultureIgnoreCase));
            foreach (NativeMethods.W7TStartMenuEntry e in files)
            {
                LeftItems.Add(FromEntry(e, indent));
            }
        }

        private StartMenuItem MakeFolderItem(string name, string relative, int indent,
            bool expanded)
        {
            string fs = ProgramsFolderPath(relative);
            return new StartMenuItem
            {
                Name = name,
                Folder = relative,
                Path = fs,
                Target = fs,
                IsFolder = true,
                IsExpanded = expanded,
                IndentLevel = indent,
                Icon = FolderIcon(fs)
            };
        }

        private static string ProgramsFolderPath(string relative)
        {
            foreach (Environment.SpecialFolder id in new[]
            {
                Environment.SpecialFolder.Programs,
                Environment.SpecialFolder.CommonPrograms
            })
            {
                try
                {
                    string root = Environment.GetFolderPath(id);
                    if (string.IsNullOrEmpty(root))
                    {
                        continue;
                    }
                    string full = string.IsNullOrEmpty(relative)
                        ? root
                        : Path.Combine(root, relative);
                    if (Directory.Exists(full))
                    {
                        return full;
                    }
                }
                catch (Exception)
                {
                }
            }
            return relative ?? string.Empty;
        }

        private ImageSource? FolderIcon(string path)
        {
            ImageSource? icon = LoadIcon(path, path);
            if (icon != null)
            {
                return icon;
            }
            if (_folderIcon != null)
            {
                return _folderIcon;
            }
            _folderIcon = IconFromDll("imageres.dll", 3)
                ?? IconFromDll("shell32.dll", 3);
            return _folderIcon;
        }

        private void RunSearch()
        {
            SearchHits.Clear();
            _filePoll.Stop();
            if (!IsSearching)
            {
                _bridge.StartMenuFileSearchCancel();
                return;
            }

            int[] indices = _bridge.StartMenuQuery(_searchText, 64);
            foreach (int index in indices)
            {
                if (index < 0 || index >= _catalog.Count)
                {
                    continue;
                }
                SearchHits.Add(FromEntry(_catalog[index]));
            }
            if (_bridge.StartMenuFileSearchStart(_searchText))
            {
                _filePoll.Start();
            }
        }

        private void OnFilePollTick(object? sender, EventArgs e)
        {
            string? joined = _bridge.StartMenuFileSearchPoll();
            if (joined == null)
            {
                return;
            }
            _filePoll.Stop();
            if (string.IsNullOrEmpty(joined))
            {
                return;
            }
            foreach (string line in joined.Split('\n'))
            {
                if (string.IsNullOrWhiteSpace(line))
                {
                    continue;
                }
                string hit = line.Trim();
                SearchHits.Add(new StartMenuItem
                {
                    Name = Path.GetFileName(hit),
                    Path = hit,
                    Icon = LoadIcon(hit, hit)
                });
            }
        }

        private void LoadUser()
        {
            try
            {
                UserName = Environment.UserName;
            }
            catch (Exception)
            {
                UserName = "User";
            }
            UserPicture = TryLoadUserPicture();
        }

        private static ImageSource? TryLoadUserPicture()
        {
            foreach (string path in EnumerateUserPicturePaths())
            {
                ImageSource? src = LoadBitmapFile(path);
                if (src != null)
                {
                    return src;
                }
            }
            return null;
        }

        private static List<string> EnumerateUserPicturePaths()
        {
            var paths = new List<string>();
            string? sid = null;
            try
            {
                sid = WindowsIdentity.GetCurrent()?.User?.Value;
            }
            catch (Exception)
            {
            }
            if (!string.IsNullOrEmpty(sid))
            {
                try
                {
                    using RegistryKey? key = Registry.LocalMachine.OpenSubKey(
                        @"SOFTWARE\Microsoft\Windows\CurrentVersion\AccountPicture\Users\" + sid);
                    if (key != null)
                    {
                        foreach (string name in new[] { "Image1080", "Image448", "Image240", "Image96", "Image64", "Image48" })
                        {
                            if (key.GetValue(name) is string p && !string.IsNullOrEmpty(p))
                            {
                                paths.Add(p);
                            }
                        }
                    }
                }
                catch (Exception)
                {
                }
            }

            string common = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),
                @"Microsoft\User Account Pictures");
            paths.Add(Path.Combine(common, Environment.UserName + ".png"));
            paths.Add(Path.Combine(common, Environment.UserName + ".bmp"));
            paths.Add(Path.Combine(common, "user.png"));
            paths.Add(Path.Combine(common, "user.bmp"));

            string roaming = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                @"Microsoft\Windows\AccountPictures");
            if (Directory.Exists(roaming))
            {
                string[] files = Array.Empty<string>();
                try
                {
                    files = Directory.GetFiles(roaming);
                }
                catch (Exception)
                {
                }
                foreach (string file in files)
                {
                    string ext = Path.GetExtension(file);
                    if (ext.Equals(".jpg", StringComparison.OrdinalIgnoreCase) ||
                        ext.Equals(".png", StringComparison.OrdinalIgnoreCase) ||
                        ext.Equals(".bmp", StringComparison.OrdinalIgnoreCase) ||
                        ext.Equals(".jpeg", StringComparison.OrdinalIgnoreCase))
                    {
                        paths.Add(file);
                    }
                }
            }
            return paths;
        }

        private static ImageSource? LoadBitmapFile(string? path)
        {
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                return null;
            }
            try
            {
                var bmp = new BitmapImage();
                bmp.BeginInit();
                bmp.UriSource = new Uri(path);
                bmp.CacheOption = BitmapCacheOption.OnLoad;
                bmp.EndInit();
                bmp.Freeze();
                return bmp;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private void BuildRightLinks()
        {
            /* Win7 two-column right pane. Icon sources are the same
             * known-folder / parsing names Open-Shell lists in
             * CustomMenu.cpp g_StdCommands7 (IDs looked up, code not copied). */
            RightLinks.Clear();
            string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            RightLinks.Add(FolderLink(UserName, "user", profile, isPrimary: true));
            RightLinks.Add(FolderLink("Documents", "documents",
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)));
            RightLinks.Add(FolderLink("Pictures", "pictures",
                Environment.GetFolderPath(Environment.SpecialFolder.MyPictures)));
            RightLinks.Add(FolderLink("Music", "music",
                Environment.GetFolderPath(Environment.SpecialFolder.MyMusic)));
            RightLinks.Add(new StartMenuItem { IsSeparator = true });
            /* FOLDERID_Games / shell:Games is dead on Windows 10/11.
             * Videos is a real user library that still opens. */
            RightLinks.Add(FolderLink("Videos", "videos",
                Environment.GetFolderPath(Environment.SpecialFolder.MyVideos)));
            RightLinks.Add(FolderLink("Computer", "computer",
                "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"));
            RightLinks.Add(new StartMenuItem { IsSeparator = true });
            RightLinks.Add(FolderLink("Control Panel", "control",
                "::{26EE0668-A00A-44D7-9371-BEB064C98683}"));
            RightLinks.Add(FolderLink("Devices and Printers", "devices",
                @"::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{A8A91A66-3A7D-4424-8D24-04E180695C7A}"));
            RightLinks.Add(FolderLink("Default Programs", "defaults",
                @"::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{17CD9488-1228-4B2F-88CE-4298E93E0966}"));
            RightLinks.Add(HelpLink());
        }

        private static StartMenuItem FolderLink(string name, string folder, string? iconPath,
            bool isPrimary = false)
        {
            return new StartMenuItem
            {
                Name = name,
                Folder = folder,
                Path = iconPath ?? string.Empty,
                IsPrimary = isPrimary,
                IsRightPane = true,
                Icon = IconFromParsingName(iconPath)
            };
        }

        private static StartMenuItem HelpLink()
        {
            return new StartMenuItem
            {
                Name = "Help and Support",
                Folder = "help",
                Path = Environment.ExpandEnvironmentVariables(@"%SystemRoot%\Help"),
                IsRightPane = true,
                Icon = IconFromDll("imageres.dll", 99)
                    ?? IconFromParsingName(@"%SystemRoot%\Help")
            };
        }

        public void OpenRightLink(StartMenuItem item)
        {
            switch (item.Folder)
            {
                case "user":
                    OpenShellFolder(Environment.SpecialFolder.UserProfile);
                    break;
                case "documents":
                    OpenShellFolder(Environment.SpecialFolder.MyDocuments);
                    break;
                case "pictures":
                    OpenShellFolder(Environment.SpecialFolder.MyPictures);
                    break;
                case "music":
                    OpenShellFolder(Environment.SpecialFolder.MyMusic);
                    break;
                case "videos":
                    OpenShellFolder(Environment.SpecialFolder.MyVideos);
                    break;
                case "computer":
                    OpenShellUri("shell:MyComputerFolder");
                    break;
                case "control":
                    StartProcess("control.exe", null);
                    break;
                case "devices":
                    StartProcess("explorer.exe",
                        @"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{A8A91A66-3A7D-4424-8D24-04E180695C7A}");
                    break;
                case "defaults":
                    StartProcess("explorer.exe",
                        @"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{17CD9488-1228-4B2F-88CE-4298E93E0966}");
                    break;
                case "help":
                    StartProcess("hh.exe", null);
                    break;
            }
        }

        /// <summary>
        /// Win32 item menu via ShowContextMenuEx. Returns true when the
        /// Start Menu should close (Open, Run as, location, delete, …).
        /// Looked-up Open-Shell verbs; no Open-Shell source copied.
        /// </summary>
        public bool ShowItemContextMenu(StartMenuItem item, int screenX, int screenY)
        {
            if (item == null || item.IsSeparator)
            {
                return false;
            }
            if (item.IsAllPrograms)
            {
                return ShowAllProgramsFooterMenu(screenX, screenY);
            }
            if (item.IsFolder)
            {
                return ShowProgramsFolderMenu(item, screenX, screenY);
            }
            if (item.IsRightPane)
            {
                return ShowRightPaneMenu(item, screenX, screenY);
            }

            string path = FirstExisting(item.Path, item.Target);
            bool pinned = item.IsPinned || StartMenuStore.IsStartMenuPinned(path);
            bool recent = item.IsRecent && !pinned;
            bool allPrograms = AllProgramsOpen && !pinned && !recent;
            bool underStart = IsUnderStartMenu(path);

            var lines = new List<string> { "Open", "Run as administrator" };
            if (pinned)
            {
                lines.Add("Unpin from Start Menu");
                lines.Add(TaskbarPinLabel(path));
                lines.Add("Open file location");
                lines.Add("Properties");
            }
            else if (recent)
            {
                lines.Add("Pin to Start Menu");
                lines.Add("Pin to Taskbar");
                lines.Add("Remove from this list");
                lines.Add("Open file location");
                lines.Add("Properties");
            }
            else
            {
                lines.Add("Pin to Start Menu");
                lines.Add("Open file location");
                if (allPrograms && underStart)
                {
                    lines.Add("Delete");
                }
                lines.Add("Properties");
            }

            int choice = _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                string.Join("\n", lines), anchorAtCursor: true);
            if (choice <= 0)
            {
                return false;
            }

            string verb = lines[choice - 1];
            switch (verb)
            {
                case "Open":
                    Launch(item);
                    return true;
                case "Run as administrator":
                    ShellVerb(path, "runas");
                    return true;
                case "Unpin from Start Menu":
                    StartMenuStore.UnpinShortcut(path);
                    RebuildLeft();
                    return false;
                case "Pin to Start Menu":
                    StartMenuStore.PinShortcut(path);
                    RebuildLeft();
                    return false;
                case "Pin to Taskbar":
                case "Unpin from Taskbar":
                    ToggleTaskbarPin(path, pin: verb.StartsWith("Pin", StringComparison.Ordinal));
                    return false;
                case "Remove from this list":
                    _store.RemoveRecent(path);
                    RebuildLeft();
                    return false;
                case "Open file location":
                    OpenFileLocation(path);
                    return true;
                case "Delete":
                    TryDeleteShortcut(path);
                    RefreshCatalog();
                    return false;
                case "Properties":
                    ShellVerb(path, "properties");
                    return true;
                default:
                    return false;
            }
        }

        public void ShowEmptyLeftContextMenu(int screenX, int screenY)
        {
            const string items = "Sort by Name\nProperties";
            int choice = _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                items, anchorAtCursor: true);
            if (choice == 1)
            {
                SortLeftByName();
            }
            else if (choice == 2)
            {
                string folder = Environment.GetFolderPath(Environment.SpecialFolder.StartMenu);
                ShellVerb(folder, "properties");
            }
        }

        private bool ShowAllProgramsFooterMenu(int screenX, int screenY)
        {
            const string items =
                "Open All Users\nExplore All Users\nSort by Name\nProperties";
            int choice = _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                items, anchorAtCursor: true);
            string common = Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu);
            switch (choice)
            {
                case 1:
                case 2:
                    StartProcess(common, null);
                    return true;
                case 3:
                    SortLeftByName();
                    return false;
                case 4:
                    ShellVerb(common, "properties");
                    return true;
                default:
                    return false;
            }
        }

        private bool ShowRightPaneMenu(StartMenuItem item, int screenX, int screenY)
        {
            const string items = "Open\nExplore\nSearch\nProperties";
            int choice = _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                items, anchorAtCursor: true);
            string path = item.Path;
            switch (choice)
            {
                case 1:
                    OpenRightLink(item);
                    return true;
                case 2:
                    OpenParsingName(path);
                    return true;
                case 3:
                    OpenSearchIn(path);
                    return true;
                case 4:
                    ShellProperties(path);
                    return true;
                default:
                    return false;
            }
        }

        private void SortLeftByName()
        {
            var ordered = LeftItems
                .Where(i => !i.IsSeparator)
                .OrderBy(i => i.Name, StringComparer.CurrentCultureIgnoreCase)
                .ToList();
            LeftItems.Clear();
            foreach (StartMenuItem row in ordered)
            {
                LeftItems.Add(row);
            }
        }

        private static string FirstExisting(string a, string b)
        {
            if (!string.IsNullOrEmpty(a) && (File.Exists(a) || Directory.Exists(a)))
            {
                return a;
            }
            if (!string.IsNullOrEmpty(b))
            {
                return b;
            }
            return a ?? string.Empty;
        }

        private bool ShowProgramsFolderMenu(StartMenuItem item, int screenX, int screenY)
        {
            const string items = "Open\nExplore\nSearch\nProperties";
            int choice = _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                items, anchorAtCursor: true);
            string path = item.Path;
            switch (choice)
            {
                case 1:
                    ToggleFolder(item);
                    return false;
                case 2:
                    OpenParsingName(path);
                    return true;
                case 3:
                    OpenSearchIn(path);
                    return true;
                case 4:
                    ShellProperties(path);
                    return true;
                default:
                    return false;
            }
        }

        private static void OpenParsingName(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            if (path.StartsWith("::", StringComparison.Ordinal))
            {
                StartProcess("explorer.exe", "shell:" + path);
            }
            else
            {
                StartProcess(path, null);
            }
        }

        private static void OpenSearchIn(string path)
        {
            if (string.IsNullOrEmpty(path) ||
                path.StartsWith("::", StringComparison.Ordinal))
            {
                StartProcess("explorer.exe", "search-ms:");
                return;
            }
            StartProcess("explorer.exe",
                "search-ms:displayname=Search&crumb=location:" + path);
        }

        private static void ShellProperties(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            if (path.StartsWith("::", StringComparison.Ordinal))
            {
                ShellVerb("shell:" + path, "properties");
            }
            else
            {
                ShellVerb(path, "properties");
            }
        }

        private static bool IsUnderStartMenu(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return false;
            }
            try
            {
                string user = Environment.GetFolderPath(Environment.SpecialFolder.StartMenu);
                string common = Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu);
                return (!string.IsNullOrEmpty(user) &&
                        path.StartsWith(user, StringComparison.OrdinalIgnoreCase)) ||
                       (!string.IsNullOrEmpty(common) &&
                        path.StartsWith(common, StringComparison.OrdinalIgnoreCase));
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static string TaskbarPinLabel(string path)
        {
            return StartMenuStore.IsTaskbarPinned(path)
                ? "Unpin from Taskbar"
                : "Pin to Taskbar";
        }

        private static void ToggleTaskbarPin(string path, bool pin)
        {
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            if (pin)
            {
                StartMenuStore.PinTaskbar(path);
            }
            else
            {
                StartMenuStore.UnpinTaskbar(path);
            }
        }

        private static void OpenFileLocation(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            try
            {
                if (path.StartsWith("::", StringComparison.Ordinal))
                {
                    StartProcess("explorer.exe", "shell:" + path);
                    return;
                }
                if (File.Exists(path))
                {
                    StartProcess("explorer.exe", "/select,\"" + path + "\"");
                    return;
                }
                string? dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir) && Directory.Exists(dir))
                {
                    StartProcess(dir, null);
                }
            }
            catch (Exception)
            {
            }
        }

        private static void TryDeleteShortcut(string path)
        {
            if (string.IsNullOrEmpty(path) || !IsUnderStartMenu(path))
            {
                return;
            }
            try
            {
                if (File.Exists(path))
                {
                    File.Delete(path);
                }
            }
            catch (Exception)
            {
            }
        }

        private static bool ShellVerb(string path, string verb)
        {
            if (string.IsNullOrEmpty(path))
            {
                return false;
            }
            try
            {
                var info = new NativeMethods.SHELLEXECUTEINFO
                {
                    cbSize = Marshal.SizeOf<NativeMethods.SHELLEXECUTEINFO>(),
                    fMask = NativeMethods.SEE_MASK_INVOKEIDLIST,
                    hwnd = IntPtr.Zero,
                    lpVerb = verb,
                    lpFile = path,
                    nShow = NativeMethods.SW_SHOWNORMAL
                };
                return NativeMethods.ShellExecuteExW(ref info);
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static ImageSource? LoadIcon(string path, string target)
            => StartMenuIcons.FromPath(path, target, 48);

        private static ImageSource? IconFromParsingName(string? probe)
            => StartMenuIcons.FromParsingName(probe, 48);

        private static ImageSource? IconFromDll(string dll, int index)
            => StartMenuIcons.FromDll(dll, index, 48);

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
