using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Interop;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Mostra i fumetti di notifica sopra l'area di sistema.
    ///
    /// Il fumetto vive in un Popup senza attivazione: deve comparire davanti a
    /// tutto senza rubare il fuoco alla finestra su cui sta lavorando l'utente,
    /// esattamente come fa Windows.
    /// </summary>
    public sealed class BalloonHost
    {
        /// <summary>
        /// Distanza del vertice della "puntina" dal bordo DESTRO del fumetto.
        /// Deriva dal tema (stile NotifyBalloon, identico nei due temi):
        /// freccia 21x21 con margine destro 13 e geometria "M 0,0 l 20,20 V 0",
        /// cioe' un triangolo rettangolo il cui vertice in BASSO (la punta che
        /// tocca la barra) sta a x=20 dei 21 px del riquadro, NON al centro.
        /// Quindi: 13 + (21 - 20) = 14.
        ///
        /// v1.21.39: valeva 23.5 (13 + 21/2), che supponeva erroneamente la
        /// punta a meta' del riquadro: il fumetto ancorato all'icona finiva
        /// ~9.5 px troppo a destra e la puntina non centrava l'icona.
        /// (RetroBar usa la stessa geometria della freccia e piazza il bordo
        /// destro del fumetto 11 px a destra del bordo destro dell'icona:
        /// la punta cade a 14 - 11 = 3 px dal bordo destro dell'icona; qui
        /// invece la punta viene centrata sull'icona, come in Windows 7.)
        /// </summary>
        private const double TipOffsetFromRightEdge = 14.0;

        /// <summary>
        /// Distacco verticale della punta dall'icona quando si ancorano
        /// direttamente all'icona che ha generato la notifica.
        /// </summary>
        private const double IconAnchorGap = 2;

        private readonly UIElement _anchor;
        private Popup? _popup;
        private NotifyBalloon? _balloon;

        /* v3.7.2: quando il fumetto e' ancorato a un'icona della tray, qui
         * teniamo l'elemento e ne verifichiamo periodicamente la validita':
         * l'icona puo' finire nel pannello di overflow, essere spostata,
         * disattivata o rimossa MENTRE il fumetto e' a video. Se l'ancora
         * non vale piu', il fumetto viene ri-ancorato all'area di notifica,
         * cosi' non compare mai puntato su una posizione sbagliata. */
        private FrameworkElement? _iconAnchorElement;
        private DispatcherTimer? _anchorWatch;

        /// <summary>
        /// Ogni quanto si ricontrolla la posizione del fumetto (v1.21.40:
        /// il watch vale per TUTTI i fumetti, anche quelli ancorati all'area
        /// di notifica: verifica l'ancora e, se il popup non sta dove deve,
        /// lo riporta a posto con SetWindowPos).
        /// </summary>
        private static readonly TimeSpan AnchorWatchInterval =
            TimeSpan.FromMilliseconds(400);

        /// <summary>
        /// Tolleranza (px fisici) fra la posizione calcolata e quella reale
        /// del popup prima di intervenire con SetWindowPos: arrotondamenti
        /// di un pixel fra il piazzamento WPF e il nostro calcolo sono
        /// normali e non vanno corretti.
        /// </summary>
        private const int PositionTolerancePx = 2;

        /// <param name="anchor">
        /// Elemento di ripiego per il posizionamento: di norma l'area di
        /// notifica, cosi' il fumetto resta allineato alla tray.
        /// </param>
        public BalloonHost(UIElement anchor)
        {
            _anchor = anchor ?? throw new ArgumentNullException(nameof(anchor));
        }

        /// <summary>
        /// Mostra una notifica. Se un fumetto e' gia' visibile viene sostituito:
        /// Windows ne tiene a video uno solo per volta.
        /// </summary>
        /// <param name="iconAnchor">
        /// v3.7: l'elemento dell'icona che HA GENERATO la notifica (quando
        /// e' in barra). Il fumetto si ancora a LUI: la punta indica l'icona
        /// giusta, non il bordo dell'intera area di notifica. Null se l'icona
        /// non c'e' (overflow, icona rimossa): si usa il ripiego.
        /// </param>
        /// <param name="userIcon">
        /// v1.21.39: icona dell'applicazione per i fumetti NIIF_USER (il
        /// ripiego legacy su hIcon documentato da Microsoft e usato da
        /// ManagedShell). Null negli altri casi.
        /// </param>
        /// <param name="feedback">
        /// v1.21.39: riceve i codici NIN_BALLOON* da recapitare alla finestra
        /// che ha generato la notifica (show/hide/timeout/click).
        /// </param>
        /// <returns>
        /// La durata applicata al fumetto (TimeSpan.Zero se non e' stato
        /// mostrato), per chi deve pianificare lavori alla sua chiusura.
        /// </returns>
        public TimeSpan Show(string title, string info, uint infoFlags, uint timeoutMs,
                             UIElement? iconAnchor = null, ImageSource? userIcon = null,
                             Action<uint>? feedback = null)
        {
            // Un fumetto senza testo non ha nulla da dire: alcune applicazioni
            // inviano NIF_INFO con stringhe vuote solo per cancellare quello
            // precedente.
            if (string.IsNullOrWhiteSpace(title) && string.IsNullOrWhiteSpace(info))
            {
                Hide();
                return TimeSpan.Zero;
            }

            Hide();

            /* v3.7.1: costruzione e apertura del popup protette: un errore
             * qui (ancora scollegata dall'albero visivo, tema non pronto,
             * configurazione video ostile...) non deve abbattere la barra;
             * si rinuncia al fumetto e basta. */
            try
            {
                _balloon = new NotifyBalloon();
                _balloon.Closed += (_, _) => Hide();

                /* v3.7: l'ancora e' l'icona che ha generato la notifica, se
                 * esiste; altrimenti l'area di notifica (ripiego d'origine).
                 * v1.21.40: la validita' dell'ancora include il collegamento
                 * a una PresentationSource: un contenitore appena ricreato
                 * puo' superare IsLoaded/IsVisible un istante prima di
                 * essere scollegato, e un Popup aperto su un target non
                 * presentato finisce parcheggiato a (0,0) dello schermo -
                 * il "fumetto in alto a sinistra" della 1.21.39. */
                bool anchoredToIcon = iconAnchor is FrameworkElement element
                                      && IsAnchorStillValid(element);
                UIElement effectiveAnchor = anchoredToIcon ? iconAnchor! : _anchor;

                _popup = new Popup
                {
                    Child = _balloon,
                    AllowsTransparency = true,
                    StaysOpen = true,
                    // Senza questo il Popup ruberebbe il fuoco e l'utente si
                    // ritroverebbe a digitare nel vuoto.
                    Focusable = false,
                    PlacementTarget = effectiveAnchor,
                    Placement = PlacementMode.Custom,
                    CustomPopupPlacementCallback = anchoredToIcon
                        ? PlaceTipOnIcon
                        : PlaceAboveAnchor,
                    // Le ombre hardware falliscono su alcune configurazioni video
                    // (e sotto Xvfb fanno terminare il processo).
                    PopupAnimation = PopupAnimation.Fade
                };

                /* v3.7.2 / v1.21.40: sorveglianza continua. Se l'icona
                 * finisce in overflow o viene rimossa mentre il fumetto e'
                 * visibile, il fumetto torna ad ancorarsi all'area di
                 * notifica; in piu' - per QUALSIASI fumetto - ogni giro
                 * verifica dove il popup sta davvero e lo forza nella
                 * posizione giusta (vedi EnforceScreenPosition). */
                _iconAnchorElement = anchoredToIcon ? (FrameworkElement)effectiveAnchor : null;
                StartAnchorWatch();

                TimeSpan shown = _balloon.Show(title, info, infoFlags, timeoutMs,
                                               userIcon, feedback);
                _popup.IsOpen = true;

                /* v1.21.40: prima verifica immediata (dopo la passata di
                 * layout che misura il popup): se WPF ha piazzato il fumetto
                 * altrove - incluso l'angolo (0,0) quando il target non e'
                 * presentabile - viene riportato a posto subito, senza
                 * aspettare il primo giro del watch. */
                Popup openedPopup = _popup;
                _popup.Dispatcher.BeginInvoke(new Action(() =>
                {
                    if (ReferenceEquals(_popup, openedPopup))
                    {
                        EnforceScreenPosition();
                    }
                }), DispatcherPriority.Loaded);

                return shown;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"balloon: fumetto non mostrato: {ex.Message}");
                Hide();
                return TimeSpan.Zero;
            }
        }

        /// <summary>Rimuove il fumetto attualmente visibile, se c'e'.</summary>
        public void Hide()
        {
            /* v3.7.1: protetta: gira anche dall'evento Closed del fumetto e
             * dalla chiusura della barra, punti senza un try/catch attorno. */
            try
            {
                StopAnchorWatch();

                if (_popup != null)
                {
                    _popup.IsOpen = false;
                    _popup.Child = null;
                    _popup = null;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"balloon: errore chiudendo il fumetto: {ex.Message}");
                _popup = null;
            }

            _balloon = null;
        }

        // ---------------------------------------------------------------
        //  v3.7.2 / v1.21.40 - Sorveglianza dell'ancora e della posizione
        // ---------------------------------------------------------------

        /// <summary>
        /// Avvia il controllo periodico del fumetto aperto. Da v1.21.40
        /// gira per TUTTI i fumetti (non solo quelli ancorati a un'icona):
        /// oltre alla validita' dell'ancora verifica la posizione reale del
        /// popup sullo schermo e la corregge quando WPF lo ha piazzato male.
        /// </summary>
        private void StartAnchorWatch()
        {
            StopAnchorWatch();

            _anchorWatch = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = AnchorWatchInterval
            };
            _anchorWatch.Tick += (_, _) =>
            {
                if (_popup == null)
                {
                    StopAnchorWatch();
                    return;
                }

                if (_iconAnchorElement != null &&
                    !IsAnchorStillValid(_iconAnchorElement))
                {
                    /* L'icona non sta piu' in barra (mandata in overflow,
                     * spostata, disattivata o rimossa): continuando ad
                     * ancorare a lei il fumetto punterebbe un punto
                     * sbagliato. Lo si ri-ancora all'area di notifica; il
                     * watch RESTA ATTIVO per la verifica di posizione. */
                    Debug.WriteLine("balloon: ancora non piu' valida, " +
                                    "ri-ancoraggio all'area di notifica");
                    RetargetToTrayAnchor();
                }

                EnforceScreenPosition();
            };
            _anchorWatch.Start();
        }

        /// <summary>Ferma il controllo periodico e dimentica l'ancora.</summary>
        private void StopAnchorWatch()
        {
            if (_anchorWatch != null)
            {
                _anchorWatch.Stop();
                _anchorWatch = null;
            }

            _iconAnchorElement = null;
        }

        /// <summary>
        /// Verifica che l'elemento sia ancora un'ancora utilizzabile:
        /// caricato, visibile, largo e - v1.21.40 - collegato a una
        /// PresentationSource viva. Un Popup il cui PlacementTarget non e'
        /// presentato da nessuna finestra non ha coordinate in cui stare e
        /// WPF lo parcheggia nell'angolo (0,0) dello schermo; PointToScreen
        /// da solo non basta per accorgersene in ogni fase del ciclo.
        /// </summary>
        private static bool IsAnchorStillValid(FrameworkElement element)
        {
            try
            {
                if (!element.IsLoaded || !element.IsVisible ||
                    element.ActualWidth <= 0)
                {
                    return false;
                }

                if (PresentationSource.FromVisual(element) == null)
                {
                    return false;
                }

                element.PointToScreen(new Point(0, 0));
                return true;
            }
            catch
            {
                return false;
            }
        }

        // ---------------------------------------------------------------
        //  v1.21.40 - Posizione forzata (Win32)
        // ---------------------------------------------------------------

        /// <summary>
        /// Verifica dove il popup sta DAVVERO e, se non e' dove dovrebbe,
        /// lo sposta con SetWindowPos.
        ///
        /// Perche' serve: il piazzamento di WPF (PlacementMode.Custom) e'
        /// corretto finche' il PlacementTarget resta presentabile per tutta
        /// la vita del popup; quando il contenitore dell'icona viene
        /// scollegato (icone promosse/rigenerate dai refresh della tray) il
        /// popup perde le coordinate e finisce in alto a sinistra. Il
        /// calcolo qui sotto non dipende da WPF: prende le coordinate
        /// schermo dell'ancora (o dell'area di notifica), la misura del
        /// fumetto e il work area del monitor, e impone il risultato.
        /// </summary>
        private void EnforceScreenPosition()
        {
            try
            {
                if (_popup == null || _balloon == null || !_popup.IsOpen)
                {
                    return;
                }

                /* HWND del PopupRoot: la finestra vera del fumetto. */
                if (PresentationSource.FromVisual(_balloon) is not HwndSource source
                    || source.Handle == IntPtr.Zero)
                {
                    return;
                }

                if (!TryComputeDesiredPosition(out int left, out int top))
                {
                    return;
                }

                if (!NativeMethods.GetWindowRect(source.Handle,
                                                 out NativeMethods.RECT actual))
                {
                    return;
                }

                if (Math.Abs(actual.Left - left) <= PositionTolerancePx &&
                    Math.Abs(actual.Top - top) <= PositionTolerancePx)
                {
                    return;
                }

                Debug.WriteLine($"balloon: posizione forzata da " +
                                $"({actual.Left},{actual.Top}) a ({left},{top})");
                NativeMethods.SetWindowPos(source.Handle, IntPtr.Zero,
                                           left, top, 0, 0,
                                           NativeMethods.SWP_NOSIZE |
                                           NativeMethods.SWP_NOZORDER |
                                           NativeMethods.SWP_NOACTIVATE);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"balloon: verifica posizione fallita: {ex.Message}");
            }
        }

        /// <summary>
        /// Posizione schermo (pixel fisici) che il fumetto deve occupare:
        /// punta centrata sull'icona quando l'ancora e' un'icona valida,
        /// altrimenti bordo destro allineato all'area di notifica - le
        /// stesse geometrie dei callback di piazzamento, ricalcolate in
        /// modo indipendente da WPF e chiuse dentro il work area del
        /// monitor dell'ancora.
        /// </summary>
        private bool TryComputeDesiredPosition(out int left, out int top)
        {
            left = 0;
            top = 0;

            if (_balloon == null || _balloon.ActualWidth <= 0 ||
                _balloon.ActualHeight <= 0)
            {
                return false;
            }

            bool onIcon = _iconAnchorElement != null &&
                          IsAnchorStillValid(_iconAnchorElement);
            if (_anchor is not FrameworkElement trayAnchor)
            {
                return false;
            }

            FrameworkElement anchor = onIcon ? _iconAnchorElement! : trayAnchor;
            if (!onIcon && !IsAnchorStillValid(anchor))
            {
                return false;
            }

            /* PointToScreen restituisce pixel fisici, lo stesso spazio di
             * GetWindowRect/SetWindowPos (lo usano gia' le anteprime DWM). */
            Point a0 = anchor.PointToScreen(new Point(0, 0));
            Point a1 = anchor.PointToScreen(
                new Point(anchor.ActualWidth, anchor.ActualHeight));

            DpiScale dpi = VisualTreeHelper.GetDpi(_balloon);
            double w = _balloon.ActualWidth * dpi.DpiScaleX;
            double h = _balloon.ActualHeight * dpi.DpiScaleY;
            double gap = (onIcon ? IconAnchorGap : 0) * dpi.DpiScaleY;

            double x = onIcon
                ? (a0.X + a1.X) / 2.0 - (w - TipOffsetFromRightEdge * dpi.DpiScaleX)
                : a1.X - w;
            double y = a0.Y - h - gap;   // sopra l'ancora

            /* Work area del monitor dell'ancora: decide il ribaltamento
             * sotto la barra (barra in alto) e il clamp finale. */
            var pt = new NativeMethods.POINT { x = (int)a0.X, y = (int)a0.Y };
            IntPtr monitor = NativeMethods.MonitorFromPoint(
                pt, NativeMethods.MONITOR_DEFAULTTONEAREST);
            var mi = new NativeMethods.MONITORINFO
            {
                cbSize = Marshal.SizeOf<NativeMethods.MONITORINFO>()
            };
            if (monitor != IntPtr.Zero && NativeMethods.GetMonitorInfoW(monitor, ref mi))
            {
                if (y < mi.rcWork.Top)
                {
                    y = a1.Y + gap;      // niente spazio sopra: sotto l'ancora
                }
                if (x + w > mi.rcWork.Right)
                {
                    x = mi.rcWork.Right - w;
                }
                if (x < mi.rcWork.Left)
                {
                    x = mi.rcWork.Left;
                }
                if (y + h > mi.rcWork.Bottom)
                {
                    y = mi.rcWork.Bottom - h;
                }
                if (y < mi.rcWork.Top)
                {
                    y = mi.rcWork.Top;
                }
            }

            left = (int)Math.Round(x);
            top = (int)Math.Round(y);
            return true;
        }

        /// <summary>
        /// Sposta il fumetto dall'icona (non piu' valida) all'area di
        /// notifica: cambia il callback di posizionamento e il target.
        /// v1.21.40: non ci si affida piu' al fatto che WPF ricalcoli il
        /// piazzamento da solo (puo' non accadere se il popup era gia' in
        /// uno stato anomalo): subito dopo questa chiamata il watch impone
        /// la posizione con SetWindowPos.
        /// </summary>
        private void RetargetToTrayAnchor()
        {
            _iconAnchorElement = null;
            if (_popup == null)
            {
                return;
            }

            _popup.CustomPopupPlacementCallback = PlaceAboveAnchor;
            _popup.PlacementTarget = _anchor;
        }

        /// <summary>
        /// v3.7: colloca il fumetto sopra l'icona che lo ha generato, con la
        /// PUNTA centrata sull'icona (la punta sta a TipOffsetFromRightEdge
        /// dal bordo destro del fumetto, quindi il bordo destro va spostato
        /// di quella distanza a destra del centro icona).
        /// </summary>
        private CustomPopupPlacement[] PlaceTipOnIcon(Size popupSize, Size targetSize,
                                                      Point offset)
        {
            /* v3.7.1: il callback gira dentro il posizionamento di WPF, che
             * non perdona le eccezioni: qualsiasi errore ripiega su una
             * collocazione semplice invece di arrivare al dispatcher. */
            try
            {
                /* v3.7.2: se tra un posizionamento e l'altro l'icona ha
                 * smesso di essere un'ancora valida, non calcolare
                 * coordinate relative a lei: si restituisce la collocazione
                 * semplice (il ri-ancoraggio alla tray arriva dal watch). */
                if (_iconAnchorElement == null ||
                    !IsAnchorStillValid(_iconAnchorElement))
                {
                    return new[]
                    {
                        new CustomPopupPlacement(new Point(0, -popupSize.Height),
                                                 PopupPrimaryAxis.Horizontal)
                    };
                }

                // Bordo destro del fumetto: x + popupWidth; la punta e' a
                // (x + popupWidth - TipOffsetFromRightEdge). Vogliamo la punta
                // al centro dell'icona:
                //   x + popupWidth - 14 = targetWidth / 2.
                double x = targetSize.Width / 2.0 - popupSize.Width
                           + TipOffsetFromRightEdge;

                var above = new CustomPopupPlacement(
                    new Point(x, -popupSize.Height - IconAnchorGap),
                    PopupPrimaryAxis.Horizontal);

                // Ripiego se non c'e' spazio sopra (barra ancorata in alto):
                // il fumetto scende sotto l'icona, la punta rovesciata dal tema
                // (AppBarEdge=Top) indica l'icona da sopra.
                var below = new CustomPopupPlacement(
                    new Point(x, IconAnchorGap),
                    PopupPrimaryAxis.Horizontal);

                return new[] { above, below };
            }
            catch
            {
                return new[]
                {
                    new CustomPopupPlacement(new Point(0, -popupSize.Height),
                                             PopupPrimaryAxis.Horizontal)
                };
            }
        }

        /// <summary>
        /// Colloca il fumetto appena sopra l'area di notifica, allineato a
        /// destra come nella barra di Windows 7 (ripiego quando non c'e'
        /// un'icona specifica a cui ancorarsi).
        /// </summary>
        private CustomPopupPlacement[] PlaceAboveAnchor(Size popupSize, Size targetSize,
                                                        Point offset)
        {
            /* v3.7.1: stesso ripiego difensivo di PlaceTipOnIcon. */
            try
            {
                // La punta del riquadro sta a destra: allineiamo il bordo destro del
                // fumetto con quello dell'area di notifica.
                double x = targetSize.Width - popupSize.Width;
                double y = -popupSize.Height;

                var above = new CustomPopupPlacement(new Point(x, y),
                                                     PopupPrimaryAxis.Horizontal);

                // Ripiego se non c'e' spazio sopra (barra ancorata in alto):
                // il fumetto scende sotto la barra.
                var below = new CustomPopupPlacement(new Point(x, targetSize.Height),
                                                     PopupPrimaryAxis.Horizontal);

                return new[] { above, below };
            }
            catch
            {
                return new[]
                {
                    new CustomPopupPlacement(new Point(0, -popupSize.Height),
                                             PopupPrimaryAxis.Horizontal)
                };
            }
        }
    }
}
