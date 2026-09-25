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
        /// <summary>
        /// Search result section header (Open-Shell / Windows 7 layout:
        /// "Programs (3)", "Settings (12)", "Files (1)"). Headers collapse
        /// / expand their section on click. SectionId identifies the
        /// section ("programs", "settings", "docs", ...).
        /// </summary>
        public bool IsSectionHeader { get; set; }
        public string SectionId { get; set; } = string.Empty;
        /// <summary>
        /// v3.12: spazio verticale (DIP) PRIMA della riga, fuori dalla sua
        /// griglia da 22 px fissi. Nel pannello dei risultati di ricerca di
        /// Windows 7 la riga finale "Cerca in Internet" stacca di ~32 px
        /// (circa 1,5 righe) dall'ultima categoria: NON e' un multiplo
        /// della griglia, quindi vive come gap esterno, non come riga vuota.
        /// 0 = nessun gap (tutte le righe normali).
        /// </summary>
        public double GapBefore { get; set; }
        /// <summary>Margine XAML pronto all'uso per <see cref="GapBefore"/>.</summary>
        public Thickness GapBeforeMargin => new Thickness(0, GapBefore, 0, 0);
        /// <summary>
        /// Right-pane hover flyout. Original wording; not a Microsoft string.
        /// </summary>
        public string? Infotip { get; set; }
        public bool HasInfotip => !string.IsNullOrWhiteSpace(Infotip);
        public bool IsSeparator { get; set; }
        public bool IsAllPrograms { get; set; }
        public bool IsFolder { get; set; }
        public bool IsPrimary { get; set; }
        public bool IsPinned { get; set; }
        public bool IsRecent { get; set; }
        public bool IsRightPane { get; set; }
        public bool IsTreeRow { get; set; }

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

        /// <summary>
        /// Open-Shell Programs tree indent is TreeView default (~19px) plus
        /// skin Programs_indent. Rewritten: 16 DIP per level at 100% scale.
        /// </summary>
        public Thickness IndentMargin =>
            new Thickness(IsTreeRow ? IndentLevel * 16 : 0, 0, 0, 0);

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
