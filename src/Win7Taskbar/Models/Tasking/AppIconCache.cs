// Win7Taskbar - shared task model: icon materialization cache
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
using System.Collections.Concurrent;
using System.Windows.Media;

namespace Win7Taskbar.Models.Tasking
{
    /// <summary>
    /// Where icon resources become presentation images. The model only carries
    /// keys (<see cref="TaskEntry.IconKey"/>); this cache is the single point
    /// that materializes ImageSource instances, and the resolution worker fills
    /// it off the UI thread. Frozen images are shared freely across threads.
    /// </summary>
    internal sealed class AppIconCache
    {
        private readonly ConcurrentDictionary<string, ImageSource> _icons =
            new ConcurrentDictionary<string, ImageSource>(StringComparer.OrdinalIgnoreCase);

        /// <summary>Icon of a tracked window (see the entry's IconRevision).</summary>
        public static string WindowKey(ulong hwnd) => "win:" + hwnd.ToString("x");

        /// <summary>Icon extracted from an executable.</summary>
        public static string ExeKey(string path) => "exe:" + path;

        /// <summary>Identity icon of a pinned shortcut (no arrow overlay).</summary>
        public static string PinKey(string lnkPath) => "pin:" + lnkPath;

        public ImageSource? Get(string key)
        {
            if (string.IsNullOrEmpty(key))
            {
                return null;
            }

            return _icons.TryGetValue(key, out ImageSource? image) ? image : null;
        }

        public void Put(string key, ImageSource? image)
        {
            if (string.IsNullOrEmpty(key) || image == null)
            {
                return;
            }

            if (image.CanFreeze && !image.IsFrozen)
            {
                image.Freeze();
            }

            _icons[key] = image;
        }

        public void Invalidate(string key)
        {
            if (!string.IsNullOrEmpty(key))
            {
                _icons.TryRemove(key, out _);
            }
        }
    }
}
