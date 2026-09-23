// Win7Taskbar - Start Menu pinned / recent store
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. No Microsoft assets. No Open-Shell source copied.
//
// Pin discovery (clean-room, same folders Open-Shell / Explorer actually
// keep .lnk files in — looked up, not copied):
//   1. Windows Explorer "Pin to Start Menu":
//      %AppData%\Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu
//   2. Open-Shell's own pin folder, if the user also has Open-Shell:
//      %AppData%\Open-Shell\Pinned
//   3. Classic Shell's pin folder (the predecessor):
//      %AppData%\ClassicShell\Pinned
// First non-empty folder wins. Nothing is invented from All Programs.

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
        public Dictionary<string, int> Usage { get; set; } =
            new(StringComparer.OrdinalIgnoreCase);

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
            string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            string[] folders =
            {
                Path.Combine(appData,
                    @"Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu"),
                Path.Combine(appData, @"Open-Shell\Pinned"),
                Path.Combine(appData, @"ClassicShell\Pinned"),
            };
            foreach (string folder in folders)
            {
                List<string> found = ReadLnkFolder(folder);
                if (found.Count > 0)
                {
                    return found;
                }
            }
            return new List<string>();
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

        /// <summary>
        /// Explorer's "Pin to Start Menu" folder (looked up, not copied).
        /// </summary>
        public static string ExplorerPinFolder()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                @"Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu");
        }

        public static bool PinShortcut(string sourcePath)
        {
            if (string.IsNullOrWhiteSpace(sourcePath) || !LooksLikePath(sourcePath))
            {
                return false;
            }
            try
            {
                string dir = ExplorerPinFolder();
                Directory.CreateDirectory(dir);
                string name = Path.GetFileName(sourcePath);
                if (string.IsNullOrEmpty(name))
                {
                    return false;
                }
                if (!name.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase))
                {
                    name += ".lnk";
                }
                string dest = Path.Combine(dir, name);
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
                return CreateShortcut(dest, sourcePath);
            }
            catch (Exception)
            {
                return false;
            }
        }

        public static bool UnpinShortcut(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return false;
            }
            try
            {
                string folder = ExplorerPinFolder();
                if (File.Exists(path) &&
                    path.StartsWith(folder, StringComparison.OrdinalIgnoreCase))
                {
                    File.Delete(path);
                    return true;
                }
                string dest = Path.Combine(folder, Path.GetFileName(path));
                if (!dest.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase))
                {
                    dest += ".lnk";
                }
                if (File.Exists(dest))
                {
                    File.Delete(dest);
                    return true;
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
                sc.Save();
                return File.Exists(lnkPath);
            }
            catch (Exception)
            {
                return false;
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
    }
}
