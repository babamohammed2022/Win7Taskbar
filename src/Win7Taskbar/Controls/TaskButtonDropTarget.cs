// Win7Taskbar - drag-and-drop capability check for taskbar buttons
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// v1.7.3: while a file hovers over an app button, the cursor must show
// the "forbidden" sign when the target executable cannot open that file
// type, exactly like the real taskbar. The capability comes from the
// public registry location applications use to declare it:
//   HKCR\Applications\<exe>\SupportedTypes   (value names = extensions)
// The check is deliberately permissive: when an executable declares
// nothing (most do not) the drop stays accepted - refusing a drop for
// missing metadata would be worse than launching a file that may fail.

using System;
using System.Collections.Generic;
using System.IO;
using Microsoft.Win32;

namespace Win7Taskbar.Controls
{
    internal static class TaskButtonDropTarget
    {
        // Cache per executable name: DragOver fires dozens of times per
        // second and the registry must not be hit on every pixel.
        private static readonly Dictionary<string, HashSet<string>?> s_supportedExtCache =
            new(StringComparer.OrdinalIgnoreCase);

        /// <summary>
        /// Extensions (lowercase, with the dot) the executable declares via
        /// the SupportedTypes registry key. Null = unknown (nothing
        /// declared or the lookup failed): treat the file as acceptable,
        /// because blocking on missing data would reject valid drops.
        /// </summary>
        internal static HashSet<string>? GetSupportedExtensions(string? exePath)
        {
            if (string.IsNullOrEmpty(exePath))
            {
                return null; // no executable known: cannot judge, do not block
            }

            string exeName = Path.GetFileName(exePath);
            if (s_supportedExtCache.TryGetValue(exeName, out var cached))
            {
                return cached;
            }

            HashSet<string>? result = null;
            try
            {
                using RegistryKey? key = Registry.ClassesRoot.OpenSubKey(
                    $@"Applications\{exeName}\SupportedTypes");
                if (key != null)
                {
                    result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                    foreach (string valueName in key.GetValueNames())
                    {
                        // SupportedTypes uses the VALUE NAME as the extension
                        // (e.g. ".txt"); the data is usually empty.
                        if (!string.IsNullOrEmpty(valueName) && valueName.StartsWith("."))
                        {
                            result.Add(valueName);
                        }
                    }
                }
            }
            catch (Exception)
            {
                // Registry access failed (permissions, missing key, ...):
                // treat as "unknown", not as "refuse".
                result = null;
            }

            // Cache the "not found" (null) too, otherwise every DragOver
            // would hit the registry again for apps without declarations.
            s_supportedExtCache[exeName] = result;
            return result;
        }

        /// <summary>
        /// True when the file is (probably) openable by this executable.
        /// Permissive by design: in doubt, yes.
        /// </summary>
        internal static bool CanLikelyOpen(string? exePath, string filePath)
        {
            try
            {
                var supported = GetSupportedExtensions(exePath);
                if (supported == null || supported.Count == 0)
                {
                    return true; // nothing declared: do not block
                }

                string ext = Path.GetExtension(filePath);
                return string.IsNullOrEmpty(ext) || supported.Contains(ext);
            }
            catch (Exception)
            {
                return true; // any error in the check stays permissive
            }
        }

        /// <summary>
        /// True when every file in the drop list is probably openable by
        /// the executable (an empty list is vacuously fine).
        /// </summary>
        internal static bool AllLikelyOpen(string? exePath, string[]? files)
        {
            if (files == null || files.Length == 0)
            {
                return true;
            }
            foreach (string file in files)
            {
                if (!CanLikelyOpen(exePath, file))
                {
                    return false;
                }
            }
            return true;
        }
    }
}
