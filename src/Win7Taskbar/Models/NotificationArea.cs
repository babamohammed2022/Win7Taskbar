// Win7Taskbar - area di notifica
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
using System.Runtime.CompilerServices;
using System.Windows.Media;

namespace Win7Taskbar.Models
{
    /// <summary>Una icona dell'area di notifica.</summary>
    public sealed class TrayIconModel : INotifyPropertyChanged
    {
        private string _tooltip = string.Empty;
        private ImageSource? _icon;
        private bool _isPinned = true;
        private bool _isHidden;

        public event PropertyChangedEventHandler? PropertyChanged;

        public TrayIconModel(ulong ownerHwnd, uint uid)
        {
            OwnerHwnd = ownerHwnd;
            Uid = uid;
        }

        public ulong OwnerHwnd { get; }

        public uint Uid { get; }

        /// <summary>Testo del tooltip fornito dall'applicazione proprietaria.</summary>
        public string Tooltip
        {
            get => _tooltip;
            set => SetField(ref _tooltip, value);
        }

        /// <summary>Il tema lega questa proprieta' come <c>{Binding Icon}</c>.</summary>
        public ImageSource? Icon
        {
            get => _icon;
            set => SetField(ref _icon, value);
        }

        /// <summary>
        /// True = visibile nella barra, false = nascosta nell'overflow.
        /// Il pin overlay del tema la scrive in TwoWay.
        /// </summary>
        public bool IsPinned
        {
            get => _isPinned;
            set => SetField(ref _isPinned, value);
        }

        public bool IsHidden
        {
            get => _isHidden;
            set => SetField(ref _isHidden, value);
        }

        public uint IconRevision { get; set; }

        /// <summary>
        /// uCallbackMessage registrato dall'applicazione con NIF_MESSAGE: la
        /// finestra proprietaria ci riceve gli eventi dell'icona e i codici
        /// NIN_BALLOON* del fumetto (v1.21.39). 0 = non registrato.
        /// </summary>
        public uint CallbackMessage { get; set; }

        /// <summary>
        /// Versione dell'interfaccia richiesta con NIM_SETVERSION (0/3/4):
        /// da NOTIFYICON_VERSION_4 in poi wParam/lParam dei messaggi di
        /// callback cambiano disposizione (v1.21.39).
        /// </summary>
        public uint Version { get; set; }

        /// <summary>
        /// Notifiche arrivate per questa icona che nessuno e' riuscito a
        /// mostrare (ManagedShell: NotifyIcon.MissedNotifications, riempita da
        /// TriggerNotificationBalloon quando l'evento non viene marcato come
        /// gestito). Vive nel modello e non nel fumetto, cosi' sopravvive alla
        /// singola nuvoletta: RetroBar le ripropone quando l'icona torna
        /// visibile. Il tipo sta in Controls per non separarlo dalla coda che lo
        /// produce e lo consuma.
        /// </summary>
        public ObservableCollection<Controls.NotificationBalloon> MissedNotifications { get; } =
            new();

        private void SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
        {
            if (Equals(field, value))
            {
                return;
            }
            field = value;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }

    /// <summary>
    /// Sorgente dati dell'area di notifica.
    ///
    /// Il tema Windows7.xaml lega l'ItemsControl dell'overflow a
    /// <c>NotificationArea.UnpinnedIcons</c>: il nome di questa proprieta'
    /// e' parte del contratto e non va cambiato.
    /// </summary>
    public sealed class NotificationArea : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler? PropertyChanged;

        public NotificationArea()
        {
            PinnedIcons = new ObservableCollection<TrayIconModel>();
            UnpinnedIcons = new ObservableCollection<TrayIconModel>();
            AllIcons = new ObservableCollection<TrayIconModel>();
        }

        /* v1.21.39 - PROMOZIONE DA FUMETTO (parita' Windows 7 / RetroBar).
         *
         * In Windows 7 un'icona che sta nell'overflow e genera una notifica
         * viene mostrata temporaneamente nell'area di notifica per la durata
         * del fumetto, cosi' la puntina ha un'icona vera da indicare (e' lo
         * stato "Only show notifications" della documentazione Microsoft:
         * "The icon is hidden, but if the program triggers a notification
         * balloon, it's displayed on the taskbar"). RetroBar fa esattamente
         * questo: NotificationArea_NotificationBalloonShown imposta
         * IsPinned=true e lo ripristina con un timer (timeout + 500 ms).
         *
         * Qui la promozione NON scrive IsPinned (che e' collegato al core e
         * alle preferenze salvate dell'utente): vive in un insieme a parte
         * che ComputeDesired consulta, cosi' i refresh periodici non la
         * annullano e alla scadenza l'icona torna dov'era senza effetti
         * collaterali. */
        private readonly HashSet<TrayIconModel> _balloonPromoted =
            new(ReferenceComparer.Instance);

        /// <summary>
        /// Mostra temporaneamente in barra un'icona di overflow per ancorarci
        /// il suo fumetto. Non fa nulla per icone gia' in barra o nascoste
        /// dall'applicazione (NIS_HIDDEN).
        /// </summary>
        public void PromoteForBalloon(TrayIconModel? icon)
        {
            if (icon == null || icon.IsHidden || icon.IsPinned)
            {
                return;
            }
            if (_balloonPromoted.Add(icon))
            {
                Resort();
            }
        }

        /// <summary>Annulla la promozione di un'icona (fine del fumetto).</summary>
        public void UnpromoteFromBalloon(TrayIconModel? icon)
        {
            if (icon != null && _balloonPromoted.Remove(icon))
            {
                Resort();
            }
        }

        /// <summary>Rimuove tutte le promozioni (chiusura della barra).</summary>
        public void ClearBalloonPromotions()
        {
            if (_balloonPromoted.Count > 0)
            {
                _balloonPromoted.Clear();
                Resort();
            }
        }

        /// <summary>Icone mostrate direttamente nella barra.</summary>
        public ObservableCollection<TrayIconModel> PinnedIcons { get; }

        /// <summary>Icone nascoste dietro la freccetta di overflow.</summary>
        public ObservableCollection<TrayIconModel> UnpinnedIcons { get; }

        /// <summary>Elenco completo, nell'ordine di arrivo.</summary>
        public ObservableCollection<TrayIconModel> AllIcons { get; }

        /// <summary>Ricostruisce le due viste a partire da <see cref="AllIcons"/>.</summary>
        /// <remarks>
        /// Le viste sono AGGIORNATE IN PLACE (remove/move/insert minime), non
        /// ricostruite: un Clear+Add a ogni riconciliazione avrebbe fatto
        /// ricreare il contenitore visivo di ogni icona anche quando nulla
        /// era cambiato — il "lampeggio senza motivo" che l'utente vieta.
        /// L'identita' dell'elemento (l'oggetto TrayIconModel) e' la stessa,
        /// quindi il visual sopravvive agli spostamenti e allo stato hover.
        /// </remarks>
        public void Resort()
        {
            /* v1.21.39: un'icona promossa che nel frattempo e' sparita dal
             * modello (applicazione chiusa) non ha piu' ragione di restare
             * nell'insieme delle promozioni. */
            if (_balloonPromoted.Count > 0)
            {
                _balloonPromoted.RemoveWhere(icon => !AllIcons.Contains(icon));
            }

            ApplyView(PinnedIcons, ComputeDesired(pinned: true));
            ApplyView(UnpinnedIcons, ComputeDesired(pinned: false));

            OnPropertyChanged(nameof(PinnedIcons));
            OnPropertyChanged(nameof(UnpinnedIcons));
        }

        private List<TrayIconModel> ComputeDesired(bool pinned)
        {
            var desired = new List<TrayIconModel>();
            foreach (TrayIconModel icon in AllIcons)
            {
                if (icon.IsHidden)
                {
                    // NIS_HIDDEN: l'applicazione stessa chiede di non mostrarla.
                    continue;
                }
                /* v1.21.39: un'icona promossa da un fumetto conta come
                 * appuntata per la durata della promozione (vedi sopra). */
                bool effectivePinned = icon.IsPinned || _balloonPromoted.Contains(icon);
                if (effectivePinned == pinned)
                {
                    desired.Add(icon);
                }
            }
            return desired;
        }

        private static void ApplyView(ObservableCollection<TrayIconModel> view,
                                      List<TrayIconModel> desired)
        {
            var keep = new HashSet<TrayIconModel>(desired, ReferenceComparer.Instance);

            // 1) Fuori i modelli che non ci devono piu' essere (indietro,
            //    per non scalare gli indici degli altri).
            for (int i = view.Count - 1; i >= 0; i--)
            {
                if (!keep.Contains(view[i]))
                {
                    view.RemoveAt(i);
                }
            }

            // 2) Gli altri al loro posto: solo Move reali (stable: ogni
            //    posizione si sistema una volta, i riordini non oscillano).
            for (int target = 0; target < desired.Count; target++)
            {
                TrayIconModel item = desired[target];
                int current = view.IndexOf(item);
                if (current < 0)
                {
                    view.Insert(Math.Min(target, view.Count), item);
                }
                else if (current != target)
                {
                    view.Move(current, target);
                }
            }
        }

        private sealed class ReferenceComparer : IEqualityComparer<TrayIconModel>
        {
            public static readonly ReferenceComparer Instance = new();
            public bool Equals(TrayIconModel? a, TrayIconModel? b) => ReferenceEquals(a, b);
            public int GetHashCode(TrayIconModel a) => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(a);
        }

        /// <summary>
        /// Sposta un'icona davanti o dietro a un'altra (riordino con il
        /// mouse). L'ordine vero e' quello di <see cref="AllIcons"/>: le due
        /// viste vengono poi ricostruite da <see cref="Resort"/>.
        /// </summary>
        /// <param name="source">icona trascinata</param>
        /// <param name="target">icona su cui e' stata lasciata</param>
        /// <param name="insertAfter">true per metterla dopo il bersaglio</param>
        /// <returns>true se l'ordine e' cambiato davvero</returns>
        public bool MoveIcon(TrayIconModel source, TrayIconModel target, bool insertAfter)
        {
            if (source == null || target == null || ReferenceEquals(source, target))
            {
                return false;
            }

            int from = AllIcons.IndexOf(source);
            int to = AllIcons.IndexOf(target);

            if (from < 0 || to < 0)
            {
                return false;
            }

            if (insertAfter)
            {
                to++;
            }

            // Tolta l'icona dalla posizione di partenza, tutti gli indici
            // successivi scalano di uno: l'inserimento va corretto.
            if (from < to)
            {
                to--;
            }

            if (from == to)
            {
                return false;
            }

            AllIcons.Move(from, to);
            Resort();
            return true;
        }

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
