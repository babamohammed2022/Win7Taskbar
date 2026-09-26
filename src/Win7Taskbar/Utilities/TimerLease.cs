// Win7Taskbar - timer usa e getta (RAII) per i timer brevi della barra
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
// v2.46: RAII per i DispatcherTimer.
//
// Un DispatcherTimer in esecuzione e' radicato dal dispatcher: finche' non lo
// si ferma resta vivo, e se il suo gestore e' una lambda cattura un pezzo di
// finestra. Nella barra questo ha un costo reale: i timer "one shot" lasciati
// in volo (il watchdog del menu Start, il ritardo di comparsa delle anteprime)
// continuavano a scattare anche quando il pulsante che li aveva armati non
// c'entrava piu' nulla - ed e' una delle cause del menu Start che si riapriva
// da solo.
//
// Con questa classe un timer si crea dentro un blocco e si smonta uscendone:
//   using var lease = new TimerLease(400, OnTick);
//   lease.Start();
// Il Dispose ferma il timer e STACCA il gestore, quindi non resta nessun
// riferimento al delegate: da quel momento l'oggetto e' spazzabile dal GC.
// ============================================================================

using System;
using System.Windows.Threading;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Involucro <see cref="IDisposable"/> attorno a un
    /// <see cref="DispatcherTimer"/>: il timer smette di scattare e il gestore
    /// viene staccato alla <see cref="Dispose"/>.
    /// </summary>
    internal sealed class TimerLease : IDisposable
    {
        private readonly DispatcherTimer _timer;
        private readonly EventHandler _handler;
        private bool _disposed;

        /// <summary>
        /// Crea il timer (fermo) e vi collega il gestore.
        /// </summary>
        /// <param name="intervalMs">Periodo in millisecondi.</param>
        /// <param name="onTick">Gestore del tick.</param>
        /// <exception cref="ArgumentNullException">Se manca il gestore.</exception>
        public TimerLease(int intervalMs, EventHandler onTick)
        {
            if (onTick == null)
            {
                throw new ArgumentNullException(nameof(onTick));
            }

            _handler = onTick;
            _timer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(intervalMs < 1 ? 1 : intervalMs)
            };
            _timer.Tick += _handler;
        }

        /// <summary>Vero se il timer e' in esecuzione.</summary>
        public bool IsEnabled => !_disposed && _timer.IsEnabled;

        /// <summary>Vero dopo la <see cref="Dispose"/>.</summary>
        public bool IsDisposed => _disposed;

        /// <summary>Avvia (o riavvia da capo) il conto alla rovescia.</summary>
        public void Start()
        {
            if (_disposed)
            {
                return;
            }

            _timer.Stop();
            _timer.Start();
        }

        /// <summary>Ferma il timer lasciandolo riutilizzabile.</summary>
        public void Stop()
        {
            if (_disposed)
            {
                return;
            }

            _timer.Stop();
        }

        /// <summary>Alias di <see cref="Start"/>: si riparte da zero.</summary>
        public void Restart() => Start();

        /// <summary>Ferma il timer e stacca il gestore.</summary>
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            _timer.Stop();
            _timer.Tick -= _handler;
        }
    }
}
