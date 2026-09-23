// Win7Taskbar - Start Menu pinned / recent / usage store
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. No Microsoft assets.
//
// Pins come from the real Windows "User Pinned\StartMenu" folder (the same
// place Open-Shell / Explorer read). Nothing is invented.

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

        public static string WindowsPinnedFolder
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    @"Microsoft\Internet Explorer\Quick Launch\User Pinned\StartMenu");
            }
        }

        public static StartMenuStore Load()
        {
            StartMenuStore store;
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
                        store = loaded;
                        store.Pinned ??= new List<string>();
                        store.Recent ??= new List<string>();
                        store.Usage ??= new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
                        return store;
                    }
                }
            }
            catch (Exception)
            {
            }

            store = new StartMenuStore();
            return store;
        }

        public static List<string> ReadWindowsPinnedShortcuts()
        {
            var paths = new List<string>();
            try
            {
                string folder = WindowsPinnedFolder;
                if (!Directory.Exists(folder))
                {
                    return paths;
                }
                foreach (string file in Directory.GetFiles(folder, "*.lnk"))
                {
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
            if (string.IsNullOrWhiteSpace(path))
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
