// Win7Taskbar - shared task model: one application group
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

using System;
using System.Collections.Generic;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// Application identity and task membership: the model group. It is not a
    /// presentation object; one group can back a single button, a combined
    /// button or a per-window split depending on the view policy.
    /// </summary>
    internal sealed class AppGroup
    {
        private readonly List<TaskEntry> _members = new List<TaskEntry>();

        public AppGroup(string key)
        {
            Key = key ?? string.Empty;
        }

        /// <summary>
        /// Canonical identity of the group. It is the AppUserModelID when the
        /// application declares one, otherwise the executable path; a view
        /// policy can ask for synthetic per-window keys (the entry keeps its
        /// real identity regardless).
        /// </summary>
        public string Key { get; }

        /// <summary>Representative application identity of the members.</summary>
        public string AppId { get; internal set; } = string.Empty;

        /// <summary>Executable path of the first member (display/identity fallback).</summary>
        public string ExePath { get; internal set; } = string.Empty;

        /// <summary>Launch target when idle: .lnk, executable or shell:AppsFolder.</summary>
        public string? LaunchPath { get; internal set; }

        /// <summary>
        /// Model state: a real shell pin claims this group. A pinned group can
        /// exist with zero running windows (idle launcher); a running-only
        /// group cannot. This is never a fake running window.
        /// </summary>
        public bool IsPinned { get; internal set; }

        /// <summary>Cache key of the pin's identity icon (wins over window icons).</summary>
        public string? PinIconKey { get; internal set; }

        /// <summary>Current members, in insertion order.</summary>
        public IReadOnlyList<TaskEntry> Members => _members;

        /// <summary>Membership list owned by the catalog.</summary>
        internal List<TaskEntry> MemberList => _members;

        /// <summary>
        /// True when the group survives empty membership: pinned launchers
        /// stay, ordinary empty groups are removed (they can come back with
        /// their next window).
        /// </summary>
        public bool KeepAlive => IsPinned;

        /// <summary>Most recent activation among the members (0 when empty).</summary>
        public long LastActivatedTicks
        {
            get
            {
                long max = 0;
                foreach (TaskEntry entry in _members)
                {
                    if (entry.LastActivatedTicks > max)
                    {
                        max = entry.LastActivatedTicks;
                    }
                }
                return max;
            }
        }
    }
}
