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
using Microsoft.Win32;
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
                    /* v3.10: piccola cache con tetto: oltre 512 voci si
                     * svuota e riparte (le icone si ricreano in pochi ms,
                     * la RAM della lista ricerca no). */
                    if (Cache.Count > 512)
                    {
                        Cache.Clear();
                    }
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
                /* v3.10: prima la pipeline nativa della shell
                 * (IShellItemImageFactory): file, collegamenti, voci del
                 * Pannello di controllo e app UWP rendono la LORO icona al
                 * formato esatto, qualita' GDI+ senza catene di upscale.
                 * E' anche il motivo per cui i risultati senza icona ora
                 * la mostrano. */
                if (size > 16 && !string.IsNullOrEmpty(probe))
                {
                    ImageSource? factory = FromShellItemFactory(probe, size);
                    if (factory != null)
                    {
                        return factory;
                    }
                }
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
                    ImageSource? alt = FromShGetFileInfo(path, File.Exists(path) || Directory.Exists(path), size);
                    if (alt != null)
                    {
                        return alt;
                    }
                }

                /* v3.15 - ispirazione Open-Shell (l'icona si chiede SEMPRE
                 * alla shell, anche per i 16px dell'albero "Tutti i
                 * programmi"): la vecchia soglia "fabbrica solo > 16" lasciava
                 * senza icona i programmi il cui collegamento risolve solo
                 * tramite IShellItemImageFactory (app UWP, estensioni
                 * registrate, voci del Pannello di controllo puntate da
                 * .lnk di sistema). Qui e' l'ultima spiaggia: jumbo a 16 li'
                 * dove tutto il resto ha fallito, non come scelta primaria
                 * (le cartelle standard restano sul SHIL_SMALL nitido). */
                ImageSource? last = FromShellItemFactory(probe, size);
                if (last != null)
                {
                    return last;
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
                    return FromShellItemFactory(probe, size)
                        ?? FromPidl(probe, size)
                        ?? FromShGetFileInfo(probe, exists: true, size);
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

        /// <summary>
        /// Estrae un'icona da una DLL con la dimensione richiesta usando
        /// SHDefExtractIconW, API documentata della shell, e la converte con
        /// la stessa pipeline GDI+ delle altre icone del menu Start. E' utile
        /// per icone di sistema che non hanno un percorso ShellItem proprio.
        /// </summary>
        public static ImageSource? FromDllGdiPlus(string dll, int index, int size)
        {
            IntPtr large = IntPtr.Zero;
            IntPtr small = IntPtr.Zero;
            try
            {
                size = Math.Clamp(size, 4, 256);
                string path = Path.Combine(Environment.SystemDirectory, dll);
                uint packedSize = (uint)size | ((uint)size << 16);
                int hr = NativeMethods.SHDefExtractIconW(path, index, 0,
                    out large, out small, packedSize);
                if (hr < 0)
                {
                    return null;
                }

                IntPtr pick;
                if (large != IntPtr.Zero)
                {
                    pick = large;
                    large = IntPtr.Zero;
                }
                else
                {
                    pick = small;
                    small = IntPtr.Zero;
                }
                return pick == IntPtr.Zero
                    ? null
                    : FromHicon(pick, size, destroy: true);
            }
            catch (Exception)
            {
                return null;
            }
            finally
            {
                if (small != IntPtr.Zero)
                {
                    NativeMethods.DestroyIcon(small);
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

        /* v3.10: icona via IShellItemImageFactory nel core nativo (Pbgra32
         * premoltiplicato top-down). Null quando il core caricato non ha il
         * nuovo export o la shell non ha un'icona: i chiamanti ripiegano
         * sulla pipeline HICON classica. */
        private static ImageSource? FromShellItemFactory(string probe, int size)
        {
            if (size < 4 || size > 256 || string.IsNullOrEmpty(probe))
            {
                return null;
            }
            try
            {
                int needed = NativeMethods.W7T_ShellItemIconBitmap(probe, size, null, 0);
                if (needed <= 0)
                {
                    return null;
                }
                byte[] pixels = new byte[needed];
                if (NativeMethods.W7T_ShellItemIconBitmap(probe, size, pixels, pixels.Length) != 0)
                {
                    return null;
                }
                var bitmap = BitmapSource.Create(
                    size, size, 96, 96, PixelFormats.Pbgra32, null,
                    pixels, size * 4);
                bitmap.Freeze();
                return bitmap;
            }
            catch (EntryPointNotFoundException)
            {
                return null; /* core piu' vecchio dell'export: fallback */
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>
        /// v3.10: icona del browser predefinito, per la riga "Cerca su
        /// Internet" della ricerca del menu Start (come il browser vero):
        /// UserChoice http -> ProgId -> DefaultIcon / ApplicationIcon /
        /// comando di apertura. Trova sempre un'icona finche' esiste un
        /// handler registrato; cache statica, tutto in try/catch.
        /// </summary>
        public static ImageSource? FromDefaultBrowser(int size)
        {
            if (_browserIcon != null)
            {
                return _browserIcon;
            }
            try
            {
                string? progId = null;
                using (RegistryKey? key = Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Windows\Shell\Associations\UrlAssociations\http\UserChoice"))
                {
                    progId = key?.GetValue("ProgId") as string;
                }
                ImageSource? icon = null;
                if (!string.IsNullOrEmpty(progId))
                {
                    icon = BrowserIconFromProgId(progId!, size);
                }
                if (icon == null)
                {
                    icon = BrowserIconFromHttpVerb(size);
                }
                _browserIcon = icon ?? FromDll("imageres.dll", 220, size);
                return _browserIcon;
            }
            catch (Exception)
            {
                _browserIcon = FromDll("imageres.dll", 220, size);
                return _browserIcon;
            }
        }

        private static ImageSource? _browserIcon;

        /* v3.18: icona della riga "Cerca su Internet" disegnata con una
         * pipeline GDI+ (System.Drawing) DEDICATA ad alta qualita':
         * - risolve l'eseguibile del browser predefinito (UserChoice http)
         * - preleva la cella JUMBO (256 px) della image-list di shell
         * - ridisegna il canvas 32bppPArgb con interpolazione
         *   HighQualityBicubic + PixelOffsetMode/CompositingQuality HQ.
         * La voce resta identica ma morbida anche a 150/200% di DPI.
         * Se uno qualunque dei passaggi non ha immagini si ripiega sulla
         * pipeline classica FromDefaultBrowser. */
        public static ImageSource? FromDefaultBrowserGdiPlus(int size)
        {
            if (_browserIconGdiPlus != null)
            {
                return _browserIconGdiPlus;
            }
            try
            {
                string? exe = ResolveDefaultBrowserExe();
                if (!string.IsNullOrEmpty(exe))
                {
                    ImageSource? img = GdiPlusIconFromExe(exe!, size);
                    if (img != null)
                    {
                        _browserIconGdiPlus = img;
                        return _browserIconGdiPlus;
                    }
                }
            }
            catch (Exception)
            {
            }
            _browserIconGdiPlus = FromDefaultBrowser(size);
            return _browserIconGdiPlus;
        }

        private static ImageSource? _browserIconGdiPlus;

        private static string? ResolveDefaultBrowserExe()
        {
            try
            {
                using RegistryKey? key = Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Windows\Shell\Associations\UrlAssociations\http\UserChoice");
                string? progId = key?.GetValue("ProgId") as string;
                if (!string.IsNullOrEmpty(progId))
                {
                    using RegistryKey? defIcon = Registry.ClassesRoot.OpenSubKey(
                        progId + @"\DefaultIcon");
                    string? exe = ExeFromIconLocation(
                        defIcon?.GetValue(null) as string);
                    if (exe != null && File.Exists(exe))
                    {
                        return exe;
                    }
                    using RegistryKey? app = Registry.ClassesRoot.OpenSubKey(
                        progId + @"\Application");
                    exe = ExeFromIconLocation(
                        app?.GetValue("ApplicationIcon") as string);
                    if (exe != null && File.Exists(exe))
                    {
                        return exe;
                    }
                }
            }
            catch (Exception)
            {
            }
            /* Una lettura sola in piu': shell\open\command dell'http. */
            try
            {
                using RegistryKey? cmd = Registry.ClassesRoot.OpenSubKey(
                    @"http\shell\open\command");
                string? line = cmd?.GetValue(null) as string;
                if (string.IsNullOrWhiteSpace(line))
                {
                    return null;
                }
                line = Environment.ExpandEnvironmentVariables(line!.Trim());
                string exe;
                if (line.StartsWith("\"", StringComparison.Ordinal))
                {
                    int end = line.IndexOf('"', 1);
                    exe = end > 1 ? line.Substring(1, end - 1) : line;
                }
                else
                {
                    int space = line.IndexOf(' ');
                    exe = space > 0 ? line.Substring(0, space) : line;
                }
                return File.Exists(exe) ? exe : null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        /* "C:\\%ProgramFiles%\\msedge.exe,0" -> percorso espanso senza
         * indice. Ritorna null quando non e' una icon-locazione classica. */
        private static string? ExeFromIconLocation(string? location)
        {
            if (string.IsNullOrWhiteSpace(location))
            {
                return null;
            }
            try
            {
                string expanded = Environment.ExpandEnvironmentVariables(
                    location.Trim().Trim('"'));
                int comma = expanded.LastIndexOf(',');
                if (comma > 0)
                {
                    expanded = expanded.Substring(0, comma);
                }
                return expanded.Length > 0 ? expanded : null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        /* Disegno GDI+ puro: jumbo HICON -> Bitmap PArgb -> ridisegno
         * bicubico HQ sul canvas di destinazione -> WPF BitmapSource. */
        private static ImageSource? GdiPlusIconFromExe(string exe, int size)
        {
            IntPtr list = IntPtr.Zero;
            try
            {
                var info = new NativeMethods.SHFILEINFOW();
                if (NativeMethods.SHGetFileInfoW(exe, 0, ref info,
                        (uint)Marshal.SizeOf<NativeMethods.SHFILEINFOW>(),
                        ShgfiSysIconIndex) == IntPtr.Zero)
                {
                    return null;
                }
                int index = info.iIcon;
                Guid iid = IidIImageList;
                ImageSource? img = GdiPlusFromImageListCell(
                    ShilJumbo, index, size, iid, ref list)
                    ?? GdiPlusFromImageListCell(
                        ShilExtraLarge, index, size, iid, ref list);
                return img;
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? GdiPlusFromImageListCell(
            int which, int index, int size, Guid iid, ref IntPtr list)
        {
            try
            {
                if (SHGetImageList(which, ref iid, out list) != 0 ||
                    list == IntPtr.Zero)
                {
                    return null;
                }
                IntPtr hicon = ImageList_GetIcon(list, index, IldTransparent);
                if (hicon == IntPtr.Zero)
                {
                    return null;
                }
                ImageSource? img = GdiPlusRedraw(hicon, size);
                NativeMethods.DestroyIcon(hicon);
                return img;
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
                    list = IntPtr.Zero;
                }
            }
        }

        private static ImageSource? GdiPlusRedraw(IntPtr hicon, int size)
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



        private static ImageSource? BrowserIconFromProgId(string progId, int size)
        {
            try
            {
                /* DefaultIcon e' il posto documentato dove i browser
                 * registrano la propria icona ("C:\...\msedge.exe,0"). */
                using RegistryKey? defIcon = Registry.ClassesRoot.OpenSubKey(
                    progId + @"\DefaultIcon");
                ImageSource? icon = IconFromIconLocation(
                    defIcon?.GetValue(null) as string, size);
                if (icon != null)
                {
                    return icon;
                }
                using RegistryKey? app = Registry.ClassesRoot.OpenSubKey(
                    progId + @"\Application");
                icon = IconFromIconLocation(
                    app?.GetValue("ApplicationIcon") as string, size);
                if (icon != null)
                {
                    return icon;
                }
                using RegistryKey? cmd = Registry.ClassesRoot.OpenSubKey(
                    progId + @"\shell\open\command");
                return IconFromCommandLine(cmd?.GetValue(null) as string, size);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? BrowserIconFromHttpVerb(int size)
        {
            try
            {
                using RegistryKey? cmd = Registry.ClassesRoot.OpenSubKey(
                    @"http\shell\open\command");
                return IconFromCommandLine(cmd?.GetValue(null) as string, size);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? IconFromIconLocation(string? location, int size)
        {
            if (string.IsNullOrWhiteSpace(location))
            {
                return null;
            }
            try
            {
                string expanded = Environment.ExpandEnvironmentVariables(
                    location.Trim().Trim('"'));
                int comma = expanded.LastIndexOf(',');
                if (comma > 0)
                {
                    expanded = expanded.Substring(0, comma);
                }
                if (expanded.Length == 0)
                {
                    return null;
                }
                return FromPath(expanded, expanded, size);
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static ImageSource? IconFromCommandLine(string? command, int size)
        {
            if (string.IsNullOrWhiteSpace(command))
            {
                return null;
            }
            try
            {
                string line = Environment.ExpandEnvironmentVariables(command.Trim());
                string exe;
                if (line.StartsWith("\"", StringComparison.Ordinal))
                {
                    int end = line.IndexOf('"', 1);
                    exe = end > 1 ? line.Substring(1, end - 1) : line;
                }
                else
                {
                    int space = line.IndexOf(' ');
                    exe = space > 0 ? line.Substring(0, space) : line;
                }
                if (exe.Length == 0 || !File.Exists(exe))
                {
                    return null;
                }
                return FromPath(exe, exe, size);
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

        // --------- v3.17: padded-icon calibration of the search rows ---------

        /// <summary>Fraction of opaque pixels of a decoration icon
        /// (alpha > 10). Modern icons with a big transparent canvas
        /// (snipping tool family) sit well below 0.34; classic Win7
        /// icons fill almost the whole frame. Everything is guarded:
        /// any failure reports full coverage (no adjustment).</summary>
        public static double OpaqueCoverage(ImageSource? icon)
        {
            const double full = 1.0;
            try
            {
                if (icon is not BitmapSource bmp ||
                    bmp.PixelWidth <= 0 || bmp.PixelHeight <= 0)
                {
                    return full;
                }
                int w = bmp.PixelWidth;
                int h = bmp.PixelHeight;
                int stride = w * 4;
                byte[] pixels = new byte[stride * h];
                BitmapSource source = bmp;
                if (bmp.Format != PixelFormats.Bgra32 &&
                    bmp.Format != PixelFormats.Pbgra32)
                {
                    source = new FormatConvertedBitmap(bmp,
                        PixelFormats.Bgra32, null, 0);
                }
                source.CopyPixels(pixels, stride, 0);
                int opaque = 0;
                for (int i = 3; i < pixels.Length; i += 4)
                {
                    if (pixels[i] > 10)
                    {
                        opaque++;
                    }
                }
                return (double)opaque / (w * h);
            }
            catch (Exception)
            {
                return full;   // no adjustment on unknown content
            }
        }

        /// <summary>Search-row icon metrics (v3.17): every row already
        /// inherits the -4% size (19.2) and the left-shifted margin
        /// (15.2). Icons whose opaque coverage is small (padded modern
        /// glyphs) render slightly smaller and further left - the
        /// user's calibration for Strumento di cattura & co. Everything
        /// wrapped: a failure leaves the defaults untouched.</summary>
        public static void ApplySearchRowMetrics(StartMenuItem item)
        {
            try
            {
                if (item?.Icon == null)
                {
                    return;
                }
                if (OpaqueCoverage(item.Icon) < 0.34)
                {
                    item.SearchIconSize = 18.4;
                    item.SearchIconLeft = 10.6;
                }
            }
            catch (Exception)
            {
                /* defaults stand */
            }
        }
    }
}
