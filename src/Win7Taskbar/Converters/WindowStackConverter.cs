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
// v3.5: SEPARATORI DI GRUPPO del pulsante della Superbar. In Windows 7 un
// gruppo con piu' finestre disegna, subito a destra dell'icona, delle linee
// verticali sottili FRA le finestre dello stesso gruppo (i separatori della
// pila di finestre). La regola e':
//
//   1 finestra  -> 0 separatori (il pulsante singolo non porta nulla)
//   2 finestre  -> 1 separatore
//   3+ finestre -> 2 separatori, MAI di piu' (le altre finestre si
//                  raggiungono dalle anteprime)
//
// Questo converter decide se l'ennesimo separatore va mostrato:
//   valore di ingresso   = TaskGroup.WindowCount
//   ConverterParameter   = posizione del separatore (1, 2)
//   risultato            = Visible se WindowCount >= posizione + 1
//                          (almeno 2 finestre), altrimenti Collapsed.
// ============================================================================

using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace Win7Taskbar.Converters
{
    /// <summary>
    /// Decide la visibilita' dell'ennesimo separatore del gruppo: si vedono
    /// un separatore fra la prima e la seconda finestra e un secondo fra la
    /// seconda e la terza; oltre, mai piu' di due, e con una sola finestra
    /// nessuno.
    /// </summary>
    [ValueConversion(typeof(int), typeof(Visibility))]
    public sealed class WindowStackVisibilityConverter : IValueConverter
    {
        /// <summary>Numero massimo di separatori disegnati (v3.5: due).</summary>
        public int MaxSheets { get; set; } = 2;

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

            /* Il separatore N-esimo sta FRA due finestre: serve almeno
             * una finestra in piu' della posizione (regola v3.5:
             * 1->0, 2->1, 3+->2 separatori). */
            return count >= index + 1 ? Visibility.Visible
                                      : Visibility.Collapsed;
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

    /// <summary>
    /// v1.7: posizione dei separatori in funzione del numero di finestre.
    /// La base (la "dimensione di quando c'e' una sola scheda aperta",
    /// il fattore passato come parametro) viene leggermente AUMENTATA:
    /// x1,015 con due finestre, x1,02 con tre o piu'. I restanti casi non
    /// hanno separatori: restituisce il parametro invariato.
    /// </summary>
    [ValueConversion(typeof(int), typeof(double))]
    public sealed class WindowStackSeparatorOffsetConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter,
                              CultureInfo culture)
        {
            double baseFactor = 0.0;
            if (parameter is string s && double.TryParse(
                    s, System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture,
                    out var parsed))
            {
                baseFactor = parsed;
            }
            else if (parameter is double d)
            {
                baseFactor = d;
            }
            int count = value is int n ? n : 0;
            if (count == 2)
            {
                return baseFactor * 1.015;
            }
            if (count >= 3)
            {
                return baseFactor * 1.02;
            }
            return baseFactor;
        }

        public object ConvertBack(object value, Type targetType,
                                  object parameter, CultureInfo culture)
            => Binding.DoNothing;
    }

    /// <summary>
    /// v1.7.2: X offset (pixel) delle linee dei separatori di gruppo.
    /// Con due o piu' schede aperte il bordo esterno scivolava verso
    /// destra del 2% della larghezza del pulsante per scheda aperta,
    /// contando fino a 3 schede (con piu' di 3 resta come a 3).
    ///
    /// v1.7.6: two additions on top of the v1.7.2 position, both purely
    /// rightward and both percentages of the button width (so they stay
    /// proportional at every DPI and every button size):
    ///  - every stacked-borders line includes the previous +2.5%, then
    ///    moves a further +2% right;
    ///  - with MORE THAN 2 sheets open the OUTER line gets a further
    ///    +3% rightward. 3 sheets and 3+ sheets are one case: the count
    ///    is clamped at 3, no new per-sheet case above it was invented.
    ///
    /// At 3+ the inner line moves another 1.5% toward the outer line,
    /// tightening the width/gap occupied by the pair.
    ///
    /// Multi-binding: values[0] = button ActualWidth, values[1] =
    /// WindowCount. parameter = "outer" (default) or "inner". Below two
    /// sheets there are no separators at all: offset 0.
    /// RenderTransform only: like the rest of the overlay, the shift takes
    /// no layout space, the icon keeps the size and position it has with a
    /// single open window.
    /// </summary>
    [ValueConversion(typeof(double), typeof(double))]
    public sealed class WindowStackOuterBorderOffsetConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type targetType,
                              object parameter, CultureInfo culture)
        {
            try
            {
                double width = values != null && values.Length > 0
                    && values[0] is double w ? w : 0.0;
                int count = values != null && values.Length > 1
                    && values[1] is int n ? n : 0;
                if (width <= 0.0 || count < 2)
                {
                    return 0.0;
                }

                /* v1.7.2 base, clamped at three sheets (4+ == 3). */
                int effective = Math.Min(count, 3);
                double ratio = 0.02 * (effective - 1);

                /* Existing v1.7.6 placement, the previous +2% shift, and
                 * the requested additional +0.3% rightward adjustment. */
                ratio += 0.025 + 0.02 + 0.003;

                bool outer = parameter as string is not "inner";
                if (outer && count > 2)
                {
                    ratio += 0.03;
                }
                else if (!outer && count > 2)
                {
                    /* With two visible lines (3+ windows), move the inner
                     * one 1.5% toward the outer one. This narrows the pair's
                     * horizontal footprint and therefore the gap, without
                     * changing the 3px artwork or the button layout. */
                    ratio += 0.015;
                }

                return width * ratio;
            }
            catch
            {
                return 0.0;
            }
        }

        public object[] ConvertBack(object value, Type[] targetTypes,
                                    object parameter, CultureInfo culture)
            => throw new NotImplementedException();
    }
}
