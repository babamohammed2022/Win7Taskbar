// Win7Taskbar - one Start Menu row
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Media;

namespace Win7Taskbar.StartMenu
{
    internal sealed class StartMenuItem : INotifyPropertyChanged
    {
        private ImageSource? _icon;
        private bool _hasJumpList;
        private Visibility _jumpVisibility = Visibility.Collapsed;
        private int _indentLevel;
        private bool _isExpanded;

        public string Name { get; set; } = string.Empty;
        public string Path { get; set; } = string.Empty;
        public string Target { get; set; } = string.Empty;
        public string Folder { get; set; } = string.Empty;
        public bool IsSeparator { get; set; }
        public bool IsAllPrograms { get; set; }
        public bool IsFolder { get; set; }
        public bool IsPrimary { get; set; }
        public bool IsPinned { get; set; }
        public bool IsRecent { get; set; }
        public bool IsRightPane { get; set; }

        public int IndentLevel
        {
            get => _indentLevel;
            set
            {
                if (_indentLevel == value)
                {
                    return;
                }
                _indentLevel = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(IndentMargin));
            }
        }

        public Thickness IndentMargin => new Thickness(4 + IndentLevel * 14, 0, 0, 0);

        public bool IsExpanded
        {
            get => _isExpanded;
            set
            {
                if (_isExpanded == value)
                {
                    return;
                }
                _isExpanded = value;
                OnPropertyChanged();
            }
        }

        public ImageSource? Icon
        {
            get => _icon;
            set { _icon = value; OnPropertyChanged(); }
        }

        public bool HasJumpList
        {
            get => _hasJumpList;
            set
            {
                _hasJumpList = value;
                JumpVisibility = value && !IsFolder
                    ? Visibility.Visible
                    : Visibility.Collapsed;
                OnPropertyChanged();
            }
        }

        public Visibility JumpVisibility
        {
            get => _jumpVisibility;
            private set { _jumpVisibility = value; OnPropertyChanged(); }
        }

        public event PropertyChangedEventHandler? PropertyChanged;

        private void OnPropertyChanged([CallerMemberName] string? name = null)
            => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
