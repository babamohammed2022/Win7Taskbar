// Win7Taskbar - high-quality Start Menu icons via GDI+
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. No Microsoft bitmaps. No Open-Shell source copied.
//
// For sizes above 16px, extract the largest available shell icon, then
// downscale with GDI+ HighQualityBicubic. At 16px (All Programs tree)
// use SHIL_SMALL / SHGFI_SMALLICON so folders are native 16x16, not a
// jumbo downscale. ExtractIconEx / SHGetFileInfo alone produced the
// low-res hover fade the user reported.

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Win7Taskbar.Interop;
using GdiPixelFormat = System.Drawing.Imaging.PixelFormat;

namespace Win7Taskbar.StartMenu
{
    internal static class StartMenuIcons
    {
        private const uint ShgfiSysIconIndex = 0x00004000;
        private const int ShilLarge = 0;
        private const int ShilSmall = 1;
        private const int ShilExtraLarge = 2;
        private const int ShilJumbo = 4;
        private const uint IldTransparent = 0x00000001;
        private static readonly Guid IidIImageList =
            new("46EB5926-582E-4017-9FDF-E8998DAA0950");
        private static readonly object CacheLock = new();
        private static readonly Dictionary<string, ImageSource?> Cache =
            new(StringComparer.OrdinalIgnoreCase);

        public static ImageSource? FromPath(string? path, string? target, int size)
        {
            try
            {
                size = size <= 0 ? 32 : size;
                string key = size.ToString() + "\n" + (path ?? string.Empty)
                    + "\n" + (target ?? string.Empty);
                lock (CacheLock)
                {
                    if (Cache.TryGetValue(key, out ImageSource? hit))
                    {
                        return hit;
                    }
                }
                ImageSource? made = ExtractFromPath(path, target, size);
                lock (CacheLock)
                {
                    Cache[key] = made;
                }
                return made;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? ExtractFromPath(string? path, string? target, int size)
        {
            try
            {
                string probe = !string.IsNullOrEmpty(target) ? target : (path ?? string.Empty);
                ImageSource? src = FromShellImageList(probe, size);
                if (src != null)
                {
                    return src;
                }

                IntPtr native = NativeMethods.W7T_GetLinkIcon(
                    string.IsNullOrEmpty(path) ? null : path,
                    string.IsNullOrEmpty(target) ? null : target,
                    1);
                src = FromHicon(native, size, destroy: true);
                if (src != null)
                {
                    return src;
                }

                if (!string.IsNullOrEmpty(probe))
                {
                    src = FromShGetFileInfo(probe, File.Exists(probe) || Directory.Exists(probe), size);
                    if (src != null)
                    {
                        return src;
                    }
                }

                if (!string.IsNullOrEmpty(path) &&
                    !string.Equals(path, probe, StringComparison.OrdinalIgnoreCase))
                {
                    return FromShGetFileInfo(path, File.Exists(path) || Directory.Exists(path), size);
                }
                return null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        public static ImageSource? FromParsingName(string? probe, int size)
        {
            try
            {
                if (string.IsNullOrEmpty(probe))
                {
                    return null;
                }
                if (probe.StartsWith("::{", StringComparison.Ordinal) ||
                    probe.StartsWith("shell:", StringComparison.OrdinalIgnoreCase))
                {
                    return FromPidl(probe, size) ?? FromShGetFileInfo(probe, exists: true, size);
                }
                string expanded = Environment.ExpandEnvironmentVariables(probe);
                return FromPath(expanded, expanded, size);
            }
            catch (Exception)
            {
                return null;
            }
        }

        public static ImageSource? FromDll(string dll, int index, int size)
        {
            IntPtr large = IntPtr.Zero;
            IntPtr small = IntPtr.Zero;
            try
            {
                string path = Path.Combine(Environment.SystemDirectory, dll);
                uint n = NativeMethods.ExtractIconEx(path, index,
                    out large, out small, 1);
                if (n == 0)
                {
                    return null;
                }
                IntPtr pick = (size <= 16 && small != IntPtr.Zero) ? small : large;
                if (pick == IntPtr.Zero)
                {
                    return null;
                }
                if (large != IntPtr.Zero && large != pick)
                {
                    NativeMethods.DestroyIcon(large);
                    large = IntPtr.Zero;
                }
                if (small != IntPtr.Zero && small != pick && small != large)
                {
                    NativeMethods.DestroyIcon(small);
                    small = IntPtr.Zero;
                }
                large = IntPtr.Zero;
                small = IntPtr.Zero;
                return FromHicon(pick, size, destroy: true);
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (small != IntPtr.Zero && small != large)
                {
                    NativeMethods.DestroyIcon(small);
                    small = IntPtr.Zero;
                }
                if (large != IntPtr.Zero)
                {
                    NativeMethods.DestroyIcon(large);
                }
            }
        }

        public static ImageSource? FromHicon(IntPtr hicon, int size, bool destroy)
        {
            if (hicon == IntPtr.Zero)
            {
                return null;
            }
            try
            {
                ImageSource? scaled = ScaleHiconGdiPlus(hicon, size);
                if (scaled != null)
                {
                    return scaled;
                }
                ImageSource src = Imaging.CreateBitmapSourceFromHIcon(
                    hicon, Int32Rect.Empty,
                    BitmapSizeOptions.FromWidthAndHeight(size, size));
                src.Freeze();
                return src;
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (destroy)
                {
                    NativeMethods.DestroyIcon(hicon);
                }
            }
        }

        private static ImageSource? FromPidl(string parsingName, int size)
        {
            IntPtr pidl = IntPtr.Zero;
            try
            {
                int hr = NativeMethods.SHParseDisplayName(parsingName, IntPtr.Zero,
                    out pidl, 0, IntPtr.Zero);
                if (hr != 0 || pidl == IntPtr.Zero)
                {
                    return null;
                }
                var info = new NativeMethods.SHFILEINFOW();
                uint flags = NativeMethods.SHGFI_PIDL | ShgfiSysIconIndex;
                IntPtr result = NativeMethods.SHGetFileInfoPidl(
                    pidl, 0, ref info,
                    (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                    flags);
                if (result != IntPtr.Zero && info.iIcon >= 0)
                {
                    ImageSource? fromList = FromImageListIndex(info.iIcon, size);
                    if (fromList != null)
                    {
                        return fromList;
                    }
                }
                flags = NativeMethods.SHGFI_PIDL | NativeMethods.SHGFI_ICON |
                        (size <= 16
                            ? NativeMethods.SHGFI_SMALLICON
                            : NativeMethods.SHGFI_LARGEICON);
                info = new NativeMethods.SHFILEINFOW();
                result = NativeMethods.SHGetFileInfoPidl(
                    pidl, 0, ref info,
                    (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                    flags);
                if (result == IntPtr.Zero || info.hIcon == IntPtr.Zero)
                {
                    return null;
                }
                return FromHicon(info.hIcon, size, destroy: true);
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (pidl != IntPtr.Zero)
                {
                    NativeMethods.ILFree(pidl);
                }
            }
        }

        private static ImageSource? FromShellImageList(string probe, int size)
        {
            if (string.IsNullOrEmpty(probe))
            {
                return null;
            }
            try
            {
                var info = new NativeMethods.SHFILEINFOW();
                uint flags = ShgfiSysIconIndex;
                uint attr = 0;
                if (!File.Exists(probe) && !Directory.Exists(probe))
                {
                    flags |= NativeMethods.SHGFI_USEFILEATTRIBUTES;
                    attr = NativeMethods.FILE_ATTRIBUTE_NORMAL;
                }
                IntPtr result = NativeMethods.SHGetFileInfoW(
                    probe, attr, ref info,
                    (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                    flags);
                if (result == IntPtr.Zero)
                {
                    return null;
                }
                return FromImageListIndex(info.iIcon, size);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? FromShGetFileInfo(string probe, bool exists, int size)
        {
            try
            {
                var info = new NativeMethods.SHFILEINFOW();
                uint flags = NativeMethods.SHGFI_ICON |
                    (size <= 16
                        ? NativeMethods.SHGFI_SMALLICON
                        : NativeMethods.SHGFI_LARGEICON);
                uint attr = 0;
                if (!exists)
                {
                    flags |= NativeMethods.SHGFI_USEFILEATTRIBUTES;
                    attr = NativeMethods.FILE_ATTRIBUTE_NORMAL;
                    try
                    {
                        if (!string.IsNullOrEmpty(probe) &&
                            (probe.EndsWith("\\", StringComparison.Ordinal) ||
                             Directory.Exists(probe)))
                        {
                            attr = NativeMethods.FILE_ATTRIBUTE_DIRECTORY;
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
                IntPtr result = NativeMethods.SHGetFileInfoW(
                    probe, attr, ref info,
                    (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                    flags);
                if (result == IntPtr.Zero || info.hIcon == IntPtr.Zero)
                {
                    return null;
                }
                return FromHicon(info.hIcon, size, destroy: true);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? FromImageListIndex(int index, int size)
        {
            if (index < 0)
            {
                return null;
            }
            if (size <= 16)
            {
                /* Open-Shell Programs tree uses SMALL_ICON (16px at 96 DPI).
                 * Jumbo/XL downscale made All Programs folders look soft. */
                return FromImageList(ShilSmall, index, size)
                    ?? FromImageList(ShilLarge, index, size);
            }
            ImageSource? jumbo = FromImageList(ShilJumbo, index, size);
            if (jumbo != null)
            {
                return jumbo;
            }
            return FromImageList(ShilExtraLarge, index, size);
        }

        private static ImageSource? FromImageList(int which, int index, int size)
        {
            IntPtr list = IntPtr.Zero;
            try
            {
                Guid iid = IidIImageList;
                int hr = SHGetImageList(which, ref iid, out list);
                if (hr != 0 || list == IntPtr.Zero)
                {
                    return null;
                }
                IntPtr hicon = ImageList_GetIcon(list, index, IldTransparent);
                if (hicon == IntPtr.Zero)
                {
                    return null;
                }
                return FromHicon(hicon, size, destroy: true);
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (list != IntPtr.Zero)
                {
                    Marshal.Release(list);
                }
            }
        }

        private static ImageSource? ScaleHiconGdiPlus(IntPtr hicon, int size)
        {
            try
            {
                using Icon icon = Icon.FromHandle(hicon);
                using Bitmap src = icon.ToBitmap();
                if (src.Width == size && src.Height == size)
                {
                    IntPtr native = src.GetHbitmap(
                        System.Drawing.Color.FromArgb(0, 0, 0, 0));
                    try
                    {
                        BitmapSource bmp = Imaging.CreateBitmapSourceFromHBitmap(
                            native, IntPtr.Zero, Int32Rect.Empty,
                            BitmapSizeOptions.FromEmptyOptions());
                        if (bmp.CanFreeze)
                        {
                            bmp.Freeze();
                        }
                        return bmp;
                    }
                    finally
                    {
                        NativeMethods.DeleteObject(native);
                    }
                }
                using Bitmap dest = new(size, size, GdiPixelFormat.Format32bppPArgb);
                using (Graphics g = Graphics.FromImage(dest))
                {
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.SmoothingMode = SmoothingMode.HighQuality;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.CompositingQuality = CompositingQuality.HighQuality;
                    g.Clear(System.Drawing.Color.Transparent);
                    g.DrawImage(src, 0, 0, size, size);
                }
                IntPtr hbitmap = dest.GetHbitmap(
                    System.Drawing.Color.FromArgb(0, 0, 0, 0));
                try
                {
                    BitmapSource bmp = Imaging.CreateBitmapSourceFromHBitmap(
                        hbitmap, IntPtr.Zero, Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions());
                    if (bmp.CanFreeze)
                    {
                        bmp.Freeze();
                    }
                    return bmp;
                }
                finally
                {
                    NativeMethods.DeleteObject(hbitmap);
                }
            }
            catch (Exception)
            {
                return null;
            }
        }

        [DllImport("shell32.dll")]
        private static extern int SHGetImageList(int iImageList, ref Guid riid, out IntPtr ppv);

        [DllImport("comctl32.dll")]
        private static extern IntPtr ImageList_GetIcon(IntPtr himl, int i, uint flags);
    }
}
