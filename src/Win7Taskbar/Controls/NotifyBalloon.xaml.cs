using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Fumetto di notifica dell'area di sistema.
    ///
    /// Le applicazioni lo richiedono passando NIF_INFO a Shell_NotifyIconW; la
    /// DLL nativa intercetta la chiamata, conserva l'ultimo messaggio e segnala
    /// l'evento TrayBalloon. Qui lo trasformiamo nella nuvoletta di Windows 7.
    /// </summary>
    public partial class NotifyBalloon : UserControl
    {
        // Flag NIIF_* di Shell_NotifyIcon: scelgono l'icona del fumetto.
        private const uint NIIF_NONE = 0x0;
        private const uint NIIF_INFO = 0x1;
        private const uint NIIF_WARNING = 0x2;
        private const uint NIIF_ERROR = 0x3;
        private const uint NIIF_USER = 0x4;
        private const uint NIIF_ICON_MASK = 0xF;

        /// <summary>
        /// Durata usata quando l'applicazione non ne indica una. Windows ignora
        /// da tempo i timeout richiesti e applica il valore di accessibilita'
        /// del sistema, che di norma vale 5 secondi.
        /// </summary>
        private static readonly TimeSpan DefaultTimeout = TimeSpan.FromSeconds(9);

        /// <summary>
        /// Limiti imposti da Windows al timeout richiesto dall'applicazione.
        /// Senza di questi un programma potrebbe inchiodare un fumetto sullo
        /// schermo per sempre.
        /// </summary>
        private static readonly TimeSpan MinTimeout = TimeSpan.FromSeconds(4);
        private static readonly TimeSpan MaxTimeout = TimeSpan.FromSeconds(30);

        private readonly DispatcherTimer _timer;

        public NotifyBalloon()
        {
            InitializeComponent();

            _timer = new DispatcherTimer();
            _timer.Tick += (_, _) => Dismiss();

            Unloaded += (_, _) => _timer.Stop();
        }

        /// <summary>Segnalato quando il fumetto va rimosso dallo schermo.</summary>
        public event EventHandler? Closed;

        public static readonly DependencyProperty TitleProperty =
            DependencyProperty.Register(nameof(Title), typeof(string),
                typeof(NotifyBalloon), new PropertyMetadata(string.Empty));

        public string Title
        {
            get => (string)GetValue(TitleProperty);
            set => SetValue(TitleProperty, value);
        }

        public static readonly DependencyProperty InfoProperty =
            DependencyProperty.Register(nameof(Info), typeof(string),
                typeof(NotifyBalloon), new PropertyMetadata(string.Empty));

        public string Info
        {
            get => (string)GetValue(InfoProperty);
            set => SetValue(InfoProperty, value);
        }

        public static readonly DependencyProperty IconSourceProperty =
            DependencyProperty.Register(nameof(IconSource), typeof(ImageSource),
                typeof(NotifyBalloon), new PropertyMetadata(null));

        public ImageSource? IconSource
        {
            get => (ImageSource?)GetValue(IconSourceProperty);
            set => SetValue(IconSourceProperty, value);
        }

        // Il tema puo' non definire un convertitore booleano, e comunque un
        // Binding.Converter non accetta DynamicResource: esponiamo direttamente
        // la Visibility come proprieta' di dipendenza.
        public static readonly DependencyProperty IconVisibilityProperty =
            DependencyProperty.Register(nameof(IconVisibility), typeof(Visibility),
                typeof(NotifyBalloon), new PropertyMetadata(Visibility.Collapsed));

        public Visibility IconVisibility
        {
            get => (Visibility)GetValue(IconVisibilityProperty);
            set => SetValue(IconVisibilityProperty, value);
        }

        /// <summary>
        /// Riempie il fumetto con i dati di una notifica e avvia il conto alla
        /// rovescia per la chiusura automatica.
        /// </summary>
        /// <param name="title">Titolo in grassetto.</param>
        /// <param name="info">Corpo del messaggio.</param>
        /// <param name="infoFlags">Flag NIIF_* che scelgono l'icona.</param>
        /// <param name="timeoutMs">Durata richiesta, 0 per il valore standard.</param>
        /// <param name="userIcon">Icona propria dell'applicazione (NIIF_USER).</param>
        public void Show(string title, string info, uint infoFlags, uint timeoutMs,
                         ImageSource? userIcon = null)
        {
            Title = title ?? string.Empty;
            Info = info ?? string.Empty;

            ApplyIcon(infoFlags, userIcon);

            TimeSpan duration = timeoutMs > 0
                ? TimeSpan.FromMilliseconds(timeoutMs)
                : DefaultTimeout;

            if (duration < MinTimeout)
            {
                duration = MinTimeout;
            }
            else if (duration > MaxTimeout)
            {
                duration = MaxTimeout;
            }

            _timer.Stop();
            _timer.Interval = duration;
            _timer.Start();
        }

        private void ApplyIcon(uint infoFlags, ImageSource? userIcon)
        {
            uint kind = infoFlags & NIIF_ICON_MASK;

            if (kind == NIIF_USER && userIcon != null)
            {
                IconSource = userIcon;
                IconVisibility = Visibility.Visible;
                return;
            }

            string? asset = kind switch
            {
                NIIF_INFO => "info",
                NIIF_WARNING => "warning",
                NIIF_ERROR => "error",
                _ => null
            };

            if (asset == null)
            {
                // NIIF_NONE: nessuna icona, il testo occupa tutta la larghezza.
                IconSource = null;
                IconVisibility = Visibility.Collapsed;
                return;
            }

            IconSource = LoadThemeIcon(asset);
            IconVisibility = IconSource != null ? Visibility.Visible : Visibility.Collapsed;
        }

        /// <summary>
        /// Recupera l'icona informativa da mostrare nel fumetto.
        ///
        /// Il tema Windows7.xaml non contiene immagini per queste tre icone, e
        /// non e' compito nostro inventarle: si usano quelle di sistema, che
        /// sono esattamente le stesse che Windows disegna nei propri fumetti e
        /// seguono l'aspetto della versione in uso.
        /// </summary>
        private ImageSource? LoadThemeIcon(string name)
        {
            // Un tema che fornisce le proprie immagini ha comunque la
            // precedenza su quelle di sistema.
            if (TryFindResource($"NotifyBalloon{char.ToUpperInvariant(name[0])}{name.Substring(1)}Icon")
                is ImageSource themed)
            {
                return themed;
            }

            IntPtr id = name switch
            {
                "info" => NativeMethods.IDI_INFORMATION,
                "warning" => NativeMethods.IDI_WARNING,
                "error" => NativeMethods.IDI_ERROR,
                _ => IntPtr.Zero
            };

            return id == IntPtr.Zero ? null : NativeMethods.LoadStockIcon(id);
        }

        private void CloseButton_Click(object sender, RoutedEventArgs e) => Dismiss();

        private void Dismiss()
        {
            _timer.Stop();
            Closed?.Invoke(this, EventArgs.Empty);
        }
    }
}
