// Win7Taskbar - caricamento autosufficiente del core nativo
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
// ============================================================================
// PERCHE' QUESTO FILE ESISTE
//
// Il frontend gestito raggiunge il core nativo con i DllImport di
// NativeMethods ("Win7TaskbarCore.dll"). Il loader di Windows risolve quel
// nome con l'ordine di ricerca standard, la cui prima voce e' la cartella
// dell'eseguibile: se la DLL non e' li', ogni avvio si ferma con
// "Win7TaskbarCore.dll non e' stata trovata". Modi reali in cui il file
// sparisce anche da un pacchetto completo:
//
//   * lo ZIP viene estratto solo in parte, oppure l'utente copia/sposta il
//     solo .exe (il classico "aggiornamento" copiando un file solo);
//   * l'eseguibile viene lancio DENTRO il visualizzatore dello ZIP: Windows
//     estrae in una cartella temporanea soltanto il file lanciato, la DLL
//     non compare mai;
//   * un antivirus mette in quarantena o tronca la DLL nativa (un processo
//     che legge la tray di un altro processo e' un bersaglio tipico);
//
// Lo stesso core nativo viene INCORPORATO nell'assembly gestito in fase di
// compilazione (vedere Win7Taskbar.csproj: gli EmbeddedResource presi da
// dist/, che e' un file versionato nel repository). L'applicazione puo'
// quindi ripararsi da sola:
//
//   1. la copia accanto all'eseguibile resta in uso quando e'
//      byte-identica a quella incorporata (il caso normale del pacchetto
//      completo: nessuna scrittura, nessun cambio di comportamento);
//   2. quando e' assente, diversa o non caricabile, la copia incorporata
//      viene riscritta: accanto all'eseguibile se la cartella e'
//      scrivibile, altrimenti in %LOCALAPPDATA%\Win7Taskbar\core;
//   3. il file ripristinato viene caricato ESPLICITAMENTE con il percorso
//      completo PRIMA del primo P/Invoke. Un modulo gia' caricato nel
//      processo risponde per nome a tutte le successive risoluzioni di
//      DllImport("Win7TaskbarCore.dll"), quindi la cartella di ripiego
//      funziona pur non essendo nell'ordine di ricerca del loader. Lo
//      stesso trucco rende visibile W7TInject.dll al codice nativo, che la
//      carica per nome (LoadLibraryW(L"W7TInject.dll") in TrayService).
//
// Tutto qui e' best-effort e mai fatale: se anche il ripiego fallisce, lo
// stato describe il tentativo e NativeBridge mostra il messaggio finale con
// le indicazioni per l'utente, esattamente come prima di questo modulo.
// ============================================================================

using System;
using System.IO;
using System.Runtime.InteropServices;
using Win7Taskbar.Utilities;

namespace Win7Taskbar.Interop
{
    /// <summary>
    /// Esito del bootstrap del core nativo: che percorso e' stato caricato,
    /// se e' stato necessario un riparo e che cosa e' andato storto.
    /// </summary>
    internal sealed class NativeCoreStatus
    {
        /// <summary>True quando una DLL e' stata caricata nel processo.</summary>
        public bool Loaded { get; internal set; }

        /// <summary>File effettivamente caricato (percorso completo).</summary>
        public string? LoadedPath { get; internal set; }

        /// <summary>"pacchetto" (copia gia' accanto all'exe) oppure
        /// "riparata" (copia incorporata riscritta dall'app).</summary>
        public string Source { get; internal set; } = "non caricato";

        /// <summary>Vero se la copia accanto all'exe e' stata riscritta.</summary>
        public bool Repaired { get; internal set; }

        /// <summary>Nota diagnostica: perche' e' servito il riparo.</summary>
        public string? RepairReason { get; internal set; }

        /// <summary>Percorso della copia incorporata, se esisteva.</summary>
        public bool HasEmbeddedCopy { get; internal set; }

        /// <summary>Descrizione dell'ultimo fallimento, se il caricamento
        /// non e' riuscito nemmeno col ripiego.</summary>
        public string? Error { get; internal set; }
    }

    /// <summary>
    /// Bootstrap del core nativo: garantisce che una Win7TaskbarCore.dll
    /// compatibile con QUESTO eseguibile sia caricata nel processo prima del
    /// primo P/Invoke, ripristinandola dalla copia incorporata quando serve.
    /// Parte dal module initializer di questo assembly (il primo istante in
    /// cui gira codice gestito); le chiamate successive, come quella di
    /// <see cref="NativeBridge.TryInitialize"/>, sono riprese a vuoto.
    /// </summary>
    internal static class NativeCore
    {
        public const string CoreFileName = "Win7TaskbarCore.dll";
        public const string InjectFileName = "W7TInject.dll";

        // Nomi delle risorse incorporate (LogicalName nel csproj). Devono
        // restare allineati: la verifica del pacchetto in publish.ps1 cerca
        // esattamente il nome della risorsa del core dentro Win7Taskbar.dll.
        private const string EmbeddedCoreName = "Win7Taskbar.Win7TaskbarCore.dll";
        private const string EmbeddedInjectName = "Win7Taskbar.W7TInject.dll";

        // Cartella di ripiego quando quella dell'eseguibile non e' scrivibile
        // (installazione in Program Files, cartella protetta, antivirus...).
        private static string FallbackDirectory => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Win7Taskbar", "core");

        /// <summary>Esito dell'ultimo bootstrap (null se non ancora eseguito).</summary>
        public static NativeCoreStatus? LastStatus { get; private set; }

        private static readonly object Sync = new();

        // Impostata a true quando il bootstrap e' gia' girato in questo
        // processo, riuscito o fallito: non si riprova (un secondo tentativo
        // non puo' migliorare nulla e le scritture su disco costano).
        private static bool _ran;

        // -----------------------------------------------------------------
        //  API
        // -----------------------------------------------------------------

        /// <summary>
        /// Punto di ingresso piu' precoce possibile: il runtime esegue un
        /// module initializer PRIMA di qualunque codice dell'assembly,
        /// compreso il costruttore statico di App, che e' il primo tocco al
        /// core (NativeLocalization per la lingua). Cosi' anche quel
        /// percorso - e qualunque altro che in futuro tocchi il nativo -
        /// trova la DLL gia' caricata, con la lingua impostata correttamente
        /// al primo avvio dopo un ripristino. EnsureLoaded e' idempotente e
        /// non lancia mai: il modulo parte in ogni caso.
        /// </summary>
        [System.Runtime.CompilerServices.ModuleInitializer]
        internal static void BootstrapAtModuleLoad()
        {
            EnsureLoaded();
        }

        /// <summary>
        /// Idempotente e mai lanciante: alla fine, <see cref="LastStatus"/>
        /// dice se il core e' caricato e da dove.
        /// </summary>
        public static NativeCoreStatus EnsureLoaded()
        {
            lock (Sync)
            {
                if (_ran && LastStatus != null)
                {
                    return LastStatus;
                }
                _ran = true;

                var status = new NativeCoreStatus();
                LastStatus = status;

                try
                {
                    Run(status);
                }
                catch (Exception ex)
                {
                    // Il bootstrap non deve MAI diventare la causa di un
                    // avvio fallito: il percorso tradizionale (loader che
                    // cerca nella cartella dell'exe) resta disponibile.
                    status.Error = ex.GetType().Name + ": " + ex.Message;
                    StartupGuard.Note("core-nativo: bootstrap fallito: " + status.Error);
                }

                return status;
            }
        }

        /// <summary>
        /// Testo per il messaggio d'errore finale: racconta che cosa ha
        /// provato a fare il ripiego automatico, cosi' chi legge sa se la
        /// DLL e' davvero assente dal disco o se il caricamento e' stato
        /// bloccato (antivirus). Stringa vuota se non c'e' nulla da dire.
        /// </summary>
        public static string DescribeRecoveryAttempt()
        {
            var status = LastStatus;
            if (status == null)
            {
                return string.Empty;
            }

            try
            {
                var sb = new System.Text.StringBuilder();
                if (!status.HasEmbeddedCopy)
                {
                    sb.AppendLine("Questo eseguibile non contiene la copia interna del core: " +
                                  "riestrai il pacchetto completo (tutti i file dello ZIP).");
                }
                else if (status.Loaded)
                {
                    sb.AppendLine("Copia interna del core ripristinata in: " + status.LoadedPath);
                }
                else
                {
                    sb.AppendLine("Il ripristino automatico della copia interna non e' riuscito" +
                                  (status.Error != null ? " (" + status.Error + ")." : "."));
                    sb.AppendLine("Cartella di ripiego provata: " + FallbackDirectory);
                    sb.AppendLine("Un antivirus puo' aver bloccato la DLL: aggiungi Win7Taskbar " +
                                  "alle esclusioni e riestrai il pacchetto.");
                }
                return sb.ToString().TrimEnd();
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }

        // -----------------------------------------------------------------
        //  Sequenza di bootstrap
        // -----------------------------------------------------------------

        private static void Run(NativeCoreStatus status)
        {
            string exeDirectory = AppDomain.CurrentDomain.BaseDirectory;
            string corePath = Path.Combine(exeDirectory, CoreFileName);

            byte[]? embeddedCore = ReadEmbeddedResource(EmbeddedCoreName);
            status.HasEmbeddedCopy = embeddedCore != null;

            // Interruttore di servizio per chi sviluppa/debugga il core: con
            // W7T_PREFER_LOCAL_CORE=1 la copia accanto all'exe vince sempre,
            // anche se differisce da quella incorporata.
            bool preferLocal = string.Equals(
                Environment.GetEnvironmentVariable("W7T_PREFER_LOCAL_CORE"), "1",
                StringComparison.Ordinal);

            if (embeddedCore == null)
            {
                // Compilazione di sviluppo senza la risorsa incorporata:
                // vale solo il file accanto all'eseguibile, come sempre.
                int err = 0;
                if (File.Exists(corePath) && TryPreload(corePath, out err))
                {
                    status.Loaded = true;
                    status.LoadedPath = corePath;
                    status.Source = "pacchetto";
                    StartupGuard.Note("core-nativo: caricata dalla cartella dell'exe (nessuna copia incorporata)");
                }
                else
                {
                    status.Error = err != 0
                        ? $"LoadLibrary fallita ({DescribeLoadError(err)})"
                        : "file assente e nessuna copia incorporata";
                }

                TryHealInject(exeDirectory, preferLocal);
                return;
            }

            bool looseExists = File.Exists(corePath);
            bool looseMatches = looseExists && !preferLocal && FilesEqual(corePath, embeddedCore);

            if (looseMatches)
            {
                // Il caso buono di sempre: pacchetto completo, nessun lavoro.
                if (TryPreload(corePath, out int err))
                {
                    status.Loaded = true;
                    status.LoadedPath = corePath;
                    status.Source = "pacchetto";
                    // Il modulo opzionale segue lo stesso trattamento anche
                    // qui: un pacchetto puo' aver perso solo W7TInject.dll.
                    TryHealInject(exeDirectory, preferLocal);
                    return;
                }

                // Raro: il file c'e' ed e' identico ma il loader lo rifiuta
                // (blocco antivirus in lettura, permessi). Si passa al
                // ripiego come se il file non ci fosse.
                status.RepairReason = $"caricamento rifiutato ({DescribeLoadError(err)})";
                StartupGuard.Note("core-nativo: " + status.RepairReason + ", attivo il ripiego");
            }
            else if (looseExists)
            {
                // Il file accanto all'exe NON viene da questo pacchetto:
                // DLL di una versione vecchia, aggiornamento parziale,
                // oppure file sostituito/troncato da un antivirus. La
                // versione incorporata e' quella compilata CON questo
                // eseguibile: e' l'unica accoppiata garantita.
                status.RepairReason = preferLocal
                    ? "W7T_PREFER_LOCAL_CORE=1 rispettato: uso la copia dell'exe"
                    : "la DLL accanto all'exe non corrisponde alla copia incorporata (versione diversa o file alterato)";
                StartupGuard.Note("core-nativo: " + status.RepairReason);

                if (preferLocal)
                {
                    if (TryPreload(corePath, out int err))
                    {
                        status.Loaded = true;
                        status.LoadedPath = corePath;
                        status.Source = "pacchetto";
                        TryHealInject(exeDirectory, preferLocal);
                        return;
                    }
                    status.RepairReason += $"; LoadLibrary fallita ({DescribeLoadError(err)})";
                }
            }
            else
            {
                status.RepairReason = "DLL assente dalla cartella dell'eseguibile " +
                                      "(estrazione parziale dello ZIP, file spostato o in quarantena)";
                StartupGuard.Note("core-nativo: " + status.RepairReason);
            }

            // Ripiego: porta la copia incorporata su disco e caricane il
            // percorso completo. Prima prova la cartella dell'eseguibile
            // (ripristina il layout atteso), poi quella utente.
            if (TryLoadFromEmbedded(status, embeddedCore, exeDirectory) ||
                TryLoadFromEmbedded(status, embeddedCore, FallbackDirectory))
            {
                status.Loaded = true;
                status.Source = "riparata";
                status.Repaired = true;
                StartupGuard.Note("core-nativo: copia incorporata ripristinata e caricata da " +
                                  status.LoadedPath);
            }

            // Il modulo opzionale si cura da solo, senza mai bloccare.
            TryHealInject(exeDirectory, preferLocal);
        }

        /// <summary>
        /// Scrive la copia incorporata in <paramref name="directory"/> e la
        /// carica. Restituisce false senza eccezioni se la cartella non e'
        /// scrivibile o il caricamento fallisce.
        /// </summary>
        private static bool TryLoadFromEmbedded(NativeCoreStatus status, byte[] embedded,
                                                string directory)
        {
            try
            {
                Directory.CreateDirectory(directory);
                string target = Path.Combine(directory, CoreFileName);

                // Scrittura atomica: file temporaneo + rename. Un antivirus
                // che scandaggia il file a meta' scrittura vedrebbe un DLL
                // incompleto (e lo segnala); col rename e' sempre intero.
                string temp = Path.Combine(directory,
                    CoreFileName + ".tmp-" + Guid.NewGuid().ToString("N").Substring(0, 8));
                try
                {
                    File.WriteAllBytes(temp, embedded);
                    File.Move(temp, target, overwrite: true);
                }
                catch
                {
                    TryDelete(temp);
                    throw;
                }

                if (TryPreload(target, out int err))
                {
                    status.LoadedPath = target;
                    status.Error = null;
                    return true;
                }

                status.Error = $"LoadLibrary fallita su {target} ({DescribeLoadError(err)})";
                StartupGuard.Note("core-nativo: " + status.Error);
                return false;
            }
            catch (Exception ex)
            {
                status.Error = $"scrittura in {directory} non riuscita: {ex.Message}";
                StartupGuard.Note("core-nativo: " + status.Error);
                return false;
            }
        }

        /// <summary>
        /// Riparo del modulo opzionale W7TInject.dll (flyout orologio
        /// congelata): il codice nativo lo carica col solo nome, quindi
        /// basta che un modulo con quel nome sia gia' caricato nel processo.
        /// Nessun fallimento qui puo' fermare l'avvio: la funzionalita'
        /// mancante e' stata sempre degradata dal core.
        /// </summary>
        private static void TryHealInject(string exeDirectory, bool preferLocal)
        {
            try
            {
                byte[]? embedded = ReadEmbeddedResource(EmbeddedInjectName);
                if (embedded == null)
                {
                    return; // pacchetto che non la includeva: com'era prima
                }

                string exePath = Path.Combine(exeDirectory, InjectFileName);
                bool exeOk = File.Exists(exePath) &&
                             (preferLocal || FilesEqual(exePath, embedded));

                if (exeOk)
                {
                    TryPreload(exePath, out _);
                    return;
                }

                // Manca o differisce: riscrivila accanto all'exe, altrimenti
                // usa la cartella di ripiego e pre-caricala da li'.
                if (WriteAtomic(exeDirectory, InjectFileName, embedded) &&
                    TryPreload(exePath, out _))
                {
                    StartupGuard.Note("core-nativo: W7TInject.dll ripristinata accanto all'eseguibile");
                    return;
                }

                string fallbackPath = Path.Combine(FallbackDirectory, InjectFileName);
                if (WriteAtomic(FallbackDirectory, InjectFileName, embedded) &&
                    TryPreload(fallbackPath, out _))
                {
                    StartupGuard.Note("core-nativo: W7TInject.dll ripristinata in " + FallbackDirectory);
                }
            }
            catch (Exception ex)
            {
                StartupGuard.Note("core-nativo: riparo W7TInject non riuscito: " + ex.Message);
            }
        }

        // -----------------------------------------------------------------
        //  Primitive
        // -----------------------------------------------------------------

        /// <summary>
        /// Carica la DLL con percorso completo. Il modulo resta nel processo
        /// per tutta la sua vita, come accadrebbe con il caricamento del
        /// loader; da questo momento ogni DllImport col nome del file
        /// risolve su questo modulo, quale che sia la sua cartella.
        /// </summary>
        private static bool TryPreload(string fullPath, out int win32Error)
        {
            IntPtr module = LoadLibraryW(fullPath);
            win32Error = module == IntPtr.Zero ? Marshal.GetLastWin32Error() : 0;
            return module != IntPtr.Zero;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string fileName);

        private static byte[]? ReadEmbeddedResource(string name)
        {
            try
            {
                Stream? stream = typeof(NativeCore).Assembly.GetManifestResourceStream(name);
                if (stream == null)
                {
                    return null;
                }

                using (stream)
                {
                    var bytes = new byte[stream.Length];
                    int read = 0;
                    while (read < bytes.Length)
                    {
                        int n = stream.Read(bytes, read, bytes.Length - read);
                        if (n <= 0)
                        {
                            return null;
                        }
                        read += n;
                    }
                    return bytes;
                }
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>Confronto per contenuto: lunghezza diversa basta a
        /// decidere, altrimenti confronto byte a byte (la DLL e' ~1,5 MB,
        /// l'operazione costa pochi millisecondi una volta per avvio).</summary>
        private static bool FilesEqual(string path, byte[] embedded)
        {
            try
            {
                using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                    FileShare.Read | FileShare.Delete);
                if (stream.Length != embedded.Length)
                {
                    return false;
                }

                var buffer = new byte[64 * 1024];
                int offset = 0;
                int read;
                while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
                {
                    for (int i = 0; i < read; i++)
                    {
                        if (buffer[i] != embedded[offset + i])
                        {
                            return false;
                        }
                    }
                    offset += read;
                }
                return offset == embedded.Length;
            }
            catch (Exception)
            {
                // File illeggibile (bloccato, permessi): trattandolo come
                // "diverso" si attiva il ripiego, che e' la direzione giusta.
                return false;
            }
        }

        private static bool WriteAtomic(string directory, string fileName, byte[] content)
        {
            try
            {
                Directory.CreateDirectory(directory);
                string target = Path.Combine(directory, fileName);
                string temp = Path.Combine(directory,
                    fileName + ".tmp-" + Guid.NewGuid().ToString("N").Substring(0, 8));
                try
                {
                    File.WriteAllBytes(temp, content);
                    File.Move(temp, target, overwrite: true);
                }
                catch
                {
                    TryDelete(temp);
                    throw;
                }
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static void TryDelete(string path)
        {
            try { File.Delete(path); } catch { /* non critico */ }
        }

        /// <summary>Traduce i codici Win32 del loader in parole: il numero
        /// da solo nel log non dice se e' un antivirus (5), un file troncato
        /// (193) o una dipendenza mancante (126).</summary>
        private static string DescribeLoadError(int error) => error switch
        {
            5 => "accesso negato",
            32 => "file in uso da un altro processo",
            126 => "modulo o dipendenza non trovata",
            193 => "file non valido per questo processo (x86/x64 o danneggiato)",
            _ => $"codice {error}"
        };
    }
}
