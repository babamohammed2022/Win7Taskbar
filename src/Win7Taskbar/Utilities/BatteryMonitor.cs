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
            _pollTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromSeconds(30)
            };
            _pollTimer.Tick += PollTimer_Tick;
        }

        public void Start()
        {
            DiagnosticLogger.Write("BATTERY", "start");
            CheckPowerStatus(true);
            _pollTimer.Start();

            try
            {
                if (GetSystemPowerStatus(out SYSTEM_POWER_STATUS sps) && sps.ACLineStatus == 0)
                {
                    DiagnosticLogger.Write("BATTERY", $"startup-on-battery percent={sps.BatteryLifePercent};flag={sps.BatteryFlag}");
                    var early = new DispatcherTimer(DispatcherPriority.Background)
                    {
                        Interval = TimeSpan.FromSeconds(1.5)
                    };
                    int ticks = 0;
                    early.Tick += (_, _) =>
                    {
                        CheckPowerStatus(true);
                        if (++ticks >= 3)
                            early.Stop();
                        else
                            early.Interval = TimeSpan.FromSeconds(2.5);
                    };
                    early.Start();
                }
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BATTERY_START_ERROR", ex);
            }
        }

        public void Stop()
        {
            _pollTimer.Stop();
            DiagnosticLogger.Write("BATTERY", "stop");
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
                {
                    int error = Marshal.GetLastWin32Error();
                    DiagnosticLogger.Write("BATTERY_ERROR", $"GetSystemPowerStatus failed;error={error};force={force}");
                    return;
                }

                bool changed = !_hasLast || force ||
                    _lastStatus.ACLineStatus != current.ACLineStatus ||
                    _lastStatus.BatteryFlag != current.BatteryFlag ||
                    _lastStatus.BatteryLifePercent != current.BatteryLifePercent;

                DiagnosticLogger.Write("BATTERY", $"check;force={force};changed={changed};ac={current.ACLineStatus};percent={current.BatteryLifePercent};flag={current.BatteryFlag}");

                if (changed)
                {
                    _lastStatus = current;
                    _hasLast = true;
                    DiagnosticLogger.Write("BATTERY_REFRESH", $"forcing icon refresh;ac={current.ACLineStatus};percent={current.BatteryLifePercent};flag={current.BatteryFlag}");
                    _onBatteryChanged?.Invoke(current);
                }
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("BATTERY_ERROR", ex, $"force={force}");
            }
        }

        public static SYSTEM_POWER_STATUS GetCurrentStatus()
        {
            if (!GetSystemPowerStatus(out SYSTEM_POWER_STATUS sps))
            {
                int error = Marshal.GetLastWin32Error();
                DiagnosticLogger.Write("BATTERY_ERROR", $"GetCurrentStatus failed;error={error}");
            }
            else
            {
                DiagnosticLogger.Write("BATTERY", $"GetCurrentStatus;ac={sps.ACLineStatus};percent={sps.BatteryLifePercent};flag={sps.BatteryFlag}");
            }
            return sps;
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
