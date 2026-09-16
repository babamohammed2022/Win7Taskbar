// Win7Taskbar - applicazione WPF
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

using System;
using System.Collections.Generic;
using System.Threading;
using System.Windows;
using System.Globalization;
using System.Windows.Markup;
using System.Windows.Media;
using Win7Taskbar.Interop;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    public partial class App : Application
    {
        private const string SingleInstanceMutexName = @"Local\Win7Taskbar.SingleInstance";

        private Mutex? _instanceMutex;
        private NativeBridge? _bridge;
        private TaskbarWindow? _taskbar;

        internal NativeBridge? Bridge => _bridge;

        protected override void OnStartup(StartupEventArgs e)
        {
            // PRIMA di tutto: i gestori globali e il marcatore di fase. Se
            // qualcosa qui sotto muore, deve lasciare una traccia.
            StartupGuard.Install(Dispatcher);
            StartupGuard.DetectPreviousCrash(e.Args);
            StartupGuard.Enter("avvio");

            if (StartupGuard.PreviousRunCrashed)
            {
                StartupGuard.Note(
                    "l'avvio precedente si e' interrotto nella fase '" +
                    StartupGuard.PreviousCrashStage + "' -> modalita' provvisoria: " +
                    StartupGuard.SafeMode);
            }

            try
            {
                base.OnStartup(e);

                // I dizionari vanno composti in codice: Base.xaml deve stare DENTRO
                // i MergedDictionaries di Windows7.xaml, altrimenti i 16 stili con
                // BasedOn su se stessi non trovano la propria base. Vedi ThemeLoader.
                if (Environment.GetEnvironmentVariable("W7T_TRACE_BINDINGS") == "1")
                {
                    System.Diagnostics.PresentationTraceSources.Refresh();
                    System.Diagnostics.PresentationTraceSources.DataBindingSource.Listeners
                        .Add(new System.Diagnostics.ConsoleTraceListener());
                    System.Diagnostics.PresentationTraceSources.DataBindingSource.Switch.Level =
                        System.Diagnostics.SourceLevels.Warning;
                }

                // WPF ignora la cultura di sistema: ogni StringFormat dei binding
                // usa "en-US" salvo che non si sovrascriva FrameworkElement.Language.
                // Senza questa riga l'orologio mostra "9:34 PM" e "9/8/2026" anche
                // su Windows italiano, invece di "21:34" e "08/09/2026".
                StartupGuard.Enter("cultura");
                FrameworkElement.LanguageProperty.OverrideMetadata(
                    typeof(FrameworkElement),
                    new FrameworkPropertyMetadata(
                        XmlLanguage.GetLanguage(CultureInfo.CurrentCulture.IetfLanguageTag)));

                StartupGuard.Enter("tema");
                Resources = ThemeLoader.Build();
                StartupGuard.Enter("lingua");
                // English: Apply language dictionary (Italian/English) after theme
                // Italiano: Applica dizionario lingua dopo il tema
                LocalizationManager.ApplyLanguage(RetroBar.Utilities.Settings.Instance.Language);

                // v2.52: qui veniva registrata la risorsa "Win7CloseGlyph"
                // (la X dell'anteprima). Da quando la X usa le immagini dei
                // pulsanti (Utilities/PreviewAssets.cs) NESSUNA parte del
                // programma legge piu' quella chiave: il blocco e' stato
                // tolto insieme all'icona che registrava.

                // Se il font richiesto dal tema non esiste su questo sistema, WPF
                // non ripiega da solo: il motore di shaping aborta il processo con
                // FailFast alla prima misura di testo. Sostituiamo la chiave con un
                // font realmente installato prima di creare qualunque finestra.
                StartupGuard.Enter("font");
                EnsureUsableFontFamily();

                // Due taskbar contemporanee litigherebbero su AppBar e tray.
                StartupGuard.Enter("istanza-unica");
                _instanceMutex = new Mutex(true, SingleInstanceMutexName, out bool isFirstInstance);
                if (!isFirstInstance)
                {
                    StartupGuard.Complete();
                    MessageBox.Show(
                        "Win7Taskbar è già in esecuzione.",
                        "Win7Taskbar",
                        MessageBoxButton.OK,
                        MessageBoxImage.Information);
                    Shutdown();
                    return;
                }

                StartupGuard.Enter("core-nativo");
                _bridge = new NativeBridge();

                if (!_bridge.TryInitialize(out string? bridgeError))
                {
                    StartupGuard.Complete();
                    StartupGuard.Note("core nativo non inizializzabile: " + bridgeError);
                    MessageBox.Show(
                        bridgeError ??
                        "Impossibile inizializzare Win7TaskbarCore.dll.\n\n" +
                        "Verificare che la DLL nativa si trovi accanto all'eseguibile " +
                        "e che l'architettura corrisponda (x64 con x64).",
                        "Win7Taskbar",
                        MessageBoxButton.OK,
                        MessageBoxImage.Error);
                    Shutdown();
                    return;
                }

                StartupGuard.Enter("finestra");
                _taskbar = new TaskbarWindow(_bridge);
                _taskbar.Show();

                StartupGuard.Enter("pronto");
                StartupGuard.Complete();
                StartupGuard.Note("avvio completato" +
                                  (StartupGuard.SafeMode ? " in modalita' provvisoria" : ""));
            }
            catch (Exception ex)
            {
                // Qualunque fase puo' fallire: scriviamo il rapporto, rimettiamo
                // la barra di Windows e ci fermiamo con un messaggio chiaro
                // invece di morire in silenzio.
                string? report = StartupGuard.Report(ex, "OnStartup");
                StartupGuard.RestoreNativeTaskbar();

                MessageBox.Show(
                    $"Win7Taskbar non è riuscito ad avviarsi.\n\n" +
                    $"Fase: {StartupGuard.CurrentStage}\n" +
                    $"Errore: {ex.GetType().Name}: {ex.Message}\n\n" +
                    (report != null
                        ? $"Rapporto completo (da allegare alla segnalazione):\n{report}\n\n"
                        : "") +
                    "La barra delle applicazioni di Windows è stata ripristinata.\n" +
                    "Al prossimo avvio il programma entrerà in modalità provvisoria.",
                    "Win7Taskbar - errore di avvio",
                    MessageBoxButton.OK,
                    MessageBoxImage.Error);

                Shutdown();
            }
        }

        protected override void OnExit(ExitEventArgs e)
        {
            // L'ordine conta: prima la finestra (che deregistra l'AppBar e
            // ripristina la taskbar nativa), poi il core.
            _taskbar?.ShutdownTaskbar();
            _bridge?.Dispose();

            _instanceMutex?.ReleaseMutex();
            _instanceMutex?.Dispose();

            base.OnExit(e);

            // v2.28: chiusura GARANTITA: dopo la pulizia ordinata, forza la
            // fine del processo anche se qualche thread nativo/agganciato
            // non fosse background (prima il processo restava vivo dopo la
            // chiusura della barra).
            Environment.Exit(e.ApplicationExitCode);
        }

        /// <summary>
        /// Garantisce che GlobalFontFamily punti a un font realmente presente.
        ///
        /// Il tema chiede "Segoe UI". Se manca (Windows N/LTSC senza i font
        /// opzionali, Wine, immagini server) WPF non ripiega su un font di
        /// sistema: il layer di shaping chiama Environment.FailFast e il
        /// processo muore alla prima misura di testo, senza eccezione
        /// catturabile. Qui verifichiamo la disponibilita' in anticipo e, se
        /// serve, ripieghiamo sul primo font utilizzabile.
        /// </summary>
        private void EnsureUsableFontFamily()
        {
            try
            {
                var candidates = new List<string>();

                if (Resources["GlobalFontFamily"] is FontFamily current)
                {
                    candidates.Add(current.Source);
                }

                // Ordine di preferenza: l'aspetto Win7, poi metriche compatibili,
                // infine qualunque cosa esista.
                candidates.AddRange(new[]
                {
                    "Segoe UI", "Tahoma", "Verdana", "Arial",
                    "Liberation Sans", "Arimo", "DejaVu Sans"
                });

                foreach (string name in candidates)
                {
                    if (string.IsNullOrWhiteSpace(name))
                    {
                        continue;
                    }

                    if (IsFontUsable(name))
                    {
                        Resources["GlobalFontFamily"] = new FontFamily(name);
                        return;
                    }
                }

                // Nessun candidato utilizzabile: lasciamo decidere al sistema.
                Resources["GlobalFontFamily"] = SystemFonts.MessageFontFamily;
            }
            catch (Exception)
            {
                // Non deve mai impedire l'avvio.
            }
        }

        /// <summary>
        /// Verifica che un font sia installato SENZA passare da WPF.
        ///
        /// Attenzione: FontFamily.GetTypefaces() e qualunque altra API che
        /// risolva davvero la famiglia chiamano Environment.FailFast quando
        /// il font non esiste e non c'e' un fallback valido — quindi non si
        /// possono usare per "provare" un font, nemmeno dentro un try/catch.
        /// L'unico controllo sicuro e' l'enumerazione GDI, che si limita a
        /// leggere l'elenco dei font installati.
        /// </summary>
        private static bool IsFontUsable(string familyName)
        {
            try
            {
                // Fonts.SystemFontFamilies e' l'elenco che usa WPF stesso per
                // risolvere i nomi: se una famiglia non e' qui, il motore di
                // testo non la trovera' mai, per quanto GDI la dichiari
                // installata (le sostituzioni di registro ingannano GDI ma non
                // il layer di shaping di WPF).
                foreach (FontFamily family in Fonts.SystemFontFamilies)
                {
                    if (string.Equals(family.Source, familyName,
                                      StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }

                    // Il nome localizzato puo' differire da Source.
                    foreach (string name in family.FamilyNames.Values)
                    {
                        if (string.Equals(name, familyName,
                                          StringComparison.OrdinalIgnoreCase))
                        {
                            return true;
                        }
                    }
                }
            }
            catch (Exception)
            {
                // in caso di dubbio, meglio considerarlo assente
            }

            return false;
        }

        /// <summary>Chiusura pulita richiesta dalla finestra Proprietà.</summary>
        internal static void RequestShutdown()
        {
            // v2.37.1: la rete di sicurezza (Environment.Exit in OnExit) NON
            // copriva questo percorso: ShutdownTaskbar() e' SINCRONA e parte
            // PRIMA di Shutdown(), quindi se si blocca (es. il join nativo
            // del thread tray fermo su una SendMessageTimeoutW verso un
            // Explorer che non risponde) OnExit non viene mai raggiunto.
            // Timer di guardia: se la pulizia ordinata non finisce in 5
            // secondi, il processo termina comunque. Se la pulizia riesce,
            // il timer viene annullato e la chiusura resta pulita.
            var forceExit = new System.Threading.Timer(
                _ => Environment.Exit(0),
                null,
                TimeSpan.FromSeconds(5),
                System.Threading.Timeout.InfiniteTimeSpan);

            if (Current is App app)
            {
                app._taskbar?.ShutdownTaskbar();
            }
            forceExit.Dispose();   // pulizia riuscita: annulla la guardia

            Current?.Shutdown();
        }
    }
}
