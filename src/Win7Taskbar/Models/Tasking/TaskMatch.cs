// Win7Taskbar - shared task model: identity matching vocabulary
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

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// How strongly two identities agree. The values are ordered: a match is
    /// accepted when its confidence reaches the requested minimum. Matching is
    /// "first group meeting the minimum", not a global best-match search.
    /// </summary>
    internal enum MatchConfidence
    {
        None = 0,
        ByExeName = 1,
        ByExePath = 2,
        ByAppId = 3,
        ByHwnd = 4,
    }

    /// <summary>Resolved identity of a window, used by the matcher.</summary>
    internal readonly struct TaskIdentity
    {
        public TaskIdentity(ulong hwnd, string appId, string exePath)
        {
            Hwnd = hwnd;
            AppId = appId ?? string.Empty;
            ExePath = exePath ?? string.Empty;
        }

        public ulong Hwnd { get; }
        public string AppId { get; }
        public string ExePath { get; }

        public static TaskIdentity From(TaskEntry entry)
            => new TaskIdentity(entry.Hwnd, entry.AppId, entry.ExePath);
    }

    /// <summary>Outcome of a matching query.</summary>
    internal readonly struct MatchResult
    {
        public MatchResult(AppGroup? group, TaskEntry? entry, MatchConfidence confidence)
        {
            Group = group;
            Entry = entry;
            Confidence = confidence;
        }

        public AppGroup? Group { get; }
        public TaskEntry? Entry { get; }
        public MatchConfidence Confidence { get; }
        public bool Found => Confidence != MatchConfidence.None;
    }
}
