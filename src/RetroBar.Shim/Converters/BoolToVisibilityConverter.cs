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

using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace RetroBar.Converters
{
    /// <summary>
    /// Converte un bool in Visibility. Il tema lo istanzia come
    /// <c>&lt;retroconv:BoolToVisibilityConverter x:Key="BoolToVisibility" /&gt;</c>
    /// e lo usa per mostrare la freccetta dell'overflow.
    /// </summary>
    public sealed class BoolToVisibilityConverter : IValueConverter
    {
        /// <summary>Visibility restituita quando il valore e' false.</summary>
        public Visibility FalseVisibility { get; set; } = Visibility.Collapsed;

        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            bool flag = value switch
            {
                bool b => b,
                null => false,
                _ => System.Convert.ToBoolean(value, CultureInfo.InvariantCulture)
            };

            // "parameter=invert" ribalta la logica, utile nei template.
            if (parameter is string s &&
                s.Equals("invert", StringComparison.OrdinalIgnoreCase))
            {
                flag = !flag;
            }

            return flag ? Visibility.Visible : FalseVisibility;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            return value is Visibility visibility && visibility == Visibility.Visible;
        }
    }
}
