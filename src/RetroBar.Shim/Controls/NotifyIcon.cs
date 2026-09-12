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
// ---------------------------------------------------------------------------
// NOTA SULLA PROVENIENZA
//
// Questo assembly si chiama "RetroBar" e usa i namespace RetroBar.Controls /
// RetroBar.Converters / RetroBar.Utilities al solo scopo di soddisfare i
// riferimenti XAML del tema Windows7.xaml, che dichiara
// "clr-namespace:RetroBar.Controls;assembly=RetroBar".
//
// Il tema e' derivato da RetroBar (https://github.com/dremin/RetroBar),
// Copyright (c) dremin, distribuito sotto Apache License 2.0.
//
// Il CODICE di questo file e' stato scritto da zero per Win7Taskbar: non
// contiene codice sorgente copiato da RetroBar. Sono replicate solo le
// SUPERFICI PUBBLICHE (nomi di tipo e di proprieta') strettamente necessarie
// a far risolvere i binding del tema. Vedere CREDITS.txt.
// ---------------------------------------------------------------------------

using System.Windows;
using System.Windows.Controls;

namespace RetroBar.Controls
{
    /// <summary>
    /// Contenitore di una singola icona dell'area di notifica.
    /// Il tema Windows7.xaml applica a questo tipo un ControlTemplate che
    /// richiede: un ContentPresenter e un binding TwoWay su <see cref="IsPinned"/>.
    /// </summary>
    public class NotifyIcon : ContentControl
    {
        static NotifyIcon()
        {
            // Consente al tema di ridefinire il template tramite
            // <Style TargetType="{x:Type controls:NotifyIcon}">.
            DefaultStyleKeyProperty.OverrideMetadata(
                typeof(NotifyIcon),
                new FrameworkPropertyMetadata(typeof(NotifyIcon)));
        }

        /// <summary>
        /// True quando l'icona e' visibile nell'area di notifica, false quando
        /// e' relegata nell'overflow. Il tema la lega al pin overlay in TwoWay.
        /// </summary>
        public static readonly DependencyProperty IsPinnedProperty =
            DependencyProperty.Register(
                nameof(IsPinned),
                typeof(bool),
                typeof(NotifyIcon),
                new FrameworkPropertyMetadata(
                    true,
                    FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

        public bool IsPinned
        {
            get => (bool)GetValue(IsPinnedProperty);
            set => SetValue(IsPinnedProperty, value);
        }

        /// <summary>
        /// Finestra ospitante. Il tema la valorizza nel DataTemplate
        /// dell'overflow con un binding FindAncestor su Window.
        /// </summary>
        public static readonly DependencyProperty HostProperty =
            DependencyProperty.Register(
                nameof(Host),
                typeof(Window),
                typeof(NotifyIcon),
                new PropertyMetadata(null));

        public Window? Host
        {
            get => (Window?)GetValue(HostProperty);
            set => SetValue(HostProperty, value);
        }
    }
}
