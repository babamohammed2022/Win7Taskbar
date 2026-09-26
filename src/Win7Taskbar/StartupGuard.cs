// Win7Taskbar - protezione e diagnostica dell'avvio
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
//
// Perche' esiste questo file
// --------------------------
// Una barra delle applicazioni che muore all'avvio e' peggio di una che non
// parte: l'utente resta senza barra e senza nessuna indicazione. Qui dentro
// stanno tre cose:
//
//   1. un MARCATORE di avanzamento, scritto su disco PRIMA di ogni fase
//      delicata dell'avvio. Se il processo muore - anche in modo non
//      catturabile, come un access violation nel codice nativo o un FailFast
//      del motore di testo di WPF - al riavvio successivo sappiamo ESATTAMENTE
//      in quale fase si e' fermato;
//   2. i gestori globali delle eccezioni (thread UI, AppDomain, task non
//      osservati), che scrivono un rapporto completo invece di far comparire
//      la finestra di crash di Windows;
//   3. la MODALITA' PROVVISORIA: se l'avvio precedente e' morto, o se l'utente
//      passa /safe, importazione tray e registrazione AppBar vengono saltate.
//      La finestra sostitutiva non viene mai mostrata senza area riservata:
//      si ripristina la barra nativa e si esce in modo esplicito.

using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Threading;

namespace Win7Taskbar
{
    /// <summary>Raccoglie lo stato dell'avvio e scrive i rapporti di crash.</summary>
    internal static class StartupGuard
    {
        private const string MarkerFileName = "avvio-in-corso.txt";
        private const int MaxReportFiles = 20;

        private static readonly object Sync = new();
        private static string _stage = "<non avviato>";
        private static bool _handlersInstalled;

        /// <summary>Directory dei rapporti: %APPDATA%\Win7Taskbar\logs.</summary>
        public static string LogDirectory
        {
            get
            {
                string dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Win7Taskbar", "logs");
                return dir;
            }
        }

        private static string MarkerPath => Path.Combine(ConfigDirectory, MarkerFileName);

        private static string ConfigDirectory
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Win7Taskbar");
            }
        }

        /// <summary>Fase in cui ci troviamo ora.</summary>
        public static string CurrentStage
        {
            get { lock (Sync) { return _stage; } }
        }

        /// <summary>
        /// True se l'avvio precedente non e' arrivato in fondo: il programma
        /// e' morto (o e' stato terminato a forza) durante <see cref="PreviousCrashStage"/>.
        /// </summary>
        public static bool PreviousRunCrashed { get; private set; }

        /// <summary>Fase in cui l'avvio precedente si e' interrotto.</summary>
        public static string? PreviousCrashStage { get; private set; }

        /// <summary>
        /// Avvio in modalita' provvisoria: saltate le operazioni piu' rischiose.
        /// Attivata da /safe sulla riga di comando oppure automaticamente dopo
        /// un avvio fallito.
        /// </summary>
        public static bool SafeMode { get; private set; }

        // -----------------------------------------------------------------
        //  Installazione dei gestori globali
        // -----------------------------------------------------------------

        /// <summary>
        /// Aggancia i gestori delle eccezioni non gestite. Va chiamato per
        /// prima cosa in OnStartup, prima di qualunque altra operazione.
        /// </summary>
        public static void Install(Dispatcher dispatcher)
        {
            lock (Sync)
            {
                if (_handlersInstalled)
                {
                    return;
                }
                _handlersInstalled = true;
            }

            // Eccezioni sul thread della UI: sono la stragrande maggioranza.
            dispatcher.UnhandledException += OnDispatcherUnhandledException;

            // Eccezioni su un thread qualsiasi (comprese quelle dentro il
            // codice nativo che risalgono come SEH tradotto).
            AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;

            // Task dimenticati: di norma non farebbero nulla, ma un'eccezione
            // silenziosa qui dentro e' esattamente il tipo di cosa che vogliamo
            // vedere nel rapporto.
            System.Threading.Tasks.TaskScheduler.UnobservedTaskException +=
                OnUnobservedTaskException;
        }

        private static void OnDispatcherUnhandledException(
            object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
        {
            string? report = Report(e.Exception, "thread UI (dispatcher)");

            // Finche' la barra non e' stata creata non c'e' nulla da salvare:
            // meglio fermarsi con un messaggio chiaro che girare a meta'.
            if (!StartupCompleted)
            {
                RestoreNativeTaskbar();
                ShowFatal(e.Exception, report);
                e.Handled = true;
                Environment.Exit(1);
                return;
            }

            // A regime: una eccezione in un gestore del mouse o in un timer
            // non deve portarsi dietro la barra. La scriviamo e andiamo avanti.
            e.Handled = true;
        }

        private static void OnDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
        {
            if (e.ExceptionObject is Exception ex)
            {
                string? report = Report(ex, e.IsTerminating ? "AppDomain (terminante)" : "AppDomain");
                if (e.IsTerminating)
                {
                    RestoreNativeTaskbar();
                    ShowFatal(ex, report);
                }
            }
        }

        private static void OnUnobservedTaskException(
            object? sender,
            System.Threading.Tasks.UnobservedTaskExceptionEventArgs e)
        {
            Report(e.Exception, "task non osservato");
            e.SetObserved();
        }

        // -----------------------------------------------------------------
        //  Marcatura delle fasi
        // -----------------------------------------------------------------

        /// <summary>
        /// Registra l'ingresso in una fase dell'avvio e lo scrive subito su
        /// disco. Costa qualche millisecondo e vale il prezzo: e' l'unico modo
        /// di sapere dove si e' fermato un processo morto senza eccezione
        /// catturabile.
        /// </summary>
        public static void Enter(string stage)
        {
            lock (Sync)
            {
                _stage = stage;
            }

            try
            {
                Directory.CreateDirectory(ConfigDirectory);
                File.WriteAllText(MarkerPath,
                    $"fase={stage}\r\n" +
                    $"istante={DateTime.Now.ToString("o", CultureInfo.InvariantCulture)}\r\n" +
                    $"pid={Environment.ProcessId}\r\n" +
                    $"versione={VersionText}\r\n",
                    Encoding.UTF8);
            }
            catch (Exception)
            {
                // Se non possiamo scrivere il marcatore andiamo avanti comunque.
            }
        }

        /// <summary>L'avvio e' arrivato in fondo: il marcatore viene tolto.</summary>
        public static void Complete()
        {
            StartupCompleted = true;
            RemoveMarker();
        }

        /// <summary>True quando l'avvio si e' completato almeno una volta.</summary>
        public static bool StartupCompleted { get; private set; }

        /// <summary>
        /// Legge il marcatore lasciato da un avvio precedente. Se c'e', quel
        /// processo non ha mai raggiunto <see cref="Complete"/>.
        /// </summary>
        public static void DetectPreviousCrash(string[] args)
        {
            SafeMode = HasArgument(args, "/safe") || HasArgument(args, "-safe");

            try
            {
                if (File.Exists(MarkerPath))
                {
                    PreviousRunCrashed = true;

                    string? markerVersion = null;
                    foreach (string line in File.ReadAllLines(MarkerPath))
                    {
                        if (line.StartsWith("fase=", StringComparison.Ordinal))
                        {
                            PreviousCrashStage = line.Substring("fase=".Length).Trim();
                        }
                        else if (line.StartsWith("versione=", StringComparison.Ordinal))
                        {
                            markerVersion = line.Substring("versione=".Length).Trim();
                        }
                    }

                    // v2.22: la modalita' provvisoria automatica scatta SOLO
                    // se a morire e' stata la STESSA versione: un crash di una
                    // build vecchia (gia' corretta) non deve mutilare quella
                    // nuova, altrimenti l'utente resta con la barra di
                    // sistema visibile e l'AppBar saltata pur avendo una
                    // build sana (regressione segnalata in v2.21).
                    bool sameVersion = markerVersion == null ||
                        string.Equals(markerVersion, VersionText,
                                      StringComparison.Ordinal);

                    if (!SafeMode && sameVersion)
                    {
                        // Il marker ancora presente vuol dire che l'avvio
                        // precedente NON e' arrivato in fondo. Puo' essere
                        // un'eccezione gestita (e allora c'e' il rapporto in
                        // logs/) oppure un fault nativo o un processo ucciso:
                        // in quei due casi il rapporto non esiste, e contare
                        // solo i rapporti lasciava ripartire in modalita'
                        // normale verso lo stesso punto che aveva ucciso il
                        // processo, all'infinito. Il marker basta: un avvio
                        // interrotto merita un avvio prudente, e al lancio
                        // successivo, completandosi, il marker sparisce.
                        SafeMode = true;
                    }
                }
            }
            catch (Exception)
            {
                // Un marcatore illeggibile non deve impedire l'avvio.
            }

            if (HasArgument(args, "/reset"))
            {
                TryResetConfiguration();
            }
        }

        private static bool HasArgument(string[] args, string name)
        {
            foreach (string a in args)
            {
                if (string.Equals(a, name, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        private static int CountReportsForStage(string? stage)
        {
            if (stage == null)
            {
                return 0;
            }

            try
            {
                if (!Directory.Exists(LogDirectory))
                {
                    return 0;
                }

                int found = 0;
                foreach (string file in Directory.GetFiles(LogDirectory, "crash-*.txt"))
                {
                    try
                    {
                        // Basta la prima riga utile: i rapporti sono piccoli.
                        foreach (string line in File.ReadLines(file))
                        {
                            if (line.StartsWith("fase:", StringComparison.Ordinal))
                            {
                                if (line.Contains(stage, StringComparison.Ordinal))
                                {
                                    found++;
                                }
                                break;
                            }
                        }
                    }
                    catch (Exception)
                    {
                        // un rapporto illeggibile non conta
                    }
                }
                return found;
            }
            catch (Exception)
            {
                return 0;
            }
        }

        private static void RemoveMarker()
        {
            try
            {
                if (File.Exists(MarkerPath))
                {
                    File.Delete(MarkerPath);
                }
            }
            catch (Exception)
            {
                // non critico
            }
        }

        private static void TryResetConfiguration()
        {
            try
            {
                string settings = Path.Combine(ConfigDirectory, "settings.json");
                if (File.Exists(settings))
                {
                    File.Delete(settings);
                }
            }
            catch (Exception)
            {
                // non critico
            }
        }

        // -----------------------------------------------------------------
        //  Rapporti
        // -----------------------------------------------------------------

        /// <summary>
        /// Scrive un rapporto di crash completo. Restituisce il percorso del
        /// file, oppure null se non e' stato possibile scriverlo.
        /// </summary>
        public static string? Report(Exception ex, string context)
        {
            try
            {
                Directory.CreateDirectory(LogDirectory);

                string path = Path.Combine(LogDirectory,
                    "crash-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture) + ".txt");

                var sb = new StringBuilder();
                sb.AppendLine("Win7Taskbar - rapporto di errore");
                sb.AppendLine("================================");
                sb.AppendLine($"istante:  {DateTime.Now.ToString("o", CultureInfo.InvariantCulture)}");
                sb.AppendLine($"versione: {VersionText}");
                sb.AppendLine($"fase:     {CurrentStage}");
                sb.AppendLine($"contesto: {context}");
                sb.AppendLine($"sicura:   {(SafeMode ? "si'" : "no")}");
                sb.AppendLine($"pid:      {Environment.ProcessId}");
                sb.AppendLine($"sistema:  {DescribeOs()}");
                sb.AppendLine($"cultura:  {CultureInfo.CurrentCulture.Name}");
                sb.AppendLine();
                sb.AppendLine("Eccezione");
                sb.AppendLine("---------");
                sb.AppendLine(Describe(ex, 0));

                File.WriteAllText(path, sb.ToString(), Encoding.UTF8);
                TrimOldReports();

                Debug.WriteLine($"[StartupGuard] rapporto scritto in {path}");
                return path;
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>Registra un evento non eccezionale nel rapporto di avvio.</summary>
        public static void Note(string message)
        {
            try
            {
                Directory.CreateDirectory(LogDirectory);
                string path = Path.Combine(LogDirectory, "avvio.log");
                File.AppendAllText(path,
                    $"{DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture)}  " +
                    $"[{CurrentStage}] {message}{Environment.NewLine}",
                    Encoding.UTF8);
            }
            catch (Exception)
            {
                // la diagnostica non deve mai diventare essa stessa un problema
            }
        }

        private static string Describe(Exception ex, int depth)
        {
            if (depth > 6)
            {
                return "(annidamento troppo profondo)";
            }

            var sb = new StringBuilder();
            sb.AppendLine($"tipo:     {ex.GetType().FullName}");
            sb.AppendLine($"messaggio:{(ex.Message.Contains('\n') ? Environment.NewLine + "  " : " ")}{ex.Message.Replace("\n", "\n  ")}");

            if (ex is System.Runtime.InteropServices.SEHException seh)
            {
                sb.AppendLine($"HRESULT:  0x{seh.ErrorCode:X8}");
            }

            string? stack = ex.StackTrace;
            sb.AppendLine("stack:");
            sb.AppendLine(string.IsNullOrEmpty(stack) ? "  (nessuno)" : Indent(stack));

            if (ex.InnerException != null)
            {
                sb.AppendLine();
                sb.AppendLine($"--- causa interna ({ex.InnerException.GetType().Name}) ---");
                sb.Append(Describe(ex.InnerException, depth + 1));
            }

            return sb.ToString();
        }

        private static string Indent(string text)
        {
            var sb = new StringBuilder();
            foreach (string line in text.Split('\n'))
            {
                sb.Append("  ").AppendLine(line.TrimEnd('\r'));
            }
            return sb.ToString().TrimEnd();
        }

        private static void TrimOldReports()
        {
            try
            {
                string[] files = Directory.GetFiles(LogDirectory, "crash-*.txt");
                if (files.Length <= MaxReportFiles)
                {
                    return;
                }

                Array.Sort(files, StringComparer.OrdinalIgnoreCase);
                for (int i = 0; i < files.Length - MaxReportFiles; i++)
                {
                    try { File.Delete(files[i]); } catch (Exception) { /* ignora */ }
                }
            }
            catch (Exception)
            {
                // non critico
            }
        }

        private static string VersionText
        {
            get
            {
                try
                {
                    var v = typeof(StartupGuard).Assembly.GetName().Version;
                    return v?.ToString() ?? "(sconosciuta)";
                }
                catch (Exception)
                {
                    return "(sconosciuta)";
                }
            }
        }

        private static string DescribeOs()
        {
            try
            {
                using var key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(
                    @"SOFTWARE\Microsoft\Windows NT\CurrentVersion");
                string name = key?.GetValue("ProductName") as string ?? "Windows";
                string build = key?.GetValue("CurrentBuildNumber") as string ?? "?";
                string display = key?.GetValue("DisplayVersion") as string ?? "";
                return $"{name} {display} (build {build}), {Environment.OSVersion.Version}, " +
                       (Environment.Is64BitOperatingSystem ? "x64" : "x86");
            }
            catch (Exception)
            {
                return Environment.OSVersion.ToString();
            }
        }

        // -----------------------------------------------------------------
        //  Recupero
        // -----------------------------------------------------------------

        /// <summary>
        /// Rimette in mostra la taskbar di Windows. Da chiamare SEMPRE prima di
        /// terminare: senza, l'utente resta con una barra nascosta e nessun
        /// modo ovvio di ripristinarla.
        /// </summary>
        public static void RestoreNativeTaskbar()
        {
            try
            {
                int result = Interop.NativeMethods.W7T_SetNativeTaskbarHidden(0);
                if (result != Interop.W7TResult.Ok)
                {
                    Note("ripristino taskbar nativa incompleto (codice core: " + result + ")");
                }
            }
            catch (Exception)
            {
                // La DLL potrebbe non essere nemmeno caricabile: in quel caso
                // la barra di sistema non e' stata nascosta, quindi non c'e'
                // nulla da ripristinare.
            }
        }

        /// <summary>
        /// Appiattisce la catena InnerException in righe "← Tipo: messaggio".
        ///
        /// XamlParseException e InvalidOperationException sono quasi sempre
        /// involucri: la causa vera ("Type reference cannot find...") vive
        /// nelle inner. Senza queste righe la finestra di errore mostra solo
        /// l'involucro e non si capisce nulla.
        /// </summary>
        public static string FormatInnerCauses(Exception ex, int maxDepth = 4)
        {
            try
            {
                var sb = new StringBuilder();
                Exception? inner = ex.InnerException;
                int depth = 0;
                while (inner != null && depth < maxDepth)
                {
                    string[] parts = (inner.Message ?? "").Split(
                        new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
                    string firstLine = parts.Length > 0 ? parts[0] : "";
                    if (sb.Length > 0)
                    {
                        sb.AppendLine();
                    }
                    sb.Append($"  ← {inner.GetType().Name}: {firstLine}");
                    inner = inner.InnerException;
                    depth++;
                }

                if (inner != null)
                {
                    if (sb.Length > 0)
                    {
                        sb.AppendLine();
                    }
                    sb.Append("  ← ...");
                }

                return sb.ToString();
            }
            catch
            {
                return "";
            }
        }

        private static void ShowFatal(Exception ex, string? reportPath)
        {
            try
            {
                string causes = FormatInnerCauses(ex);
                if (!string.IsNullOrEmpty(causes))
                {
                    causes = "Cause:\n" + causes + "\n\n";
                }

                string text =
                    "Win7Taskbar non e' riuscito ad avviarsi.\n\n" +
                    $"Fase: {CurrentStage}\n" +
                    $"Errore: {ex.GetType().Name}: {ex.Message}\n" +
                    causes + "\n" +
                    (SafeMode
                        ? "L'avvio era gia' in modalita' provvisoria.\n\n"
                        : "Al prossimo avvio il programma entrera' automaticamente in " +
                          "modalita' provvisoria (niente AppBar, niente importazione " +
                          "delle icone da Explorer).\n\n") +
                    (reportPath != null
                        ? $"Rapporto completo:\n{reportPath}\n\nAllegalo alla segnalazione."
                        : "Non e' stato possibile scrivere il rapporto su disco.") +
                    "\n\nLa barra delle applicazioni di Windows e' stata ripristinata.";

                System.Windows.MessageBox.Show(text, "Win7Taskbar - errore di avvio",
                    System.Windows.MessageBoxButton.OK, System.Windows.MessageBoxImage.Error);
            }
            catch (Exception)
            {
                // Se non riesce nemmeno il messaggio, il rapporto su disco resta.
            }
        }
    }
}
