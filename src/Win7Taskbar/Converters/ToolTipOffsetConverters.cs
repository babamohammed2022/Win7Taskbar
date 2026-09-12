// Win7Taskbar - Posizionamento delle anteprime di finestra
//
// PORTATO DA RetroBar - Copyright (c) dremin
// https://github.com/dremin/RetroBar  -  Apache License 2.0
// File originali: RetroBar/Converters/ToolTipHorizontalOffsetConverter.cs,
//                 ToolTipVerticalOffsetConverter.cs, ToolTipPlacementConverter.cs
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
// La formula di centratura e' quella di RetroBar:
//
//     (larghezzaPulsante / 2) - (larghezzaAnteprima / 2 / scala)
//
// Serve perche' un offset di 0 allineerebbe il BORDO SINISTRO dell'anteprima
// al bordo sinistro del pulsante, non il centro col centro. Con la barra sui
// lati (Left/Right) l'offset orizzontale va invece azzerato.
// ============================================================================

using System;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Data;

namespace Win7Taskbar.Converters
{
    /// <summary>Bordi su cui puo' stare la barra. Coerente con AppBarEdge.</summary>
    public enum TaskbarEdge
    {
        Left = 0,
        Top = 1,
        Right = 2,
        Bottom = 3
    }

    /// <summary>
    /// Centra l'anteprima orizzontalmente sul pulsante.
    /// Valori attesi: [0] larghezza del pulsante, [1] larghezza dell'anteprima,
    /// [2] scala, [3] bordo della barra.
    /// </summary>
    public sealed class ToolTipHorizontalOffsetConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type targetType, object parameter,
                              CultureInfo culture)
        {
            if (values == null || values.Length < 3 ||
                values.Any(v => v == DependencyProperty.UnsetValue))
            {
                return double.NaN;
            }

            TaskbarEdge edge = ReadEdge(values, 3);
            if (edge == TaskbarEdge.Left || edge == TaskbarEdge.Right)
            {
                return 0d;
            }

            double placementTargetWidth = System.Convert.ToDouble(values[0], culture);
            double toolTipWidth = System.Convert.ToDouble(values[1], culture);
            double scale = System.Convert.ToDouble(values[2], culture);
            if (scale <= 0)
            {
                scale = 1.0;
            }

            return (placementTargetWidth / 2.0) - (toolTipWidth / 2.0 / scale);
        }

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter,
                                    CultureInfo culture)
            => throw new NotSupportedException();

        internal static TaskbarEdge ReadEdge(object[] values, int index)
        {
            if (values.Length > index && values[index] is not null)
            {
                try
                {
                    return (TaskbarEdge)System.Convert.ToInt32(
                        values[index], CultureInfo.InvariantCulture);
                }
                catch (Exception e) when (e is FormatException or InvalidCastException
                                              or OverflowException)
                {
                    // valore non interpretabile: si assume la barra in basso
                }
            }
            return TaskbarEdge.Bottom;
        }
    }

    /// <summary>Centra l'anteprima verticalmente quando la barra e' laterale.</summary>
    public sealed class ToolTipVerticalOffsetConverter : IMultiValueConverter
    {
        public object Convert(object[] values, Type targetType, object parameter,
                              CultureInfo culture)
        {
            if (values == null || values.Length < 3 ||
                values.Any(v => v == DependencyProperty.UnsetValue))
            {
                return double.NaN;
            }

            TaskbarEdge edge = ToolTipHorizontalOffsetConverter.ReadEdge(values, 3);
            if (edge == TaskbarEdge.Top || edge == TaskbarEdge.Bottom)
            {
                return 0d;
            }

            double placementTargetHeight = System.Convert.ToDouble(values[0], culture);
            double toolTipHeight = System.Convert.ToDouble(values[1], culture);
            double scale = System.Convert.ToDouble(values[2], culture);
            if (scale <= 0)
            {
                scale = 1.0;
            }

            return (placementTargetHeight / 2.0) - (toolTipHeight / 2.0 / scale);
        }

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter,
                                    CultureInfo culture)
            => throw new NotSupportedException();
    }

    /// <summary>
    /// Sceglie il lato su cui aprire l'anteprima: barra in basso -> anteprima
    /// sopra, barra in alto -> sotto, e cosi' via.
    ///
    /// Implementa ENTRAMBE le interfacce: come IValueConverter per un binding
    /// singolo sul bordo, come IMultiValueConverter per restare compatibile
    /// con la forma usata da RetroBar. Usarne uno al posto dell'altro provoca
    /// una InvalidCastException a tempo di caricamento del template, che WPF
    /// riporta solo come "Set property Binding.Converter threw an exception".
    /// </summary>
    public sealed class ToolTipPlacementConverter : IValueConverter, IMultiValueConverter
    {
        public object Convert(object value, Type targetType, object parameter,
                              CultureInfo culture)
            => FromEdge(ToolTipHorizontalOffsetConverter.ReadEdge(new[] { value }, 0));

        public object ConvertBack(object value, Type targetType, object parameter,
                                  CultureInfo culture)
            => throw new NotSupportedException();

        public object Convert(object[] values, Type targetType, object parameter,
                              CultureInfo culture)
            => FromEdge(ToolTipHorizontalOffsetConverter.ReadEdge(values, 0));

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter,
                                    CultureInfo culture)
            => throw new NotSupportedException();

        private static PlacementMode FromEdge(TaskbarEdge edge) => edge switch
        {
            TaskbarEdge.Top => PlacementMode.Bottom,
            TaskbarEdge.Left => PlacementMode.Right,
            TaskbarEdge.Right => PlacementMode.Left,
            _ => PlacementMode.Top
        };
    }
}
