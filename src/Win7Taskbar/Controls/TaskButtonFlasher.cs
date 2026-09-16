using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Fa pulsare in ambra un pulsante della Superbar quando l'applicazione
    /// richiede attenzione (FlashWindowEx).
    ///
    /// Il tema Windows7.xaml fornisce gia' due stili completi, "TaskButton" e
    /// "TaskButtonFlashing"; qui li alterniamo a intervalli regolari, che e' il
    /// modo in cui la barra di Windows 7 ottiene il lampeggio.
    ///
    /// Perche' un comportamento allegato e non un semplice DataTrigger: WPF non
    /// permette di assegnare la proprieta' Style da un setter di stile (sarebbe
    /// una definizione ricorsiva e il parser solleva un'eccezione). Serve quindi
    /// codice esterno che scambi lo stile dall'esterno del template.
    /// </summary>
    public static class TaskButtonFlasher
    {
        /// <summary>
        /// Cadenza del lampeggio. Windows 7 usa il tempo di battito di ciglia
        /// del sistema; 500 ms e' il valore predefinito su tutte le
        /// installazioni ed evita una P/Invoke in piu'.
        /// </summary>
        private static readonly TimeSpan BlinkInterval = TimeSpan.FromMilliseconds(500);

        /// <summary>
        /// Numero di lampeggi prima di restare fissi in ambra. Windows 7
        /// lampeggia alcune volte e poi lascia il pulsante evidenziato finche'
        /// l'utente non lo apre: cosi' la notifica resta visibile senza
        /// diventare fastidiosa.
        /// </summary>
        private const int MaxBlinks = 7;

        public static readonly DependencyProperty IsFlashingProperty =
            DependencyProperty.RegisterAttached(
                "IsFlashing",
                typeof(bool),
                typeof(TaskButtonFlasher),
                new PropertyMetadata(false, OnIsFlashingChanged));

        public static void SetIsFlashing(DependencyObject element, bool value)
            => element.SetValue(IsFlashingProperty, value);

        public static bool GetIsFlashing(DependencyObject element)
            => (bool)element.GetValue(IsFlashingProperty);

        // Stato per pulsante: timer, stile originale e conteggio dei lampeggi.
        private static readonly DependencyProperty StateProperty =
            DependencyProperty.RegisterAttached(
                "State", typeof(FlashState), typeof(TaskButtonFlasher),
                new PropertyMetadata(null));

        private sealed class FlashState
        {
            public DispatcherTimer? Timer;
            public int Blinks;
            public bool ShowingFlash;
        }

        // Chiavi degli stili del tema. Si applicano con SetResourceReference
        // invece di assegnare l'oggetto Style: cosi' resta un riferimento
        // dinamico e un eventuale cambio di tema a caldo continua a
        // propagarsi, esattamente come per il markup DynamicResource.
        private const string NormalStyleKey = "TaskButton";
        private const string FlashStyleKey = "TaskButtonFlashing";

        private static void OnIsFlashingChanged(DependencyObject d,
                                                DependencyPropertyChangedEventArgs e)
        {
            if (d is not Button button)
            {
                return;
            }

            if (e.NewValue is true)
            {
                Start(button);
            }
            else
            {
                Stop(button);
            }
        }

        private static void Start(Button button)
        {
            // Lo stile ambra e' opzionale: un tema che non lo definisce non deve
            // far crollare la barra, semplicemente non lampeggia. Il controllo
            // va fatto prima, perche' SetResourceReference su una chiave
            // assente lascerebbe il pulsante senza stile.
            if (button.TryFindResource(FlashStyleKey) is not Style)
            {
                return;
            }

            var state = (FlashState?)button.GetValue(StateProperty);
            if (state?.Timer != null)
            {
                return; // gia' in corso
            }

            state = new FlashState
            {
                Blinks = 0,
                ShowingFlash = false
            };

            var timer = new DispatcherTimer(DispatcherPriority.Render)
            {
                Interval = BlinkInterval
            };

            timer.Tick += (_, _) =>
            {
                // Il pulsante puo' sparire mentre lampeggia (finestra chiusa):
                // in quel caso il timer va fermato o resta appeso per sempre.
                if (!button.IsLoaded)
                {
                    Stop(button);
                    return;
                }

                state.ShowingFlash = !state.ShowingFlash;
                button.SetResourceReference(FrameworkElement.StyleProperty,
                    state.ShowingFlash ? FlashStyleKey : NormalStyleKey);

                if (!state.ShowingFlash)
                {
                    state.Blinks++;
                }

                if (state.Blinks >= MaxBlinks)
                {
                    // Fine della sequenza: si resta accesi, non spenti.
                    timer.Stop();
                    button.SetResourceReference(FrameworkElement.StyleProperty,
                                                FlashStyleKey);
                }
            };

            button.SetValue(StateProperty, state);

            // Primo fotogramma subito: aspettare 500 ms farebbe sembrare la
            // notifica in ritardo.
            state.ShowingFlash = true;
            button.SetResourceReference(FrameworkElement.StyleProperty, FlashStyleKey);
            timer.Start();
            state.Timer = timer;

            button.Unloaded += OnButtonUnloaded;
        }

        private static void Stop(Button button)
        {
            button.Unloaded -= OnButtonUnloaded;

            if (button.GetValue(StateProperty) is not FlashState state)
            {
                return;
            }

            state.Timer?.Stop();
            state.Timer = null;

            button.SetResourceReference(FrameworkElement.StyleProperty, NormalStyleKey);
            button.ClearValue(StateProperty);
        }

        private static void OnButtonUnloaded(object sender, RoutedEventArgs e)
        {
            if (sender is Button button)
            {
                Stop(button);
            }
        }
    }
}
