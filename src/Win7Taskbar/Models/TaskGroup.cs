// Win7Taskbar - modelli della Superbar
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
using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows.Media;

namespace Win7Taskbar.Models
{
    /// <summary>Una singola finestra rappresentata sulla barra.</summary>
    public sealed class TaskWindow : INotifyPropertyChanged
    {
        private string _title = string.Empty;
        private bool _isActive;
        private bool _isMinimized;
        private bool _isMaximized;
        private bool _isFlashing;
        private ImageSource? _icon;
        private string _applicationName = string.Empty;

        public event PropertyChangedEventHandler? PropertyChanged;

        public TaskWindow(ulong hwnd, string appId)
        {
            Hwnd = hwnd;
            AppId = appId;
        }

        public ulong Hwnd { get; }

        /// <summary>
        /// Lo stesso handle come IntPtr: e' la forma che vogliono le API DWM
        /// usate dall'anteprima live.
        /// </summary>
        public IntPtr HandlePtr => new IntPtr(unchecked((long)Hwnd));

        public string AppId { get; }

        public string Title
        {
            get => _title;
            set => SetField(ref _title, value);
        }

        public bool IsActive
        {
            get => _isActive;
            set => SetField(ref _isActive, value);
        }

        public bool IsMinimized
        {
            get => _isMinimized;
            set => SetField(ref _isMinimized, value);
        }

        public bool IsMaximized
        {
            get => _isMaximized;
            set => SetField(ref _isMaximized, value);
        }

        /// <summary>
        /// True quando l'applicazione richiede attenzione (FlashWindowEx).
        /// In Windows 7 il pulsante corrispondente pulsa in ambra.
        /// </summary>
        public bool IsFlashing
        {
            get => _isFlashing;
            set => SetField(ref _isFlashing, value);
        }

        public ImageSource? Icon
        {
            get => _icon;
            set => SetField(ref _icon, value);
        }

        /// <summary>Localized executable metadata used only where the UI asks
        /// for an application identity; Title remains the live HWND caption.</summary>
        public string ApplicationName
        {
            get => _applicationName;
            set => SetField(ref _applicationName, value ?? string.Empty);
        }

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
    /// Gruppo di finestre della stessa applicazione: e' il bottone della
    /// Superbar. Con una sola finestra si comporta da bottone semplice, con
    /// piu' finestre espone contatore e picker.
    /// </summary>
    public sealed class TaskGroup : INotifyPropertyChanged
    {
        // FileVersionInfo reads executable resources from disk. Cache only
        // metadata (empty means unavailable), never a mutable window title.
        private static readonly ConcurrentDictionary<string, string>
            FriendlyNameCache = new(StringComparer.OrdinalIgnoreCase);

        private ImageSource? _icon;
        private bool _isFlashing;
        private bool _isActive;

        public event PropertyChangedEventHandler? PropertyChanged;

        public TaskGroup(string appId, string exePath)
        {
            AppId = appId;
            ExePath = exePath;
            Windows = new ObservableCollection<TaskWindow>();

            // UWP/Store pins can have a valid AUMID but no usable filesystem
            // target. Use the shell AppsFolder activation path as the idle
            // launch target; regular desktop pins keep their existing path.
            if (string.IsNullOrWhiteSpace(exePath) &&
                !string.IsNullOrWhiteSpace(appId) &&
                appId.IndexOf('!') > 0)
            {
                LaunchPath = $"shell:AppsFolder\\{appId}";
            }

            Windows.CollectionChanged += (_, _) =>
            {
                OnPropertyChanged(nameof(WindowCount));
                OnPropertyChanged(nameof(HasMultipleWindows));
                OnPropertyChanged(nameof(DisplayTitle));
                OnPropertyChanged(nameof(IsRunning));
            };
        }

        public string AppId { get; }

        public string ExePath { get; }

        public ObservableCollection<TaskWindow> Windows { get; }

        public int WindowCount => Windows.Count;

        /// <summary>Con piu' finestre il click apre il picker invece di attivare.</summary>
        public bool HasMultipleWindows => Windows.Count > 1;

        /// <summary>Stato Running della Superbar: almeno una finestra aperta.</summary>
        public bool IsRunning => Windows.Count > 0;

        /// <summary>True se l'app e' appuntata sulla barra (bottone anche idle).</summary>
        public bool IsPinned
        {
            get => _isPinned;
            set
            {
                if (_isPinned == value)
                {
                    return;
                }
                _isPinned = value;
                OnPropertyChanged();
            }
        }
        private bool _isPinned;

        /// <summary>Percorso .lnk/exe da avviare quando il gruppo e' idle.</summary>
        public string? LaunchPath { get; set; }

        public ImageSource? Icon
        {
            get => _icon;
            set
            {
                if (Equals(_icon, value))
                {
                    return;
                }
                _icon = value;
                OnPropertyChanged();
            }
        }

        /// <summary>True se una qualsiasi finestra del gruppo e' in primo piano.</summary>
        public bool IsActive
        {
            get => _isActive;
            set
            {
                if (_isActive == value)
                {
                    return;
                }
                _isActive = value;
                OnPropertyChanged();
            }
        }

        /// <summary>Friendly application identity used by the Superbar
        /// tooltip. Individual preview title bands keep TaskWindow.Title.</summary>
        public string DisplayTitle => ResolveFriendlyApplicationName(
            ExePath,
            Windows.FirstOrDefault()?.Title,
            AppId);

        internal static string ResolveFriendlyApplicationName(
            string? exePath, string? windowTitle, string? finalFallback = null)
        {
            if (!string.IsNullOrWhiteSpace(exePath))
            {
                string metadata = FriendlyNameCache.GetOrAdd(exePath, path =>
                {
                    try
                    {
                        FileVersionInfo info = FileVersionInfo.GetVersionInfo(path);
                        if (!string.IsNullOrWhiteSpace(info.FileDescription))
                        {
                            return info.FileDescription.Trim();
                        }
                        if (!string.IsNullOrWhiteSpace(info.ProductName))
                        {
                            return info.ProductName.Trim();
                        }
                    }
                    catch
                    {
                        // Elevated, missing and virtual executables are normal;
                        // bindings must degrade silently to live model data.
                    }
                    return string.Empty;
                });
                if (!string.IsNullOrWhiteSpace(metadata))
                {
                    return metadata;
                }
            }

            if (!string.IsNullOrWhiteSpace(windowTitle))
            {
                return windowTitle.Trim();
            }

            if (!string.IsNullOrWhiteSpace(exePath))
            {
                try
                {
                    string? fileName = Path.GetFileNameWithoutExtension(exePath);
                    if (!string.IsNullOrWhiteSpace(fileName))
                    {
                        return fileName;
                    }
                }
                catch
                {
                    // Last-resort identity below.
                }
            }

            return finalFallback ?? string.Empty;
        }

        /// <summary>
        /// True se ALMENO UNA finestra del gruppo richiede attenzione: nella
        /// Superbar il pulsante raggruppato pulsa se lampeggia una qualsiasi
        /// delle finestre che rappresenta.
        /// </summary>
        public bool IsFlashing
        {
            get => _isFlashing;
            set
            {
                if (_isFlashing == value)
                {
                    return;
                }
                _isFlashing = value;
                OnPropertyChanged();
            }
        }

        public void RefreshAggregateState()
        {
            IsActive = Windows.Any(w => w.IsActive);
            IsFlashing = Windows.Any(w => w.IsFlashing);
            OnPropertyChanged(nameof(DisplayTitle));
            OnPropertyChanged(nameof(WindowCount));
            OnPropertyChanged(nameof(HasMultipleWindows));
            OnPropertyChanged(nameof(IsRunning));
        }

        // ===============================================================
        //  v2.60 - Larghezza del pulsante quando i programmi aperti sono
        //  tanti (vedi UpdateTaskButtonLayout in TaskbarWindow.xaml.cs).
        //
        //  Fino alla v2.59 il pulsante non aveva NESSUN vincolo di
        //  larghezza: si dimensionava sul contenuto fra il minimo del tema e
        //  l'icona. Con molti programmi la somma superava lo spazio della
        //  barra e i pulsanti finivano disegnati SOPRA l'orologio e la tray,
        //  con le cornici tagliate a meta' (i "bordi disegnati male").
        //
        //  Qui il numero lo scrive la finestra: NaN = "quanto chiede il
        //  contenuto" (comportamento di sempre), un valore = larghezza
        //  imposta dal calcolo che fa stare tutti i pulsanti nello spazio
        //  disponibile, come fa la Superbar vera quando si stringe.
        // ===============================================================

        private double _buttonWidth = double.NaN;
        private double _buttonMinWidth = 52;

        /// <summary>Larghezza imposta al pulsante (NaN = automatica).</summary>
        public double ButtonWidth
        {
            get => _buttonWidth;
            set
            {
                if (_buttonWidth.Equals(value))
                {
                    return;
                }
                _buttonWidth = value;
                OnPropertyChanged();
            }
        }

        /// <summary>Minimo del pulsante: cresce fino a quello del tema quando
        /// c'e' spazio, scende quando i pulsanti devono stringersi.</summary>
        public double ButtonMinWidth
        {
            get => _buttonMinWidth;
            set
            {
                if (_buttonMinWidth.Equals(value))
                {
                    return;
                }
                _buttonMinWidth = value;
                OnPropertyChanged();
            }
        }

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
