// Win7Taskbar - high-quality Start Menu icons via GDI+
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. No Microsoft bitmaps. No Open-Shell source copied.
//
// Extract the largest available shell icon, then downscale with GDI+
// HighQualityBicubic. ExtractIconEx / SHGetFileInfo alone produced the
// low-res hover fade the user reported.

using System;
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
        private const int ShilJumbo = 4;
        private const int ShilExtraLarge = 2;
        private const uint IldTransparent = 0x00000001;
        private static readonly Guid IidIImageList =
            new("46EB5926-582E-4017-9FDF-E8998DAA0950");

        public static ImageSource? FromPath(string? path, string? target, int size)
        {
            size = size <= 0 ? 32 : size;
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
                src = FromShGetFileInfo(probe, File.Exists(probe), size);
                if (src != null)
                {
                    return src;
                }
            }

            if (!string.IsNullOrEmpty(path) &&
                !string.Equals(path, probe, StringComparison.OrdinalIgnoreCase))
            {
                return FromShGetFileInfo(path, File.Exists(path), size);
            }
            return null;
        }

        public static ImageSource? FromParsingName(string? probe, int size)
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

        public static ImageSource? FromDll(string dll, int index, int size)
        {
            try
            {
                string path = Path.Combine(Environment.SystemDirectory, dll);
                uint n = NativeMethods.ExtractIconEx(path, index,
                    out IntPtr large, out IntPtr small, 1);
                if (small != IntPtr.Zero && small != large)
                {
                    NativeMethods.DestroyIcon(small);
                }
                if (n == 0 || large == IntPtr.Zero)
                {
                    return null;
                }
                return FromHicon(large, size, destroy: true);
            }
            catch (Exception)
            {
                return null;
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
                        NativeMethods.SHGFI_LARGEICON;
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
                uint flags = NativeMethods.SHGFI_ICON | NativeMethods.SHGFI_LARGEICON;
                uint attr = 0;
                if (!exists)
                {
                    flags |= NativeMethods.SHGFI_USEFILEATTRIBUTES;
                    attr = NativeMethods.FILE_ATTRIBUTE_NORMAL;
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
