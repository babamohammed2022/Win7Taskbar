// Win7Taskbar - shared task model: per-view item selection
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
    /// <summary>The presentations that select items from the shared model.</summary>
    internal enum TaskViewKind
    {
        TaskbarButtons,
        ThumbnailPicker,
    }

    /// <summary>
    /// "Which item appears in which view": one shared task identity, several
    /// independently filtered presentations. The taskbar buttons and the
    /// thumbnail picker can legitimately show different selections of the same
    /// group (for example when a group gains child/hosted windows); a future
    /// monitor view filters on <see cref="TaskEntry.MonitorIndex"/>.
    /// </summary>
    internal interface ITaskViewFilter
    {
        TaskViewKind Kind { get; }

        bool Includes(TaskEntry entry, AppGroup group);
    }

    /// <summary>
    /// Top-level taskbar buttons. Every grouped entry is a candidate today:
    /// windows the core does not track never reach the model, so there is
    /// nothing else to exclude yet.
    /// </summary>
    internal sealed class TopLevelButtonsFilter : ITaskViewFilter
    {
        public TaskViewKind Kind => TaskViewKind.TaskbarButtons;

        public bool Includes(TaskEntry entry, AppGroup group)
            => entry != null && group != null;
    }

    /// <summary>
    /// Thumbnail picker entries. The picker selection may differ from the
    /// button selection when the model grows child/hosted-window relations
    /// (the picker then shows the children instead of their container). Such
    /// relations do not exist in the model yet, so this currently matches the
    /// button selection: the contract is the separation itself. Since v2.64
    /// this filter is what ShowWindowPicker actually selects through
    /// (TaskbarViewModel.PickerWindows -> TaskProjection.PickerWindows).
    /// </summary>
    internal sealed class ThumbnailPickerFilter : ITaskViewFilter
    {
        public TaskViewKind Kind => TaskViewKind.ThumbnailPicker;

        public bool Includes(TaskEntry entry, AppGroup group)
            => entry != null && group != null;
    }
}
