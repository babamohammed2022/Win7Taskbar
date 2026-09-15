using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;

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
        /// Deriva dal tema (stile NotifyBalloon): freccia 21x21 con margine
        /// destro 13, quindi 13 + 21/2. Servirebbe per allineare la punta
        /// all'icona che ha generato la notifica.
        /// </summary>
        private const double TipOffsetFromRightEdge = 23.5;

        /// <summary>
        /// Distacco verticale della punta dall'icona quando si ancorano
        /// direttamente all'icona che ha generato la notifica.
        /// </summary>
        private const double IconAnchorGap = 2;

        private readonly UIElement _anchor;
        private Popup? _popup;
        private NotifyBalloon? _balloon;

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
        public void Show(string title, string info, uint infoFlags, uint timeoutMs,
                         UIElement? iconAnchor = null)
        {
            // Un fumetto senza testo non ha nulla da dire: alcune applicazioni
            // inviano NIF_INFO con stringhe vuote solo per cancellare quello
            // precedente.
            if (string.IsNullOrWhiteSpace(title) && string.IsNullOrWhiteSpace(info))
            {
                Hide();
                return;
            }

            Hide();

            _balloon = new NotifyBalloon();
            _balloon.Closed += (_, _) => Hide();

            /* v3.7: l'ancora e' l'icona che ha generato la notifica, se
             * esiste; altrimenti l'area di notifica (ripiego d'origine). */
            bool anchoredToIcon = iconAnchor != null
                                  && iconAnchor.ActualWidth > 0;
            UIElement effectiveAnchor = anchoredToIcon ? (UIElement)iconAnchor! : _anchor;

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
                CustomPopupPlacementCallback = PlaceAboveAnchor,
                // Le ombre hardware falliscono su alcune configurazioni video
                // (e sotto Xvfb fanno terminare il processo).
                PopupAnimation = PopupAnimation.Fade
            };

            _balloon.Show(title, info, infoFlags, timeoutMs);
            _popup.IsOpen = true;
        }

        /// <summary>Rimuove il fumetto attualmente visibile, se c'e'.</summary>
        public void Hide()
        {
            if (_popup != null)
            {
                _popup.IsOpen = false;
                _popup.Child = null;
                _popup = null;
            }

            _balloon = null;
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
            // Bordo destro del fumetto: x + popupWidth; la punta e' a
            // (x + popupWidth - TipOffsetFromRightEdge). Vogliamo la punta al
            // centro dell'icona: x + popupWidth - 23.5 = targetWidth/2.
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

        /// <summary>
        /// Colloca il fumetto appena sopra l'area di notifica, allineato a
        /// destra come nella barra di Windows 7 (ripiego quando non c'e'
        /// un'icona specifica a cui ancorarsi).
        /// </summary>
        private CustomPopupPlacement[] PlaceAboveAnchor(Size popupSize, Size targetSize,
                                                        Point offset)
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
    }
}
