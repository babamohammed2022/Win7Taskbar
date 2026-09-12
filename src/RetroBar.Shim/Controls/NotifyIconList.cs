// Win7Taskbar - RetroBar compatibility shim
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
// Superficie pubblica minima richiesta dal tema Windows7.xaml (derivato da
// RetroBar, Copyright (c) dremin, Apache 2.0). Codice scritto da zero.

using System.Collections;
using System.Windows;
using System.Windows.Controls;

namespace RetroBar.Controls
{
    /// <summary>
    /// Area di notifica completa: icone visibili + pulsante di overflow.
    /// Il tema Windows7.xaml sostituisce integralmente il template e si
    /// aspetta di trovare <c>NotificationArea.UnpinnedIcons</c> come
    /// ItemsSource dell'ItemsControl "OverflowIcons".
    /// </summary>
    public class NotifyIconList : Control
    {
        static NotifyIconList()
        {
            DefaultStyleKeyProperty.OverrideMetadata(
                typeof(NotifyIconList),
                new FrameworkPropertyMetadata(typeof(NotifyIconList)));
        }

        /// <summary>
        /// Sorgente dati dell'area di notifica. Il tema naviga
        /// <c>Path=NotificationArea.UnpinnedIcons</c>.
        /// </summary>
        public static readonly DependencyProperty NotificationAreaProperty =
            DependencyProperty.Register(
                nameof(NotificationArea),
                typeof(object),
                typeof(NotifyIconList),
                new PropertyMetadata(null));

        public object? NotificationArea
        {
            get => GetValue(NotificationAreaProperty);
            set => SetValue(NotificationAreaProperty, value);
        }

        /// <summary>Icone ancorate, mostrate direttamente nella barra.</summary>
        public static readonly DependencyProperty PinnedIconsProperty =
            DependencyProperty.Register(
                nameof(PinnedIcons),
                typeof(IEnumerable),
                typeof(NotifyIconList),
                new PropertyMetadata(null));

        public IEnumerable? PinnedIcons
        {
            get => (IEnumerable?)GetValue(PinnedIconsProperty);
            set => SetValue(PinnedIconsProperty, value);
        }
    }
}
