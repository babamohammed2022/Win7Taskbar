// Win7Taskbar - Start Menu search of Control Panel / Settings / All Tasks
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
// Written from scratch. Public Shell APIs only (IShellItem, IEnumShellItems,
// SHCreateItemFromParsingName, FileVersionInfo). Open-Shell inspired the
// result classes (bSearchSettings / God Mode / Search the Internet as a
// clickable provider row) — no Open-Shell source is copied.
// v3.10: 24px icons, the Open-Shell (MIT) Win7 search row metric.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Media;
using Microsoft.Win32;

namespace Win7Taskbar.StartMenu
{
    internal sealed class ShellSearchHit
    {
        public string Name { get; set; } = string.Empty;
        public string Path { get; set; } = string.Empty;
        public ImageSource? Icon { get; set; }
    }

    /// <summary>
    /// Enumerates Control Panel applets, All Tasks (God Mode CLSID), and
    /// the Settings app on a worker thread. Filtering is cheap after that.
    /// </summary>
    internal static class StartMenuShellSearch
    {
        private static readonly object Gate = new();
        private static List<ShellSearchHit>? _catalog;
        private static int _state; /* 0 idle, 1 loading, 2 ready */

        /* Control Panel (category), All Control Panel Items, All Tasks. */
        private static readonly string[] kFolders =
        {
            "shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}",
            "shell:::{21EC2020-3AEA-1069-A2DD-08002B30309D}",
            "shell:::{ED7BA470-8E54-465E-825C-99712043E01C}"
        };

        private static readonly Guid IidShellItem =
            new("43826d1e-e718-42ee-bc55-a1e261c37bfe");
        private static readonly Guid IidEnumShellItems =
            new("70629033-e363-4a28-a567-0db78006e6d7");
        private static readonly Guid BhidEnumItems =
            new("94f60519-2850-4924-aa5a-502dd77eb87f");

        private const uint SigdnNormalDisplay = 0;
        private const uint SigdnDesktopAbsoluteParsing = 0x80028000;
        /* v3.15: ricostruzione del parsing name quando l'assoluto fallisce
         * (alcuni provider del Pannello di controllo rifiutano il formato
         * assoluto su installazioni localizzate): chiede al figlio il
         * parsing RELATIVO alla propria cartella e lo appende al nome di
         * parsing della cartella padre, che noi stiamo enumerando e che
         * conosciamo gia'. Stessa tecnica del catalogo di Open-Shell per
         * gli elementi "Settings": mai scartare una voce davvero
         * presente nella cartella virtuale. */
        private const uint SigdnParentRelativeParsing = 0x80018001;

        public static bool IsReady
        {
            get { return Volatile.Read(ref _state) == 2; }
        }

        public static void BeginLoad()
        {
            if (Interlocked.CompareExchange(ref _state, 1, 0) != 0)
            {
                return;
            }
            ThreadPool.QueueUserWorkItem(_ =>
            {
                try
                {
                    List<ShellSearchHit> list = Enumerate();
                    lock (Gate)
                    {
                        _catalog = list;
                    }
                    Interlocked.Exchange(ref _state, 2);
                }
                catch (Exception)
                {
                    Interlocked.Exchange(ref _state, 0);
                }
            });
        }

        public static IReadOnlyList<ShellSearchHit> Match(string query, int cap)
        {
            if (string.IsNullOrWhiteSpace(query) || cap <= 0)
            {
                return Array.Empty<ShellSearchHit>();
            }
            if (!IsReady)
            {
                BeginLoad();
                return Array.Empty<ShellSearchHit>();
            }
            List<ShellSearchHit>? catalog;
            lock (Gate)
            {
                catalog = _catalog;
            }
            if (catalog == null || catalog.Count == 0)
            {
                return Array.Empty<ShellSearchHit>();
            }
            string needle = query.Trim();
            var hits = new List<ShellSearchHit>(Math.Min(cap, 16));
            try
            {
                foreach (ShellSearchHit item in catalog)
                {
                    if (hits.Count >= cap)
                    {
                        break;
                    }
                    if (string.IsNullOrEmpty(item.Name))
                    {
                        continue;
                    }
                    if (item.Name.IndexOf(needle, StringComparison.CurrentCultureIgnoreCase) < 0)
                    {
                        continue;
                    }
                    hits.Add(item);
                }
            }
            catch (Exception)
            {
            }
            return hits;
        }

        public static string InternetSearchUrl(string query)
        {
            string terms = Uri.EscapeDataString(query ?? string.Empty);
            try
            {
                using RegistryKey? scopes = Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Internet Explorer\SearchScopes");
                string? def = scopes?.GetValue("DefaultScope") as string;
                if (!string.IsNullOrEmpty(def) && scopes != null)
                {
                    using RegistryKey? scope = scopes.OpenSubKey(def);
                    string? url = scope?.GetValue("URL") as string;
                    if (!string.IsNullOrEmpty(url) &&
                        url.IndexOf("{searchTerms}", StringComparison.OrdinalIgnoreCase) >= 0)
                    {
                        return url.Replace("{searchTerms}", terms,
                            StringComparison.OrdinalIgnoreCase);
                    }
                }
            }
            catch (Exception)
            {
            }
            return "https://www.bing.com/search?q=" + terms;
        }

        public static bool SettingsAppExists()
        {
            try
            {
                string root = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
                return Directory.Exists(Path.Combine(root, "ImmersiveControlPanel"));
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static List<ShellSearchHit> Enumerate()
        {
            var list = new List<ShellSearchHit>(256);
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string folder in kFolders)
            {
                try
                {
                    EnumerateFolder(folder, list, seen, 400);
                }
                catch (Exception)
                {
                }
            }
            try
            {
                AddCplFiles(list, seen);
            }
            catch (Exception)
            {
            }
            return list;
        }

        private static void AddCplFiles(List<ShellSearchHit> list, HashSet<string> seen)
        {
            string sys = Environment.SystemDirectory;
            if (string.IsNullOrEmpty(sys) || !Directory.Exists(sys))
            {
                return;
            }
            foreach (string cpl in Directory.GetFiles(sys, "*.cpl"))
            {
                string name = string.Empty;
                try
                {
                    FileVersionInfo info = FileVersionInfo.GetVersionInfo(cpl);
                    if (!string.IsNullOrWhiteSpace(info.FileDescription))
                    {
                        /* v3.12: unica funzione di normalizzazione per
                         * tutto il catalogo (prima era un .Trim() manuale,
                         * incoerente col percorso shell e sorgente dei
                         * duplicati "link vuoto" come "Pannello di
                         * controllo" x2). */
                        name = NormalizeDisplayName(info.FileDescription);
                    }
                }
                catch (Exception)
                {
                }
                if (string.IsNullOrEmpty(name))
                {
                    name = NormalizeDisplayName(
                        Path.GetFileNameWithoutExtension(cpl));
                }
                if (string.IsNullOrEmpty(name) || !seen.Add(name))
                {
                    continue;
                }
                list.Add(new ShellSearchHit
                {
                    Name = name,
                    Path = cpl,
                    Icon = StartMenuIcons.FromPath(cpl, cpl, 24)
                });
            }
        }

        private static void EnumerateFolder(string parsingName, List<ShellSearchHit> list,
            HashSet<string> seen, int cap)
        {
            Guid iidItem = IidShellItem;
            int created = SHCreateItemFromParsingName(parsingName, IntPtr.Zero,
                ref iidItem, out IShellItem? folder);
            if (created != 0 || folder == null)
            {
                return;
            }
            IntPtr enumPtr = IntPtr.Zero;
            try
            {
                Guid bhid = BhidEnumItems;
                Guid iidEnum = IidEnumShellItems;
                int bind = folder.BindToHandler(IntPtr.Zero, ref bhid, ref iidEnum, out enumPtr);
                if (bind != 0 || enumPtr == IntPtr.Zero)
                {
                    return;
                }
                object enumObj = Marshal.GetObjectForIUnknown(enumPtr);
                try
                {
                    var enumerator = (IEnumShellItems)enumObj;
                    while (list.Count < cap)
                    {
                        IntPtr itemPtr = IntPtr.Zero;
                        uint fetched = 0;
                        int next = enumerator.Next(1, out itemPtr, out fetched);
                        if (next != 0 || fetched == 0 || itemPtr == IntPtr.Zero)
                        {
                            break;
                        }
                        object? itemObj = null;
                        try
                        {
                            itemObj = Marshal.GetObjectForIUnknown(itemPtr);
                            if (itemObj is IShellItem item)
                            {
                                AddShellItem(item, parsingName, list, seen);
                            }
                        }
                        catch (Exception)
                        {
                        }
                        finally
                        {
                            if (itemObj != null)
                            {
                                try { Marshal.ReleaseComObject(itemObj); }
                                catch (Exception) { }
                            }
                            Marshal.Release(itemPtr);
                        }
                    }
                }
                finally
                {
                    try { Marshal.ReleaseComObject(enumObj); }
                    catch (Exception) { }
                }
            }
            finally
            {
                if (enumPtr != IntPtr.Zero)
                {
                    Marshal.Release(enumPtr);
                }
                try { Marshal.ReleaseComObject(folder); }
                catch (Exception) { }
            }
        }

        private static void AddShellItem(IShellItem item, List<ShellSearchHit> list,
            HashSet<string> seen)
            => AddShellItem(item, null, list, seen);

        private static void AddShellItem(IShellItem item, string? parentParsing,
            List<ShellSearchHit> list, HashSet<string> seen)
        {
            /* v3.12: normalizza PRIMA del controllo di deduplicazione.
             * Senza questo trim, whitespace invisibili (spazi finali, NBSP,
             * ritorni a capo residui restituiti da alcuni provider shell)
             * facevano fallire silenziosamente l'HashSet e producevano
             * voci duplicate ("Pannello di controllo" x2), una delle quali
             * spesso con Path/Icon non risolti (il "link vuoto"). */
            string name = NormalizeDisplayName(ReadName(item, SigdnNormalDisplay));
            if (string.IsNullOrWhiteSpace(name))
            {
                return;
            }
            string parse = ReadName(item, SigdnDesktopAbsoluteParsing);
            /* v3.15: il parsing assoluto manca a diversi provider del
             * Pannello di controllo (la regola v3.13 "solo nomi reali" li
             * scartava tutti - niente icone/risultati). Prima di
             * rinunciare si ricostruisce: parsing relativo alla cartella +
             * nome di parsing della cartella padre che stiamo enumerando.
             * Rimane un VERO percorso lanciabile da SHCreateItemFromParsingName,
             * semplicemente ottenuto a pezzi anziche' in un colpo solo. */
            if (string.IsNullOrWhiteSpace(parse) &&
                !string.IsNullOrWhiteSpace(parentParsing))
            {
                string relative = ReadName(item, SigdnParentRelativeParsing);
                if (!string.IsNullOrWhiteSpace(relative))
                {
                    string rebuilt = parentParsing.TrimEnd('\\')
                        + "\\" + relative.Trim('\\');
                    /* Verifica documentata: solo se la shell riconosce il
                     * percorso ricostruito la voce viene pubblicata. */
                    if (ShellItemExists(rebuilt))
                    {
                        parse = rebuilt;
                    }
                }
            }
            /* v3.15.1 - RIPRISTINO RICHIESTO: se anche la ricostruzione
             * padre+relativo non basta, la voce NON viene piu' scartata ma
             * torna il comportamento storico (pre-v3.13) che rendeva le
             * voci del Pannello di controllo VISIBILI con la loro icona:
             * si usa il nome mostrato come nome di parsing, con il solito
             * prefisso "shell:". Avviso onesto, ripetuto qui come in
             * release note: su queste voci di *riserva* il click puo' non
             * risolvere nulla sulla macchina (e' il compromesso accettato
             * tra v3.13 e v3.15 - voci visibili vs. tutti lanciabili), ma
             * la riga resta completa di icona e nome, come era. */
            if (string.IsNullOrWhiteSpace(parse))
            {
                parse = name;
            }
            if (!parse.StartsWith("shell:", StringComparison.OrdinalIgnoreCase) &&
                !parse.StartsWith("::", StringComparison.Ordinal) &&
                !parse.StartsWith("ms-", StringComparison.OrdinalIgnoreCase) &&
                parse.IndexOf(':') < 0 && parse.IndexOf('\\') < 0)
            {
                parse = "shell:" + parse;
            }
            /* v3.12: scarta voci "morte": nessun percorso risolvibile.
             * Il controllo di unicita' avviene SOLO ora, sul nome
             * normalizzato: se fallisce, la voce e' un vero duplicato. */
            if (string.IsNullOrWhiteSpace(parse) || !seen.Add(name))
            {
                return;
            }
            ImageSource? icon = null;
            try
            {
                icon = StartMenuIcons.FromParsingName(parse, 24)
                    ?? StartMenuIcons.FromDll("imageres.dll", 22, 24);
            }
            catch (Exception)
            {
                /* Nessuna risorsa nativa e' stata acquisita qui
                 * (StartMenuIcons gestisce i propri handle): si prosegue
                 * senza icona invece di propagare l'eccezione. */
            }
            list.Add(new ShellSearchHit
            {
                Name = name,
                Path = parse,
                Icon = icon
            });
        }

        /// <summary>
        /// v3.12: normalizza un nome visualizzato dallo shell: converte i
        /// NBSP in spazi normali, rimuove il whitespace ai bordi (inclusi
        /// tab e newline residui) e comprime gli spazi multipli interni,
        /// cosi' che due IShellItem che mostrano lo stesso testo a video
        /// producano SEMPRE la stessa chiave nel dedup HashSet.
        /// </summary>
        private static string NormalizeDisplayName(string? raw)
        {
            if (string.IsNullOrEmpty(raw))
            {
                return string.Empty;
            }
            /*   = non-breaking space, spesso restituito da provider
             * shell localizzati al posto dello spazio normale. */
            string cleaned = raw.Replace(' ', ' ').Trim();
            if (cleaned.IndexOf("  ", StringComparison.Ordinal) >= 0)
            {
                cleaned = string.Join(' ',
                    cleaned.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));
            }
            return cleaned;
        }

        /// <summary>v3.15: verifica che un nome di parsing ricostruito sia
        /// davvero risolvibile dalla shell (come farebbe il click nel menu):
        /// solo allora la voce viene tenuta. Tutto protetto: COM rilasciato
        /// anche sul ramo di uscita forzata.</summary>
        private static bool ShellItemExists(string parsingName)
        {
            Guid iidItem = IidShellItem;
            try
            {
                int hr = SHCreateItemFromParsingName(parsingName, IntPtr.Zero,
                    ref iidItem, out IShellItem? probe);
                if (hr != 0 || probe == null)
                {
                    return false;
                }
                try { Marshal.ReleaseComObject(probe); }
                catch (Exception) { }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static string ReadName(IShellItem item, uint sigdn)
        {
            IntPtr p = IntPtr.Zero;
            try
            {
                item.GetDisplayName(sigdn, out p);
                if (p == IntPtr.Zero)
                {
                    return string.Empty;
                }
                return Marshal.PtrToStringUni(p) ?? string.Empty;
            }
            catch (Exception)
            {
                return string.Empty;
            }
            finally
            {
                if (p != IntPtr.Zero)
                {
                    CoTaskMemFree(p);
                }
            }
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
        private interface IShellItem
        {
            [PreserveSig]
            int BindToHandler(IntPtr pbc, [In] ref Guid bhid, [In] ref Guid riid, out IntPtr ppv);
            void GetParent(out IShellItem ppsi);
            void GetDisplayName(uint sigdnName, out IntPtr ppszName);
            void GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
            void Compare(IShellItem psi, uint hint, out int piOrder);
        }

        [ComImport]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        [Guid("70629033-e363-4a28-a567-0db78006e6d7")]
        private interface IEnumShellItems
        {
            [PreserveSig]
            int Next(uint celt, out IntPtr rgelt, out uint pceltFetched);
            [PreserveSig]
            int Skip(uint celt);
            [PreserveSig]
            int Reset();
            [PreserveSig]
            int Clone(out IEnumShellItems ppenum);
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = true)]
        private static extern int SHCreateItemFromParsingName(
            string pszPath, IntPtr pbc, [In] ref Guid riid,
            [MarshalAs(UnmanagedType.Interface)] out IShellItem? ppv);

        [DllImport("ole32.dll")]
        private static extern void CoTaskMemFree(IntPtr pv);
    }
}
