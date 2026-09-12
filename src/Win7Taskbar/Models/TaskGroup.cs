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
using System.Collections.ObjectModel;
using System.ComponentModel;
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
        private ImageSource? _icon;
        private bool _isFlashing;
        private bool _isActive;

        public event PropertyChangedEventHandler? PropertyChanged;

        public TaskGroup(string appId, string exePath)
        {
            AppId = appId;
            ExePath = exePath;
            Windows = new ObservableCollection<TaskWindow>();
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

        /// <summary>Nome mostrato nel tooltip e nell'intestazione del picker.</summary>
        public string DisplayTitle
        {
            get
            {
                if (Windows.Count == 1)
                {
                    string title = Windows[0].Title;
                    if (!string.IsNullOrWhiteSpace(title))
                    {
                        return title;
                    }
                }

                if (!string.IsNullOrEmpty(ExePath))
                {
                    try
                    {
                        return Path.GetFileNameWithoutExtension(ExePath);
                    }
                    catch (ArgumentException)
                    {
                        // percorso non valido: si passa al fallback
                    }
                }

                return AppId;
            }
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

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
