// Win7Taskbar - Start Menu pinned / recent store
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. No Microsoft assets. No Open-Shell source copied.
//
// Pin READS merge:
//   0. %AppData%\Win7Taskbar\Pinned\StartMenu   (ours; writes go here only)
//   1. %AppData%\Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu
//   2. %AppData%\Open-Shell\Pinned
//   3. %AppData%\ClassicShell\Pinned
// Writes never touch Explorer / Open-Shell / Classic. Superbar pins come
// from the native core.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace Win7Taskbar.StartMenu
{
    internal sealed class StartMenuStore
    {
        public List<string> Pinned { get; set; } = new();
        public List<string> Recent { get; set; } = new();
        /// <summary>
        /// Explorer/Open-Shell pins the user hid from OUR menu. We never
        /// write Explorer's User Pinned folder; hiding is local.
        /// </summary>
        public List<string> Unpinned { get; set; } = new();
        public Dictionary<string, int> Usage { get; set; } =
            new(StringComparer.OrdinalIgnoreCase);

        public static event EventHandler? PinsChanged;

        public static string StorePath
        {
            get
            {
                string dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Win7Taskbar");
                return Path.Combine(dir, "startmenu.json");
            }
        }

        public static string StartMenuPinFolder
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Win7Taskbar", "Pinned", "StartMenu");
            }
        }

        public static string TaskBarPinFolder
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Win7Taskbar", "Pinned", "TaskBar");
            }
        }

        public static StartMenuStore Load()
        {
            try
            {
                string path = StorePath;
                if (File.Exists(path))
                {
                    string json = File.ReadAllText(path);
                    StartMenuStore? loaded =
                        JsonSerializer.Deserialize<StartMenuStore>(json);
                    if (loaded != null)
                    {
                        loaded.Pinned ??= new List<string>();
                        loaded.Recent ??= new List<string>();
                        loaded.Unpinned ??= new List<string>();
                        loaded.Usage ??= new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
                        return loaded;
                    }
                }
            }
            catch (Exception)
            {
            }

            return new StartMenuStore();
        }

        /// <summary>
        /// Real .lnk pins only. Empty list if the user has never pinned
        /// anything — never a synthetic catalogue fill.
        /// </summary>
        public static List<string> ReadPinnedShortcuts()
        {
            /* Our folder first (user wins), then Explorer / Open-Shell /
             * Classic discovery. Never write those foreign folders. */
            string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            string[] folders =
            {
                StartMenuPinFolder,
                Path.Combine(appData,
                    @"Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu"),
                Path.Combine(appData, @"Open-Shell\Pinned"),
                Path.Combine(appData, @"ClassicShell\Pinned"),
            };
            var merged = new List<string>();
            StartMenuStore store = Load();
            foreach (string folder in folders)
            {
                foreach (string lnk in ReadLnkFolder(folder))
                {
                    if (merged.Exists(p => SamePin(p, lnk)))
                    {
                        continue;
                    }
                    if (store.Unpinned.Exists(p => SamePin(p, lnk)))
                    {
                        continue;
                    }
                    merged.Add(lnk);
                }
            }
            return merged;
        }

        /// <summary>
        /// Explorer's "Pin to Start Menu" folder (looked up, not copied).
        /// </summary>
        public static string ExplorerPinFolder()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                @"Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu");
        }

        public static List<string> ReadTaskbarPins()
        {
            return ReadLnkFolder(TaskBarPinFolder);
        }

        private static List<string> ReadLnkFolder(string folder)
        {
            var paths = new List<string>();
            try
            {
                if (!Directory.Exists(folder))
                {
                    return paths;
                }
                string[] files = Directory.GetFiles(folder, "*.lnk");
                Array.Sort(files, StringComparer.OrdinalIgnoreCase);
                foreach (string file in files)
                {
                    if (string.Equals(Path.GetFileName(file), "desktop.ini",
                            StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }
                    paths.Add(file);
                }
            }
            catch (Exception)
            {
            }
            return paths;
        }

        public void RecordLaunch(string path)
        {
            if (string.IsNullOrWhiteSpace(path) || !LooksLikePath(path))
            {
                return;
            }
            Usage.TryGetValue(path, out int n);
            Usage[path] = n + 1;
            Recent.RemoveAll(x => string.Equals(x, path, StringComparison.OrdinalIgnoreCase));
            Recent.Insert(0, path);
            while (Recent.Count > 16)
            {
                Recent.RemoveAt(Recent.Count - 1);
            }
            Save();
        }

        public static bool LooksLikePath(string value)
        {
            if (string.IsNullOrWhiteSpace(value) || value.Length < 3)
            {
                return false;
            }
            return value.IndexOf('\\') >= 0 || value.IndexOf('/') >= 0;
        }

        public static bool PinShortcut(string sourcePath)
        {
            /* Writes only %AppData%\Win7Taskbar\Pinned\StartMenu. Never
             * Explorer User Pinned / Open-Shell / Classic. */
            if (!CopyOrCreateShortcut(sourcePath, StartMenuPinFolder, out string dest))
            {
                return false;
            }
            try
            {
                StartMenuStore store = Load();
                bool dirty = false;
                if (!store.Pinned.Exists(p =>
                        string.Equals(p, dest, StringComparison.OrdinalIgnoreCase)))
                {
                    store.Pinned.Add(dest);
                    dirty = true;
                }
                int hidden = store.Unpinned.RemoveAll(p => SamePin(p, sourcePath) ||
                    SamePin(p, dest));
                if (hidden > 0)
                {
                    dirty = true;
                }
                if (dirty)
                {
                    store.Save();
                }
            }
            catch (Exception)
            {
            }
            RaisePinsChanged();
            return true;
        }

        public static bool UnpinShortcut(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }
            /* Delete only our .lnk. Foreign Explorer/Open-Shell/Classic
             * pins stay on disk; they are hidden via Unpinned. */
            bool removed = DeleteMatchingShortcut(path, StartMenuPinFolder);
            try
            {
                StartMenuStore store = Load();
                int before = store.Pinned.Count;
                store.Pinned.RemoveAll(p =>
                    string.Equals(p, path, StringComparison.OrdinalIgnoreCase) ||
                    SamePin(p, path));
                bool hidden = !store.Unpinned.Exists(p => SamePin(p, path));
                if (hidden)
                {
                    store.Unpinned.Add(path);
                }
                if (store.Pinned.Count != before || hidden)
                {
                    store.Save();
                    removed = true;
                }
            }
            catch (Exception)
            {
            }
            if (removed)
            {
                RaisePinsChanged();
            }
            return removed;
        }

        public static bool PinTaskbar(string sourcePath)
        {
            if (!CopyOrCreateShortcut(sourcePath, TaskBarPinFolder, out _))
            {
                return false;
            }
            RaisePinsChanged();
            return true;
        }

        public static bool UnpinTaskbar(string path)
        {
            if (!DeleteMatchingShortcut(path, TaskBarPinFolder))
            {
                return false;
            }
            RaisePinsChanged();
            return true;
        }

        public static bool IsTaskbarPinned(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }
            foreach (string lnk in ReadTaskbarPins())
            {
                if (SamePin(lnk, path))
                {
                    return true;
                }
            }
            return false;
        }

        public static bool IsStartMenuPinned(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }
            foreach (string lnk in ReadPinnedShortcuts())
            {
                if (SamePin(lnk, path))
                {
                    return true;
                }
            }
            return false;
        }

        public static bool SamePin(string a, string b)
        {
            if (string.IsNullOrEmpty(a) || string.IsNullOrEmpty(b))
            {
                return false;
            }
            if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
            try
            {
                string ta = ResolveTarget(a);
                string tb = ResolveTarget(b);
                if (!string.IsNullOrEmpty(ta) && !string.IsNullOrEmpty(tb) &&
                    string.Equals(ta, tb, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
                string nameA = Path.GetFileNameWithoutExtension(a);
                string nameB = Path.GetFileNameWithoutExtension(b);
                return !string.IsNullOrEmpty(nameA) &&
                       string.Equals(nameA, nameB, StringComparison.OrdinalIgnoreCase);
            }
            catch (Exception)
            {
                return false;
            }
        }

        public static string ResolveTarget(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return string.Empty;
            }
            try
            {
                if (path.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase) &&
                    File.Exists(path))
                {
                    string target = ResolveShortcutTarget(path);
                    if (!string.IsNullOrEmpty(target))
                    {
                        return target;
                    }
                }
            }
            catch (Exception)
            {
            }
            return path;
        }

        private static bool CopyOrCreateShortcut(string sourcePath, string destFolder,
            out string dest)
        {
            dest = string.Empty;
            if (string.IsNullOrWhiteSpace(sourcePath) || !LooksLikePath(sourcePath))
            {
                return false;
            }
            try
            {
                Directory.CreateDirectory(destFolder);
                string name = Path.GetFileName(sourcePath);
                if (string.IsNullOrEmpty(name))
                {
                    name = Path.GetFileNameWithoutExtension(sourcePath);
                }
                if (string.IsNullOrEmpty(name))
                {
                    return false;
                }
                if (!name.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase))
                {
                    name += ".lnk";
                }
                dest = Path.Combine(destFolder, name);
                if (File.Exists(dest))
                {
                    return true;
                }
                if (sourcePath.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase) &&
                    File.Exists(sourcePath))
                {
                    File.Copy(sourcePath, dest, overwrite: false);
                    return File.Exists(dest);
                }
                string target = File.Exists(sourcePath) || Directory.Exists(sourcePath)
                    ? sourcePath
                    : ResolveTarget(sourcePath);
                if (string.IsNullOrEmpty(target))
                {
                    target = sourcePath;
                }
                return CreateShortcut(dest, target);
            }
            catch (Exception)
            {
                dest = string.Empty;
                return false;
            }
        }

        private static bool DeleteMatchingShortcut(string path, string folder)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }
            try
            {
                if (File.Exists(path) &&
                    path.StartsWith(folder, StringComparison.OrdinalIgnoreCase))
                {
                    File.Delete(path);
                    return true;
                }
                foreach (string lnk in ReadLnkFolder(folder))
                {
                    if (SamePin(lnk, path))
                    {
                        File.Delete(lnk);
                        return true;
                    }
                }
            }
            catch (Exception)
            {
            }
            return false;
        }

        public static bool CreateShortcut(string lnkPath, string target)
        {
            try
            {
                Type? t = Type.GetTypeFromProgID("WScript.Shell");
                if (t == null)
                {
                    return false;
                }
                object? sh = Activator.CreateInstance(t);
                if (sh == null)
                {
                    return false;
                }
                dynamic sc = ((dynamic)sh).CreateShortcut(lnkPath);
                sc.TargetPath = target;
                try
                {
                    string? dir = Path.GetDirectoryName(target);
                    if (!string.IsNullOrEmpty(dir))
                    {
                        sc.WorkingDirectory = dir;
                    }
                }
                catch (Exception)
                {
                }
                sc.Save();
                return File.Exists(lnkPath);
            }
            catch (Exception)
            {
                return false;
            }
        }

        public static string ResolveShortcutTarget(string lnkPath)
        {
            try
            {
                Type? t = Type.GetTypeFromProgID("WScript.Shell");
                if (t == null)
                {
                    return string.Empty;
                }
                object? sh = Activator.CreateInstance(t);
                if (sh == null)
                {
                    return string.Empty;
                }
                dynamic sc = ((dynamic)sh).CreateShortcut(lnkPath);
                string target = Convert.ToString(sc.TargetPath) ?? string.Empty;
                return Environment.ExpandEnvironmentVariables(target).Trim().Trim('"');
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }

        public void RemoveRecent(string path)
        {
            Recent.RemoveAll(x => string.Equals(x, path, StringComparison.OrdinalIgnoreCase));
            Save();
        }

        public void Save()
        {
            try
            {
                string path = StorePath;
                string? dir = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(dir))
                {
                    Directory.CreateDirectory(dir);
                }
                string json = JsonSerializer.Serialize(this, new JsonSerializerOptions
                {
                    WriteIndented = true
                });
                string temp = path + ".tmp";
                File.WriteAllText(temp, json);
                File.Move(temp, path, overwrite: true);
            }
            catch (Exception)
            {
            }
        }

        private static void RaisePinsChanged()
        {
            try
            {
                PinsChanged?.Invoke(null, EventArgs.Empty);
            }
            catch (Exception)
            {
            }
        }
    }
}
