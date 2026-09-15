using System;
using System.Windows;
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
        private readonly UIElement _anchor;
        private Popup? _popup;
        private NotifyBalloon? _balloon;

        /// <param name="anchor">
        /// Elemento sopra il quale far comparire il fumetto: di norma l'area di
        /// notifica, cosi' la puntina del riquadro indica l'icona giusta.
        /// </param>
        public BalloonHost(UIElement anchor)
        {
            _anchor = anchor ?? throw new ArgumentNullException(nameof(anchor));
        }

        /// <summary>
        /// Mostra una notifica. Se un fumetto e' gia' visibile viene sostituito:
        /// Windows ne tiene a video uno solo per volta.
        /// </summary>
        public void Show(string title, string info, uint infoFlags, uint timeoutMs)
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

            _popup = new Popup
            {
                Child = _balloon,
                AllowsTransparency = true,
                StaysOpen = true,
                // Senza questo il Popup ruberebbe il fuoco e l'utente si
                // ritroverebbe a digitare nel vuoto.
                Focusable = false,
                PlacementTarget = _anchor,
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
        /// Colloca il fumetto appena sopra l'area di notifica, allineato a
        /// destra come nella barra di Windows 7.
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
