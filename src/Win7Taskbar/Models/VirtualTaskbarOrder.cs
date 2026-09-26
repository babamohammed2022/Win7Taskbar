// Win7Taskbar - virtual order of the taskbar icons
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
using System.Linq;

namespace Win7Taskbar.Models
{
    /// <summary>
    /// v1.21.7 - Ordering layer of the software taskbar.
    ///
    /// WHAT IT IS (AND WHAT IT IS NOT)
    ///
    ///   pinned items / window discovery          (native, unchanged)
    ///                 |
    ///   existing application model               (TaskbarViewModel, unchanged)
    ///                 |
    ///   ORDERING LAYER                           (this file)
    ///                 |
    ///   the Win7Taskbar bar                      (ItemsControl, unchanged)
    ///
    /// The order decides ONLY how the items are laid out in the Win7Taskbar
    /// bar. It does not touch the Windows taskbar, it does not touch Explorer
    /// and it does not use the Windows pinning system: the previous order is
    /// still computed as always (pins in folder order, then running
    /// applications) and it is merely reordered here according to the user's
    /// choice, which is stored in settings.json.
    ///
    /// HOW ITEMS ARE RECOGNISED
    ///
    /// The stored order is a list of KEYS, not of positions: a position
    /// changes by itself when a window opens or closes. The key reuses the
    /// identification the project already has (TaskbarViewModel.PinMatches /
    /// SameApp): first the Application ID (AppUserModelID) of the group, then
    /// the executable path, then the launch .lnk of the "shell item" pins
    /// (which have no path at all).
    ///
    /// APPLICATIONS THAT ARE GONE
    ///
    /// Keys that match no live item stay in the configuration: nothing is
    /// deleted. If an application is uninstalled or its shortcut stops
    /// resolving, its key simply stops having an effect; if the application
    /// comes back, it returns to its place.
    /// </summary>
    internal static class VirtualTaskbarOrder
    {
        /// <summary>
        /// Stable key of a group. Empty string = an item that cannot be
        /// ordered: it is never stored and it always stays where discovery put
        /// it (the previous behaviour).
        /// </summary>
        public static string KeyFor(TaskGroup? group)
        {
            if (group == null)
            {
                return string.Empty;
            }

            // 1) Application ID / AppUserModelID: the identity the project
            //    already groups windows by.
            if (!string.IsNullOrWhiteSpace(group.AppId))
            {
                return group.AppId.Trim();
            }

            // 2) Executable path.
            if (!string.IsNullOrWhiteSpace(group.ExePath))
            {
                return group.ExePath.Trim();
            }

            // 3) Launch shortcut: "shell item" pins exist only as a .lnk (they
            //    have no executable), so that is their key.
            if (!string.IsNullOrWhiteSpace(group.LaunchPath))
            {
                return group.LaunchPath.Trim();
            }

            return string.Empty;
        }

        /// <summary>Keys of a sequence of groups, in the given order.</summary>
        public static List<string> KeysFor(IEnumerable<TaskGroup> groups)
        {
            var keys = new List<string>();
            foreach (TaskGroup group in groups)
            {
                string key = KeyFor(group);
                if (!string.IsNullOrEmpty(key) &&
                    !keys.Contains(key, StringComparer.OrdinalIgnoreCase))
                {
                    keys.Add(key);
                }
            }
            return keys;
        }

        /// <summary>
        /// Applies the stored order to the discovery order.
        ///
        /// Rule: items whose key is in the stored list go to the position the
        /// user chose; all the others stay where the program would always put
        /// them, that is right BEFORE the first ordered item that follows them
        /// in discovery (or at the head, when they have none). This way an
        /// application that has just been opened, or a new pin, does not end
        /// up in the middle of a hand-made order and does not upset the rest.
        ///
        /// With an empty list (a user who never reordered anything) the result
        /// is EXACTLY the discovery order: no behaviour change for anyone who
        /// does not use the feature.
        /// </summary>
        public static List<TaskGroup> Apply(IEnumerable<TaskGroup> discoveryOrder,
                                            IReadOnlyList<string>? savedOrder)
        {
            var discovery = new List<TaskGroup>();
            var seen = new HashSet<TaskGroup>();
            foreach (TaskGroup group in discoveryOrder)
            {
                if (seen.Add(group))
                {
                    discovery.Add(group);
                }
            }

            if (savedOrder == null || savedOrder.Count == 0 || discovery.Count == 0)
            {
                return discovery;
            }

            // Position chosen by the user (first occurrence wins: the
            // normalized list has no duplicates, but the configuration may
            // have been edited by hand).
            var rank = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < savedOrder.Count; i++)
            {
                string key = savedOrder[i];
                if (!string.IsNullOrEmpty(key) && !rank.ContainsKey(key))
                {
                    rank[key] = i;
                }
            }

            var ranked = new List<TaskGroup>();
            var rankedSet = new HashSet<TaskGroup>();
            foreach (TaskGroup group in discovery)
            {
                string key = KeyFor(group);
                if (!string.IsNullOrEmpty(key) && rank.ContainsKey(key))
                {
                    ranked.Add(group);
                    rankedSet.Add(group);
                }
            }

            if (ranked.Count == 0)
            {
                return discovery;
            }

            // User order among the items that were placed. List.Sort is not
            // stable: sort by (position, discovery index) so the result is
            // always the same.
            var discoveryIndex = new Dictionary<TaskGroup, int>();
            for (int i = 0; i < discovery.Count; i++)
            {
                discoveryIndex[discovery[i]] = i;
            }
            ranked.Sort((a, b) =>
            {
                int cmp = rank[KeyFor(a)].CompareTo(rank[KeyFor(b)]);
                return cmp != 0 ? cmp : discoveryIndex[a].CompareTo(discoveryIndex[b]);
            });

            // Unplaced items, grouped by "last ordered item that precedes them
            // in discovery"; the ones with none go to the head.
            var head = new List<TaskGroup>();
            var after = new Dictionary<TaskGroup, List<TaskGroup>>();
            TaskGroup? anchor = null;
            foreach (TaskGroup group in discovery)
            {
                if (rankedSet.Contains(group))
                {
                    anchor = group;
                    continue;
                }

                if (anchor == null)
                {
                    head.Add(group);
                }
                else
                {
                    if (!after.TryGetValue(anchor, out List<TaskGroup>? list))
                    {
                        list = new List<TaskGroup>();
                        after[anchor] = list;
                    }
                    list.Add(group);
                }
            }

            var result = new List<TaskGroup>(discovery.Count);
            result.AddRange(head);
            foreach (TaskGroup group in ranked)
            {
                result.Add(group);
                if (after.TryGetValue(group, out List<TaskGroup>? following))
                {
                    result.AddRange(following);
                }
            }

            // Guard: any item that was not considered goes to the end, so a
            // group can never disappear from the bar because of a mistake
            // here.
            foreach (TaskGroup group in discovery)
            {
                if (!result.Contains(group))
                {
                    result.Add(group);
                }
            }
            return result;
        }
    }
}
