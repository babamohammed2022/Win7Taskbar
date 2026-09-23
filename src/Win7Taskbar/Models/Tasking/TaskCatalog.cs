// Win7Taskbar - shared task model: the task catalog
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
using Win7Taskbar.Interop;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// The shared task model: one identity per window, one identity per
    /// application group, independent of any presentation. Discovery
    /// ("insert"), identity resolution and presentation are three distinct
    /// steps: a window becomes a Pending entry immediately, the async resolver
    /// completes its identity, and views read the result through filters.
    ///
    /// Threading: every member must be called on the UI (dispatcher) thread;
    /// async results come back through <see cref="CompleteResolution"/>, also
    /// on that thread. There are no locks to take.
    ///
    /// Removal contract: <see cref="EntryRemoving"/> fires while the model
    /// still contains the entry and its group relationship - listeners can
    /// inspect what is about to leave (that is what drives transitions like
    /// "last window -> idle launcher"). Only then is the membership mutated.
    /// </summary>
    internal sealed class TaskCatalog : IDisposable
    {
        private const StringComparison Oic = StringComparison.OrdinalIgnoreCase;

        private readonly List<TaskEntry> _entries = new List<TaskEntry>();
        private readonly List<AppGroup> _groups = new List<AppGroup>();
        private readonly Dictionary<ulong, TaskEntry> _byHwnd = new Dictionary<ulong, TaskEntry>();
        private bool _disposed;

        public TaskMatcher Matcher { get; } = new TaskMatcher();

        public IReadOnlyList<AppGroup> Groups => _groups;

        public IReadOnlyList<TaskEntry> Entries => _entries;

        /// <summary>
        /// Hook used to queue asynchronous resolution work (icon, title,
        /// identity) - wired to the TaskResolver by the owner. Called for
        /// discovered entries and for entries whose icon is not materialized.
        /// </summary>
        public Action<TaskEntry, ResolveReason, bool>? ResolveRequested { get; set; }

        public event EventHandler<TaskEntryEventArgs>? EntryAppeared;

        public event EventHandler<TaskEntryEventArgs>? EntryResolved;

        public event EventHandler<TaskEntryEventArgs>? EntryChanged;

        /// <summary>Fires BEFORE the entry is removed from the model.</summary>
        public event EventHandler<TaskEntryEventArgs>? EntryRemoving;

        public event EventHandler<TaskGroupEventArgs>? GroupChanged;

        // ----------------------------------------------------------------
        //  Event dispatch
        //
        //  Each handler runs on its own, guarded: one broken listener must
        //  never abort a model mutation half-way (removal notifies BEFORE
        //  mutating - the entry must still go away when a handler fails) and
        //  must never take down the caller (core pump or dispatcher).
        // ----------------------------------------------------------------

        private void RaiseEntry(EventHandler<TaskEntryEventArgs>? handlers,
                                TaskEntry entry)
        {
            if (handlers == null)
            {
                return;
            }

            var args = new TaskEntryEventArgs(entry);
            foreach (Delegate handler in handlers.GetInvocationList())
            {
                try
                {
                    ((EventHandler<TaskEntryEventArgs>)handler)(this, args);
                }
                catch
                {
                    // Best-effort notification: see the note above.
                }
            }
        }

        private void RaiseGroup(EventHandler<TaskGroupEventArgs>? handlers,
                                AppGroup group)
        {
            if (handlers == null)
            {
                return;
            }

            var args = new TaskGroupEventArgs(group);
            foreach (Delegate handler in handlers.GetInvocationList())
            {
                try
                {
                    ((EventHandler<TaskGroupEventArgs>)handler)(this, args);
                }
                catch
                {
                    // Best-effort notification: see the note above.
                }
            }
        }

        private void RequestResolve(TaskEntry entry, ResolveReason reason,
                                    bool wantExeIcon)
        {
            try
            {
                ResolveRequested?.Invoke(entry, reason, wantExeIcon);
            }
            catch
            {
                // Scheduling is best-effort; the next refresh re-queues what
                // is still missing.
            }
        }

        // ----------------------------------------------------------------
        //  Discovery (insert intent)
        // ----------------------------------------------------------------

        /// <summary>
        /// Get-or-add for a discovered window: returns a Pending entry
        /// immediately (the caller can bind to it at once) and queues
        /// asynchronous resolution. Ghost resolution happens here: the ghost
        /// handle stays the identity, the resolved handle is for queries.
        /// </summary>
        public TaskEntry? Discover(ulong hwnd)
        {
            if (hwnd == 0)
            {
                return null;
            }

            if (_byHwnd.TryGetValue(hwnd, out TaskEntry? existing))
            {
                return existing;
            }

            TaskEntry entry = CreateEntry(hwnd);
            entry.ResolveState = TaskResolveState.Resolving;
            RaiseEntry(EntryAppeared, entry);
            RequestResolve(entry, ResolveReason.Discovered, false);
            return entry;
        }

        private TaskEntry CreateEntry(ulong hwnd)
        {
            var entry = new TaskEntry(hwnd);

            if (GhostHandle.IsGhostWindow(hwnd))
            {
                entry.Flags |= TaskEntryFlags.IsGhost;
                if (GhostHandle.TryResolve(hwnd, out ulong realHwnd))
                {
                    entry.HwndResolve = realHwnd;
                }
                // Without the optional export the ghost keeps the identity and
                // hung windows are not queried (IsHung guards the workers).
            }

            _byHwnd[hwnd] = entry;
            _entries.Add(entry);
            return entry;
        }

        // ----------------------------------------------------------------
        //  Matching
        // ----------------------------------------------------------------

        /// <summary>
        /// Looks up a tracked window with a minimum confidence: exact window
        /// first (the pending state is discoverable by handle before its
        /// identity resolves), then identity matching over the groups.
        /// </summary>
        public MatchResult MatchWindow(ulong hwnd, MatchConfidence min)
        {
            if (_byHwnd.TryGetValue(hwnd, out TaskEntry? entry))
            {
                if (MatchConfidence.ByHwnd >= min)
                {
                    return new MatchResult(entry.Group, entry, MatchConfidence.ByHwnd);
                }

                MatchResult byIdentity = Matcher.MatchGroup(
                    _groups, TaskIdentity.From(entry), min);
                return byIdentity.Found
                    ? new MatchResult(byIdentity.Group, entry, byIdentity.Confidence)
                    : default;
            }

            return default;
        }

        /// <summary>
        /// Finds a group for the identity at the given confidence, otherwise
        /// creates one (the "assign to a group" half of insert).
        /// </summary>
        public AppGroup MatchOrCreateGroup(in TaskIdentity id, MatchConfidence min)
        {
            MatchResult match = Matcher.MatchGroup(_groups, id, min);
            if (match.Group != null)
            {
                return match.Group;
            }

            string key = !string.IsNullOrEmpty(id.AppId) ? id.AppId : id.ExePath;
            var group = new AppGroup(key)
            {
                AppId = id.AppId,
                ExePath = id.ExePath,
            };
            _groups.Add(group);
            RaiseGroup(GroupChanged, group);
            return group;
        }

        /// <summary>
        /// The set of entries a given view shows for a group: the same model,
        /// independently filtered per presentation.
        /// </summary>
        public IEnumerable<TaskEntry> SelectView(AppGroup group, ITaskViewFilter filter)
        {
            if (group == null || filter == null)
            {
                yield break;
            }

            foreach (TaskEntry entry in group.Members)
            {
                if (filter.Includes(entry, group))
                {
                    yield return entry;
                }
            }
        }

        // ----------------------------------------------------------------
        //  Snapshot reconciliation (update in place, never rebuild)
        // ----------------------------------------------------------------

        /// <summary>
        /// Merges a core snapshot (plus the current pin set) into the model.
        /// Entries are added/updated/removed first; the grouping pass then
        /// applies pin claiming and window-to-group assignment with the
        /// historical criteria and order preserved.
        /// </summary>
        public void Reconcile(IReadOnlyList<W7TWindowInfo>? snapshot,
                              IReadOnlyList<PinInfo>? pins,
                              ProjectionPolicy? policy)
        {
            snapshot ??= Array.Empty<W7TWindowInfo>();
            pins ??= Array.Empty<PinInfo>();
            policy ??= new ProjectionPolicy(null, null);

            MergeEntries(snapshot);
            ClaimPins(pins);
            AssignGroups(snapshot, pins, policy);
        }

        private void MergeEntries(IReadOnlyList<W7TWindowInfo> snapshot)
        {
            var live = new HashSet<ulong>();

            foreach (W7TWindowInfo info in snapshot)
            {
                if (info.Hwnd == 0)
                {
                    continue;
                }

                live.Add(info.Hwnd);

                if (!_byHwnd.TryGetValue(info.Hwnd, out TaskEntry? entry))
                {
                    entry = CreateEntry(info.Hwnd);
                    ApplySnapshot(entry, info);
                    entry.ResolveState = TaskResolveState.Resolving;
                    RaiseEntry(EntryAppeared, entry);
                    RequestResolve(entry, ResolveReason.Discovered, false);
                    continue;
                }

                bool changed = ApplySnapshot(entry, info);
                if (changed)
                {
                    RaiseEntry(EntryChanged, entry);
                }
            }

            for (int i = _entries.Count - 1; i >= 0; i--)
            {
                if (!live.Contains(_entries[i].Hwnd))
                {
                    RemoveEntryCore(_entries[i].Hwnd);
                }
            }
        }

        /// <summary>
        /// Copies the snapshot's facts into the entry. An identity change
        /// bumps the generation, so results of older resolution jobs are
        /// dropped instead of being applied blindly.
        /// </summary>
        private static bool ApplySnapshot(TaskEntry entry, W7TWindowInfo info)
        {
            bool changed = false;

            string appId = info.AppId ?? string.Empty;
            string exePath = info.ExePath ?? string.Empty;
            if (!string.Equals(entry.AppId, appId, Oic) ||
                !string.Equals(entry.ExePath, exePath, Oic))
            {
                entry.BumpGeneration();
                entry.AppId = appId;
                entry.ExePath = exePath;
                changed = true;
            }

            string title = info.Title ?? string.Empty;
            if (!string.Equals(entry.Title, title, StringComparison.Ordinal))
            {
                entry.Title = title;
                changed = true;
            }

            var state = (WindowStateFlags)info.State;
            if (entry.WindowState != state)
            {
                if ((state & WindowStateFlags.Active) != 0 &&
                    (entry.WindowState & WindowStateFlags.Active) == 0)
                {
                    entry.LastActivatedTicks = DateTime.UtcNow.Ticks;
                }

                entry.WindowState = state;
                changed = true;
            }

            if (entry.MonitorIndex != info.MonitorIndex)
            {
                entry.MonitorIndex = info.MonitorIndex;
                changed = true;
            }

            if (entry.IconRevision != info.IconRevision)
            {
                entry.IconRevision = info.IconRevision;
                changed = true;
            }

            if (entry.ResolveState == TaskResolveState.Pending && !string.IsNullOrEmpty(appId))
            {
                entry.ResolveState = TaskResolveState.Resolved;
            }

            return changed;
        }

        /// <summary>
        /// Pin claiming (order preserved from the historical refresh): each
        /// pin claims an existing group by identity, an Explorer special case,
        /// or becomes an idle pinned group of its own. A pinned group can
        /// exist with zero running windows. Pins that disappeared drop their
        /// claim (the group goes back to running-only).
        /// </summary>
        private void ClaimPins(IReadOnlyList<PinInfo> pins)
        {
            foreach (PinInfo pin in pins)
            {
                AppGroup? group = null;
                foreach (AppGroup candidate in _groups)
                {
                    if (TaskMatcher.PinMatches(pin, candidate))
                    {
                        group = candidate;
                        break;
                    }
                }

                if (group == null)
                {
                    // An explorer.exe window group born before the pin list was
                    // refreshed is the Explorer app: the "Esplora file" pin
                    // claims it instead of creating a second button.
                    foreach (AppGroup candidate in _groups)
                    {
                        if (!candidate.IsPinned &&
                            TaskMatcher.IsExplorerPin(pin, candidate.ExePath ?? string.Empty))
                        {
                            group = candidate;
                            break;
                        }
                    }
                }

                if (group == null)
                {
                    group = new AppGroup(pin.AppId)
                    {
                        AppId = pin.AppId ?? string.Empty,
                        ExePath = pin.TargetPath ?? string.Empty,
                        IsPinned = true,
                        LaunchPath = pin.LnkPath,
                        PinIconKey = AppIconCache.PinKey(pin.LnkPath ?? string.Empty),
                    };
                    _groups.Add(group);
                    RaiseGroup(GroupChanged, group);
                }
                else
                {
                    group.IsPinned = true;
                    group.LaunchPath ??= pin.LnkPath;
                    // The pin icon IS the app identity: it always wins over the
                    // windows' content icon (Esplora file opening "This PC"
                    // must show Explorer's icon, not the content's).
                    group.PinIconKey = AppIconCache.PinKey(pin.LnkPath ?? string.Empty);
                }
            }

            foreach (AppGroup group in _groups)
            {
                if (group.IsPinned)
                {
                    bool stillPinned = false;
                    foreach (PinInfo pin in pins)
                    {
                        if (TaskMatcher.PinMatches(pin, group))
                        {
                            stillPinned = true;
                            break;
                        }
                    }

                    if (!stillPinned)
                    {
                        group.IsPinned = false;
                        group.PinIconKey = null;
                    }
                }
            }
        }

        /// <summary>
        /// Window-to-group assignment. Buckets are keyed by application
        /// identity (or a synthetic per-window key when the view policy asks
        /// for per-window buttons); several buckets can land on the same group
        /// through pin affinity, and membership is synced once per group with
        /// the union (so multi-identity applications keep one button).
        /// </summary>
        private void AssignGroups(IReadOnlyList<W7TWindowInfo> snapshot,
                                  IReadOnlyList<PinInfo> pins,
                                  ProjectionPolicy policy)
        {
            // Buckets preserve first-occurrence order, like GroupBy.
            var buckets = new List<KeyValuePair<string, List<TaskEntry>>>();
            var bucketIndex = new Dictionary<string, List<TaskEntry>>(StringComparer.OrdinalIgnoreCase);
            var entryByHwnd = _byHwnd;

            foreach (W7TWindowInfo info in snapshot)
            {
                if (!entryByHwnd.TryGetValue(info.Hwnd, out TaskEntry? entry) ||
                    string.IsNullOrEmpty(entry.AppId))
                {
                    // Entries without an application identity are tracked but
                    // never grouped (they show in no view).
                    continue;
                }

                string key = policy.PerWindow(entry.ExePath)
                    ? PerWindowKey(entry.Hwnd)
                    : entry.AppId;

                if (!bucketIndex.TryGetValue(key, out List<TaskEntry>? bucket))
                {
                    bucket = new List<TaskEntry>();
                    bucketIndex[key] = bucket;
                    buckets.Add(new KeyValuePair<string, List<TaskEntry>>(key, bucket));
                }

                bucket.Add(entry);
            }

            // Keys currently in use: a non-pinned group whose key vanished is
            // removed even before its membership is touched (pinned groups
            // always survive).
            foreach (AppGroup group in _groups.ToArray())
            {
                if (!group.IsPinned && !bucketIndex.ContainsKey(group.Key))
                {
                    _groups.Remove(group);
                    foreach (TaskEntry entry in group.MemberList)
                    {
                        entry.Group = null;
                    }

                    group.MemberList.Clear();
                    RaiseGroup(GroupChanged, group);
                }
            }

            var assigned = new Dictionary<AppGroup, List<TaskEntry>>();
            var touched = new HashSet<AppGroup>();

            foreach (KeyValuePair<string, List<TaskEntry>> bucket in buckets)
            {
                string key = bucket.Key;
                string firstExe = bucket.Value.Count > 0
                    ? bucket.Value[0].ExePath ?? string.Empty
                    : string.Empty;

                AppGroup? group = null;
                foreach (AppGroup candidate in _groups)
                {
                    if (string.Equals(candidate.Key, key, Oic))
                    {
                        group = candidate;
                        break;
                    }
                }

                if (group == null)
                {
                    bool perWindowKey = key.StartsWith(PerWindowPrefix, StringComparison.Ordinal);
                    PinInfo? viaPin = null;

                    if (!perWindowKey)
                    {
                        foreach (PinInfo p in pins)
                        {
                            if (string.Equals(p.AppId, key, Oic) ||
                                (!string.IsNullOrEmpty(p.TargetPath) &&
                                 string.Equals(p.TargetPath, key, Oic)) ||
                                TaskMatcher.SameApp(p.TargetPath ?? string.Empty, firstExe) ||
                                TaskMatcher.IsExplorerPin(p, firstExe))
                            {
                                viaPin = p;
                                break;
                            }
                        }
                    }

                    // The window may belong to an app already present as a PIN
                    // with a different identity (shortcut AppUserModelID vs the
                    // path the core uses): claim the pinned group instead of
                    // creating a duplicate button.
                    if (!perWindowKey && viaPin == null &&
                        Matcher.ConsumeLaunchAffinity(firstExe, pins, out PinInfo? claimed) &&
                        claimed != null)
                    {
                        viaPin = claimed;
                    }

                    if (viaPin != null)
                    {
                        foreach (AppGroup candidate in _groups)
                        {
                            if (TaskMatcher.PinMatches(viaPin, candidate))
                            {
                                group = candidate;
                                break;
                            }
                        }
                    }
                }

                if (group == null)
                {
                    group = new AppGroup(key)
                    {
                        AppId = bucket.Value.Count > 0 ? bucket.Value[0].AppId : string.Empty,
                        ExePath = firstExe,
                    };
                    _groups.Add(group);
                    RaiseGroup(GroupChanged, group);
                }

                if (!assigned.TryGetValue(group, out List<TaskEntry>? members))
                {
                    members = new List<TaskEntry>();
                    assigned[group] = members;
                }

                // One membership sync per group, with every window that
                // belongs to it (even from different native AppIds).
                foreach (TaskEntry entry in bucket.Value)
                {
                    if (!members.Contains(entry))
                    {
                        members.Add(entry);
                    }
                }

                touched.Add(group);
            }

            foreach (AppGroup group in _groups)
            {
                if (!touched.Contains(group))
                {
                    assigned[group] = new List<TaskEntry>();
                }
            }

            foreach (KeyValuePair<AppGroup, List<TaskEntry>> pair in assigned)
            {
                SyncMembership(pair.Key, pair.Value);
            }
        }

        /// <summary>Per-window button identity (view policy key).</summary>
        internal const string PerWindowPrefix = "w7t:win:";

        internal static string PerWindowKey(ulong hwnd) => PerWindowPrefix + hwnd.ToString("x");

        /// <summary>
        /// Membership is replaced in one step per group (adding and removing),
        /// so windows assigned from different buckets do not cancel each other
        /// out. Model membership changes are distinct from entry lifetime.
        /// </summary>
        private void SyncMembership(AppGroup group, List<TaskEntry> members)
        {
            bool changed = false;

            for (int i = group.MemberList.Count - 1; i >= 0; i--)
            {
                TaskEntry member = group.MemberList[i];
                if (!members.Contains(member))
                {
                    group.MemberList.RemoveAt(i);
                    member.Group = null;
                    changed = true;
                }
            }

            foreach (TaskEntry entry in members)
            {
                if (!group.MemberList.Contains(entry))
                {
                    group.MemberList.Add(entry);
                    entry.Group = group;
                    changed = true;
                }
                else if (entry.Group != group)
                {
                    entry.Group = group;
                    changed = true;
                }
            }

            if (changed)
            {
                RaiseGroup(GroupChanged, group);
            }
        }

        // ----------------------------------------------------------------
        //  Async completion (the posted-result contract)
        // ----------------------------------------------------------------

        /// <summary>
        /// Applies a resolution result. Every arrival is validated against the
        /// CURRENT model: a result for a window that died is dropped, a result
        /// from an older generation is dropped (the identity moved on), and an
        /// identity change regroups the entry explicitly instead of assuming
        /// the old group still fits.
        /// </summary>
        public void CompleteResolution(ResolveResult? result)
        {
            if (result == null || result.Hwnd == 0)
            {
                return;
            }

            if (!_byHwnd.TryGetValue(result.Hwnd, out TaskEntry? entry))
            {
                // The task disappeared before completion: clean up, never
                // recreate it from the result alone.
                return;
            }

            if (entry.Generation != result.Generation)
            {
                return;
            }

            if (result.Outcome == TaskResolveOutcome.Stale)
            {
                return;
            }

            if (result.Outcome == TaskResolveOutcome.Failed)
            {
                entry.ResolveState = TaskResolveState.Failed;
                return;
            }

            bool changed = false;
            bool regroup = false;

            if (!string.IsNullOrEmpty(result.Title) &&
                !string.Equals(entry.Title, result.Title, StringComparison.Ordinal))
            {
                entry.Title = result.Title;
                changed = true;
            }

            // Identity refinement: a better identity than the one the snapshot
            // carried moves the item to the matching group (or a new one).
            if (!string.IsNullOrEmpty(result.AppId) &&
                !string.Equals(result.AppId, entry.AppId, Oic))
            {
                entry.BumpGeneration();
                entry.AppId = result.AppId;
                changed = true;
                regroup = true;
            }

            if (!string.IsNullOrEmpty(result.ExePath) &&
                !string.Equals(result.ExePath, entry.ExePath, Oic))
            {
                if (!regroup)
                {
                    entry.BumpGeneration();
                }

                entry.ExePath = result.ExePath;
                changed = true;
                regroup = true;
            }

            entry.IconRevision = result.IconRevision;
            entry.ResolveState = TaskResolveState.Resolved;

            if (regroup && entry.Group != null)
            {
                AppGroup destination = MatchOrCreateGroup(
                    TaskIdentity.From(entry), MatchConfidence.ByExeName);

                AppGroup? previous = entry.Group;
                previous.MemberList.Remove(entry);
                entry.Group = null;
                if (!destination.MemberList.Contains(entry))
                {
                    destination.MemberList.Add(entry);
                }
                entry.Group = destination;

                RaiseGroup(GroupChanged, previous);
                RaiseGroup(GroupChanged, destination);
            }

            RaiseEntry(EntryResolved, entry);
            if (changed)
            {
                RaiseEntry(EntryChanged, entry);
            }
        }

        // ----------------------------------------------------------------
        //  Removal (notify first, mutate after)
        // ----------------------------------------------------------------

        /// <summary>
        /// Logical removal of a task. The notification sees the entry and its
        /// group/tab relationships intact; only afterwards is the membership
        /// mutated and the empty group dropped (pinned groups survive, they
        /// become idle launchers again).
        /// </summary>
        public bool RemoveEntry(ulong hwnd)
            => RemoveEntryCore(hwnd);

        private bool RemoveEntryCore(ulong hwnd)
        {
            if (!_byHwnd.TryGetValue(hwnd, out TaskEntry? entry))
            {
                return false;
            }

            RaiseEntry(EntryRemoving, entry);

            _byHwnd.Remove(hwnd);
            _entries.Remove(entry);

            AppGroup? group = entry.Group;
            if (group != null)
            {
                group.MemberList.Remove(entry);
                entry.Group = null;
            }

            entry.BumpGeneration();
            entry.ResolveState = TaskResolveState.Failed;

            if (group != null && group.MemberList.Count == 0 && !group.KeepAlive)
            {
                _groups.Remove(group);
                RaiseGroup(GroupChanged, group);
            }
            else if (group != null)
            {
                RaiseGroup(GroupChanged, group);
            }

            return true;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            ResolveRequested = null;
            EntryAppeared = null;
            EntryResolved = null;
            EntryChanged = null;
            EntryRemoving = null;
            GroupChanged = null;
            foreach (AppGroup group in _groups)
            {
                group.MemberList.Clear();
            }
            _groups.Clear();
            _entries.Clear();
            _byHwnd.Clear();
        }
    }

    internal sealed class TaskEntryEventArgs : EventArgs
    {
        public TaskEntryEventArgs(TaskEntry entry)
        {
            Entry = entry;
        }

        public TaskEntry Entry { get; }
    }

    internal sealed class TaskGroupEventArgs : EventArgs
    {
        public TaskGroupEventArgs(AppGroup group)
        {
            Group = group;
        }

        public AppGroup Group { get; }
    }
}
