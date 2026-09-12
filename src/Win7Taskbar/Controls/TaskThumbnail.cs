// Win7Taskbar - per-window preview control (temporarily disabled)
//
// PORTED FROM RetroBar - Copyright (c) dremin
// https://github.com/dremin/RetroBar  -  Apache License 2.0
// Original file: RetroBar/Controls/TaskThumbnail.xaml.cs
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
// v2.56 - WINDOW THUMBNAILS: TEMPORARILY DISABLED
//
// Status in the taskbar:
//   * Window thumbnails: DISABLED (this control is a no-op).
//   * Tooltips: SUPPORTED (they live in TaskbarWindow.xaml, unaffected).
//
// Why: both implementations we shipped produced *unwanted rectangles* drawn
// inside the preview popup (an empty grey/white box, identical for every
// window). The two failure modes were different but the visible result was
// the same, so the feature is parked instead of shipped half-broken:
//
//   * v2.54 and earlier - live DWM thumbnail (DwmRegisterThumbnail +
//     DwmUpdateThumbnailProperties). The compositor paints the source window
//     into a rectangle of ours, frame by frame. It can fail *silently*: the
//     registration succeeds, the call returns S_OK, and the compositor never
//     paints anything, so what remains on screen is just the popup backdrop.
//     There is no return value to check and no event to react to.
//   * v2.55 - static capture with PrintWindow into a GDI bitmap. It fails
//     loudly (PrintWindow returns FALSE and we fall back to the app icon),
//     but on real hardware it still showed up as an empty rectangle for the
//     windows the user tried, and it made the popup feel frozen (a still
//     image instead of a moving one).
//
// Thumbnail previews will be reintroduced in a future version, once the
// preview rendering system has been properly implemented (see the README).
// Until then the popup is not opened at all (TaskbarWindow.xaml.cs keeps a
// single switch for that) and hovering a task button shows only the app-name
// tooltip, which is the part we can guarantee works.
//
// The previous implementations are kept verbatim at the bottom of this file,
// commented out, as the reference for that future work: the DWM one is the
// starting point, and the notes above describe what has to be proven before
// it can be re-enabled (a positive confirmation that the compositor actually
// painted the thumbnail, not just that the call succeeded).
// ============================================================================

using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Win7Taskbar.Controls
{
    /// <summary>
    /// Placeholder for the per-window preview. It keeps the exact public API
    /// the XAML expects (so the preview item template still loads if the popup
    /// is re-enabled by flipping the switch in TaskbarWindow.xaml.cs) but it
    /// never registers a DWM thumbnail, never captures a window and never runs
    /// any per-frame work: it reports "no thumbnail available" and shows the
    /// application icon instead.
    /// </summary>
    public sealed class TaskThumbnail : UserControl
    {
        private const double MaxWidth_ = 180;
        private const double MaxHeight_ = 120;

        private readonly Image _image;

        public double DpiScale { get; private set; } = 1.0;

        public TaskThumbnail()
        {
            Width = MaxWidth_;
            Height = MaxHeight_;

            _image = new Image
            {
                Stretch = Stretch.Uniform,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
            };
            RenderOptions.SetBitmapScalingMode(_image, BitmapScalingMode.HighQuality);
            Content = _image;

            Loaded += OnLoaded;
        }

        /// <summary>
        /// Icon shown when no thumbnail is available (which, with thumbnails
        /// disabled, is always the case).
        /// </summary>
        public static readonly DependencyProperty FallbackIconProperty =
            DependencyProperty.Register(nameof(FallbackIcon), typeof(ImageSource),
                typeof(TaskThumbnail), new PropertyMetadata(null, OnFallbackIconChanged));

        public ImageSource? FallbackIcon
        {
            get => (ImageSource?)GetValue(FallbackIconProperty);
            set => SetValue(FallbackIconProperty, value);
        }

        private static void OnFallbackIconChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if (d is TaskThumbnail t && !t.IsThumbnailAvailable)
            {
                t._image.Source = t.FallbackIcon;
            }
        }

        public static readonly DependencyProperty SourceWindowHandleProperty =
            DependencyProperty.Register(nameof(SourceWindowHandle), typeof(IntPtr),
                typeof(TaskThumbnail), new PropertyMetadata(IntPtr.Zero));

        /// <summary>Window whose preview would be shown. Unused while disabled.</summary>
        public IntPtr SourceWindowHandle
        {
            get => (IntPtr)GetValue(SourceWindowHandleProperty);
            set => SetValue(SourceWindowHandleProperty, value);
        }

        public static readonly DependencyProperty TitleProperty =
            DependencyProperty.Register(nameof(Title), typeof(string),
                typeof(TaskThumbnail), new PropertyMetadata(string.Empty));

        public string Title
        {
            get => (string)GetValue(TitleProperty);
            set => SetValue(TitleProperty, value);
        }

        private static readonly DependencyPropertyKey IsThumbnailAvailablePropertyKey =
            DependencyProperty.RegisterReadOnly(nameof(IsThumbnailAvailable), typeof(bool),
                typeof(TaskThumbnail), new PropertyMetadata(false));

        public static readonly DependencyProperty IsThumbnailAvailableProperty =
            IsThumbnailAvailablePropertyKey.DependencyProperty;

        /// <summary>Always false while thumbnails are disabled.</summary>
        public bool IsThumbnailAvailable
        {
            get => (bool)GetValue(IsThumbnailAvailableProperty);
            private set
            {
                SetValue(IsThumbnailAvailablePropertyKey, value);
                SetValue(FallbackVisibilityPropertyKey,
                         value ? Visibility.Collapsed : Visibility.Visible);
                _image.Visibility = value ? Visibility.Visible : Visibility.Collapsed;
                if (!value)
                {
                    _image.Source = FallbackIcon;
                }
            }
        }

        private static readonly DependencyPropertyKey FallbackVisibilityPropertyKey =
            DependencyProperty.RegisterReadOnly(nameof(FallbackVisibility), typeof(Visibility),
                typeof(TaskThumbnail), new PropertyMetadata(Visibility.Visible));

        public static readonly DependencyProperty FallbackVisibilityProperty =
            FallbackVisibilityPropertyKey.DependencyProperty;

        /// <summary>Visible when the live preview is not available.</summary>
        public Visibility FallbackVisibility => (Visibility)GetValue(FallbackVisibilityProperty);

        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            // No DWM registration, no capture, no rendering hook: the control
            // only ever shows the fallback icon. Kept as an explicit step so
            // the disabled state is visible in the code and not implied.
            IsThumbnailAvailable = false;
        }
    }
}

// ============================================================================
// LEGACY IMPLEMENTATIONS - KEPT FOR REFERENCE ONLY. DO NOT RE-ENABLE AS IS.
//
// Both variants below are the code that shipped up to v2.55, in the order they
// were written. They are commented out because each of them produced unwanted
// rectangles inside the preview on real hardware (see the header of this file
// for the full explanation). They are preserved because they are the starting
// point for the future, properly implemented preview system: whoever picks
// this up should first add a *positive* confirmation that the source window
// has actually been painted (a real pixel read-back, not just a success code),
// and only then turn the popup back on.
//
// ----------------------------------------------------------------------------
// #1 - LIVE DWM THUMBNAIL - shipped up to v2.54 (the "previous behaviour")
// ----------------------------------------------------------------------------
// // Win7Taskbar - Anteprima live di una finestra (DWM thumbnail)
// //
// // PORTATO DA RetroBar - Copyright (c) dremin
// // https://github.com/dremin/RetroBar  -  Apache License 2.0
// // File originale: RetroBar/Controls/TaskThumbnail.xaml.cs
// //
// // This program is free software: you can redistribute it and/or modify
// // it under the terms of the GNU General Public License as published by
// // the Free Software Foundation, either version 3 of the License, or
// // (at your option) any later version.
// //
// // This program is distributed in the hope that it will be useful,
// // but WITHOUT ANY WARRANTY; without even the implied warranty of
// // MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// // GNU General Public License for more details.
// //
// // You should have received a copy of the GNU General Public License
// // along with this program.  If not, see <https://www.gnu.org/licenses/>.
// //
// // ============================================================================
// // NOTA SULLA DERIVAZIONE
// //
// // Questo file e' un adattamento diretto di TaskThumbnail di RetroBar. La
// // logica e' la sua: registrazione del thumbnail DWM, calcolo del rettangolo
// // di destinazione, scalatura che preserva le proporzioni distinguendo il caso
// // "finestra piccola" (nessuna scalatura) da "finestra grande" (adatta a
// // 180x120 mantenendo l'aspect ratio) e aggiornamento a ogni frame tramite
// // CompositionTarget.Rendering.
// //
// // Adattamenti: i P/Invoke DWM vivono in Win7Taskbar.Interop invece che in
// // ManagedShell.Interop, e le proprieta' sono agganciate ai nostri modelli.
// // ============================================================================
//
// using System;
// using System.Windows;
// using System.Windows.Controls;
// using System.Windows.Data;
// using System.Windows.Interop;
// using System.Windows.Media;
// using System.Windows.Threading;
// using Win7Taskbar.Interop;
//
// namespace Win7Taskbar.Controls
// {
//     /// <summary>
//     /// Mostra l'anteprima dal vivo di una finestra usando i thumbnail del DWM.
//     /// E' la stessa tecnica della Superbar di Windows 7: non si cattura uno
//     /// screenshot, si chiede al compositore di ridisegnare la finestra dentro
//     /// un rettangolo della nostra.
//     /// </summary>
//     public sealed class TaskThumbnail : UserControl
//     {
//         private const double MaxWidth_ = 180;
//         private const double MaxHeight_ = 120;
//
//         private IntPtr _thumbHandle;
//         private EventHandler? _renderingHandler;
//
//         /* v2.52: la passata di layout serve SOLO quando la dimensione del
//          * controllo cambia. Prima Refresh() la forzava a ogni frame
//          * (UpdateLayout dentro il ciclo di rendering): con il riquadro a
//          * schermo significava una misura/organizzazione completa del popup
//          * 60 volte al secondo per OGNI finestra del gruppo, ed e' una delle
//          * cause del processore sempre alto mentre l'anteprima e' aperta.
//          * Il riposizionamento del thumbnail non ne ha bisogno: la posizione
//          * si legge dal transform, che e' aggiornato comunque. */
//         private bool _layoutPending = true;
//         private readonly Image _fallbackImage;
//
//         public double DpiScale { get; private set; } = 1.0;
//
//         public TaskThumbnail()
//         {
//             Width = MaxWidth_;
//             Height = MaxHeight_;
//
//             // v2.2: il ripiego (icona dell'app) vive DENTRO il controllo ed
//             // e' mutuamente esclusivo con la miniatura DWM: FallbackVisibility
//             // lo mostra SOLO quando il DWM non ha potuto registrare il
//             // thumbnail. Prima il ripiego era un Image separato nello stesso
//             // DataTemplate, legato con ElementName=Thumb: dentro un ToolTip
//             // l'albero visuale e' separato e quel binding non risolveva,
//             // quindi i due livelli restavano VISIBILI ENTRAMBI, uno sopra
//             // l'altro. Ora e' strutturalmente impossibile averli insieme.
//             // Il contenuto (Image) non sposta il rettangolo DWM: il
//             // DestinationRect e' calcolato sulla posizione del controllo,
//             // non sui suoi figli.
//             _fallbackImage = new Image
//             {
//                 Width = 48,
//                 Height = 48,
//                 Stretch = Stretch.Uniform,
//                 Opacity = 0.85,
//                 HorizontalAlignment = HorizontalAlignment.Center,
//                 VerticalAlignment = VerticalAlignment.Center,
//             };
//             RenderOptions.SetBitmapScalingMode(_fallbackImage, BitmapScalingMode.HighQuality);
//             _fallbackImage.SetBinding(Image.SourceProperty,
//                 new Binding(nameof(FallbackIcon)) { Source = this });
//             _fallbackImage.SetBinding(VisibilityProperty,
//                 new Binding(nameof(FallbackVisibility)) { Source = this });
//             Content = _fallbackImage;
//
//             Loaded += OnLoaded;
//             Unloaded += OnUnloaded;
//         }
//
//         /// <summary>
//         /// v2.2: icona da mostrare SOLO quando il DWM non fornisce la
//         /// miniatura (ripiego rarissimo: composizione spenta, RDP ridotto,
//         /// API assente). Mai insieme alla miniatura viva.
//         /// </summary>
//         public static readonly DependencyProperty FallbackIconProperty =
//             DependencyProperty.Register(nameof(FallbackIcon), typeof(ImageSource),
//                 typeof(TaskThumbnail), new PropertyMetadata(null));
//
//         public ImageSource? FallbackIcon
//         {
//             get => (ImageSource?)GetValue(FallbackIconProperty);
//             set => SetValue(FallbackIconProperty, value);
//         }
//
//         public static readonly DependencyProperty SourceWindowHandleProperty =
//             DependencyProperty.Register(nameof(SourceWindowHandle), typeof(IntPtr),
//                 typeof(TaskThumbnail), new PropertyMetadata(IntPtr.Zero));
//
//         /// <summary>Finestra di cui mostrare l'anteprima.</summary>
//         public IntPtr SourceWindowHandle
//         {
//             get => (IntPtr)GetValue(SourceWindowHandleProperty);
//             set => SetValue(SourceWindowHandleProperty, value);
//         }
//
//         public static readonly DependencyProperty TitleProperty =
//             DependencyProperty.Register(nameof(Title), typeof(string),
//                 typeof(TaskThumbnail), new PropertyMetadata(string.Empty));
//
//         public string Title
//         {
//             get => (string)GetValue(TitleProperty);
//             set => SetValue(TitleProperty, value);
//         }
//
//         private static readonly DependencyPropertyKey IsThumbnailAvailablePropertyKey =
//             DependencyProperty.RegisterReadOnly(nameof(IsThumbnailAvailable), typeof(bool),
//                 typeof(TaskThumbnail), new PropertyMetadata(true));
//
//         public static readonly DependencyProperty IsThumbnailAvailableProperty =
//             IsThumbnailAvailablePropertyKey.DependencyProperty;
//
//         /// <summary>
//         /// False quando il DWM non ha potuto registrare la miniatura: il
//         /// contenitore puo' allora mostrare un ripiego (icona + titolo)
//         /// invece di un riquadro vuoto.
//         /// </summary>
//         public bool IsThumbnailAvailable
//         {
//             get => (bool)GetValue(IsThumbnailAvailableProperty);
//             private set
//             {
//                 SetValue(IsThumbnailAvailablePropertyKey, value);
//                 SetValue(FallbackVisibilityPropertyKey,
//                          value ? Visibility.Collapsed : Visibility.Visible);
//             }
//         }
//
//         private static readonly DependencyPropertyKey FallbackVisibilityPropertyKey =
//             DependencyProperty.RegisterReadOnly(nameof(FallbackVisibility), typeof(Visibility),
//                 typeof(TaskThumbnail), new PropertyMetadata(Visibility.Collapsed));
//
//         public static readonly DependencyProperty FallbackVisibilityProperty =
//             FallbackVisibilityPropertyKey.DependencyProperty;
//
//         /// <summary>
//         /// Visible quando la miniatura NON e' disponibile. Esposta gia' come
//         /// Visibility per non dover passare da un converter: Binding.Converter
//         /// non e' una DependencyProperty, quindi non accetta DynamicResource, e
//         /// uno StaticResource dentro un DataTemplate annidato in un ToolTip non
//         /// e' garantito che si risolva.
//         /// </summary>
//         public Visibility FallbackVisibility => (Visibility)GetValue(FallbackVisibilityProperty);
//
//         /// <summary>Handle della finestra WPF che ospita l'anteprima.</summary>
//         private IntPtr HostHandle
//         {
//             get
//             {
//                 if (PresentationSource.FromVisual(this) is HwndSource source)
//                 {
//                     return source.Handle;
//                 }
//                 return IntPtr.Zero;
//             }
//         }
//
//         /// <summary>
//         /// Rettangolo di destinazione in coordinate del contenitore, in pixel
//         /// fisici: il DWM non ragiona in DIP.
//         /// </summary>
//         private NativeMethods.RECT DestinationRect
//         {
//             get
//             {
//                 try
//                 {
//                     // Il rettangolo va espresso rispetto alla RADICE della
//                     // finestra che ospita l'anteprima (l'HWND del popup), non
//                     // rispetto al genitore diretto: il DWM disegna dentro
//                     // quell'HWND e ignora l'albero WPF. Usando il genitore, se
//                     // fra il controllo e la radice c'e' anche un solo
//                     // contenitore (un Border, uno StackPanel) la miniatura
//                     // finisce fuori posto - tipicamente sopra il bordo
//                     // superiore - e il riquadro appare VUOTO.
//                     if (PresentationSource.FromVisual(this)?.RootVisual is not Visual root)
//                     {
//                         return default;
//                     }
//
//                     GeneralTransform transform = TransformToAncestor(root);
//                     Point topLeft = transform.Transform(new Point(0, 0));
//
//                     return new NativeMethods.RECT
//                     {
//                         Left = (int)(topLeft.X * DpiScale),
//                         Top = (int)(topLeft.Y * DpiScale),
//                         Right = (int)(topLeft.X * DpiScale) + (int)(MaxWidth_ * DpiScale),
//                         Bottom = (int)(topLeft.Y * DpiScale) + (int)(MaxHeight_ * DpiScale)
//                     };
//                 }
//                 catch (InvalidOperationException)
//                 {
//                     return default;
//                 }
//             }
//         }
//
//         /// <summary>
//         /// Riallinea il thumbnail alla posizione e alle dimensioni correnti.
//         /// La logica di scalatura e' quella di RetroBar.
//         /// </summary>
//         public void Refresh()
//         {
//             if (_thumbHandle == IntPtr.Zero)
//             {
//                 return;
//             }
//
//             // Mostriamo solo l'area client: senza questo comparirebbero anche
//             // il bordo e la barra del titolo della finestra sorgente.
//             var clientAreaProps = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
//             {
//                 dwFlags = NativeMethods.DWM_TNP_SOURCECLIENTAREAONLY,
//                 fSourceClientAreaOnly = true
//             };
//             NativeMethods.DwmUpdateThumbnailProperties(_thumbHandle, ref clientAreaProps);
//
//             /* v2.54: la lettura della dimensione della sorgente NON deve piu'
//              * bloccare la visibilita' del thumbnail. Prima, se questa query
//              * falliva (capita nel primissimo frame, quando il DWM non ha
//              * ancora interrogato la finestra appena registrata) la funzione
//              * usciva SENZA impostare DWM_TNP_VISIBLE: il thumbnail restava
//              * registrato ma invisibile, e al suo posto si vedeva soltanto lo
//              * sfondo del popup (un rettangolo pieno, identico per qualunque
//              * finestra). Ora l'adattamento delle proporzioni e' un
//              * miglioramento successivo, non un prerequisito per vedere
//              * qualcosa: la visibilita' si imposta SEMPRE, in fondo. */
//             bool sizeOk = NativeMethods.DwmQueryThumbnailSourceSize(_thumbHandle, out NativeMethods.SIZE size) == 0
//                           && size.cx > 0 && size.cy > 0;
//
//             double aspectRatio = sizeOk ? (double)size.cx / size.cy : (MaxWidth_ / MaxHeight_);
//
//             // v2.6: ordine corretto delle operazioni (prima si decide la
//             // dimensione del controllo, POI si legge la posizione a schermo).
//             // DestinationRect usa TransformToAncestor: se Width/Height
//             // cambiano DOPO averlo letto, il popup che centra/allinea il
//             // controllo (tooltip/anteprima) si riposiziona ma il rettangolo
//             // di destinazione del DWM resta calcolato sul frame precedente e
//             // la miniatura appare ancorata nel punto sbagliato, lasciando
//             // vedere lo sfondo della cornice attorno (riquadro bianco).
//             // 1) Prima Width/Height... (in variabili locali: si toccano le
//             //    proprieta' solo se il valore cambia davvero, altrimenti ogni
//             //    frameInvalidate avrebbe invalidato misura e organizzazione).
//             double wantWidth;
//             double wantHeight;
//
//             if (!sizeOk)
//             {
//                 wantWidth = Width;
//                 wantHeight = Height;
//             }
//             else if (size.cx <= MaxWidth_ * DpiScale && size.cy <= MaxHeight_ * DpiScale)
//             {
//                 // Finestra piccola: nessuna scalatura, si mostra 1:1.
//                 wantWidth = size.cx / DpiScale;
//                 wantHeight = size.cy / DpiScale;
//             }
//             else
//             {
//                 // Finestra grande: si adatta al riquadro conservando le proporzioni.
//                 const double controlAspectRatio = MaxWidth_ / MaxHeight_;
//
//                 if (aspectRatio > controlAspectRatio)
//                 {
//                     wantWidth = MaxWidth_;
//                     wantHeight = (int)(MaxWidth_ / aspectRatio);
//                 }
//                 else if (aspectRatio < controlAspectRatio)
//                 {
//                     wantWidth = (int)(MaxHeight_ * aspectRatio);
//                     wantHeight = MaxHeight_;
//                 }
//                 else
//                 {
//                     wantWidth = MaxWidth_;
//                     wantHeight = MaxHeight_;
//                 }
//             }
//
//             if (Math.Abs(Width - wantWidth) > 0.01 ||
//                 Math.Abs(Height - wantHeight) > 0.01)
//             {
//                 Width = wantWidth;
//                 Height = wantHeight;
//                 _layoutPending = true;
//             }
//
//             // 2) ...POI DestinationRect. La passata di layout si fa solo se
//             // la dimensione e' appena cambiata: cosi' il popup si riposiziona
//             // PRIMA che il DWM disegni (Left/Top riflettono il controllo gia'
//             // ridimensionato) senza pagare una misura completa a ogni frame.
//             if (_layoutPending)
//             {
//                 UpdateLayout();
//                 _layoutPending = false;
//             }
//
//             NativeMethods.RECT dest = DestinationRect;
//             dest.Right = dest.Left + (int)(Width * DpiScale);
//             dest.Bottom = dest.Top + (int)(Height * DpiScale);
//
//             var props = new NativeMethods.DWM_THUMBNAIL_PROPERTIES
//             {
//                 fVisible = true,
//                 dwFlags = NativeMethods.DWM_TNP_VISIBLE | NativeMethods.DWM_TNP_RECTDESTINATION,
//                 rcDestination = dest
//             };
//
//             NativeMethods.DwmUpdateThumbnailProperties(_thumbHandle, ref props);
//         }
//
//         private void OnLoaded(object sender, RoutedEventArgs e)
//         {
//             if (PresentationSource.FromVisual(this)?.CompositionTarget is { } target)
//             {
//                 DpiScale = target.TransformToDevice.M11;
//             }
//
//             // Anche queste uscite anticipate devono accendere il ripiego:
//             // senza composizione (o senza un handle valido) non ci sara' mai
//             // una miniatura, e il riquadro resterebbe vuoto.
//             if (!NativeMethods.IsCompositionEnabled())
//             {
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             if (SourceWindowHandle == IntPtr.Zero || HostHandle == IntPtr.Zero)
//             {
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             if (NativeMethods.DwmRegisterThumbnail(HostHandle, SourceWindowHandle,
//                                                    out _thumbHandle) != 0)
//             {
//                 // Il DWM non ha potuto fornire la miniatura. Succede quando la
//                 // composizione e' disattivata a meta' (Windows in modalita'
//                 // provvisoria, sessioni RDP con accelerazione ridotta) o su
//                 // implementazioni che non espongono l'API. Invece di lasciare
//                 // un riquadro vuoto segnaliamo lo stato al contenitore, che
//                 // mostrera' icona e titolo.
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             IsThumbnailAvailable = true;
//
//             _layoutPending = true;
//             Refresh();
//
//             // Il thumbnail non segue da solo il layout: va riposizionato a ogni
//             // frame, altrimenti resta indietro quando il popup si muove.
//             _renderingHandler = (_, _) =>
//                 Dispatcher.BeginInvoke(DispatcherPriority.Render, new Action(Refresh));
//             CompositionTarget.Rendering += _renderingHandler;
//         }
//
//         private void OnUnloaded(object sender, RoutedEventArgs e)
//         {
//             if (_renderingHandler != null)
//             {
//                 CompositionTarget.Rendering -= _renderingHandler;
//                 _renderingHandler = null;
//             }
//
//             if (_thumbHandle != IntPtr.Zero)
//             {
//                 NativeMethods.DwmUnregisterThumbnail(_thumbHandle);
//                 _thumbHandle = IntPtr.Zero;
//             }
//         }
//     }
// }

// ----------------------------------------------------------------------------
// #2 - STATIC PrintWindow CAPTURE - shipped in v2.55 (extract: capture path)
//
// Full file in the v2.55 release. It is included here as the reference for the
// "loud failure" contract: PrintWindow either returns TRUE with real pixels or
// FALSE and we fall back to the icon. The remaining problem is that a TRUE
// return still does not prove the pixels are the window content (several apps
// answer with an empty or stale surface), which is exactly what a future
// implementation has to validate before the popup can be re-enabled.
// ----------------------------------------------------------------------------
//         /// <summary>
//         /// Cattura la finestra sorgente UNA VOLTA sola con PrintWindow e la
//         /// mostra come immagine statica. In caso di fallimento (finestra
//         /// minimizzata, app che non supporta PrintWindow, handle invalido)
//         /// passa esplicitamente al ripiego: nessuno stato intermedio.
//         /// </summary>
//         private void CaptureOnce()
//         {
//             if (SourceWindowHandle == IntPtr.Zero)
//             {
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             BitmapSource? captured = TryCapture(SourceWindowHandle, out double aspectRatio);
//             if (captured == null)
//             {
//                 IsThumbnailAvailable = false;
//                 return;
//             }
//
//             // Stessa logica di scalatura di prima: 1:1 se la finestra e'
//             // piccola, altrimenti si adatta a 180x120 mantenendo le
//             // proporzioni.
//             double wantWidth;
//             double wantHeight;
//             const double controlAspectRatio = MaxWidth_ / MaxHeight_;
//
//             if (captured.PixelWidth <= MaxWidth_ * DpiScale && captured.PixelHeight <= MaxHeight_ * DpiScale)
//             {
//                 wantWidth = captured.PixelWidth / DpiScale;
//                 wantHeight = captured.PixelHeight / DpiScale;
//             }
//             else if (aspectRatio > controlAspectRatio)
//             {
//                 wantWidth = MaxWidth_;
//                 wantHeight = MaxWidth_ / aspectRatio;
//             }
//             else
//             {
//                 wantWidth = MaxHeight_ * aspectRatio;
//                 wantHeight = MaxHeight_;
//             }
//
//             Width = wantWidth;
//             Height = wantHeight;
//
//             _image.Source = captured;
//             IsThumbnailAvailable = true;
//         }
//
//         /// <summary>
//         /// PrintWindow nell'area client della finestra sorgente. Prova prima
//         /// con PW_RENDERFULLCONTENT (necessario per le app con superficie
//         /// accelerata: Chrome, Edge, molte app moderne), poi senza, per le
//         /// build di Windows/driver dove quel flag stesso fa fallire la
//         /// chiamata. Ritorna null se nessuna delle due produce pixel validi.
//         /// </summary>
//         private static BitmapSource? TryCapture(IntPtr hwnd, out double aspectRatio)
//         {
//             aspectRatio = 1.0;
//
//             if (!NativeMethods.GetClientRect(hwnd, out NativeMethods.RECT rc))
//             {
//                 return null;
//             }
//
//             int width = rc.Right - rc.Left;
//             int height = rc.Bottom - rc.Top;
//             if (width <= 0 || height <= 0)
//             {
//                 return null;
//             }
//
//             aspectRatio = (double)width / height;
//
//             IntPtr hdcSrc = IntPtr.Zero;
//             IntPtr hdcMem = IntPtr.Zero;
//             IntPtr hBitmap = IntPtr.Zero;
//             IntPtr hOld = IntPtr.Zero;
//
//             try
//             {
//                 hdcSrc = NativeMethods.GetWindowClientDC(hwnd);
//                 if (hdcSrc == IntPtr.Zero)
//                 {
//                     return null;
//                 }
//
//                 hdcMem = NativeMethods.CreateCompatibleDC(hdcSrc);
//                 if (hdcMem == IntPtr.Zero)
//                 {
//                     return null;
//                 }
//
//                 hBitmap = NativeMethods.CreateCompatibleBitmap(hdcSrc, width, height);
//                 if (hBitmap == IntPtr.Zero)
//                 {
//                     return null;
//                 }
//
//                 hOld = NativeMethods.SelectObject(hdcMem, hBitmap);
//
//                 bool ok = NativeMethods.PrintWindow(hwnd, hdcMem,
//                     NativeMethods.PW_CLIENTONLY | NativeMethods.PW_RENDERFULLCONTENT);
//                 if (!ok)
//                 {
//                     // Ripiego: alcune build ignorano/rifiutano
//                     // PW_RENDERFULLCONTENT (0x02) come combinazione di flag.
//                     ok = NativeMethods.PrintWindow(hwnd, hdcMem, NativeMethods.PW_CLIENTONLY);
//                 }
//
//                 if (!ok)
//                 {
//                     return null;
//                 }
//
//                 BitmapSource bmp = Imaging.CreateBitmapSourceFromHBitmap(
//                     hBitmap, IntPtr.Zero, Int32Rect.Empty,
//                     BitmapSizeOptions.FromEmptyOptions());
//                 bmp.Freeze();
//                 return bmp;
//             }
//             catch (Exception)
//             {
//                 return null;
//             }
//             finally
//             {
//                 if (hOld != IntPtr.Zero)
//                 {
//                     NativeMethods.SelectObject(hdcMem, hOld);
//                 }
//                 if (hBitmap != IntPtr.Zero)
//                 {
//                     NativeMethods.DeleteObject(hBitmap);
//                 }
//                 if (hdcMem != IntPtr.Zero)
//                 {
//                     NativeMethods.DeleteDC(hdcMem);
//                 }
//                 if (hdcSrc != IntPtr.Zero)
//                 {
//                     NativeMethods.ReleaseWindowClientDC(hwnd, hdcSrc);
//                 }
//             }
//         }
