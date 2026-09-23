// Win7Taskbar - shared task model: one tracked window
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
using Win7Taskbar.Interop;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>Identity-resolution state of a task entry.</summary>
    internal enum TaskResolveState
    {
        /// <summary>Discovered but not yet merged with any source of truth.</summary>
        Pending,

        /// <summary>A resolution job is queued or running (icon, title, identity).</summary>
        Resolving,

        /// <summary>Identity is known and the last resolution job completed.</summary>
        Resolved,

        /// <summary>The last resolution job failed; the entry keeps its last-known data.</summary>
        Failed,
    }

    /// <summary>Model-level facts that are not window states.</summary>
    [Flags]
    internal enum TaskEntryFlags
    {
        None = 0,

        /// <summary>
        /// The original handle is a hung-window ghost: it stays the identity of
        /// the entry, while queries go through <see cref="TaskEntry.HwndResolve"/>.
        /// </summary>
        IsGhost = 0x0001,
    }

    /// <summary>
    /// One tracked application window with a stable identity, independent of
    /// how it is presented (taskbar button, thumbnail picker, future monitor
    /// views). The model never holds presentation types.
    /// </summary>
    internal sealed class TaskEntry
    {
        private static long s_idCounter;

        public TaskEntry(ulong hwnd)
        {
            Hwnd = hwnd;
            HwndResolve = hwnd;
            Id = unchecked((ulong)System.Threading.Interlocked.Increment(ref s_idCounter));
            ResolveState = TaskResolveState.Pending;
        }

        /// <summary>
        /// Stable identity for views and animations. Unlike the handle, it is
        /// never reused and can outlive the window (logical removal, exit
        /// animation and HWND destruction are distinct events).
        /// </summary>
        public ulong Id { get; }

        /// <summary>
        /// Original window handle: the exact-match key. Kept even when the
        /// window is a ghost (the ghost handle is what the system reports).
        /// </summary>
        public ulong Hwnd { get; }

        /// <summary>
        /// Handle used for queries (title, icon, process): the ghost-resolved
        /// window when <see cref="TaskEntryFlags.IsGhost"/> is set, otherwise
        /// the same as <see cref="Hwnd"/>.
        /// </summary>
        public ulong HwndResolve { get; internal set; }

        /// <summary>
        /// Bumped whenever the identity is re-assigned or the window is
        /// re-discovered: asynchronous results from an older generation are
        /// stale and must not be applied.
        /// </summary>
        public uint Generation { get; private set; }

        public TaskResolveState ResolveState { get; internal set; }

        public TaskEntryFlags Flags { get; internal set; }

        /// <summary>Application identity (AppUserModelID or the core's fallback).</summary>
        public string AppId { get; internal set; } = string.Empty;

        public string ExePath { get; internal set; } = string.Empty;

        /// <summary>Live window caption. Never a cached "friendly" name.</summary>
        public string Title { get; internal set; } = string.Empty;

        /// <summary>Active/minimized/maximized/flashing bits from the core.</summary>
        public WindowStateFlags WindowState { get; internal set; }

        /// <summary>Monitor the window currently sits on (future monitor views).</summary>
        public int MonitorIndex { get; internal set; }

        /// <summary>Changes when the application replaces its icon.</summary>
        public uint IconRevision { get; internal set; }

        /// <summary>Recency of the last activation (ordering/combining policy input).</summary>
        public long LastActivatedTicks { get; internal set; }

        /// <summary>Cache key of the window icon (see <see cref="AppIconCache"/>).</summary>
        public string IconKey => AppIconCache.WindowKey(Hwnd);

        /// <summary>Current model group; null while pending or ungrouped.</summary>
        public AppGroup? Group { get; internal set; }

        public bool IsActive => (WindowState & WindowStateFlags.Active) != 0;

        public bool IsMinimized => (WindowState & WindowStateFlags.Minimized) != 0;

        public bool IsMaximized => (WindowState & WindowStateFlags.Maximized) != 0;

        /// <summary>
        /// True while the window asks for attention (FlashWindowEx) and is not
        /// being watched: the window state keeps the raw bit.
        /// </summary>
        public bool IsAttentionRequested =>
            (WindowState & WindowStateFlags.Flashing) != 0 && !IsActive;

        internal uint BumpGeneration()
        {
            unchecked
            {
                Generation++;
            }
            return Generation;
        }
    }
}
