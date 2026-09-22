// Win7Taskbar - user group-icon policy (read-only)
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

using System.Collections.Generic;
using Microsoft.Win32;

namespace Win7Taskbar.Models
{
    /// <summary>
    /// v2.62-alpha (G4): the user's group-icon policy, read-only. Both
    /// candidate locations are probed (Taskband first, then Advanced) and
    /// the reader reports which one answered. Absent value => null =>
    /// current behaviour.
    /// </summary>
    internal sealed class GroupIconPolicy
    {
        private const string TaskbandKey =
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\Taskband";
        private const string AdvancedKey =
            @"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";

        /// <summary>UseExecutableForTaskbarGroupIcon: the group button shows
        /// the executable's icon. Null when absent.</summary>
        public bool? UseExecutable;
        public string? UseExecutableSource;

        /// <summary>TaskbarExceptionsIcons: executives whose windows must
        /// never be grouped (file names, as stored by the user). Empty when
        /// absent.</summary>
        public List<string> Exceptions = new List<string>();
        public string? ExceptionsSource;

        /// <summary>TaskbarGroupIcon: the "criterion" value. Read and
        /// logged only - its meaning is not fully documented and the project
        /// does not apply an unverified value.</summary>
        public int? TaskbarGroupIcon;
        public string? TaskbarGroupIconSource;

        public static GroupIconPolicy Read()
        {
            var g = new GroupIconPolicy();
            ReadBool(TaskbandKey, "UseExecutableForTaskbarGroupIcon",
                     out g.UseExecutable, out g.UseExecutableSource);
            if (g.UseExecutable == null)
            {
                ReadBool(AdvancedKey, "UseExecutableForTaskbarGroupIcon",
                         out g.UseExecutable, out g.UseExecutableSource);
            }
            ReadMultiSz(TaskbandKey, "TaskbarExceptionsIcons",
                        out g.Exceptions, out g.ExceptionsSource);
            if (g.ExceptionsSource == null)
            {
                ReadMultiSz(AdvancedKey, "TaskbarExceptionsIcons",
                            out g.Exceptions, out g.ExceptionsSource);
            }
            ReadDword(TaskbandKey, "TaskbarGroupIcon",
                      out g.TaskbarGroupIcon, out g.TaskbarGroupIconSource);
            if (g.TaskbarGroupIcon == null)
            {
                ReadDword(AdvancedKey, "TaskbarGroupIcon",
                          out g.TaskbarGroupIcon, out g.TaskbarGroupIconSource);
            }
            return g;
        }

        private static void ReadBool(string subKey, string value, out bool? val,
            out string? source)
        {
            val = null;
            source = null;
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
                /* absent: the caller keeps the current behaviour */
            }
        }

        private static void ReadMultiSz(string subKey, string value,
            out List<string> list, out string? source)
        {
            list = new List<string>();
            source = null;
            try
            {
                using RegistryKey? k = Registry.CurrentUser.OpenSubKey(subKey);
                if (k?.GetValue(value) is string[] arr)
                {
                    foreach (string s in arr)
                    {
                        if (!string.IsNullOrWhiteSpace(s))
                        {
                            list.Add(s);
                        }
                    }
                    if (list.Count > 0)
                    {
                        source = subKey;
                    }
                }
            }
            catch
            {
                /* absent: the caller keeps the current behaviour */
            }
        }

        private static void ReadDword(string subKey, string value, out int? val,
            out string? source)
        {
            val = null;
            source = null;
            try
            {
                using RegistryKey? k = Registry.CurrentUser.OpenSubKey(subKey);
                if (k?.GetValue(value) is int raw)
                {
                    val = raw;
                    source = subKey;
                }
            }
            catch
            {
                /* absent: the caller keeps the current behaviour */
            }
        }
    }
}
