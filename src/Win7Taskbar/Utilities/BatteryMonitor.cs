// Win7Taskbar - Battery monitor using public APIs
// English: Uses GetSystemPowerStatus and power setting notifications to detect battery changes and force icon refresh
// Italiano: Usa GetSystemPowerStatus e notifiche per rilevare cambi batteria e forzare refresh icona
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using System;
using System.Runtime.InteropServices;
using System.Windows.Threading;

namespace Win7Taskbar.Utilities
{
    /// <summary>
    /// Monitors battery status using public Windows APIs.
    /// Triggers refresh when AC line status or battery percentage changes.
    /// This fixes "Battery icon doesn't update when unplugged" by using GetSystemPowerStatus.
    /// English: Uses public API GetSystemPowerStatus + RegisterPowerSettingNotification
    /// Italiano: Usa API pubbliche GetSystemPowerStatus e notifiche alimentazione
    /// </summary>
    public class BatteryMonitor : IDisposable
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct SYSTEM_POWER_STATUS
        {
            public byte ACLineStatus;
            public byte BatteryFlag;
            public byte BatteryLifePercent;
            public byte SystemStatusFlag;
            public int BatteryLifeTime;
            public int BatteryFullLifeTime;
        }

        [DllImport("kernel32.dll")]
        private static extern bool GetSystemPowerStatus(out SYSTEM_POWER_STATUS lpSystemPowerStatus);

        private readonly DispatcherTimer _pollTimer;
        private SYSTEM_POWER_STATUS _lastStatus;
        private bool _hasLast;
        private readonly Action<SYSTEM_POWER_STATUS> _onBatteryChanged;

        public BatteryMonitor(Action<SYSTEM_POWER_STATUS> onBatteryChanged)
        {
            _onBatteryChanged = onBatteryChanged;

            // Reto, non motore: il vero rilevamento AC/DC e' nativo e a
            // eventi (WM_POWERBROADCAST + RegisterPowerSettingNotification
            // nel core, che fa UNA passata mirata con confronto dei soli
            // campi di stato). Qui resta un risveglio raro per rimettere
            // in pari il livello gestito se un evento si fosse perso.
            // Prima era 3 s: ripeteva il lavoro del nativo a polling.
            _pollTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromSeconds(30)
            };
            _pollTimer.Tick += PollTimer_Tick;
        }

        public void Start()
        {
            CheckPowerStatus(true);
            _pollTimer.Start();

            // v2.24: se il PC parte gia' a batteria, l'icona di Explorer
            // puo' arrivare in ritardo: ripassi conservativi a 1,5/4 s.
            try
            {
                if (GetSystemPowerStatus(out SYSTEM_POWER_STATUS sps) &&
                    sps.ACLineStatus == 0)
                {
                    var early = new DispatcherTimer(DispatcherPriority.Background)
                    {
                        Interval = TimeSpan.FromSeconds(1.5)
                    };
                    int ticks = 0;
                    early.Tick += (_, _) =>
                    {
                        CheckPowerStatus(true);
                        if (++ticks >= 3)
                        {
                            early.Stop();
                        }
                        else
                        {
                            early.Interval = TimeSpan.FromSeconds(2.5);
                        }
                    };
                    early.Start();
                }
            }
            catch { }
        }

        public void Stop()
        {
            _pollTimer.Stop();
        }

        private void PollTimer_Tick(object? sender, EventArgs e)
        {
            CheckPowerStatus(false);
        }

        public void CheckPowerStatus(bool force)
        {
            try
            {
                if (!GetSystemPowerStatus(out SYSTEM_POWER_STATUS current))
                    return;

                bool changed = false;
                if (!_hasLast || force)
                    changed = true;
                else if (_lastStatus.ACLineStatus != current.ACLineStatus ||
                         _lastStatus.BatteryFlag != current.BatteryFlag ||
                         _lastStatus.BatteryLifePercent != current.BatteryLifePercent)
                    changed = true;

                if (changed)
                {
                    _lastStatus = current;
                    _hasLast = true;
                    System.Diagnostics.Debug.WriteLine($"BatteryMonitor: AC={current.ACLineStatus} Battery={current.BatteryLifePercent}% Flag={current.BatteryFlag} -> forcing refresh");
                    _onBatteryChanged?.Invoke(current);
                }
            }
            catch { }
        }

        public static SYSTEM_POWER_STATUS GetCurrentStatus()
        {
            GetSystemPowerStatus(out SYSTEM_POWER_STATUS sps);
            return sps;
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
