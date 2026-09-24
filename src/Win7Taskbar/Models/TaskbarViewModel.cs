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
using Win7Taskbar.Models.Tasking;
using Win7Taskbar.StartMenu;

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

        /* v2.27: when the user launches a pin from the bar, for a few seconds
         * new explorer.exe windows belong to that pin even when the shortcut
         * is a shell item without a path: it is the behaviour with which the
         * Windows taskbar associates a just-opened window with the pressed
         * button instead of creating a new button with the content icon. The
         * affinity window now lives in TaskMatcher (model identity), not in
         * the view. */
        private readonly AppIconCache _icons;
        private readonly TaskCatalog _catalog;
        private readonly TaskResolver _resolver;
        private readonly TaskProjection _projection;
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

            // Shared task model (identity + grouping + async resolution) and
            // its presentation projection (the bindable buttons the theme
            // knows). The resolver worker materializes icons, titles and
            // friendly names off the UI thread and completes through the
            // catalog, which re-validates every result against the current
            // model before applying it.
            _icons = new AppIconCache();
            _catalog = new TaskCatalog();
            _resolver = new TaskResolver(bridge, _icons, Dispatcher.CurrentDispatcher);
            _resolver.Completed = result => _catalog.CompleteResolution(result);
            _catalog.ResolveRequested = (entry, reason, wantExeIcon) =>
                _resolver.Schedule(entry, reason, wantExeIcon);
            _projection = new TaskProjection(_catalog, _icons,
                (entry, reason, wantExeIcon) => _resolver.Schedule(entry, reason, wantExeIcon));

            // Deterministic icon-cache cleanup (RAII): a window's materialized
            // icon dies with its entry. EntryRemoving is the documented
            // notify-first hook, so this covers every removal path (core event
            // and reconcile alike); the frozen ImageSource itself lives on as
            // long as any departing button still references it.
            _catalog.EntryRemoving += (_, args) =>
                _icons.Invalidate(AppIconCache.WindowKey(args.Entry.Hwnd));

            NotificationArea = new NotificationArea();
            Clock = new ClockModel();

            _bridge.CoreEventRaised += OnCoreEvent;
            StartMenuStore.PinsChanged += OnOurPinsChanged;

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
                // Same contract as the core-event pump: a failed safety-net
                // tick must not kill the timer.
                try
                {
                    RefreshWindows();
                    RefreshTray();
                    TrimWorkingSet();
                }
                catch (Exception ex)
                {
                    _bridge.Log("refresh tick failed: " + ex.Message);
                }
            };
        }

        /// <summary>The bindable buttons: a projection of the shared model.</summary>
        public ObservableCollection<TaskGroup> Groups => _projection.Groups;

        /// <summary>Thumbnail-picker window list of a group: the
        /// <c>ThumbnailPickerFilter</c> view of the catalog (v2.64), mapped
        /// to the same <see cref="TaskWindow"/> instances the button list
        /// binds to. Snapshot at call time (see TaskProjection.PickerWindows).</summary>
        public IReadOnlyList<TaskWindow> PickerWindows(TaskGroup group)
            => _projection.PickerWindows(group);

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
            // Choke point: one malformed or hostile event must never take
            // down the pump or the dispatcher callback it runs on. The next
            // event (or the safety-net refresh) re-syncs the truth from the
            // core.
            try
            {
                HandleCoreEvent(e);
            }
            catch (Exception ex)
            {
                _bridge.Log("core event " + e.EventType + " failed: " + ex.Message);
            }
        }

        private void HandleCoreEvent(CoreEventArgs e)
        {
            switch (e.EventType)
            {
                case CoreEvent.WindowAdded:
                    // Discovery is not resolution: a pending entry exists at
                    // once, its identity completes asynchronously.
                    _catalog.Discover(e.A);
                    RefreshWindows();
                    break;

                case CoreEvent.WindowRemoved:
                    // Notify-first removal in the model (listeners can still
                    // inspect the departing task), then the mirror pass.
                    _catalog.RemoveEntry(e.A);
                    RefreshWindows();
                    break;

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
        public void RefreshWindows()
        {
            // Choke point of every refresh caller (core pump, safety-net tick,
            // UI actions): a failure is logged and swallowed, the next refresh
            // re-syncs the truth from the core.
            try
            {
                RefreshWindowsCore();
            }
            catch (Exception ex)
            {
                _bridge.Log("refresh failed: " + ex.Message);
            }
        }

        private void RefreshWindowsCore()
        {
            IReadOnlyList<W7TWindowInfo> windows = _bridge.GetWindows();
            List<PinInfo> pins = _pinsCache ??= LoadPinsFromCore();

            // v2.62-alpha (G3/G4): the user's own grouping configuration,
            // read at most once per refresh and only when the project switch
            // is on. Absent value => null => exactly the current behaviour.
            // It is VIEW policy (per-window buttons, group icon); identity
            // stays on the model entries regardless. "Never group"
            // (TaskbarGlomLevel 0) and the TaskbarExceptionsIcons list make
            // each window its own button: the group key becomes per-window (a
            // synthetic identity that never matches a pin and survives while
            // the window lives), while the real AppId stays on the TaskWindow
            // for the group commands (see EffectiveAppId).
            var groupCfg = RetroBar.Utilities.Settings.Instance.TaskbarGroupingPolicy
                ? TaskbarSettings.Read() : null;
            var iconCfg = RetroBar.Utilities.Settings.Instance.TaskbarGroupingPolicy
                ? GroupIconPolicy.Read() : null;
            var policy = new ProjectionPolicy(groupCfg, iconCfg);

            // Shared model: entries (ghost-aware), pin claiming (a pin and a
            // group are the same app by AppUserModelID OR executable path -
            // without the double criterion the same app shows up several
            // times), window-to-group assignment with ONE membership sync per
            // group so several native AppIds keep one button (the v2.29 rule:
            // two Chrome profiles = one button), and empty-group GC where
            // pinned groups always survive. The historical criteria and order
            // are preserved inside the catalog.
            _catalog.Reconcile(windows, pins, policy);

            // Presentation: mirror the model onto the bindable buttons, in
            // place (no rebuild: no flicker, hover state preserved).
            _projection.Sync(policy);

            // 3) v2.20: ordine Superbar = pin nell'ordine della cartella,
            //    poi le app solo-running (senza ricreare i bottoni).
            // (due .lnk possono dichiarare lo stesso AppUserModelID:
            // senza deduplica la lista desired sforava e Move() lanciava
            // ArgumentOutOfRangeException al primo evento del core.)
            // v1.21.7: the variable is no longer read-only, because step 3b
            // applies the user's ordering layer to it.
            List<TaskGroup> desired = new List<TaskGroup>(Groups.Count);
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
                TaskGroup? g = Groups.FirstOrDefault(x =>
                    TaskMatcher.PinMatches(pin, x.AppId, x.ExePath, x.IsPinned));
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

        /// <summary>v2.27: la barra ha appena avviato questo pin: per i
        /// prossimi secondi le finestre di explorer.exe senza altra
        /// corrispondenza vengono agganciate a questo gruppo.</summary>
        public void NotePinLaunch(TaskGroup group)
        {
            if (group == null || string.IsNullOrEmpty(group.AppId))
            {
                return;
            }

            // The matcher keeps the affinity window (model identity,
            // not view state).
            _catalog.Matcher.NoteRecentLaunch(group.AppId);
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
                try
                {
                    var icon = PinReader.ReadIcon(pn.LnkPath ?? string.Empty,
                                                  pn.Target ?? string.Empty);
                    if (icon != null)
                    {
                        _icons.Put(AppIconCache.PinKey(pn.LnkPath ?? string.Empty), icon);
                    }

                    list.Add(new PinInfo
                    {
                        AppId = pn.Identity ?? string.Empty,
                        LnkPath = pn.LnkPath ?? string.Empty,
                        TargetPath = pn.Target ?? string.Empty,
                        Icon = icon,
                    });
                }
                catch (Exception)
                {
                }
            }
            return list;
        }

        private void OnOurPinsChanged(object? sender, EventArgs e)
        {
            try
            {
                Dispatcher.CurrentDispatcher.BeginInvoke(new Action(InvalidatePins));
            }
            catch (Exception)
            {
            }
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

                    /* v1.21.39: servono al feedback dei fumetti: i codici
                     * NIN_BALLOON* viaggiano su uCallbackMessage verso la
                     * finestra proprietaria, e da NOTIFYICON_VERSION_4 in poi
                     * con una disposizione diversa di wParam/lParam. */
                    model.CallbackMessage = info.CallbackMessage;
                    model.Version = info.Version;

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
            _bridge.MinimizeGroup(EffectiveAppId(group));
            RefreshWindows();
        }

        public void CloseGroup(TaskGroup group)
        {
            _bridge.CloseGroup(EffectiveAppId(group));
            RefreshWindows();
        }

        /* v2.62-alpha (G3/G4): the per-window groups carry a synthetic
         * AppId (so the pipeline never merges them): the real AppId lives
         * on the windows, and that is what the native group commands need.
         * Every other group is untouched: the behaviour is byte-identical. */
        private static string EffectiveAppId(TaskGroup group)
        {
            if (group.AppId.StartsWith("w7t:win:", StringComparison.Ordinal))
            {
                TaskWindow? first = group.Windows.FirstOrDefault();
                if (first != null && !string.IsNullOrEmpty(first.AppId))
                {
                    return first.AppId;
                }
            }
            return group.AppId;
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

            _projection.Dispose();
            _resolver.Dispose();
            _catalog.Dispose();
            _refreshTimer.Stop();
            _bridge.CoreEventRaised -= OnCoreEvent;
            StartMenuStore.PinsChanged -= OnOurPinsChanged;
            Clock.Dispose();
        }
    }
}
