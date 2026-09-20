//
// Backup reversibile delle scelte ImmersiveShell (orologio/batteria/volume).
//
// Prima che Win7Taskbar modifichi per la prima volta uno dei tre valori
//   UseWin32TrayClockExperience / UseWin32BatteryFlyout / EnableMtcUvc
// in HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\ImmersiveShell, qui se ne
// fotografa lo stato originale (esisteva? che DWORD aveva?) sotto la chiave
// NOSTRA HKCU\Software\Win7Taskbar\RegistryBackup. Il backup viene preso UNA
// volta sola e non viene mai sovrascritto: ne' dai riavvii, ne' dagli
// Applica/OK, ne' dai passaggi fra "Windows 7" e "Windows 10/11".
//
// In chiusura pulita (ShutdownTaskbar) ogni valore viene restituito al suo
// stato originale esatto - DWORD originale, oppure valore rimosso se in
// origine non esisteva - e poi il backup corrispondente viene cancellato.
// Guardia di sicurezza: si ripristina un valore SOLO se contiene ancora
// quello che Win7Taskbar ha applicato per ultimo; se l'utente o un altro
// programma l'ha cambiato mentre eravamo attivi, NON lo si sovrascrive: il
// restore si salta e il backup si TIENE.
//
// La batteria e' un caso a parte (OPZIONE B): il gestito non scrive MAI il
// valore live, che resta transitorio in mano al nativo; qui se ne tiene solo
// una copia contabile dell'originale, buttata in chiusura senza toccare il
// live (il restore vero l'ha gia' fatto Stop nativo).
//
// Tutto e' idempotente e fail safe: in caso di errore non si cancella mai il
// backup e non si tocca mai il live con dati incerti.

using System;
using System.Linq;
using Microsoft.Win32;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Backup/ripristino reversibile dei tre valori ImmersiveShell. Vedi
    /// l'intestazione del file per la politica completa.
    /// </summary>
    internal static class ImmersiveShellBackup
    {
        private const string ShellKeyPath =
            @"SOFTWARE\Microsoft\Windows\CurrentVersion\ImmersiveShell";

        private const string BackupRootPath = @"Software\Win7Taskbar\RegistryBackup";

        private const string ClockValueName = "UseWin32TrayClockExperience";
        private const string BatteryValueName = "UseWin32BatteryFlyout";
        private const string VolumeValueName = "EnableMtcUvc";

        /* Valori che il livello gestito SCRIVE davvero. La batteria NON e'
         * in questo insieme (OPZIONE B): il gestito ne fotografa solo
         * l'originale e in chiusura butta la copia senza toccare il live. */
        private static readonly string[] s_managedWrittenValues =
        {
            ClockValueName,
            VolumeValueName,
        };

        /* Campi della sottochiave di backup HKCU\...\RegistryBackup\<nome>:
         *   Existed      DWORD 0/1, marcatore di esistenza del backup;
         *   Original     DWORD originale (solo se esisteva ed era DWORD);
         *   OriginalKind DWORD con RegistryValueKind originale (se esisteva);
         *   LastApplied  DWORD applicata per ultima dal gestito (se mai). */
        private const string FieldExisted = "Existed";
        private const string FieldOriginal = "Original";
        private const string FieldOriginalKind = "OriginalKind";
        private const string FieldLastApplied = "LastApplied";

        private sealed class BackupData
        {
            public bool Existed;
            public int? Original;
            public RegistryValueKind OriginalKind = RegistryValueKind.Unknown;
            public int? LastApplied;
        }

        private static string BackupSubKeyPath(string name) => BackupRootPath + "\\" + name;

        private static bool IsManagedWritten(string name) =>
            s_managedWrittenValues.Any(n => n.Equals(name, StringComparison.OrdinalIgnoreCase));

        /// <summary>
        /// Fotografa lo stato originale di un valore, UNA volta sola. Se il
        /// backup esiste gia' non fa niente e torna true. Torna false solo
        /// se la cattura fallisce: il chiamante NON deve modificare il live.
        /// </summary>
        internal static bool EnsureBackup(string name)
        {
            try
            {
                if (HasBackup(name))
                {
                    return true;   /* gia' catturato: mai piu' toccato */
                }

                bool existed = false;
                int original = 0;
                bool originalIsDword = false;
                RegistryValueKind originalKind = RegistryValueKind.Unknown;
                using (RegistryKey? shell = Registry.CurrentUser.OpenSubKey(ShellKeyPath, writable: false))
                {
                    if (shell != null)
                    {
                        existed = shell.GetValueNames().Any(n =>
                            n.Equals(name, StringComparison.OrdinalIgnoreCase));
                        if (existed)
                        {
                            object? live = shell.GetValue(name);
                            if (live is int dword)
                            {
                                original = dword;
                                originalIsDword = true;
                                originalKind = RegistryValueKind.DWord;
                            }
                            else
                            {
                                try { originalKind = shell.GetValueKind(name); }
                                catch { originalKind = RegistryValueKind.Unknown; }
                            }
                        }
                    }
                }

                using (RegistryKey? sub = Registry.CurrentUser.CreateSubKey(BackupSubKeyPath(name)))
                {
                    if (sub == null)
                    {
                        return false;
                    }
                    sub.SetValue(FieldExisted, existed ? 1 : 0, RegistryValueKind.DWord);
                    if (existed)
                    {
                        sub.SetValue(FieldOriginalKind, (int)originalKind, RegistryValueKind.DWord);
                        if (originalIsDword)
                        {
                            sub.SetValue(FieldOriginal, original, RegistryValueKind.DWord);
                        }
                    }
                }

                if (!HasBackup(name))
                {
                    /* Scrittura parziale (non dovrebbe mai succedere): si
                     * pulisce quel che abbiamo scritto noi - il live non e'
                     * stato toccato, nessun originale e' andato perso - e si
                     * segnala fallimento. */
                    DeleteBackup(name);
                    return false;
                }
                DiagnosticLogger.Write("REGBACKUP", "backup captured for " + name +
                    " (existed=" + (existed ? "1" : "0") + ")");
                return true;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "EnsureBackup " + name);
                return false;
            }
        }

        /// <summary>
        /// Annota l'ultimo valore applicato dal gestito, DOPO una scrittura
        /// riuscita. Serve alla guardia anti-sovrascrittura del restore.
        /// Best effort: se fallisce, il restore salta prudente e il backup
        /// viene tenuto (vedi RestoreOne).
        /// </summary>
        internal static bool RecordApplied(string name, int value)
        {
            try
            {
                if (!HasBackup(name))
                {
                    /* Difensivo: non si registra un'applicazione senza
                     * backup. Il chiamante scrive solo dopo EnsureBackup
                     * riuscito, quindi qui non si dovrebbe mai arrivare. */
                    DiagnosticLogger.Write("REGBACKUP", "RecordApplied without backup for " + name);
                    return false;
                }
                using (RegistryKey? sub = Registry.CurrentUser.CreateSubKey(BackupSubKeyPath(name)))
                {
                    if (sub == null)
                    {
                        return false;
                    }
                    sub.SetValue(FieldLastApplied, value, RegistryValueKind.DWord);
                }
                return true;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "RecordApplied " + name);
                return false;
            }
        }

        /// <summary>
        /// Restituisce un valore al suo stato originale esatto. Torna true
        /// se a fine chiamata non resta piu' nessun backup per il valore
        /// (ripristinato, gia' a posto, o copia contabile buttata); false se
        /// il backup e' stato TENUTO (salto prudente o errore: si ritenta
        /// alla prossima chiusura pulita). Idempotente.
        /// </summary>
        internal static bool RestoreOne(string name)
        {
            try
            {
                if (!HasBackup(name))
                {
                    return true;   /* idempotente: niente backup, niente da fare */
                }
                BackupData? data = ReadBackup(name);
                if (data == null)
                {
                    return false;   /* illeggibile: si tiene tutto, fail safe */
                }

                if (!data.LastApplied.HasValue)
                {
                    if (IsManagedWritten(name))
                    {
                        /* Dovremmo averlo scritto noi ma la registrazione e'
                         * andata persa (RecordApplied fallito): NON si tocca
                         * il live e NON si cancella il backup. Resta tutto
                         * per il prossimo giro. */
                        DiagnosticLogger.Write("REGBACKUP", "restore deferred for " + name +
                            ": last-applied record missing, keeping backup");
                        return false;
                    }
                    /* Batteria: il gestito non la scrive mai (OPZIONE B, il
                     * transitorio e' del nativo e il suo restore e' gia'
                     * avvenuto in Stop). La copia di backup e' solo
                     * contabile: si butta senza toccare il valore live. */
                    DiagnosticLogger.Write("REGBACKUP",
                        "dropping battery backup copy for " + name + " (live untouched)");
                    DeleteBackup(name);
                    return !HasBackup(name);
                }

                /* Completamento senza scritture: se il live e' GIA' nello
                 * stato originale non c'e' niente da disfare, si butta solo
                 * il backup. Nessun dato esterno viene toccato. */
                if (LiveMatchesOriginal(name, data))
                {
                    DiagnosticLogger.Write("REGBACKUP",
                        "live already at original state for " + name + ", dropping backup");
                    DeleteBackup(name);
                    return !HasBackup(name);
                }

                /* Guardia anti-sovrascrittura: il live deve essere
                 * ESATTAMENTE quello che abbiamo applicato noi per ultimi.
                 * Se l'utente o un'altra applicazione l'ha cambiato (o
                 * cancellato) mentre eravamo attivi, NON lo sovrascriviamo:
                 * si salta il restore e si TIENE il backup. */
                int? live = ReadLiveDword(name);
                if (!live.HasValue || live.Value != data.LastApplied.Value)
                {
                    DiagnosticLogger.Write("REGBACKUP", "restore skipped for " + name +
                        ": live value changed externally, keeping backup");
                    return false;
                }

                /* Il live e' nostro: si ripristina lo stato originale esatto. */
                bool restored;
                string detail;
                if (!data.Existed)
                {
                    /* Non c'era: si rimuove del tutto, non si mette 0. */
                    restored = DeleteLiveValue(name);
                    detail = " (removed)";
                }
                else if (data.OriginalKind == RegistryValueKind.DWord && data.Original.HasValue)
                {
                    int original = data.Original.Value;
                    restored = WriteLiveValue(name, original);
                    detail = " (" + original + ")";
                }
                else
                {
                    /* Originale non-DWORD (irraggiungibile in pratica: sono
                     * DWORD documentate): non lo sappiamo ripristinare
                     * fedelmente, quindi non tocchiamo niente e teniamo il
                     * backup. */
                    DiagnosticLogger.Write("REGBACKUP", "restore skipped for " + name +
                        ": non-DWORD original cannot be restored faithfully");
                    return false;
                }

                if (!restored)
                {
                    return false;   /* si ritenta alla prossima chiusura pulita */
                }
                DiagnosticLogger.Write("REGBACKUP", "restored " + name + " to original" + detail);
                DeleteBackup(name);
                return !HasBackup(name);
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "RestoreOne " + name);
                return false;
            }
        }

        /// <summary>
        /// Restore di tutti e tre i valori, uno alla volta e isolati: il
        /// fallimento di uno non blocca gli altri. Torna una riga di riepilogo
        /// ("ok" = backup risolto, "kept" = backup tenuto, vedi RestoreOne).
        /// Idempotente e sicuro da chiamare piu' volte.
        /// </summary>
        internal static string RestoreAll()
        {
            string clock = RestoreOne(ClockValueName) ? "ok" : "kept";
            string battery = RestoreOne(BatteryValueName) ? "ok" : "kept";
            string volume = RestoreOne(VolumeValueName) ? "ok" : "kept";
            CleanupRootIfEmpty();
            string summary = "clock=" + clock + " battery=" + battery + " volume=" + volume;
            DiagnosticLogger.Write("REGBACKUP", "shutdown restore: " + summary);
            return summary;
        }

        private static bool HasBackup(string name)
        {
            try
            {
                using RegistryKey? sub =
                    Registry.CurrentUser.OpenSubKey(BackupSubKeyPath(name), writable: false);
                /* Il backup esiste solo se c'e' il marcatore Existed: una
                 * sottochiave vuota (crash fra CreateSubKey e SetValue) NON
                 * conta e verra' ricatturata. */
                return sub?.GetValue(FieldExisted) is int existed && (existed == 0 || existed == 1);
            }
            catch
            {
                return false;
            }
        }

        private static BackupData? ReadBackup(string name)
        {
            try
            {
                using RegistryKey? sub =
                    Registry.CurrentUser.OpenSubKey(BackupSubKeyPath(name), writable: false);
                if (sub == null)
                {
                    return null;
                }
                if (sub.GetValue(FieldExisted) is not int existed || (existed != 0 && existed != 1))
                {
                    return null;
                }
                var data = new BackupData { Existed = existed == 1 };
                if (sub.GetValue(FieldOriginalKind) is int kind)
                {
                    data.OriginalKind = (RegistryValueKind)kind;
                }
                if (sub.GetValue(FieldOriginal) is int original)
                {
                    data.Original = original;
                }
                if (sub.GetValue(FieldLastApplied) is int applied)
                {
                    data.LastApplied = applied;
                }
                return data;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "ReadBackup " + name);
                return null;
            }
        }

        private static void DeleteBackup(string name)
        {
            try
            {
                using RegistryKey? root = Registry.CurrentUser.OpenSubKey(BackupRootPath, writable: true);
                root?.DeleteSubKeyTree(name, throwOnMissingSubKey: false);
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "DeleteBackup " + name);
            }
        }

        private static void CleanupRootIfEmpty()
        {
            /* Impronta zero: se non resta nessun backup si rimuove anche la
             * radice. Best effort, il fallimento e' innocuo. */
            try
            {
                using (RegistryKey? root = Registry.CurrentUser.OpenSubKey(BackupRootPath, writable: false))
                {
                    if (root == null || root.SubKeyCount != 0 || root.ValueCount != 0)
                    {
                        return;
                    }
                }
                Registry.CurrentUser.DeleteSubKey(BackupRootPath, throwOnMissingSubKey: false);
            }
            catch { }
        }

        private static bool LiveMatchesOriginal(string name, BackupData data)
        {
            try
            {
                using RegistryKey? shell = Registry.CurrentUser.OpenSubKey(ShellKeyPath, writable: false);
                if (shell == null)
                {
                    return !data.Existed;
                }
                bool liveExists = shell.GetValueNames().Any(n =>
                    n.Equals(name, StringComparison.OrdinalIgnoreCase));
                if (!data.Existed)
                {
                    return !liveExists;
                }
                return liveExists
                    && data.OriginalKind == RegistryValueKind.DWord
                    && data.Original.HasValue
                    && shell.GetValue(name) is int live
                    && live == data.Original.Value;
            }
            catch
            {
                /* Dubbio: si segue la strada prudente (guardia/restore). */
                return false;
            }
        }

        private static int? ReadLiveDword(string name)
        {
            try
            {
                using RegistryKey? shell = Registry.CurrentUser.OpenSubKey(ShellKeyPath, writable: false);
                if (shell?.GetValue(name) is int live)
                {
                    return live;
                }
                return null;
            }
            catch
            {
                /* Dubbio = guardia fallita = skip prudente. */
                return null;
            }
        }

        private static bool WriteLiveValue(string name, int value)
        {
            try
            {
                using (RegistryKey? shell = Registry.CurrentUser.CreateSubKey(ShellKeyPath))
                {
                    if (shell == null)
                    {
                        return false;
                    }
                    shell.SetValue(name, value, RegistryValueKind.DWord);
                }
                /* Verifica in lettura: il backup si cancella solo se il live
                 * e' davvero tornato all'originale. */
                return ReadLiveDword(name) == value;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "WriteLiveValue " + name);
                return false;
            }
        }

        private static bool DeleteLiveValue(string name)
        {
            try
            {
                using (RegistryKey? shell = Registry.CurrentUser.OpenSubKey(ShellKeyPath, writable: true))
                {
                    /* Idempotente: se non c'e' gia' piu' e' come averlo rimosso. */
                    shell?.DeleteValue(name, throwOnMissingValue: false);
                }
                using (RegistryKey? shell = Registry.CurrentUser.OpenSubKey(ShellKeyPath, writable: false))
                {
                    return shell == null || !shell.GetValueNames().Any(n =>
                        n.Equals(name, StringComparison.OrdinalIgnoreCase));
                }
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("REGBACKUP", ex, "DeleteLiveValue " + name);
                return false;
            }
        }
    }
}
