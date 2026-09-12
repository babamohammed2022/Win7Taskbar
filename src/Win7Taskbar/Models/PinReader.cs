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
            System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Microsoft", "Internet Explorer", "Quick Launch",
                "User Pinned", "TaskBar");

        /// <summary>
        /// Icona grande dell'applicazione pinnata (presentazione), senza
        /// overlay di collegamento. Risolta dal core nativo; se non trova
        /// nulla restituisce null e il pulsante usa il fallback del tema.
        /// </summary>
        internal static BitmapSource? ReadIcon(string lnk, string target)
        {
            IntPtr hicon = IntPtr.Zero;
            try
            {
                hicon = Interop.NativeMethods.W7T_GetLinkIcon(
                    string.IsNullOrEmpty(lnk) ? null : lnk,
                    string.IsNullOrEmpty(target) ? null : target, 1);
                if (hicon == IntPtr.Zero)
                {
                    return null;
                }

                return System.Windows.Interop.Imaging
                    .CreateBitmapSourceFromHIcon(
                        hicon, System.Windows.Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions());
            }
            catch
            {
                return null;
            }
            finally
            {
                if (hicon != IntPtr.Zero)
                {
                    Interop.NativeMethods.DestroyIcon(hicon);
                }
            }
        }
    }
}
