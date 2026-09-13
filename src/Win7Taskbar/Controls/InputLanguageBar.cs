// Win7Taskbar - host gestito dell'indicatore della lingua di input
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
// v3.6: PORT COMPLETO DELL'INDICATORE, LATO NATIVO.
//
// Le tre mod Windhawk (layout-control, more-space, fix-legacy) sono
// portate una a una nel core nativo (LanguageBar.cpp), che crea la STESSA
// struttura di Windows (cornice TrayInputIndicatorWClass + figlio-testo
// InputIndicatorButton), disegna la sigla in stile mod e gestisce i layout
// (meccanica ManagedShell: sondaggio del thread in primo piano, cambio
// lingua con LoadKeyboardLayout + broadcast WM_INPUTLANGCHANGEREQUEST).
//
// Questo controllo gestito resta l'INGOMBRO nella disposizione della barra:
// riserva lo spazio giusto per lo stile scelto e passa al core il
// rettangolo a schermo (fisico) a ogni riposizionamento. Nulla di visibile
// si disegna qui: il testo lo disegna il porting nativo, come richiesto.
// ============================================================================

using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Host dell'indicatore della lingua: riserva lo spazio nella TrayArea
    /// e alimenta il porting nativo con il rettangolo a schermo.
    /// Italiano: l'ospite dell'indicatore della lingua; il disegno e la
    /// logica sono nel core nativo.
    /// </summary>
    public sealed class InputLanguageBar : FrameworkElement
    {
        /* Modo scelto dalle Proprieta' (stessi valori delle tabelle native). */
        public const int ModeHidden = 0;
        public const int ModeWin7 = 1;
        public const int ModeWin81 = 2;
        public const int ModeWin10 = 3;

        public static readonly DependencyProperty ModeProperty =
            DependencyProperty.Register(nameof(Mode), typeof(int),
                typeof(InputLanguageBar),
                new FrameworkPropertyMetadata(ModeWin7,
                    new PropertyChangedCallback(OnModeChanged)));

        /// <summary>0 nascosta, 1 Windows 7, 2 Windows 8.1, 3 Windows 10/11.</summary>
        public int Mode
        {
            get => (int)GetValue(ModeProperty);
            set => SetValue(ModeProperty, value);
        }

        /* Ingombri in DIP per modo (l'altezza 32 della targhetta Windows 8.1
         * e' la regola della mod more-space: sotto 32 le due righe non
         * stanno). */
        private static readonly Size SlotWin7 = new Size(28, 24);
        private static readonly Size SlotWin81 = new Size(34, 32);
        private static readonly Size SlotWin10 = new Size(40, 26);

        private Size _lastSentSize;
        private Point _lastSentScreenTopLeft;
        private int _lastSentMode = -1;
        private bool _forwardScheduled;

        public InputLanguageBar()
        {
            Visibility = Visibility.Collapsed;   /* lo accende il modo */
            LayoutUpdated += (s, e) => ScheduleForward();
            SizeChanged += (s, e) => ScheduleForward();
        }

        /// <summary>Chiude l'indicatore nativo allo smontaggio.</summary>
        protected override void OnVisualParentChanged(DependencyObject oldParent)
        {
            base.OnVisualParentChanged(oldParent);
            if (VisualParent == null && PresentationSource.FromVisual(this) == null)
            {
                TryShutdown();
            }
        }

        private static void OnModeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            var bar = (InputLanguageBar)d;
            int mode = bar.Mode;
            bar.Visibility = (mode == ModeHidden)
                ? Visibility.Collapsed
                : Visibility.Visible;

            Size slot = mode == ModeWin81 ? SlotWin81
                      : mode == ModeWin10 ? SlotWin10
                      : SlotWin7;
            /* La riserva di spazio cambia col modo: lo dice subito al
             * pannello che ci sta attorno. */
            bar.Width = slot.Width;
            bar.Height = slot.Height;
            bar.InvalidateMeasure();
            bar.ScheduleForward();
        }

        protected override Size MeasureOverride(Size availableSize)
        {
            int mode = Mode;
            Size slot = mode == ModeWin81 ? SlotWin81
                      : mode == ModeWin10 ? SlotWin10
                      : SlotWin7;
            if (mode == ModeHidden)
            {
                return new Size(0, 0);
            }
            return slot;
        }

        /// <summary>
        /// Passa il rettangolo a schermo al porting nativo. Le coordinate
        /// arrivano FISICHE (pixel reali), come le vuole il core.
        /// </summary>
        private void ScheduleForward()
        {
            if (_forwardScheduled || Mode == ModeHidden)
            {
                return;
            }
            /* Si lascia concludere il layout: dentro LayoutUpdated il
             * misurare ancora e' vietato. */
            _forwardScheduled = true;
            Dispatcher.BeginInvoke(new Action(ForwardRect),
                System.Windows.Threading.DispatcherPriority.Loaded);
        }

        private void ForwardRect()
        {
            _forwardScheduled = false;
            try
            {
                int mode = Mode;
                if (mode == ModeHidden)
                {
                    return;
                }

                var source = PresentationSource.FromVisual(this) as HwndSource;
                if (source?.CompositionTarget == null || !IsLoaded)
                {
                    return;
                }

                /* Angolo dell'elemento -> schermo -> pixel fisici. */
                Point screenTopLeft = PointToScreen(new Point(0, 0));
                double scale = source.CompositionTarget.TransformToDevice.M11;
                if (scale <= 0)
                {
                    scale = 1.0;
                }
                int x = (int)Math.Round(screenTopLeft.X * scale);
                int y = (int)Math.Round(screenTopLeft.Y * scale);
                int w = (int)Math.Round(ActualWidth * scale);
                int h = (int)Math.Round(ActualHeight * scale);

                /* Niente chiamate inutili: il core non deve ridisegnare a
                 * ogni respiro del layout. */
                if (mode == _lastSentMode
                    && w == _lastSentSize.Width && h == _lastSentSize.Height
                    && screenTopLeft == _lastSentScreenTopLeft)
                {
                    return;
                }
                _lastSentMode = mode;
                _lastSentSize = new Size(w, h);
                _lastSentScreenTopLeft = screenTopLeft;

                NativeMethods.W7T_LangBarPlace(
                    (ulong)source.Handle, mode, x, y, w, h);
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
            catch (Exception)
            {
                /* Mai far cadere la barra per l'indicatore. */
            }
        }

        private static void TryShutdown()
        {
            try
            {
                NativeMethods.W7T_LangBarShutdown();
            }
            catch (DllNotFoundException) { }
            catch (EntryPointNotFoundException) { }
            catch (Exception) { }
        }
    }
}
