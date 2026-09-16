// Win7Taskbar - calendario del riquadro dell'orologio
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
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Griglia mensile con l'aspetto del calendario di Windows 7.
    ///
    /// Perche' non il controllo Calendar di WPF: quello ha un tema proprio,
    /// squadrato e con i colori di Aero moderno, che stona con il riquadro
    /// di Windows 7 e che per essere corretto richiederebbe di riscriverne
    /// per intero il ControlTemplate. Disegnarlo qui costa meno codice,
    /// da' il controllo esatto su intestazioni e colori, e usa solo
    /// TextBlock e Grid.
    /// </summary>
    public sealed class Win7Calendar : Control
    {
        private static readonly Brush HeaderBrush =
            new SolidColorBrush(Color.FromRgb(0x1E, 0x39, 0x5B));

        private static readonly Brush DayNameBrush =
            new SolidColorBrush(Color.FromRgb(0x33, 0x66, 0x99));

        private static readonly Brush NormalDayBrush =
            new SolidColorBrush(Color.FromRgb(0x20, 0x20, 0x20));

        private static readonly Brush OtherMonthBrush =
            new SolidColorBrush(Color.FromRgb(0xA0, 0xA8, 0xB0));

        private static readonly Brush TodayBackground =
            new SolidColorBrush(Color.FromArgb(0x66, 0x99, 0xC4, 0xEB));

        private static readonly Brush TodayBorder =
            new SolidColorBrush(Color.FromRgb(0x3C, 0x7F, 0xB1));

        private static readonly Brush SeparatorBrush =
            new SolidColorBrush(Color.FromRgb(0xD5, 0xDF, 0xE9));

        private readonly FontFamily _font = new FontFamily("Segoe UI, Tahoma, Arial");

        private readonly Grid _root;
        private readonly TextBlock _monthLabel;
        private readonly Grid _daysGrid;

        private DateTime _displayMonth;

        public Win7Calendar()
        {
            _displayMonth = new DateTime(DateTime.Today.Year, DateTime.Today.Month, 1);

            _root = new Grid();
            _root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            _root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            _root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            // ----- Intestazione: frecce e nome del mese -----
            var headerGrid = new Grid { Margin = new Thickness(2, 2, 2, 6) };
            headerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            headerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            headerGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

            Button prev = CreateArrowButton(pointsLeft: true);
            prev.Click += (_, _) => ShiftMonth(-1);
            Grid.SetColumn(prev, 0);
            headerGrid.Children.Add(prev);

            _monthLabel = new TextBlock
            {
                FontFamily = _font,
                FontSize = 12,
                FontWeight = FontWeights.SemiBold,
                Foreground = HeaderBrush,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };
            Grid.SetColumn(_monthLabel, 1);
            headerGrid.Children.Add(_monthLabel);

            Button next = CreateArrowButton(pointsLeft: false);
            next.Click += (_, _) => ShiftMonth(1);
            Grid.SetColumn(next, 2);
            headerGrid.Children.Add(next);

            Grid.SetRow(headerGrid, 0);
            _root.Children.Add(headerGrid);

            var separator = new Border
            {
                Height = 1,
                Background = SeparatorBrush,
                Margin = new Thickness(2, 0, 2, 4)
            };
            Grid.SetRow(separator, 1);
            _root.Children.Add(separator);

            _daysGrid = new Grid { Margin = new Thickness(2, 0, 2, 2) };
            for (int c = 0; c < 7; c++)
            {
                _daysGrid.ColumnDefinitions.Add(
                    new ColumnDefinition { Width = new GridLength(26) });
            }
            // Una riga per i nomi dei giorni + sei righe di date: sei bastano
            // sempre, anche per un mese di 31 giorni che inizia di domenica.
            for (int r = 0; r < 7; r++)
            {
                _daysGrid.RowDefinitions.Add(
                    new RowDefinition { Height = new GridLength(20) });
            }

            Grid.SetRow(_daysGrid, 2);
            _root.Children.Add(_daysGrid);

            AddChild(_root);
            Build();
        }

        // Il controllo e' costruito da codice: serve un template minimo che
        // mostri il contenuto, altrimenti non verrebbe disegnato nulla.
        static Win7Calendar()
        {
            DefaultStyleKeyProperty.OverrideMetadata(
                typeof(Win7Calendar),
                new FrameworkPropertyMetadata(typeof(Win7Calendar)));
        }

        private Grid? _visualChild;

        private void AddChild(Grid child)
        {
            _visualChild = child;
            AddVisualChild(child);
            AddLogicalChild(child);
        }

        protected override int VisualChildrenCount => _visualChild == null ? 0 : 1;

        protected override Visual GetVisualChild(int index) => _visualChild!;

        protected override Size MeasureOverride(Size constraint)
        {
            if (_visualChild == null)
            {
                return new Size(0, 0);
            }

            _visualChild.Measure(constraint);
            return _visualChild.DesiredSize;
        }

        protected override Size ArrangeOverride(Size arrangeBounds)
        {
            _visualChild?.Arrange(new Rect(arrangeBounds));
            return arrangeBounds;
        }

        /// <summary>
        /// Freccia di scorrimento del mese.
        ///
        /// Il triangolino e' un disegno vettoriale e non un carattere:
        /// i glifi come U+25C4 mancano in parecchi font e verrebbero
        /// mostrati come rettangoli vuoti.
        /// </summary>
        private Button CreateArrowButton(bool pointsLeft)
        {
            var triangle = new System.Windows.Shapes.Path
            {
                Fill = HeaderBrush,
                Data = Geometry.Parse(pointsLeft
                    ? "M 5,0 L 5,8 L 0,4 Z"
                    : "M 0,0 L 5,4 L 0,8 Z"),
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };

            return new Button
            {
                Content = triangle,
                Width = 20,
                Height = 18,
                Padding = new Thickness(0),
                Focusable = false,
                Cursor = Cursors.Hand
            };
        }

        private void ShiftMonth(int delta)
        {
            _displayMonth = _displayMonth.AddMonths(delta);
            Build();
        }

        /// <summary>Ridisegna la griglia per il mese corrente.</summary>
        private void Build()
        {
            CultureInfo culture = CultureInfo.CurrentCulture;

            _monthLabel.Text = _displayMonth.ToString("MMMM yyyy", culture);
            _daysGrid.Children.Clear();

            // I nomi abbreviati partono dal primo giorno della settimana
            // secondo la cultura: in Italia lunedi', negli Stati Uniti
            // domenica.
            DayOfWeek firstDay = culture.DateTimeFormat.FirstDayOfWeek;
            string[] shortest = culture.DateTimeFormat.ShortestDayNames;

            for (int i = 0; i < 7; i++)
            {
                var dow = (DayOfWeek)(((int)firstDay + i) % 7);

                var label = new TextBlock
                {
                    Text = shortest[(int)dow],
                    FontFamily = _font,
                    FontSize = 11,
                    Foreground = DayNameBrush,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                };

                Grid.SetColumn(label, i);
                Grid.SetRow(label, 0);
                _daysGrid.Children.Add(label);
            }

            // Quanti giorni del mese precedente riempiono la prima riga.
            int offset = ((int)_displayMonth.DayOfWeek - (int)firstDay + 7) % 7;
            DateTime cursor = _displayMonth.AddDays(-offset);
            DateTime today = DateTime.Today;

            for (int cell = 0; cell < 42; cell++)
            {
                int row = (cell / 7) + 1;
                int column = cell % 7;

                bool inMonth = cursor.Month == _displayMonth.Month;
                bool isToday = cursor == today;

                var text = new TextBlock
                {
                    Text = cursor.Day.ToString(culture),
                    FontFamily = _font,
                    FontSize = 11,
                    Foreground = inMonth ? NormalDayBrush : OtherMonthBrush,
                    FontWeight = isToday ? FontWeights.Bold : FontWeights.Normal,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                };

                if (isToday)
                {
                    // Il giorno corrente ha il riquadro azzurro di Windows 7.
                    var highlight = new Border
                    {
                        Background = TodayBackground,
                        BorderBrush = TodayBorder,
                        BorderThickness = new Thickness(1),
                        CornerRadius = new CornerRadius(2),
                        Margin = new Thickness(1),
                        Child = text
                    };

                    Grid.SetColumn(highlight, column);
                    Grid.SetRow(highlight, row);
                    _daysGrid.Children.Add(highlight);
                }
                else
                {
                    Grid.SetColumn(text, column);
                    Grid.SetRow(text, row);
                    _daysGrid.Children.Add(text);
                }

                cursor = cursor.AddDays(1);
            }
        }
    }
}
