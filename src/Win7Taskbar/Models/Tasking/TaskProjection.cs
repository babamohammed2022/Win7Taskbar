// Win7Taskbar - shared task model: presentation projection
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
using System.Collections.ObjectModel;
using System.IO;
using Win7Taskbar.Interop;
using System.Linq;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// View policy applied while projecting the model onto buttons: which
    /// windows must get their own button (never-group / exceptions) and which
    /// icon the group button shows. Identity stays on the entries; this only
    /// decides how it is split and dressed on screen.
    /// </summary>
    internal sealed class ProjectionPolicy
    {
        private readonly TaskbarSettings? _settings;
        private readonly GroupIconPolicy? _iconPolicy;

        public ProjectionPolicy(TaskbarSettings? settings, GroupIconPolicy? iconPolicy)
        {
            _settings = settings;
            _iconPolicy = iconPolicy;
        }

        /// <summary>"Use the executable icon for the group" (G4).</summary>
        public bool GroupIconFromExecutable => _iconPolicy?.UseExecutable == true;

        /// <summary>
        /// True when this window must be its own button: "never group"
        /// (TaskbarGlomLevel 0) or an executables listed in
        /// TaskbarExceptionsIcons. With no user configuration this is false
        /// and the normal application grouping applies.
        /// </summary>
        public bool PerWindow(string? exePath)
        {
            if (_settings == null && _iconPolicy == null)
            {
                return false;
            }

            if (_settings?.GlomLevel == 0)
            {
                return true;
            }

            if (_iconPolicy != null && _iconPolicy.Exceptions.Count > 0)
            {
                string name = Path.GetFileName(exePath ?? string.Empty);
                if (_iconPolicy.Exceptions.Contains(name, StringComparer.OrdinalIgnoreCase))
                {
                    return true;
                }
            }

            return false;
        }
    }

    /// <summary>
    /// Mirrors the task catalog onto the bindable objects the theme knows
    /// (TaskGroup/TaskWindow), in place so buttons do not flicker and keep
    /// their hover state. The projection holds no identity decisions: grouping
    /// and pin claiming live in the catalog, per-view selection goes through
    /// <see cref="ITaskViewFilter"/>.
    /// </summary>
    internal sealed class TaskProjection : IDisposable
    {
        /* Diagnostic switch (W7T_DEBUG_FLASH=1): show the first inactive
         * window as flashing, to verify the rendering where flash
         * notifications do not arrive (e.g. under Wine). No effect in normal
         * use. */
        private static readonly bool DebugForceFlash =
            Environment.GetEnvironmentVariable("W7T_DEBUG_FLASH") == "1";

        private readonly TaskCatalog _catalog;
        private readonly AppIconCache _icons;
        private readonly Action<TaskEntry, ResolveReason, bool> _requestResolve;
        private readonly ITaskViewFilter _filter = new TopLevelButtonsFilter();
        private readonly ITaskViewFilter _pickerFilter = new ThumbnailPickerFilter();
        private readonly Dictionary<AppGroup, TaskGroup> _map = new Dictionary<AppGroup, TaskGroup>();
        private readonly Dictionary<ulong, TaskWindow> _windows = new Dictionary<ulong, TaskWindow>();
        private bool _disposed;

        public TaskProjection(TaskCatalog catalog, AppIconCache icons,
                              Action<TaskEntry, ResolveReason, bool> requestResolve)
        {
            _catalog = catalog ?? throw new ArgumentNullException(nameof(catalog));
            _icons = icons ?? throw new ArgumentNullException(nameof(icons));
            _requestResolve = requestResolve ?? throw new ArgumentNullException(nameof(requestResolve));

            // Targeted updates when the async resolver completes: one entry's
            // bindable moves without a full refresh.
            _catalog.EntryChanged += OnEntryChanged;
            _catalog.GroupChanged += OnGroupChanged;
        }

        /// <summary>The collection the theme binds to (same shape as before).</summary>
        public ObservableCollection<TaskGroup> Groups { get; } = new ObservableCollection<TaskGroup>();

        /// <summary>
        /// Full mirror pass, update-in-place: new groups append at the end (the
        /// ordering layer runs afterwards), existing objects are never
        /// recreated.
        /// </summary>
        public void Sync(ProjectionPolicy? policy)
        {
            if (_disposed)
            {
                return;
            }

            MirrorGroups();
            MirrorWindows(policy ?? new ProjectionPolicy(null, null));
        }

        /// <summary>
        /// The thumbnail-picker projection of a group (v2.64): the windows
        /// the picker lists, selected through
        /// <see cref="ThumbnailPickerFilter"/> instead of the buttons'
        /// filter, so the two views can diverge when the model grows
        /// child/hosted-window relations. Today's selection equals the
        /// button selection by contract (see ITaskViewFilter): a snapshot of
        /// the live <see cref="TaskWindow"/> instances in catalog order,
        /// taken at call time — their property changes keep flowing to the
        /// open popup, only membership is not observed.
        /// </summary>
        public IReadOnlyList<TaskWindow> PickerWindows(TaskGroup view)
        {
            var list = new List<TaskWindow>();
            if (_disposed || view == null)
            {
                return list;
            }

            AppGroup? model = FindModelOf(view);
            if (model == null)
            {
                return list;
            }

            foreach (TaskEntry entry in _catalog.SelectView(model, _pickerFilter))
            {
                if (_windows.TryGetValue(entry.Hwnd, out TaskWindow? window) &&
                    view.Windows.Contains(window))
                {
                    list.Add(window);
                }
            }

            return list;
        }

        private void MirrorGroups()
        {
            var live = new HashSet<AppGroup>(_catalog.Groups);

            for (int i = Groups.Count - 1; i >= 0; i--)
            {
                TaskGroup view = Groups[i];
                AppGroup? model = FindModelOf(view);
                if (model == null || !live.Contains(model))
                {
                    DropView(view, model);
                    Groups.RemoveAt(i);
                }
            }

            foreach (AppGroup group in _catalog.Groups)
            {
                if (!_map.TryGetValue(group, out TaskGroup? view))
                {
                    // Same constructor inputs as before: button identity key
                    // and the representative executable path.
                    view = new TaskGroup(group.Key, group.ExePath);
                    _map[group] = view;
                    Groups.Add(view);
                }
            }
        }

        private void MirrorWindows(ProjectionPolicy policy)
        {
            foreach (AppGroup group in _catalog.Groups)
            {
                if (!_map.TryGetValue(group, out TaskGroup? view))
                {
                    continue;
                }

                view.IsPinned = group.IsPinned;
                if (group.IsPinned && !string.IsNullOrEmpty(group.LaunchPath))
                {
                    view.LaunchPath ??= group.LaunchPath;
                }

                var seen = new HashSet<ulong>();
                foreach (TaskEntry entry in _catalog.SelectView(group, _filter))
                {
                    seen.Add(entry.Hwnd);
                    EnsureWindow(view, entry);
                }

                for (int i = view.Windows.Count - 1; i >= 0; i--)
                {
                    TaskWindow window = view.Windows[i];
                    if (!seen.Contains(window.Hwnd))
                    {
                        view.Windows.RemoveAt(i);
                        if (_windows.TryGetValue(window.Hwnd, out TaskWindow? known) &&
                            ReferenceEquals(known, window))
                        {
                            _windows.Remove(window.Hwnd);
                        }
                    }
                }

                ApplyGroupIcon(group, view, policy);
                view.RefreshAggregateState();
            }
        }

        private void ApplyGroupIcon(AppGroup group, TaskGroup view, ProjectionPolicy policy)
        {
            // Pinned groups: the pin's identity icon always wins (v2.26 rule).
            if (group.IsPinned && !string.IsNullOrEmpty(group.PinIconKey))
            {
                var pinIcon = _icons.Get(group.PinIconKey);
                if (pinIcon != null)
                {
                    view.Icon = pinIcon;
                }
            }

            // The group button falls back to the first window icon available.
            view.Icon ??= view.Windows.Select(w => w.Icon).FirstOrDefault(icon => icon != null);

            // "Use the executable for the group icon" (G4): the group button
            // shows the executable's own icon. The pin's identity icon above
            // always wins: pinned groups keep their shortcut icon.
            if (policy.GroupIconFromExecutable && !group.IsPinned &&
                !string.IsNullOrEmpty(group.ExePath))
            {
                var exeIcon = _icons.Get(AppIconCache.ExeKey(group.ExePath));
                if (exeIcon != null)
                {
                    view.Icon = exeIcon;
                }
                else if (group.Members.Count > 0)
                {
                    // Materialize off the UI thread; the completion updates us.
                    _requestResolve(group.Members[0], ResolveReason.IconStale, true);
                }
            }
        }

        private void EnsureWindow(TaskGroup view, TaskEntry entry)
        {
            TaskWindow? window = view.Windows.FirstOrDefault(w => w.Hwnd == entry.Hwnd);
            if (window == null)
            {
                window = new TaskWindow(entry.Hwnd, entry.AppId);
                view.Windows.Add(window);
                _windows[entry.Hwnd] = window;
            }

            UpdateWindow(window, entry);
        }

        private void UpdateWindow(TaskWindow window, TaskEntry entry)
        {
            window.Title = entry.Title ?? string.Empty;

            // Reuse the identity already resolved by the model; no extra
            // process query here. Title stays the live caption; the friendly
            // name is metadata only (its cache is warmed by the resolver).
            window.ApplicationName = TaskGroup.ResolveFriendlyApplicationName(
                entry.ExePath, window.Title, entry.AppId);

            ApplyWindowState(window, entry);

            // Never overwrite a good icon with null; fill at most once (the
            // historical behaviour), asynchronously when needed.
            if (window.Icon == null)
            {
                var icon = _icons.Get(entry.IconKey);
                if (icon != null)
                {
                    window.Icon = icon;
                }
                else
                {
                    _requestResolve(entry, ResolveReason.IconStale, false);
                }
            }
        }

        private static void ApplyWindowState(TaskWindow window, TaskEntry entry)
        {
            var state = entry.WindowState;
            window.IsActive = (state & WindowStateFlags.Active) != 0;
            window.IsMinimized = (state & WindowStateFlags.Minimized) != 0;
            window.IsMaximized = (state & WindowStateFlags.Maximized) != 0;

            // An active window never flashes: Windows drops the attention
            // request as soon as the user looks at it.
            window.IsFlashing = entry.IsAttentionRequested;

            if (DebugForceFlash && !window.IsActive)
            {
                window.IsFlashing = true;
            }
        }

        private void OnEntryChanged(object? sender, TaskEntryEventArgs e)
        {
            if (_disposed)
            {
                return;
            }

            if (_windows.TryGetValue(e.Entry.Hwnd, out TaskWindow? window))
            {
                UpdateWindow(window, e.Entry);
                if (e.Entry.Group != null &&
                    _map.TryGetValue(e.Entry.Group, out TaskGroup? view))
                {
                    view.RefreshAggregateState();
                }
            }
        }

        private void OnGroupChanged(object? sender, TaskGroupEventArgs e)
        {
            // Group identity/pin/membership changed in the model: the next
            // Sync mirrors it. The event exists for views that want to react
            // sooner (thumbnail picker, exit animations).
        }

        private AppGroup? FindModelOf(TaskGroup view)
        {
            foreach (KeyValuePair<AppGroup, TaskGroup> pair in _map)
            {
                if (ReferenceEquals(pair.Value, view))
                {
                    return pair.Key;
                }
            }

            return null;
        }

        private void DropView(TaskGroup view, AppGroup? model)
        {
            if (model != null)
            {
                _map.Remove(model);
            }

            foreach (TaskWindow window in view.Windows)
            {
                if (_windows.TryGetValue(window.Hwnd, out TaskWindow? known) &&
                    ReferenceEquals(known, window))
                {
                    _windows.Remove(window.Hwnd);
                }
            }

            view.Windows.Clear();
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            _catalog.EntryChanged -= OnEntryChanged;
            _catalog.GroupChanged -= OnGroupChanged;
            Groups.Clear();
            _map.Clear();
            _windows.Clear();
        }
    }
}
