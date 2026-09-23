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
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Windows;
using System.Windows.Interop;
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
                foreach (NativeMethods.W7TStartMenuEntry entry in _catalog)
                {
                    LeftItems.Add(FromEntry(entry));
                }
                return;
            }

            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string pinPath in StartMenuStore.ReadWindowsPinnedShortcuts())
            {
                StartMenuItem? item = FromPathOrCatalog(pinPath);
                if (item == null || !seen.Add(item.Path))
                {
                    continue;
                }
                LeftItems.Add(item);
            }
            if (LeftItems.Count == 0)
            {
                /* No Explorer pins: show real catalog entries, never invented names. */
                int n = 0;
                foreach (NativeMethods.W7TStartMenuEntry entry in _catalog)
                {
                    if (string.IsNullOrEmpty(entry.Path) && string.IsNullOrEmpty(entry.Target))
                    {
                        continue;
                    }
                    StartMenuItem item = FromEntry(entry);
                    if (!seen.Add(item.Path))
                    {
                        continue;
                    }
                    LeftItems.Add(item);
                    if (++n >= 8)
                    {
                        break;
                    }
                }
            }

            bool addedRecent = false;
            foreach (string recent in _store.Recent)
            {
                StartMenuItem? item = FromPathOrCatalog(recent);
                if (item == null || !seen.Add(item.Path))
                {
                    continue;
                }
                if (!addedRecent)
                {
                    LeftItems.Add(new StartMenuItem { IsSeparator = true });
                    addedRecent = true;
                }
                LeftItems.Add(item);
            }
        }

        private StartMenuItem? FromPathOrCatalog(string pathOrName)
        {
            if (string.IsNullOrWhiteSpace(pathOrName))
            {
                return null;
            }
            NativeMethods.W7TStartMenuEntry? found = FindByName(pathOrName);
            if (found.HasValue)
            {
                return FromEntry(found.Value);
            }
            if (File.Exists(pathOrName))
            {
                return new StartMenuItem
                {
                    Name = Path.GetFileNameWithoutExtension(pathOrName),
                    Path = pathOrName,
                    Target = pathOrName,
                    Icon = LoadIcon(pathOrName, pathOrName)
                };
            }
            return null;
        }

        private NativeMethods.W7TStartMenuEntry? FindByName(string name)
        {
            foreach (NativeMethods.W7TStartMenuEntry e in _catalog)
            {
                if ((e.Name != null &&
                     e.Name.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0) ||
                    (e.Path != null &&
                     string.Equals(e.Path, name, StringComparison.OrdinalIgnoreCase)) ||
                    (e.Target != null &&
                     string.Equals(e.Target, name, StringComparison.OrdinalIgnoreCase)))
                {
                    return e;
                }
            }
            return null;
        }

        private StartMenuItem FromEntry(NativeMethods.W7TStartMenuEntry e)
        {
            var item = new StartMenuItem
            {
                Name = e.Name ?? string.Empty,
                Path = e.Path ?? string.Empty,
                Target = e.Target ?? string.Empty,
                Folder = e.Folder ?? string.Empty
            };
            item.Icon = LoadIcon(item.Path, item.Target);
            return item;
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
            RightLinks.Clear();
            RightLinks.Add(FolderLink("Documents", "documents",
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments)));
            RightLinks.Add(FolderLink("Pictures", "pictures",
                Environment.GetFolderPath(Environment.SpecialFolder.MyPictures)));
            RightLinks.Add(FolderLink("Music", "music",
                Environment.GetFolderPath(Environment.SpecialFolder.MyMusic)));
            RightLinks.Add(FolderLink("Games", "games", "shell:Games"));
            RightLinks.Add(new StartMenuItem { IsSeparator = true });
            RightLinks.Add(FolderLink("Computer", "computer", "shell:MyComputerFolder"));
            RightLinks.Add(FolderLink("Control Panel", "control", "control.exe"));
            RightLinks.Add(FolderLink("Devices and Printers", "devices", "control.exe"));
            RightLinks.Add(FolderLink("Default Programs", "defaults", "control.exe"));
            RightLinks.Add(FolderLink("Help and Support", "help", null));
        }

        private static StartMenuItem FolderLink(string name, string folder, string? iconPath)
        {
            return new StartMenuItem
            {
                Name = name,
                Folder = folder,
                Icon = string.IsNullOrEmpty(iconPath) ? null : LoadIcon(iconPath, iconPath)
            };
        }

        public void OpenRightLink(StartMenuItem item)
        {
            switch (item.Folder)
            {
                case "documents":
                    OpenShellFolder(Environment.SpecialFolder.MyDocuments);
                    break;
                case "pictures":
                    OpenShellFolder(Environment.SpecialFolder.MyPictures);
                    break;
                case "music":
                    OpenShellFolder(Environment.SpecialFolder.MyMusic);
                    break;
                case "games":
                    OpenShellUri("shell:Games");
                    break;
                case "computer":
                    OpenShellUri("shell:MyComputerFolder");
                    break;
                case "control":
                    StartProcess("control.exe", null);
                    break;
                case "devices":
                    StartProcess("control.exe", "printers");
                    break;
                case "defaults":
                    StartProcess("control.exe", "/name Microsoft.DefaultPrograms");
                    break;
                case "help":
                    OpenShellUri("https://support.microsoft.com");
                    break;
            }
        }

        private static ImageSource? LoadIcon(string path, string target)
        {
            ImageSource? fromNative = IconFromHicon(
                NativeMethods.W7T_GetLinkIcon(
                    string.IsNullOrEmpty(path) ? null : path,
                    string.IsNullOrEmpty(target) ? null : target,
                    1));
            if (fromNative != null)
            {
                return fromNative;
            }

            string probe = !string.IsNullOrEmpty(target) ? target : path;
            if (string.IsNullOrEmpty(probe))
            {
                return null;
            }
            ImageSource? fromShell = IconFromShell(probe, File.Exists(probe));
            if (fromShell != null)
            {
                return fromShell;
            }
            if (!string.Equals(probe, path, StringComparison.OrdinalIgnoreCase) &&
                !string.IsNullOrEmpty(path))
            {
                return IconFromShell(path, File.Exists(path));
            }
            return null;
        }

        private static ImageSource? IconFromHicon(IntPtr hicon)
        {
            if (hicon == IntPtr.Zero)
            {
                return null;
            }
            try
            {
                ImageSource src = Imaging.CreateBitmapSourceFromHIcon(
                    hicon, Int32Rect.Empty,
                    BitmapSizeOptions.FromEmptyOptions());
                src.Freeze();
                return src;
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                NativeMethods.DestroyIcon(hicon);
            }
        }

        private static ImageSource? IconFromShell(string probe, bool exists)
        {
            try
            {
                var info = new NativeMethods.SHFILEINFOW();
                uint flags = NativeMethods.SHGFI_ICON | NativeMethods.SHGFI_LARGEICON;
                uint attr = 0;
                if (!exists)
                {
                    flags |= NativeMethods.SHGFI_USEFILEATTRIBUTES;
                    attr = NativeMethods.FILE_ATTRIBUTE_NORMAL;
                }
                IntPtr result = NativeMethods.SHGetFileInfoW(
                    probe, attr, ref info,
                    (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                    flags);
                if (result == IntPtr.Zero || info.hIcon == IntPtr.Zero)
                {
                    return null;
                }
                return IconFromHicon(info.hIcon);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
