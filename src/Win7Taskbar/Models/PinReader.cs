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
        /// viene cercato un collegamento Desktop che punti allo stesso target.
        /// Questo permette, ad esempio, di usare l'icona personalizzata di
        /// "Roblox.lnk" invece di quella incorporata nell'eseguibile Roblox.
        /// Il pin mantiene comunque la precedenza quando dichiara gia' una
        /// propria icona personalizzata.
        /// </summary>
        internal static BitmapSource? ReadIcon(string lnk, string target)
        {
            try
            {
                // First preserve an explicit custom icon already stored in the
                // real taskbar pin. Only when it is absent/default do we look
                // for a matching user shortcut on the Desktop.
                if (HasExplicitCustomIcon(lnk, target) &&
                    TryReadNativeIcon(lnk, target, out BitmapSource? explicitIcon))
                {
                    return explicitIcon;
                }

                if (TryFindDesktopShortcut(target, out string desktopLnk) &&
                    TryReadNativeIcon(desktopLnk, target, out BitmapSource? desktopIcon))
                {
                    return desktopIcon;
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
                return icon != null;
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
        /// Finds a Desktop .lnk whose target is the same executable and whose
        /// icon is explicitly customized. Only the user's Desktop is searched;
        /// there is no recursive filesystem scan.
        /// </summary>
        private static bool TryFindDesktopShortcut(string target, out string lnk)
        {
            lnk = string.Empty;
            if (string.IsNullOrWhiteSpace(target))
            {
                return false;
            }

            var folders = new List<string>
            {
                Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory)
            };

            try
            {
                string commonDesktop = Environment.GetFolderPath(
                    Environment.SpecialFolder.CommonDesktopDirectory);
                if (!string.IsNullOrWhiteSpace(commonDesktop) &&
                    !folders.Exists(f => string.Equals(f, commonDesktop,
                        StringComparison.OrdinalIgnoreCase)))
                {
                    folders.Add(commonDesktop);
                }
            }
            catch
            {
                // Some restricted Windows environments do not expose the
                // common desktop folder. The user Desktop remains sufficient.
            }

            foreach (string folder in folders)
            {
                if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder))
                {
                    continue;
                }

                IEnumerable<string> links;
                try
                {
                    links = Directory.EnumerateFiles(folder, "*.lnk", SearchOption.TopDirectoryOnly);
                }
                catch
                {
                    continue;
                }

                foreach (string candidate in links)
                {
                    if (!TryGetShortcutTarget(candidate, out string candidateTarget) ||
                        !PathsEqual(candidateTarget, target) ||
                        !HasExplicitCustomIcon(candidate, target))
                    {
                        continue;
                    }

                    lnk = candidate;
                    return true;
                }
            }

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