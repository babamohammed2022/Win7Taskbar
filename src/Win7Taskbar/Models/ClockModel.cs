// Win7Taskbar - orologio
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
using System.ComponentModel;
using System.Windows.Threading;
using RetroBar.Utilities;

namespace Win7Taskbar.Models
{
    /// <summary>
    /// Sorgente dati dell'orologio.
    ///
    /// Il template ClockTemplateKey del tema Windows7.xaml lega
    /// <c>Path=Now</c> con StringFormat "t" (ora), "dddd" (giorno della
    /// settimana) e "d" (data). Espone quindi <see cref="Now"/> come
    /// DateTime e lascia la formattazione al tema.
    ///
    /// Requisito: aggiornamento al secondo quando "mostra secondi" e' attivo,
    /// al minuto altrimenti. Il timer si riallinea al bordo esatto del
    /// secondo/minuto per non accumulare deriva.
    /// </summary>
    public sealed class ClockModel : INotifyPropertyChanged, IDisposable
    {
        private readonly DispatcherTimer _timer;
        private DateTime _now = DateTime.Now;
        private bool _showSeconds;
        private bool _disposed;

        public event PropertyChangedEventHandler? PropertyChanged;

        public ClockModel()
        {
            _showSeconds = Settings.Instance.ShowClockSeconds;

            _timer = new DispatcherTimer(DispatcherPriority.Render);
            _timer.Tick += OnTick;

            Settings.Instance.PropertyChanged += OnSettingsChanged;

            UpdateNow();
            ScheduleNextTick();
            _timer.Start();
        }

        /// <summary>Istante corrente. Il tema lo formatta con StringFormat.</summary>
        public DateTime Now
        {
            get => _now;
            private set
            {
                if (_now == value)
                {
                    return;
                }
                _now = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Now)));
            }
        }

        /// <summary>
        /// Formato orario coerente con l'opzione "mostra secondi".
        /// Utile a chi voglia un binding diretto senza StringFormat.
        /// </summary>
        public string TimeText => _now.ToString(_showSeconds ? "HH:mm:ss" : "HH:mm");

        /// <summary>Data in formato gg/mm/aaaa, come nella specifica.</summary>
        public string DateText => _now.ToString("dd/MM/yyyy");

        public bool ShowSeconds
        {
            get => _showSeconds;
            set
            {
                if (_showSeconds == value)
                {
                    return;
                }
                _showSeconds = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(ShowSeconds)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TimeText)));
                ScheduleNextTick();
            }
        }

        private void OnSettingsChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(Settings.ShowClockSeconds))
            {
                ShowSeconds = Settings.Instance.ShowClockSeconds;
            }
        }

        private void OnTick(object? sender, EventArgs e)
        {
            UpdateNow();
            ScheduleNextTick();
        }

        private void UpdateNow()
        {
            DateTime current = DateTime.Now;

            // Senza secondi cambia solo al minuto: azzeriamo la parte inferiore
            // per non notificare aggiornamenti inutili alla UI.
            Now = _showSeconds
                ? new DateTime(current.Year, current.Month, current.Day,
                               current.Hour, current.Minute, current.Second, current.Kind)
                : new DateTime(current.Year, current.Month, current.Day,
                               current.Hour, current.Minute, 0, current.Kind);

            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TimeText)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(DateText)));
        }

        /// <summary>
        /// Programma il tick successivo sul confine esatto del secondo o del
        /// minuto, cosi' l'ora cambia quando deve e non con ritardo crescente.
        /// </summary>
        private void ScheduleNextTick()
        {
            DateTime current = DateTime.Now;
            TimeSpan delay;

            if (_showSeconds)
            {
                delay = TimeSpan.FromMilliseconds(1000 - current.Millisecond);
            }
            else
            {
                delay = TimeSpan.FromMilliseconds(
                    (60 - current.Second) * 1000 - current.Millisecond);
            }

            if (delay < TimeSpan.FromMilliseconds(50))
            {
                delay = TimeSpan.FromMilliseconds(50);
            }

            _timer.Interval = delay;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;

            _timer.Stop();
            _timer.Tick -= OnTick;
            Settings.Instance.PropertyChanged -= OnSettingsChanged;
        }
    }
}
