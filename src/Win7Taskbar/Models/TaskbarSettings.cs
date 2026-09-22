// Win7Taskbar - user taskbar grouping configuration (read-only)
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

using Microsoft.Win32;

namespace Win7Taskbar.Models
{
    /// <summary>
    /// v2.62-alpha (G3): the user's own taskbar grouping configuration,
    /// read-only. Both locations that can define these values are probed
    /// (the Taskband key first - the historical location - then Advanced)
    /// and the reader reports WHICH one answered, so the log line can say
    /// where the behaviour comes from. A value that is absent anywhere
    /// stays null: the caller keeps the current behaviour, never a guess.
    /// No write, ever; the read happens once per RefreshWindows (not on a
    /// hover/paint path).
    /// </summary>
    internal sealed class TaskbarSettings
    {
        private const string TaskbandKey =
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\Taskband";
        private const string AdvancedKey =
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";

        /// <summary>TaskbarGlomLevel (0 = never group, 1 = group similar,
        /// 2 = always group). Null when absent.</summary>
        public int? GlomLevel;
        /// <summary>The key that answered for GlomLevel (for the log).</summary>
        public string? GlomSource;

        /// <summary>TaskbarSmallIcons. Null when absent.</summary>
        public bool? SmallIcons;
        /// <summary>The key that answered for SmallIcons (for the log).</summary>
        public string? SmallIconsSource;

        public static TaskbarSettings Read()
        {
            var t = new TaskbarSettings();
            ReadDword(TaskbandKey, "TaskbarGlomLevel", out t.GlomLevel,
                      out t.GlomSource, clamp: v => v < 0 ? null : (int?)(v > 2 ? 2 : v));
            ReadDword(AdvancedKey, "TaskbarGlomLevel", out t.GlomLevel,
                      out t.GlomSource, clamp: v => v < 0 ? null : (int?)(v > 2 ? 2 : v));
            ReadBool(TaskbandKey, "TaskbarSmallIcons", out t.SmallIcons,
                     out t.SmallIconsSource);
            ReadBool(AdvancedKey, "TaskbarSmallIcons", out t.SmallIcons,
                     out t.SmallIconsSource);
            return t;
        }

        private static void ReadDword(string subKey, string value, out int? val,
            out string? source, System.Func<int, int?>? clamp = null)
        {
            val = null;
            source = null;
            if (val != null)
            {
                return;   /* the first key that answered wins */
            }
            try
            {
                using RegistryKey? k = Registry.CurrentUser.OpenSubKey(subKey);
                if (k?.GetValue(value) is int raw)
                {
                    val = clamp != null ? clamp(raw) : raw;
                    source = subKey;
                }
            }
            catch
            {
                /* no value: the caller keeps its default */
            }
        }

        private static void ReadBool(string subKey, string value, out bool? val,
            out string? source)
        {
            val = null;
            source = null;
            if (val != null)
            {
                return;
            }
            try
            {
                using RegistryKey? k = Registry.CurrentUser.OpenSubKey(subKey);
                if (k?.GetValue(value) is int raw)
                {
                    val = raw != 0;
                    source = subKey;
                }
            }
            catch
            {
                /* no value: the caller keeps its default */
            }
        }
    }
}
