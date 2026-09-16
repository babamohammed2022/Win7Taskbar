// Win7Taskbar - barre degli strumenti desktop/collegamenti/indirizzi
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v2.3: tre bande come quelle della shell di Windows 7, ispirate a
// ExplorerEx (kfh83/ExplorerEx, che le reimplementa in WPF):
//   - Desktop : menu a comparsa con icone/file/cartelle del desktop, per
//               aprirli senza minimizzare le finestre;
//   - Collegamenti (Links): scorciatoie rapide della cartella Collegamenti;
//   - Indirizzo : casella per digitare un percorso o un URL e aprirlo.
// Implementate come layer applicativo WPF sopra API shell pubbliche
// (SHGetFileInfo per le icone reali, ShellExecute per l'apertura), senza
// reimplementare controlli comuni e senza toccare explorer.exe.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media.Imaging;

namespace Win7Taskbar
{
    public partial class TaskbarWindow
    {
        private sealed class ShellBandItem
        {
            public string Name { get; init; } = string.Empty;
            public string Path { get; init; } = string.Empty;
            public BitmapSource? Icon { get; init; }
        }

        /// <summary>RAII ownership for SHGetFileInfo's copied HICON. This also
        /// covers exceptions during WPF BitmapSource conversion.</summary>
        private sealed class SafeShellIconHandle
            : Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid
        {
            internal SafeShellIconHandle(IntPtr handle) : base(true)
            {
                SetHandle(handle);
            }

            protected override bool ReleaseHandle()
                => Interop.NativeMethods.DestroyIcon(handle);
        }

        // ------------------------------------------------------------------
        // v2.5: preferenze delle barre salvate in un INI leggibile a mano:
        //   %LocalAppData%\Win7Taskbar\toolbars.ini
        //     [Toolbars]
        //     desktop=0/1
        //     links=0/1
        //     address=0/1
        // Le barre restano opt-in (default 0: nessuna barra forzata, si
        // attivano dal menu contestuale della barra); una volta attivate,
        // la scelta sopravvive ai riavvii dell'app.
        // ------------------------------------------------------------------

        private static string ToolbarPrefsPath =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                         "Win7Taskbar", "toolbars.ini");

        private static Dictionary<string, string> LoadToolbarPrefs()
        {
            var prefs = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                if (!File.Exists(ToolbarPrefsPath))
                {
                    return prefs;
                }

                foreach (string raw in File.ReadAllLines(ToolbarPrefsPath))
                {
                    string line = raw.Trim();
                    if (line.Length == 0 || line.StartsWith("[") || line.StartsWith(";"))
                    {
                        continue;
                    }

                    int eq = line.IndexOf('=');
                    if (eq <= 0)
                    {
                        continue;
                    }

                    prefs[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"LoadToolbarPrefs: {ex.Message}");
            }
            return prefs;
        }

        private void SaveToolbarPrefs()
        {
            try
            {
                string dir = Path.GetDirectoryName(ToolbarPrefsPath)!;
                Directory.CreateDirectory(dir);

                File.WriteAllText(ToolbarPrefsPath,
                    "; Win7Taskbar - barre degli strumenti scelte dal menu contestuale\n" +
                    "; 1 = visibile, 0 = nascosta\n" +
                    "[Toolbars]\n" +
                    $"desktop={(DesktopBandHost.Visibility == Visibility.Visible ? 1 : 0)}\n" +
                    $"links={(LinksBandHost.Visibility == Visibility.Visible ? 1 : 0)}\n" +
                    $"address={(AddressBandHost.Visibility == Visibility.Visible ? 1 : 0)}\n");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"SaveToolbarPrefs: {ex.Message}");
            }
        }

        private void InitShellToolbars()
        {
            LoadDesktopBand();
            LoadLinksBand();

            // Riapplica le scelte salvate nell'INI (default: tutto nascosto,
            // le barre sono opt-in dal menu contestuale della barra).
            var prefs = LoadToolbarPrefs();
            bool One(string key) => prefs.TryGetValue(key, out string? v) && v == "1";

            DesktopBandHost.Visibility = One("desktop") ? Visibility.Visible : Visibility.Collapsed;
            LinksBandHost.Visibility = One("links") && LinksBand.Items.Count > 0
                ? Visibility.Visible : Visibility.Collapsed;
            AddressBandHost.Visibility = One("address") ? Visibility.Visible : Visibility.Collapsed;
        }

        // ------------------- Desktop -------------------

        /// <summary>
        /// v2.47: stato attuale delle NOSTRE barre degli strumenti (quelle
        /// della mod: Desktop, Indirizzi, Collegamenti). Lo legge la finestra
        /// Proprieta' per mostrare le caselle della scheda "Barre degli
        /// strumenti" allineate a quello che c'e' davvero sulla barra.
        /// </summary>
        internal void GetToolbarStates(out bool desktop, out bool links, out bool address)
        {
            desktop = DesktopBandHost.Visibility == Visibility.Visible;
            links = LinksBandHost.Visibility == Visibility.Visible;
            address = AddressBandHost.Visibility == Visibility.Visible;
        }

        /// <summary>
        /// v2.47: la finestra Proprieta' accende e spegne le nostre barre.
        ///
        /// Stesso percorso del menu contestuale della barra: si cambia la
        /// Visibility degli host e si riscrive il file delle preferenze, cosi'
        /// la scelta sopravvive al riavvio. L'ini registra l'INTENZIONE
        /// dell'utente anche quando la barra non puo' comparire subito (per
        /// esempio i Collegamenti senza alcun elemento): appena ci sara'
        /// qualcosa da mostrare, la preferenza e' gia' quella giusta.
        /// </summary>
        internal void SetToolbarStates(bool desktop, bool links, bool address)
        {
            try
            {
                /* v2.48: si usano GLI STESSI COMANDI del menu contestuale della
                 * barra ("Barre degli strumenti"): SetBandVisible accende la
                 * banda della mod - non una barra di Windows - ne carica il
                 * contenuto quando serve e salva le preferenze. */
                SetBandVisible(DesktopBandHost, desktop);
                SetBandVisible(LinksBandHost, links);
                SetBandVisible(AddressBandHost, address);

                /* L'ini registra l'INTENZIONE dell'utente anche quando la
                 * banda non puo' comparire subito (Collegamenti senza
                 * elementi): appena ci sara' qualcosa da mostrare, la
                 * preferenza e' gia' quella giusta. */
                string dir = System.IO.Path.GetDirectoryName(ToolbarPrefsPath)!;
                System.IO.Directory.CreateDirectory(dir);
                System.IO.File.WriteAllText(ToolbarPrefsPath,
                    $"desktop={(desktop ? 1 : 0)}\n" +
                    $"links={(links ? 1 : 0)}\n" +
                    $"address={(address ? 1 : 0)}\n");

                Debug.WriteLine(
                    $"barre degli strumenti: desktop={desktop} links={links} address={address}");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"SetToolbarStates: {ex.Message}");
            }
        }

        private void LoadDesktopBand()
        {
            try
            {
                var items = new List<ShellBandItem>();
                var folders = new List<string>
                {
                    Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                    Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory),
                };

                foreach (string folder in folders.Where(f => !string.IsNullOrEmpty(f) && Directory.Exists(f)).Distinct())
                {
                    foreach (var entry in new DirectoryInfo(folder).EnumerateFileSystemInfos()
                                 .OrderBy(e => !(e is DirectoryInfo)).ThenBy(e => e.Name, StringComparer.OrdinalIgnoreCase))
                    {
                        if (items.Count >= 60) break;
                        if (items.Any(i => string.Equals(i.Path, entry.FullName, StringComparison.OrdinalIgnoreCase))) continue;
                        items.Add(new ShellBandItem
                        {
                            Name = entry.Name,
                            Path = entry.FullName,
                            Icon = ExtractFileIcon(entry.FullName, entry is DirectoryInfo),
                        });
                    }
                }

                DesktopBandList.ItemsSource = items;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"DesktopBand: {ex.Message}");
            }
        }

        private void DesktopBandToggle_Click(object sender, RoutedEventArgs e)
        {
            if (DesktopBandPopup.IsOpen)
            {
                DesktopBandPopup.IsOpen = false;
            }
            else
            {
                LoadDesktopBand();   // ricarica: il desktop cambia
                DesktopBandPopup.IsOpen = true;
            }
        }

        private void DesktopBandItem_Click(object sender, MouseButtonEventArgs e)
        {
            if (sender is FrameworkElement fe && fe.DataContext is ShellBandItem item)
            {
                OpenShellPath(item.Path);
                DesktopBandPopup.IsOpen = false;
            }
        }

        // ------------------- Collegamenti -------------------

        private void LoadLinksBand()
        {
            try
            {
                var items = new List<ShellBandItem>();
                string? folder = null;
                foreach (string candidate in new[]
                         {
                             Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Links"),
                             Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Favorites), "Links"),
                         })
                {
                    if (Directory.Exists(candidate)) { folder = candidate; break; }
                }

                if (folder != null)
                {
                    foreach (var file in new DirectoryInfo(folder).EnumerateFiles()
                                 .Where(f => f.Extension.Equals(".lnk", StringComparison.OrdinalIgnoreCase)
                                          || f.Extension.Equals(".url", StringComparison.OrdinalIgnoreCase))
                                 .OrderBy(f => f.Name, StringComparer.OrdinalIgnoreCase)
                                 .Take(12))
                    {
                        items.Add(new ShellBandItem
                        {
                            Name = System.IO.Path.GetFileNameWithoutExtension(file.Name),
                            Path = file.FullName,
                            Icon = ExtractFileIcon(file.FullName, false),
                        });
                    }
                }

                LinksBand.ItemsSource = items;
                LinksBandHost.Visibility = items.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"LinksBand: {ex.Message}");
            }
        }

        /* v2.30: il Button gestisce internamente MouseLeftButtonUp
         * (lo marca handled per il Click): l'handler XAML sul mouse non
         * arrivava mai e i collegamenti sembravano "morti". Si usa Click. */
        private void LinksBandItem_Click(object sender, RoutedEventArgs e)
        {
            if (sender is FrameworkElement fe && fe.DataContext is ShellBandItem item)
            {
                OpenShellPath(item.Path);
            }
        }

        // ------------------- Indirizzo -------------------

        private void AddressBand_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Enter || sender is not TextBox box)
            {
                return;
            }

            string text = box.Text.Trim();
            if (text.Length == 0)
            {
                return;
            }

            OpenShellPath(text);
            Keyboard.ClearFocus();
        }

        // ------------------- comuni -------------------

        /// <summary>Apre percorsi, URL e shell: come fa la shell (ExplorerEx
        /// usa lo stesso meccanismo: ShellExecute sul testo digitato).</summary>
        private static void OpenShellPath(string path)
        {
            string target = Environment.ExpandEnvironmentVariables(path.Trim());
            if (target.Length == 0)
            {
                return;
            }

            // v2.30: euristica della barra Indirizzi di Explorer: testo
            // senza schema, senza barre rovesce e con un punto = URL
            // (es. "youtube.com"), altrimenti la shell fallirebbe in
            // ERROR_FILE_NOT_FOUND e "non succederebbe nulla".
            if (!target.Contains("://") && !target.Contains("\\") &&
                !target.Contains(" ") && target.Contains(".") &&
                !System.IO.File.Exists(target) &&
                !System.IO.Directory.Exists(target))
            {
                target = "https://" + target;
            }

            try
            {
                if (Interop.NativeMethods.W7T_ShellOpen(target))
                {
                    return;
                }
            }
            catch
            {
                // ripiego managed qui sotto
            }

            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = target,
                    UseShellExecute = true,
                });
                return;
            }
            catch (Exception directError)
            {
                // ShellExecute treats the entire string as one file. The
                // Windows 7 Address toolbar also accepted an executable plus
                // arguments, so retry only that conservative command shape.
                try
                {
                    if (TrySplitAddressCommand(target,
                                               out string executable,
                                               out string arguments))
                    {
                        Process.Start(new ProcessStartInfo
                        {
                            FileName = executable,
                            Arguments = arguments,
                            UseShellExecute = true,
                        });
                        return;
                    }
                }
                catch (Exception commandError)
                {
                    Debug.WriteLine(
                        $"Address command ('{target}'): {commandError.Message}");
                    return;
                }

                Debug.WriteLine($"OpenShellPath('{target}'): {directError.Message}");
            }
        }

        private static bool TrySplitAddressCommand(
            string text, out string executable, out string arguments)
        {
            executable = string.Empty;
            arguments = string.Empty;
            string value = text.Trim();
            if (value.Length == 0 || Uri.TryCreate(value, UriKind.Absolute, out _))
            {
                return false;
            }

            if (value[0] == '"')
            {
                int closingQuote = value.IndexOf('"', 1);
                if (closingQuote <= 1)
                {
                    return false;
                }
                executable = value.Substring(1, closingQuote - 1);
                arguments = value.Substring(closingQuote + 1).TrimStart();
            }
            else
            {
                int separator = value.IndexOfAny(new[] { ' ', '\t' });
                if (separator <= 0)
                {
                    return false; // direct ShellExecute already tried it
                }
                executable = value.Substring(0, separator);
                arguments = value.Substring(separator + 1).TrimStart();
            }

            return executable.Length > 0 && arguments.Length > 0;
        }

        /// <summary>Icona REALE del file/cartella dalla shell (SHGetFileInfo),
        /// non un disegno inventato.</summary>
        private static BitmapSource? ExtractFileIcon(string path, bool isFolder)
        {
            try
            {
                var shfi = new Interop.NativeMethods.SHFILEINFOW();
                uint flags = Interop.NativeMethods.SHGFI_ICON | Interop.NativeMethods.SHGFI_SMALLICON;
                uint attrib = isFolder ? Interop.NativeMethods.FILE_ATTRIBUTE_DIRECTORY
                                       : Interop.NativeMethods.FILE_ATTRIBUTE_NORMAL;
                if (Interop.NativeMethods.SHGetFileInfoW(path, attrib, ref shfi,
                        (uint)System.Runtime.InteropServices.Marshal.SizeOf<Interop.NativeMethods.SHFILEINFOW>(),
                        flags) != IntPtr.Zero && shfi.hIcon != IntPtr.Zero)
                {
                    using var icon = new SafeShellIconHandle(shfi.hIcon);
                    var bmp = Imaging.CreateBitmapSourceFromHIcon(
                        icon.DangerousGetHandle(), Int32Rect.Empty,
                        BitmapSizeOptions.FromEmptyOptions());
                    if (bmp.CanFreeze)
                    {
                        bmp.Freeze();
                    }
                    return bmp;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"ExtractFileIcon: {ex.Message}");
            }
            return null;
        }
    }
}
