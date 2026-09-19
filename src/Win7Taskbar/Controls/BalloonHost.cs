using System;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Threading;

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
        /// Ogni quanto si ricontrolla che l'ancora dell'icona valga ancora.
        /// </summary>
        private static readonly TimeSpan AnchorWatchInterval =
            TimeSpan.FromMilliseconds(400);

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
                 * esiste; altrimenti l'area di notifica (ripiego d'origine). */
                bool anchoredToIcon = iconAnchor is FrameworkElement element
                                      && element.IsLoaded
                                      && element.IsVisible
                                      && element.ActualWidth > 0;
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

                /* v3.7.2: sorveglianza dell'ancora: se l'icona finisce in
                 * overflow o viene rimossa mentre il fumetto e' visibile, il
                 * fumetto torna ad ancorarsi all'area di notifica. */
                _iconAnchorElement = anchoredToIcon ? (FrameworkElement)effectiveAnchor : null;
                StartAnchorWatch();

                TimeSpan shown = _balloon.Show(title, info, infoFlags, timeoutMs,
                                               userIcon, feedback);
                _popup.IsOpen = true;
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
        //  v3.7.2 - Sorveglianza dell'ancora dell'icona
        // ---------------------------------------------------------------

        /// <summary>
        /// Avvia il controllo periodico dell'icona a cui il fumetto e'
        /// ancorato. Senza ancora (ripiego sulla tray) non serve.
        /// </summary>
        private void StartAnchorWatch()
        {
            StopAnchorWatch();
            if (_iconAnchorElement == null)
            {
                return;
            }

            _anchorWatch = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = AnchorWatchInterval
            };
            _anchorWatch.Tick += (_, _) =>
            {
                if (_popup == null || _iconAnchorElement == null)
                {
                    StopAnchorWatch();
                    return;
                }

                if (IsAnchorStillValid(_iconAnchorElement))
                {
                    return;
                }

                /* L'icona non sta piu' in barra (mandata in overflow,
                 * spostata, disattivata o rimossa): continuando ad
                 * ancorare a lei il fumetto punterebbe un punto
                 * sbagliato. Lo si ri-ancora all'area di notifica. */
                Debug.WriteLine("balloon: ancora non piu' valida, " +
                                "ri-ancoraggio all'area di notifica");
                StopAnchorWatch();
                RetargetToTrayAnchor();
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
        /// Verifica che l'elemento dell'icona sia ancora un'ancora
        /// utilizzabile: caricato, visibile, largo e collegato a una
        /// finestra vera (PointToScreen alza un'eccezione se l'elemento
        /// non e' piu' presentato da nessuna finestra).
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

                element.PointToScreen(new Point(0, 0));
                return true;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Sposta il fumetto dall'icona (non piu' valida) all'area di
        /// notifica: cambia il callback di posizionamento e il target; la
        /// variazione del target fa ricalcolare subito il piazzamento a WPF.
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
