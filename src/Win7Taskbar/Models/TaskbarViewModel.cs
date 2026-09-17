// Win7Taskbar - view model principale
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
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows.Threading;
using Win7Taskbar.Controls;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Models
{
    /// <summary>
    /// Stato della barra: gruppi della Superbar, orologio e area di notifica.
    /// Fa da ponte fra gli eventi del core nativo e i binding del tema.
    /// </summary>
    internal sealed class TaskbarViewModel : INotifyPropertyChanged, IDisposable
    {
        private readonly NativeBridge _bridge;
        private readonly DispatcherTimer _refreshTimer;
        private List<PinInfo>? _pinsCache;

        /* v2.27: quando l'utente avvia un pin dalla barra, per pochi
         * secondi le nuove finestre di explorer.exe appartengono a quel
         * pin anche se il collegamento e' un elemento shell senza
         * percorso (IDList): e' il comportamento con cui la taskbar di
         * Windows associa la finestra appena aperta al pulsante premuto,
         * invece di creare un bottone nuovo con l'icona del contenuto. */
        private (string PinAppId, DateTime UntilUtc)? _pendingLaunch;
        private bool _disposed;

        public event PropertyChangedEventHandler? PropertyChanged;

        /// <summary>
        /// Segnalato quando un'applicazione invia una notifica a fumetto.
        /// La finestra della barra lo intercetta e mostra la nuvoletta.
        /// </summary>
        public event EventHandler<BalloonNotification>? BalloonReceived;
        public event EventHandler? OverflowHidden;   // v3.2

        public TaskbarViewModel(NativeBridge bridge)
        {
            _bridge = bridge;
            Groups = new ObservableCollection<TaskGroup>();
            NotificationArea = new NotificationArea();
            Clock = new ClockModel();

            _bridge.CoreEventRaised += OnCoreEvent;

            // Rete di sicurezza: alcuni cambi di stato non generano WinEvent
            // (es. una finestra che cambia icona senza notificarlo).
            //
            // NOTA SULLE PRESTAZIONI: ogni giro marshalla un array di struct
            // con tre stringhe da 260 caratteri ciascuna, quindi ~1,6 KB per
            // finestra. A 2 secondi fissi era spazzatura continua che gonfiava
            // l'heap senza motivo: gli eventi del core coprono gia' il 99% dei
            // casi. Qui teniamo solo un controllo di sicurezza rado.
            _refreshTimer = new DispatcherTimer(DispatcherPriority.ApplicationIdle)
            {
                Interval = TimeSpan.FromSeconds(10)
            };
            _refreshTimer.Tick += (_, _) =>
            {
                RefreshWindows();
                RefreshTray();
                TrimWorkingSet();
            };
        }

        public ObservableCollection<TaskGroup> Groups { get; }

        // v1.21.15: ordine personalizzato dal trascinamento (meccanismo
        // copiato da RetroBar): lista di sessione, svuotata ad ogni
        // MoveTaskGroup e usata da RefreshWindows per non far tornare i
        // bottoni alla posizione di default al prossimo refresh.
        private readonly List<TaskGroup> _userOrder = new();

        /// <summary>Sposta un gruppo alla posizione di inserimento
        /// <paramref name="rawInsertIndex"/> (indice fra i bottoni, 0..Count;
        /// la semantica e' quella del trascinamento: il bottone va PRIMA
        /// dell'i-esimo bottone corrente). Registra l'ordine risultante
        /// come preferenza di sessione.</summary>
        public void MoveTaskGroup(TaskGroup group, int rawInsertIndex)
        {
            int cur = Groups.IndexOf(group);
            if (cur < 0)
            {
                return;
            }
            int index = rawInsertIndex;
            if (index < 0) index = 0;
            if (index > Groups.Count) index = Groups.Count;
            if (index > cur) index--;
            if (index != cur)
            {
                Groups.Move(cur, index);
            }
            _userOrder.Clear();
            _userOrder.AddRange(Groups);
        }

        public NotificationArea NotificationArea { get; }

        public ClockModel Clock { get; }

        public void Start()
        {
            RefreshWindows();
            RefreshTray();
            _refreshTimer.Start();

            // All'avvio WPF ha appena finito di compilare il tema e decodificare
            // le PNG: l'heap e' al massimo storico. Una compattazione singola
            // qui restituisce al sistema decine di MB che non serviranno piu'.
            CompactMemory();
        }

        private int _idleCycles;

        /// <summary>
        /// Restituisce al sistema la memoria non piu' in uso.
        ///
        /// Una taskbar sta accesa per giorni e resta quasi sempre ferma: senza
        /// questo, il working set cresce e non torna mai indietro, ed e' quello
        /// che Gestione attivita' mostra come "memoria usata".
        /// </summary>
        private void TrimWorkingSet()
        {
            // Non a ogni giro: comprimere costa. Una volta al minuto basta.
            if (++_idleCycles < 6)
            {
                return;
            }
            _idleCycles = 0;
            CompactMemory();
        }

        private static void CompactMemory()
        {
            try
            {
                GC.Collect(2, GCCollectionMode.Optimized, blocking: false, compacting: true);
                NativeMethods.EmptyWorkingSet(NativeMethods.GetCurrentProcess());
            }
            catch (EntryPointNotFoundException)
            {
                // psapi non disponibile: non e' critico.
            }
        }

        // ---------------------------------------------------------------
        //  Eventi dal core
        // ---------------------------------------------------------------

        private void OnCoreEvent(object? sender, CoreEventArgs e)
        {
            switch (e.EventType)
            {
                case CoreEvent.WindowAdded:
                case CoreEvent.WindowRemoved:
                case CoreEvent.WindowChanged:
                case CoreEvent.WindowActivated:
                // Una richiesta di attenzione cambia lo stato della finestra
                // (bit FLASHING): va riletto come per gli altri eventi.
                case CoreEvent.WindowFlash:
                    RefreshWindows();
                    break;

                case CoreEvent.TrayAdd:
                case CoreEvent.TrayModify:
                case CoreEvent.TrayDelete:
                    RefreshTray();
                    break;

                case CoreEvent.TrayBalloon:
                    // NIF_INFO puo' arrivare insieme a NIM_ADD/NIM_MODIFY:
                    // l'icona va aggiornata comunque, non solo il fumetto.
                    RefreshTray();
                    RaiseBalloon();
                    break;

                case CoreEvent.PinnedChanged:
                    // v2.25: cartella pin cambiata (watcher nativo).
                    _pinsCache = null;
                    /* v1.21.8: la mappa collegamento -> icona personalizzata
                     * si ricostruisce qui: e' il momento in cui l'utente puo'
                     * aver creato o modificato una scorciatoia. */
                    PinReader.InvalidateShortcutIconIndex();
                    RefreshWindows();
                    break;

                case CoreEvent.OverflowHidden:
                    // v3.2: il pannello nativo si e' chiuso da solo (click
                    // interno o fuori): la barra aggiorna freccetta/texture.
                    OverflowHidden?.Invoke(this, EventArgs.Empty);
                    break;
            }
        }

        private void RaiseBalloon()
        {
            if (_bridge.TryGetLastBalloon(out BalloonNotification balloon))
            {
                BalloonReceived?.Invoke(this, balloon);
            }
        }

        // ---------------------------------------------------------------
        //  Superbar: sincronizzazione dei gruppi
        // ---------------------------------------------------------------

        /// <summary>
        /// Riallinea i gruppi allo stato reale del sistema. Aggiorna in place
        /// invece di ricostruire: cosi' i bottoni non "sfarfallano" e non
        /// perdono lo stato di hover.
        /// </summary>
        /// <summary>
        /// Attivo solo se la variabile d'ambiente W7T_DEBUG_FLASH vale "1".
        /// Non ha effetto in uso normale.
        /// </summary>
        private static readonly bool DebugForceFlash =
            Environment.GetEnvironmentVariable("W7T_DEBUG_FLASH") == "1";

        public void RefreshWindows()
        {
            IReadOnlyList<W7TWindowInfo> windows = _bridge.GetWindows();
            List<PinInfo> pins = _pinsCache ??= LoadPinsFromCore();

            // Raggruppa per AppUserModelID / percorso eseguibile.
            var byApp = windows
                .Where(w => !string.IsNullOrEmpty(w.AppId))
                .GroupBy(w => w.AppId, StringComparer.OrdinalIgnoreCase)
                .ToList();

            // 0) v2.20: i pinnati REALI (lnk della shell) esistono sempre,
            //    anche con zero finestre (stato Idle della Superbar).
            foreach (PinInfo pin in pins)
            {
                TaskGroup? pg = Groups.FirstOrDefault(g => PinMatches(pin, g));
                if (pg == null)
                {
                    // v2.27: un gruppo di finestre di explorer.exe nato
                    // PRIMA del riallineamento dei pin e' l'app Explorer:
                    // il pin "Esplora file" gli si aggancia invece di
                    // creare un secondo bottone.
                    pg = Groups.FirstOrDefault(g => !g.IsPinned &&
                        IsExplorerPin(pin, g.ExePath ?? string.Empty));
                }

                if (pg == null)
                {
                    pg = new TaskGroup(pin.AppId, pin.TargetPath)
                    {
                        IsPinned = true,
                        LaunchPath = pin.LnkPath,
                        Icon = pin.Icon
                    };
                    Groups.Add(pg);
                }
                else
                {
                    pg.IsPinned = true;
                    pg.LaunchPath ??= pin.LnkPath;
                    // v2.26: l'icona del pin E' l'identita' dell'app:
                    // prevale sempre sull'icona del contenuto delle
                    // finestre (Esplora file che apre "Questo PC" deve
                    // mostrare l'icona di Explorer, non del contenuto).
                    pg.Icon = pin.Icon;
                }
            }

            // chi non e' piu' pinnato torna gruppo "solo running"
            foreach (TaskGroup g in Groups)
            {
                if (g.IsPinned && !pins.Any(p => PinMatches(p, g)))
                {
                    g.IsPinned = false;
                }
            }

            // 1) Elimina i gruppi che non esistono piu' (mai i pinnati).
            for (int i = Groups.Count - 1; i >= 0; i--)
            {
                if (Groups[i].IsPinned)
                {
                    continue;
                }
                if (!byApp.Any(g => string.Equals(g.Key, Groups[i].AppId,
                                                  StringComparison.OrdinalIgnoreCase)))
                {
                    Groups.RemoveAt(i);
                }
            }

            // 2) Aggiorna quelli esistenti e aggiungi i nuovi.
            // v2.24: i gruppi NON presenti fra le finestre vive vanno
            // sincronizzati a zero finestre (pin torna idle, morti spariscono).
            // v2.29: FIX raggruppamento multi-AppId sullo stesso TaskGroup
            // (es. due profili Chrome, AppUserModelID diverso per profilo).
            // PRIMA si risolve, per ogni IGrouping nativo, quale TaskGroup gli
            // appartiene e si ACCUMULANO le finestre in una mappa per-gruppo;
            // SOLO DOPO si chiama SyncGroupWindows, una volta sola per ogni
            // TaskGroup, con l'unione di tutte le finestre che gli spettano.
            // (Prima del fix: SyncGroupWindows veniva chiamato una volta per
            // ogni AppId nativo, e la sua logica di rimozione "finestre non
            // piu' presenti" cancellava, alla chiamata successiva sullo
            // stesso gruppo, le finestre appena aggiunte dalla chiamata
            // precedente: con due profili Chrome sopravviveva solo l'ultimo
            // processato, il gruppo restava sempre a 1 finestra.)
            var windowsByGroup = new Dictionary<TaskGroup, List<W7TWindowInfo>>();

            List<W7TWindowInfo> GetBucket(TaskGroup g)
            {
                if (!windowsByGroup.TryGetValue(g, out List<W7TWindowInfo>? bucket))
                {
                    bucket = new List<W7TWindowInfo>();
                    windowsByGroup[g] = bucket;
                }
                return bucket;
            }

            foreach (IGrouping<string, W7TWindowInfo> group in byApp)
            {
                TaskGroup? existing = Groups.FirstOrDefault(
                    g => string.Equals(g.AppId, group.Key, StringComparison.OrdinalIgnoreCase));

                if (existing == null)
                {
                    // v2.22: la finestra puo' appartenere a un'app gia'
                    // presente come PIN con identita' diversa (AppUserModelID
                    // del lnk vs percorso usato dal core): aggancia al gruppo
                    // pinnato invece di creare un bottone duplicato
                    // (il "tre Google" venivano da qui).
                    string firstExe = group.First().ExePath ?? string.Empty;
                    PinInfo? viaPin = pins.FirstOrDefault(p =>
                        string.Equals(p.AppId, group.Key,
                                      StringComparison.OrdinalIgnoreCase) ||
                        (!string.IsNullOrEmpty(p.TargetPath) &&
                         string.Equals(p.TargetPath, group.Key,
                                       StringComparison.OrdinalIgnoreCase)) ||
                        // v2.26: stessa app anche se l'identita' e' scritta
                        // in forme diverse (AUMID nel lnk, exe nel processo,
                        // o viceversa). Questo e' il criterio con cui la
                        // Superbar raggruppa, come Explorer, piu' finestre
                        // dello stesso eseguibile sotto un unico pulsante
                        // (due profili di Chrome = un bottone, 2 finestre).
                        SameApp(p.TargetPath, firstExe) ||
                        IsExplorerPin(p, firstExe));
                    if (viaPin == null && _pendingLaunch is { } pending &&
                        DateTime.UtcNow < pending.UntilUtc &&
                        firstExe.EndsWith("\\explorer.exe",
                                          StringComparison.OrdinalIgnoreCase))
                    {
                        // v2.27: finestra di explorer.exe comparsa subito
                        // dopo l'avvio di un pin shell-item: appartiene al
                        // pin appena lanciato (niente bottone "cartella").
                        viaPin = pins.FirstOrDefault(pp =>
                            string.Equals(pp.AppId, pending.PinAppId,
                                          StringComparison.OrdinalIgnoreCase));
                        _pendingLaunch = null;
                    }

                    if (viaPin != null)
                    {
                        existing = Groups.FirstOrDefault(g => PinMatches(viaPin, g));
                    }
                }

                if (existing == null)
                {
                    existing = new TaskGroup(group.Key, group.First().ExePath);
                    Groups.Add(existing);
                }

                // v2.29: non piu' SyncGroupWindows qui - si accumula soltanto.
                GetBucket(existing).AddRange(group);
            }

            var synced = new HashSet<TaskGroup>();
            foreach (KeyValuePair<TaskGroup, List<W7TWindowInfo>> kv in windowsByGroup)
            {
                // v2.29: UNA sola chiamata per gruppo, con TUTTE le finestre
                // che gli appartengono (anche da AppId nativi diversi):
                // niente piu' finestre cancellate a vicenda fra profili.
                SyncGroupWindows(kv.Key, kv.Value);
                synced.Add(kv.Key);
            }
            foreach (TaskGroup g in Groups)
            {
                if (!synced.Contains(g))
                {
                    SyncGroupWindows(g, Enumerable.Empty<W7TWindowInfo>());
                }
            }

            // 3) v2.20: ordine Superbar = pin nell'ordine della cartella,
            //    poi le app solo-running (senza ricreare i bottoni).
            // (due .lnk possono dichiarare lo stesso AppUserModelID:
            // senza deduplica la lista desired sforava e Move() lanciava
            // ArgumentOutOfRangeException al primo evento del core.)
            // v1.21.7: the variable is no longer read-only, because step 3b
            // applies the user's ordering layer to it.
            List<TaskGroup> desired = new List<TaskGroup>(Groups.Count);
            var desired = new List<TaskGroup>(Groups.Count);
            // v1.21.15: riordino con trascinamento (come RetroBar): se
            // l'utente ha già spostato dei bottoni, QUELL'ordine comanda
            // per tutti i gruppi che coinvolge; i gruppi spariti si
            // purgano, i gruppi nuovi si accodano più sotto con la regola
            // normale. Di sessione, non salvato su disco (come RetroBar).
            if (_userOrder.Count > 0)
            {
                _userOrder.RemoveAll(g => !Groups.Contains(g));
                foreach (TaskGroup g in _userOrder)
                {
                    if (!desired.Contains(g))
                    {
                        desired.Add(g);
                    }
                }
            }
            foreach (PinInfo pin in pins)
            {
                TaskGroup? g = Groups.FirstOrDefault(x => PinMatches(pin, x));
                if (g != null && !desired.Contains(g))
                {
                    desired.Add(g);
                }
            }
            foreach (TaskGroup g in Groups)
            {
                if (!g.IsPinned && !desired.Contains(g))
                {
                    desired.Add(g);
                }
            }

            // 3b) v1.21.7: ORDERING LAYER of the software taskbar. The order
            //     just computed (pins in folder order, then running
            //     applications) stays the BASE: the user's choice, when it
            //     exists, is applied on top of it. With an empty list the
            //     sequence does not move by one position (see
            //     VirtualTaskbarOrder). Nothing of Windows is touched: it is
            //     only the order our buttons are laid out in.
            desired = VirtualTaskbarOrder.Apply(
                desired, ReadSavedTaskbarOrder());

            if (desired.Count == Groups.Count)
            {
                for (int i = 0; i < desired.Count; i++)
                {
                    int cur = Groups.IndexOf(desired[i]);
                    if (cur != i && cur >= 0 && cur < Groups.Count)
                    {
                        Groups.Move(cur, i);
                    }
                }
            }
        }

        /// <summary>
        /// v2.22: un pin e un gruppo di finestre sono la STESSA app se
        /// coincidono per AppUserModelID OPPURE per percorso eseguibile
        /// (il core puo' identificare le finestre col path quando l'app non
        /// dichiara AppId, mentre il .lnk pinnato dichiara l'AppUserModelID:
        /// senza questo doppio criterio Chrome compariva tre volte).
        /// </summary>
        private static bool PinMatches(PinInfo pin, TaskGroup group)
        {
            if (string.Equals(pin.AppId, group.AppId,
                              StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            if (!string.IsNullOrEmpty(pin.TargetPath) &&
                !string.IsNullOrEmpty(group.ExePath) &&
                string.Equals(pin.TargetPath, group.ExePath,
                              StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            // v2.26: finestre di explorer.exe con pin avente l'AUMID
            // canonico di Explorer. v2.27: il criterio "elemento shell"
            // vale solo su gruppi non ancora rivendicati da un altro pin.
            if (!group.IsPinned &&
                IsExplorerPin(pin, group.ExePath ?? string.Empty))
            {
                return true;
            }

            // ultimo criterio: nome file senza estensione identico
            try
            {
                string pn = System.IO.Path.GetFileNameWithoutExtension(pin.TargetPath)
                            ?? string.Empty;
                string gn = System.IO.Path.GetFileNameWithoutExtension(group.ExePath)
                            ?? string.Empty;
                return !string.IsNullOrEmpty(pn) &&
                       string.Equals(pn, gn, StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// v2.26: "stessa applicazione" fra target del pin ed eseguibile
        /// della finestra: stesso percorso normalizzato OPPURE stesso nome
        /// file senza estensione (confronto case-insensitive). E' il
        /// criterio di fallback che la taskbar di Windows usa quando
        /// l'AppUserModelID non coincide fra collegamento e processo.
        /// </summary>
        private static bool SameApp(string pinTarget, string windowExe)
        {
            if (string.IsNullOrEmpty(pinTarget) || string.IsNullOrEmpty(windowExe))
            {
                return false;
            }

            if (string.Equals(pinTarget, windowExe, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            try
            {
                string pn = System.IO.Path.GetFileNameWithoutExtension(pinTarget);
                string gn = System.IO.Path.GetFileNameWithoutExtension(windowExe);
                return !string.IsNullOrEmpty(pn) &&
                       string.Equals(pn, gn, StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// v2.26: le finestre di explorer.exe appartengono all'elemento
        /// Explorer quando il pin ha l'AUMID canonico dichiarato da
        /// Windows ("Microsoft.Windows.Explorer"): e' il comportamento con
        /// cui Explorer stesso mantiene il grouping delle proprie
        /// finestre (incluse quelle che mostrano "Questo PC"/cartelle).
        /// </summary>
        private static bool IsExplorerPin(PinInfo pin, string windowExe)
        {
            if (string.IsNullOrEmpty(windowExe) ||
                !windowExe.EndsWith("\\explorer.exe", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            return string.Equals(pin.AppId, "Microsoft.Windows.Explorer",
                                 StringComparison.OrdinalIgnoreCase) ||
                   string.Equals(pin.TargetPath, "Microsoft.Windows.Explorer",
                                 StringComparison.OrdinalIgnoreCase) ||
                   // v2.27: i pin "elemento shell" (IDList, nessun exe:
                   // l'Esplora file pinnato da Windows) si aprono comunque
                   // dentro explorer.exe: le sue finestre gli appartengono.
                   // (Caso limite documentato: con piu' pin shell-item la
                   // prima enumerata riceve le finestre, come in Windows.)
                   string.IsNullOrEmpty(pin.TargetPath);
        }

        /// <summary>v2.27: la barra ha appena avviato questo pin: per i
        /// prossimi secondi le finestre di explorer.exe senza altra
        /// corrispondenza vengono agganciate a questo gruppo.</summary>
        public void NotePinLaunch(TaskGroup group)
        {
            if (group == null || string.IsNullOrEmpty(group.AppId))
            {
                return;
            }

            _pendingLaunch = (group.AppId, DateTime.UtcNow.AddSeconds(8));
        }

        /// <summary>v2.25: dopo un unpin reale o su richiesta.</summary>
        public void InvalidatePins()
        {
            _bridge.PinnedRefresh();
            _pinsCache = null;
            PinReader.InvalidateShortcutIconIndex();
            RefreshWindows();
        }

        // ---------------------------------------------------------------
        //  v1.21.7 - Icon order chosen by the user
        // ---------------------------------------------------------------

        /// <summary>
        /// Order stored in the configuration (empty list = the user never
        /// reordered anything, so the usual order is used). Reading it must
        /// never break a taskbar refresh, therefore any error turns into "no
        /// order".
        /// </summary>
        private static IReadOnlyList<string> ReadSavedTaskbarOrder()
        {
            try
            {
                return RetroBar.Utilities.Settings.Instance.TaskbarIconOrder;
            }
            catch (Exception)
            {
                return Array.Empty<string>();
            }
        }

        /// <summary>v1.21.7: the keys the order is stored with, for
        /// diagnostics (a readable log line instead of an index).</summary>
        public List<string> CurrentTaskbarOrderKeys()
            => VirtualTaskbarOrder.KeysFor(Groups);

        /// <summary>
        /// v1.21.7 - applies the order chosen by dragging a button.
        ///
        /// It reorders the groups that are already there (no rebuild: the
        /// buttons do not flicker and the hover state is not lost) and saves
        /// the list of keys in the program configuration. The Windows taskbar
        /// is neither read nor written: only OUR representation is reordered.
        ///
        /// It rejects the list unless it is exactly a permutation of the shown
        /// one: that is the defence against a partial reorder, or one coming
        /// from a model that changed in the meantime (a window closed during
        /// the drag), which would otherwise move the wrong buttons.
        /// </summary>
        public bool ApplyUserTaskbarOrder(IList<TaskGroup> ordered)
        {
            if (ordered == null || ordered.Count != Groups.Count || Groups.Count == 0)
            {
                return false;
            }

            var shown = new HashSet<TaskGroup>(Groups);
            foreach (TaskGroup group in ordered)
            {
                if (!shown.Contains(group))
                {
                    return false;
                }
            }

            for (int i = 0; i < ordered.Count; i++)
            {
                int current = Groups.IndexOf(ordered[i]);
                if (current < 0)
                {
                    return false;
                }
                if (current != i)
                {
                    Groups.Move(current, i);
                }
            }

            try
            {
                /* SetTaskbarIconOrder (not the property): this is the method
                 * that saves - the property exists for reading the
                 * configuration file back and must not rewrite it. */
                RetroBar.Utilities.Settings.Instance.SetTaskbarIconOrder(
                    VirtualTaskbarOrder.KeysFor(Groups));
                return true;
            }
            catch (Exception)
            {
                // Persistence must never break the bar: the order stays
                // applied on screen and will be written again at the next
                // successful reorder.
                return false;
            }
        }

        /// <summary>
        /// v2.25: il modello pin arriva dal core (C++/Shell); qui si aggiunge
        /// solo l'icona di presentazione estratta dal .lnk: la UI non fa
        /// discovery.
        /// </summary>
        private List<PinInfo> LoadPinsFromCore()
        {
            var list = new List<PinInfo>();
            foreach (var pn in _bridge.GetPinnedApps())
            {
                list.Add(new PinInfo
                {
                    AppId = pn.Identity ?? string.Empty,
                    LnkPath = pn.LnkPath ?? string.Empty,
                    TargetPath = pn.Target ?? string.Empty,
                    Icon = PinReader.ReadIcon(pn.LnkPath ?? string.Empty, pn.Target ?? string.Empty)
                });
            }
            return list;
        }

        private void SyncGroupWindows(TaskGroup group, IEnumerable<W7TWindowInfo> windows)
        {
            List<W7TWindowInfo> list = windows.ToList();

            // Rimuovi le finestre chiuse.
            for (int i = group.Windows.Count - 1; i >= 0; i--)
            {
                if (list.All(w => w.Hwnd != group.Windows[i].Hwnd))
                {
                    group.Windows.RemoveAt(i);
                }
            }

            // Aggiungi/aggiorna.
            foreach (W7TWindowInfo info in list)
            {
                TaskWindow? window = group.Windows.FirstOrDefault(w => w.Hwnd == info.Hwnd);
                if (window == null)
                {
                    window = new TaskWindow(info.Hwnd, info.AppId);
                    group.Windows.Add(window);
                }

                var state = (WindowStateFlags)info.State;
                window.Title = info.Title ?? string.Empty;
                // Reuse the executable path already supplied by the native
                // window enumeration; don't perform another process query.
                window.ApplicationName = TaskGroup.ResolveFriendlyApplicationName(
                    info.ExePath, window.Title, info.AppId);
                window.IsActive = state.HasFlag(WindowStateFlags.Active);
                window.IsMinimized = state.HasFlag(WindowStateFlags.Minimized);
                window.IsMaximized = state.HasFlag(WindowStateFlags.Maximized);

                // Una finestra in primo piano non lampeggia mai: Windows spegne
                // la richiesta di attenzione appena l'utente la guarda.
                window.IsFlashing = state.HasFlag(WindowStateFlags.Flashing)
                                    && !window.IsActive;

                // Interruttore diagnostico: con W7T_DEBUG_FLASH=1 la prima
                // finestra non attiva viene mostrata come lampeggiante. Serve
                // a verificare la resa grafica dove HSHELL_FLASH non arriva
                // (per esempio sotto Wine, che non registra gli hook di shell).
                if (DebugForceFlash && !window.IsActive)
                {
                    window.IsFlashing = true;
                }

                window.Icon ??= _bridge.GetWindowIcon(info.Hwnd);
            }

            // L'icona del gruppo e' quella della prima finestra disponibile.
            group.Icon ??= group.Windows.Select(w => w.Icon).FirstOrDefault(icon => icon != null);
            group.RefreshAggregateState();
        }

        // ---------------------------------------------------------------
        //  Area di notifica
        // ---------------------------------------------------------------

        /* v1.7.6: the EnableAutoTray read that used to live here (with a
         * cached registry lookup) moved to the native core, where the same
         * rule is resolved together with the saved per-icon tray
         * behaviors. One authority, one cache, no view-level
         * overrides. */

        /// <summary>
        /// v2.62: vero mentre RefreshTray sta applicando lo stato letto dal
        /// core. Serve a NON rimandare indietro al core cio' che il core ci
        /// ha appena detto: senza questa distinzione ogni allineamento
        /// veniva riscritto come se fosse una scelta dell'utente e finiva
        /// salvato come preferenza, congelando per sempre la disposizione
        /// letta da Explorer.
        /// </summary>
        internal bool ApplyingTrayState { get; private set; }

        /// <summary>
        /// v2.62: letture della tray sospese (trascinamento in corso).
        ///
        /// Mentre l'utente trascina un'icona il modello non deve cambiare:
        /// un aggiornamento in quel momento ricrea i contenitori delle icone,
        /// il mouse perde la cattura e il trascinamento si interrompe da solo.
        /// La lettura non si perde: si rimanda a quando il trascinamento
        /// finisce (vedi ResumeTrayRefresh).
        /// </summary>
        public void SuspendTrayRefresh()
        {
            _trayRefreshSuspended = true;
        }

        public void ResumeTrayRefresh()
        {
            if (!_trayRefreshSuspended)
            {
                return;
            }
            _trayRefreshSuspended = false;
            if (_trayRefreshPending)
            {
                _trayRefreshPending = false;
                RefreshTray();
            }
        }

        private bool _trayRefreshSuspended;
        private bool _trayRefreshPending;

        public void RefreshTray()
        {
            if (_trayRefreshSuspended)
            {
                _trayRefreshPending = true;
                return;
            }

            IReadOnlyList<W7TTrayIconInfo> icons = _bridge.GetTrayIcons();
            bool changed = false;
            /* v1.7.6: the "show all icons" rule (EnableAutoTray=0) is NOT
             * applied here any more. It lives in the native resolver, which
             * also knows the per-icon choices of the "Notification Area
             * Icons" page: a VIEW must not re-apply a default over a row
             * the user explicitly set to "Only show notifications". The
             * IsPinned/IsHidden values read below already ARE the resolved,
             * final answer of the model. */

            // Rimuovi le icone sparite.
            for (int i = NotificationArea.AllIcons.Count - 1; i >= 0; i--)
            {
                TrayIconModel model = NotificationArea.AllIcons[i];
                if (!icons.Any(x => x.OwnerHwnd == model.OwnerHwnd && x.Uid == model.Uid))
                {
                    NotificationArea.AllIcons.RemoveAt(i);
                    changed = true;
                }
            }

            // Aggiungi/aggiorna.
            ApplyingTrayState = true;
            try
            {
                foreach (W7TTrayIconInfo info in icons)
                {
                    TrayIconModel? model = NotificationArea.AllIcons.FirstOrDefault(
                        x => x.OwnerHwnd == info.OwnerHwnd && x.Uid == info.Uid);

                    if (model == null)
                    {
                        model = new TrayIconModel(info.OwnerHwnd, info.Uid);

                        /* Lo spillo del tema sposta l'icona fra barra e overflow
                         * scrivendo IsPinned sul modello. La scelta deve arrivare
                         * anche al core: senza questa propagazione il refresh
                         * periodico la sovrascriveva (il core la credeva ancora
                         * appuntata) e il menu di overflow si svuotava da solo.
                         * RetroBar persiste allo stesso modo l'ordine e i pin
                         * (NotifyIconList.UpdateIconOrder). */
                        model.PropertyChanged += (_, ev) =>
                        {
                            if (ev.PropertyName == nameof(TrayIconModel.IsPinned) &&
                                !ApplyingTrayState)
                            {
                                _bridge.SetTrayIconPinned(model.OwnerHwnd, model.Uid, model.IsPinned);
                            }
                        };

                        NotificationArea.AllIcons.Add(model);
                        changed = true;
                    }

                    model.Tooltip = info.Tooltip ?? string.Empty;

                    if (model.IsPinned != (info.IsPinned != 0))
                    {
                        model.IsPinned = info.IsPinned != 0;
                        changed = true;
                    }

                    if (model.IsHidden != (info.IsHidden != 0))
                    {
                        model.IsHidden = info.IsHidden != 0;
                        changed = true;
                    }

                    // Ricarica il bitmap solo quando il core segnala una revisione
                    // diversa: evita di ricostruire BitmapSource a ogni giro.
                    //
                    // v2.1: MAI sovrascrivere un'icona buona con null. Se il core
                    // in questo momento non ha pixel per l'icona (CopyIcon
                    // rifiutata da Explorer, cattura PrintWindow non riuscita),
                    // GetTrayIcon restituisce null: si tiene l'ultimo fotogramma
                    // buono e si riprova al giro successivo, come fa la shell
                    // (l'icona non deve SPARIRE per un aggiornamento vuoto).
                    if (model.Icon == null || model.IconRevision != info.IconRevision)
                    {
                        var fresh = _bridge.GetTrayIcon(info.OwnerHwnd, info.Uid);
                        if (fresh != null)
                        {
                            model.Icon = fresh;
                            model.IconRevision = info.IconRevision;

                        }
                        else if (model.Icon == null)
                        {
                            // Mai avuta un'immagine: lascia IconRevision a 0 cosi'
                            // il prossimo refresh riprova subito (il core intanto
                            // pianifica da solo la ricattura dei pixel mancanti).
                            model.IconRevision = 0;
                        }
                        // else: icona buona gia' presente -> non toccare nulla.
                    }
                }
            }
            finally
            {
                ApplyingTrayState = false;
            }

            if (changed)
            {
                NotificationArea.Resort();
            }
        }

        // ---------------------------------------------------------------
        //  Azioni
        // ---------------------------------------------------------------

        public void ActivateWindow(TaskWindow window)
            => _bridge.ExecuteCommand(window.Hwnd, WindowCommand.Activate);

        public void ExecuteWindowCommand(TaskWindow window, int command)
        {
            _bridge.ExecuteCommand(window.Hwnd, command);
            RefreshWindows();
        }

        public void MinimizeGroup(TaskGroup group)
        {
            _bridge.MinimizeGroup(group.AppId);
            RefreshWindows();
        }

        public void CloseGroup(TaskGroup group)
        {
            _bridge.CloseGroup(group.AppId);
            RefreshWindows();
        }

        public void SendTrayClick(TrayIconModel icon, int clickType, int x, int y)
            => _bridge.SendTrayClick(icon.OwnerHwnd, icon.Uid, clickType, x, y);

        public void SetTrayIconPinned(TrayIconModel icon, bool pinned)
        {
            _bridge.SetTrayIconPinned(icon.OwnerHwnd, icon.Uid, pinned);
            icon.IsPinned = pinned;
            NotificationArea.Resort();
        }

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;

            _refreshTimer.Stop();
            _bridge.CoreEventRaised -= OnCoreEvent;
            Clock.Dispose();
        }
    }
}
