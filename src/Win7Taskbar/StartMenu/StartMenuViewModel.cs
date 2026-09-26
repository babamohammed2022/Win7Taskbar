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
using Win7Taskbar.Utilities;

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
        private bool _fileSearchActive;

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
            try
            {
                RetroBar.Utilities.Settings.Instance.PropertyChanged += OnSettingsChanged;
            }
            catch (Exception)
            {
            }
            LoadUser();
            SearchHint = T("lang_sm_search", "Search programs and files");
            BuildRightLinks();
            StartMenuShellSearch.BeginLoad();
        }

        private void OnSettingsChanged(object? sender, PropertyChangedEventArgs e)
        {
            try
            {
                if (e.PropertyName != nameof(RetroBar.Utilities.Settings.ConnectionFlyoutPrivacyMode))
                {
                    return;
                }
                /* Settings.PropertyChanged can fire off the Start Menu
                 * dispatcher. Clear+Add on the wrong thread drops the
                 * right pane (privacy on then off). Marshal, and never
                 * leave RightLinks empty if a later Add throws. */
                if (_dispatcher.CheckAccess())
                {
                    RefreshPrivacyIdentity();
                }
                else
                {
                    _dispatcher.BeginInvoke(new Action(RefreshPrivacyIdentity));
                }
            }
            catch (Exception)
            {
            }
        }

        private void RefreshPrivacyIdentity()
        {
            try
            {
                LoadUser();
                BuildRightLinks();
            }
            catch (Exception)
            {
            }
        }

        internal static string T(string key, string fallback)
        {
            try
            {
                string s = LocalizationManager.GetString(key);
                if (!string.IsNullOrEmpty(s) && s != key)
                {
                    return s;
                }
            }
            catch (Exception)
            {
            }
            return fallback;
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
            try { _bridge.StartMenuScan(); } catch (Exception) { }
            PullCatalog();
        }

        public void PullCatalog()
        {
            try
            {
                _catalog.Clear();
                _catalog.AddRange(_bridge.StartMenuGetEntries());
            }
            catch (Exception)
            {
                _catalog.Clear();
            }
            try { RebuildLeft(); } catch (Exception) { }
            if (IsSearching)
            {
                try { RunSearch(); } catch (Exception) { }
            }
        }

        public void WarmIcons()
        {
            try
            {
                foreach (NativeMethods.W7TStartMenuEntry e in _catalog)
                {
                    try
                    {
                        StartMenuIcons.FromPath(e.Path, e.Target, 16);
                        StartMenuIcons.FromPath(e.Path, e.Target, 48);
                    }
                    catch (Exception)
                    {
                    }
                }
            }
            catch (Exception)
            {
            }
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

        public bool Launch(StartMenuItem item)
        {
            if (item == null || item.IsSeparator)
            {
                return false;
            }
            if (item.IsSectionHeader)
            {
                /* Open-Shell click on a section: collapse / expand. */
                ToggleSearchSection(item);
                return true;
            }
            if (item.IsAllPrograms)
            {
                ToggleAllPrograms();
                return true;
            }
            if (string.Equals(item.Folder, "seemore", StringComparison.Ordinal))
            {
                /* "See more results": il primo tentativo usa Explorer come
                 * canale documentato per aprire la query completa; i fallback
                 * mantengono il risultato locale e riferiscono l'esito reale. */
                return OpenSearchResults(item.Path);
            }
            if (item.IsFolder)
            {
                ToggleFolder(item);
                return true;
            }
            string path = PickLaunchPath(item);
            if (string.IsNullOrEmpty(path))
            {
                return false;
            }
            if (TryLaunch(path))
            {
                _store.RecordLaunch(path);
                return true;
            }
            return false;
        }

        private static string PickLaunchPath(StartMenuItem item)
        {
            string path = item.Path ?? string.Empty;
            string target = item.Target ?? string.Empty;
            try
            {
                if (!string.IsNullOrEmpty(path) &&
                    (File.Exists(path) || Directory.Exists(path) ||
                     path.StartsWith("shell:", StringComparison.OrdinalIgnoreCase) ||
                     path.StartsWith("::{", StringComparison.Ordinal) ||
                     path.StartsWith("ms-settings:", StringComparison.OrdinalIgnoreCase) ||
                     path.StartsWith("http", StringComparison.OrdinalIgnoreCase)))
                {
                    return path;
                }
                if (!string.IsNullOrEmpty(target) &&
                    (File.Exists(target) || Directory.Exists(target) ||
                     target.StartsWith("shell:", StringComparison.OrdinalIgnoreCase)))
                {
                    return target;
                }
            }
            catch (Exception)
            {
            }
            return !string.IsNullOrEmpty(path) ? path : target;
        }

        private bool TryLaunch(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }
            path = Environment.ExpandEnvironmentVariables(path.Trim().Trim('"'));
            try
            {
                if (_bridge.StartMenuLaunch(path))
                {
                    return true;
                }
            }
            catch (Exception)
            {
            }
            try
            {
                var info = new NativeMethods.SHELLEXECUTEINFO
                {
                    cbSize = Marshal.SizeOf<NativeMethods.SHELLEXECUTEINFO>(),
                    fMask = NativeMethods.SEE_MASK_INVOKEIDLIST,
                    lpFile = path,
                    nShow = NativeMethods.SW_SHOWNORMAL
                };
                if (NativeMethods.ShellExecuteExW(ref info))
                {
                    return true;
                }
            }
            catch (Exception)
            {
            }
            try
            {
                using (Process? started = Process.Start(new ProcessStartInfo
                {
                    FileName = path,
                    UseShellExecute = true
                }))
                {
                    if (started != null)
                    {
                        return true;
                    }
                }
            }
            catch (Exception)
            {
            }
            try
            {
                using (Process? started = Process.Start(new ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    Arguments = path,
                    UseShellExecute = true
                }))
                {
                    if (started != null)
                    {
                        return true;
                    }
                }
            }
            catch (Exception)
            {
            }
            return false;
        }

        public void Power(int action)
        {
            try
            {
                _bridge.StartMenuPower(action);
            }
            catch (Exception)
            {
            }
        }

        /// <summary>
        /// Win32 TrackPopupMenu for Shut down / Log off / Sleep — same
        /// hover as the taskbar context menu, not a WPF Popup.
        /// Returns the 1-based choice, or 0 if cancelled.
        /// </summary>
        public int ShowPowerMenu(int screenX, int screenY)
        {
            string items = T("lang_sm_switch_user", "Switch user") + "\n" +
                T("lang_sm_logoff", "Log off") + "\n" +
                T("lang_sm_lock", "Lock") + "\n-\n" +
                T("lang_sm_restart", "Restart") + "\n" +
                T("lang_sm_sleep", "Sleep");
            try
            {
                if (NativeMethods.IsHibernateSupported())
                {
                    items += "\n" + T("lang_sm_hibernate", "Hibernate");
                }
            }
            catch (Exception)
            {
            }
            try
            {
                return _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: true,
                    items, anchorAtCursor: true);
            }
            catch (Exception)
            {
                return 0;
            }
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

        public bool OpenShellFolder(Environment.SpecialFolder folder)
        {
            try
            {
                string path = Environment.GetFolderPath(folder);
                return !string.IsNullOrWhiteSpace(path) && TryLaunch(path);
            }
            catch (Exception)
            {
                return false;
            }
        }

        public bool OpenShellUri(string uri)
        {
            if (string.IsNullOrWhiteSpace(uri))
            {
                return false;
            }
            bool opened = TryLaunch(uri);
            if (!opened)
            {
                Debug.WriteLine($"[Win7Taskbar] apertura URI shell non riuscita: {uri}");
            }
            return opened;
        }

        /* v3.19: "Visualizza altri risultati" prima NON apriva nulla su
         * molti sistemi: invocare direttamente l'URI "search-ms:..."
         * dipende dalla registrazione del protocollo (quando l'handler
         * e' il motore di ricerca moderno senza UI esporre finestra il
         * ProcessStartInfo viene osservato come "non fa nulla"). Il 7
         * e Open-Shell passano invece l'URI a EXPLORER.exe: si apre la
         * cartella dei risultati di ricerca. Doppi fallback documentati:
         * la cartella shell dei risultati (CLSID documentato da COM) e,
         * in estremis, la shell window di Esplora file. */
        private bool OpenSearchResults(string? searchUri)
        {
            if (string.IsNullOrEmpty(searchUri))
            {
                return false;
            }
            if (TryStartProcess("explorer.exe", searchUri))
            {
                return true;
            }
            if (TryStartProcess("explorer.exe",
                "shell:::{9343812e-1c37-4a49-a12e-4b2d810d956b}"))
            {
                return true;
            }
            return OpenShellUri(searchUri);
        }

        private static bool TryStartProcess(string fileName,
            string? arguments)
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
                using (Process? started = Process.Start(info))
                {
                    return started != null;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] avvio processo non riuscito: {fileName}: {ex.Message}");
                return false;
            }
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

        private static bool StartProcess(string fileName, string? arguments)
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
                using (Process? started = Process.Start(info))
                {
                    return started != null;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] avvio processo non riuscito: {fileName}: {ex.Message}");
                return false;
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
            /* v3.10: colonna sinistra misurata come il menu di Windows 7 -
             * al massimo 5 programmi pinnati e 9 piu' frequenti: oltre, la
             * lista supererebbe la cornice del menu. */
            int shownPins = 0;
            foreach (string pinPath in pinSet)
            {
                if (shownPins >= MaxPinnedShown)
                {
                    break;
                }
                StartMenuItem? item = FromExistingShortcut(pinPath);
                if (item == null || !seen.Add(item.Path))
                {
                    continue;
                }
                item.IsPinned = true;
                LeftItems.Add(item);
                shownPins++;
            }

            int pinCount = LeftItems.Count;
            bool addedRecent = false;
            int shownRecent = 0;
            foreach (string recent in _store.Recent)
            {
                if (shownRecent >= MaxRecentShown)
                {
                    break;
                }
                if (!StartMenuStore.LooksLikePath(recent) || !File.Exists(recent))
                {
                    continue;
                }
                StartMenuItem? item = FromExistingShortcut(recent);
                if (item == null || !seen.Add(item.Path))
                {
                    continue;
                }
                if (StartMenuLinkFilter.Hide(item.Name, item.Path,
                    StartMenuStore.ResolveTarget(item.Path)))
                {
                    continue;
                }
                item.IsRecent = true;
                item.IsPinned = StartMenuStore.IsStartMenuPinned(item.Path)
                    || StartMenuStore.IsStartMenuPinned(item.Target);
                if (!addedRecent)
                {
                    if (pinCount > 0)
                    {
                        LeftItems.Add(new StartMenuItem { IsSeparator = true });
                    }
                    addedRecent = true;
                }
                LeftItems.Add(item);
                shownRecent++;
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
                Name = StartMenuStore.ShellDisplayName(path,
                    Path.GetFileNameWithoutExtension(path) ?? path),
                Path = path,
                Target = path,
                Icon = LoadIcon(path, path)
            };
        }

        private StartMenuItem FromEntry(NativeMethods.W7TStartMenuEntry e, int indent = 0,
            int iconSize = 24)
        {
            string path = e.Path ?? string.Empty;
            string fallback = e.Name ?? string.Empty;
            var item = new StartMenuItem
            {
                Name = StartMenuStore.ShellDisplayName(path, fallback),
                Path = path,
                Target = e.Target ?? string.Empty,
                Folder = e.Folder ?? string.Empty,
                IndentLevel = indent
            };
            item.Icon = LoadIcon(item.Path, item.Target, iconSize);
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
                if (StartMenuLinkFilter.Hide(e.Name, e.Path, e.Target))
                {
                    continue;
                }
                StartMenuItem row = FromEntry(e, indent, 16);
                row.IsTreeRow = true;
                row.Icon = StartMenuIcons.FromPath(row.Path, row.Target, 16);
                LeftItems.Add(row);
            }
        }

        private StartMenuItem MakeFolderItem(string name, string relative, int indent,
            bool expanded)
        {
            string fs = ProgramsFolderPath(relative);
            return new StartMenuItem
            {
                Name = StartMenuStore.ShellDisplayName(fs, name),
                Folder = relative,
                Path = fs,
                Target = fs,
                IsFolder = true,
                IsExpanded = expanded,
                IndentLevel = indent,
                IsTreeRow = true,
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
            ImageSource? icon = StartMenuIcons.FromPath(path, path, 16);
            if (icon != null)
            {
                return icon;
            }
            if (_folderIcon != null)
            {
                return _folderIcon;
            }
            _folderIcon = StartMenuIcons.FromDll("imageres.dll", 3, 16)
                ?? StartMenuIcons.FromDll("shell32.dll", 3, 16);
            return _folderIcon;
        }
        /* ================= RICERCA A SEZIONI (v3.9) =================
         *
         * Riscritta partendo dal comportamento pubblico di Open-Shell
         * (SearchManager: sezione Programmi, sezione Impostazioni, sezione
         * File con intestazioni "Nome (N)", "Visualizza altri risultati" e
         * "Cerca in Internet"; nessuna riga di codice copiata) e dalla
         * foto di riferimento.
         *
         * Stato in cache: i tre elenchi sotto vengono (ri)emessi dalla
         * ricerca (testo cambiato, poll del file search, catalogo settings
         * pronto) e RebuildSearchView() ricompone la lista visibile
         * applicando le SEZIONI collassabili. La ricerca file vera gira
         * nel core nativo (cartelle note, token, prefisso per famiglia);
         * qui contano solo i prefissi D/P/M/V/F gia' pronti.
         */

        private const int MaxProgramsShown = 8;
        private const int MaxPerFileKindShown = 5;   /* anche Cartelle */
        private const int MaxSettingsShown = 8;
        private const int MaxPinnedShown = 5;   /* v3.10: 4-5 pinnate max */
        private const int MaxRecentShown = 9;   /* v3.10: piu' frequenti   */

        private sealed class FileSearchRow
        {
            public char Kind;
            public string Path = string.Empty;
        }

        private readonly List<StartMenuItem> _programResults = new();
        private readonly List<StartMenuItem> _settingResults = new();
        private readonly List<FileSearchRow> _fileRows = new();
        private bool _fileTruncated;
        private int _filePollTicks;
        /* v3.10: anti-race (stessa regola di Open-Shell / della specifica):
         * una risposta della ricerca file vale solo per la query che l'ha
         * avviata - se nel frattempo l'utente ha digitato altro, il poll
         * vecchio viene ignorato e mai sovrascrive i risultati nuovi. */
        private string _activeFileQuery = string.Empty;
        private readonly HashSet<string> _collapsedSections =
            new(StringComparer.Ordinal);

        private void RunSearch()
        {
            try
            {
                _filePoll.Stop();
                _fileSearchActive = false;

                if (!IsSearching)
                {
                    try { _bridge.StartMenuFileSearchCancel(); }
                    catch (Exception) { }
                    _programResults.Clear();
                    _settingResults.Clear();
                    _fileRows.Clear();
                    _fileTruncated = false;
                    RebuildSearchView();
                    return;
                }

                /* Programmi: indice del core (programmi classici + app UWP da
                 * AppsFolder). Ogni voce apre anche le app UWP: il path e' un
                 * nome di parsing ::{AppsFolder}\Pkg!App e il lancio passa da
                 * IApplicationActivationManager nel core. */
                _programResults.Clear();
                try
                {
                    foreach (int index in _bridge.StartMenuQuery(_searchText, 64))
                    {
                        if (index < 0 || index >= _catalog.Count)
                        {
                            continue;
                        }
                        NativeMethods.W7TStartMenuEntry entry = _catalog[index];
                        if (string.IsNullOrWhiteSpace(entry.Path) &&
                            string.IsNullOrWhiteSpace(entry.Target))
                        {
                            continue;
                        }
                        if (StartMenuLinkFilter.Hide(entry.Name, entry.Path,
                                entry.Target))
                        {
                            continue;
                        }
                        _programResults.Add(FromEntry(entry));
                    }
                }
                catch (Exception)
                {
                }

                _fileRows.Clear();
                _fileTruncated = false;

                CollectSettingsHits();

                /* Ricerca file nativa: le righe "K|path" arrivano dal poll. */
                try
                {
                    _activeFileQuery = _searchText;
                    _fileSearchActive = _bridge.StartMenuFileSearchStart(_searchText);
                }
                catch (Exception)
                {
                    _fileSearchActive = false;
                }
                if (_fileSearchActive || !StartMenuShellSearch.IsReady)
                {
                    _filePollTicks = 0;
                    _filePoll.Start();
                }

                RebuildSearchView();
            }
            catch (Exception)
            {
            }
        }

        private void OnFilePollTick(object? sender, EventArgs e)
        {
            try
            {
                if (!IsSearching)
                {
                    _filePoll.Stop();
                    return;
                }
                /* Anti-race: la query e' cambiata dopo l'avvio di questo
                 * file-search ("calc" -> "calcu"): i suoi risultati sono
                 * obsoleti, si ferma senza toccare le liste. */
                if (!string.Equals(_activeFileQuery, _searchText, StringComparison.Ordinal))
                {
                    _fileSearchActive = false;
                    _filePoll.Stop();
                    return;
                }
                /* Guard: mai un poll infinito se il core muore col
                 * risultato ancora marcato "non pronto" (bridge azzerato,
                 * DLL corrotta). 100 tick x 120 ms = 12 s di kappa. */
                if (++_filePollTicks > 100)
                {
                    _fileSearchActive = false;
                    _filePoll.Stop();
                }
                string? joined = _fileSearchActive
                    ? _bridge.StartMenuFileSearchPoll()
                    : string.Empty;
                if (joined == null)
                {
                    /* Non pronto: le impostazioni potrebbero pero' essersi
                     * caricate nel frattempo (primo avvio). */
                    CollectSettingsHits();
                    RebuildSearchView();
                    return;
                }
                if (_fileSearchActive)
                {
                    _fileSearchActive = false;
                    MergeFileSearchLines(joined);
                }
                CollectSettingsHits();
                RebuildSearchView();
                if (!_fileSearchActive && StartMenuShellSearch.IsReady)
                {
                    _filePoll.Stop();
                }
            }
            catch (Exception)
            {
                _filePoll.Stop();
            }
        }

        private void MergeFileSearchLines(string joined)
        {
            _fileRows.Clear();
            _fileTruncated = false;
            if (string.IsNullOrEmpty(joined))
            {
                return;
            }
            foreach (string raw in joined.Split('\n'))
            {
                string line = raw.Trim('\r', '\n', ' ');
                if (line.Length == 0)
                {
                    continue;
                }
                if (line == "T")
                {
                    _fileTruncated = true;
                    continue;
                }
                char kind = 'F';
                string path = line;
                if (line.Length > 2 && line[1] == '|')
                {
                    kind = line[0];
                    path = line.Substring(2);
                }
                /* v3.10: 'R' = cartella (backend Windows Search + walker). */
                if (kind != 'D' && kind != 'P' && kind != 'M' && kind != 'V' &&
                    kind != 'R' && kind != 'F')
                {
                    kind = 'F';
                }
                if (path.IndexOf('\\') < 0 && path.IndexOf('/') < 0)
                {
                    continue;
                }
                if (_fileRows.Exists(r => string.Equals(r.Path, path,
                        StringComparison.OrdinalIgnoreCase)))
                {
                    continue;
                }
                _fileRows.Add(new FileSearchRow { Kind = kind, Path = path });
            }
        }

        private void CollectSettingsHits()
        {
            _settingResults.Clear();
            if (!IsSearching || !StartMenuShellSearch.IsReady)
            {
                return;
            }
            try
            {
                /* Riga "Settings" dell'app: stessa regola storica (appare se
                 * il nome localizzato contiene la query). */
                string settingsName = T("lang_sm_settings", "Settings");
                if (StartMenuShellSearch.SettingsAppExists() &&
                    settingsName.IndexOf(_searchText,
                        StringComparison.CurrentCultureIgnoreCase) >= 0)
                {
                    _settingResults.Add(new StartMenuItem
                    {
                        Name = settingsName,
                        Path = "ms-settings:",
                        Folder = "settings",
                        Icon = StartMenuIcons.FromDll("imageres.dll", 109, 24)
                            ?? StartMenuIcons.FromDll("shell32.dll", 21, 24)
                    });
                }
                foreach (ShellSearchHit hit in
                    StartMenuShellSearch.Match(_searchText, 24))
                {
                    if (string.IsNullOrEmpty(hit.Path))
                    {
                        continue;
                    }
                    bool duplicate = _settingResults.Exists(p =>
                        string.Equals(p.Name, hit.Name,
                            StringComparison.CurrentCultureIgnoreCase));
                    if (duplicate)
                    {
                        continue;
                    }
                    _settingResults.Add(new StartMenuItem
                    {
                        Name = hit.Name,
                        Path = hit.Path,
                        Folder = "settings",
                        Icon = hit.Icon
                            ?? StartMenuIcons.FromDll("imageres.dll", 22, 24)
                    });
                }
            }
            catch (Exception)
            {
            }
        }

        /* Ricostruisce la lista SOLO dalle cache (nessuna query): sicura e
         * usabile a ogni arrivo dati o collassa/espandi. */
        private void RebuildSearchView()
        {
            try
            {
            SearchHits.Clear();
            if (!IsSearching)
            {
                return;
            }

            /* Ordine delle sezioni = ricerca Windows 7/Open-Shell:
             * Programmi, Impostazioni, poi i file per famiglia. */
            AppendSection("programs",
                T("lang_sm_sec_programs", "Programs"),
                _programResults, MaxProgramsShown);

            AppendSection("settings",
                T("lang_sm_settings", "Settings"),
                _settingResults, MaxSettingsShown);

            bool filesHidden = false;
            filesHidden |= AppendFileSection("docs", 'D',
                T("lang_sm_documents", "Documents"));
            filesHidden |= AppendFileSection("pics", 'P',
                T("lang_sm_pictures", "Pictures"));
            filesHidden |= AppendFileSection("music", 'M',
                T("lang_sm_music", "Music"));
            filesHidden |= AppendFileSection("videos", 'V',
                T("lang_sm_videos", "Videos"));
            /* v3.10: sezione Cartelle (righe 'R' da entrambi i backend). */
            filesHidden |= AppendFileSection("folders", 'R',
                T("lang_sm_sec_folders", "Folders"));
            filesHidden |= AppendFileSection("files", 'F',
                T("lang_sm_sec_files", "Files"));

            if (filesHidden || _fileTruncated)
            {
                SearchHits.Add(new StartMenuItem
                {
                    Name = T("lang_sm_see_more", "See more results"),
                    Path = "search-ms:query=" + Uri.EscapeDataString(_searchText),
                    Folder = "seemore",
                    Icon = SeeMoreResultsIcon()
                });
            }

            /* v3.13: "Cerca in Internet" NON e' piu' una voce della lista
             * (appariva solo con certi risultati e scorreva via col resto):
             * ora e' un piccolo FOOTER FISSO del pannello, presente in ogni
             * ricerca - vive nello XAML del SearchHost e la sua azione usa
             * StartMenuShellSearch.InternetSearchUrl(SearchText) dal
             * code-behind della finestra. */
            }
            catch (Exception)
            {
            }
        }

        /* true quando una parte della sezione resta nascosta oltre il cap:
         * per le sezioni file l'operatore attiva "Visualizza altri risultati". */
        private bool AppendSection(string id, string label,
            List<StartMenuItem> items, int maxShown)
        {
            if (items.Count == 0)
            {
                return false;
            }
            TryAddSectionHeader(id, label, items.Count);
            if (_collapsedSections.Contains(id))
            {
                return false;
            }
            int shown = 0;
            foreach (StartMenuItem item in items)
            {
                if (shown >= maxShown)
                {
                    break;
                }
                /* v3.17: metrica icone della riga di ricerca (-4% e
                 * spostamento a sinistra, + calibrazione per le icone
                 * moderne imbottite tipo Strumento di cattura). */
                StartMenuIcons.ApplySearchRowMetrics(item);
                SearchHits.Add(item);
                shown++;
            }
            return shown < items.Count;
        }

        private bool AppendFileSection(string id, char kind, string label)
        {
            var items = new List<StartMenuItem>();
            foreach (FileSearchRow row in _fileRows)
            {
                if (row.Kind != kind)
                {
                    continue;
                }
                try
                {
                    string name = StartMenuStore.ShellDisplayName(
                        row.Path, Path.GetFileName(row.Path) ?? row.Path);
                    if (StartMenuLinkFilter.Hide(name, row.Path, row.Path))
                    {
                        continue;
                    }
                    items.Add(new StartMenuItem
                    {
                        Name = name,
                        Path = row.Path,
                        Target = row.Path,
                        Folder = row.Kind == 'R' ? "folders" : "files",
                        /* v3.10: icone 24 px (metrica Open-Shell Win7 skin),
                         * dalla pipeline IShellItemImageFactory ad alta
                         * qualita' con fallback classico: niente piu' righe
                         * senza icona. */
                        Icon = row.Kind == 'R'
                            ? StartMenuIcons.FromPath(row.Path, row.Path, 24)
                            : LoadIcon(row.Path, row.Path, 24)
                    });
                }
                catch (Exception)
                {
                }
            }
            return AppendSection(id, label, items, MaxPerFileKindShown);
        }

        private void TryAddSectionHeader(string id, string label, int count)
        {
            try
            {
                SearchHits.Add(new StartMenuItem
                {
                    IsSectionHeader = true,
                    SectionId = id,
                    Name = label + " (" + count + ")",
                    IsExpanded = !_collapsedSections.Contains(id)
                });
            }
            catch (Exception)
            {
            }
        }

        private void ToggleSearchSection(StartMenuItem header)
        {
            if (header == null || string.IsNullOrEmpty(header.SectionId))
            {
                return;
            }
            try
            {
                if (!_collapsedSections.Add(header.SectionId))
                {
                    _collapsedSections.Remove(header.SectionId);
                }
                RebuildSearchView();
            }
            catch (Exception)
            {
            }
        }

        private void LoadUser()
        {
            try
            {
                bool privacy = false;
                try
                {
                    privacy = RetroBar.Utilities.Settings.Instance.ConnectionFlyoutPrivacyMode == 1;
                }
                catch (Exception)
                {
                }
                UserName = privacy
                    ? T("lang_sm_user_placeholder", "User")
                    : Environment.UserName;
            }
            catch (Exception)
            {
                UserName = T("lang_sm_user_placeholder", "User");
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
             * CustomMenu.cpp g_StdCommands7 (IDs looked up, code not copied).
             * Infotips are original wording from public Win7 Start layout
             * descriptions (O'Reilly Missing Manual / Computer Hope), not
             * Microsoft strings. */
            List<StartMenuItem> snapshot = new(RightLinks);
            var next = new List<StartMenuItem>();
            try
            {
            string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            next.Add(FolderLink(UserName, "user", profile,
                T("lang_sm_tip_user", "Opens the personal folder for this account, with your documents, pictures, and other files."),
                isPrimary: true));
            next.Add(FolderLink(T("lang_sm_documents", "Documents"), "documents",
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                T("lang_sm_tip_documents", "Opens the Documents library, where you keep letters, notes, spreadsheets, and similar files.")));
            next.Add(FolderLink(T("lang_sm_pictures", "Pictures"), "pictures",
                Environment.GetFolderPath(Environment.SpecialFolder.MyPictures),
                T("lang_sm_tip_pictures", "Opens the Pictures library, where you keep photos and other images.")));
            next.Add(FolderLink(T("lang_sm_music", "Music"), "music",
                Environment.GetFolderPath(Environment.SpecialFolder.MyMusic),
                T("lang_sm_tip_music", "Opens the Music library, where you keep songs and other audio.")));
            next.Add(new StartMenuItem { IsSeparator = true });
            next.Add(FolderLink(T("lang_sm_videos", "Videos"), "videos",
                Environment.GetFolderPath(Environment.SpecialFolder.MyVideos),
                T("lang_sm_tip_videos", "Opens the Videos library, where you keep movies and other video files.")));
            next.Add(FolderLink(T("lang_sm_computer", "Computer"), "computer",
                "::{20D04FE0-3AEA-1069-A2D8-08002B30309D}",
                T("lang_sm_tip_computer", "Opens a window for the disk drives, devices, and other hardware attached to this PC.")));
            next.Add(new StartMenuItem { IsSeparator = true });
            StartMenuItem controlPanel = FolderLink(T("lang_sm_control", "Control Panel"), "control",
                "::{26EE0668-A00A-44D7-9371-BEB064C98683}",
                T("lang_sm_tip_control", "Opens Control Panel, where you change settings, add or remove programs, and manage accounts."));
            /* Windows 7 "Display as a menu": the row cascades its applets
             * (ControlPanelCascade). As in Open-Shell a click on the row
             * opens the cascade; the folder itself is reachable from the
             * row's context menu ("Open"). */
            controlPanel.HasCascade = true;
            next.Add(controlPanel);
            next.Add(FolderLink(T("lang_sm_devices", "Devices and Printers"), "devices",
                "shell:::{A8A91A66-3A7D-4424-8D24-04E180695C7A}",
                T("lang_sm_tip_devices", "Opens Devices and Printers, where you view and manage printers, scanners, and other hardware.")));
            next.Add(FolderLink(T("lang_sm_defaults", "Default Programs"), "defaults",
                @"::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{17CD9488-1228-4B2F-88CE-4298E93E0966}",
                T("lang_sm_tip_defaults", "Choose which program Windows uses for web browsing, mail, photos, and media.")));
            next.Add(HelpLink());
                RightLinks.Clear();
                foreach (StartMenuItem item in next)
                {
                    RightLinks.Add(item);
                }
            }
            catch (Exception)
            {
                if (RightLinks.Count == 0)
                {
                    try
                    {
                        foreach (StartMenuItem item in snapshot)
                        {
                            RightLinks.Add(item);
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }
        }

        private static StartMenuItem FolderLink(string name, string folder, string? iconPath,
            string infotip, bool isPrimary = false)
        {
            return new StartMenuItem
            {
                Name = name,
                Folder = folder,
                Path = iconPath ?? string.Empty,
                IsPrimary = isPrimary,
                IsRightPane = true,
                Infotip = infotip,
                Icon = IconFromParsingName(iconPath)
            };
        }

        private static StartMenuItem HelpLink()
        {
            /* Si conserva la stessa icona gia' mostrata dal collegamento
             * Guida: shell32.dll, indice 23. Si migliora soltanto il percorso
             * di estrazione, chiedendo a SHDefExtractIconW la dimensione
             * richiesta e passando poi dalla pipeline GDI+. Le altre risorse
             * restano fallback, non diventano una nuova icona primaria. */
            ImageSource? helpIcon = SeeMoreResultsIcon()
                ?? IconFromDllGdiPlus("imageres.dll", 99)
                ?? IconFromParsingName(@"%SystemRoot%\Help")
                ?? IconFromDll("imageres.dll", 99);
            return new StartMenuItem
            {
                Name = T("lang_sm_help", "Help and Support"),
                Folder = "help",
                Path = "https://support.microsoft.com",
                IsRightPane = true,
                Infotip = T("lang_sm_tip_help", "Opens Microsoft support in your browser for help topics, tutorials, and troubleshooting."),
                Icon = helpIcon
            };
        }

        private static ImageSource? SeeMoreResultsIcon()
        {
            try
            {
                /* La sorgente e l'ordine restano quelli gia' usati dal menu:
                 * cambia solo la qualita' dell'estrazione. */
                return IconFromDllGdiPlus("shell32.dll", 23)
                    ?? IconFromDllGdiPlus("imageres.dll", 11)
                    ?? IconFromDll("shell32.dll", 23)
                    ?? IconFromDll("imageres.dll", 11);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(
                    $"icona Guida/risultati: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Launches one Control Panel applet from the cascade the way the
        /// shell (and Open-Shell) does: the item's default verb through
        /// IContextMenu, which is right for CLSID applets, .cpl pages,
        /// legacy/third-party applets and the Administrative Tools folder
        /// alike. Falls back to explorer.exe / control.exe only when the
        /// shell refuses, so a broken handler never leaves a dead click.
        /// </summary>
        public bool LaunchControlPanelItem(ControlPanelItem item, IntPtr owner, bool runAs)
        {
            if (item == null || string.IsNullOrEmpty(item.ParsingName))
            {
                return false;
            }
            try
            {
                if (ShellContextMenu.TryInvokeDefault(item.ParsingName, owner, runAs))
                {
                    return true;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel item: default verb failed: {ex.Message}");
            }

            /* Fallbacks. A parsing name that is a real file (a .cpl or a
             * shortcut restored by a legacy-applet tool) goes through
             * control.exe, which knows how to host applets; a virtual
             * "::{...}" name goes through explorer.exe shell:::, the same
             * channel the other right-pane links already use. */
            try
            {
                string parsing = item.ParsingName;
                if (parsing.StartsWith("::{", StringComparison.Ordinal))
                {
                    return StartProcess("explorer.exe", "shell:" + parsing);
                }
                if (File.Exists(parsing) &&
                    parsing.EndsWith(".cpl", StringComparison.OrdinalIgnoreCase))
                {
                    return StartProcess("control.exe", "\"" + parsing + "\"");
                }
                return StartProcess(parsing, null);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[Win7Taskbar] control panel item: fallback failed: {ex.Message}");
                return false;
            }
        }

        public bool OpenRightLink(StartMenuItem item)
        {
            if (item == null)
            {
                return false;
            }
            try
            {
                switch (item.Folder)
                {
                    case "user":
                        return OpenShellFolder(Environment.SpecialFolder.UserProfile);
                    case "documents":
                        return OpenShellFolder(Environment.SpecialFolder.MyDocuments);
                    case "pictures":
                        return OpenShellFolder(Environment.SpecialFolder.MyPictures);
                    case "music":
                        return OpenShellFolder(Environment.SpecialFolder.MyMusic);
                    case "videos":
                        return OpenShellFolder(Environment.SpecialFolder.MyVideos);
                    case "computer":
                        return OpenShellUri("shell:MyComputerFolder");
                    case "control":
                        return StartProcess("control.exe", null);
                    case "devices":
                        return OpenShellUri("shell:::{A8A91A66-3A7D-4424-8D24-04E180695C7A}");
                    case "defaults":
                        return StartProcess("explorer.exe",
                            @"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}\0\::{17CD9488-1228-4B2F-88CE-4298E93E0966}");
                    case "help":
                        return OpenShellUri("https://support.microsoft.com");
                    default:
                        return false;
                }
            }
            catch (Exception)
            {
                return false;
            }
        }

        /// <summary>
        /// Win32 item menu via ShowContextMenuEx. Returns true when the
        /// Start Menu should close (Open, Run as, location, delete, …).
        /// Looked-up Open-Shell verbs; no Open-Shell source copied.
        /// </summary>
        public bool ShowItemContextMenu(StartMenuItem item, int screenX, int screenY)
            => ShowItemContextMenu(item, screenX, screenY, IntPtr.Zero);

        public bool ShowItemContextMenu(StartMenuItem item, int screenX, int screenY,
            IntPtr owner)
        {
            if (item == null || item.IsSeparator)
            {
                return false;
            }
            /* v3.9: no menu on section headers / action rows of the search. */
            if (item.IsSectionHeader ||
                string.Equals(item.Folder, "seemore", StringComparison.Ordinal))
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
            string pinLabel = T("lang_sm_pin", "Pin to Start Menu (Win7Taskbar)");
            string unpinLabel = T("lang_sm_unpin", "Unpin from Start Menu (Win7Taskbar)");
            bool pinned = item.IsPinned
                || StartMenuStore.IsStartMenuPinned(path)
                || StartMenuStore.IsStartMenuPinned(item.Path)
                || StartMenuStore.IsStartMenuPinned(item.Target);
            if (!string.IsNullOrEmpty(path) &&
                (File.Exists(path) || Directory.Exists(path)))
            {
                uint extra = 0;
                if (ShellContextMenu.TryShow(path, screenX, screenY, owner,
                        pinned ? null : pinLabel,
                        pinned ? unpinLabel : null,
                        out extra))
                {
                    if (extra == ShellContextMenu.ExtraPinCommand)
                    {
                        StartMenuStore.PinShortcut(path);
                        RebuildLeft();
                        return false;
                    }
                    if (extra == ShellContextMenu.ExtraUnpinCommand)
                    {
                        StartMenuStore.UnpinShortcut(path);
                        RebuildLeft();
                        return false;
                    }
                    return true;
                }
            }

            bool recent = item.IsRecent && !pinned;
            bool allPrograms = AllProgramsOpen && !pinned && !recent;
            bool underStart = IsUnderStartMenu(path);

            string open = T("lang_sm_open", "Open");
            string runas = T("lang_sm_runas", "Run as administrator");
            string loc = T("lang_sm_open_location", "Open file location");
            string props = T("lang_sm_properties", "Properties");
            string del = T("lang_sm_delete", "Delete");
            string remove = T("lang_sm_remove_recent", "Remove from this list");
            string pinTb = T("lang_menu_pin", "Pin this program to taskbar");
            string unpinTb = T("lang_menu_unpin", "Unpin this program from taskbar");

            var lines = new List<string> { open, runas };
            if (pinned)
            {
                lines.Add(unpinLabel);
                lines.Add(TaskbarPinLabel(path));
                lines.Add(loc);
                lines.Add(props);
            }
            else if (recent)
            {
                lines.Add(pinLabel);
                lines.Add(pinTb);
                lines.Add(remove);
                lines.Add(loc);
                lines.Add(props);
            }
            else
            {
                lines.Add(pinLabel);
                lines.Add(loc);
                if (allPrograms && underStart)
                {
                    lines.Add(del);
                }
                lines.Add(props);
            }

            int choice = PopupAtCursor(screenX, screenY, string.Join("\n", lines));
            if (choice <= 0)
            {
                return false;
            }

            string verb = lines[choice - 1];
            if (verb == open)
            {
                Launch(item);
                return true;
            }
            if (verb == runas)
            {
                ShellVerb(path, "runas");
                return true;
            }
            if (verb == unpinLabel)
            {
                StartMenuStore.UnpinShortcut(path);
                RebuildLeft();
                return false;
            }
            if (verb == pinLabel)
            {
                StartMenuStore.PinShortcut(path);
                RebuildLeft();
                return false;
            }
            if (verb == pinTb || verb == unpinTb ||
                verb == TaskbarPinLabel(path))
            {
                ToggleTaskbarPin(path, pin: verb == pinTb);
                return false;
            }
            if (verb == remove)
            {
                _store.RemoveRecent(path);
                RebuildLeft();
                return false;
            }
            if (verb == loc)
            {
                OpenFileLocation(path);
                return true;
            }
            if (verb == del)
            {
                TryDeleteShortcut(path);
                RefreshCatalog();
                return false;
            }
            if (verb == props)
            {
                ShellVerb(path, "properties");
                return true;
            }
            return false;
        }

        public void ShowEmptyLeftContextMenu(int screenX, int screenY)
        {
            string items = T("lang_sm_sort_name", "Sort by Name") + "\n" +
                T("lang_sm_properties", "Properties");
            int choice = PopupAtCursor(screenX, screenY, items);
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
            string items =
                T("lang_sm_open_all_users", "Open All Users") + "\n" +
                T("lang_sm_explore_all_users", "Explore All Users") + "\n" +
                T("lang_sm_sort_name", "Sort by Name") + "\n" +
                T("lang_sm_properties", "Properties");
            int choice = PopupAtCursor(screenX, screenY, items);
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
            bool computer = string.Equals(item.Folder, "computer", StringComparison.Ordinal);
            string items = computer
                ? T("lang_sm_open", "Open") + "\n" + T("lang_sm_properties", "Properties")
                : T("lang_sm_open", "Open");
            int choice = PopupAtCursor(screenX, screenY, items);
            if (choice == 1)
            {
                return OpenRightLink(item);
            }
            if (computer && choice == 2)
            {
                return StartProcess("explorer.exe",
                    "shell:::{BB06C0E4-D293-4f75-8A90-CB05B6477EEE}");
            }
            return false;
        }

        private int PopupAtCursor(int screenX, int screenY, string items)
        {
            try
            {
                return _bridge.ShowContextMenuEx(screenX, screenY, bottomEdge: false,
                    items, anchorAtCursor: true);
            }
            catch (Exception)
            {
                return 0;
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
            string folderPath = item.Path;
            if (!string.IsNullOrEmpty(folderPath) && Directory.Exists(folderPath) &&
                ShellContextMenu.TryShow(folderPath, screenX, screenY))
            {
                return true;
            }
            string items = T("lang_sm_open", "Open") + "\n" +
                T("lang_sm_explore", "Explore") + "\n" +
                T("lang_sm_search_folder", "Search") + "\n" +
                T("lang_sm_properties", "Properties");
            int choice = PopupAtCursor(screenX, screenY, items);
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
                ? T("lang_menu_unpin", "Unpin this program from taskbar")
                : T("lang_menu_pin", "Pin this program to taskbar");
        }

        private void ToggleTaskbarPin(string path, bool pin)
        {
            string exe = ResolveExe(path);
            if (string.IsNullOrEmpty(exe))
            {
                return;
            }
            try
            {
                string name = Path.GetFileNameWithoutExtension(exe);
                _bridge.ToggleTaskbarPin(exe, name, pin ? 1 : 0);
            }
            catch (Exception)
            {
            }
        }

        private static string ResolveExe(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return string.Empty;
            }
            if (path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && File.Exists(path))
            {
                return path;
            }
            string target = StartMenuStore.ResolveTarget(path);
            if (!string.IsNullOrEmpty(target))
            {
                return target;
            }
            return path;
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

        /// <summary>
        /// Search icons: shell-namespace paths (AppsFolder UWP entries are
        /// "::{4234..}\Pkg!App", Control Panel items "::{26EE..}") resolve
        /// through SHParseDisplayName, not the filesystem probe — otherwise
        /// every UWP search hit showed the blank generic-file icon.
        /// </summary>
        private static ImageSource? LoadIcon(string path, string target)
            => LoadIcon(path, target, 48);

        private static ImageSource? LoadIcon(string path, string target, int size)
        {
            string probe = !string.IsNullOrEmpty(target) ? target : (path ?? string.Empty);
            if (probe.StartsWith("::{", StringComparison.Ordinal) ||
                probe.StartsWith("shell:", StringComparison.OrdinalIgnoreCase))
            {
                ImageSource? shell = StartMenuIcons.FromParsingName(probe, size);
                if (shell != null)
                {
                    return shell;
                }
            }
            return StartMenuIcons.FromPath(path, target, size);
        }

        /* Icone del menu: ShellItem/immagini di sistema + conversione GDI+
         * alla dimensione del controllo, senza usare bitmap inventate. */
        private static ImageSource? IconFromParsingName(string? probe)
            => StartMenuIcons.FromParsingName(probe, 50);

        private static ImageSource? IconFromDll(string dll, int index)
            => StartMenuIcons.FromDll(dll, index, 50);

        private static ImageSource? IconFromDllGdiPlus(string dll, int index)
            => StartMenuIcons.FromDllGdiPlus(dll, index, 50);

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
