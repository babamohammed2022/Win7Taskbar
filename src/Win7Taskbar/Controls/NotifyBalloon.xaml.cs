using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
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
    ///
    /// v1.21.39 - allineamento a RetroBar/ManagedShell e alla documentazione
    /// NOTIFYICONDATA (learn.microsoft.com, ns-shellapi-notifyicondataw):
    ///  - icona assegnata direttamente all'Image (il fumetto vive in un Popup
    ///    staccato dall'albero della finestra: i binding RelativeSource li'
    ///    dentro sono un anello debole) e nascosta quando il titolo e' vuoto
    ///    ("If the szInfoTitle member is zero-length, the icon is not shown");
    ///  - NIIF_USER usa l'icona dell'applicazione passata dal chiamante
    ///    (ripiego legacy su hIcon, come ManagedShell.NotificationBalloon);
    ///  - durata: richiesta valida (>= 1 s) oppure SPI_GETMESSAGEDURATION,
    ///    perche' da Vista uTimeout e' deprecato e vale l'accessibilita';
    ///  - suono "SystemNotification" all'apertura, salvo NIIF_NOSOUND;
    ///  - feedback NIN_BALLOON* all'applicazione (show / hide / timeout /
    ///    click), la stessa semantica wParam/lParam di ManagedShell.
    /// </summary>
    public partial class NotifyBalloon : UserControl
    {
        // Flag NIIF_* di Shell_NotifyIcon: scelgono l'icona del fumetto.
        private const uint NIIF_NONE = 0x0;
        private const uint NIIF_INFO = 0x1;
        private const uint NIIF_WARNING = 0x2;
        private const uint NIIF_ERROR = 0x3;
        private const uint NIIF_USER = 0x4;
        private const uint NIIF_NOSOUND = 0x10;
        private const uint NIIF_ICON_MASK = 0xF;

        /* Codici NIN_* (WM_USER + n) che la shell recapita alla finestra
         * proprietaria dell'icona tramite uCallbackMessage: dicono
         * all'applicazione cosa e' successo al suo fumetto (shellapi.h). */
        private const uint NIN_BALLOONSHOW = 0x402;
        private const uint NIN_BALLOONHIDE = 0x403;
        private const uint NIN_BALLOONTIMEOUT = 0x404;
        private const uint NIN_BALLOONUSERCLICK = 0x405;

        /// <summary>
        /// Durata usata quando ne' l'applicazione ne' il sistema ne indicano
        /// una. Windows ignora da tempo i timeout richiesti e applica il
        /// valore di accessibilita' del sistema, che di norma vale 5 secondi.
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

        /* Chi ha generato il fumetto va informato della sua fine: questa
         * callback (costruita da TaskbarWindow con i dati dell'icona: hwnd,
         * uid, uCallbackMessage, versione) riceve il codice NIN_*. */
        private Action<uint>? _feedback;

        public NotifyBalloon()
        {
            InitializeComponent();

            _timer = new DispatcherTimer();
            _timer.Tick += (_, _) => Dismiss(NIN_BALLOONTIMEOUT);

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
        // la Visibility come proprieta' di dipendenza. Il trigger del tema la
        // legge per azzerare il rientro del testo quando l'icona non c'e'.
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
        /// <param name="timeoutMs">Durata richiesta, 0 per il valore di sistema.</param>
        /// <param name="userIcon">Icona propria dell'applicazione (NIIF_USER).</param>
        /// <param name="feedback">Riceve i codici NIN_BALLOON* per l'applicazione.</param>
        /// <returns>La durata effettivamente applicata al fumetto.</returns>
        public TimeSpan Show(string title, string info, uint infoFlags, uint timeoutMs,
                             ImageSource? userIcon = null, Action<uint>? feedback = null)
        {
            Title = title ?? string.Empty;
            Info = info ?? string.Empty;
            _feedback = feedback;

            ApplyIcon(infoFlags, userIcon);

            TimeSpan duration = ComputeDuration(timeoutMs);

            _timer.Stop();
            _timer.Interval = duration;
            _timer.Start();

            /* Windows 7 accompagna l'apertura del fumetto col suono di
             * notifica del sistema, salvo NIIF_NOSOUND (RetroBar fa lo
             * stesso: SoundHelper.PlayNotificationSound). */
            if ((infoFlags & NIIF_NOSOUND) == 0)
            {
                NativeMethods.PlayNotificationSound();
            }

            SendFeedback(NIN_BALLOONSHOW);

            return duration;
        }

        /// <summary>
        /// Durata visibile del fumetto. Da Vista in poi uTimeout e' deprecato:
        /// vale la richiesta solo se plausibile (>= 1 s, come in ManagedShell),
        /// altrimenti l'impostazione di accessibilita' del sistema
        /// (SPI_GETMESSAGEDURATION). Resta il clamp 4..30 s gia' in uso: senza,
        /// un programma potrebbe inchiodare un fumetto sullo schermo.
        /// </summary>
        public static TimeSpan ComputeDuration(uint timeoutMs)
        {
            TimeSpan duration;
            if (timeoutMs >= 1000)
            {
                duration = TimeSpan.FromMilliseconds(timeoutMs);
            }
            else
            {
                uint seconds = NativeMethods.GetMessageDurationSeconds();
                duration = seconds > 0
                    ? TimeSpan.FromSeconds(seconds)
                    : DefaultTimeout;
            }

            if (duration < MinTimeout)
            {
                duration = MinTimeout;
            }
            else if (duration > MaxTimeout)
            {
                duration = MaxTimeout;
            }

            return duration;
        }

        private void ApplyIcon(uint infoFlags, ImageSource? userIcon)
        {
            /* Documentazione NOTIFYICONDATA, dwInfoFlags: "The icon is placed
             * to the left of the title. If the szInfoTitle member is
             * zero-length, the icon is not shown." */
            if (string.IsNullOrEmpty(Title))
            {
                SetIcon(null);
                return;
            }

            uint kind = infoFlags & NIIF_ICON_MASK;

            if (kind == NIIF_USER)
            {
                /* NIIF_USER: da Vista Windows usa hBalloonIcon; se manca, il
                 * comportamento legacy usa hIcon, cioe' l'icona dell'icona di
                 * tray. ManagedShell.NotificationBalloon fa esattamente questo
                 * doppio ripiego (hBalloonIcon -> hIcon -> icona predefinita);
                 * qui il chiamante passa il bitmap dell'icona di tray, perche'
                 * hBalloonIcon vive nel processo mittente e non attraversa il
                 * confine nativo/gestito. */
                SetIcon(userIcon);
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
                SetIcon(null);
                return;
            }

            SetIcon(LoadThemeIcon(asset));
        }

        /// <summary>
        /// Assegna l'icona sia alla proprieta' di dipendenza (il trigger del
        /// tema la legge per il rientro del testo) sia direttamente all'Image:
        /// il fumetto sta in un Popup senza albero genitoriale e il binding
        /// RelativeSource del XAML, li' dentro, e' solo un modo in piu' per
        /// perdere l'icona.
        /// </summary>
        private void SetIcon(ImageSource? icon)
        {
            IconSource = icon;
            IconVisibility = icon != null ? Visibility.Visible : Visibility.Collapsed;

            BalloonIcon.Source = icon;
            BalloonIcon.Visibility = IconVisibility;
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

        /// <summary>
        /// Click sul corpo del fumetto: l'applicazione riceve
        /// NIN_BALLOONUSERCLICK e il fumetto si chiude (RetroBar:
        /// ContentControl_MouseLeftButtonUp -> balloonInfo.Click()).
        /// </summary>
        private void Balloon_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            Dismiss(NIN_BALLOONUSERCLICK);
            e.Handled = true;
        }

        private void CloseButton_Click(object sender, RoutedEventArgs e) =>
            Dismiss(NIN_BALLOONHIDE);

        private void SendFeedback(uint ninCode)
        {
            if (_feedback == null)
            {
                return;
            }

            /* Un'applicazione appesa non deve trascinare la barra: la
             * callback usa SendNotifyMessage (non bloccante) ed e' comunque
             * protetta qui. */
            try
            {
                _feedback(ninCode);
            }
            catch (Exception)
            {
                _feedback = null;
            }
        }

        private void Dismiss(uint ninCode)
        {
            _timer.Stop();
            SendFeedback(ninCode);
            Closed?.Invoke(this, EventArgs.Empty);
        }
    }
}
