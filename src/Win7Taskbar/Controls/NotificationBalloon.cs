// Win7Taskbar - coda dei fumetti di notifica (modello RetroBar/ManagedShell)
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
// along with this program.  If not, see <https://www.gnu.org/licenses/>
//
// ============================================================================
// PERCHE' QUESTO FILE ESISTE
//
// Sostituisce il percorso dei fumetti usato fino a v1.21.41 con lo schema di
// RetroBar, che e' quello di ManagedShell:
//
//   1. la notifica arriva come OGGETTO (NotificationBalloon), non come "ultimo
//      valore" letto dal core: due notifiche ravvicinate restano due notifiche;
//   2. la coda alza NotificationBalloonShown e chi mostra il fumetto marca
//      Handled; una notifica NON gestita entra nelle MissedNotifications
//      dell'icona che l'ha generata (ManagedShell:
//      NotificationArea.handleBalloonData -> NotifyIcon.TriggerNotificationBalloon
//      -> MissedNotifications.Add);
//   3. a video ce n'e' uno solo per volta (Windows ne mostra uno): le altre
//      aspettano in una coda FIFO limitata e vengono mostrate alla chiusura di
//      quella visibile, invece di essere scartate;
//   4. l'icona in overflow viene promossa in barra per la durata del fumetto
//      (durata + 500 ms per l'animazione), come fa RetroBar.
//
// Tutto il percorso e' dentro try/catch e le risorse hanno un proprietario:
// il timer di unpromote e' un TimerLease (Utilities/TimerLease.cs, il RAII per
// i DispatcherTimer gia' in uso nella barra) e la coda si svuota alla Dispose.
// Se qualcosa solleva, Submit restituisce false e il chiamante
// (TaskbarWindow.OnBalloonReceived) ripiega su ShowBalloonLegacy: il
// comportamento della v1.21.41, che da percorso unico diventa un caso
// rarissimo.
// ============================================================================

using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;
using Win7Taskbar.Models;
using Win7Taskbar.Utilities;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Superficie della barra di cui la coda ha bisogno. Esiste per tenere la
    /// coda confinata: da qui non si raggiungono i flyout di rete, batteria,
    /// volume o orologio, ne' le anteprime, ne' l'overflow.
    /// </summary>
    internal interface IBalloonSurface
    {
        /// <summary>Contenitore visivo dell'icona, se esiste ed e' presentabile.</summary>
        UIElement? ResolveAnchor(TrayIconModel? model);

        /// <summary>Icona del modello che ha generato la notifica, se c'e'.</summary>
        TrayIconModel? FindModel(BalloonNotification balloon);

        /// <summary>
        /// Icona dell'applicazione per i fumetti NIIF_USER (ripiego legacy su
        /// hIcon documentato da Microsoft); null se non disponibile.
        /// </summary>
        ImageSource? GetUserIcon(TrayIconModel? model);

        /// <summary>Callback NIN_BALLOON* verso l'applicazione, null se non registrata.</summary>
        Action<uint>? BuildFeedback(TrayIconModel? model);

        /// <summary>Promozione/revoca dell'icona di overflow per la durata del fumetto.</summary>
        void Promote(TrayIconModel model);

        void Unpromote(TrayIconModel model);

        /// <summary>Riga di diagnostica. Non deve mai sollevare.</summary>
        void Log(string message);
    }

    /// <summary>
    /// Una notifica a fumetto, con il segno di "gestita" di ManagedShell: chi la
    /// mostra a video imposta <see cref="Handled"/>, e solo una notifica non
    /// gestita entra nelle <c>MissedNotifications</c> della sua icona.
    ///
    /// E' public perche' <c>TrayIconModel</c> (public) espone la lista delle
    /// missed: un tipo meno accessibile del membro che lo usa non compila
    /// (CS0053). Il resto del meccanismo - coda, evento, IBalloonSurface -
    /// resta interno all'assembly.
    /// </summary>
    public sealed class NotificationBalloon
    {
        public NotificationBalloon(BalloonNotification data, TrayIconModel? model)
        {
            Data = data;
            Model = model;
            Received = DateTime.Now;
        }

        /// <summary>Dati arrivati dal core (testo, flag NIIF_*, timeout).</summary>
        public BalloonNotification Data { get; }

        /// <summary>Icona che l'ha generata; null se non e' (piu') nel modello.</summary>
        public TrayIconModel? Model { get; }

        /// <summary>Istante di ricezione: le piu' vecchie scadono per prime.</summary>
        public DateTime Received { get; }

        /// <summary>True quando la notifica e' stata mostrata (o presa in carico).</summary>
        public bool Handled { get; set; }
    }

    /// <summary>
    /// Argomento dell'evento, stessa forma di
    /// <c>ManagedShell.WindowsTray.NotificationBalloonEventArgs</c>.
    /// </summary>
    internal sealed class NotificationBalloonEventArgs : EventArgs
    {
        public NotificationBalloonEventArgs(NotificationBalloon balloon)
        {
            Balloon = balloon ?? throw new ArgumentNullException(nameof(balloon));
        }

        public NotificationBalloon Balloon { get; }

        public bool Handled
        {
            get => Balloon.Handled;
            set => Balloon.Handled = value;
        }
    }

    /// <summary>
    /// Percorso primario dei fumetti. Una istanza per barra, creata al primo
    /// fumetto e smontata alla chiusura della finestra.
    /// </summary>
    internal sealed class BalloonQueue : IDisposable
    {
        /// <summary>
        /// Quante notifiche possono aspettare. Oltre si scarta la piu' vecchia
        /// (drop-oldest), la stessa politica di CoreState::QueueEvent nel core
        /// (native/src/Common.cpp:66-77): una raffica non cresce all'infinito.
        /// Lo stesso limite vale per le MissedNotifications di una icona.
        /// </summary>
        private const int MaxPending = 16;

        /// <summary>
        /// Eta' oltre la quale una notifica in attesa non ha piu' senso: chi la
        /// manda si aspetta di essere visto subito, non minuti dopo.
        /// </summary>
        private static readonly TimeSpan MaxAge = TimeSpan.FromMinutes(2);

        /// <summary>Ritardo di revoca della promozione, come RetroBar.</summary>
        private static readonly TimeSpan UnpromoteGrace = TimeSpan.FromMilliseconds(500);

        private readonly IBalloonSurface _surface;
        private readonly BalloonHost _host;
        private readonly Queue<NotificationBalloon> _pending = new();
        private readonly object _gate = new();

        /* Generazione delle risoluzioni differite: il contenitore di un'icona
         * appena promossa nasce alla passata di layout successiva, quindi la
         * risoluzione dell'ancora viene ripetuta una volta con
         * DispatcherPriority.Loaded (il pattern "missed notifications" di
         * RetroBar). Ogni nuovo fumetto e ogni Dispose la incrementano, cosi' il
         * lavoro differito obsoleto se ne accorge e non tocca nulla. */
        private int _generation;

        private NotificationBalloon? _visible;
        private TrayIconModel? _promotedModel;
        private TimerLease? _unpromoteTimer;
        private bool _disposed;

        /// <summary>Alzato per ogni notifica, come NotificationBalloonShown.</summary>
        public event EventHandler<NotificationBalloonEventArgs>? NotificationBalloonShown;

        public BalloonQueue(IBalloonSurface surface, BalloonHost host)
        {
            _surface = surface ?? throw new ArgumentNullException(nameof(surface));
            _host = host ?? throw new ArgumentNullException(nameof(host));
        }

        /// <summary>Quante notifiche stanno aspettando il proprio turno.</summary>
        public int PendingCount
        {
            get { lock (_gate) { return _pending.Count; } }
        }

        /// <summary>
        /// Consegna una notifica al percorso RetroBar.
        /// </summary>
        /// <returns>
        /// True se il percorso RetroBar l'ha presa in carico (mostrata, messa in
        /// coda, registrata come missed o riconosciuta come rimozione). False
        /// solo se qualcosa ha sollevato: allora il chiamante usa il ripiego.
        /// </returns>
        public bool Submit(BalloonNotification data)
        {
            if (_disposed)
            {
                return false;
            }

            try
            {
                /* Rimozione documentata: NIF_INFO con szInfo vuoto chiede di
                 * togliere il fumetto (ns-shellapi-notifyicondataw: "To remove
                 * the balloon notification from the UI ... set szInfo to an
                 * empty string"). Non e' una notifica da mostrare: chiude il
                 * visibile e scarta l'attesa di quella stessa icona. */
                if (IsRemoval(data))
                {
                    CancelPendingFor(data.OwnerHwnd, data.Uid);
                    DiscardVisibleIf(data.OwnerHwnd, data.Uid);
                    return true;
                }

                var balloon = new NotificationBalloon(data, _surface.FindModel(data));
                Raise(balloon);

                if (!balloon.Handled)
                {
                    Remember(balloon);
                }

                lock (_gate)
                {
                    if (_visible != null)
                    {
                        EnqueuePendingLocked(balloon);
                        return true;
                    }

                    /* Riservato subito: due notifiche nella stessa passata non
                     * aprono due fumetti. */
                    _visible = balloon;
                }

                if (!ShowNow(balloon))
                {
                    lock (_gate) { _visible = null; }
                    return false;
                }

                return true;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "submit");
                return false;
            }
        }

        /// <summary>
        /// Chiude tutto: fumetto visibile, attesa e promozione. Dopo, Submit
        /// restituisce false e il chiamante ripiega.
        /// </summary>
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            _generation++;

            ReleasePromotion();

            lock (_gate)
            {
                _pending.Clear();
                _visible = null;
            }

            try
            {
                _host.Hide();
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "dispose/hide");
            }
        }

        // ---------------------------------------------------------------
        //  Evento e missed notifications
        // ---------------------------------------------------------------

        private void Raise(NotificationBalloon balloon)
        {
            var handler = NotificationBalloonShown;
            if (handler == null)
            {
                return;
            }

            try
            {
                handler(this, new NotificationBalloonEventArgs(balloon));
            }
            catch (Exception ex)
            {
                /* Un sottoscrittore che solleva non deve ne' perdere la
                 * notifica ne' abbattere la barra: la si tratta come non
                 * gestita, quindi finisce nelle missed della sua icona. */
                DiagnosticLogger.WriteException("BALLOON", ex, "shown-handler");
            }
        }

        /// <summary>
        /// ManagedShell: NotifyIcon.TriggerNotificationBalloon aggiunge alla
        /// ObservableCollection dell'icona quando la notifica non e' gestita.
        /// Anche qui la lista vive nel modello dell'icona, cosi' sopravvive al
        /// singolo fumetto e la si puo' riproporre quando l'icona torna visibile.
        /// </summary>
        private static void Remember(NotificationBalloon balloon)
        {
            var missed = balloon.Model?.MissedNotifications;
            if (missed == null)
            {
                return;
            }

            missed.Add(balloon);
            while (missed.Count > MaxPending)
            {
                missed.RemoveAt(0);
            }
        }

        // ---------------------------------------------------------------
        //  Coda
        // ---------------------------------------------------------------

        private void EnqueuePendingLocked(NotificationBalloon balloon)
        {
            while (_pending.Count >= MaxPending)
            {
                _pending.Dequeue();      /* drop-oldest */
            }

            _pending.Enqueue(balloon);
        }

        private void CancelPendingFor(ulong ownerHwnd, uint uid)
        {
            lock (_gate)
            {
                if (_pending.Count == 0)
                {
                    return;
                }

                var kept = new List<NotificationBalloon>(_pending.Count);
                while (_pending.Count > 0)
                {
                    var item = _pending.Dequeue();
                    if (item.Data.OwnerHwnd != ownerHwnd || item.Data.Uid != uid)
                    {
                        kept.Add(item);
                    }
                }

                foreach (var item in kept)
                {
                    _pending.Enqueue(item);
                }
            }
        }

        private NotificationBalloon? TakeNextPending()
        {
            lock (_gate)
            {
                while (_pending.Count > 0)
                {
                    var item = _pending.Dequeue();
                    if (DateTime.Now - item.Received <= MaxAge)
                    {
                        return item;
                    }
                }

                return null;
            }
        }

        // ---------------------------------------------------------------
        //  Visualizzazione
        // ---------------------------------------------------------------

        private bool ShowNow(NotificationBalloon balloon)
        {
            try
            {
                _generation++;
                int generation = _generation;

                var model = balloon.Model;

                /* Icona in overflow: promossa in barra per la durata del
                 * fumetto, cosi' la puntina ha un'icona vera da indicare.
                 * NIS_HIDDEN resta fuori: e' l'applicazione a chiedere di non
                 * essere mostrata. */
                if (model != null && !model.IsHidden && !model.IsPinned)
                {
                    Promote(model);
                }

                var anchor = _surface.ResolveAnchor(model);

                if (model != null && !model.IsHidden && anchor == null)
                {
                    /* Il contenitore non e' ancora stato generato (icona appena
                     * aggiunta o appena promossa): si riprova dopo la prossima
                     * passata di layout invece di rinunciare all'ancora. */
                    Dispatcher.CurrentDispatcher.BeginInvoke(new Action(() =>
                    {
                        if (_disposed || generation != _generation)
                        {
                            return;
                        }

                        try
                        {
                            Open(balloon, _surface.ResolveAnchor(model));
                        }
                        catch (Exception ex)
                        {
                            DiagnosticLogger.WriteException("BALLOON", ex, "deferred-open");
                            /* Come nel ramo sincrono: si libera lo slot e si
                             * serve la prossima, altrimenti l'attesa resterebbe
                             * bloccata su una notifica che non si aprira' mai.
                             * Se invece Open e' riuscito il fumetto e' a video e
                             * sara' il suo Closed a chiamare Advance. */
                            CloseVisible(balloon);
                            Advance(balloon);
                        }
                    }), DispatcherPriority.Loaded);

                    /* Gia' segnata come visibile in Submit: la notifica e'
                     * presa in carico, non va ne' persa ne' al ripiego. */
                    return true;
                }

                Open(balloon, anchor);
                return true;
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "show");
                CloseVisible(balloon);
                /* Lo slot e' liberato e la notifica resta non gestita (quindi
                 * nelle missed della sua icona): si serve comunque la prossima,
                 * altrimenti l'attesa si fermerebbe qui per sempre. */
                Advance(balloon);
                return false;
            }
        }

        private void Open(NotificationBalloon balloon, UIElement? anchor)
        {
            var data = balloon.Data;
            var model = balloon.Model;

            TimeSpan shown = _host.Show(
                data.Title,
                data.Text,
                data.InfoFlags,
                data.Timeout,
                anchor,
                _surface.GetUserIcon(model),
                _surface.BuildFeedback(model));

            balloon.Handled = true;

            if (shown > TimeSpan.Zero)
            {
                ScheduleUnpromote(shown);
                _host.Closed += OnVisibleClosed;
            }

            /* shown == TimeSpan.Zero significa che il fumetto non si e' aperto
             * (tema non pronto, ancora ostile): non c'e' nessun Closed da
             * aspettare. Chi ha chiamato Open serve la coda subito dopo, in ogni
             * caso, cosi' il percorso di uscita e' uno solo. */
        }

        private void OnVisibleClosed(object? sender, EventArgs e)
        {
            if (sender is BalloonHost host)
            {
                host.Closed -= OnVisibleClosed;
            }

            /* expected == null: chiusura arrivata dal fumetto stesso, che e'
             * sicuramente quello visibile. */
            Advance(null);
        }

        /// <summary>
        /// Libera lo slot del visibile e serve la prossima notifica in attesa.
        /// </summary>
        /// <param name="expected">
        /// Quando la chiusura non arriva dal fumetto (rimozione chiesta
        /// dall'applicazione) il chiamante passa la notifica che sta chiudendo:
        /// se nel frattempo lo slot e' gia' stato preso da un'altra notifica,
        /// qui non si fa nulla invece di chiuderne una per errore.
        /// </param>
        private void Advance(NotificationBalloon? expected)
        {
            if (_disposed)
            {
                return;
            }

            lock (_gate)
            {
                if (expected != null && !ReferenceEquals(_visible, expected))
                {
                    return;
                }

                _visible = null;
            }

            ReleasePromotion();

            var next = TakeNextPending();
            if (next == null)
            {
                return;
            }

            lock (_gate)
            {
                _visible = next;
            }

            if (!ShowNow(next))
            {
                lock (_gate) { _visible = null; }
                _surface.Log("balloon: notifica in attesa non mostrabile, ripiego");
            }
        }

        private void DiscardVisibleIf(ulong ownerHwnd, uint uid)
        {
            NotificationBalloon? visible;
            lock (_gate)
            {
                visible = _visible;
            }

            if (visible == null ||
                visible.Data.OwnerHwnd != ownerHwnd ||
                visible.Data.Uid != uid)
            {
                return;
            }

            try
            {
                _host.Closed -= OnVisibleClosed;
                _host.Hide();
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "removal-hide");
            }

            /* BalloonHost.Hide chiude il popup senza passare da
             * NotifyBalloon.Dismiss, quindi l'evento Closed non arriva: lo slot
             * si libera qui, in modo esplicito. */
            Advance(visible);
        }

        private void CloseVisible(NotificationBalloon expected)
        {
            lock (_gate)
            {
                /* Solo se lo slot e' ancora suo: nel frattempo Open potrebbe
                 * aver aperto un fumetto che resta a video, e azzerare lo slot
                 * lo farebbe restare aperto senza nessuno che lo chiuda. */
                if (ReferenceEquals(_visible, expected))
                {
                    _visible = null;
                }
            }

            try
            {
                _host.Closed -= OnVisibleClosed;
                _host.Hide();
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "close-visible");
            }

            /* Nessuna rimessa in coda: una notifica che non si riesce a mostrare
             * resta con Handled = false, quindi e' gia' finita nelle
             * MissedNotifications della sua icona (Submit -> Remember). E' la
             * semantica di ManagedShell, e evita di riprovare all'infinito una
             * notifica che ha appena fatto fallire l'apertura. */
        }

        // ---------------------------------------------------------------
        //  Promozione dell'icona (timer in RAII)
        // ---------------------------------------------------------------

        private void Promote(TrayIconModel model)
        {
            if (ReferenceEquals(_promotedModel, model))
            {
                return;
            }

            ReleasePromotion();

            try
            {
                _surface.Promote(model);
                _promotedModel = model;
            }
            catch (Exception ex)
            {
                /* La promozione e' un miglioramento, non un requisito: senza,
                 * il fumetto si ancora all'area di notifica. */
                DiagnosticLogger.WriteException("BALLOON", ex, "promote");
                _promotedModel = null;
            }
        }

        private void ScheduleUnpromote(TimeSpan shownFor)
        {
            if (_promotedModel == null)
            {
                return;
            }

            var model = _promotedModel;
            double totalMs = shownFor.TotalMilliseconds + UnpromoteGrace.TotalMilliseconds;
            int graceMs = totalMs > int.MaxValue ? int.MaxValue : (int)Math.Max(1, totalMs);

            try
            {
                _unpromoteTimer?.Dispose();
                var timer = new TimerLease(graceMs, (_, _) =>
                {
                    if (ReferenceEquals(_promotedModel, model))
                    {
                        ReleasePromotion();
                    }
                });

                _unpromoteTimer = timer;
                timer.Start();
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "unpromote-timer");
                ReleasePromotion();
            }
        }

        private void ReleasePromotion()
        {
            try
            {
                _unpromoteTimer?.Dispose();
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "unpromote-dispose");
            }
            finally
            {
                _unpromoteTimer = null;
            }

            var model = _promotedModel;
            _promotedModel = null;

            if (model == null)
            {
                return;
            }

            try
            {
                _surface.Unpromote(model);
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BALLOON", ex, "unpromote");
            }
        }

        private static bool IsRemoval(BalloonNotification data) =>
            string.IsNullOrWhiteSpace(data.Title) && string.IsNullOrWhiteSpace(data.Text);
    }
}
