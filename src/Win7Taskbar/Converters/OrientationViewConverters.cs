// Win7Taskbar - converter per il layout della barra verticale
// Copyright (c) 2026 Win7Taskbar contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// ============================================================================
// v3.12: supporto alla barra VERTICALE (bordi sinistro/destro). I trigger
// del tema gestiscono solo il cromato; la GEOMETRIA di scorrimento e le
// frecce della Superbar vanno convertite da Orientation del Window:
// qui vivono i due piccoli converter usati da TaskbarWindow.xaml.
// ============================================================================

using System;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace Win7Taskbar.Converters
{
    /// <summary>
    /// v3.12: angolo (0/90 gradi) delle frecce di scorrimento della
    /// Superbar. In orizzontale le frecce puntano a sinistra/destra; in
    /// verticale la STESSA freccia, ruotata di 90 gradi in senso orario,
    /// punta in alto sulla freccia di sinistra (ora in cima) e in basso
    /// sulla freccia di destra (ora in fondo): la simmetria dei glifi
    /// fa il lavoro, nessun asset nuovo.
    /// </summary>
    [ValueConversion(typeof(Orientation), typeof(double))]
    public sealed class OrientationArrowAngleConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter,
                              CultureInfo culture)
        {
            try
            {
                return value is Orientation o && o == Orientation.Vertical
                    ? 90.0
                    : 0.0;
            }
            catch
            {
                return 0.0;
            }
        }

        public object ConvertBack(object value, Type targetType, object parameter,
                                  CultureInfo culture)
            => Binding.DoNothing;
    }

    /// <summary>
    /// v3.12: stato della barra di scorrimento VERTICALE dello
    /// ScrollViewer della Superbar. In orizzontale e' Disabled (mai
    /// usata, si scorre in larghezza); in verticale diventa Hidden:
    /// attiva ma invisibile, specchio della horizontale di oggi, cosi'
    /// i pulsanti che non entrano nell'altezza restano raggiungibili
    /// dalle frecce e da Shift+rotellina.
    /// </summary>
    [ValueConversion(typeof(Orientation), typeof(ScrollBarVisibility))]
    public sealed class OrientationVerticalScrollConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter,
                              CultureInfo culture)
        {
            try
            {
                return value is Orientation o && o == Orientation.Vertical
                    ? ScrollBarVisibility.Hidden
                    : ScrollBarVisibility.Disabled;
            }
            catch
            {
                return ScrollBarVisibility.Disabled;
            }
        }

        public object ConvertBack(object value, Type targetType, object parameter,
                                  CultureInfo culture)
            => Binding.DoNothing;
    }
}
