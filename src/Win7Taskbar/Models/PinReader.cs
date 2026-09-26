// Win7Taskbar - icone delle applicazioni pinnate
// Copyright (c) 2026 Win7Taskbar contributors
// GPL v3 or later.
//
// v2.26: l'icona si estrae con il resolver nativo UNICO del progetto
// (W7T_GetLinkIcon -> Common::ResolveAppIcon): GetIconLocation/ExtractIconEx
// dal .lnk e poi icona dell'eseguibile target. MAI SHGetFileInfo sul .lnk:
// la shell ci compone sopra la freccia dei collegamenti, che sulla Superbar
// non deve comparire (stesso criterio di RetroBar/Open-Shell).

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media.Imaging;

namespace Win7Taskbar.Models
{
    /// <summary>
    /// Un'applicazione pinnata gia' normalizzata dal core nativo:
    /// identita' (AUMID o exe normalizzato), .lnk, target, icona.
    /// </summary>
    internal sealed class PinInfo
    {
        public string AppId { get; set; } = string.Empty;
        public string LnkPath { get; set; } = string.Empty;
        public string TargetPath { get; set; } = string.Empty;
        public BitmapSource? Icon { get; set; }
    }

    internal static class PinReader
    {
        /// <summary>
        /// Percorso della cartella dei pin reali della shell.
        /// Conservato per riferimento/debug: la lettura la fa il nativo.
        /// </summary>
        public static string PinnedFolder =>
            Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Microsoft", "Internet Explorer", "Quick Launch",
                "User Pinned", "TaskBar");

        /// <summary>
        /// Icona grande dell'applicazione pinnata (presentazione), senza
        /// overlay di collegamento. Risolta dal core nativo; se non trova
        /// nulla restituisce null e il pulsante usa il fallback del tema.
        ///
        /// Se il .lnk generato nella cartella dei pin punta direttamente
        /// all'EXE e quindi non contiene l'icona personalizzata dell'utente,
        /// viene cercata una scorciatoia dell'utente che punti allo stesso
        /// target e porti un'icona personalizzata esplicita (Desktop, menu
        /// Start, Quick Launch: vedi CustomIconIndex). Questo permette, ad
        /// esempio, di usare l'icona personalizzata di "Discord.lnk" invece
        /// di quella incorporata nell'eseguibile. Il pin mantiene comunque la
        /// precedenza quando dichiara gia' una propria icona personalizzata.
        /// </summary>
        internal static BitmapSource? ReadIcon(string lnk, string target)
        {
            try
            {
                // First preserve an explicit custom icon already stored in the
                // real taskbar pin (v1.21.8: including the ones whose path is
                // quoted or relative, resolved by the native core). Only when
                // it is absent/default do we look for a matching user shortcut
                // that carries a custom icon.
                if (HasExplicitCustomIcon(lnk, target) &&
                    TryReadNativeIcon(lnk, target, out BitmapSource? explicitIcon))
                {
                    return explicitIcon;
                }

                if (TryFindCustomIconShortcut(target, out string customLnk) &&
                    TryReadNativeIcon(customLnk, target, out BitmapSource? customIcon))
                {
                    return customIcon;
                }

                return TryReadNativeIcon(lnk, target, out BitmapSource? pinIcon)
                    ? pinIcon
                    : null;
            }
            catch
            {
                return null;
            }
        }

        private static bool TryReadNativeIcon(
            string lnk, string target, out BitmapSource? icon)
        {
            icon = null;
            IntPtr hicon = IntPtr.Zero;
            try
            {
                hicon = Interop.NativeMethods.W7T_GetLinkIcon(
                    string.IsNullOrEmpty(lnk) ? null : lnk,
                    string.IsNullOrEmpty(target) ? null : target, 1);
                if (hicon == IntPtr.Zero)
                {
                    return false;
                }

                icon = System.Windows.Interop.Imaging
                    .CreateBitmapSourceFromHIcon(
                        hicon, System.Windows.Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions());
                if (icon == null || !DrawsSomething(icon))
                {
                    // A fully transparent result is what an empty taskbar
                    // button looks like, and on a pinned group it would
                    // override the real window icon: treat it as a failure so
                    // the caller tries the next candidate.
                    icon = null;
                    return false;
                }
                return true;
            }
            catch
            {
                return false;
            }
            finally
            {
                if (hicon != IntPtr.Zero)
                {
                    Interop.NativeMethods.DestroyIcon(hicon);
                }
            }
        }

        /// <summary>
        /// True when the converted bitmap really draws at least one pixel.
        /// The HICON to BitmapSource conversion can hand back a well-formed
        /// but completely transparent image; on screen that is an empty
        /// button, and in the pin path it would win over the icon of the
        /// running window. Mirrors the native BitmapHasContent check.
        /// </summary>
        private static bool DrawsSomething(BitmapSource icon)
        {
            try
            {
                if (icon.Format != System.Windows.Media.PixelFormats.Bgra32 &&
                    icon.Format != System.Windows.Media.PixelFormats.Pbgra32)
                {
                    // Not a 32-bit source: there is nothing to inspect here,
                    // so the icon is kept rather than thrown away.
                    return true;
                }

                int stride = icon.PixelWidth * 4;
                if (stride <= 0 || icon.PixelHeight <= 0)
                {
                    return false;
                }

                byte[] pixels = new byte[stride * icon.PixelHeight];
                icon.CopyPixels(pixels, stride, 0);
                for (int i = 3; i < pixels.Length; i += 4)
                {
                    if (pixels[i] != 0 &&
                        (pixels[i - 3] | pixels[i - 2] | pixels[i - 1]) != 0)
                    {
                        return true;
                    }
                }
                return false;
            }
            catch
            {
                // A bitmap that cannot even be read is not usable.
                return false;
            }
        }

        /* v1.21.8: shortcuts the user may have customised. The shell keeps
         * its shortcuts in well-known folders; these are the documented ones,
         * and the only ones looked at. Nothing outside them is ever read.
         *
         * The map is built once and then reused: looking for an icon must not
         * turn into a filesystem walk on every taskbar refresh. It is dropped
         * when the pin set changes (InvalidateShortcutIconIndex), which is
         * also the moment a user could have created or edited a shortcut. */
        private static readonly object IconIndexLock = new();
        private static Dictionary<string, string>? _customIconByTarget;

        /// <summary>
        /// Forgets the shortcut/icon map. Called when the pinned set changes:
        /// the next icon lookup rebuilds it.
        /// </summary>
        public static void InvalidateShortcutIconIndex()
        {
            lock (IconIndexLock)
            {
                _customIconByTarget = null;
            }
        }

        /// <summary>
        /// Folders that may hold a user shortcut with a custom icon, in the
        /// order they are preferred. Every folder is resolved through the
        /// shell's own known-folder API, so redirected profiles and different
        /// system languages work without hardcoding a path.
        /// </summary>
        private static List<string> ShortcutFolders()
        {
            var folders = new List<string>();
            void Add(Environment.SpecialFolder folder)
            {
                try
                {
                    string path = Environment.GetFolderPath(folder);
                    if (!string.IsNullOrWhiteSpace(path) &&
                        !folders.Exists(f => string.Equals(f, path,
                            StringComparison.OrdinalIgnoreCase)))
                    {
                        folders.Add(path);
                    }
                }
                catch
                {
                    // A missing known folder never stops the icon lookup.
                }
            }

            Add(Environment.SpecialFolder.DesktopDirectory);
            Add(Environment.SpecialFolder.CommonDesktopDirectory);
            Add(Environment.SpecialFolder.Programs);
            Add(Environment.SpecialFolder.CommonPrograms);

            try
            {
                string appData = Environment.GetFolderPath(
                    Environment.SpecialFolder.ApplicationData);
                if (!string.IsNullOrWhiteSpace(appData))
                {
                    string quickLaunch = Path.Combine(appData, "Microsoft",
                        "Internet Explorer", "Quick Launch");
                    if (Directory.Exists(quickLaunch))
                    {
                        folders.Add(quickLaunch);
                    }
                }
            }
            catch
            {
            }

            return folders;
        }

        /// <summary>
        /// Walks a folder without following it forever: bounded depth, bounded
        /// number of files and every unreadable folder simply skipped. The
        /// Start Menu tree is the only deeply nested one and no shortcut of
        /// interest sits below the first few levels.
        /// </summary>
        private static void EnumerateLinks(string root, int maxDepth,
                                           int maxFiles, List<string> found)
        {
            if (maxDepth < 0 || found.Count >= maxFiles)
            {
                return;
            }

            IEnumerable<string> files;
            try
            {
                files = Directory.EnumerateFiles(root, "*.lnk",
                    SearchOption.TopDirectoryOnly);
            }
            catch
            {
                return;
            }

            foreach (string file in files)
            {
                if (found.Count >= maxFiles)
                {
                    return;
                }
                found.Add(file);
            }

            IEnumerable<string> dirs;
            try
            {
                dirs = Directory.EnumerateDirectories(root);
            }
            catch
            {
                return;
            }

            foreach (string dir in dirs)
            {
                EnumerateLinks(dir, maxDepth - 1, maxFiles, found);
            }
        }

        /// <summary>
        /// Target path (normalised, case-insensitive) to shortcut that carries
        /// a custom icon. Built once per pin refresh; a shortcut without a
        /// custom icon is not part of the map, so a lookup can only ever
        /// return a real user choice.
        /// </summary>
        private static Dictionary<string, string> CustomIconIndex()
        {
            lock (IconIndexLock)
            {
                if (_customIconByTarget != null)
                {
                    return _customIconByTarget;
                }

                var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
                var links = new List<string>();
                foreach (string folder in ShortcutFolders())
                {
                    EnumerateLinks(folder, 4, 4000, links);
                }

                foreach (string candidate in links)
                {
                    try
                    {
                        if (!TryGetShortcutTarget(candidate, out string target) ||
                            string.IsNullOrWhiteSpace(target) ||
                            !HasExplicitCustomIcon(candidate, target))
                        {
                            continue;
                        }

                        string key = NormalizeKey(target);
                        if (key.Length != 0 && !map.ContainsKey(key))
                        {
                            map[key] = candidate;
                        }
                    }
                    catch
                    {
                        // One unreadable shortcut never stops the others.
                    }
                }

                _customIconByTarget = map;
                return map;
            }
        }

        private static string NormalizeKey(string path)
        {
            try
            {
                return Path.GetFullPath(
                        Environment.ExpandEnvironmentVariables(path ?? string.Empty))
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            }
            catch
            {
                return (path ?? string.Empty).Trim();
            }
        }

        /// <summary>
        /// Returns true when the pinned .lnk explicitly points its icon to a
        /// resource/path different from the target EXE, or uses a non-zero
        /// resource index. WScript.Shell is already used by the taskbar for
        /// creating/removing pins, so this is a conservative reuse of an
        /// existing Windows component rather than a new native dependency.
        /// </summary>
        private static bool HasExplicitCustomIcon(string lnk, string target)
        {
            if (string.IsNullOrWhiteSpace(lnk) || !File.Exists(lnk))
            {
                return false;
            }

            object? shell = null;
            object? shortcut = null;
            try
            {
                Type? shellType = Type.GetTypeFromProgID("WScript.Shell");
                if (shellType == null)
                {
                    return false;
                }

                shell = Activator.CreateInstance(shellType);
                dynamic sh = shell!;
                shortcut = sh.CreateShortcut(lnk);
                dynamic sc = shortcut!;

                string iconLocation = Convert.ToString(sc.IconLocation) ?? string.Empty;
                if (string.IsNullOrWhiteSpace(iconLocation))
                {
                    return false;
                }

                int comma = iconLocation.LastIndexOf(',');
                string iconPath = comma >= 0
                    ? iconLocation[..comma].Trim().Trim('"')
                    : iconLocation.Trim().Trim('"');
                string indexText = comma >= 0
                    ? iconLocation[(comma + 1)..].Trim()
                    : "0";

                int index = 0;
                _ = int.TryParse(indexText, out index);

                if (index != 0)
                {
                    return true;
                }

                iconPath = Environment.ExpandEnvironmentVariables(iconPath);
                return !PathsEqual(iconPath, target);
            }
            catch
            {
                return false;
            }
            finally
            {
                ReleaseComObject(shortcut);
                ReleaseComObject(shell);
            }
        }

        /// <summary>
        /// Finds a shortcut of the user whose target is the same executable
        /// and whose icon is explicitly customised, so that a custom icon is
        /// honoured instead of the executable's own icon.
        ///
        /// v1.21.8: the folders searched are all the documented shell shortcut
        /// locations - the two Desktops, the two Start Menu program trees and
        /// Quick Launch - and the result is cached (see CustomIconIndex). Only
        /// .lnk files inside those folders are read, at most a few thousand,
        /// never deeper than a few levels: no unrestricted filesystem scan.
        /// </summary>
        private static bool TryFindCustomIconShortcut(string target, out string lnk)
        {
            lnk = string.Empty;
            if (string.IsNullOrWhiteSpace(target))
            {
                return false;
            }

            try
            {
                Dictionary<string, string> map = CustomIconIndex();
                if (map.TryGetValue(NormalizeKey(target), out string? found) &&
                    !string.IsNullOrEmpty(found))
                {
                    lnk = found;
                    return true;
                }
            }
            catch
            {
                // An unreadable index only means "no custom icon found": the
                // caller keeps the icons it already knows.
            }

            lnk = string.Empty;
            return false;
        }

        private static bool TryGetShortcutTarget(string lnk, out string target)
        {
            target = string.Empty;
            object? shell = null;
            object? shortcut = null;
            try
            {
                Type? shellType = Type.GetTypeFromProgID("WScript.Shell");
                if (shellType == null)
                {
                    return false;
                }

                shell = Activator.CreateInstance(shellType);
                dynamic sh = shell!;
                shortcut = sh.CreateShortcut(lnk);
                dynamic sc = shortcut!;
                target = Convert.ToString(sc.TargetPath) ?? string.Empty;
                target = Environment.ExpandEnvironmentVariables(target).Trim().Trim('"');
                return !string.IsNullOrWhiteSpace(target);
            }
            catch
            {
                return false;
            }
            finally
            {
                ReleaseComObject(shortcut);
                ReleaseComObject(shell);
            }
        }

        private static bool PathsEqual(string left, string right)
        {
            try
            {
                string a = Path.GetFullPath(
                    Environment.ExpandEnvironmentVariables(left ?? string.Empty))
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                string b = Path.GetFullPath(
                    Environment.ExpandEnvironmentVariables(right ?? string.Empty))
                    .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                return string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return string.Equals(left, right, StringComparison.OrdinalIgnoreCase);
            }
        }

        private static void ReleaseComObject(object? value)
        {
            try
            {
                if (value != null && Marshal.IsComObject(value))
                {
                    Marshal.FinalReleaseComObject(value);
                }
            }
            catch
            {
                // Cleanup must never affect icon loading.
            }
        }
    }
}