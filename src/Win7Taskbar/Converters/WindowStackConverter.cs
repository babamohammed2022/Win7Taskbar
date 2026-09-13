// Win7Taskbar - indicatore "finestre impilate" del pulsante della Superbar
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
// v2.46: in Windows 7 un gruppo con piu' finestre non mostra NESSUN numero:
// il pulsante disegna, subito a destra dell'icona, fino a TRE rettangolini
// affiancati (le "schede" impilate). Oltre la terza finestra l'indicatore non
// cresce piu': le altre restano raggiungibili dalle anteprime.
//
// Questo converter decide se l'ennesima scheda va mostrata:
//   valore di ingresso   = TaskGroup.WindowCount
//   ConverterParameter   = posizione della scheda (1, 2, 3)
//   risultato            = Visible se WindowCount >= posizione (e almeno 2),
//                          altrimenti Collapsed.
// Con UNA sola finestra non si mostra nulla: e' la regola di Windows 7, dove
// il pulsante "singolo" non porta alcun indicatore di pila.
// ============================================================================

using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace Win7Taskbar.Converters
{
    /// <summary>
    /// Decide la visibilita' della N-esima "scheda" impilata del pulsante:
    /// si vedono tante schede quante sono le finestre del gruppo, fino a tre,
    /// e nessuna quando la finestra e' una sola.
    /// </summary>
    [ValueConversion(typeof(int), typeof(Visibility))]
    public sealed class WindowStackVisibilityConverter : IValueConverter
    {
        /// <summary>Numero massimo di schede disegnate (come Windows 7).</summary>
        public int MaxSheets { get; set; } = 3;

        public object Convert(object value, Type targetType, object parameter,
                              CultureInfo culture)
        {
            int count = value is int n ? n : 0;

            if (count < 2)
            {
                return Visibility.Collapsed;
            }

            int index = 1;
            if (parameter != null)
            {
                int.TryParse(parameter.ToString(), NumberStyles.Integer,
                             CultureInfo.InvariantCulture, out index);
            }

            if (index < 1 || index > MaxSheets)
            {
                return Visibility.Collapsed;
            }

            return count >= index ? Visibility.Visible : Visibility.Collapsed;
        }

        public object ConvertBack(object value, Type targetType, object parameter,
                                  CultureInfo culture)
        {
            // Solo lettura: il contatore lo decide il modello delle finestre.
            return Binding.DoNothing;
        }
    }
    /// <summary>
    /// v2.61: percentuale di una lunghezza. Serve a spostare le linee
    /// dell'indicatore "finestre impilate" del 5% della larghezza del
    /// pulsante: il valore resta la stessa percentuale a qualunque DPI e con
    /// qualunque larghezza del pulsante, perche' si ricalcola da ActualWidth.
    /// </summary>
    [ValueConversion(typeof(double), typeof(double))]
    public sealed class WidthRatioConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter,
                              CultureInfo culture)
        {
            double width = value is double d ? d : 0.0;
            double ratio = 0.05;
            if (parameter is string text && double.TryParse(
                    text, NumberStyles.Float, CultureInfo.InvariantCulture,
                    out double parsed))
            {
                ratio = parsed;
            }
            else if (parameter is double pd)
            {
                ratio = pd;
            }

            return width * ratio;
        }

        public object ConvertBack(object value, Type targetType, object parameter,
                                  CultureInfo culture)
            => Binding.DoNothing;
    }
}
