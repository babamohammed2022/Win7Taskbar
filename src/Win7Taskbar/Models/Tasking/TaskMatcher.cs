// Win7Taskbar - shared task model: matching and grouping rules
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
using System.IO;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// "Find the existing group, otherwise create and assign" rules. The pin
    /// affinity criteria were moved here from the view model verbatim (same
    /// criteria, same order): they are model identity decisions, not layout.
    /// </summary>
    internal sealed class TaskMatcher
    {
        private const StringComparison Oic = StringComparison.OrdinalIgnoreCase;

        /* Windows associates a window opened right after the user launched a
         * pin with the button that was pressed, even when the shortcut is a
         * shell item without a path: for a few seconds new explorer.exe
         * windows belong to that pin instead of becoming a new button. */
        private (string PinAppId, DateTime UntilUtc)? _pendingLaunch;

        private static readonly TimeSpan LaunchAffinity = TimeSpan.FromSeconds(8);

        // ----------------------------------------------------------------
        //  Window identity vs group identity
        // ----------------------------------------------------------------

        public MatchConfidence ConfidenceOf(in TaskIdentity id, AppGroup? group)
        {
            if (group == null)
            {
                return MatchConfidence.None;
            }

            if (!string.IsNullOrEmpty(id.AppId) &&
                (string.Equals(id.AppId, group.Key, Oic) ||
                 (!string.IsNullOrEmpty(group.AppId) &&
                  string.Equals(id.AppId, group.AppId, Oic))))
            {
                return MatchConfidence.ByAppId;
            }

            if (!string.IsNullOrEmpty(id.ExePath) &&
                !string.IsNullOrEmpty(group.ExePath) &&
                string.Equals(id.ExePath, group.ExePath, Oic))
            {
                return MatchConfidence.ByExePath;
            }

            if (!string.IsNullOrEmpty(id.ExePath) &&
                !string.IsNullOrEmpty(group.ExePath) &&
                SameFileName(id.ExePath, group.ExePath))
            {
                return MatchConfidence.ByExeName;
            }

            return MatchConfidence.None;
        }

        /// <summary>
        /// First group whose confidence reaches <paramref name="min"/>:
        /// collection order matters when more than one group qualifies.
        /// </summary>
        public MatchResult MatchGroup(IReadOnlyList<AppGroup> groups, in TaskIdentity id,
                                      MatchConfidence min)
        {
            if (groups == null)
            {
                return default;
            }

            for (int i = 0; i < groups.Count; i++)
            {
                MatchConfidence confidence = ConfidenceOf(id, groups[i]);
                if (confidence != MatchConfidence.None && confidence >= min)
                {
                    return new MatchResult(groups[i], null, confidence);
                }
            }

            return default;
        }

        // ----------------------------------------------------------------
        //  Pin affinity (moved from TaskbarViewModel; criteria preserved)
        // ----------------------------------------------------------------

        /// <summary>
        /// A pin and a window group are the same application when they agree on
        /// the AppUserModelID OR on the executable (the core identifies windows
        /// by path when the app declares no AppId, while the pinned shortcut
        /// declares the AppUserModelID: without this double criterion the same
        /// app would show up several times).
        /// </summary>
        public static bool PinMatches(PinInfo pin, AppGroup group)
        {
            if (group == null)
            {
                return false;
            }

            // The group key is the button identity (a view policy can make it a
            // synthetic per-window key, which never matches a pin).
            return PinMatches(pin, group.Key, group.ExePath, group.IsPinned);
        }

        /// <summary>Identity-only form: works for model groups and buttons.</summary>
        public static bool PinMatches(PinInfo pin, string groupKey, string? groupExePath,
                                      bool groupIsPinned)
        {
            if (pin == null)
            {
                return false;
            }

            if (string.Equals(pin.AppId, groupKey, Oic))
            {
                return true;
            }

            if (!string.IsNullOrEmpty(pin.TargetPath) &&
                !string.IsNullOrEmpty(groupExePath) &&
                string.Equals(pin.TargetPath, groupExePath, Oic))
            {
                return true;
            }

            // explorer.exe windows with a pin holding Explorer's canonical
            // AppUserModelID. The "shell item" criterion only applies to groups
            // not yet claimed by another pin.
            if (!groupIsPinned && IsExplorerPin(pin, groupExePath ?? string.Empty))
            {
                return true;
            }

            // Last criterion: identical file name without extension.
            try
            {
                string pn = Path.GetFileNameWithoutExtension(pin.TargetPath) ?? string.Empty;
                string gn = Path.GetFileNameWithoutExtension(groupExePath) ?? string.Empty;
                return !string.IsNullOrEmpty(pn) &&
                       string.Equals(pn, gn, Oic);
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// "Same application" between a pin target and a window executable:
        /// same normalized path OR same file name without extension. This is
        /// the fallback criterion the Windows taskbar uses when the
        /// AppUserModelID differs between shortcut and process.
        /// </summary>
        public static bool SameApp(string pinTarget, string windowExe)
        {
            if (string.IsNullOrEmpty(pinTarget) || string.IsNullOrEmpty(windowExe))
            {
                return false;
            }

            if (string.Equals(pinTarget, windowExe, Oic))
            {
                return true;
            }

            return SameFileName(pinTarget, windowExe);
        }

        /// <summary>
        /// explorer.exe windows belong to the Explorer element when the pin
        /// declares the canonical AppUserModelID ("Microsoft.Windows.Explorer"):
        /// this is how Explorer keeps its own windows grouped (including
        /// "This PC"/folder windows).
        /// </summary>
        public static bool IsExplorerPin(PinInfo pin, string windowExe)
        {
            if (string.IsNullOrEmpty(windowExe) ||
                !windowExe.EndsWith("\\explorer.exe", Oic))
            {
                return false;
            }

            return string.Equals(pin.AppId, "Microsoft.Windows.Explorer", Oic) ||
                   string.Equals(pin.TargetPath, "Microsoft.Windows.Explorer", Oic) ||
                   // "Shell item" pins (IDList, no exe: the Explorer pin Windows
                   // itself creates) still open inside explorer.exe, so its
                   // windows belong to them. Documented corner case: with
                   // several shell-item pins the first enumerated receives the
                   // windows, as in Windows.
                   string.IsNullOrEmpty(pin.TargetPath);
        }

        public static bool SameFileName(string pathA, string pathB)
        {
            try
            {
                string an = Path.GetFileNameWithoutExtension(pathA) ?? string.Empty;
                string bn = Path.GetFileNameWithoutExtension(pathB) ?? string.Empty;
                return !string.IsNullOrEmpty(an) &&
                       string.Equals(an, bn, Oic);
            }
            catch
            {
                return false;
            }
        }

        // ----------------------------------------------------------------
        //  Launch affinity (was TaskbarViewModel._pendingLaunch)
        // ----------------------------------------------------------------

        /// <summary>The bar just launched this pin (by its identity).</summary>
        public void NoteRecentLaunch(string pinAppId)
        {
            if (string.IsNullOrEmpty(pinAppId))
            {
                return;
            }

            _pendingLaunch = (pinAppId, DateTime.UtcNow.Add(LaunchAffinity));
        }

        /// <summary>
        /// Consumes the launch-affinity window for an explorer.exe window with
        /// no other match: returns true when the window fell inside a recent
        /// pin launch (and clears the window either way). The claimed pin can
        /// still be null when no pin has that identity anymore.
        /// </summary>
        public bool ConsumeLaunchAffinity(string windowExe, IReadOnlyList<PinInfo> pins,
                                          out PinInfo? claimedPin)
        {
            claimedPin = null;
            if (_pendingLaunch is not { } pending ||
                DateTime.UtcNow >= pending.UntilUtc ||
                string.IsNullOrEmpty(windowExe) ||
                !windowExe.EndsWith("\\explorer.exe", Oic))
            {
                return false;
            }

            if (pins != null)
            {
                foreach (PinInfo pin in pins)
                {
                    if (string.Equals(pin.AppId, pending.PinAppId, Oic))
                    {
                        claimedPin = pin;
                        break;
                    }
                }
            }

            _pendingLaunch = null;
            return true;
        }
    }
}
