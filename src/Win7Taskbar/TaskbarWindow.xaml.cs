// Win7Taskbar - main taskbar window
// English: Main window handling taskbar logic, start button states, flyout anchoring, clock, tray, battery monitoring
// Italiano: Finestra principale della barra
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using RetroBar.Utilities;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Windows.Data;
using System.Windows.Documents;
using System.Globalization;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using Win7Taskbar.Converters;
using Win7Taskbar.Interop;
using Win7Taskbar.Models;
using Win7Taskbar.Utilities;

namespace Win7Taskbar
{
    public partial class TaskbarWindow : Window
    {
        private readonly NativeBridge _bridge;
        private readonly TaskbarViewModel _viewModel;

        private HwndSource? _hwndSource;
        private const string DwmPreviewAccentBrushKey = "DwmPreviewAccentBrush";
        private const string DwmPreviewBorderMaskImageKey = "DwmPreviewBorderMaskImage";
        private bool _dwmPreviewMaskReady;
        // Ultimo colore di colorizzazione DWM con cui e' stata tinta la
        // cornice delle anteprime: il raffronto col valore vivo copre i
        // cambi di "colore dietro" che non alzano alcun messaggio.
        private uint? _dwmAccentArgb;
        private bool _appBarRegistered;
        private bool _shuttingDown;

        /// <summary>
        /// v1.21.19: skin attualmente disegnata (id TaskbarThemeIds), cosi' il
        /// pacchetto delle Impostazioni extra riapplica il tema SOLO quando
        /// l'utente l'ha davvero cambiato. -1 = non ancora letto: il valore
        /// arriva dalla configurazione (la stessa che App.xaml.cs usa per
        /// scegliere il file del tema all'avvio).
        /// </summary>
        private int _appliedThemeSelection = -1;

        private int AppliedThemeSelection
        {
            get
            {
                if (_appliedThemeSelection < 0)
                {
                    try
                    {
                        _appliedThemeSelection = RetroBar.Utilities.TaskbarThemeIds.Normalize(
                            RetroBar.Utilities.Settings.Instance.ThemeSelection);
                    }
                    catch (Exception)
                    {
                        _appliedThemeSelection = RetroBar.Utilities.TaskbarThemeIds.Windows7;
                    }
                }
                return _appliedThemeSelection;
            }
            set
            {
                _appliedThemeSelection = value;
            }
        }

        // v3.4: messaggi registrati a livello di sessione.
        // _appBarCallbackMessage = quello passato ad ABM_NEW (notifiche ABN_*).
        // _taskbarCreatedMessage = "TaskbarCreated", trasmesso quando Explorer
        // (ri)parte: le registrazioni AppBar vivono nella shell, quindi a ogni
        // riavvio di Explorer la nostra prenotazione muore con la vecchia shell
        // e va rifatta.
        private int _appBarCallbackMessage;
        private readonly uint _taskbarCreatedMessage =
            NativeMethods.RegisterWindowMessage("TaskbarCreated");
        private bool _geometrySyncPending;

        // Ultimo rettangolo confermato dalla shell (PIXEL FISICI). Serve a
        // WM_WINDOWPOSCHANGED per capire se la barra e' stata spostata da
        // qualcun altro: la shell risolve le sovrapposizioni fra AppBar
        // SPOSTANDO le finestre, e in quel caso la barra deve tornare sul
        // rettangolo riservato.
        private Rect _appBarRect = Rect.Empty;

        // Windows 7 Superbar height at 96 DPI. Fallback: theme TaskbarHeight key
        private const int TaskbarHeightPx = 40;

        /// <summary>
        /// Height in DIP from theme (key TaskbarHeight), so bar height matches graphics.
        /// English: Theme height
        /// </summary>
        private double ThemeTaskbarHeightDip
        {
            get
            {
                if (TryFindResource("TaskbarHeight") is double value && value > 0)
                {
                    return value;
                }
                return TaskbarHeightPx;
            }
        }

        /* =====================================================================
         * ROTAZIONE TASKBAR (1.21.28) - DISATTIVATA.
         *
         * La barra resta sempre in BASSO: geometria, AppBar, orientamento del
         * tema e anchor delle anteprime DWM sono quelli di sempre. Il codice
         * della rotazione e' tenuto qui commentato, non cancellato.
         *
         * Perche' era rotta: il valore salvato ("posizione") e' codificato
         * 0=Basso, 1=Alto, 2=Sinistra, 3=Destra, mentre l'enum esistente e'
         * TaskbarEdge { Left=0, Top=1, Right=2, Bottom=3 }. Il cast diretto
         * (TaskbarEdge)position spostava ogni scelta di una posizione - con
         * "A destra" (3) si finiva su Bottom - e SetThumbnailEdge riceveva
         * l'edge sbagliato, quindi le anteprime DWM erano ancorate al lato
         * sbagliato. Prima di riattivare qualunque cosa serve una conversione
         * esplicita, mai un cast:
         *
         *     // posizione (persistita)   -> TaskbarEdge / AppBarEdgeValue
         *     // 0 Basso                  -> TaskbarEdge.Bottom (3)
         *     // 1 Alto                   -> TaskbarEdge.Top    (1)
         *     // 2 Sinistra               -> TaskbarEdge.Left   (0)
         *     // 3 Destra                 -> TaskbarEdge.Right  (2)
         *     private static TaskbarEdge EdgeFromPosition(int position) =>
         *         position switch
         *         {
         *             1 => TaskbarEdge.Top,
         *             2 => TaskbarEdge.Left,
         *             3 => TaskbarEdge.Right,
         *             _ => TaskbarEdge.Bottom,
         *         };
         *
         * Il resto dell'infrastruttura (AppBarService accetta i 4 edge,
         * ComputeTaskPreviewPlacement ragiona gia' per TaskbarEdge, i temi
         * hanno i DataTrigger su Orientation) resta dov'e' e non e' stato
         * toccato: la sola cosa che mancava era la traduzione dei valori.
         * ===================================================================== */

        internal TaskbarWindow(NativeBridge bridge)
        {
            _bridge = bridge;

            InitializeComponent();

            _viewModel = new TaskbarViewModel(bridge);
            _viewModel.BalloonReceived += OnBalloonReceived;
            // v2.27: la freccetta deve tornare chiusa quando il pannello
            // nativo si chiude da solo (es. click su "Personalizza...").
            _viewModel.OverflowHidden += OnOverflowHidden;
            DataContext = _viewModel;

            Loaded += OnLoaded;
            Closing += OnClosing;
        }

        // ===============================================================
        //  Properties required by DataTriggers in Windows7.xaml theme.
        //  English: Required for theme bindings
        // ===============================================================

        public static readonly DependencyProperty OrientationProperty =
            DependencyProperty.Register(
                nameof(Orientation), typeof(Orientation), typeof(TaskbarWindow),
                new PropertyMetadata(Orientation.Horizontal));

        public Orientation Orientation
        {
            get => (Orientation)GetValue(OrientationProperty);
            set => SetValue(OrientationProperty, value);
        }

        public static readonly DependencyProperty RowsProperty =
            DependencyProperty.Register(
                nameof(Rows), typeof(int), typeof(TaskbarWindow),
                new PropertyMetadata(1));

        public int Rows
        {
            get => (int)GetValue(RowsProperty);
            set => SetValue(RowsProperty, value);
        }

        public static readonly DependencyProperty AppBarEdgeProperty =
            DependencyProperty.Register(
                nameof(AppBarEdge), typeof(string), typeof(TaskbarWindow),
                new PropertyMetadata("Bottom"));

        public string AppBarEdge
        {
            get => (string)GetValue(AppBarEdgeProperty);
            set => SetValue(AppBarEdgeProperty, value);
        }

        public static readonly DependencyProperty IsScaledProperty =
            DependencyProperty.Register(
                nameof(IsScaled), typeof(bool), typeof(TaskbarWindow),
                new PropertyMetadata(false));

        public bool IsScaled
        {
            get => (bool)GetValue(IsScaledProperty);
            set => SetValue(IsScaledProperty, value);
        }

        public static readonly DependencyProperty ThumbnailScaleProperty =
            DependencyProperty.RegisterAttached(
                "ThumbnailScale", typeof(double), typeof(TaskbarWindow),
                new FrameworkPropertyMetadata(1.0,
                    FrameworkPropertyMetadataOptions.Inherits));

        public static void SetThumbnailScale(DependencyObject element, double value)
            => element.SetValue(ThumbnailScaleProperty, value);

        public static double GetThumbnailScale(DependencyObject element)
            => (double)element.GetValue(ThumbnailScaleProperty);

        public static readonly DependencyProperty ThumbnailEdgeProperty =
            DependencyProperty.RegisterAttached(
                "ThumbnailEdge", typeof(int), typeof(TaskbarWindow),
                new FrameworkPropertyMetadata((int)TaskbarEdge.Bottom,
                    FrameworkPropertyMetadataOptions.Inherits));

        public static void SetThumbnailEdge(DependencyObject element, int value)
            => element.SetValue(ThumbnailEdgeProperty, value);

        public static int GetThumbnailEdge(DependencyObject element)
            => (int)element.GetValue(ThumbnailEdgeProperty);

        public static readonly DependencyProperty AppBarEdgeIndexProperty =
            DependencyProperty.Register(
                nameof(AppBarEdgeIndex), typeof(int), typeof(TaskbarWindow),
                new PropertyMetadata((int)TaskbarEdge.Bottom));

        public int AppBarEdgeIndex
        {
            get => (int)GetValue(AppBarEdgeIndexProperty);
            set => SetValue(AppBarEdgeIndexProperty, value);
        }

        public static readonly DependencyProperty HasOverflowIconsProperty =
            DependencyProperty.Register(
                nameof(HasOverflowIcons), typeof(bool), typeof(TaskbarWindow),
                new PropertyMetadata(false));

        public bool HasOverflowIcons
        {
            get => (bool)GetValue(HasOverflowIconsProperty);
            set => SetValue(HasOverflowIconsProperty, value);
        }

        // ===============================================================
        //  Lifecycle
        // ===============================================================

        private bool _startupDone;

        private void RunStage(string stage, Action action)
        {
            StartupGuard.Enter(stage);
            try
            {
                action();
            }
            catch (Exception ex)
            {
                StartupGuard.Report(ex, $"OnLoaded -> {stage}");
                StartupGuard.Note($"fase \"{stage}\" fallita: {ex.GetType().Name}: {ex.Message}");
            }
        }

        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            if (_startupDone)
            {
                return;
            }
            _startupDone = true;

            var helper = new WindowInteropHelper(this);
            _hwndSource = HwndSource.FromHwnd(helper.Handle);
            _hwndSource?.AddHook(WndProc);

            // The documented DWM API supplies the current glass/accent color.
            // The same hook receives WM_DWMCOLORIZATIONCOLORCHANGED later, so
            // previews already open update without restarting the application.
            UpdateDwmPreviewAccentColor();

            // v2.7: pannello overflow nativo con vetro Aero vero
            // (SetWindowCompositionAttribute + blur-behind, come le mod
            // Win7-style). Se la finestra nativa non si crea (ambienti
            // RDP/sandbox molto ristretti) resta il Popup WPF: ramo
            // FALLBACK RARO, non piu' mantenuto attivamente - se serve
            // toccarlo, valutare prima se il vero problema e' nel path
            // nativo.
            RunStage("overflow-nativo", () =>
            {
                _useNativeOverflow = _bridge.OverflowInit(helper.Handle);
                _overflowShellFlyout = _useNativeOverflow && _bridge.OverflowUsesShellFlyout();
                if (_overflowShellFlyout)
                {
                    _bridge.Log("overflow: Windows 11, la freccetta apre il flyout di sistema");
                }
                if (_useNativeOverflow)
                {
                    System.Windows.Data.BindingOperations.ClearBinding(
                        OverflowPopup, System.Windows.Controls.Primitives.Popup.IsOpenProperty);
                    _bridge.Log("overflow: pannello nativo con vetro Aero attivo");
                }
                else
                {
                    _bridge.Log("overflow: nativo non disponibile, uso fallback WPF");
                }
            });

            RunStage("stili-finestra", () => ApplyTaskbarWindowStyles(helper.Handle));
            RunStage("dpi", UpdateDpiScaling);
            RunStage("posizione", PositionOnScreen);

            RunStage("importa-icone-explorer", ImportExplorerIconsWithRetry);

            if (StartupGuard.SafeMode)
            {
                StartupGuard.Note("modalita' provvisoria: saltati AppBar e nascondimento della barra nativa");
                RunStage("ripristina-barra-nativa",
                         () => _bridge.SetNativeTaskbarHidden(false));
            }
            else
            {
                RunStage("nascondi-barra-nativa", () => _bridge.SetNativeTaskbarHidden(true));
                RunStage("appbar", RegisterAppBar);
            }

            AppDomain.CurrentDomain.ProcessExit += OnProcessExitRestoreTaskbar;

            RunStage("server-tray", () => _bridge.StartTray());

            RunStage("superbar", () => _viewModel.Start());

            RunStage("lente-ricerca", () =>
            {
                // v3.3: icona e visibilita' del pulsante ricerca a sinistra
                // dello Start (solo durante la nostra taskbar).
                LoadSearchIconPixels();
                UpdateSearchButtonVisibility();
                RetroBar.Utilities.Settings.Instance.PropertyChanged += (_, e) =>
                {
                    if (e.PropertyName == nameof(RetroBar.Utilities.Settings.EnableAppSearch))
                    {
                        UpdateSearchButtonVisibility();
                    }
                };
            });

            RunStage("barra-lingua", () =>
            {
                // v3.5: indicatore della lingua di input (stile Win7/8.1/10).
                ApplyInputLanguageMode();
                RetroBar.Utilities.Settings.Instance.PropertyChanged += (_, e) =>
                {
                    if (e.PropertyName == nameof(RetroBar.Utilities.Settings.InputLanguageMode))
                    {
                        ApplyInputLanguageMode();
                    }
                };
            });

            RunStage("area-di-notifica", () =>
            {
                _viewModel.NotificationArea.PropertyChanged += (_, _) => UpdateOverflowState();
                HookIconRectReporting();
                _viewModel.NotificationArea.UnpinnedIcons.CollectionChanged += (_, _) => UpdateOverflowState();
                // v3.1: ogni cambio di pin nato lato C# (drag, overlay del
                // tema) viene comunicato al nativo, cosi' il pannello
                // overflow si aggiorna sempre (sync conservativa bidirez.).
                _viewModel.NotificationArea.AllIcons.CollectionChanged += NotificationAreaIcons_CollectionChanged;
                foreach (var ic in _viewModel.NotificationArea.AllIcons)
                {
                    ic.PropertyChanged += TrayIconModel_PinChanged;
                }
                UpdateOverflowState();

                OverflowPopup.Placement = PlacementMode.Custom;
                OverflowPopup.CustomPopupPlacementCallback =
                    (System.Windows.Size size, System.Windows.Size target, System.Windows.Point offset) =>
                        new[]
                        {
                            new CustomPopupPlacement(
                                new Point((target.Width - size.Width) / 2, -size.Height - 1),
                                PopupPrimaryAxis.Vertical)
                        };

                ReportShellRects();
                // Unico aggancio per i cambi di forma/posizione: riporta le
                // rects della shell AL nativo e schedula il ricalcolo delle
                // rects-per-icona (vedi HookIconRectReporting).
                SizeChanged += (_, _) => { ReportShellRects(); ScheduleIconRectReport(); };
                LocationChanged += (_, _) => { ReportShellRects(); ScheduleIconRectReport(); };
            });

            RunStage("preferenze-riquadri", () =>
            {
                /* v2.63 - LA CONFIGURAZIONE LETTA ADESSO DIVENTA LA DECISIONE.
                 *
                 * Questa e' la prima cosa che parla con il core: le quattro
                 * scelte salvate vengono pubblicate (W7T_SetFlyoutPreferences)
                 * e le tre chiavi ImmersiveShell allineate, cosi' i clic sulle
                 * icone della tray aprono il riquadro scelto dall'utente e non
                 * quello imposto dal default. Vedi ApplyShellFlyoutPreferences:
                 * prima questa applicazione avveniva solo premendo Applica. */
                ApplyShellFlyoutPreferences();
            });

            RunStage("riquadro-rete-win7", () =>
            {
                /* v2.62 - IL RIQUADRO DI RETE DI WINDOWS 7 SI PREPARA ALL'AVVIO.
                 *
                 * Prima veniva inizializzato al primo clic sull'icona di rete, e
                 * solo se quella icona arrivava dal tray vero di Explorer
                 * (IsNetworkTrayIcon). Su Windows 11 l'icona di rete la
                 * disegniamo noi: quel ramo non passava mai, il modulo restava
                 * spento e il clic finiva sul riquadro della shell - per
                 * l'utente "si apre quello sbagliato".
                 *
                 * Con l'inizializzazione all'avvio il riquadro ricreato e'
                 * pronto quando serve (nessuna attesa al primo clic) e il core
                 * sa che puo' usarlo. */
                ApplyNetworkFlyoutMode();
            });

            RunStage("riquadro-orologio", () =>
            {
                /* v2.61: il calendario di Windows 7 si costruisce ADESSO,
                 * all'avvio, non al primo clic. Al clic resta solo da
                 * mostrarlo: nessun lavoro di layout mentre l'utente aspetta
                 * (ed e' anche il momento in cui il riquadro nativo di
                 * Windows ci metteva meno a comparire). */
                EnsureCalendarFlyout();
            });

            RunStage("orologio", () =>
            {
                ApplyClockSecondsFormat();
                Settings.Instance.PropertyChanged += OnSettingsChangedForClock;
            });

            RunStage("start-monitor", () =>
            {
                // Start menu monitor for 3-state start button (idle/hover/pressed)
                _startMenuMonitor = new StartMenuMonitor();
                _startMenuMonitor.StartMenuVisibilityChanged += OnStartMenuVisibilityChanged;
            });

            RunStage("pulsanti-superbar", () =>
            {
                // v2.60: larghezza adattiva dei pulsanti quando i programmi
                // aperti sono tanti (vedi UpdateTaskButtonLayout).
                InitTaskButtonLayout();
            });

            RunStage("pulsante-start-idle", ArmStartOrb);

            // v2.3: bande Desktop/Collegamenti/Indirizzo (ispirate a ExplorerEx)
            RunStage("barre-strumenti", InitShellToolbars);

            RunStage("battery-monitor", () =>
            {
                // Battery monitor using public API GetSystemPowerStatus
                // English: Fixes battery icon not updating when unplugged - uses public APIs, forces tray refresh
                // Italiano: Risolve icona batteria che non si aggiorna - usa API pubbliche, forza refresh tray
                // NOTE: We do NOT override icons with synthetic here, to avoid breaking volume/network icons
                // The native side already bumps IconRevision for battery icons when power status changes
                _batteryMonitor = new BatteryMonitor((status) =>
                {
                    Dispatcher.BeginInvoke(new Action(() =>
                    {
                        // v2.4: il glifo surrogato e' stato rimosso: la fonte
                        // resta Explorer (pixel vivi, vedi lettore nativo che
                        // ora preferisce l'hIcon aggiornato via NIM_MODIFY).
                        _viewModel.RefreshTray();
                        UpdateOverflowState();
                        _bridge.ReanchorFlyouts();
                    }));
                });
                _batteryMonitor.Start();
            });

            StartupGuard.Complete();
        }

        // ===============================================================
        //  v2.60 - Larghezza dei pulsanti della Superbar con molti
        //  programmi aperti.
        //
        //  Il difetto: il pulsante non aveva nessun vincolo di larghezza
        //  (minimo del tema, poi "quanto chiede il contenuto"). Con molti
        //  programmi la somma delle larghezze superava lo spazio della
        //  barra e lo StackPanel continuava a disporli: gli ultimi
        //  finivano SOPRA l'orologio e la tray, con la cornice Aero tagliata
        //  dal bordo della finestra (i "bordi disegnati male").
        //
        //  Come nella Superbar vera: quando non c'e' spazio i pulsanti si
        //  stringono TUTTI della stessa misura (i "titoli" qui non esistono,
        //  il contenuto e' icona + schede delle finestre impilate), restando
        //  dentro i propri limiti; esaurito anche il minimo, la striscia si
        //  scorre (Shift + rotellina) invece di sovrapporsi alla tray.
        //  I due minimi sono quelli del mod di riferimento
        //  (windows-11-taskbar-styling-guide -> "Taskbar Labels for Windows
        //  11": minimumTaskbarItemWidth = 40) piu' lo spazio che qui serve
        //  davvero alle schede delle finestre impilate.
        // ===============================================================

        private const double TaskButtonMinWidthCompact = 44;   /* icona, senza schede */
        private const double TaskButtonMinWidthStacked = 52;   /* icona + schede      */
        private const double TaskButtonSideMargin    = 2;      /* TaskButtonMargin 1+1 */

        private bool   _taskButtonLayoutHooked;
        private bool   _taskButtonLayoutPending;
        private double _taskButtonThemeMinWidth = 52;
        private double _taskButtonAppliedWidth  = double.NaN;
        private bool   _taskButtonCompactLogged;

        private void InitTaskButtonLayout()
        {
            if (_taskButtonLayoutHooked)
            {
                return;
            }
            _taskButtonLayoutHooked = true;

            _taskButtonThemeMinWidth = TryFindResource("TaskButtonMinWidthOverride") is double d && d > 0
                                     ? d : 52;

            _viewModel.Groups.CollectionChanged += OnTaskGroupsChanged;
            foreach (TaskGroup g in _viewModel.Groups)
            {
                g.PropertyChanged += OnTaskGroupPropertyChanged;
            }

            SizeChanged += (_, _) => ScheduleTaskButtonLayout();
            if (TaskListScroller != null)
            {
                TaskListScroller.SizeChanged += (_, _) => ScheduleTaskButtonLayout();
                TaskListScroller.ScrollChanged += (_, _) => SyncTaskListScrollButtons();
            }

            ScheduleTaskButtonLayout();
        }

        private void OnTaskGroupsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (e.OldItems != null)
            {
                foreach (TaskGroup g in e.OldItems.OfType<TaskGroup>())
                {
                    g.PropertyChanged -= OnTaskGroupPropertyChanged;
                    // v3.9: se il gruppo mostrato nel popup viene rimosso
                    // (app chiusa con anteprima aperta), chiudi subito il
                    // popup invece di lasciarlo vuoto o ancorato al nulla.
                    if (ReferenceEquals(g, _previewGroup))
                    {
                        CloseTaskPreview();
                    }
                }
            }
            if (e.NewItems != null)
            {
                foreach (TaskGroup g in e.NewItems.OfType<TaskGroup>())
                {
                    g.PropertyChanged += OnTaskGroupPropertyChanged;
                }
            }
            ScheduleTaskButtonLayout();
        }

        private void OnTaskGroupPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            // Solo cio' che cambia la larghezza richiesta: il numero di
            // finestre (schede) e lo stato (l'imbottitura della cornice
            // cambia fra riposo ed evidenziato).
            if (e.PropertyName is nameof(TaskGroup.WindowCount)
                or nameof(TaskGroup.IsRunning)
                or nameof(TaskGroup.IsActive))
            {
                ScheduleTaskButtonLayout();
            }
        }

        /* Il calcolo tocca la larghezza dei pulsanti, cioe' fa ripartire il
         * layout: lo si rimanda alla coda per non rientrare nel layout in
         * corso (SizeChanged viene consegnato dentro la passata). */
        private void ScheduleTaskButtonLayout()
        {
            if (_taskButtonLayoutPending)
            {
                return;
            }
            _taskButtonLayoutPending = true;
            Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
            {
                _taskButtonLayoutPending = false;
                UpdateTaskButtonLayout();
            }));
        }

        private double DevicePixelScale()
        {
            PresentationSource? source = PresentationSource.FromVisual(this);
            double scale = source?.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
            return scale > 0 ? scale : 1.0;
        }

        private void UpdateTaskButtonLayout()
        {
            if (TaskListScroller == null || TaskList == null || _viewModel == null)
            {
                return;
            }

            IList<TaskGroup> groups = _viewModel.Groups;
            int count = groups.Count;
            if (count == 0)
            {
                _taskButtonAppliedWidth = double.NaN;
                return;
            }

            // Spazio che la barra lascia ai pulsanti: il ScrollViewer riempie
            // sempre la sua colonna, anche quando il contenuto e' piu' largo.
            double available = TaskListScroller.ActualWidth - TaskButtonSideMargin;
            if (available <= 0)
            {
                return;
            }

            // 1) Nessun vincolo e si misura quanto chiederebbero davvero.
            foreach (TaskGroup g in groups)
            {
                g.ButtonMinWidth = _taskButtonThemeMinWidth;
                g.ButtonWidth = double.NaN;
            }
            TaskListScroller.UpdateLayout();

            double natural = 0;
            int measured = 0;
            for (int i = 0; i < count; i++)
            {
                if (TaskList.ItemContainerGenerator.ContainerFromIndex(i) is FrameworkElement container)
                {
                    natural += container.DesiredSize.Width;
                    measured++;
                }
            }

            if (measured == count && natural <= available)
            {
                // C'e' posto: nessun vincolo, esattamente come prima della
                // v2.60 (larghezza decisa dal contenuto fra minimo e
                // imbottitura della cornice).
                if (!double.IsNaN(_taskButtonAppliedWidth))
                {
                    _taskButtonAppliedWidth = double.NaN;
                    _taskButtonCompactLogged = false;
                    _bridge.Log("superbar: pulsanti a larghezza naturale");
                }
                return;
            }

            // 2) Non ci stanno: larghezza uniforme che li fa entrare tutti,
            //    mai sotto il minimo per tipo di contenuto.
            double scale = DevicePixelScale();
            double SnapDown(double value) => Math.Floor(value * scale) / scale;

            int stacked = 0;
            foreach (TaskGroup g in groups)
            {
                if (g.WindowCount > 1)
                {
                    stacked++;
                }
            }
            int single = count - stacked;

            double uniform = SnapDown(available / count) - TaskButtonSideMargin;
            double applied;
            if (uniform > TaskButtonMinWidthStacked || stacked == 0)
            {
                uniform = Math.Max(TaskButtonMinWidthCompact, uniform);
                foreach (TaskGroup g in groups)
                {
                    g.ButtonWidth = uniform;
                    g.ButtonMinWidth = uniform;
                }
                applied = uniform;
            }
            else
            {
                /* I gruppi con finestre impilate hanno bisogno di piu'
                 * spazio (icona + tre schede): si stringono solo quelli con
                 * una finestra sola, finche' basta. */
                double stackedWidth = TaskButtonMinWidthStacked;
                double leftover = available - stacked * (stackedWidth + TaskButtonSideMargin);
                double singleWidth = single > 0
                                   ? Math.Max(TaskButtonMinWidthCompact,
                                              SnapDown(leftover / single) - TaskButtonSideMargin)
                                   : 0;
                foreach (TaskGroup g in groups)
                {
                    double w = g.WindowCount > 1 ? stackedWidth : singleWidth;
                    g.ButtonWidth = w;
                    g.ButtonMinWidth = w;
                }
                applied = singleWidth;
            }

            if (!_taskButtonCompactLogged ||
                Math.Abs(_taskButtonAppliedWidth - applied) > 0.5)
            {
                _taskButtonCompactLogged = true;
                _bridge.Log($"superbar: pulsanti stretti a {applied:0.#} px ({count} gruppi)");
            }
            _taskButtonAppliedWidth = applied;

            TaskListScroller.UpdateLayout();
            SyncTaskListScrollButtons();
        }

        /* Le frecce compaiono solo quando c'e' davvero qualcosa da scorrere
         * (stessa regola della barra vera) e si spengono ai due estremi. */
        private void SyncTaskListScrollButtons()
        {
            if (TaskListScroller == null)
            {
                return;
            }

            double scrollable = TaskListScroller.ExtentWidth - TaskListScroller.ViewportWidth;
            bool overflow = scrollable > 0.5;

            if (TaskListScrollLeft != null)
            {
                TaskListScrollLeft.Visibility = overflow ? Visibility.Visible : Visibility.Collapsed;
                TaskListScrollLeft.IsEnabled = overflow && TaskListScroller.HorizontalOffset > 0.5;
            }
            if (TaskListScrollRight != null)
            {
                TaskListScrollRight.Visibility = overflow ? Visibility.Visible : Visibility.Collapsed;
                TaskListScrollRight.IsEnabled = overflow &&
                    TaskListScroller.HorizontalOffset < scrollable - 0.5;
            }
        }

        private void ScrollTaskList(int direction)
        {
            if (TaskListScroller == null)
            {
                return;
            }

            double extent = TaskListScroller.ExtentWidth - TaskListScroller.ViewportWidth;
            if (extent <= 0)
            {
                return;
            }

            /* Un quarto di barra per scatto: e' il passo che usa anche la
             * barra vera quando si tiene premuta la freccia. */
            double step = Math.Max(40, TaskListScroller.ViewportWidth / 4);
            double target = TaskListScroller.HorizontalOffset + direction * step;
            TaskListScroller.ScrollToHorizontalOffset(Math.Max(0, Math.Min(extent, target)));
            SyncTaskListScrollButtons();
        }

        private void TaskListScrollLeft_Click(object sender, RoutedEventArgs e)
            => ScrollTaskList(-1);

        private void TaskListScrollRight_Click(object sender, RoutedEventArgs e)
            => ScrollTaskList(1);

        /* Shift + rotellina: scorre la striscia quando nemmeno la larghezza
         * minima basta (la rotellina da sola resta il comando di Windows 7
         * per passare fra le finestre del gruppo). */
        private void TaskListScroller_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
        {
            if (TaskListScroller == null ||
                (Keyboard.Modifiers & ModifierKeys.Shift) == 0)
            {
                return;
            }

            double extent = TaskListScroller.ExtentWidth - TaskListScroller.ViewportWidth;
            if (extent <= 0)
            {
                return;
            }

            double step = Math.Max(40, TaskListScroller.ViewportWidth / 4);
            double target = TaskListScroller.HorizontalOffset - Math.Sign(e.Delta) * step;
            TaskListScroller.ScrollToHorizontalOffset(
                Math.Max(0, Math.Min(extent, target)));
            SyncTaskListScrollButtons();
            e.Handled = true;
        }

        private void OnProcessExitRestoreTaskbar(object? sender, EventArgs e)
        {
            try
            {
                _bridge.SetNativeTaskbarHidden(false);
            }
            catch { }
        }

        private void OnSettingsChangedForClock(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(Settings.ShowClockSeconds))
            {
                Dispatcher.BeginInvoke(new Action(ApplyClockSecondsFormat));
            }
            else if (e.PropertyName == nameof(Settings.NetworkFlyoutMode))
            {
                /* v2.62: la scelta del riquadro di rete si puo' cambiare
                 * mentre la barra e' aperta (finestra Proprieta'): il core
                 * deve sapere subito quale usare per le icone ricreate. */
                Dispatcher.BeginInvoke(new Action(ApplyNetworkFlyoutMode));
            }
        }

        /// <summary>
        /// v2.62 - Il riquadro di rete di Windows 7 si prepara ADESSO.
        ///
        /// Prima veniva inizializzato al primo clic sull'icona di rete, e solo
        /// se quella icona arrivava dal tray vero di Explorer
        /// (IsNetworkTrayIcon): su Windows 11 l'icona di rete la disegniamo
        /// noi, quel ramo non passava mai, il modulo restava spento e il clic
        /// finiva sul riquadro della shell - per l'utente "si apre quello
        /// sbagliato". Con la preparazione all'avvio il riquadro ricreato e'
        /// pronto quando serve (nessuna attesa al primo clic) e il core sa
        /// che puo' usarlo.
        /// </summary>
        private void ApplyNetworkFlyoutMode()
        {
            try
            {
                int netMode = RetroBar.Utilities.Settings.Instance.NetworkFlyoutMode;
                if (netMode == 0)
                {
                    if (!_netFlyoutInit)
                    {
                        _netFlyoutInit = _bridge.NetFlyoutInit();
                    }
                    _bridge.SetWin7NetworkFlyout(_netFlyoutInit);
                    _bridge.SetWin8NetworkFlyout(false);
                    _bridge.Log(_netFlyoutInit
                        ? "rete: riquadro Windows 7 pronto"
                        : "rete: riquadro Windows 7 non disponibile");
                }
                else if (netMode == 2)
                {
                    /* v3.8: variante Windows 8 (riquadro ricreato). Il suo
                     * Init porta su anche la logica di rete condivisa del
                     * modulo Windows 7 (una volta sola per processo). */
                    if (!_net8FlyoutInit)
                    {
                        _net8FlyoutInit = _bridge.Net8FlyoutInit();
                    }
                    _bridge.SetWin7NetworkFlyout(false);
                    _bridge.SetWin8NetworkFlyout(_net8FlyoutInit);
                    _bridge.Log(_net8FlyoutInit
                        ? "rete: riquadro Windows 8 (ricreato) pronto"
                        : "rete: riquadro Windows 8 non disponibile");
                }
                else
                {
                    /* Scelta dell'utente: modalita' senza riquadro Windows 8.
                     * Se il cambio e' arrivato a riquadro aperto, lo si
                     * chiude: ogni modalita' spettina un riquadro SOLO. */
                    if (_net8FlyoutInit)
                    {
                        try { _bridge.Net8FlyoutHide(); } catch { }
                    }
                    _bridge.SetWin7NetworkFlyout(false);
                    _bridge.SetWin8NetworkFlyout(false);
                }
            }
            catch (Exception ex)
            {
                _bridge.Log($"rete: preparazione del riquadro fallita: {ex.Message}");
            }
        }

        private void ApplyClockSecondsFormat()
        {
            if (ClockHost?.Template?.FindName("ClockText", ClockHost) is not TextBlock clockText)
            {
                return;
            }

            CultureInfo culture = CultureInfo.CurrentCulture;
            string format = Settings.Instance.ShowClockSeconds
                ? culture.DateTimeFormat.LongTimePattern
                : culture.DateTimeFormat.ShortTimePattern;

            Rebind(clockText, format);

            if (ClockHost.Template.FindName("ClockVerticalDate", ClockHost) is TextBlock dateText)
            {
                Rebind(dateText, culture.DateTimeFormat.ShortDatePattern);
            }

            if (ClockHost.Template.FindName("ClockVerticalDayOfWeek", ClockHost) is TextBlock dayText)
            {
                Rebind(dayText, "dddd");
            }

            static void Rebind(TextBlock target, string pattern)
            {
                target.SetBinding(TextBlock.TextProperty, new Binding(nameof(ClockModel.Now))
                {
                    Mode = BindingMode.OneWay,
                    StringFormat = "{0:" + pattern + "}",
                    ConverterCulture = CultureInfo.CurrentCulture
                });
            }
        }

        private void NotificationAreaIcons_CollectionChanged(object? sender,
            System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
        {
            try
            {
                if (e.OldItems != null)
                {
                    foreach (var item in e.OldItems)
                    {
                        if (item is TrayIconModel m) m.PropertyChanged -= TrayIconModel_PinChanged;
                    }
                }
                if (e.NewItems != null)
                {
                    foreach (var item in e.NewItems)
                    {
                        if (item is TrayIconModel m) m.PropertyChanged += TrayIconModel_PinChanged;
                    }
                }
            }
            catch (Exception ex)
            {
                _bridge.Log($"sync pin: hook icone: {ex.Message}");
            }
        }

        private void TrayIconModel_PinChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName != nameof(TrayIconModel.IsPinned) ||
                sender is not TrayIconModel icon)
            {
                return;
            }

            /* v2.62: lo stato appena scritto dal core non torna indietro.
             * Rimandarlo significava salvarlo come preferenza dell'utente,
             * congelando per sempre la disposizione letta da Explorer e
             * svuotando l'overflow. */
            if (_viewModel.ApplyingTrayState)
            {
                return;
            }

            try
            {
                _bridge.SetTrayIconPinned(icon.OwnerHwnd, icon.Uid, icon.IsPinned);
            }
            catch (Exception ex)
            {
                _bridge.Log($"sync pin: {ex.Message}");
            }
        }

        private void UpdateOverflowState()
        {
            /* v2.62 - LA FRECCETTA C'E' SE LA TRAY RAGGRUPPA, NON "SE ADESSO
             * C'E' QUALCOSA DENTRO".
             *
             * Prima la freccetta spariva quando il modello non aveva icone
             * nell'overflow in quell'istante: bastava una lettura della tray
             * in ritardo (o tutte le icone di sistema della shell filtrate
             * via) e la freccetta si nascondeva. L'utente la cliccava e non
             * si apriva niente, perche' non c'era piu' niente da cliccare.
             * In Windows 7 la freccetta fa parte della tray: c'e' quando il
             * raggruppamento e' attivo, e il pannello puo' anche essere
             * vuoto (contiene comunque "Personalizza...").
             *
             * Il raggruppamento e' spento quando l'utente sceglie "mostra
             * tutte le icone": in quel caso la freccetta non serve. */
            bool collapse = true;
            try { collapse = RetroBar.Utilities.Settings.Instance.CollapseNotifyIcons; }
            catch (Exception) { /* impostazioni non disponibili: si mostra */ }

            HasOverflowIcons = collapse || _viewModel.NotificationArea.UnpinnedIcons.Count > 0;

            // La freccetta che appare o scompare cambia la larghezza della
            // riga: le icone si spostano e i loro rettangoli vanno riferiti
            // di nuovo al nativo (per GetRect e per i flyout).
            ScheduleIconRectReport();
        }

        private void ReportShellRects()
        {
            try
            {
                if (_hwndSource?.CompositionTarget == null)
                {
                    return;
                }

                double scale = _hwndSource.CompositionTarget.TransformToDevice.M11;
                if (scale <= 0)
                {
                    scale = 1.0;
                }

                Point bar = this.PointToScreen(new Point(0, 0));
                Point tray = TrayArea.PointToScreen(new Point(0, 0));

                _bridge.SetShellRects(
                    (int)bar.X, (int)bar.Y,
                    (int)(bar.X + ActualWidth * scale),
                    (int)(bar.Y + ActualHeight * scale),
                    (int)tray.X, (int)tray.Y,
                    (int)(tray.X + TrayArea.ActualWidth * scale),
                    (int)(tray.Y + TrayArea.ActualHeight * scale));

                Point chevron = OverflowToggle.PointToScreen(new Point(0, 0));
                _bridge.SetChevronRect(
                    (int)chevron.X, (int)chevron.Y,
                    (int)(chevron.X + OverflowToggle.ActualWidth * scale),
                    (int)(chevron.Y + OverflowToggle.ActualHeight * scale));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Sincronizzazione rettangoli shell non riuscita: {ex.Message}");
            }
        }

        // ---------------------------------------------------------------
        //  Drag file to task buttons
        // ---------------------------------------------------------------

        // ---------------------------------------------------------------
        // v1.21.15: spostamento dei bottoni con trascinamento, meccanismo
        // copiato da RetroBar (ManagedShell TaskList): si preme un
        // bottone (es. Chrome) e lo si trascina orizzontalmente; una
        // barretta verticale segna il punto di inserimento e al rilascio
        // il gruppo si sposta li. L ordine scelto resta tutta la sessione
        // (come in RetroBar: niente salvataggio su disco).
        // ---------------------------------------------------------------
        private const string TaskGroupReorderFormat = "W7T.TaskGroupReorder";

        private FrameworkElement? _reorderCandidate;
        private Point _reorderPressPoint;
        private TaskGroup? _reorderDragging;
        private TaskGroupInsertionAdorner? _insertionAdorner;

        /// <summary>Riga verticale (ombreggiata + nucleo chiaro) che segna
        /// il punto di inserimento, come l'indicatore di RetroBar.</summary>
        private sealed class TaskGroupInsertionAdorner : Adorner
        {
            private readonly double _x;

            public TaskGroupInsertionAdorner(UIElement adorned, double x)
                : base(adorned)
            {
                _x = x;
                IsHitTestVisible = false;
            }

            protected override void OnRender(DrawingContext dc)
            {
                double h = AdornedElement.RenderSize.Height;
                var glow = new Pen(
                    new SolidColorBrush(Color.FromArgb(110, 0, 0, 0)), 6.0);
                if (glow.CanFreeze) glow.Freeze();
                var core = new Pen(Brushes.White, 2.0);
                if (core.CanFreeze) core.Freeze();
                dc.DrawLine(glow, new Point(_x, 2), new Point(_x, h - 2));
                dc.DrawLine(core, new Point(_x, 2), new Point(_x, h - 2));
            }
        }

        private void TaskButton_PreviewMouseMove(object sender, MouseEventArgs e)
        {
            if (_reorderCandidate == null || _reorderDragging != null)
            {
                return;
            }
            if (e.LeftButton != MouseButtonState.Pressed)
            {
                _reorderCandidate = null;
                return;
            }

            Point pos = e.GetPosition(this);
            Vector delta = pos - _reorderPressPoint;
            if (Math.Abs(delta.X) < SystemParameters.MinimumHorizontalDragDistance &&
                Math.Abs(delta.Y) < SystemParameters.MinimumVerticalDragDistance)
            {
                return;
            }

            FrameworkElement sourceButton = _reorderCandidate;
            _reorderCandidate = null;
            if (sourceButton.DataContext is not TaskGroup group)
            {
                return;
            }

            _reorderDragging = group;
            try
            {
                DragDrop.DoDragDrop(sourceButton,
                    new DataObject(TaskGroupReorderFormat, group),
                    DragDropEffects.Move);
            }
            catch (Exception)
            {
                // Drag annullato dal sistema operativo (bottone perso,
                // transito su DDE...): nessun ordine cambia, si ripulisce.
            }
            finally
            {
                _reorderDragging = null;
                HideInsertionMark();
            }
        }

        /// <summary>Indice di inserimento davanti al clic: la x del
        /// puntatore rispetto alla meta destra/sinistra di ogni bottone,
        /// lo stesso conto che fa RetroBar.</summary>
        private int TaskGroupInsertionIndexAt(Point posInList)
        {
            int count = TaskList.Items.Count;
            for (int i = 0; i < count; i++)
            {
                if (TaskList.ItemContainerGenerator.ContainerFromIndex(i)
                        is FrameworkElement container)
                {
                    Point p = container.TransformToAncestor(TaskList)
                                       .Transform(new Point(0, 0));
                    if (posInList.X < p.X + container.ActualWidth / 2)
                    {
                        return i;
                    }
                }
            }
            return count;
        }

        private double TaskGroupInsertionXAt(int index)
        {
            int count = TaskList.Items.Count;
            if (count == 0)
            {
                return 2;
            }
            if (index >= count)
            {
                if (TaskList.ItemContainerGenerator.ContainerFromIndex(count - 1)
                        is FrameworkElement last)
                {
                    return last.TransformToAncestor(TaskList)
                               .Transform(new Point(0, 0)).X + last.ActualWidth;
                }
                return TaskList.ActualWidth;
            }
            if (TaskList.ItemContainerGenerator.ContainerFromIndex(index)
                    is FrameworkElement target)
            {
                return target.TransformToAncestor(TaskList)
                             .Transform(new Point(0, 0)).X;
            }
            return 2;
        }

        private void ShowInsertionMark(int index)
        {
            try
            {
                AdornerLayer? layer = AdornerLayer.GetAdornerLayer(TaskList);
                if (layer == null)
                {
                    return;
                }
                HideInsertionMark();
                _insertionAdorner =
                    new TaskGroupInsertionAdorner(TaskList, TaskGroupInsertionXAt(index));
                layer.Add(_insertionAdorner);
            }
            catch (Exception)
            {
                // Manca solo il feedback visivo: il drop funziona uguale.
            }
        }

        private void HideInsertionMark()
        {
            if (_insertionAdorner == null)
            {
                return;
            }
            try
            {
                AdornerLayer.GetAdornerLayer(TaskList)?.Remove(_insertionAdorner);
            }
            catch (Exception)
            {
                // layer gia' smontato
            }
            _insertionAdorner = null;
        }

        /// <summary>Ramo comune di Hover/Drop del riordino: ritorna true
        /// quando l evento riguardava lo spostamento dei bottoni (in quel
        /// caso i percorsi preesistenti del drop dei file non si toccano).
        /// </summary>
        private bool HandleTaskGroupReorderHover(object sender, DragEventArgs e)
        {
            if (_reorderDragging == null ||
                !e.Data.GetDataPresent(TaskGroupReorderFormat))
            {
                return false;
            }
            e.Effects = DragDropEffects.Move;
            try
            {
                Point pos = e.GetPosition(TaskList);
                ShowInsertionMark(TaskGroupInsertionIndexAt(pos));
            }
            catch (Exception)
            {
                // senza indicatore il drop funziona lo stesso
            }
            e.Handled = true;
            return true;
        }

        private bool HandleTaskGroupReorderDrop(object sender, DragEventArgs e)
        {
            if (_reorderDragging == null ||
                !e.Data.GetDataPresent(TaskGroupReorderFormat))
            {
                return false;
            }
            try
            {
                if (e.Data.GetData(TaskGroupReorderFormat) is TaskGroup group)
                {
                    Point pos = e.GetPosition(TaskList);
                    _viewModel.MoveTaskGroup(
                        group, TaskGroupInsertionIndexAt(pos));

                    /* v1.21.21: il gesto e' quello di RetroBar, ma l'ordine
                     * non resta solo in sessione: la lista risultante viene
                     * salvata nella configurazione del programma con lo stesso
                     * metodo usato dall'altro sistema (ApplyUserTaskbarOrder,
                     * che scrive in settings.json e non tocca nulla di
                     * Windows). Se il salvataggio non riesce, l'ordine resta
                     * comunque applicato sullo schermo. */
                    bool saved = _viewModel.ApplyUserTaskbarOrder(
                        _viewModel.Groups.ToList());
                    _bridge.Log(saved
                        ? "ordine icone: spostamento salvato (gesto RetroBar)"
                        : "ordine icone: spostamento applicato ma non salvato");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Riordino del bottone non riuscito: {ex.Message}");
            }
            HideInsertionMark();
            e.Handled = true;
            return true;
        }

        private void TaskList_DragOver(object sender, DragEventArgs e)
        {
            if (!HandleTaskGroupReorderHover(sender, e))
            {
                e.Effects = DragDropEffects.None;
                e.Handled = true;
            }
        }

        private void TaskList_Drop(object sender, DragEventArgs e)
        {
            if (!HandleTaskGroupReorderDrop(sender, e))
            {
                e.Handled = true;
            }
        }

        private void TaskList_DragLeave(object sender, DragEventArgs e)
        {
            HideInsertionMark();
        }

        private void TaskButton_DragOver(object sender, DragEventArgs e)
        {
            // v1.21.15: spostamento dei bottoni (riordino stile RetroBar),
            // indipendente dal drop dei file dei rami sotto.
            if (HandleTaskGroupReorderHover(sender, e))
            {
                return;
            }

            // v1.7.3: the cursor shows "forbidden" when the target
            // executable declares (via the registry) that it cannot open
            // the dragged file type - exactly like the real taskbar.
            try
            {
                if (!e.Data.GetDataPresent(DataFormats.FileDrop))
                {
                    e.Effects = DragDropEffects.None;
                    e.Handled = true;
                    return;
                }

                string? exePath = null;
                if (sender is FrameworkElement element &&
                    element.DataContext is Models.TaskGroup group)
                {
                    exePath = !string.IsNullOrEmpty(group.ExePath)
                        ? group.ExePath : group.LaunchPath;
                }

                var files = e.Data.GetData(DataFormats.FileDrop) as string[];
                e.Effects = Controls.TaskButtonDropTarget.AllLikelyOpen(
                    exePath, files)
                    ? DragDropEffects.Link
                    : DragDropEffects.None;
            }
            catch (Exception)
            {
                // Any error in the capability check stays permissive.
                e.Effects = DragDropEffects.Link;
            }
            e.Handled = true;
        }

        private void TaskButton_Drop(object sender, DragEventArgs e)
        {
            // v1.21.15: rilascio che sposta il bottone (riordino stile
            // RetroBar). Il ramo file piu' sotto continua a trattare solo
            // DataFormats.FileDrop come prima.
            if (HandleTaskGroupReorderDrop(sender, e))
            {
                return;
            }

            e.Handled = true;

            try
            {
                if (sender is not FrameworkElement element ||
                    element.DataContext is not Models.TaskGroup group)
                {
                    return;
                }

                if (e.Data.GetData(DataFormats.FileDrop) is not string[] files ||
                    files.Length == 0 ||
                    string.IsNullOrEmpty(group.ExePath))
                {
                    return;
                }

                // v1.7.3: launch only the files the executable most likely
                // opens; unsupported ones are skipped instead of aborting
                // the whole drop (permissive, never blocking).
                var launchable = files
                    .Where(f => Controls.TaskButtonDropTarget.CanLikelyOpen(
                        group.ExePath, f))
                    .ToList();
                if (launchable.Count == 0)
                {
                    return;
                }
                files = launchable.ToArray();

                string args = string.Join(" ",
                    files.Select(f => "\"" + f + "\""));

                Process.Start(new ProcessStartInfo
                {
                    FileName = group.ExePath,
                    Arguments = args,
                    UseShellExecute = true,
                    WorkingDirectory =
                        System.IO.Path.GetDirectoryName(files[0]) ?? string.Empty
                });
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Apertura del file sul pulsante non riuscita: {ex.Message}");
            }
        }

        private static void ApplyTaskbarWindowStyles(IntPtr handle)
        {
            if (handle == IntPtr.Zero)
            {
                return;
            }

            long exStyle = NativeMethods.GetWindowLongPtr(
                handle, NativeMethods.GWL_EXSTYLE).ToInt64();

            exStyle |= NativeMethods.WS_EX_NOACTIVATE | NativeMethods.WS_EX_TOOLWINDOW;

            NativeMethods.SetWindowLongPtr(
                handle, NativeMethods.GWL_EXSTYLE, new IntPtr(exStyle));
        }

        // ---------------------------------------------------------------
        // v2.5: banda Indirizzo scrivibile. La barra ha WS_EX_NOACTIVATE
        // (giusto: i click non devono rubare il focus alle altre finestre),
        // ma quando l'utente clicca proprio nella casella Indirizzo deve
        // poter digitare, come nella barra Indirizzo di ExplorerEx. Tecnica:
        // su WM_MOUSEACTIVATE, se il click è dentro la casella, si toglie
        // WS_EX_NOACTIVATE e si risponde MA_ACTIVATE; lo stile viene
        // rimesso appena la casella perde il focus della tastiera.
        // ---------------------------------------------------------------

        private bool _noActivateLifted;

        /// <summary>lParam di WM_MOUSEACTIVATE: coordinate SCHERMO del click.</summary>
        private bool IsMouseInsideAddressBand(IntPtr lParam)
        {
            try
            {
                if (AddressBandHost.Visibility != Visibility.Visible || !AddressBandHost.IsLoaded)
                {
                    return false;
                }

                int x = (short)(lParam.ToInt64() & 0xFFFF);
                int y = (short)((lParam.ToInt64() >> 16) & 0xFFFF);

                Point topLeft = AddressBandHost.PointToScreen(new Point(0, 0));
                Point bottomRight = AddressBandHost.PointToScreen(
                    new Point(AddressBandHost.ActualWidth, AddressBandHost.ActualHeight));

                return x >= topLeft.X && x < bottomRight.X &&
                       y >= topLeft.Y && y < bottomRight.Y;
            }
            catch
            {
                return false;
            }
        }

        private void LiftNoActivateForTyping()
        {
            IntPtr handle = _hwndSource?.Handle ?? IntPtr.Zero;
            if (handle == IntPtr.Zero)
            {
                return;
            }

            long exStyle = NativeMethods.GetWindowLongPtr(
                handle, NativeMethods.GWL_EXSTYLE).ToInt64();

            if ((exStyle & NativeMethods.WS_EX_NOACTIVATE) != 0)
            {
                NativeMethods.SetWindowLongPtr(
                    handle, NativeMethods.GWL_EXSTYLE,
                    new IntPtr(exStyle & ~NativeMethods.WS_EX_NOACTIVATE));
                _noActivateLifted = true;
            }
        }

        /// <summary>Quando la casella Indirizzo perde la tastiera, la barra
        /// torna non attivabile: i click comuni non devono rubare il focus.</summary>
        private void AddressBand_LostKeyboardFocus(object? sender, KeyboardFocusChangedEventArgs e)
        {
            if (!_noActivateLifted)
            {
                return;
            }

            _noActivateLifted = false;

            IntPtr handle = _hwndSource?.Handle ?? IntPtr.Zero;
            if (handle == IntPtr.Zero)
            {
                return;
            }

            long exStyle = NativeMethods.GetWindowLongPtr(
                handle, NativeMethods.GWL_EXSTYLE).ToInt64();

            NativeMethods.SetWindowLongPtr(
                handle, NativeMethods.GWL_EXSTYLE,
                new IntPtr(exStyle | NativeMethods.WS_EX_NOACTIVATE));
        }

        private void OnClosing(object? sender, System.ComponentModel.CancelEventArgs e)
        {
            /* v2.46: smontaggio in ordine inverso a come si e' costruito (RAII:
             * quello che la finestra ha creato lo libera la finestra). Prima
             * qui restavano vivi il watchdog del menu Start, i due timer
             * dell'anteprima e il popup aperto: ognuno con dentro un pezzo
             * della barra gia' chiusa. */
            try
            {
                CancelStartWatchdog();
                CloseTaskPreview();
                _previewShowTimer?.Dispose();
                _previewShowTimer = null;
                _previewWatchTimer?.Dispose();
                _previewWatchTimer = null;

                if (TaskPreviewPopup != null)
                {
                    TaskPreviewPopup.IsOpen = false;
                    TaskPreviewPopup.Child = null;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"chiusura barra (risorse): {ex.Message}");
            }

            _balloonHost?.Hide();
            _globalMouseHook?.Dispose();
            _startMenuMonitor?.Dispose();
            _batteryMonitor?.Dispose();
            ShutdownTaskbar();
        }

        // ---------------------------------------------------------------
        //  Balloon notifications
        // ---------------------------------------------------------------

        private Controls.BalloonHost? _balloonHost;

        private void OnBalloonReceived(object? sender, BalloonNotification balloon)
        {
            _balloonHost ??= new Controls.BalloonHost(TrayArea);

            try
            {
                /* v3.7: se l'icona che ha generato la notifica sta in barra,
                 * il fumetto si ancora a LEI (la punta la indica) invece che
                 * al bordo dell'intera area di notifica: prima capitava che
                 * la puntina "non cogliesse" l'icona quando non era quella
                 * piu' a destra. Icona in overflow o rimossa: ripiego
                 * all'area di notifica (comportamento originale). */
                UIElement? iconAnchor = null;
                foreach (TrayIconModel m in _viewModel.NotificationArea.AllIcons)
                {
                    if (m.OwnerHwnd == balloon.OwnerHwnd && m.Uid == balloon.Uid
                        && TrayIcons.ItemContainerGenerator.ContainerFromItem(m)
                            is FrameworkElement container
                        && container.IsLoaded
                        && container.IsVisible
                        && container.ActualWidth > 0)
                    {
                        iconAnchor = container;
                        break;
                    }
                }

                _balloonHost.Show(balloon.Title, balloon.Text,
                                  balloon.InfoFlags, balloon.Timeout, iconAnchor);
            }
            catch (Exception)
            {
                _balloonHost.Hide();
            }
        }

        internal void ShutdownTaskbar()
        {
            if (_shuttingDown)
            {
                return;
            }
            _shuttingDown = true;

            try
            {
                try { _bridge.NetFlyoutUninit(); } catch { }
                try { _bridge.SetWin7NetworkFlyout(false); } catch { }
                try { _bridge.Net8FlyoutUninit(); } catch { }
                try { _bridge.SetWin8NetworkFlyout(false); } catch { }

                if (_appBarRegistered && _hwndSource != null)
                {
                    _bridge.UnregisterAppBar(_hwndSource.Handle);
                    _appBarRegistered = false;
                }

                _bridge.StopTray();
                _bridge.SetNativeTaskbarHidden(false);

                _hwndSource?.RemoveHook(WndProc);
                _viewModel.Dispose();
            }
            catch (Exception) { }
        }

        // ===============================================================
        //  AppBar and positioning
        // ===============================================================

        private void UpdateDpiScaling()
        {
            if (_hwndSource?.CompositionTarget == null)
            {
                return;
            }

            double scale = _hwndSource.CompositionTarget.TransformToDevice.M11;
            IsScaled = scale > 1.0;

            // v1.21.27 - la bandierina Start 8.1 e' ricampionata via GDI+ alla
            // scala dispositivo del monitor: quando questa cambia (avvio su uno
            // schermo secondario, WM_DPICHANGED) ripubblica lo sprite nitido.
            Win7Taskbar.Utilities.StartFlagAssets.ApplyToResources(scale);
        }

        private void PositionOnScreen()
        {
            if (_hwndSource == null)
            {
                return;
            }

            double scale = _hwndSource.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
            if (scale <= 0)
            {
                scale = 1.0;
            }

            double screenWidthDip = SystemParameters.PrimaryScreenWidth;
            double screenHeightDip = SystemParameters.PrimaryScreenHeight;

            double heightDip = ThemeTaskbarHeightDip;

            Width = screenWidthDip;
            Height = heightDip;
            Left = 0;
            Top = screenHeightDip - heightDip;

            AppBarEdge = "Bottom";
            AppBarEdgeIndex = (int)TaskbarEdge.Bottom;
            Orientation = Orientation.Horizontal;

            SetThumbnailEdge(this, (int)TaskbarEdge.Bottom);
            SetThumbnailScale(this,
                _hwndSource?.CompositionTarget?.TransformToDevice.M11 ?? 1.0);
        }

        private void RegisterAppBar()
        {
            if (_hwndSource == null || _appBarRegistered)
            {
                return;
            }

            double scale = _hwndSource.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
            int sizePx = Math.Max(1, (int)Math.Round(ThemeTaskbarHeightDip * scale));

            _appBarRegistered = _bridge.RegisterAppBar(
                _hwndSource.Handle, AppBarEdgeValue.Bottom, sizePx);
            if (_appBarRegistered)
            {
                // Il core, dentro la Register, esegue gia' QUERYPOS/SETPOS e
                // sposta la finestra sul rettangolo confermato dalla shell.
                _appBarCallbackMessage = _bridge.AppBarCallbackMessage();
                UpdateAppBarPosition();   // registra _appBarRect
            }
        }

        /// <summary>
        /// v3.4: UN SOLO punto in cui la barra comunica alla shell il rettangolo
        /// riservato (avvio, cambio monitor, cambio DPI, ABN_POSCHANGED, riavvio
        /// di Explorer). Il core esegue ABM_QUERYPOS/ABM_SETPOS e SPOSTA la
        /// finestra sul rettangolo confermato: area riservata e barra visibile
        /// restano la stessa cosa (invariante di ManagedShell AppBarWindow).
        /// English: single AppBar position update used by every geometry event.
        /// </summary>
        private void UpdateAppBarPosition()
        {
            if (!_appBarRegistered || _hwndSource == null)
            {
                return;
            }

            double scale = _hwndSource.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
            if (scale <= 0)
            {
                scale = 1.0;
            }
            int sizePx = Math.Max(1, (int)Math.Round(ThemeTaskbarHeightDip * scale));

            // Il rettangolo confermato dalla shell e' in PIXEL FISICI: il core
            // ci sposta lui stesso la finestra (SetWindowPos con SWP_NOZORDER,
            // lo stato topresta intatto) e notifica ABM_WINDOWPOSCHANGED.
            if (_bridge.SetAppBarPos(_hwndSource.Handle, AppBarEdgeValue.Bottom,
                                     sizePx, out Rect reserved) && !reserved.IsEmpty)
            {
                _appBarRect = reserved;
            }
        }

        /// <summary>
        /// WM_WINDOWPOSCHANGED: qualcuno ha mosso/ridimensionato la barra.
        /// Il nostro spostamento (il core, su UpdateAppBarPosition) porta la
        /// finestra esattamente sul rettangolo riservato e passa di qui senza
        /// effetti; se le coordinate NON coincidono, la barra e' stata spostata
        /// da un altro (la shell, risolvendo una sovrapposizione fra AppBar) e
        /// va riportata sul rettangolo riservato - la "restore state" di
        /// ManagedShell::AppBarWindow.
        /// </summary>
        private void MaybeReassertAppBarRect(IntPtr lParam)
        {
            try
            {
                if (lParam == IntPtr.Zero || _appBarRect.IsEmpty)
                {
                    return;
                }

                const uint SWPFLAG_NOSIZE = 0x0001;
                const uint SWPFLAG_NOMOVE = 0x0002;
                var wp = System.Runtime.InteropServices.Marshal
                    .PtrToStructure<NativeMethods.WINDOWPOS>(lParam);
                if (wp == null ||
                    ((wp.flags & SWPFLAG_NOMOVE) != 0 && (wp.flags & SWPFLAG_NOSIZE) != 0))
                {
                    return;   /* solo z-order: il rettangolo non cambia */
                }

                if (wp.x == (int)_appBarRect.X && wp.y == (int)_appBarRect.Y &&
                    wp.cx == (int)Math.Ceiling(_appBarRect.Width) &&
                    wp.cy == (int)Math.Ceiling(_appBarRect.Height))
                {
                    return;   /* e' il nostro spostamento */
                }

                ScheduleGeometrySync();
            }
            catch (Exception)
            {
                /* mai far morire la barra per un controllo di posizione */
            }
        }

        /// <summary>
        /// Ricalcolo dell'AppBar differito a priorita' di sfondo: dopo
        /// WM_DPICHANGED WPF ridimensiona la finestra con il rettangolo
        /// suggerito dal sistema, quindi il nostro aggiustamento deve partire
        /// DOPO il giro interno di WPF, non durante.
        /// </summary>
        private void ScheduleGeometrySync()
        {
            if (_geometrySyncPending)
            {
                return;
            }
            _geometrySyncPending = true;
            Dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() =>
            {
                _geometrySyncPending = false;
                if (_shuttingDown)
                {
                    return;
                }
                UpdateDpiScaling();
                // Posizione DIP di ripiego (aggiorna anche la scala delle
                // anteprime); il rettangolo definitivo lo detta subito dopo
                // la shell con UpdateAppBarPosition, in pixel fisici.
                PositionOnScreen();
                UpdateAppBarPosition();
            }));
        }

        /// <summary>
        /// v3.4: riavvio di Explorer (broadcast "TaskbarCreated").
        /// La registrazione AppBar vive nel processo della shell: con la
        /// vecchia shell muore anche la nostra area riservata, e il nuovo
        /// Explorer non sa nulla di noi. Qui la prenotazione viene rifatta,
        /// altrimenti dopo un riavvio di Explorer la work area non corrisponde
        /// piu' alla barra.
        /// </summary>
        private void OnExplorerRestarted()
        {
            if (_shuttingDown || _hwndSource == null || StartupGuard.SafeMode)
            {
                return;
            }

            _bridge.Log("appbar: TaskbarCreated ricevuto, ri-registrazione");

            // La vecchia registrazione e' gia' morta con la shell: si rimuove
            // lo stato interno (ABM_REMOVE sul nuovo shell e' un no-op) e si
            // rifatta la sequenza ABM_NEW + QUERYPOS/SETPOS.
            if (_appBarRegistered || _bridge.IsAppBarRegistered)
            {
                _bridge.UnregisterAppBar(_hwndSource.Handle);
                _appBarRegistered = false;
            }

            RunStage("appbar-riavvio-explorer", () =>
            {
                // Prima si rimette in auto-hide la barra nuova di Explorer
                // (come all'avvio), poi si registra la nostra: la shell non
                // deve trovare due barre sul bordo quando calcola la posa.
                _bridge.SetNativeTaskbarHidden(true);
                RegisterAppBar();
                UpdateAppBarPosition();
            });
        }

        /// <summary>v3.3: il dialogo Proprietà nativo rimanda qui le
        /// impostazioni scelte (OK/Applica/Apri ricerca).</summary>
        [System.Runtime.InteropServices.StructLayout(
            System.Runtime.InteropServices.LayoutKind.Sequential)]
        private struct NativeCopyData
        {
            public UIntPtr dwData;
            public int cbData;
            public IntPtr lpData;
        }

        /// <summary>
        /// Ordine della combo lingua in Proprietà: e' l'elenco del CORE
        /// NATIVO (Strings.cpp: 0=it, 1=en, ... 10=ar), letto una volta
        /// all'avvio. Non e' piu' una copia scritta a mano: prima l'arabo
        /// mancava, "IndexOf" rispondeva -1 e il nativo apriva le Proprieta'
        /// in italiano su un sistema arabo (vedi NativeLanguageRegistry).
        /// </summary>
        private static readonly string[] kLangCodes = NativeLanguageRegistry.Codes;

        private bool HandlePropsCopyData(IntPtr lParam)
        {
            try
            {
                var cds = System.Runtime.InteropServices.Marshal
                    .PtrToStructure<NativeCopyData>(lParam);
                if (cds.dwData.ToUInt64() != 0x57375041UL ||
                    cds.cbData < 24 || cds.lpData == IntPtr.Zero)
                {
                    return false;
                }

                int seconds      = System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 0);
                int nativeFlyout = System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 4);
                int enableSearch = System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 8);
                int lang         = System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 12);
                int openSearch   = System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 16);
                int closeApp     = System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 20);
                var st = RetroBar.Utilities.Settings.Instance;
                int netFlyout    = cds.cbData >= 28
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 24)
                    : st.NetworkFlyoutMode;
                /* v2.38: mixer volume classico e flyout batteria ricreato.
                 * Letti solo se il pacchetto li contiene (>= 32/36 byte),
                 * cosi' un nativo piu' vecchio non li azzera. */
                int classicVolume = cds.cbData >= 32
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 28)
                    : (st.UseClassicVolumeMixer ? 1 : 0);
                int batteryFlyout = cds.cbData >= 36
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 32)
                    : (st.UseBatteryFlyout ? 1 : 0);
                /* v2.47: Aero Peek e le NOSTRE barre degli strumenti.
                 * Campi aggiunti IN CODA al pacchetto, come i precedenti:
                 * si leggono solo se il pacchetto li contiene davvero
                 * (nativo piu' vecchio -> nessun azzeramento). */
                int aeroPeek = cds.cbData >= 40
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 36)
                    : (st.AeroPeek ? 1 : 0);
                bool hasToolbars = cds.cbData >= 52;
                int tbDesktop = hasToolbars
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 40) : -1;
                int tbAddress = hasToolbars
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 44) : -1;
                int tbLinks = hasToolbars
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 48) : -1;
                st.ShowClockSeconds   = seconds == 1;
                st.UseNativeClockFlyout = nativeFlyout == 1;
                st.EnableAppSearch    = enableSearch == 1;
                /* Indice fuori elenco: inglese, mai italiano per omissione. */
                string newLang = (lang >= 0 && lang < kLangCodes.Length)
                    ? kLangCodes[lang] : RetroBar.Utilities.Settings.DefaultLanguageCode;
                if (newLang != st.Language)
                {
                    st.Language = newLang;
                    Win7Taskbar.Utilities.LocalizationManager.ApplyLanguage(newLang);
                }
                // v2.37 punto 16: anche il flyout di rete parla la lingua
                // dell'app (traduzioni gia' presenti nella mod).
                try { _bridge.NetFlyoutSetLanguage(lang); } catch { }
                // v3.8: anche il riquadro variante Windows 8.
                try { _bridge.Net8FlyoutSetLanguage(lang); } catch { }
                st.NetworkFlyoutMode = netFlyout;
                st.UseClassicVolumeMixer = classicVolume == 1;
                st.UseBatteryFlyout = batteryFlyout == 1;
                st.AeroPeek = aeroPeek == 1;
                /* v3.5: stile dell'indicatore della lingua di input.
                 * Aggiunto IN CODA al pacchetto (offset 52, 56 byte): si
                 * legge solo se il nativo lo contiene davvero, cosi' un
                 * core piu' vecchio non lo azzera. */
                int inputLanguageMode = cds.cbData >= 56
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 52)
                    : st.InputLanguageMode;
                st.InputLanguageMode =
                    (inputLanguageMode is < 0 or > 3) ? 1 : inputLanguageMode;
                /* Windows 11-only Task Manager selector, appended at offset
                 * 56. Older native cores and Windows 10 retain Automatic. */
                int taskManagerMode = cds.cbData >= 60
                    ? System.Runtime.InteropServices.Marshal.ReadInt32(cds.lpData, 56)
                    : st.TaskManagerMode;
                st.TaskManagerMode = taskManagerMode is >= 0 and <= 2
                    ? taskManagerMode : 0;

                /* v1.21.7 - extra settings (offsets 60, 64, 68, 72 = a
                 * 76-byte packet). Same rule as the previous fields: only what
                 * the packet really contains is read, so an older core never
                 * clears anything. */
                if (cds.cbData >= 76)
                {
                    int flyoutColorMode = System.Runtime.InteropServices.Marshal
                        .ReadInt32(cds.lpData, 60);
                    int flyoutColorRgb = System.Runtime.InteropServices.Marshal
                        .ReadInt32(cds.lpData, 64);
                    int privacyMode = System.Runtime.InteropServices.Marshal
                        .ReadInt32(cds.lpData, 68);
                    int themeSelection = System.Runtime.InteropServices.Marshal
                        .ReadInt32(cds.lpData, 72);

                    st.FlyoutColorMode = flyoutColorMode == 1 ? 1 : 0;

                    /* The colour arrives as 0x00RRGGBB and is stored in the
                     * canonical "#RRGGBB" form (the property normalizes and
                     * SetField saves). With the "system colour" mode the custom
                     * colour is NOT touched: the previous choice stays, and the
                     * user finds it again when switching back to custom. */
                    if (flyoutColorMode == 1)
                    {
                        st.FlyoutCustomColor = "#" + (flyoutColorRgb & 0xFFFFFF)
                            .ToString("X6", System.Globalization.CultureInfo.InvariantCulture);
                    }

                    st.ConnectionFlyoutPrivacyMode = privacyMode == 1 ? 1 : 0;

                    /* Skin: v1.21.19 - entrambe le skin esistono davvero
                     * (Windows 7 e Windows 8.1, vedi TaskbarThemeIds). Il
                     * valore salvato passa comunque da Normalize(), quindi un
                     * pacchetto che chiede una skin inesistente non entra in
                     * configurazione e non fa caricare un tema per mano. */
                    bool themeChanged = themeSelection != AppliedThemeSelection;
                    st.ThemeSelection = themeSelection;
                    if (themeChanged &&
                        RetroBar.Utilities.TaskbarThemeIds.IsImplemented(st.ThemeSelection))
                    {
                        AppliedThemeSelection = RetroBar.Utilities.TaskbarThemeIds.Normalize(
                            st.ThemeSelection);
                        /* La skin si applica SUBITO: sostituire
                         * Application.Resources e' quello che fa App.xaml.cs
                         * all'avvio (vedi ThemeLoader.ReapplyNow). Se non
                         * riesce, resta il tema precedente e il messaggio nel
                         * log dice che serve un riavvio: nessuna barra rotta. */
                        string appliedSkin = (AppliedThemeSelection ==
                                              RetroBar.Utilities.TaskbarThemeIds.Windows81)
                                                 ? "Windows 8.1" : "Windows 7";
                        string themeSwapResult = ThemeLoader.ReapplyNow();
                        _bridge.Log("proprieta': skin " + appliedSkin + " -> " +
                                    themeSwapResult);

                        /* v1.21.27 - ThemeLoader.ReapplyNow sostituisce
                         * Application.Resources e con esso perde le risorse
                         * pubblicate a runtime SOPRA il tema: la maschera alpha
                         * derivata del bordo anteprima (DwmPreviewBorderMaskImage)
                         * e il pennello d'accento dal vivo (DwmPreviewAccentBrush).
                         * Senza di esse la cornice delle anteprime torna alla PNG
                         * grezza ad alpha pieno: "le trasparenze non ci sono".
                         * Ricostruiamole subito dopo uno swap riuscito. */
                        if (themeSwapResult.StartsWith(
                                "tema applicato subito", StringComparison.Ordinal))
                        {
                            RestorePreviewResourcesAfterThemeSwap();
                        }
                    }

                    /* The choice is saved: it is published to the core right
                     * away, so the recreated flyout speaks the chosen mode
                     * without waiting for a restart. */
                    ApplyExtraSettings();
                }
                /* v1.21.37 - avvio automatico con Windows, implementazione
                 * COPIATA DA RETROBAR (PropertiesWindow.xaml.cs:
                 * LoadAutoStart/CbAutoStart_OnChecked). Offset 76, pacchetto
                 * da 80 byte: si legge solo se il nativo lo contiene davvero,
                 * come tutti i campi aggiunti in coda. L'effetto e'
                 * reversibile: spuntato scrive il percorso dell'eseguibile
                 * nel valore "Win7Taskbar" della chiave Run dell'utente,
                 * non spuntato elimina il valore (Utilities/AutoStart.cs). */
                if (cds.cbData >= 80)
                {
                    int autoStart = System.Runtime.InteropServices.Marshal
                        .ReadInt32(cds.lpData, 76);
                    Win7Taskbar.Utilities.AutoStart.SetEnabled(autoStart == 1);
                    _bridge.Log("proprieta': avvio automatico " +
                                (autoStart == 1 ? "attivato" : "disattivato") +
                                " (logica RetroBar)");
                }
                if (hasToolbars)
                {
                    /* Le caselle della scheda "Barre degli strumenti" sono le
                     * stesse barre che si accendono dal menu contestuale della
                     * barra: qui si applica e si salva la scelta. */
                    SetToolbarStates(tbDesktop == 1, tbLinks == 1, tbAddress == 1);
                }
                /* v2.63: le quattro scelte dei riquadri, applicate qui come
                 * all'avvio (registro di sistema + core): prima si scriveva
                 * solo la chiave della batteria, e solo se l'utente premeva
                 * OK/Applica. */
                ApplyShellFlyoutPreferences();
                UpdateSearchButtonVisibility();
                if (openSearch == 1)
                {
                    OpenAppSearch();
                }
                if (closeApp == 1)
                {
                    // come il vecchio pulsante "Chiudi" della finestra WPF
                    App.RequestShutdown();
                }
                return true;
            }
            catch (Exception ex)
            {
                _bridge.Log($"props copydata: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// WPF opacity masks use only brush alpha; grayscale luminance is not
        /// converted to opacity. Keep the source PNG untouched and derive a
        /// readable Aero-style alpha mask from its shading once in memory.
        /// </summary>
        private void EnsureDwmPreviewBorderMask()
        {
            if (_dwmPreviewMaskReady)
            {
                return;
            }

            try
            {
                if (TryFindResource("DwmPreviewBorderImage") is not
                    System.Windows.Media.Imaging.BitmapSource source ||
                    source.PixelWidth <= 0 || source.PixelHeight <= 0)
                {
                    return;
                }

                var converted = new System.Windows.Media.Imaging.FormatConvertedBitmap(
                    source, PixelFormats.Bgra32, null, 0);
                int stride = checked(converted.PixelWidth * 4);
                byte[] pixels = new byte[checked(stride * converted.PixelHeight)];
                converted.CopyPixels(pixels, stride, 0);

                for (int i = 0; i < pixels.Length; i += 4)
                {
                    int blue = pixels[i];
                    int green = pixels[i + 1];
                    int red = pixels[i + 2];
                    int sourceAlpha = pixels[i + 3];
                    // Integer Rec.709 approximation. Pure luminance produced
                    // about 21% median opacity and was a little too faint.
                    // A very small floor keeps the typical frame near 23%:
                    // slightly more transparent than the previous 25%, but
                    // still readable compared with the original 21% result.
                    int luminance = (54 * red + 183 * green + 19 * blue + 128) >> 8;
                    int maskCoverage = 8 + ((247 * luminance + 127) / 255);
                    pixels[i] = 0xFF;
                    pixels[i + 1] = 0xFF;
                    pixels[i + 2] = 0xFF;
                    pixels[i + 3] = (byte)((sourceAlpha * maskCoverage + 127) / 255);
                }

                var mask = System.Windows.Media.Imaging.BitmapSource.Create(
                    converted.PixelWidth, converted.PixelHeight,
                    converted.DpiX, converted.DpiY,
                    PixelFormats.Bgra32, null, pixels, stride);
                if (mask.CanFreeze)
                {
                    mask.Freeze();
                }

                Application.Current.Resources[DwmPreviewBorderMaskImageKey] = mask;
                _dwmPreviewMaskReady = true;
            }
            catch (Exception ex)
            {
                // The original PNG alpha remains a valid fallback mask.
                try { _bridge.Log($"preview accent mask: {ex.Message}"); }
                catch { }
            }
        }

        /// <summary>
        /// Publishes a new brush object instead of mutating the fallback brush.
        /// WPF DynamicResource expressions then re-evaluate the key for every
        /// frame slice, including slices in an already-open preview popup.
        /// </summary>
        private void UpdateDwmPreviewAccentColor(uint? messageArgb = null)
        {
            try
            {
                EnsureDwmPreviewBorderMask();

                uint argb;
                if (messageArgb.HasValue)
                {
                    argb = messageArgb.Value;
                }
                else if (NativeMethods.DwmGetColorizationColor(
                             out argb, out _) < 0)
                {
                    // Keep the XAML fallback if DWM cannot supply a color.
                    return;
                }

                /* Unchanged colour: nothing to re-tint. The message path and
                 * the drift check (EnsureDwmAccentFresh) can both fire for
                 * the same change, so the last applied value is recorded
                 * here and doubles as the de-dup key. */
                if (_dwmAccentArgb == argb)
                {
                    return;
                }
                _dwmAccentArgb = argb;

                // Opacity comes from the derived shaded-alpha mask. Keep
                // this brush opaque: using the DWM alpha here as well
                // would multiply frame transparency a second time.
                Color accent = Color.FromArgb(
                    0xFF,
                    (byte)((argb >> 16) & 0xFF),
                    (byte)((argb >> 8) & 0xFF),
                    (byte)(argb & 0xFF));
                var brush = new SolidColorBrush(accent);
                if (brush.CanFreeze)
                {
                    brush.Freeze();
                }

                Application.Current.Resources[DwmPreviewAccentBrushKey] = brush;

                /* The XAML slices pick the new brush up on their own through
                 * their DynamicResource; the frames the core already rendered
                 * were tinted with the previous accent, so they are dropped
                 * and re-rendered (only when a popup is actually open). */
                RefreshNativePreviewFrames();
            }
            catch (Exception ex)
            {
                // Accent color is cosmetic: failure must never affect taskbar
                // startup or the live DWM thumbnail relationship.
                try { _bridge.Log($"preview accent update: {ex.Message}"); }
                catch { }
            }
        }

        /// <summary>
        /// Copertura del caso in cui il "colore dietro" le anteprime cambia
        /// senza che WM_DWMCOLORIZATIONCOLORCHANGED venga mai trasmesso
        /// (accento automatico preso dallo sfondo, slideshow del tema,
        /// passaggio a contrasto elevato su Windows 10/11): confronta il
        /// colore di colorizzazione vivo con l'ultimo effettivamente
        /// applicato e, solo a deriva reale, riesegue lo stesso identico
        /// percorso del messaggio (brush + invalidazione dei frame nativi).
        /// Una query DWM per controllo; la logica di render non cambia.
        /// </summary>
        /// <summary>
        /// v1.21.27 - dopo uno swap del tema (ThemeLoader.ReapplyNow) le risorse
        /// pubblicate a runtime sopra il dizionario del tema non esistono piu':
        /// azzera le cache della maschera e dell'accento e ripubblica entrambe nel
        /// NUOVO Application.Resources, cosi' le anteprime DWM ritrovano la loro
        /// trasparenza (la maschera alpha derivata) e il colore d'accento dal vivo.
        /// Idempotente e puramente cosmetica: un'eccezione lascia la cornice XAML.
        /// </summary>
        private void RestorePreviewResourcesAfterThemeSwap()
        {
            try
            {
                _dwmPreviewMaskReady = false;
                _dwmAccentArgb = null;
                UpdateDwmPreviewAccentColor();
            }
            catch (Exception ex)
            {
                try { _bridge.Log($"ripristino risorse anteprima: {ex.Message}"); }
                catch { }
            }
        }

        private void EnsureDwmAccentFresh()
        {
            try
            {
                if (NativeMethods.DwmGetColorizationColor(out uint live,
                                                        out _) < 0)
                {
                    return;
                }

                if (_dwmAccentArgb != live)
                {
                    UpdateDwmPreviewAccentColor();
                }
            }
            catch
            {
                /* Come il resto del percorso accento: cosmetico, mai fatale. */
            }
        }

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            const int WM_DISPLAYCHANGE = 0x007E;
            const int WM_DWMCOLORIZATIONCOLORCHANGED = 0x0320;
            const int WM_DPICHANGED = 0x02E0;
            const int WM_MOUSEACTIVATE = 0x0021;
            const int WM_ACTIVATE = 0x0006;
            const int WM_COPYDATA = 0x004A;
            const int WM_WINDOWPOSCHANGED = 0x0047;
            const int WM_SIZE = 0x0005;
            const int WM_SYSCOMMAND = 0x0112;
            const int SC_MINIMIZE = 0xF020;
            const int SIZE_MINIMIZED = 1;
            const int SW_SHOWNOACTIVATE = 4;
            const int MA_ACTIVATE = 1;

            // v3.4: notifiche ABN_* della shell sul messaggio registrato con
            // ABM_NEW. Prima il messaggio arrivava e nessuno lo guardava:
            // quando la shell riorganizza le AppBar (una compare, un'altra
            // sparisce, cambia un monitor) la nostra area riservata restava
            // quella di prima. Il core dentro AppBarNotify riesegue
            // QUERYPOS/SETPOS e risistema la finestra (flusso ManagedShell).
            if (_appBarCallbackMessage != 0 && msg == _appBarCallbackMessage)
            {
                _bridge.AppBarNotify((uint)wParam.ToInt64(), lParam.ToInt32());
                handled = true;
                return IntPtr.Zero;
            }

            // v3.4: Explorer (ri)avviato: la registrazione AppBar e' morta
            // con la vecchia shell. Si rifatta fuori dal messaggio, per non
            // chiamare SHAppBarMessage mentre la shell e' ancora nel bel
            // mezzo del proprio broadcast.
            if (_taskbarCreatedMessage != 0 && msg == (int)_taskbarCreatedMessage)
            {
                handled = true;
                Dispatcher.BeginInvoke(DispatcherPriority.Background,
                                       new Action(OnExplorerRestarted));
                return IntPtr.Zero;
            }

            switch (msg)
            {
                case WM_DWMCOLORIZATIONCOLORCHANGED:
                    // Microsoft documents wParam as the new 0xAARRGGBB
                    // colorization color. This hook runs on WPF's UI thread,
                    // where ResourceDictionary updates are valid.
                    UpdateDwmPreviewAccentColor(
                        unchecked((uint)wParam.ToInt64()));
                    handled = true;
                    return IntPtr.Zero;
                case WM_COPYDATA:
                    handled = HandlePropsCopyData(lParam);
                    return IntPtr.Zero;
                case WM_WINDOWPOSCHANGED:
                    // v3.4: se la barra e' stata spostata da qualcun altro
                    // (la shell, risolvendo una sovrapposizione fra AppBar)
                    // torna sul rettangolo riservato. Guardare il dettaglio
                    // in MaybeReassertAppBarRect.
                    if (_appBarRegistered)
                    {
                        MaybeReassertAppBarRect(lParam);
                    }
                    break;
                case WM_ACTIVATE:
                    // v3.4: ABM_ACTIVATE come in ManagedShell (AppBarActivate):
                    // la shell tiene conto dello stato attivo delle AppBar.
                    if (_appBarRegistered && wParam.ToInt64() != 0)
                    {
                        _bridge.AppBarActivate(hwnd);
                    }
                    break;
                case WM_SYSCOMMAND:
                    // v2.19: il pulsante Aero Peek / Mostra desktop / Win+D
                    // minimizza ogni finestra top-level: la nostra taskbar
                    // deve restare visibile come quella vera -> ingoia
                    // SC_MINIMIZE.
                    if ((wParam.ToInt64() & 0xFFF0) == SC_MINIMIZE)
                    {
                        handled = true;
                        return IntPtr.Zero;
                    }
                    break;
                case WM_SIZE:
                    // Se la shell ci minimizza comunque (ShowWindow diretto
                    // senza WM_SYSCOMMAND), ripristinaci subito senza rubare
                    // il focus.
                    if (wParam.ToInt64() == SIZE_MINIMIZED)
                    {
                        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                        handled = true;
                        return IntPtr.Zero;
                    }
                    break;
                case WM_MOUSEACTIVATE:
                    // v2.5: la banda Indirizzo deve accettare la tastiera
                    // (come quella di ExplorerEx). WS_EX_NOACTIVATE di solito
                    // fa sì che il click non attivi la finestra e il TextBox
                    // resta muto: quando il click cade proprio lì dentro,
                    // togliamo WS_EX_NOACTIVATE per quel click e lasciamo
                    // attivare (MA_ACTIVATE); lo stile torna al suo posto
                    // quando il campo perde il focus (vedi OnLostFocus).
                    if (IsMouseInsideAddressBand(lParam))
                    {
                        LiftNoActivateForTyping();
                        return new IntPtr(MA_ACTIVATE);
                    }
                    break;
                case WM_DISPLAYCHANGE:
                case WM_DPICHANGED:
                    // v3.4: il ricalcolo geometry va fatto DOPO che WPF ha
                    // sistemato la finestra (su WM_DPICHANGED applica il
                    // rettangolo suggerito dal sistema dopo questo hook):
                    // lo scheduling a priorita' sfondo evita che il nostro
                    // spostamento venga sovrascritto dal resize di WPF.
                    OverflowPopup.IsOpen = false;
                    _bridge.ReanchorFlyouts();
                    UpdateDpiScaling();
                    _bridge.ReassertNativeTaskbarHidden();
                    ScheduleGeometrySync();
                    break;
            }

            return IntPtr.Zero;
        }

        // ===============================================================
        //  Superbar: click and jump-list - Start button with 3 states
        // ===============================================================

        // Start menu monitoring for 3-state button (idle/hover/pressed)
        private StartMenuMonitor? _startMenuMonitor;
        private bool _allowOpenStart = true;
        private BatteryMonitor? _batteryMonitor;
        private GlobalMouseHook? _globalMouseHook;

        // ===============================================================
        //  Orb START: stato iniziale IDLE garantito
        // ===============================================================
        //
        // Richiesta dell'utente: "quando il programma parte l'orb parte
        // PREMETUTO o in HOVER; non deve succedere". Il tema reagisce a
        // IsMouseOver/IsPressed/IsChecked: tutti e tre vanno tenuti spenti
        // finche' un evento VERO del mouse non arriva. Il soppressore si
        // scioglie al primo movimento reale del mouse sul window (o con un
        // colpo di sicurezza a 2 s per il solo puntatore touch, che non
        // genera MouseMove prima del tap) e non tocca niente altro: dopo,
        // hover/pressed seguono solo gli eventi del puntatore, come su
        // Windows 7 dove l'orb parte Idle per definizione.

        private bool _startOrbArmed;

        private void ArmStartOrb()
        {
            if (StartButton == null || _startOrbArmed)
            {
                return;
            }
            _startOrbArmed = true;

            // Nessuna eredita' dallo stato precedente: si parte Idle.
            StartButton.IsChecked = false;
            _allowOpenStart = true;

            // Finche' il puntatore non si muove davvero, l'orb non puo'
            // diventare hover: IsHitTestVisible=false rende l'elemento
            // invisibile all'hit-testing e IsMouseOver resta falso anche
            // se il cursore e' gia' fermo sopra di lui all'avvio.
            StartButton.IsHitTestVisible = false;

            PreviewMouseMove += StartOrbFirstMouseMove;
            PreviewMouseDown += StartOrbFirstMouseMove;

            var safety = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromSeconds(2),
            };
            safety.Tick += (_, _) =>
            {
                safety.Stop();
                ReleaseStartOrbSuppressor();
            };
            safety.Start();
        }

        private void StartOrbFirstMouseMove(object? sender, MouseEventArgs e)
        {
            ReleaseStartOrbSuppressor();
        }

        private void ReleaseStartOrbSuppressor()
        {
            if (StartButton != null)
            {
                StartButton.IsHitTestVisible = true;
            }
            PreviewMouseMove -= StartOrbFirstMouseMove;
            PreviewMouseDown -= StartOrbFirstMouseMove;
        }

        private void StartButton_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            // English: Determine if we should open start menu (when button not already pressed)
            // Italiano: Determina se aprire menu Start (quando pulsante non già premuto)
            //
            // v2.46: il vero stato del menu lo sa il monitor (eventi di sistema
            // reali). Qui si tiene solo aggiornato _allowOpenStart, che resta
            // usato dal resto del file.
            if (sender is ToggleButton toggle)
            {
                _allowOpenStart = _startMenuMonitor?.IsPressed != true &&
                                  toggle.IsChecked != true;
            }
        }

        /// <summary>
        /// v2.46: click sull'orb.
        ///
        /// Il vecchio codice decideva "apri o chiudi" da un flag aggiornato al
        /// MouseDown, e ad ogni apertura armava un NUOVO timer di controllo
        /// (700 ms) che non veniva mai annullato. Con un paio di click ravvicinati
        /// restavano piu' timer in volo: ognuno che scadeva chiamava il ripiego
        /// OpenStartFallback(), che apriva il menu un'altra volta. E' il "menu
        /// Start che si apre in loop" segnalato dall'utente.
        ///
        /// Adesso:
        ///   - lo stato di partenza e' quello confermato dal monitor
        ///     (StartMenuMonitor.IsPressed), non da un flag indovinato;
        ///   - i click troppo ravvicinati (rimbalzi, doppio click, click
        ///     sintetici) vengono ignorati con una finestra di tolleranza;
        ///   - esiste UN solo timer di controllo: armarlo di nuovo annulla il
        ///     precedente, e il ripiego scatta solo se nel frattempo il monitor
        ///     non ha pubblicato alcun evento (tasto Win davvero inghiottito);
        ///   - la chiusura si fa con lo stesso tasto Win che il sistema usa
        ///     per chiudere il menu aperto.
        /// </summary>
        private void StartButton_Click(object sender, RoutedEventArgs e)
        {
            // English: Start button 3-state logic - hover/idle/pressed with fades already correct in XAML
            // Italiano: Logica 3 stati pulsante Start

            if (sender is not ToggleButton toggle)
            {
                return;
            }

            try
            {
                var now = DateTime.UtcNow;

                // Anti-rimbalzo: due click nello stesso "tocco" non devono
                // aprire e chiudere il menu a raffica (era l'altra meta' del
                // loop visto a schermo).
                if ((now - _lastStartToggleUtc).TotalMilliseconds <
                    StartToggleDebounceMs)
                {
                    if (StartButton != null)
                    {
                        StartButton.IsChecked = _startMenuMonitor?.IsPressed == true;
                    }
                    return;
                }
                _lastStartToggleUtc = now;

                bool menuOpen = _startMenuMonitor?.IsPressed == true ||
                                toggle.IsChecked == true;

                if (menuOpen)
                {
                    // Il menu e' aperto: questo click lo chiude. Il tasto Win
                    // e' gia' il "toggle" che chiude il menu aperto su Windows
                    // 10/11, e non serve armare alcun ripiego.
                    CancelStartWatchdog();
                    toggle.IsChecked = false;
                    _bridge.ShowStartMenu();
                    StartTaskbarGuard();
                    return;
                }

                // Aprire
                _bridge.ShowStartMenu();
                StartTaskbarGuard();

                // Lo stato premuto resta finche' il menu e' visibile: lo
                // spegne il monitor quando il menu si chiude.
                toggle.IsChecked = true;
                _startMenuMonitor?.NotifyStartMenuOpened();
                ArmStartWatchdog();
            }
            catch (Exception ex)
            {
                // Un errore qui non deve lasciare l'orb premuto per sempre.
                Debug.WriteLine($"pulsante Start: {ex}");
                CancelStartWatchdog();
                if (StartButton != null)
                {
                    StartButton.IsChecked = false;
                }
            }
        }

        /// <summary>Finestra di tolleranza fra due click sull'orb.</summary>
        private const int StartToggleDebounceMs = 300;

        /// <summary>
        /// Ritardo del controllo di apertura del menu Start.
        /// v2.60: 700 -> 1200 ms. A 700 ms un menu Start di Windows 11 che
        /// sta ancora comparendo veniva giudicato "non aperto" e il ripiego
        /// lo apriva una seconda volta: era l'altra intermittenza segnalata.
        /// </summary>
        private const int StartWatchdogMs = 1200;

        private DateTime _lastStartToggleUtc;
        private TimerLease? _startWatchdog;
        private int _startMenuEventSeq;
        private int _startWatchdogSeqAtArm;

        /// <summary>
        /// v2.32/v2.46: watchdog di apertura. Se entro il ritardo il monitor
        /// non ha pubblicato NESSUN evento (tasto Win filtrato da hook di
        /// Windhawk, UIPI, policy...), si usa il ripiego alla Open-Shell
        /// (SC_TASKLIST mirato alla Shell_TrayWnd di Explorer). Ce n'e' uno
        /// solo: armarlo di nuovo smonta il precedente (Dispose), quindi non
        /// possono piu' restare timer in volo che riaprono il menu a sorpresa.
        /// </summary>
        private void ArmStartWatchdog()
        {
            CancelStartWatchdog();

            _startWatchdogSeqAtArm = _startMenuEventSeq;

            var lease = new TimerLease(StartWatchdogMs, StartWatchdog_Tick);
            _startWatchdog = lease;
            lease.Start();
        }

        private void StartWatchdog_Tick(object? sender, EventArgs e)
        {
            CancelStartWatchdog();

            // Il monitor ha parlato (menu aperto, o tentativo gia' deciso):
            // nessun ripiego.
            if (_startMenuEventSeq != _startWatchdogSeqAtArm)
            {
                return;
            }

            /* v2.47: ultima condizione, la piu' importante: il ripiego parte
             * solo se il menu NON e' davvero a schermo. Prima bastava che il
             * monitor non avesse pubblicato eventi, ma su un sistema dove
             * l'hook non vede il menu l'apertura riuscita veniva "riscoperta
             * come fallita" e il menu si apriva una seconda volta: era l'altra
             * meta' del "menu Start che si apre in loop". */
            if (StartButton != null && StartButton.IsChecked == true &&
                _startMenuMonitor?.IsMenuVisible() != true)
            {
                _bridge.OpenStartFallback();
            }
        }

        /// <summary>Smonta il watchdog (RAII: stop + distacco del gestore).</summary>
        private void CancelStartWatchdog()
        {
            var lease = _startWatchdog;
            _startWatchdog = null;
            lease?.Dispose();
        }

        private void OnStartMenuVisibilityChanged(object? sender, StartMenuVisibilityEventArgs e)
        {
            _startMenuEventSeq++;

            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (StartButton != null)
                {
                    StartButton.IsChecked = e.Visible;
                }
            }));
        }

        // Guard against Explorer taskbar reappearing

        private int _importFailures;

        private void ImportExplorerIconsWithRetry()
        {
            // Una sola chiamata. Il gestore nativo (TrayService::
            // ImportExplorerIcons) e' idempotente e pilotato dagli EVENTI:
            // prima passata subito, un solo ripiego interno dopo 2,5 s,
            // poi aggiunte perche' il nostro server tray e' attivo e il
            // riavvio di Explorer si annuncia da solo (TaskbarCreated).
            // Il timer gestito a 5 tentativi della 1.9.x ripeteva lavoro
            // gia' coperto e moltiplicava le letture della toolbar altrui.
            try
            {
                int first = _bridge.ImportExplorerTrayIcons();
                Debug.WriteLine($"Importazione icone avviata (esito: {first})");
            }
            catch (Exception ex)
            {
                StartupGuard.Report(ex, "importazione icone da Explorer (avvio)");
                _importFailures++;
            }
        }

        /// <summary>
        /// v2.60: il ritorno della barra nativa non si insegue piu' con un
        /// DispatcherTimer a 100 ms per tre secondi. Quella raffica di
        /// ri-nascondi era una delle cause del lampeggio visto premendo
        /// Start: Explorer rimostra la sua barra e il ciclo la rinascondeva
        /// a ripetizione. Adesso il ri-nascondi vive nel core come EVENTO
        /// (AppBarService::HideWatcherProc), quindi qui basta una richiesta
        /// singola, subito dopo l'apertura del menu.
        /// </summary>
        private void StartTaskbarGuard()
        {
            if (!_bridge.IsNativeTaskbarHidden())
            {
                return;
            }

            _bridge.ReassertNativeTaskbarHidden();
        }

        private void ShowDesktopButton_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            e.Handled = true;

            Point origin = PointToScreen(Mouse.GetPosition(this));

            int choice = _bridge.ShowContextMenu(
                (int)Math.Round(origin.X),
                (int)Math.Round(origin.Y),
                bottomEdge: true,
                L("lang_show_desktop", "Show desktop"),
                L("lang_peek_desktop", "Peek at desktop"));

            switch (choice)
            {
                case 1:
                    SetDesktopPeek(false);
                    _bridge.ToggleShowDesktop();
                    break;

                case 2:
                    SetDesktopPeek(true);
                    break;
            }
        }

        private void ShowDesktopButton_Click(object sender, RoutedEventArgs e)
        {
            SetDesktopPeek(false);

            _bridge.ToggleShowDesktop();

            if (sender is ToggleButton toggle)
            {
                toggle.IsChecked = false;
            }
        }

        private void ShowDesktopButton_MouseEnter(object sender, MouseEventArgs e)
        {
            SetDesktopPeek(true);
        }

        private void ShowDesktopButton_MouseLeave(object sender, MouseEventArgs e)
        {
            SetDesktopPeek(false);
        }

        private void SetDesktopPeek(bool enable)
        {
            /* v2.47: se nelle Proprieta' la casella "Anteprima del desktop con
             * Aero Peek" e' spenta, il pulsante Mostra desktop continua a
             * minimizzare ma NON rende trasparenti le finestre. */
            if (enable && !RetroBar.Utilities.Settings.Instance.AeroPeek)
            {
                return;
            }

            if (_peekActive == enable)
            {
                return;
            }

            _peekActive = enable;

            IntPtr self = _hwndSource?.Handle ?? IntPtr.Zero;
            NativeMethods.ActivateLivePreview(enable, self);
        }

        private bool _peekActive;

        private bool _pressedWhileActive;

        private void TaskButton_PreviewMouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Left)
            {
                return;
            }

            _pressedWhileActive =
                sender is FrameworkElement { DataContext: TaskGroup group } &&
                group.IsActive;

            // v1.21.21: spostamento del bottone con il meccanismo di RetroBar
            // (DragDrop OLE sul bottone + indicatore di inserimento + drop),
            // che e' l'unico gesto armato. Il riordino "persistente" scritto
            // in TaskbarWindow.TaskOrder.cs (cattura del mouse, icona fantasma,
            // caret in una finestra separata) resta compilato ma NON armato:
            // era quello che sul campo si comportava in modo incoerente, e due
            // gesti che catturano la stessa pressione si contendono il mouse.
            // L'ordine ottenuto con il trascinamento viene comunque SALVATO:
            // vedi HandleTaskGroupReorderDrop -> ApplyUserTaskbarOrder.
            const bool UseRetroBarSessionReorder = true;
            if (UseRetroBarSessionReorder &&
                sender is FrameworkElement pressedButton)
            {
                _reorderCandidate = pressedButton;
                _reorderPressPoint = e.GetPosition(this);
            }

            // INCOMPLETE / TEMPORARILY DISABLED: keep the complete Jump List
            // gesture implementation in TaskbarWindow.JumpList.cs, but do not
            // arm it until its remaining behavior has been completed.
            // if (sender is FrameworkElement fe)
            // {
            //     BeginPotentialJumpListDrag(fe, e);
            // }

            // v1.21.7: possible button reorder (extra settings -> icon
            // order). The gesture is the tray one: immediate capture, and the
            // drag only starts past the system threshold. A normal click is
            // unchanged. If the Jump List is re-armed one day,
            // BeginPotentialTaskOrderDrag steps back by itself while that
            // gesture owns the pointer.
            if (sender is FrameworkElement orderElement)
            {
                BeginPotentialTaskOrderDrag(orderElement, e);
            }
        }

        /// <summary>v2.28: avvio robusto: prima la shell nativa con retry,
        /// poi il ripiego managed. Ritorna false solo se entrambi falliscono
        /// (il log-core tiene traccia dei tentativi).</summary>
        private bool LaunchPathSafe(string path)
        {
            if (string.IsNullOrEmpty(path))
            {
                return false;
            }

            if (_bridge.ShellOpen(path))
            {
                return true;
            }

            try
            {
                System.Diagnostics.Process.Start(
                    new System.Diagnostics.ProcessStartInfo
                    {
                        FileName = path,
                        UseShellExecute = true
                    });
                return true;
            }
            catch
            {
                return false;
            }
        }

        private void TaskButton_Click(object sender, RoutedEventArgs e)
        {
            // A press + drag-up that opened (or attempted) the Jump List
            // consumes this release: it must not activate the group. A
            // normal click never sets the flag (see TaskbarWindow.JumpList.cs).
            if (ShouldSuppressClickAfterJumpList())
            {
                e.Handled = true;
                return;
            }

            // v1.21.7: the release that ends a reorder is not a click: moving
            // an icon must not bring the application to the foreground (same
            // rule as the Jump List gesture).
            if (_taskOrderConsumedClick)
            {
                _taskOrderConsumedClick = false;
                e.Handled = true;
                return;
            }

            if (sender is not FrameworkElement element || element.DataContext is not TaskGroup group)
            {
                return;
            }

            // v1.7.1: a click that fails (dead window, gone preview, native
            // error) must not bubble up as an unhandled exception.
            try
            {
                ActivateTaskButton(element, group, e);
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKCLICK", ex);
            }
        }

        private void ActivateTaskButton(FrameworkElement element, TaskGroup group, RoutedEventArgs e)
        {

            // v2.20: gruppo idle (pinnata non avviata) -> avvia l'app reale.
            if (group.Windows.Count == 0)
            {
                if (!string.IsNullOrEmpty(group.LaunchPath))
                {
                    LaunchPathSafe(group.LaunchPath);

                    // v2.27: avvio dalla barra: le finestre che compaiono
                    // nei prossimi secondi appartengono a questo pin.
                    _viewModel.NotePinLaunch(group);
                }
                return;
            }

            if (group.Windows.Count == 1)
            {
                if (_pressedWhileActive)
                {
                    _viewModel.ExecuteWindowCommand(group.Windows[0], WindowCommand.Minimize);
                }
                else
                {
                    AllowForegroundChange();
                    _viewModel.ActivateWindow(group.Windows[0]);
                }
                return;
            }

            if (group.Windows.Count > 1)
            {
                TaskWindow? active = group.Windows.FirstOrDefault(w => w.IsActive);
                if (active != null)
                {
                    int index = group.Windows.IndexOf(active);
                    TaskWindow next = group.Windows[(index + 1) % group.Windows.Count];
                    _viewModel.ActivateWindow(next);
                    return;
                }

                ShowWindowPicker(element, group);
            }
        }

        private static void AllowForegroundChange()
        {
            var inputs = new NativeMethods.INPUT[2];

            inputs[0].type = NativeMethods.INPUT_KEYBOARD;
            inputs[0].u.ki = new NativeMethods.KEYBDINPUT { wVk = 0xE8 };

            inputs[1].type = NativeMethods.INPUT_KEYBOARD;
            inputs[1].u.ki = new NativeMethods.KEYBDINPUT
            {
                wVk = 0xE8,
                dwFlags = NativeMethods.KEYEVENTF_KEYUP
            };

            NativeMethods.SendInput((uint)inputs.Length, inputs,
                                    System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.INPUT>());
        }

        // ---------------------------------------------------------------
        //  v2.44: anteprime live CLICCABILI (Popup, non piu' ToolTip)
        //
        //  Il ToolTip di prima si chiudeva appena il mouse usciva dal
        //  pulsante, anche se stava entrando nell'anteprima: non poteva
        //  quindi ospitare clic. Ora l'anteprima e' un Popup vero e la sua
        //  permanenza e' decisa qui:
        //    - MouseEnter sul pulsante -> timer di comparsa (400 ms, lo
        //      stesso InitialShowDelay che aveva il ToolTip);
        //    - apertura -> timer di controllo: il popup resta aperto
        //      finche' il mouse e' sopra il PULSANTE oppure sopra il POPUP
        //      (sono due finestre diverse: l'IsMouseOver del pulsante non
        //      vede il popup). Un solo MouseLeave, quindi, non puo' mai
        //      chiudere l'anteprima mentre l'utente ci sta entrando.
        //  Dentro l'anteprima: clic sulla miniatura = attiva la finestra,
        //  clic sulla X = chiude DAVVERO la finestra (WM_CLOSE nativo).
        // ---------------------------------------------------------------

        /// <summary>Ritardo di comparsa dell'anteprima, in millisecondi.</summary>
        /* The popup, frame, close button and navigation stay owned here.
         * TaskThumbnail.xaml.cs is deliberately limited to registering,
         * sizing, repositioning and deregistering the live DWM surface. */
        // A static readonly field (not a const) on purpose: a compile-time
        // constant would make the rest of ShowTaskPreview unreachable code.
        // Direct DWM previews are active again. The popup now contains only
        // the live surface, the existing image border and its close button.
        private static readonly bool TaskPreviewsEnabled = true;

        private const int PreviewShowDelayMs = 400;

        /// <summary>Ogni quanto si controlla se il mouse e' ancora sul
        /// pulsante o sull'anteprima.</summary>
        private const int PreviewWatchIntervalMs = 200;

        /// <summary>Distacco fra barra e anteprima (come gli altri riquadri).
        /// Value in 100%-DPI pixels: ApplyPreviewPopupDpiNormalisation keeps
        /// the whole preview at that geometry, so the placement converts it to
        /// DIPs with the same factor the popup is scaled by.</summary>
        private const int PreviewGapPx = 4;

        /// <summary>v1.21.20: di quanto vengono ingrandite le anteprime su un
        /// monitor con scaling sopra il 100%. La geometria resta quella da
        /// 100% (stessa cornice, stesso ritaglio, stesse misure): su un DPI
        /// alto il riquadro viene solo DISEGNATO un 10% piu' grande. A 100% il
        /// fattore non si applica e le anteprime restano 1:1.</summary>
        private const double PreviewHighDpiEnlargement = 1.10;

        /* v1.21.18: fattore di normalizzazione applicato al contenuto del
         * popup anteprime (1 / scala DPI del monitor, v1.21.20: moltiplicato
         * per l'ingrandimento da DPI alto). 1 = nessuna normalizzazione: a
         * 100% il contenuto e' gia' quello disegnato. */
        private double _previewDpiNormalisation = 1.0;
        private double _previewNormalisedDpiScale = 0.0;

        /* v2.46: TimerLease, non DispatcherTimer: si smontano con Dispose()
         * (stop + distacco del gestore) e in chiusura non resta nessun timer
         * in volo con dentro un pezzo di finestra. */
        private TimerLease? _previewShowTimer;
        private TimerLease? _previewWatchTimer;

        /* v2.53: ultimo tooltip di testo mostrato da un pulsante della barra.
         * Serve a chiuderlo quando l'anteprima viva sta per comparire. */
        private ToolTip? _openButtonTip;

        private FrameworkElement? _previewAnchor;
        private TaskGroup? _previewGroup;
        // Explicit item hover ownership keeps the layered popup alive while
        // the pointer is over a DWM destination (which is not WPF-painted).
        private bool _previewPointerInside;

        /* v3.8: stato del riordino delle anteprime col trascinamento
         * sinistro (ispirazione dalla mod "Taskbar Thumbnail Reorder").
         * Il candidato e' l'elemento premuto; il riordino vero parte solo
         * oltre la soglia di trascinamento del sistema, cosi' il clic
         * semplice resta "attiva la finestra". */
        private FrameworkElement? _previewReorderCandidate;
        private Point _previewReorderStartScreen;
        private bool _previewReorderActive;

        /// <summary>
        /// v2.53: apertura del tooltip di testo del pulsante della Superbar.
        /// Si mostra solo quando il riquadro delle anteprime NON e' a schermo
        /// (con l'anteprima aperta il nome non serve: c'e' la miniatura viva).
        /// Non tocca nient'altro del pulsante: click, trascinamento, overflow
        /// e flyout passano di li' come prima.
        /// </summary>
        private void TaskButton_ToolTipOpening(object sender, ToolTipEventArgs e)
        {
            if (TaskPreviewPopup?.IsOpen == true)
            {
                e.Handled = true;
                return;
            }

            // The Jump List gesture never gets a tooltip on top of it (same
            // "one popup at a time" rule as the preview, v2.53).
            if (IsJumpListGestureActive())
            {
                e.Handled = true;
                return;
            }

            // v1.21.7: neither does the icon reorder: while a button is being
            // dragged no tooltip appears over the bar.
            if (IsTaskOrderDragActive())
            {
                e.Handled = true;
                return;
            }

            _openButtonTip = sender as ToolTip;
        }

        private void TaskButton_MouseEnter(object sender, MouseEventArgs e)
        {
            try
            {
                if (sender is not FrameworkElement button ||
                    button.DataContext is not TaskGroup group)
                {
                    return;
                }

                if (group.Windows.Count == 0)
                {
                    /* App non avviata: nessuna anteprima, e NESSUN accento
                     * colorato: in Windows 7 l'hot-track riguardava solo i
                     * programmi APERTI, e un'icona pinnata deve restare
                     * esattamente com'era. Se il mouse arriva da un altro
                     * pulsante, la sua anteprima non serve piu'. */
                    if (!ReferenceEquals(_previewAnchor, button))
                    {
                        CloseTaskPreview();
                    }
                    return;
                }

                /* v1.21.34: Color hot-track, solo sui programmi aperti e con
                 * la sola nuance di colore al 5% (vedi il template: l'opacita'
                 * dell'accento e' animata a 0.05). La luce segue poi il
                 * cursore in TaskButton_MouseMove. */
                if (button is Button taskButton && group.IsRunning)
                {
                    ApplyTaskButtonHotlight(taskButton, group);
                }

                /* v3.8: anteprima GIA' a schermo: il passaggio del mouse su
                 * un altro pulsante cambia il riquadro SUBITO, senza un
                 * secondo ritardo di 400 ms (ispirazione dalla mod
                 * "Instant Taskbar Thumbnail Previews": la Superbar vera non
                 * aspetta tra un'anteprima e l'altra). Sullo STESSO
                 * pulsante il riquadro resta aperto com'e': prima il timer
                 * di comparsa veniva riavviato anche li' e il popup si
                 * chiudeva/riapriva da solo (sfarfallio). Il ritardo di
                 * 400 ms resta per la PRIMA apertura, cosi' il mouse che
                 * attraversa la barra non fa lampeggiare riquadri. */
                if (TaskPreviewPopup?.IsOpen == true)
                {
                    if (ReferenceEquals(_previewAnchor, button))
                    {
                        return;
                    }

                    CloseTaskPreview();
                    _previewAnchor = button;
                    _previewGroup = group;
                    ShowTaskPreview(button, group);
                    return;
                }

                /* Il mouse arriva da un altro pulsante: l'anteprima di quello
                 * non serve piu' (nella Superbar se ne vede una sola per
                 * volta). */
                if (!ReferenceEquals(_previewAnchor, button))
                {
                    CloseTaskPreview();
                }

                _previewAnchor = button;
                _previewGroup = group;

                _previewShowTimer ??= new TimerLease(PreviewShowDelayMs,
                                                     PreviewShowTimer_Tick);
                _previewShowTimer.Stop();
                _previewShowTimer.Start();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"entrata pulsante (anteprima): {ex.Message}");
                CloseTaskPreview();
            }
        }

        private void TaskButton_MouseLeave(object sender, MouseEventArgs e)
        {
            try
            {
                /* Se l'anteprima non e' ancora comparsa, il timer di comparsa
                 * non deve piu' scattare (evita di aprire un riquadro mentre il
                 * mouse e' gia' altrove). Per l'anteprima APERTA decide il timer
                 * di controllo: qui non si chiude nulla, perche' il mouse puo'
                 * essere appena passato dall'altra parte, dentro il popup. */
                if (TaskPreviewPopup?.IsOpen != true)
                {
                    _previewShowTimer?.Stop();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"uscita pulsante (anteprima): {ex.Message}");
            }
        }

        /* ------------------------------------------------------------------ */
        /*  v1.21.34: Color hot-track, 5% accent on open programs              */
        /*                                                                     */
        /*  Microsoft describes the behaviour precisely (Raymond Chen,         */
        /*  official Microsoft blog, 2011-12-06): the hovered taskbar button   */
        /*  "lights up in a color that matches the colors in the icon itself", */
        /*  "the lighting effect is centered on the mouse", and "the code just */
        /*  looks for the predominant color in the icon [...] black, white,    */
        /*  and shades of gray are not considered 'colors'".                   */
        /*                                                                     */
        /*  Scope, after the feedback on the first attempt: the hover look     */
        /*  itself is untouched (the theme's own gravity: tray hover tile +    */
        /*  Aero glow) and the colour is ONLY a 5% accent on top of it,        */
        /*  only on programs that are open. The extraction                */
        /*  (HotlightColor.cs) is cached per icon; the brush is per BUTTON,    */
        /*  because its centre moves with the cursor.                          */
        /* ------------------------------------------------------------------ */

        private sealed class HotlightState
        {
            /// <summary>Pixels the current brush was built from.</summary>
            public ulong IconKey;

            /// <summary>Element the brush was assigned to. A template change
            /// (running -> active -> notification) creates new visual children,
            /// so the brush has to be re-assigned to whatever Hotlight is live
            /// now.</summary>
            public Border? Element;

            public RadialGradientBrush? Brush;
        }

        private readonly System.Runtime.CompilerServices.ConditionalWeakTable<Button, HotlightState> _hotlights = new();

        private void ApplyTaskButtonHotlight(Button button, TaskGroup? group)
        {
            try
            {
                if (button.Template?.FindName("Hotlight", button) is not Border accent)
                {
                    return;
                }

                Color light = HotlightColor.DominantLight(group?.Icon, out ulong key);
                HotlightState state = _hotlights.GetOrCreateValue(button);
                if (state.Brush == null || state.IconKey != key)
                {
                    state.Brush = HotlightColor.CreateLightBrush(light);
                    state.IconKey = key;
                    HotlightColor.ResetLight(state.Brush);
                }

                if (!ReferenceEquals(state.Element, accent))
                {
                    state.Element = accent;
                    accent.Background = state.Brush;
                }
            }
            catch (Exception ex)
            {
                /* The hover has to survive a missing or unreadable icon: the
                 * theme's own glow stays in place. */
                Debug.WriteLine($"alone del pulsante: {ex.Message}");
            }
        }

        private void TaskButton_MouseMove(object sender, MouseEventArgs e)
        {
            if (sender is not Button button ||
                button.Template?.FindName("Hotlight", button) is not Border accent ||
                accent.Background is not RadialGradientBrush brush)
            {
                return;
            }
            if (!_hotlights.TryGetValue(button, out HotlightState? state) ||
                state.Brush == null || !ReferenceEquals(state.Brush, brush))
            {
                return;
            }

            double width = button.ActualWidth;
            double height = button.ActualHeight;
            if (width <= 0 || height <= 0)
            {
                return;
            }

            Point position = e.GetPosition(button);
            double fractionX = position.X / width;
            double fractionY = position.Y / height;

            if (Orientation == Orientation.Vertical)
            {
                /* Vertical bar: the light travels along the bar and keeps a
                 * fixed horizontal position, so it never lands on the text of
                 * the button. */
                HotlightColor.MoveLight(brush, 0.42, 0.10 + (fractionY * 0.80));
            }
            else
            {
                /* Horizontal bar: the light is centered on the cursor,
                 * keeping the Aero bias toward the lower half of the tile. */
                HotlightColor.MoveLight(brush, 0.10 + (fractionX * 0.80),
                                        0.62 + ((fractionY - 0.5) * 0.25));
            }
        }

        private void PreviewShowTimer_Tick(object? sender, EventArgs e)
        {
            try
            {
                _previewShowTimer?.Stop();

                if (_previewAnchor is not FrameworkElement anchor ||
                    _previewGroup is not TaskGroup group)
                {
                    return;
                }

                /* Il mouse puo' essere uscito (o la finestra essersi chiusa)
                 * nei 400 ms di attesa. */
                if (!anchor.IsMouseOver || group.Windows.Count == 0)
                {
                    return;
                }

                ShowTaskPreview(anchor, group);
            }
            catch (Exception ex)
            {
                /* v2.46: un tick andato male non deve restare in sospeso ne'
                 * arrivare al dispatcher: si chiude e si torna a uno stato
                 * pulito. */
                Debug.WriteLine($"comparsa anteprima: {ex.Message}");
                CloseTaskPreview();
            }
        }

        private void ShowTaskPreview(FrameworkElement anchor, TaskGroup group)
        {
            /* v2.56: the only choke point of the preview popup. Returning here
             * leaves every other behaviour (tooltips, click, middle click,
             * jump lists) exactly as it was. */
            if (!TaskPreviewsEnabled)
            {
                return;
            }

            /* The Jump List gesture owns the pointer: a preview popping up
             * while its list is open (or being dragged out) would stack two
             * flyovers over one button - Windows 7 shows one or the other. */
            if (IsJumpListGestureActive())
            {
                return;
            }

            /* v1.21.7: the same holds for the icon reorder: the pointer is
             * moving a button, not looking at one. */
            if (IsTaskOrderDragActive())
            {
                return;
            }

            try
            {
                /* Chiusura e riapertura riposiziona il popup quando cambia
                 * il pulsante. Ogni TaskThumbnail registra il proprio live
                 * thumbnail DWM su Loaded e lo deregistra su Unloaded; la
                 * chiusura azzera l'ItemsSource per garantire il cleanup. */
                TaskPreviewPopup.IsOpen = false;

                /* v2.53: se il tooltip di testo e' a schermo, si toglie prima
                 * di aprire l'anteprima: mai due riquadri insieme. */
                if (_openButtonTip is { IsOpen: true })
                {
                    _openButtonTip.IsOpen = false;
                }

                TaskPreviewItems.ItemsSource = group.Windows;
                TaskPreviewPopup.PlacementTarget = anchor;
                TaskPreviewPopup.Placement = PlacementMode.Custom;
                TaskPreviewPopup.CustomPopupPlacementCallback = PlaceTaskPreview;

                TaskPreviewPopup.IsOpen = true;

                _previewWatchTimer ??= new TimerLease(PreviewWatchIntervalMs,
                                                      PreviewWatchTimer_Tick);
                _previewWatchTimer.Stop();
                _previewWatchTimer.Start();

            }
            catch (Exception ex)
            {
                Debug.WriteLine($"anteprima live: {ex.Message}");
            }
        }

        /// <summary>
        /// <summary>
        /// Collocazione dell'anteprima: centrata sul pulsante e appoggiata
        /// al lato interno della barra (sopra, se la barra e' in basso). Il
        /// callback viene richiamato dal sistema anche quando il popup cambia
        /// dimensione, per esempio quando compare la seconda anteprima: la
        /// centratura resta corretta da sola.
        /// </summary>
        private CustomPopupPlacement[] PlaceTaskPreview(Size popupSize, Size targetSize,
                                                        Point offset)
        {
            try
            {
                return ComputeTaskPreviewPlacement(popupSize, targetSize);
            }
            catch (Exception ex)
            {
                /* v2.46: il callback lo chiama il sistema durante il layout:
                 * un'eccezione qui non e' innocua come altrove. Il ripiego e'
                 * la collocazione di base (angolo in alto a sinistra del
                 * pulsante, sopra la barra), che e' sempre valida. */
                Debug.WriteLine($"posizione anteprima: {ex.Message}");
                return new[] { new CustomPopupPlacement(default, PopupPrimaryAxis.Horizontal) };
            }
        }

        private CustomPopupPlacement[] ComputeTaskPreviewPlacement(Size popupSize,
                                                                   Size targetSize)
        {
            var edge = (TaskbarEdge)GetThumbnailEdge(_previewAnchor ?? (DependencyObject)this);

            /* v1.21.18: PreviewGapPx is a 100%-DPI distance, like every other
             * preview measurement (see ApplyPreviewPopupDpiNormalisation);
             * the offsets below are DIPs of the monitor the popup is on, so
             * the gap is converted with the same factor the popup content is
             * normalised by. At 100% the factor is 1 and nothing changes. */
            double previewScale = _previewNormalisedDpiScale > 0.0
                ? _previewNormalisedDpiScale
                : 1.0;
            double gap = PreviewGapPx / previewScale;

            double dx = (targetSize.Width - popupSize.Width) / 2.0;
            double dy;

            switch (edge)
            {
                case TaskbarEdge.Top:
                    dy = targetSize.Height + gap;
                    break;

                case TaskbarEdge.Left:
                    dx = targetSize.Width + gap;
                    dy = (targetSize.Height - popupSize.Height) / 2.0;
                    break;

                case TaskbarEdge.Right:
                    dx = -popupSize.Width - gap;
                    dy = (targetSize.Height - popupSize.Height) / 2.0;
                    break;

                default:   /* barra in basso: anteprima SOPRA il pulsante */
                    dy = -popupSize.Height - gap;
                    break;
            }

            return new[]
            {
                new CustomPopupPlacement(new Point(dx, dy), PopupPrimaryAxis.Horizontal)
            };
        }

        /// <summary>
        /// A layered popup can contain pixels painted directly by DWM rather
        /// than WPF. Those pixels are visibly under the pointer but do not
        /// always update IsMouseOver, so use the popup HWND bounds as the
        /// authoritative hover test. It remains open only while the cursor is
        /// actually inside the popup (or over its taskbar anchor).
        /// </summary>
        private bool IsPointerInsideTaskPreviewPopup()
        {
            if (TaskPreviewPopup?.IsOpen != true ||
                TaskPreviewPopup.Child is not Visual child ||
                PresentationSource.FromVisual(child) is not HwndSource source ||
                source.Handle == IntPtr.Zero ||
                !NativeMethods.GetWindowRect(source.Handle, out NativeMethods.RECT rect) ||
                !NativeMethods.GetCursorPos(out NativeMethods.POINT cursor))
            {
                return false;
            }

            return cursor.x >= rect.Left && cursor.x < rect.Right &&
                   cursor.y >= rect.Top && cursor.y < rect.Bottom;
        }

        private void PreviewWatchTimer_Tick(object? sender, EventArgs e)
        {
            try
            {
                /* ... e il colore dietro puo' cambiare anche MENTRE il popup
                 * e' aperto: il timer di permanenza ticca gia', agganciarvi
                 * il confronto rende l'adattamento dinamico a costo zero. */
                EnsureDwmAccentFresh();

                /* v1.21.18: the popup can be dragged to a monitor with another
                 * scaling while it is open; the normalisation is re-checked
                 * here because it only costs a DPI read when nothing moved. */
                ApplyPreviewPopupDpiNormalisation(false);

                if (TaskPreviewPopup?.IsOpen != true)
                {
                    _previewWatchTimer?.Stop();
                    return;
                }

                // v3.9: se il gruppo mostrato non esiste piu' (finestra
                // chiusa, gruppo rimosso o svuotato) chiudi subito il popup.
                // Evita riquadri vuoti persistenti quando l'app si chiude
                // con l'anteprima aperta.
                if (_previewGroup == null ||
                    _previewGroup.Windows.Count == 0 ||
                    !_viewModel.Groups.Contains(_previewGroup))
                {
                    CloseTaskPreview();
                    return;
                }

                bool overAnchor = _previewAnchor?.IsMouseOver == true;
                bool overPopup = _previewPointerInside ||
                    (TaskPreviewPopup.Child is FrameworkElement child && child.IsMouseOver) ||
                    IsPointerInsideTaskPreviewPopup();

                if (!overAnchor && !overPopup)
                {
                    CloseTaskPreview();
                }
            }
            catch (Exception ex)
            {
                /* Un tick andato male non deve lasciare il riquadro aperto
                 * per sempre ne' far esplodere il dispatcher. */
                Debug.WriteLine($"controllo anteprima: {ex.Message}");
                CloseTaskPreview();
            }
        }

        /// <summary>RAII owner for the HBITMAP returned by Bitmap.GetHbitmap.
        /// BitmapSource copies the pixels, so ownership ends immediately after
        /// conversion rather than surviving for the popup lifetime.</summary>
        private sealed class SafePreviewHBitmap
            : Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid
        {
            internal SafePreviewHBitmap(IntPtr handle) : base(true)
            {
                SetHandle(handle);
            }

            protected override bool ReleaseHandle()
                => NativeMethods.DeleteObject(handle);
        }

        private void CaptureTaskPreviewBackdrop()
        {
            try
            {
                TaskPreviewBlurImage.Source = null;
                TaskPreviewBlurHost.Clip = null;
                TaskPreviewPopupRoot.UpdateLayout();

                if (!TaskPreviewPopupRoot.IsLoaded ||
                    TaskPreviewPopupRoot.ActualWidth <= 0 ||
                    TaskPreviewPopupRoot.ActualHeight <= 0 ||
                    PresentationSource.FromVisual(TaskPreviewPopupRoot)
                        is not HwndSource popupSource)
                {
                    return;
                }

                /* v1.21.18: the capture covers the popup's real client area,
                 * asked to Windows in device pixels, instead of multiplying
                 * the WPF size by the monitor scale. The two agree at 100%;
                 * with the preview geometry normalisation (see
                 * ApplyPreviewPopupDpiNormalisation) the WPF size is no longer
                 * the physical one, and the capture must follow the window,
                 * not the layout. */
                Point screenOrigin = TaskPreviewPopupRoot.PointToScreen(new Point(0, 0));
                int pixelWidth = 1;
                int pixelHeight = 1;
                if (NativeMethods.GetClientRect(popupSource.Handle, out NativeMethods.RECT client))
                {
                    pixelWidth = Math.Max(1, client.Right - client.Left);
                    pixelHeight = Math.Max(1, client.Bottom - client.Top);
                }

                // Bitmap and Graphics are IDisposable; GetHbitmap has separate
                // ownership and is wrapped immediately in SafePreviewHBitmap.
                // Opened runs before the first useful popup frame is painted,
                // so this single capture represents the desktop behind it.
                using var capture = new System.Drawing.Bitmap(
                    pixelWidth, pixelHeight,
                    System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
                using (System.Drawing.Graphics graphics =
                       System.Drawing.Graphics.FromImage(capture))
                {
                    graphics.CopyFromScreen(
                        (int)Math.Round(screenOrigin.X),
                        (int)Math.Round(screenOrigin.Y),
                        0, 0, new System.Drawing.Size(pixelWidth, pixelHeight),
                        System.Drawing.CopyPixelOperation.SourceCopy);
                }

                using var hBitmap = new SafePreviewHBitmap(capture.GetHbitmap());
                var source = Imaging.CreateBitmapSourceFromHBitmap(
                    hBitmap.DangerousGetHandle(), IntPtr.Zero, Int32Rect.Empty,
                    System.Windows.Media.Imaging.BitmapSizeOptions.FromEmptyOptions());
                if (source.CanFreeze)
                {
                    source.Freeze();
                }

                TaskPreviewBlurImage.Source = source;
                ClipTaskPreviewBackdropToFrames();
            }
            catch (Exception ex)
            {
                // Capture is cosmetic: keep the accepted accent frame and
                // never prevent DWM thumbnail registration or popup opening.
                TaskPreviewBlurImage.Source = null;
                TaskPreviewBlurHost.Clip = null;
                try { _bridge.Log($"preview static blur: {ex.Message}"); }
                catch { }
            }
        }

        private void ClipTaskPreviewBackdropToFrames()
        {
            var frameBands = new GeometryGroup();
            int count = TaskPreviewItems.Items.Count;
            for (int index = 0; index < count; index++)
            {
                if (TaskPreviewItems.ItemContainerGenerator.ContainerFromIndex(index)
                    is not FrameworkElement item ||
                    item.ActualWidth <= 34 || item.ActualHeight <= 57)
                {
                    continue;
                }

                Rect bounds = item.TransformToAncestor(TaskPreviewPopupRoot)
                                  .TransformBounds(new Rect(
                                      0, 0, item.ActualWidth, item.ActualHeight));
                double middleHeight = Math.Max(0, bounds.Height - 57);

                // Exact DWMBorder.png slices: top 38, sides 17, bottom 19.
                // Four bands leave the complete central DWM cell unpainted.
                frameBands.Children.Add(new RectangleGeometry(
                    new Rect(bounds.X, bounds.Y, bounds.Width, 38)));
                frameBands.Children.Add(new RectangleGeometry(
                    new Rect(bounds.X, bounds.Y + 38, 17, middleHeight)));
                frameBands.Children.Add(new RectangleGeometry(
                    new Rect(bounds.Right - 17, bounds.Y + 38, 17, middleHeight)));
                frameBands.Children.Add(new RectangleGeometry(
                    new Rect(bounds.X, bounds.Bottom - 19, bounds.Width, 19)));
            }

            TaskPreviewBlurHost.Clip = frameBands;
        }

        /* ------------------------------------------------------------------ */
        /*  Preview frame drawn by the core's native 9-slice renderer           */
        /* ------------------------------------------------------------------ */

        /// <summary>Template parts of TaskPreviewFrameVista: the rectangle the
        /// core's renderer paints into, and the eight-rectangle XAML accent
        /// layer that paint replaces while it is in use.</summary>
        private const string NativeFrameRectPart = "NativeAeroFrameRect";
        private const string AccentSliceLayerPart = "AccentSliceLayer";

        private void PreviewFrameHost_Loaded(object sender, RoutedEventArgs e)
            => ApplyNativePreviewFrame(sender as ContentControl);

        private void PreviewFrameHost_SizeChanged(object sender, SizeChangedEventArgs e)
            => ApplyNativePreviewFrame(sender as ContentControl);

        /// <summary>
        /// Offers one preview frame the chance to draw its border with the
        /// core's native 9-slice renderer (native/src/AeroThumbnailFrame.cpp,
        /// reached through W7T_RenderAeroThumbnailFrame and cached by
        /// Utilities/NativePreviewFrame).
        ///
        /// The swap is limited to the accent layer: the grayscale overlay, the
        /// clipped static blur, the title band, the close button and the live
        /// DWM thumbnail are not touched. When the core cannot supply the
        /// frame - not initialized, slice PNGs missing next to the executable,
        /// size below the border sum, translucent accent brush - the eight
        /// masked rectangles are put back and the border is drawn exactly as
        /// it was before this path existed. Both states are re-checked on
        /// every call, so a frame that loses its native render recovers by
        /// itself on the next size change.
        ///
        /// Called from the frame's Loaded and SizeChanged, so it runs while
        /// the popup is being laid out; every path through it is either a
        /// property assignment or an early return, and the whole body is
        /// guarded because a cosmetic border must never reach the caller.
        /// </summary>
        private void ApplyNativePreviewFrame(ContentControl? frameHost)
        {
            try
            {
                if (frameHost?.Template == null)
                {
                    return;
                }

                if (frameHost.Template.FindName(NativeFrameRectPart, frameHost)
                    is not System.Windows.Shapes.Rectangle frameRect)
                {
                    /* A theme that does not expose the part: nothing to swap,
                     * and its own frame stays exactly as it is. */
                    return;
                }

                var accentLayer = frameHost.Template.FindName(
                    AccentSliceLayerPart, frameHost) as UIElement;

                var rendered = NativePreviewFrame.TryRender(frameHost);
                if (rendered != null)
                {
                    /* One frozen brush per rendered frame: the rectangle is
                     * repainted on every layout pass and must not rebuild its
                     * brush then. Fill maps the bitmap - rendered at this
                     * element's own device size and DPI - onto the frame 1:1,
                     * so the corners are not resampled. The reference check
                     * works because the cache hands back the same frozen
                     * bitmap until the size or the accent changes. */
                    if (!ReferenceEquals((frameRect.Fill as ImageBrush)?.ImageSource,
                                         rendered))
                    {
                        var brush = new ImageBrush(rendered)
                        {
                            Stretch = System.Windows.Media.Stretch.Fill
                        };
                        brush.Freeze();
                        frameRect.Fill = brush;
                    }

                    frameRect.Visibility = Visibility.Visible;
                    if (accentLayer != null)
                    {
                        accentLayer.Visibility = Visibility.Collapsed;
                    }
                    return;
                }

                /* No native frame: the XAML accent layer draws the border. */
                frameRect.Fill = null;
                frameRect.Visibility = Visibility.Collapsed;
                if (accentLayer != null)
                {
                    accentLayer.Visibility = Visibility.Visible;
                }
            }
            catch (Exception ex)
            {
                /* Cosmetic path: the XAML frame is the fallback and stays up,
                 * so this is logged and swallowed like the other preview
                 * helpers around it. */
                Debug.WriteLine($"native preview frame: {ex.Message}");
            }
        }

        /// <summary>
        /// v1.21.18: the preview popup keeps its 100%-DPI pixel geometry at
        /// every display scaling.
        ///
        /// The preview is a bitmap-designed surface: the 9-slice DWMBorder.png
        /// frame has 17/38/19-pixel slices, the aperture is 202x109 and the
        /// live DWM surface is stretched into exactly that aperture. Those
        /// numbers are whole pixels at 100%; at 125% the frame is 166 DIPs =
        /// 207.5 device pixels tall and the aperture 136.25, so something has
        /// to be rounded - which is how the frame stops being 1:1 with its own
        /// slices and the live surface ends up a fraction of a pixel off the
        /// opening it belongs to. Scaling the popup content by the inverse of
        /// the monitor scale puts all of those numbers back on whole pixels:
        /// one preview pixel stays one screen pixel, exactly as at 100%, and
        /// the frame, the aperture and the DWM rectangle agree by
        /// construction. The popup itself is still positioned in DIPs, so it
        /// keeps hanging on its own task button.
        ///
        /// At 100% this is a no-op (no transform). Every failure leaves the
        /// normal, monitor-scaled layout in place: the preview keeps working,
        /// it just keeps the old rounding.
        ///
        /// v1.21.20: on a display scaled above 100% the normalised surface is
        /// drawn 10% larger (PreviewHighDpiEnlargement). The geometry is still
        /// the 100%-pixel one; only its size on screen changes, so every
        /// measurement taken from the screen follows automatically.
        /// </summary>
        private void ApplyPreviewPopupDpiNormalisation(bool force)
        {
            try
            {
                if (TaskPreviewPopupRoot == null)
                {
                    return;
                }

                /* The popup's own window transform is authoritative for the
                 * monitor the popup is on; GetDpi on the visual is the
                 * fallback for the moment the popup is created but not yet
                 * connected to its HwndSource. */
                double scale = 0.0;
                if (PresentationSource.FromVisual(TaskPreviewPopupRoot)
                        is HwndSource popupSource &&
                    popupSource.CompositionTarget is { } popupTarget)
                {
                    scale = popupTarget.TransformToDevice.M11;
                }

                if (scale <= 0.0)
                {
                    var dpi = VisualTreeHelper.GetDpi(TaskPreviewPopupRoot);
                    scale = dpi.DpiScaleX;
                }

                if (scale <= 0.0)
                {
                    return;
                }

                if (!force && Math.Abs(scale - _previewNormalisedDpiScale) < 0.001)
                {
                    return;
                }

                _previewNormalisedDpiScale = scale;

                if (Math.Abs(scale - 1.0) < 0.001)
                {
                    TaskPreviewPopupRoot.LayoutTransform = null;
                    _previewDpiNormalisation = 1.0;
                    return;
                }

                /* v1.21.20: sopra il 100% il contenuto normalizzato viene
                 * ingrandito del 10% (un pixel della geometria da 100% vale
                 * 1,1 pixel sullo schermo). La logica resta identica - stesse
                 * cornici, stesso ritaglio, stesse misure - e chi misura lo
                 * schermo (PointToScreen) segue da solo la nuova dimensione. */
                double enlargement = scale > 1.001
                    ? PreviewHighDpiEnlargement
                    : 1.0;
                double inverse = enlargement / scale;
                TaskPreviewPopupRoot.LayoutTransform =
                    new ScaleTransform(inverse, inverse);
                _previewDpiNormalisation = inverse;

                /* Re-layout before anything measures the popup, then rebuild
                 * the native frames: they are cached by rendered size, which
                 * just changed. */
                TaskPreviewPopupRoot.UpdateLayout();
                RefreshNativePreviewFrames();
            }
            catch (Exception ex)
            {
                /* Back to "nothing applied": the next call tries again instead
                 * of believing it already succeeded, and the placement keeps
                 * the un-normalised gap. */
                _previewNormalisedDpiScale = 0.0;
                _previewDpiNormalisation = 1.0;
                Debug.WriteLine($"preview dpi normalisation: {ex.Message}");
            }
        }

        /// <summary>One diagnostic line per opening: a report about the
        /// previews on a 125% display can then be checked against what the app
        /// actually applied.</summary>
        private void LogPreviewDpiNormalisation()
        {
            try
            {
                if (Math.Abs(_previewNormalisedDpiScale - 1.0) < 0.001)
                {
                    _bridge.Log("preview popup: DPI 100%, geometry unchanged");
                    return;
                }

                double enlargement = _previewNormalisedDpiScale > 1.001
                    ? PreviewHighDpiEnlargement
                    : 1.0;
                _bridge.Log(
                    $"preview popup: monitor DPI {(_previewNormalisedDpiScale * 100):0}%, " +
                    $"geometry normalised by {_previewDpiNormalisation:0.###}, " +
                    $"previews drawn {enlargement:0.###}x the 100% pixel size");
            }
            catch
            {
                /* Diagnostics only. */
            }
        }

        /// <summary>
        /// Re-applies the native frame to every preview of the open popup
        /// after the DWM colorization colour changed: the cached bitmaps were
        /// tinted with the previous accent, while the XAML layer re-evaluates
        /// its DynamicResource brush on its own.
        /// </summary>
        private void RefreshNativePreviewFrames()
        {
            try
            {
                NativePreviewFrame.Invalidate();

                if (TaskPreviewPopup is not { IsOpen: true } || TaskPreviewItems == null)
                {
                    return;
                }

                for (int index = 0; index < TaskPreviewItems.Items.Count; index++)
                {
                    if (TaskPreviewItems.ItemContainerGenerator
                            .ContainerFromIndex(index) is FrameworkElement container)
                    {
                        ApplyNativePreviewFrame(FindPreviewFrameHost(container, 0));
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"preview frame refresh: {ex.Message}");
            }
        }

        /// <summary>The frame ContentControl of one preview item: the first
        /// ContentControl in the item's visual tree, a couple of levels below
        /// the container. Depth-capped because the popup tree is shallow and a
        /// cosmetic lookup must never walk far.</summary>
        private static ContentControl? FindPreviewFrameHost(DependencyObject parent, int depth)
        {
            if (depth > 6)
            {
                return null;
            }

            int count = VisualTreeHelper.GetChildrenCount(parent);
            for (int i = 0; i < count; i++)
            {
                DependencyObject child = VisualTreeHelper.GetChild(parent, i);
                if (child is ContentControl contentControl)
                {
                    return contentControl;
                }

                ContentControl? found = FindPreviewFrameHost(child, depth + 1);
                if (found != null)
                {
                    return found;
                }
            }

            return null;
        }

        private void TaskPreviewPopup_Opened(object? sender, EventArgs e)
        {
            try
            {
                /* Il colore dietro puo' essere cambiato mentre il popup era
                 * chiuso senza alzare alcun messaggio: l'apertura e' il
                 * momento in cui la tinta torna visibile, quindi si verifica
                 * adesso. */
                EnsureDwmAccentFresh();

                /* v1.21.18: normalise the geometry BEFORE the first frame is
                 * painted and before the backdrop is captured, so the capture
                 * has the size the popup really occupies. */
                ApplyPreviewPopupDpiNormalisation(true);
                LogPreviewDpiNormalisation();

                CaptureTaskPreviewBackdrop();

                /* v2.45: punto unico di controllo dopo che il popup e' davvero a
                 * schermo. Lo sfondo statico e' gia' stato catturato e limitato
                 * alle sole bande esterne; ora si riavvia il timer che decide
                 * la chiusura, cosi' la permanenza non
                 * dipende dall'ordine con cui WPF alza Opened rispetto al
                 * codice chiamante. */
                _previewWatchTimer ??= new TimerLease(PreviewWatchIntervalMs,
                                                      PreviewWatchTimer_Tick);
                _previewWatchTimer.Stop();
                _previewWatchTimer.Start();
            }
            catch (Exception ex)
            {
                /* v2.46: se il riquadro e' a schermo ma il controllo di
                 * permanenza non parte, e' meglio chiuderlo subito che
                 * lasciarlo li' senza nessuno che lo governi. */
                Debug.WriteLine($"apertura anteprima: {ex.Message}");
                CloseTaskPreview();
            }
        }

        private void TaskPreviewPopup_Closed(object? sender, EventArgs e)
        {
            try
            {
                _previewShowTimer?.Stop();
                _previewWatchTimer?.Stop();
                ClearPreviewReorderState();

                // Release the managed BitmapSource reference after every
                // short-lived hover popup; the HBITMAP was already released
                // by SafePreviewHBitmap immediately after conversion.
                TaskPreviewBlurImage.Source = null;
                TaskPreviewBlurHost.Clip = null;

                _previewAnchor = null;
                _previewGroup = null;
                _previewPointerInside = false;
                _openButtonTip = null;

                /* Sgancia i controlli TaskThumbnail, che deregistrano sempre
                 * il proprio handle DWM durante Unloaded. */
                if (TaskPreviewItems != null)
                {
                    TaskPreviewItems.ItemsSource = null;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"chiusura anteprima: {ex.Message}");
            }
        }

        private void CloseTaskPreview()
        {
            try
            {
                _previewShowTimer?.Stop();
                _previewWatchTimer?.Stop();

                if (TaskPreviewPopup?.IsOpen == true)
                {
                    TaskPreviewPopup.IsOpen = false;   /* -> TaskPreviewPopup_Closed */
                }
                else
                {
                    _previewAnchor = null;
                    _previewGroup = null;
                }
            }
            catch (Exception ex)
            {
                /* La chiusura non deve mai propagare: e' chiamata da timer,
                 * eventi mouse e dal percorso di attivazione delle finestre. */
                Debug.WriteLine($"chiusura anteprima (fallback): {ex.Message}");
                _previewShowTimer?.Stop();
                _previewWatchTimer?.Stop();
                _previewAnchor = null;
                _previewGroup = null;
            }
        }

        private void PreviewThumbnail_MouseEnter(object sender, MouseEventArgs e)
        {
            _previewPointerInside = true;
        }

        private void PreviewThumbnail_MouseLeave(object sender, MouseEventArgs e)
        {
            _previewPointerInside = false;
        }

        /// <summary>
        /// Clic sulla miniatura: la finestra va DAVVERO in primo piano, come
        /// nella Superbar di Windows 7 (che, a differenza di Windows 10/11,
        /// non ha il pulsante "anteprima" separato: si clicca la miniatura).
        /// Dopo un trascinamento di riordino (v3.8) NON attiva nulla: il
        /// rilascio conclude il riordino, punto.
        /// </summary>
        private void PreviewThumbnail_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            bool wasReorder = _previewReorderActive;
            ClearPreviewReorderState();
            if (wasReorder)
            {
                e.Handled = true;
                return;
            }

            try
            {
                if (sender is not FrameworkElement element ||
                    element.DataContext is not TaskWindow window)
                {
                    return;
                }

                /* v3.8: rilascio FUORI dalla miniatura (il mouse era stato
                 * catturato da una pressione poi non diventata riordino):
                 * non conta come clic. Senza la cattura questo Up non
                 * sarebbe nemmeno arrivato qui. */
                Point releasePoint = e.GetPosition(element);
                if (releasePoint.X < 0 || releasePoint.Y < 0 ||
                    releasePoint.X > element.ActualWidth ||
                    releasePoint.Y > element.ActualHeight)
                {
                    e.Handled = true;
                    return;
                }

                e.Handled = true;

                /* La barra e' WS_EX_NOACTIVATE: senza lo sblocco del foreground
                 * il sistema rifiuterebbe l'attivazione (stessa tecnica usata
                 * per la rotellina del mouse sui pulsanti). */
                AllowForegroundChange();
                _viewModel.ActivateWindow(window);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"attivazione da anteprima: {ex.Message}");
            }
            finally
            {
                // Comunque vada, il riquadro si chiude: l'utente ha scelto.
                CloseTaskPreview();
            }
        }

        /// <summary>
        /// v3.8: pressione sulla miniatura: comincia il POSSIBILE
        /// trascinamento di riordino. Il mouse viene catturato cosi' i
        /// movimenti arrivano tutti qui anche se il cursore scavalca le
        /// altre anteprime; se il rilascio avviene senza superare la soglia
        /// di trascinamento, il percorso resta il normale clic che attiva
        /// la finestra.
        /// </summary>
        private void PreviewThumbnail_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            try
            {
                if (sender is not FrameworkElement element ||
                    element.DataContext is not TaskWindow ||
                    _previewGroup is not { Windows.Count: > 1 })
                {
                    return;   /* finestra sola nel gruppo: niente da riordinare */
                }

                _previewReorderCandidate = element;
                _previewReorderStartScreen = element.PointToScreen(e.GetPosition(element));
                _previewReorderActive = false;
                element.CaptureMouse();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"inizio riordino anteprime: {ex.Message}");
                ClearPreviewReorderState();
            }
        }

        /// <summary>
        /// v3.8: movimento sulla miniatura: oltre la soglia di
        /// trascinamento si entra in modalita' riordino e la posizione del
        /// cursore decide dove spostare l'anteprima trascinata nella fila.
        /// </summary>
        private void PreviewThumbnail_MouseMove(object sender, MouseEventArgs e)
        {
            try
            {
                if (_previewReorderCandidate == null)
                {
                    return;
                }

                if (e.LeftButton != MouseButtonState.Pressed)
                {
                    ClearPreviewReorderState();
                    return;
                }

                Point screenNow = _previewReorderCandidate.PointToScreen(
                    e.GetPosition(_previewReorderCandidate));

                if (!_previewReorderActive)
                {
                    double dx = screenNow.X - _previewReorderStartScreen.X;
                    double dy = screenNow.Y - _previewReorderStartScreen.Y;
                    double threshold = SystemParameters.MinimumHorizontalDragDistance;
                    if (dx * dx + dy * dy < threshold * threshold)
                    {
                        return;
                    }
                    _previewReorderActive = true;
                }

                ReorderPreviewsAtCursor(screenNow);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"riordino anteprime: {ex.Message}");
                ClearPreviewReorderState();
            }
        }

        /// <summary>
        /// v3.8: sposta l'anteprima trascinata nella collezione del gruppo.
        /// L'ordine nuovo resta: la sincronizzazione e' per differenze
        /// (aggiunge in coda, aggiorna sul posto, toglie le chiuse), quindi
        /// non lo calpesta. Le anteprime sono in fila orizzontale: il punto
        /// di inserimento e' la prima anteprima il cui centro sta a destra
        /// del cursore.
        /// </summary>
        private void ReorderPreviewsAtCursor(Point screenPoint)
        {
            if (_previewGroup is not { Windows.Count: > 1 } group ||
                _previewReorderCandidate?.DataContext is not TaskWindow dragged ||
                TaskPreviewItems == null)
            {
                return;
            }

            int from = group.Windows.IndexOf(dragged);
            if (from < 0)
            {
                ClearPreviewReorderState();
                return;
            }

            int insertAt = group.Windows.Count;   /* dopo l'ultima */
            for (int i = 0; i < group.Windows.Count; ++i)
            {
                if (ReferenceEquals(group.Windows[i], dragged))
                {
                    continue;
                }
                if (TaskPreviewItems.ItemContainerGenerator
                        .ContainerFromItem(group.Windows[i])
                        is not FrameworkElement container)
                {
                    continue;
                }
                try
                {
                    Point center = container.PointToScreen(new Point(
                        container.ActualWidth / 2.0,
                        container.ActualHeight / 2.0));
                    if (screenPoint.X < center.X)
                    {
                        insertAt = i;
                        break;
                    }
                }
                catch
                {
                    /* contenitore in transizione: si salta */
                }
            }

            /* ObservableCollection.Move vuole l'indice di destinazione nel
             * listino DOPO la rimozione dell'elemento trascinato. */
            int to = insertAt > from ? insertAt - 1 : insertAt;
            if (to != from)
            {
                group.Windows.Move(from, to);
            }
        }

        /// <summary>Rilascia la cattura del mouse e dimentica il riordino.</summary>
        private void ClearPreviewReorderState()
        {
            if (_previewReorderCandidate != null)
            {
                if (_previewReorderCandidate.IsMouseCaptured)
                {
                    _previewReorderCandidate.ReleaseMouseCapture();
                }
                _previewReorderCandidate = null;
            }
            _previewReorderActive = false;
        }

        /// <summary>
        /// <summary>
        /// v1.21.32: vertical position of the preview close X.
        ///
        /// The top margin is no longer a magic number hard-coded in the
        /// template: once the preview is laid out, the X copies the height
        /// of the title label (PreviewTitleText) and centres itself on it,
        /// so it stays aligned with the text at any band height, DPI or
        /// skin.
        ///
        /// The historical value (14) stays as the FALLBACK: when the text
        /// is not laid out yet, when another skin supplies the template or
        /// when the measurement fails, the X returns exactly where it was.
        /// </summary>
        private const double PreviewCloseTopFallback = 14d;
        private const double PreviewCloseHeightFallback = 20d;
        private const string PreviewTitleTextName = "PreviewTitleText";

        private void PreviewCloseButton_Loaded(object sender, RoutedEventArgs e)
        {
            AlignPreviewCloseButton(sender as Button);
        }

        private void PreviewCloseButton_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            /* The first layout pass runs after Loaded: this re-aligns the X
             * as soon as its real size is known. */
            if (e.HeightChanged)
            {
                AlignPreviewCloseButton(sender as Button);
            }
        }

        private void AlignPreviewCloseButton(Button? button)
        {
            if (button == null)
            {
                return;
            }

            double top = PreviewCloseTopFallback;
            try
            {
                if (VisualTreeHelper.GetParent(button) is FrameworkElement root &&
                    root.FindName(PreviewTitleTextName) is TextBlock title)
                {
                    double titleHeight = title.ActualHeight;
                    if (!double.IsNaN(titleHeight) && titleHeight > 0)
                    {
                        /* Vertical centre of the text, in the coordinates of
                         * the template root (the grid that also holds the
                         * button). */
                        Point origin = title.TransformToAncestor(root)
                                            .Transform(new Point(0, 0));
                        double titleCenter = origin.Y + (titleHeight / 2d);

                        double buttonHeight = button.ActualHeight;
                        if (double.IsNaN(buttonHeight) || buttonHeight <= 0)
                        {
                            buttonHeight = PreviewCloseHeightFallback;
                        }

                        double candidate = titleCenter - (buttonHeight / 2d);
                        if (!double.IsNaN(candidate) && !double.IsInfinity(candidate))
                        {
                            /* Never outside the title band (38 px, the same
                             * height the native core reserves on top). */
                            top = Math.Max(0d, Math.Min(candidate, 38d - buttonHeight));
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                /* No exception may stop the preview from opening: the
                 * fallback value keeps the X where it has always been. */
                Debug.WriteLine($"preview close X alignment: {ex.Message}");
                top = PreviewCloseTopFallback;
            }

            var margin = button.Margin;
            if (Math.Abs(margin.Top - top) > 0.1d)
            {
                button.Margin = new Thickness(margin.Left, top, margin.Right, margin.Bottom);
            }
        }

        /// v2.44: la X dell'anteprima chiude DAVVERO la finestra. Usa lo
        /// stesso comando della voce "Chiudi" della jump list (WM_CLOSE
        /// inviato alla finestra dal core nativo), quindi funziona anche con
        /// le finestre che chiedono conferma di chiusura: la domanda la fa
        /// l'applicazione, non noi. Se era l'ultima finestra del gruppo
        /// l'anteprima non ha piu' nulla da mostrare e si chiude.
        ///
        /// v2.48: la X e' di nuovo quella del template dell'anteprima, sopra
        /// l'immagine e sempre visibile (la finestra sovrapposta e' stata
        /// tolta: due meccanismi per la stessa X si pestavano i piedi).
        /// </summary>
        private void PreviewCloseButton_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (sender is not FrameworkElement element ||
                    element.DataContext is not TaskWindow window)
                {
                    return;
                }

                e.Handled = true;

                /* La finestra vive in un altro processo: se nel frattempo e'
                 * sparita, ExecuteWindowCommand ritorna un errore che qui non
                 * interessa (il modello si riallinea al giro successivo). */
                _viewModel.ExecuteWindowCommand(window, WindowCommand.Close);

                if (_previewGroup == null || _previewGroup.Windows.Count == 0)
                {
                    CloseTaskPreview();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"chiusura da anteprima: {ex.Message}");
                CloseTaskPreview();
            }
        }

        private void TaskButton_MouseWheel(object sender, MouseWheelEventArgs e)
        {
            if (sender is not FrameworkElement { DataContext: TaskGroup group })
            {
                return;
            }

            if (group.Windows.Count == 0 ||
                (Keyboard.Modifiers & ModifierKeys.Shift) != 0)
            {
                return;
            }

            TaskWindow target = group.Windows.FirstOrDefault(w => w.IsActive)
                                ?? group.Windows[0];

            if (e.Delta > 0)
            {
                AllowForegroundChange();
                _viewModel.ActivateWindow(target);
            }
            else
            {
                _viewModel.ExecuteWindowCommand(target, WindowCommand.Minimize);
            }

            e.Handled = true;
        }

        private void TaskButton_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Middle)
            {
                return;
            }

            if (sender is not FrameworkElement element || element.DataContext is not TaskGroup group)
            {
                return;
            }

            e.Handled = true;

            if (string.IsNullOrEmpty(group.ExePath))
            {
                return;
            }

            LaunchPathSafe(group.ExePath);
        }

        private void ShowWindowPicker(FrameworkElement placementTarget, TaskGroup group)
        {
            var listBox = new ListBox
            {
                ItemsSource = group.Windows,
                ItemTemplate = TryFindResource("WindowPickerItemTemplate") as DataTemplate,
                BorderThickness = new Thickness(0),
                Background = Brushes.Transparent,
                MaxHeight = 400
            };

            var popup = new Popup
            {
                PlacementTarget = placementTarget,
                Placement = PlacementMode.Top,
                StaysOpen = false,
                AllowsTransparency = true,
                PopupAnimation = PopupAnimation.Fade,
                Child = new Border
                {
                    Background = new LinearGradientBrush(
                        Color.FromArgb(0xF3, 0xFF, 0xFF, 0xFF),
                        Color.FromArgb(0xED, 0xF4, 0xF4, 0xF4),
                        90),
                    BorderBrush = new SolidColorBrush(Color.FromRgb(0x86, 0x86, 0x86)),
                    BorderThickness = new Thickness(1),
                    CornerRadius = new CornerRadius(3),
                    Padding = new Thickness(4),
                    Child = listBox
                }
            };

            listBox.SelectionChanged += (_, args) =>
            {
                if (args.AddedItems.Count > 0 && args.AddedItems[0] is TaskWindow window)
                {
                    _viewModel.ActivateWindow(window);
                    popup.IsOpen = false;
                }
            };

            popup.IsOpen = true;
        }

        // v2.40: il clic DESTRO ripristina il menu contestuale di Windows 7
        // (sistema per una finestra, gruppo per piu' finestre, avvio/rimozione
        // per un pin idle). La Jump List si apre invece col TRASCINAMENTO
        // verso l'alto del clic SINISTRO (sistema in TaskbarWindow.JumpList.cs),
        // come fa la Superbar originale: cosi' il destro non perde mai "Chiudi" & co.
        // Il menu del destro NON guadagna nessuna voce di Jump List: le due
        // superfici restano separate esattamente come in Windows 7.
        private void TaskButton_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            if (sender is not FrameworkElement element || element.DataContext is not TaskGroup group)
            {
                return;
            }

            e.Handled = true;

            // v1.7.1: a context-menu failure must never become an unhandled
            // exception (the native side already falls back to a standard
            // menu for stub system menus, e.g. UWP frame windows).
            try
            {
                OpenTaskButtonMenu(element, group);
            }
            catch (Exception ex)
            {
                DiagnosticLogger.WriteException("TASKMENU", ex);
            }
        }

        private void OpenTaskButtonMenu(FrameworkElement element, TaskGroup group)
        {

            Point origin;
            try { origin = element.PointToScreen(new Point(0, element.ActualHeight)); }
            catch { origin = element.PointToScreen(new Point(0, 0)); }
            int x = (int)Math.Round(origin.X);
            int y = (int)Math.Round(origin.Y);

            if (group.Windows.Count == 0)
            {
                // Idle pin: launch / pin-unpin. Dedicated text-only menu
                // (not the generic menu: that one is shared with the bar,
                // the clock and the tray).
                string pinText = group.IsPinned
                    ? L("lang_menu_unpin",
                        "Unpin this program from taskbar")
                    : L("lang_menu_pin",
                        "Pin this program to taskbar");
                string launchText = L("lang_start_context",
                    L("lang_start_tip", "Start"));
                int choice;
                try
                {
                    choice = _bridge.ShowPinMenu(
                        x, y, launchText, pinText,
                        group.LaunchPath ?? string.Empty,
                        group.ExePath ?? string.Empty,
                        bottomEdge: true);
                }
                catch (EntryPointNotFoundException)
                {
                    // Native DLL older than the managed side: same two
                    // rows through the generic menu.
                    choice = _bridge.ShowContextMenu(
                        x, y, bottomEdge: true, launchText, pinText);
                }
                switch (choice)
                {
                    case 1:
                        LaunchPathSafe(!string.IsNullOrEmpty(group.LaunchPath)
                            ? group.LaunchPath : (group.ExePath ?? string.Empty));
                        _viewModel.NotePinLaunch(group);
                        break;
                    case 2:
                        ToggleTaskPin(group);
                        break;
                }
            }
            else if (group.Windows.Count == 1)
            {
                // Una finestra: menu di sistema REALE (Ripristina/Sposta/.../Chiudi).
                _bridge.ShowWindowSystemMenu(group.Windows[0].Hwnd, x, y, bottomEdge: true);
            }
            else
            {
                // Piu' finestre: menu di gruppo (riduci a icona / chiudi tutte).
                int r = _bridge.ShowGroupMenu(group.Windows[0].Hwnd, x, y,
                    L("lang_minimize_group", "Minimize group"),
                    L("lang_close_group", "Close group"), bottomEdge: true);
                if (r == 1) _viewModel.MinimizeGroup(group);
                else if (r == 2) _viewModel.CloseGroup(group);
            }
        }

        // v2.40: fissa/rimuovi un'app dalla barra scrivendo/cancellando il .lnk
        // nella cartella reale dei pin; il watcher nativo aggiorna il modello.
        private void ToggleTaskPin(TaskGroup group)
        {
            try
            {
                if (group.IsPinned)
                {
                    if (!string.IsNullOrEmpty(group.LaunchPath))
                        System.IO.File.Delete(group.LaunchPath);
                }
                else
                {
                    string exe = group.ExePath ?? string.Empty;
                    if (string.IsNullOrEmpty(exe)) return;
                    string dir = System.IO.Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                        @"Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar");
                    System.IO.Directory.CreateDirectory(dir);
                    string name = System.IO.Path.GetFileNameWithoutExtension(exe);
                    string lnk = System.IO.Path.Combine(dir, name + ".lnk");
                    // crea collegamento con WScript.Shell se disponibile
                    var t = Type.GetTypeFromProgID("WScript.Shell");
                    if (t != null)
                    {
                        dynamic sh = Activator.CreateInstance(t)
                                     ?? throw new InvalidOperationException(
                                         "WScript.Shell non disponibile");
                        dynamic sc = sh.CreateShortcut(lnk);
                        sc.TargetPath = exe;
                        sc.Save();
                    }
                }
                _viewModel.InvalidatePins();
            }
            catch (Exception ex)
            {
                _bridge.Log($"pin/unpin da menu: {ex.Message}");
            }
        }

        // The left-button press + drag-up Jump List gesture (state machine,
        // identity hand-off, popup calls and teardown) lives in the partial
        // file TaskbarWindow.JumpList.cs together with its icon-transport
        // helper. The right-click menu above stays untouched by design.

        /// <summary>
        /// v2.61 - Testo di menu localizzato.
        ///
        /// Le voci dei menu che costruiamo qui (barra, orologio, gruppo di
        /// finestre) erano scritte in italiano nel codice: su un sistema
        /// inglese, tedesco o giapponese il menu restava italiano. Ora ogni
        /// voce arriva dai dizionari Languages/*.xaml - una traduzione per
        /// lingua, con l'inglese come ripiego - e il cambio di lingua si
        /// vede subito, perche' il testo si ricostruisce a ogni apertura.
        ///
        /// Il secondo argomento e' il ripiego inglese: serve solo se il
        /// dizionario della lingua non e' ancora caricato.
        /// </summary>
        private string L(string key, string fallback)
        {
            try
            {
                if (Application.Current?.TryFindResource(key) is string text &&
                    !string.IsNullOrEmpty(text))
                {
                    return text;
                }
            }
            catch
            {
                /* risorsa non disponibile: si usa il ripiego inglese */
            }

            /* v1.21.21: il ripiego e' inglese, quindi una chiave assente dal
             * dizionario della lingua attiva si vede come "menu in inglese
             * mentre il resto e' tradotto" - e finora non lasciava traccia.
             * Una riga per chiave nel log (non una per apertura di menu): se
             * succede di nuovo, il log dice QUALE chiave e' caduta sul
             * ripiego. */
            if (_missingLanguageKeys.Add(key))
            {
                try
                {
                    _bridge.Log($"lingua: chiave '{key}' assente dal dizionario, uso il ripiego inglese");
                }
                catch
                {
                    /* diagnostica: mai critica */
                }
            }

            return fallback;
        }

        /// <summary>Chiavi di menu gia' segnalate come assenti dal dizionario
        /// della lingua attiva (una riga di log ciascuna, vedi <see cref="L"/>).</summary>
        private readonly HashSet<string> _missingLanguageKeys = new();

        private static MenuItem CreateMenuItem(string header, Action action, bool enabled = true)
        {
            var item = new MenuItem { Header = header, IsEnabled = enabled };
            item.Click += (_, _) => action();
            return item;
        }

        // ===============================================================
        //  Notification area - drag and drop (v2.7: mouse-capture nativo,
        //  come CTrayNotify/CTrayOverflow reali: nessun OLE DoDragDrop,
        //  nessun tooltip "Sposta fra tray e overflow" - il pulsante segue
        //  semplicemente il cursore con hit-test manuale)
        // ===============================================================

        private Point _trayDragStart;
        private TrayIconModel? _trayDragCandidate;
        private FrameworkElement? _trayDragElement;
        private bool _trayDragging;
        private bool _trayDragJustFinished;
        private bool _trayDragOverNativeOverflow;

        private readonly Brush _trayDropBrush = new SolidColorBrush(Color.FromRgb(0x33, 0x99, 0xFF));
        private FrameworkElement? _trayDropTarget;

        // v3.1: icona fantasma che segue il cursore anche nel drag
        // tray -> overflow (speculare al ghost nativo overflow -> tray).
        private Window? _trayDragGhost;

        private void ShowTrayDragGhost()
        {
            if (_trayDragGhost != null || _trayDragCandidate?.Icon == null) return;
            try
            {
                _trayDragGhost = new Window
                {
                    WindowStyle = WindowStyle.None,
                    AllowsTransparency = true,
                    Background = null,
                    ShowInTaskbar = false,
                    ShowActivated = false,
                    Topmost = true,
                    Width = 24,
                    Height = 24,
                    IsHitTestVisible = false,
                    Content = new System.Windows.Controls.Image
                    {
                        Source = _trayDragCandidate.Icon,
                        Width = 16,
                        Height = 16,
                        Opacity = 0.85,
                        HorizontalAlignment = HorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center,
                    },
                };
                _trayDragGhost.Show();
            }
            catch (Exception ex)
            {
                // cosmetico: mai fallire il drag per il fantasma
                _trayDragGhost = null;
                _bridge.Log($"tray ghost: {ex.Message}");
            }
        }

        private void MoveTrayDragGhost(Point screenPt)
        {
            if (_trayDragGhost == null) return;
            try
            {
                _trayDragGhost.Left = screenPt.X + 8;
                _trayDragGhost.Top  = screenPt.Y + 8;
                ReassertTrayDragGhostOnTop();
            }
            catch (Exception) { /* ignora */ }
        }

        /* v2.56: keeps the dragged icon visible while it is over the overflow
         * panel.
         *
         * The ghost is a Topmost WPF window and so is the native overflow
         * panel: both live in the topmost band, and inside that band the
         * window shown LAST is the one in front. The panel opens while the
         * drag is already running (the arrow auto-opens it when the cursor
         * gets close), so it ends up on top of the ghost and the icon being
         * dragged vanishes - but only there: anywhere else the ghost has
         * nothing above it, which is why the icon stays visible.
         *
         * SetWindowPos(HWND_TOPMOST) moves a window to the FRONT of the
         * topmost band without moving or resizing it, so calling it while the
         * ghost follows the cursor (and once more as soon as the cursor is
         * over the panel) puts the dragged icon back in front for the rest of
         * the drag. No other window is touched: the panel keeps its own flags,
         * position and behaviour. */
        private void ReassertTrayDragGhostOnTop()
        {
            if (_trayDragGhost == null) return;
            try
            {
                IntPtr hwnd = new WindowInteropHelper(_trayDragGhost).Handle;
                if (hwnd != IntPtr.Zero)
                {
                    NativeMethods.SetWindowPos(hwnd, NativeMethods.HWND_TOPMOST,
                                               0, 0, 0, 0,
                                               NativeMethods.SWP_NOMOVE |
                                               NativeMethods.SWP_NOSIZE |
                                               NativeMethods.SWP_NOACTIVATE);
                }
            }
            catch (Exception) { /* cosmetico: mai far fallire il drag */ }
        }

        private void HideTrayDragGhost()
        {
            try { _trayDragGhost?.Close(); } catch (Exception) { /* ignora */ }
            _trayDragGhost = null;
        }

        private void TrayIcon_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (sender is FrameworkElement { DataContext: TrayIconModel icon } element)
            {
                _trayDragStart = e.GetPosition(null);
                _trayDragCandidate = icon;
                _trayDragElement = element;

                /* v2.62 - IL MODELLO NON SI TOCCA MENTRE SI TRASCINA.
                 *
                 * Un aggiornamento della tray in questo momento ricrea i
                 * contenitori delle icone: il mouse perde la cattura, gli
                 * handler muoiono con l'elemento e il trascinamento si
                 * interrompe da solo ("non riesco a spostare le icone").
                 * Le letture riprendono al rilascio, con una passata sola. */
                _viewModel.SuspendTrayRefresh();

                // Cattura subito sull'elemento: niente ciclo OLE, il tracking
                // del mouse resta sul thread UI come in TrayUI::WndProc reale.
                //
                // v2.62: la cattura resta sull'elemento (e' cosi' che il clic
                // continua a funzionare quando NON si trascina). Il
                // trascinamento non si perde piu' perche' adesso il modello
                // non viene piu' aggiornato mentre il pulsante e' premuto.
                element.CaptureMouse();
                PreviewMouseMove += TrayIcon_CapturedMouseMove;
                PreviewMouseLeftButtonUp += TrayIcon_CapturedMouseUp;
                element.LostMouseCapture += TrayIcon_LostCapture;
            }
        }

        private void TrayIcon_CapturedMouseMove(object sender, MouseEventArgs e)
        {
            if (e.LeftButton != MouseButtonState.Pressed || _trayDragCandidate == null)
            {
                return;
            }

            if (!_trayDragging)
            {
                Vector moved = e.GetPosition(null) - _trayDragStart;
                if (Math.Abs(moved.X) < SystemParameters.MinimumHorizontalDragDistance &&
                    Math.Abs(moved.Y) < SystemParameters.MinimumVerticalDragDistance)
                {
                    return;
                }
                _trayDragging = true;
                _bridge.Log($"tray: inizio trascinamento di {_trayDragCandidate.Tooltip} (pinned={_trayDragCandidate.IsPinned})");
                ShowTrayDragGhost();   // v3.1
            }

            // Hit-test manuale: esattamente cio' che fa la shell confrontando
            // il rettangolo del pulsante trascinato con quello delle due
            // toolbar (tray visibile / overflow), invece di negoziare un
            // IDataObject con un loop OLE separato.
            Point screenPt = PointToScreenSafe(e.GetPosition(this));
            MoveTrayDragGhost(screenPt);   // v3.1
            UpdateTrayDragHitTest(screenPt);
        }

        private void TrayIcon_CapturedMouseUp(object sender, MouseButtonEventArgs e)
        {
            EndTrayDrag(commit: true);
        }

        /// <summary>La cattura del mouse e' finita per conto suo (alt-tab,
        /// finestra disattivata): il trascinamento si annulla, ma lo stato non
        /// resta appeso — altrimenti la tray smetterebbe di aggiornarsi.</summary>
        private void TrayIcon_LostCapture(object sender, MouseEventArgs e)
        {
            EndTrayDrag(commit: false);
        }

        private void EndTrayDrag(bool commit)
        {
            if (_trayDragElement == null && _trayDragCandidate == null)
            {
                return;   /* gia' concluso: EndTrayDrag puo' arrivare due volte */
            }

            /* Il rilascio della cattura fa scattare LostMouseCapture, che
             * richiama questa funzione: senza il guardiano il secondo giro
             * azzererebbe lo stato e il trascinamento non verrebbe mai
             * applicato (l'icona tornerebbe al suo posto da sola). */
            if (_trayDragEnding)
            {
                return;
            }
            _trayDragEnding = true;
            try
            {
                try
                {
                    _trayDragElement?.ReleaseMouseCapture();
                    ReleaseMouseCapture();
                }
                catch (Exception) { /* ignora */ }

                PreviewMouseMove -= TrayIcon_CapturedMouseMove;
                PreviewMouseLeftButtonUp -= TrayIcon_CapturedMouseUp;
                if (_trayDragElement != null)
                {
                    _trayDragElement.LostMouseCapture -= TrayIcon_LostCapture;
                }
                LostMouseCapture -= TrayIcon_LostCapture;

                if (commit && _trayDragging && _trayDragCandidate != null)
                {
                    CommitTrayDrag();
                    _trayDragJustFinished = true;
                }

                _trayDragging = false;
                _trayDragCandidate = null;
                _trayDragElement = null;
                HideTrayDragGhost();   // v3.1
                ClearTrayDropAdorner();

                /* Le letture riprendono adesso, con una passata sola se
                 * qualcosa e' cambiato nel frattempo. */
                _viewModel.ResumeTrayRefresh();
            }
            finally
            {
                _trayDragEnding = false;
            }
        }

        /// <summary>Guardiano di rientranza di <see cref="EndTrayDrag"/>.</summary>
        private bool _trayDragEnding;

        /// <summary>
        /// Hit-test a mano contro TrayIcons/OverflowIcons/OverflowToggle,
        /// aggiorna l'adorner e apre l'overflow se il cursore ci resta sopra
        /// (come il rettangolo della freccetta che Windows osserva mentre
        /// trascini).
        /// </summary>
        private void UpdateTrayDragHitTest(Point screenPt)
        {
            ClearTrayDropAdorner();
            _trayDragOverNativeOverflow = false;

            // v2.8: sopra il pannello overflow NATIVO aperto: il drop manda
            // l'icona nell'overflow (come una zona vuota del pannello).
            if (_useNativeOverflow && _bridge.OverflowIsVisible() &&
                _bridge.OverflowGetRect(out int nl, out int nt, out int nr, out int nb) &&
                screenPt.X >= nl && screenPt.X <= nr && screenPt.Y >= nt && screenPt.Y <= nb)
            {
                _trayDragOverNativeOverflow = true;
                // v2.56: il pannello si e' appena aperto sopra il fantasma.
                ReassertTrayDragGhostOnTop();
                return;
            }

            if (OverflowToggle != null && IsPointOverElement(OverflowToggle, screenPt) && HasOverflowIcons)
            {
                // v2.7: un solo instradamento (nativo o WPF) via Checked.
                if (OverflowToggle.IsChecked != true)
                {
                    OverflowToggle.IsChecked = true;
                }
                return;
            }

            foreach (FrameworkElement candidate in EnumerateTrayIconElements())
            {
                if (candidate.DataContext is not TrayIconModel target ||
                    ReferenceEquals(target, _trayDragCandidate))
                {
                    continue;
                }

                if (IsPointOverElement(candidate, screenPt))
                {
                    bool after = screenPt.X > candidate.PointToScreen(new Point(0, 0)).X + candidate.ActualWidth / 2;
                    ShowTrayDropAdorner(candidate, after);
                    return;
                }
            }
        }

        private Point _lastTrayDragScreenPoint;

        private Point PointToScreenSafe(Point clientPt)
        {
            _lastTrayDragScreenPoint = PointToScreen(clientPt);
            return _lastTrayDragScreenPoint;
        }

        private void CommitTrayDrag()
        {
            Point screenPt = _lastTrayDragScreenPoint;

            // v2.8: rilascio dentro il pannello overflow nativo: l'icona
            // diventa nascosta, come un drop su zona vuota del pannello.
            if (_trayDragOverNativeOverflow && _trayDragCandidate!.IsPinned)
            {
                _trayDragCandidate.IsPinned = false;
                _bridge.SetTrayIconPinned(_trayDragCandidate.OwnerHwnd,
                                          _trayDragCandidate.Uid, false);
                _viewModel.NotificationArea.Resort();
                UpdateOverflowState();
                _bridge.OverflowRefresh();   // v3.1: il pannello si ricarica
                _bridge.Log("tray: drop sul pannello nativo -> nascosta");
                return;
            }

            // Rilascio sulla freccetta: come trascinare su OverflowToggle in Win7.
            if (OverflowToggle != null && IsPointOverElement(OverflowToggle, screenPt) &&
                _trayDragCandidate!.IsPinned)
            {
                _trayDragCandidate.IsPinned = false;
                _bridge.SetTrayIconPinned(_trayDragCandidate.OwnerHwnd,
                                          _trayDragCandidate.Uid, false);
                _viewModel.NotificationArea.Resort();
                UpdateOverflowState();
                _bridge.OverflowRefresh();   // v3.1
                _bridge.Log("tray: drop su freccetta overflow -> nascosta");
                return;
            }

            foreach (FrameworkElement candidate in EnumerateTrayIconElements())
            {
                if (candidate.DataContext is not TrayIconModel target ||
                    ReferenceEquals(target, _trayDragCandidate) ||
                    !IsPointOverElement(candidate, screenPt))
                {
                    continue;
                }

                bool after = screenPt.X > candidate.PointToScreen(new Point(0, 0)).X + candidate.ActualWidth / 2;
                bool zoneChanged = _trayDragCandidate!.IsPinned != target.IsPinned;
                if (zoneChanged)
                {
                    _bridge.OverflowRefresh();   // v3.1
                    _trayDragCandidate.IsPinned = target.IsPinned;
                }

                if (_viewModel.NotificationArea.MoveIcon(_trayDragCandidate, target, after))
                {
                    _bridge.TrayMoveIcon(_trayDragCandidate.OwnerHwnd, _trayDragCandidate.Uid,
                                         target.OwnerHwnd, target.Uid, after);
                    UpdateOverflowState();
                    _bridge.Log($"tray: drop su icona -> riordino (after={after}, zona cambiata={zoneChanged})");
                    if (zoneChanged && target.IsPinned)
                    {
                        CloseOverflowPopup();
                    }
                }
                else if (zoneChanged)
                {
                    _viewModel.NotificationArea.Resort();
                    UpdateOverflowState();
                }
                return;
            }

            // Rilascio su zona vuota (tray o overflow) senza icona bersaglio:
            // conta solo il cambio di zona, come TrayZone_Drop originale.
            bool overOverflowZone = (OverflowIcons != null && IsPointOverElement(OverflowIcons, screenPt))
                                    || _trayDragOverNativeOverflow;
            bool overTrayZone = TrayIcons != null && IsPointOverElement(TrayIcons, screenPt);
            if (overOverflowZone == overTrayZone)
            {
                return; // ne' l'una ne' l'altra: nessun cambiamento
            }

            bool dropToPinned = overTrayZone;
            if (_trayDragCandidate!.IsPinned == dropToPinned)
            {
                return;
            }

            _trayDragCandidate.IsPinned = dropToPinned;
            _viewModel.NotificationArea.Resort();
            UpdateOverflowState();
            _bridge.Log($"tray: drop di zona -> {(dropToPinned ? "tray visibile" : "overflow")}");
            if (dropToPinned)
            {
                CloseOverflowPopup();
            }
        }

        private static bool IsPointOverElement(FrameworkElement element, Point screenPt)
        {
            if (!element.IsVisible || element.ActualWidth <= 0 || element.ActualHeight <= 0)
            {
                return false;
            }
            Point topLeft = element.PointToScreen(new Point(0, 0));
            Rect rect = new(topLeft, new Size(element.ActualWidth, element.ActualHeight));
            return rect.Contains(screenPt);
        }

        private IEnumerable<FrameworkElement> EnumerateTrayIconElements()
        {
            foreach (var host in new ItemsControl?[] { TrayIcons, OverflowIcons })
            {
                if (host == null) continue;
                for (int i = 0; i < host.Items.Count; i++)
                {
                    if (host.ItemContainerGenerator.ContainerFromIndex(i) is FrameworkElement container)
                    {
                        yield return container;
                    }
                }
            }
        }

        // ===============================================================
        //  v2.2 - Pannello overflow: chiusura esplicita e posizionamento
        //  Aero. Misure e comportamenti adattati dal mod Windhawk
        //  "Aero Tray" di aubymori (github.com/aubymori), riscritti senza
        //  hooking: Win7Taskbar disegna la propria UI, quindi del mod si
        //  prende solo la logica di layout/DPI/posizionamento, non la
        //  parte di symbol hooking (non applicabile qui).
        // ===============================================================

        private GlobalMouseHook? _overflowMouseHook;

        /// <summary>Chiude il pannello tramite la freccetta (binding TwoWay).</summary>
        private void CloseOverflowPopup()
        {
            if (OverflowToggle != null)
            {
                OverflowToggle.IsChecked = false;
            }
            else if (OverflowPopup != null)
            {
                OverflowPopup.IsOpen = false;
            }
        }

        private bool _useNativeOverflow;

        /// <summary>Il ripristino della freccetta dopo l'apertura del flyout
        /// di sistema non deve essere interpretato come "l'utente ha chiuso
        /// il pannello".</summary>
        private bool _overflowToggleResetting;

        /// <summary>v2.60: la freccetta non apre il nostro pannello ma il
        /// flyout vero della shell (Windows 11, dove le icone nascoste non
        /// sono pulsanti di una toolbar e quindi non si possono disegnare
        /// nel pannello nostrano).</summary>
        private bool _overflowShellFlyout;

        /// <summary>v2.7: punto unico di instradamento dell'apertura del
        /// pannello: finestra nativa con vetro Aero se disponibile,
        /// altrimenti il Popup WPF (fallback raro).</summary>
        /// <summary>v2.27: il pannello nativo si e' chiuso da solo
        /// (click su "Personalizza...", click interno, focus perso):
        /// riporta la freccetta allo stato chiuso. Prima nessuno era
        /// iscritto all'evento e la freccetta restava "aperta".</summary>
        private void OnOverflowHidden(object? sender, EventArgs e)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (OverflowToggle != null && OverflowToggle.IsChecked == true)
                {
                    OverflowToggle.IsChecked = false;
                }
            }));
        }

        private void OverflowToggle_Checked(object sender, RoutedEventArgs e)
        {
            if (!_useNativeOverflow)
            {
                OverflowPopup.IsOpen = true;
                return;
            }

            // v2.60: rivalutato a ogni clic, non una volta sola all'avvio:
            // il servizio della tray puo' aver riconosciuto Windows 11 piu'
            // tardi (isola XAML creata dopo di noi).
            _overflowShellFlyout = _bridge.OverflowUsesShellFlyout();

            if (OverflowToggle != null)
            {
                Point tl = OverflowToggle.PointToScreen(new Point(0, 0));
                Point br = OverflowToggle.PointToScreen(
                    new Point(OverflowToggle.ActualWidth, OverflowToggle.ActualHeight));
                _bridge.OverflowShow((int)tl.X, (int)tl.Y, (int)br.X, (int)br.Y);
            }

            if (_overflowShellFlyout)
            {
                // Il flyout e' quello di Windows: si chiude da solo al primo
                // clic fuori o con Esc, e non manda nessun evento a noi.
                // La freccetta quindi non resta "premuta": torna subito su,
                // senza far passare nulla dal ramo Unchecked.
                Dispatcher.BeginInvoke(new Action(() =>
                {
                    if (OverflowToggle != null)
                    {
                        _overflowToggleResetting = true;
                        OverflowToggle.IsChecked = false;
                        _overflowToggleResetting = false;
                    }
                }));
                return;
            }

            /* v2.62 - IL PANNELLO NATIVO DEVE ESSERE DAVVERO COMPARSO.
             *
             * ShowNear non puo' fallire in modo visibile: se la finestra non
             * esiste (creazione rifiutata, sessione ristretta) la chiamata
             * non fa niente e per l'utente la freccetta e' morta - "il menu
             * di overflow non si apre". Qui si controlla e, se il pannello
             * nostro non c'e', si apre quello WPF (lo stesso contenuto, lo
             * stesso elenco di icone). Il clic non resta mai senza effetto. */
            bool nativeVisible = false;
            try { nativeVisible = _bridge.OverflowIsVisible(); } catch { nativeVisible = false; }

            if (!nativeVisible)
            {
                _bridge.Log("overflow: il pannello nativo non e' visibile, apro quello WPF");
                OverflowPopup.IsOpen = true;
                StartOverflowOutsideClose();
                return;
            }

            StartNativeOverflowOutsideClose();
        }

        private void OverflowToggle_Unchecked(object sender, RoutedEventArgs e)
        {
            if (!_useNativeOverflow)
            {
                OverflowPopup.IsOpen = false;
                return;
            }

            if (_overflowShellFlyout)
            {
                // Nessun pannello nostro da chiudere: il flyout di sistema
                // non e' nostro e non si comanda da qui.
                if (!_overflowToggleResetting)
                {
                    StopOverflowOutsideClose();
                }
                return;
            }

            /* v2.62: puo' essere aperto il pannello WPF (rete di sicurezza di
             * OverflowToggle_Checked) invece di quello nativo: si chiude
             * quello che c'e' davvero. */
            if (OverflowPopup != null && OverflowPopup.IsOpen)
            {
                OverflowPopup.IsOpen = false;
            }

            _bridge.OverflowHide();
            StopOverflowOutsideClose();
        }

        /// <summary>Come StartOverflowOutsideClose ma col rettangolo della
        /// finestra nativa: i clic dentro il pannello non lo chiudono.</summary>
        private void StartNativeOverflowOutsideClose()
        {
            try
            {
                if (!_bridge.OverflowGetRect(out int l, out int t, out int r, out int b))
                {
                    return;
                }

                _overflowMouseHook ??= new GlobalMouseHook();
                // GetWindowRect nativa: gia' in pixel fisici, come vuole l'hook.
                _overflowMouseHook.ExcludeRect = new Rect(l, t, r - l, b - t);
                _overflowMouseHook.ExcludeRect2 = Rect.Empty;
                if (OverflowToggle != null)
                {
                    double scale = 1.0;
                    if (PresentationSource.FromVisual(OverflowToggle)?.CompositionTarget is { } ct)
                    {
                        scale = ct.TransformToDevice.M11;
                    }
                    /* v2.62: origine gia' in pixel fisici (vedi
                     * StartOverflowOutsideClose): solo le dimensioni vanno
                     * convertite. Con l'origine moltiplicata, su uno schermo
                     * al 125% il rettangolo della freccetta finiva fuori
                     * posto e il clic sulla freccetta - quello che deve
                     * CHIUDERE il pannello - veniva trattato come un clic
                     * esterno. */
                    Point ttl = OverflowToggle.PointToScreen(new Point(0, 0));
                    _overflowMouseHook.ExcludeRect2 = new Rect(
                        ttl.X, ttl.Y,
                        OverflowToggle.ActualWidth * scale, OverflowToggle.ActualHeight * scale);
                }
                _overflowMouseHook.MouseDownOutside -= OnOverflowMouseDownOutside;
                _overflowMouseHook.MouseDownOutside += OnOverflowMouseDownOutside;
                _overflowMouseHook.Start();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartNativeOverflowOutsideClose: {ex.Message}");
            }
        }

        private void OverflowPopup_Opened(object? sender, EventArgs e)
        {
            _bridge.Log("overflow: pannello aperto");
            AdjustOverflowPopupOffset();
            StartOverflowOutsideClose();
        }

        private void OverflowPopup_Closed(object? sender, EventArgs e)
        {
            StopOverflowOutsideClose();
            if (OverflowPopup != null)
            {
                OverflowPopup.HorizontalOffset = 0;
                OverflowPopup.VerticalOffset = 0;
            }
        }

        /// <summary>
        /// Come AdjustWindowPosForTaskbar di "Aero Tray": se il pannello
        /// tocca (o sfiora) un bordo del monitor lo si spinge a 8 px
        /// (DIP: in WPF scalano gia' col DPI, l'equivalente di
        /// MulDiv(FLYOUT_OFFSET, dpiY, 96)) verso l'interno, stile flyout
        //  Aero.
        /// </summary>
        private void AdjustOverflowPopupOffset()
        {
            try
            {
                if (OverflowPopup?.Child is not FrameworkElement child ||
                    child.ActualWidth <= 0 || child.ActualHeight <= 0)
                {
                    return;
                }

                const double flyoutOffset = 8;

                // Rettangolo schermo in pixel fisici del pannello.
                double scale = 1.0;
                if (PresentationSource.FromVisual(child)?.CompositionTarget is { } ct)
                {
                    scale = ct.TransformToDevice.M11;
                }

                Point tl = child.PointToScreen(new Point(0, 0));
                double[] rc =
                {
                    tl.X * scale, tl.Y * scale,
                    (tl.X + child.ActualWidth) * scale,
                    (tl.Y + child.ActualHeight) * scale
                };

                var mi = new NativeMethods.MONITORINFO();
                mi.cbSize = System.Runtime.InteropServices.Marshal.SizeOf<NativeMethods.MONITORINFO>();
                var pt = new NativeMethods.POINT { x = (int)rc[0], y = (int)rc[1] };
                IntPtr hm = NativeMethods.MonitorFromPoint(pt, NativeMethods.MONITOR_DEFAULTTONEAREST);
                if (hm == IntPtr.Zero || !NativeMethods.GetMonitorInfoW(hm, ref mi))
                {
                    return;
                }

                double[] work =
                {
                    mi.rcWork.Left, mi.rcWork.Top, mi.rcWork.Right, mi.rcWork.Bottom
                };

                double dx = 0, dy = 0;
                for (int i = 0; i < 4; i++)
                {
                    double cur = Math.Abs(work[i] - rc[i]);
                    if (cur < flyoutOffset * scale)
                    {
                        double delta = (flyoutOffset * scale) - cur;
                        if (i % 2 == 0) { dx += (i > 1) ? -delta : delta; }
                        else            { dy += (i > 1) ? -delta : delta; }
                    }
                }

                if (dx != 0 || dy != 0)
                {
                    OverflowPopup.HorizontalOffset += dx / scale;
                    OverflowPopup.VerticalOffset += dy / scale;
                    _bridge.Log("overflow: pannello spinto a 8 px dal bordo (flyout Aero)");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"AdjustOverflowPopupOffset: {ex.Message}");
            }
        }

        /// <summary>
        /// Con StaysOpen=True la chiusura fuori dal pannello la facciamo
        /// noi, come per il flyout dell'orologio: hook globale del mouse,
        /// click fuori da pannello+freccetta = chiude.
        /// </summary>
        private void StartOverflowOutsideClose()
        {
            try
            {
                if (OverflowPopup?.Child is not FrameworkElement child)
                {
                    return;
                }

                double scale = 1.0;
                if (PresentationSource.FromVisual(child)?.CompositionTarget is { } ct2)
                {
                    scale = ct2.TransformToDevice.M11;
                }

                /* v2.62 - ORIGINE IN PIXEL FISICI, DIMENSIONE CONVERTITA.
                 *
                 * PointToScreen restituisce gia' pixel dello schermo: moltiplicare
                 * anche l'origine per il fattore DPI spostava il rettangolo
                 * (e lo ingrandiva) su ogni schermo scalato, cosi' i clic
                 * dentro il pannello venivano letti come "fuori" e il pannello
                 * si chiudeva da solo. Le dimensioni, invece, arrivano dal
                 * layout in unita' indipendenti e vanno convertite. */
                Point tl = child.PointToScreen(new Point(0, 0));
                Rect popupRect = new Rect(tl.X, tl.Y,
                                          child.ActualWidth * scale,
                                          child.ActualHeight * scale);

                Rect toggleRect = Rect.Empty;
                if (OverflowToggle != null)
                {
                    Point ttl = OverflowToggle.PointToScreen(new Point(0, 0));
                    toggleRect = new Rect(ttl.X, ttl.Y,
                                          OverflowToggle.ActualWidth * scale,
                                          OverflowToggle.ActualHeight * scale);
                }

                _overflowMouseHook ??= new GlobalMouseHook();
                _overflowMouseHook.ExcludeRect = popupRect;
                _overflowMouseHook.ExcludeRect2 = toggleRect;
                _overflowMouseHook.MouseDownOutside -= OnOverflowMouseDownOutside;
                _overflowMouseHook.MouseDownOutside += OnOverflowMouseDownOutside;
                _overflowMouseHook.Start();
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"StartOverflowOutsideClose: {ex.Message}");
            }
        }

        private void StopOverflowOutsideClose()
        {
            _overflowMouseHook?.Stop();
        }

        private void OnOverflowMouseDownOutside(object? sender, Point pt)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                // v2.7: pannello overflow nativo: chiude su clic fuori
                // (fuori dalla finestra nativa e fuori dalla freccetta).
                if (_useNativeOverflow && _bridge.OverflowIsVisible() && !_trayDragging)
                {
                    bool insideNative = _bridge.OverflowGetRect(out int l, out int t, out int r, out int b)
                                        && pt.X >= l && pt.X <= r && pt.Y >= t && pt.Y <= b;
                    bool insideToggle = OverflowToggle != null && OverflowToggle.IsMouseOver;
                    if (!insideNative && !insideToggle)
                    {
                        CloseOverflowPopup();
                    }
                }

                if (OverflowPopup != null && OverflowPopup.IsOpen && !_trayDragging)
                {
                    // v2.3: i clic VUOTI dentro il pannello (padding, sfondo,
                    // zone senza icone) NON devono chiuderlo: chiude solo un
                    // clic davvero fuori. IsMouseOver e' la verifica
                    // autorevole (il rettangolo calcolato puo' sbagliare di
                    // un bordo con scaling DPI > 100%).
                    bool insidePopup = OverflowPopup.Child is FrameworkElement pc && pc.IsMouseOver;
                    bool insideToggle = OverflowToggle != null && OverflowToggle.IsMouseOver;
                    if (!insidePopup && !insideToggle)
                    {
                        CloseOverflowPopup();
                    }
                }
            }));
        }

        /// <summary>Stato "premuto" del link Personalizza... (come Aero Tray).</summary>
        private void CustomizeTrayLink_Pressed(object sender, MouseButtonEventArgs e)
        {
            if (sender is TextBlock tb)
            {
                tb.Foreground = new SolidColorBrush(Color.FromRgb(0x00, 0x4E, 0x9E));
                tb.TextDecorations = TextDecorations.Underline;
            }
        }

        private void CustomizeTrayLink_Unpressed(object sender, System.Windows.Input.MouseEventArgs e)
        {
            if (sender is TextBlock tb)
            {
                tb.ClearValue(TextBlock.ForegroundProperty);
                tb.ClearValue(TextBlock.TextDecorationsProperty);
            }
        }

        private void ShowTrayDropAdorner(FrameworkElement element, bool after)
        {
            ClearTrayDropAdorner();

            if (element is Control control)
            {
                control.BorderBrush = _trayDropBrush;
                control.BorderThickness = after
                    ? new Thickness(0, 0, 2, 0)
                    : new Thickness(2, 0, 0, 0);

                _trayDropTarget = element;
            }
        }

        private void ClearTrayDropAdorner()
        {
            if (_trayDropTarget is Control control)
            {
                control.ClearValue(Control.BorderBrushProperty);
                control.ClearValue(Control.BorderThicknessProperty);
            }

            _trayDropTarget = null;
        }

        private DateTime _trayDownUtc = DateTime.MinValue;
        private readonly Dictionary<(ulong, uint), DateTime> _lastLeftClickUtc = new();

        // v2.36: flyout di rete Windows 7.
        private bool _netFlyoutInit;

        // v3.8: flyout di rete variante Windows 8 (riquadro ricreato).
        private bool _net8FlyoutInit;
        private readonly Dictionary<ulong, bool> _networkOwnerCache = new();

        /// <summary>True se l'icona tray appartiene a pnidui.dll (rete).
        /// Risultato in cache per finestra proprietaria.</summary>
        private bool IsNetworkTrayIcon(TrayIconModel icon)
        {
            if (icon == null) return false;
            if (!_networkOwnerCache.TryGetValue(icon.OwnerHwnd, out bool isNet))
            {
                try { isNet = _bridge.IsNetworkTrayOwner(icon.OwnerHwnd); }
                catch { isNet = false; }
                _networkOwnerCache[icon.OwnerHwnd] = isNet;
            }
            return isNet;
        }

        // v2.38: modulo proprietario generico (SndVolSSO.dll = volume,
        // stobject.dll = batteria). Cache per (finestra, modulo).
        private readonly Dictionary<(ulong, string), bool> _ownerModuleCache = new();

        private bool IsOwnerModule(TrayIconModel icon, string moduleName)
        {
            if (icon == null || string.IsNullOrEmpty(moduleName)) return false;
            var key = (icon.OwnerHwnd, moduleName.ToLowerInvariant());
            if (!_ownerModuleCache.TryGetValue(key, out bool match))
            {
                try { match = _bridge.TrayOwnerModuleMatch(icon.OwnerHwnd, moduleName); }
                catch { match = false; }
                _ownerModuleCache[key] = match;
            }
            return match;
        }

        private void TrayIcon_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            // If drag just finished, don't send click (avoid click after drag)
            if (_trayDragJustFinished)
            {
                _trayDragJustFinished = false;
                e.Handled = true;
                return;
            }

            _trayDragCandidate = null;

            if (sender is not FrameworkElement element ||
                element.DataContext is not TrayIconModel icon)
            {
                return;
            }

            // Simple click handling - always send LeftDown + Left like original working version
            // English: Send tray click to owner window, which will open flyout (volume, network, etc)
            // Italiano: Invia click alla finestra proprietaria, che aprirà il flyout
            _trayDownUtc = DateTime.MinValue;

            // v2.61: il rettangolo corrente dell'icona prima del clic (vedi
            // ReportClickedIconRect): il riquadro si ancora dove l'icona sta
            // adesso, non dove stava quando e' stata importata.
            ReportClickedIconRect(element, icon);

            // v2.2: come in Windows 7, il click su un'icona del pannello
            // overflow avvia l'app e chiude il pannello.
            CloseOverflowPopup();

            // v2.36: icona di rete. L'icona mostrata resta SEMPRE quella
            // originale di Windows (importata dal tray reale): qui si decide
            // solo QUALE flyout aprire. Con "Windows 7 (ricreato)" il click
            // non viene inoltrato al proprietario (altrimenti si aprirebbe
            // anche il flyout moderno); con "Windows 10/11" si inoltra come
            // prima. Il tasto destro continua ad aprire il menu nativo.
            // v3.8: stessa conseguenza per "Windows 8 (ricreato)".
            int netModeClick = RetroBar.Utilities.Settings.Instance.NetworkFlyoutMode;
            if ((netModeClick == 0 || netModeClick == 2) && IsNetworkTrayIcon(icon))
            {
                if (netModeClick == 0)
                {
                    if (!_netFlyoutInit)
                    {
                        _netFlyoutInit = _bridge.NetFlyoutInit();
                        /* v2.62: il core deve saperlo, perche' il clic sulle
                         * icone di rete RICREATE lo gestisce lui. */
                        try { _bridge.SetWin7NetworkFlyout(_netFlyoutInit); } catch { }
                    }
                    if (_netFlyoutInit)
                    {
                        // v2.37 punto 16: sincronizza la lingua del flyout con
                        // quella dell'app prima di ogni apertura.
                        try
                        {
                            var stLang = RetroBar.Utilities.Settings.Instance;
                            int langIdx = Math.Max(0,
                                Array.IndexOf(kLangCodes, stLang.Language ?? RetroBar.Utilities.Settings.DefaultLanguageCode));
                            _bridge.NetFlyoutSetLanguage(langIdx);
                        }
                        catch { }

                        Point topLeft = element.PointToScreen(new Point(0, 0));
                        int iw = (int)Math.Ceiling(element.ActualWidth);
                        int ih = (int)Math.Ceiling(element.ActualHeight);
                        _bridge.NetFlyoutToggleAt((int)topLeft.X, (int)topLeft.Y,
                            (int)topLeft.X + iw, (int)topLeft.Y + ih);
                        e.Handled = true;
                        return;
                    }
                }
                else /* netModeClick == 2: v3.8, riquadro di rete stile Windows 8 */
                {
                    if (!_net8FlyoutInit)
                    {
                        _net8FlyoutInit = _bridge.Net8FlyoutInit();
                        /* come SetWin7NetworkFlyout per la variante Win7: il
                         * core deve saperlo prima dei click sintetici. */
                        try { _bridge.SetWin8NetworkFlyout(_net8FlyoutInit); } catch { }
                    }
                    if (_net8FlyoutInit)
                    {
                        try
                        {
                            var stLang = RetroBar.Utilities.Settings.Instance;
                            int langIdx = Math.Max(0,
                                Array.IndexOf(kLangCodes, stLang.Language ?? RetroBar.Utilities.Settings.DefaultLanguageCode));
                            _bridge.Net8FlyoutSetLanguage(langIdx);
                        }
                        catch { }

                        Point topLeft = element.PointToScreen(new Point(0, 0));
                        int iw = (int)Math.Ceiling(element.ActualWidth);
                        int ih = (int)Math.Ceiling(element.ActualHeight);
                        _bridge.Net8FlyoutToggleAt((int)topLeft.X, (int)topLeft.Y,
                            (int)topLeft.X + iw, (int)topLeft.Y + ih);
                        e.Handled = true;
                        return;
                    }
                }
            }

            // v2.38 punto 1: mixer volume classico (SndVol.exe -f). Spento di
            // default; si attiva dalle Proprieta'. Se il lancio non riesce il
            // click prosegue verso il flyout moderno (ripiego silenzioso).
            var stFly = RetroBar.Utilities.Settings.Instance;
            if (stFly.UseClassicVolumeMixer && IsOwnerModule(icon, "SndVolSSO.dll"))
            {
                Point tlVol = element.PointToScreen(new Point(0, 0));
                int cx = (int)tlVol.X + (int)(element.ActualWidth / 2);
                int cy = (int)tlVol.Y;   // SndVol si ancora sopra l'icona
                if (_bridge.LaunchClassicVolume(cx, cy))
                {
                    e.Handled = true;
                    return;
                }
                // lancio fallito: si prosegue col percorso normale
            }

            // v2.42: flyout batteria con l'approccio di ExplorerPatcher.
            // Invece di ricreare il flyout, si abilita il flyout Win32
            // classico di Windows (chiave ImmersiveShell
            // "UseWin32BatteryFlyout"=1, come la mod di riferimento) e si
            // inoltra il clic all'icona originale: e' stobject ad aprire il
            // VERO flyout Windows 7, gia' ancorato all'icona e gestito dal
            // sistema (chiusura sui clic esterni inclusa). Ripiego sul
            // flyout ricreato solo se l'inoltro non e' possibile.
            if (stFly.UseBatteryFlyout && IsOwnerModule(icon, "stobject.dll"))
            {
                EnsureWin32BatteryFlyoutReg(true);
                Point screenBat = element.PointToScreen(e.GetPosition(element));
                _viewModel.SendTrayClick(icon, TrayClick.LeftDown,
                    (int)screenBat.X, (int)screenBat.Y);
                _viewModel.SendTrayClick(icon, TrayClick.Left,
                    (int)screenBat.X, (int)screenBat.Y);
                e.Handled = true;
                return;
            }

            Point screen = element.PointToScreen(e.GetPosition(element));

            _viewModel.SendTrayClick(icon, TrayClick.LeftDown, (int)screen.X, (int)screen.Y);
            _viewModel.SendTrayClick(icon, TrayClick.Left, (int)screen.X, (int)screen.Y);
            e.Handled = true;
        }

        private void TrayIcon_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (sender is not FrameworkElement element ||
                element.DataContext is not TrayIconModel icon)
            {
                return;
            }

            _trayDownUtc = DateTime.UtcNow;
        }

        /// <summary>
        /// v2.61: riporta al core il rettangolo ATTUALE dell'icona cliccata,
        /// un attimo prima del clic.
        ///
        /// Serve al core per ancorare il riquadro di sistema che sta per
        /// aprire: quel rettangolo viene letto ADESSO dall'elemento vero, con
        /// lo schermo e il DPI di adesso. Cosi' il flyout segue l'icona se la
        /// barra si e' spostata, il DPI e' cambiato, le icone sono state
        /// riordinate, l'overflow e' stato aperto/chiuso o Explorer e'
        /// ripartito: mai una coordinata ricordata dall'importazione.
        /// Vale anche per le icone che stanno nel pannello di overflow, che
        /// il rapporto periodico dei rettangoli non copre.
        /// </summary>
        private void ReportClickedIconRect(FrameworkElement element, TrayIconModel icon)
        {
            try
            {
                double scale = _hwndSource?.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
                if (scale <= 0)
                {
                    scale = 1.0;
                }

                Point origin = element.PointToScreen(new Point(0, 0));
                _bridge.SetIconRect(
                    icon.OwnerHwnd, icon.Uid,
                    (int)origin.X, (int)origin.Y,
                    (int)(origin.X + element.ActualWidth * scale),
                    (int)(origin.Y + element.ActualHeight * scale));
            }
            catch
            {
                /* cosmetico: se il rapporto non riesce, il core usa il
                 * rettangolo della barra (ripiego dichiarato). */
            }
        }

        private void TrayIcon_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            if (sender is not FrameworkElement element ||
                element.DataContext is not TrayIconModel icon)
            {
                return;
            }

            ReportClickedIconRect(element, icon);
            Point screen = element.PointToScreen(e.GetPosition(element));
            _viewModel.SendTrayClick(icon, TrayClick.Right, (int)screen.X, (int)screen.Y);
            e.Handled = true;
        }

        private void TrayArea_MouseWheel(object sender, MouseWheelEventArgs e)
        {
            IntPtr hwnd = _hwndSource?.Handle ?? IntPtr.Zero;
            if (hwnd == IntPtr.Zero)
            {
                return;
            }

            const int WM_APPCOMMAND = 0x0319;
            const int APPCOMMAND_VOLUME_DOWN = 0x90000;
            const int APPCOMMAND_VOLUME_UP = 0xA0000;

            NativeMethods.SendMessage(hwnd, WM_APPCOMMAND, hwnd,
                (IntPtr)(e.Delta > 0 ? APPCOMMAND_VOLUME_UP : APPCOMMAND_VOLUME_DOWN));
            e.Handled = true;
        }

        private void TrayIcon_MouseUp(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton != MouseButton.Middle)
            {
                return;
            }

            if (sender is not FrameworkElement element ||
                element.DataContext is not TrayIconModel icon)
            {
                return;
            }

            Point screen = element.PointToScreen(e.GetPosition(element));
            _viewModel.SendTrayClick(icon, TrayClick.Middle, (int)screen.X, (int)screen.Y);
            e.Handled = true;
        }

        // ===============================================================
        //  Clock and flyouts - close on taskbar click and outside click
        // ===============================================================

        private void CloseAllFlyoutsSimple(bool skipClock = false)
        {
            try
            {
                _bridge.HideFlyout(FlyoutKind.Network);
                _bridge.HideFlyout(FlyoutKind.Battery);
                _bridge.HideFlyout(FlyoutKind.Sound);
                // v2.41: anche i flyout ricreati/nativi seguono la regola
                // "clic sulla barra = chiude tutto" (come l'orologio):
                // flyout batteria ricreato e mixer classico SndVol.
                _bridge.BatteryFlyoutHide();
                _bridge.CloseClassicVolume();
                // v2.9: se il clic e' sul pulsante orologio, il calendario lo
                // gestisce il toggle nativo: chiuderlo noi qui lo farebbe
                // riaprire dal clic inoltrato a explorer.
                if (!skipClock)
                {
                    _bridge.HideFlyout(FlyoutKind.Clock);
                }
            }
            catch { }
            try
            {
                if (_calendarPopup != null)
                {
                    _calendarPopup.IsOpen = false;
                    /* v2.61: l'istanza resta in memoria, pronta per il
                     * prossimo clic (prima si buttava via per ricostruirla). */
                }
                _globalMouseHook?.Stop();
            }
            catch { }
        }

        private void TaskbarBackground_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.Handled) return;

            CloseAllFlyoutsSimple();

            if (OverflowPopup != null && OverflowPopup.IsOpen)
            {
                if (e.Source is FrameworkElement fe && fe.Name == "OverflowToggle") return;
                OverflowPopup.IsOpen = false;
                if (OverflowToggle != null) OverflowToggle.IsChecked = false;
            }
        }

        /// <summary>v2.7: come in Explorer, QUALSIASI clic sulla barra chiude
        /// cio' che e' aperto (flyout, pannello overflow, popup delle bande,
        /// menu start). Tunneling: i clic assorbiti dai pulsanti (task, tray,
        /// orologio) arrivano qui prima che il pulsante li marchi gestiti.
        /// Esclusioni: il clic sul pulsante Start non chiude il menu start
        /// (il toggle lo gestisce), il clic sulla freccetta non chiude il
        /// pannello overflow (il toggle lo gestisce).</summary>
        private void TaskbarRoot_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            bool onStartButton = false;
            bool onOverflowToggle = false;
            bool onClock = false;
            bool onSearchButton = false;

            if (e.OriginalSource is DependencyObject src)
            {
                DependencyObject? cur = src;
                while (cur != null)
                {
                    if (cur is FrameworkElement fe)
                    {
                        if (fe.Name == "StartButton") { onStartButton = true; break; }
                        if (fe.Name == "OverflowToggle") { onOverflowToggle = true; break; }
                        if (fe.Name == "ClockHost") { onClock = true; }
                        if (fe.Name == "SearchButton") { onSearchButton = true; }
                    }
                    cur = System.Windows.Media.VisualTreeHelper.GetParent(cur);
                }
            }

            CloseAllFlyoutsSimple(onClock);

            // v2.41: la ricerca si chiude con qualunque clic esterno alla
            // sua interfaccia; sulla barra, il clic sulla lente e' il
            // toggle e non deve chiuderla (lo riaprirebbe subito dopo).
            if (!onSearchButton)
            {
                try { _bridge.AppSearchHide(); } catch { }
            }

            if (!onOverflowToggle && OverflowPopup is { IsOpen: true })
            {
                OverflowPopup.IsOpen = false;
                if (OverflowToggle != null) OverflowToggle.IsChecked = false;
            }

            if (DesktopBandPopup is { IsOpen: true })
            {
                DesktopBandPopup.IsOpen = false;
            }

            if (!onStartButton)
            {
                // v2.24: sync semplice col menu Start: click sulla barra
                // (tranne orb) = chiude il menu E azzera lo stato premuto.
                _startMenuMonitor?.TryCloseStartMenu();
                if (StartButton != null && StartButton.IsChecked == true)
                {
                    StartButton.IsChecked = false;
                }
            }
        }

        private void TaskbarBackground_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            if (e.Handled)
            {
                return;
            }

            e.Handled = true;

            // v2.8: come in Explorer, anche il tasto destro chiude cio' che
            // era aperto (flyout, overflow, menu start) prima di mostrare
            // il menu contestuale della barra.
            CloseAllFlyoutsSimple();
            if (OverflowPopup is { IsOpen: true } || _bridge.OverflowIsVisible())
            {
                CloseOverflowPopup();
            }
            if (DesktopBandPopup is { IsOpen: true })
            {
                DesktopBandPopup.IsOpen = false;
            }
            _startMenuMonitor?.TryCloseStartMenu();

            ShowTaskbarContextMenu(sender as FrameworkElement);
        }

        private void Clock_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            e.Handled = true;

            FrameworkElement anchor = sender as FrameworkElement ?? this;

            Point origin;
            try
            {
                origin = anchor.PointToScreen(Mouse.GetPosition(anchor));
            }
            catch (InvalidOperationException)
            {
                origin = anchor.PointToScreen(new Point(0, 0));
            }

            int choice = _bridge.ShowContextMenuEx(
                (int)Math.Round(origin.X),
                (int)Math.Round(origin.Y),
                bottomEdge: true,
                // v2.7: il menu dell'orologio e' quello della barra PARI PARI
                // (screenshot Windows 7) con le due voci dell'orologio
                // inserite dopo "Barre degli strumenti". I separatori non
                // consumano indici: 1-3 barre, 4 data/ora, 5 icone notifica,
                // 6 sovrapponi, 7 pila, 8 affiancate, 9 desktop, 10 gest.
                // attivita', 11 blocca, 12 proprieta'.
                ">" + L("lang_menu_toolbars", "Toolbars") + "\n" +
                (DesktopBandHost.Visibility == Visibility.Visible ? "*" : "") +
                    L("lang_menu_desktop", "Desktop") + "\n" +
                (LinksBandHost.Visibility == Visibility.Visible ? "*" : "") +
                    L("lang_menu_links", "Links") + "\n" +
                (AddressBandHost.Visibility == Visibility.Visible ? "*" : "") +
                    L("lang_menu_address", "Address") + "\n" +
                "<\n" +
                L("lang_edit_datetime", "Adjust date/time") + "\n" +
                L("lang_customize_notify", "Customize notification area...") + "\n" +
                "-\n" +
                L("lang_menu_cascade", "Cascade windows") + "\n" +
                L("lang_menu_stack", "Show windows stacked") + "\n" +
                L("lang_menu_sidebyside", "Show windows side by side") + "\n" +
                L("lang_show_desktop", "Show desktop") + "\n" +
                "-\n" +
                L("lang_task_manager", "Start Task Manager") + "\n" +
                "-\n" +
                (_taskbarLocked ? "*" : "") +
                    L("lang_menu_lock", "Lock the taskbar") + "\n" +
                L("lang_properties", "Properties"),
                // v2.43: il menu dell'orologio si apre DOVE STA IL CURSORE
                // (come un menu contestuale normale), non ancorato alla
                // barra: e' la stessa richiesta fatta per il menu semplice
                // della barra. I menu delle APP (finestra di sistema,
                // gruppo, pin) non passano da qui e restano invariati.
                anchorAtCursor: true);

            switch (choice)
            {
                case 1:
                    SetBandVisible(DesktopBandHost, DesktopBandHost.Visibility != Visibility.Visible);
                    break;
                case 2:
                    SetBandVisible(LinksBandHost, LinksBandHost.Visibility != Visibility.Visible);
                    break;
                case 3:
                    SetBandVisible(AddressBandHost, AddressBandHost.Visibility != Visibility.Visible);
                    break;
                case 4:
                    OpenDateTimeSettings();
                    break;
                case 5:
                    OpenNativeNotificationAreaSettings();
                    break;
                case 6:
                    NativeMethods.CascadeWindows(IntPtr.Zero, 0, IntPtr.Zero, 0, null);
                    break;
                case 7:
                    NativeMethods.TileWindows(IntPtr.Zero, 2 /*MDITILE_HORIZONTAL*/, IntPtr.Zero, 0, null);
                    break;
                case 8:
                    NativeMethods.TileWindows(IntPtr.Zero, 1 /*MDITILE_VERTICAL*/, IntPtr.Zero, 0, null);
                    break;
                case 9:
                    _bridge.ToggleShowDesktop();
                    break;
                case 10:
                    _bridge.ShowTaskManager();
                    break;
                case 11:
                    _taskbarLocked = !_taskbarLocked;
                    break;
                case 12:
                    ShowPropertiesWindow();
                    break;
            }
        }

        private void OpenDateTimeSettings()
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    Arguments = "shell:::{E2E7934B-DCE5-43C4-9576-7FE4F75E7480}",
                    UseShellExecute = true
                });
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Apertura di Data e ora non riuscita: {ex.Message}");
            }
        }

        private void Clock_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            e.Handled = true;

            /* v2.63 - LA SCELTA DELL'UTENTE VALE ANCHE SU WINDOWS 11.
             *
             * Con "Windows 7" selezionato il clic deve aprire il calendario di
             * Windows 7, non quello ricreato: e' il difetto segnalato. Il core
             * ora prova il riquadro classico (finestra ClockFlyoutWindow, la
             * stessa che Windows 7 usa) e risponde si' solo quando quella
             * finestra compare davvero: se compare, il calendario ricreato non
             * si apre sopra, se non compare si apre il nostro come ripiego
             * dichiarato. La logica sta nel core, che conosce la build con
             * certezza; qui non si decide piu' nulla. */
            if (Settings.Instance.UseNativeClockFlyout)
            {
                IntPtr handle = _hwndSource?.Handle ?? IntPtr.Zero;

                if (handle != IntPtr.Zero && _bridge.ShowClockFlyout(handle))
                {
                    return;
                }
            }

            /* v2.62 - UN RIQUADRO SOLO, SEMPRE.
             *
             * Su Windows 11 il riquadro e' SEMPRE il nostro. Se la shell ha
             * aperto il suo per conto (il clic puo' arrivare anche alla barra
             * nativa, che su alcune build resta dietro la nostra) va chiuso
             * PRIMA di mostrare il nostro: senza questa riga i due convivono.
             * La chiamata chiude e basta, non apre nulla, e se il riquadro
             * non c'e' non ha effetto. */
            if (IsWindows11Host())
            {
                _bridge.HideClockFlyout();
            }

            ShowClassicCalendarFlyout(sender as FrameworkElement);
        }

        private void CustomizeTrayLink_Click(object sender, MouseButtonEventArgs e)
        {
            // v2.1: il link era gia' collegato ma l'apertura passava solo da
            // Process.Start(explorer.exe ...): se la shell lo inghiottiva
            // (barra nativa nascosta, avvio in corso) non succedeva nulla e
            // l'errore finiva solo in Debug. Ora l'apertura e' reale e
            // verificata: prima il meccanismo nativo del core (ShellExecuteEx
            // sul namespace shell:::{05D7B0F4-...}), poi due ripieghi
            // espliciti, e ogni fallimento viene registrato.
            e.Handled = true;

            if (sender is TextBlock link)
            {
                link.ClearValue(TextBlock.ForegroundProperty);
                link.ClearValue(TextBlock.TextDecorationsProperty);
            }

            if (OverflowPopup != null)
            {
                OverflowPopup.IsOpen = false;
            }
            if (OverflowToggle != null)
            {
                OverflowToggle.IsChecked = false;
            }

            OpenNativeNotificationAreaSettings();
        }

        private void OpenNativeNotificationAreaSettings()
        {
            /* Open Windows' native Notification Area settings page directly,
             * as this command did before the removed imitation existed. */
            // Native shell namespace first.
            try
            {
                if (_bridge.OpenNotificationIconsSettings())
                {
                    return;
                }
                Debug.WriteLine("Apertura delle impostazioni native delle icone rifiutata dalla shell: ripiego 1");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Apertura delle impostazioni native delle icone non riuscita: {ex.Message}");
            }

            // 2) Ripiego gestito: control.exe col nome canonico dell'applet
            //    (stesso meccanismo del nativo, lanciato da qui per sicurezza).
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "control.exe",
                    Arguments = "/name Microsoft.NotificationAreaIcons",
                    UseShellExecute = true
                });
                return;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Ripiego control.exe non riuscito: {ex.Message}");
            }

            // 3) Ripiego: explorer.exe col percorso del Pannello come
            //    argomento (come quando l'utente lo digita in Esegui;
            //    stesso formato del mod "Aero Tray" di aubymori).
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    Arguments = "shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0\\::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}",
                    UseShellExecute = true
                });
                return;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Ripiego explorer.exe non riuscito: {ex.Message}");
            }

            // 3) Ultimo ripiego: pagina Impostazioni equivalente di
            //    Windows 10/11 (su Win11 il CLSID reindirizza gia' li').
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "ms-settings:taskbar",
                    UseShellExecute = true
                });
            }
            catch (Exception ex)
            {
                StartupGuard.Note($"apertura 'Personalizza...' fallita: {ex.GetType().Name}: {ex.Message}");
                Debug.WriteLine($"Apertura impostazioni icone non riuscita: {ex.Message}");
            }
        }

        private void ClockHost_ToolTipOpening(object sender, ToolTipEventArgs e)
        {
            if (sender is FrameworkElement element)
            {
                element.ToolTip = DateTime.Now.ToString("D", CultureInfo.CurrentCulture);
            }
        }

        private static void OpenControlPanelApplet(string applet, string arguments)
        {
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "rundll32.exe",
                    Arguments = $"shell32.dll,Control_RunDLL {applet}{arguments}",
                    UseShellExecute = true
                });
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Apertura di {applet} non riuscita: {ex.Message}");
            }
        }

        // ===============================================================
        //  Riportamento dei rettangoli icona: a eventi, non a timer
        // ===============================================================
        //
        // Il core risponde a Shell_NotifyIconGetRect con questi rettangoli
        // e ci aggancia i flyout: devono essere AGGIORNATI, ma non c'e'
        // motivo di ricavarli ogni 500 ms quando la barra si muove solo
        // per motivi precisi (layout, resize, DPI, overflow, riordino).
        // Ora ogni fonte nota ne chiede la ricalcolazione con un debounce
        // one-shot, e il riporto chiama il nativo solo per le icone il cui
        // rettangolo e' DAVVERO cambiato.

        private DispatcherTimer? _iconRectDebounce;
        private readonly Dictionary<(ulong Hwnd, uint Uid), (int L, int T, int R, int B)> _lastReportedRects = new();

        private void HookIconRectReporting()
        {
            // Una richiesta a ogni evento che sposta le icone: layout e
            // posizione della barra (agganciati piu' sopra), apertura o
            // chiusura dell'overflow, cambio di insieme. Il nativo ha il
            // suo ripiego (TB_GETITEMRECT sul toolbar del modello) per il
            // tempo fra l'evento e il debounce.
            _viewModel.NotificationArea.PinnedIcons.CollectionChanged += (_, _) => ScheduleIconRectReport();
            _viewModel.NotificationArea.UnpinnedIcons.CollectionChanged += (_, _) => ScheduleIconRectReport();
            ScheduleIconRectReport();
        }

        private void ScheduleIconRectReport()
        {
            _iconRectDebounce ??= new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(120),
            };
            _iconRectDebounce.Stop();
            _iconRectDebounce.Tick -= IconRectDebounce_Tick;
            _iconRectDebounce.Tick += IconRectDebounce_Tick;
            _iconRectDebounce.Start();
        }

        private void IconRectDebounce_Tick(object? sender, EventArgs e)
        {
            if (_iconRectDebounce != null)
            {
                _iconRectDebounce.Stop();
                _iconRectDebounce.Tick -= IconRectDebounce_Tick;
            }
            ReportIconRects();
        }

        private void ReportIconRects()
        {
            if (_hwndSource?.CompositionTarget == null)
            {
                return;
            }

            /* v3.7.1: il rapporto periodico dei rettangoli gira da un timer
             * e tocca sia gli elementi visivi (PointToScreen) sia il core
             * nativo (SetIconRect): un elemento che sparisce a meta' giro o
             * una chiamata nativa fallita non devono abbattere la barra.
             * In caso di errore si salta il giro: il core tiene l'ultimo
             * rettangolo noto e si riprova al prossimo tick, come gia'
             * avviene nel rapporto al clic (ReportClickedIconRect). */
            try
            {
                double scale = _hwndSource.CompositionTarget.TransformToDevice.M11;
                if (scale <= 0)
                {
                    scale = 1.0;
                }

                var seen = new HashSet<(ulong, uint)>();

                foreach (var model in _viewModel.NotificationArea.PinnedIcons)
                {
                    if (TrayIcons.ItemContainerGenerator
                            .ContainerFromItem(model) is not FrameworkElement element
                        || element.ActualWidth <= 0)
                    {
                        continue;
                    }

                    seen.Add((model.OwnerHwnd, model.Uid));

                    Point origin = element.PointToScreen(new Point(0, 0));
                    var rect = ((int)origin.X,
                                (int)origin.Y,
                                (int)(origin.X + element.ActualWidth * scale),
                                (int)(origin.Y + element.ActualHeight * scale));

                    if (_lastReportedRects.TryGetValue((model.OwnerHwnd, model.Uid), out (int, int, int, int) previous)
                        && previous == rect)
                    {
                        continue;   // nessuna variazione: nessuna chiamata nativa
                    }

                    _lastReportedRects[(model.OwnerHwnd, model.Uid)] = rect;
                    _bridge.SetIconRect(model.OwnerHwnd, model.Uid,
                                        rect.Item1, rect.Item2, rect.Item3, rect.Item4);
                }

                // Icone uscite dalla barra (overflow, rimozione): la memoria
                // del "gia' riportato" va pulita per non crescere per sempre.
                if (_lastReportedRects.Count > seen.Count)
                {
                    var stale = new List<(ulong, uint)>();
                    foreach (var kv in _lastReportedRects)
                    {
                        if (!seen.Contains(kv.Key))
                        {
                            stale.Add(kv.Key);
                        }
                    }
                    foreach (var key in stale)
                    {
                        _lastReportedRects.Remove(key);
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"rapporto rettangoli icone: {ex.Message}");
            }
        }

        private Popup? _calendarPopup;
        private TextBlock? _calendarHeader;
        private bool _calendarFlyoutReady;

        /// <summary>
        /// Vero se il sistema che ci ospita e' Windows 11 (build >= 22000).
        /// Letto una volta sola dal core, che lo sa con certezza: il manifest
        /// dell'app non dichiara Windows 10 e quindi la versione gestita
        /// mentirebbe.
        /// </summary>
        private bool IsWindows11Host()
        {
            if (_isWindows11Host == null)
            {
                try
                {
                    _isWindows11Host = _bridge.IsWindows11();
                }
                catch
                {
                    /* v2.62 - IN CASO DI DUBBIO SI USA IL RIQUADRO NOSTRO.
                     *
                     * Prima un errore qui valeva "non e' Windows 11", e con
                     * quel "no" il percorso nativo tornava attivo: la shell
                     * accettava di mostrare il suo riquadro, l'isola XAML
                     * compariva piu' tardi del tempo che possiamo aspettare, e
                     * l'utente vedeva prima il calendario di sistema e poi il
                     * nostro. Il comportamento giusto e' l'opposto: si usa il
                     * riquadro nativo solo se sappiamo CON CERTEZZA che questo
                     * non e' Windows 11. */
                    _isWindows11Host = true;
                }
            }

            return _isWindows11Host.Value;
        }

        private bool? _isWindows11Host;

        /// <summary>
        /// v2.61: crea UNA VOLTA il riquadro del calendario di Windows 7 e lo
        /// tiene in memoria.
        ///
        /// Prima veniva costruito a ogni clic e distrutto alla chiusura: il
        /// primo clic pagava la costruzione dell'albero visuale del
        /// calendario mentre l'utente guardava lo schermo, ed era il momento
        /// in cui il riquadro nativo trovava il tempo di comparire.
        /// Ora l'istanza e' pronta da prima (viene costruita all'avvio) e
        /// mostrarla e' solo IsOpen = true.
        /// </summary>
        private void EnsureCalendarFlyout()
        {
            if (_calendarFlyoutReady && _calendarPopup != null)
            {
                return;
            }

            var flyoutFont = new FontFamily("Segoe UI, Tahoma, Arial");

            var calendar = new Win7Taskbar.Controls.Win7Calendar
            {
                Margin = new Thickness(4)
            };

            _calendarHeader = new TextBlock
            {
                Text = DateTime.Now.ToString("dddd d MMMM yyyy"),
                HorizontalAlignment = HorizontalAlignment.Center,
                Margin = new Thickness(0, 6, 0, 2),
                FontFamily = flyoutFont,
                FontSize = 12,
                FontWeight = FontWeights.SemiBold,
                Foreground = new SolidColorBrush(Color.FromRgb(0x1E, 0x39, 0x5B))
            };

            var stack = new StackPanel();
            stack.Children.Add(_calendarHeader);
            stack.Children.Add(calendar);

            _calendarPopup = new Popup
            {
                /* v2.62 - POSIZIONE CALCOLATA, NON DELEGATA AL LAYOUT.
                 *
                 * Prima il riquadro si agganciava all'elemento con
                 * Placement=Custom e un callback che lo metteva "sopra, a
                 * filo con la destra": nessuno teneva conto del monitor, dei
                 * bordi dello schermo ne' del DPI, e con la barra spostata o a
                 * DPI diversi il riquadro finiva fuori posto (o fuori dallo
                 * schermo). Ora la posizione la calcola PlaceCalendarFlyout:
                 * rettangolo reale dell'orologio, area di lavoro del monitor
                 * su cui l'orologio si trova, e correzione dopo l'apertura se
                 * il risultato non e' quello richiesto. */
                PlacementTarget = (UIElement)this,
                Placement = PlacementMode.Absolute,
                StaysOpen = true,
                AllowsTransparency = true,
                PopupAnimation = PopupAnimation.Fade,
                Child = new Border
                {
                    Background = new LinearGradientBrush(
                        Color.FromArgb(0xF8, 0xFF, 0xFF, 0xFF),
                        Color.FromArgb(0xF2, 0xE9, 0xF0, 0xF7), 90),
                    BorderBrush = new SolidColorBrush(Color.FromRgb(0x86, 0x9A, 0xB4)),
                    BorderThickness = new Thickness(1),
                    CornerRadius = new CornerRadius(4),
                    Padding = new Thickness(2),
                    Child = stack
                }
            };

            _calendarPopup.MouseLeave += CalendarPopup_MouseLeave;
            _calendarPopup.Opened += CalendarPopup_Opened;
            _calendarPopup.Closed += CalendarPopup_Closed;
            _calendarFlyoutReady = true;
        }

        private void ShowClassicCalendarFlyout(FrameworkElement? target)
        {
            /* Riquadro gia' pronto: mostrarlo o nasconderlo non costruisce
             * niente. La data si riallinea al momento dell'apertura. */
            EnsureCalendarFlyout();

            if (_calendarPopup == null)
            {
                return;
            }

            if (_calendarPopup.IsOpen)
            {
                _calendarPopup.IsOpen = false;
                _globalMouseHook?.Stop();
                return;
            }

            if (_calendarHeader != null)
            {
                _calendarHeader.Text = DateTime.Now.ToString("dddd d MMMM yyyy");
            }

            /* La mira e' l'orologio di ADESSO: se la barra si e' spostata o
             * il DPI e' cambiato, il riquadro compare comunque attaccato
             * all'orologio. */
            _calendarPopup.PlacementTarget = target ?? (UIElement)this;

            /* Misura prima di aprire: serve la dimensione del riquadro per
             * calcolarne la posizione, e chiederla qui evita che il primo
             * disegno avvenga nella posizione sbagliata. */
            try
            {
                if (_calendarPopup.Child is FrameworkElement child)
                {
                    child.Measure(new Size(double.PositiveInfinity,
                                           double.PositiveInfinity));
                }
            }
            catch { }

            PlaceCalendarFlyout();
            _calendarPopup.IsOpen = true;
        }

        /* ------------------------------------------------------------------ */
        /*  v2.62 - Posizione del riquadro dell'orologio                      */
        /*                                                                    */
        /*  Regole, nell'ordine:                                              */
        /*   1. il riquadro e' agganciato all'orologio VERO, sul monitor in    */
        /*      cui l'orologio si trova adesso (barra spostata, monitor        */
        /*      diverso, DPI diverso: si ricalcola ogni volta);                */
        /*   2. come Windows 7: a filo con la destra dell'orologio e subito    */
        /*      sopra la barra, con un piccolo distacco;                       */
        /*   3. se sopra non c'e' spazio (barra in alto, schermo piccolo) si   */
        /*      passa sotto l'orologio;                                        */
        /*   4. il riquadro resta sempre dentro l'area di lavoro del monitor.  */
        /*                                                                    */
        /*  Le coordinate sono pixel fisici; gli offset del Popup sono in      */
        /* unita' indipendenti dal DPI, quindi si dividono per la scala del    */
        /* monitor. La conversione viene poi verificata (vedi                */
        /* CorrectCalendarPlacement) perche' un riquadro sbagliato di 200 px  */
        /* e' peggio di nessun riquadro.                                      */
        /* ------------------------------------------------------------------ */

        private const int CalendarFlyoutGap = 2;

        /// <summary>Scala DPI dell'albero a cui appartiene l'elemento.</summary>
        private static double GetDpiScale(Visual visual)
        {
            try
            {
                double scale = VisualTreeHelper.GetDpi(visual).DpiScaleX;
                return scale > 0 ? scale : 1.0;
            }
            catch
            {
                return 1.0;
            }
        }

        /// <summary>Rettangolo fisico (pixel) dell'orologio.</summary>
        private bool TryGetClockScreenRect(out int left, out int top,
                                           out int right, out int bottom)
        {
            left = top = right = bottom = 0;

            FrameworkElement anchor = ClockHost;
            if (anchor == null)
            {
                anchor = this;
            }

            try
            {
                Point a = anchor.PointToScreen(new Point(0, 0));
                Point b = anchor.PointToScreen(
                    new Point(anchor.ActualWidth, anchor.ActualHeight));
                left = (int)Math.Round(Math.Min(a.X, b.X));
                top = (int)Math.Round(Math.Min(a.Y, b.Y));
                right = (int)Math.Round(Math.Max(a.X, b.X));
                bottom = (int)Math.Round(Math.Max(a.Y, b.Y));
            }
            catch (InvalidOperationException)
            {
                return false;
            }

            return right > left && bottom > top;
        }

        /// <summary>
        /// Misura in pixel fisici del riquadro (0 se non e' ancora disegnato).
        /// </summary>
        private bool TryGetFlyoutSize(out int width, out int height)
        {
            width = height = 0;

            if (_calendarPopup?.Child is not FrameworkElement child)
            {
                return false;
            }

            double scale = GetDpiScale(child);
            double w = child.ActualWidth > 0 ? child.ActualWidth : child.DesiredSize.Width;
            double h = child.ActualHeight > 0 ? child.ActualHeight : child.DesiredSize.Height;
            if (w <= 0 || h <= 0)
            {
                return false;
            }

            width = (int)Math.Ceiling(w * scale);
            height = (int)Math.Ceiling(h * scale);
            return true;
        }

        /// <summary>Posizione fisica voluta per il riquadro.</summary>
        private bool TryGetCalendarFlyoutTarget(out int x, out int y)
        {
            x = y = 0;

            if (!TryGetClockScreenRect(out int clockLeft, out int clockTop,
                                       out int clockRight, out int clockBottom) ||
                !TryGetFlyoutSize(out int width, out int height))
            {
                return false;
            }

            var center = new NativeMethods.POINT
            {
                x = (clockLeft + clockRight) / 2,
                y = (clockTop + clockBottom) / 2
            };
            IntPtr monitor = NativeMethods.MonitorFromPoint(
                center, NativeMethods.MONITOR_DEFAULTTONEAREST);
            if (monitor == IntPtr.Zero)
            {
                return false;
            }

            var info = new NativeMethods.MONITORINFO
            {
                cbSize = System.Runtime.InteropServices.Marshal
                    .SizeOf<NativeMethods.MONITORINFO>()
            };
            if (!NativeMethods.GetMonitorInfoW(monitor, ref info))
            {
                return false;
            }

            NativeMethods.RECT work = info.rcWork;

            /* Come Windows 7: a filo con la destra dell'orologio, sopra la
             * barra. Se sopra non c'e' spazio si passa sotto. */
            x = clockRight - width;
            y = clockTop - height - CalendarFlyoutGap;
            if (y < work.Top)
            {
                y = clockBottom + CalendarFlyoutGap;
            }

            /* Mai fuori dall'area di lavoro del monitor. */
            int maxX = work.Right - width;
            int maxY = work.Bottom - height;
            x = Math.Max(work.Left, Math.Min(x, Math.Max(work.Left, maxX)));
            y = Math.Max(work.Top, Math.Min(y, Math.Max(work.Top, maxY)));
            return true;
        }

        /// <summary>
        /// Quanti pixel fisici vale una unita' di offset del Popup.
        ///
        /// Non si presume: si MISURA. Con l'app consapevole del DPI per
        /// monitor, la conversione dipende dal monitor su cui la finestra del
        /// riquadro viene creata, e sbagliarla di un fattore 1.5 lascia il
        /// riquadro a meta' strada. Alla prima apertura si parte dalla scala
        /// DPI dell'elemento, poi il valore si calibra sullo spostamento
        /// realmente ottenuto.
        /// </summary>
        private double _calendarOffsetScale;

        /// <summary>Applica la posizione voluta (in unita' del Popup).</summary>
        private void PlaceCalendarFlyout()
        {
            if (_calendarPopup == null ||
                !TryGetCalendarFlyoutTarget(out int x, out int y))
            {
                return;
            }

            if (_calendarOffsetScale <= 0)
            {
                _calendarOffsetScale = GetDpiScale(_calendarPopup.Child);
            }

            _calendarPopup.HorizontalOffset = x / _calendarOffsetScale;
            _calendarPopup.VerticalOffset = y / _calendarOffsetScale;
        }

        /// <summary>Posizione fisica attuale del riquadro (NaN se non c'e').</summary>
        private bool TryGetFlyoutOrigin(out double x, out double y)
        {
            x = y = double.NaN;
            if (_calendarPopup?.Child is not FrameworkElement child)
            {
                return false;
            }
            try
            {
                Point screen = child.PointToScreen(new Point(0, 0));
                x = screen.X;
                y = screen.Y;
                return true;
            }
            catch (InvalidOperationException)
            {
                return false;
            }
        }

        /// <summary>
        /// v2.62: dopo l'apertura si controlla DOVE il riquadro e' finito
        /// davvero e, se non e' dove deve stare, lo si sposta della differenza.
        /// La conversione fra unita' del Popup e pixel si misura dallo
        /// spostamento ottenuto (vedi _calendarOffsetScale): cosi' anche il
        /// caso peggiore, un monitor con DPI diverso da quello su cui il
        /// riquadro crede di stare, converge in un paio di passaggi invece di
        /// restare sbagliato. Dopo tre tentativi si lascia stare: meglio un
        /// riquadro fuori posto di un riquadro che continua a saltare.
        /// </summary>
        private void CorrectCalendarPlacement(int attempt = 0)
        {
            if (_calendarPopup == null || !_calendarPopup.IsOpen ||
                !TryGetCalendarFlyoutTarget(out int wantedX, out int wantedY) ||
                !TryGetFlyoutOrigin(out double actualX, out double actualY))
            {
                return;
            }

            double dx = wantedX - actualX;
            double dy = wantedY - actualY;
            if (Math.Abs(dx) < 1 && Math.Abs(dy) < 1)
            {
                return;
            }

            if (_calendarOffsetScale <= 0)
            {
                _calendarOffsetScale = GetDpiScale(_calendarPopup.Child);
            }

            double appliedX = dx / _calendarOffsetScale;
            double appliedY = dy / _calendarOffsetScale;
            _calendarPopup.HorizontalOffset += appliedX;
            _calendarPopup.VerticalOffset += appliedY;

            if (attempt >= 3)
            {
                return;
            }

            /* Secondo tempo: si guarda di quanto si e' spostato davvero e si
             * corregge il fattore di conversione, poi si riprova. */
            Dispatcher.BeginInvoke(DispatcherPriority.Loaded, new Action(() =>
            {
                if (!TryGetFlyoutOrigin(out double movedX, out double movedY))
                {
                    return;
                }

                double movedBy = Math.Abs(movedX - actualX) > Math.Abs(movedY - actualY)
                    ? movedX - actualX
                    : movedY - actualY;
                double asked = Math.Abs(appliedX) > Math.Abs(appliedY)
                    ? appliedX
                    : appliedY;

                if (Math.Abs(asked) > 1 && Math.Abs(movedBy) > 1)
                {
                    double measured = movedBy / asked;
                    if (measured > 0.2 && measured < 5)
                    {
                        _calendarOffsetScale = measured;
                    }
                }

                CorrectCalendarPlacement(attempt + 1);
            }));
        }

        private void CalendarPopup_Opened(object? sender, EventArgs e)
        {
            /* v2.62: dove e' finito davvero il riquadro? Se non e' attaccato
             * all'orologio lo si sposta (DPI, monitor, barra spostata). */
            CorrectCalendarPlacement();

            // Start global mouse hook to detect clicks outside flyout
            // Requirement: If clock flyout open and click other parts of screen outside flyout, it must close
            try
            {
                if (_calendarPopup?.Child is FrameworkElement child)
                {
                    // Get flyout bounds
                    Point topLeft = child.PointToScreen(new Point(0, 0));
                    Rect flyoutRect = new Rect(topLeft.X, topLeft.Y, child.ActualWidth, child.ActualHeight);
                    if (flyoutRect.Width <= 0 || flyoutRect.Height <= 0)
                    {
                        // Fallback estimate
                        flyoutRect = new Rect(topLeft.X - 200, topLeft.Y - 300, 250, 350);
                    }

                    // Clock host bounds
                    Rect clockRect = Rect.Empty;
                    if (ClockHost != null)
                    {
                        try
                        {
                            Point clockTopLeft = ClockHost.PointToScreen(new Point(0, 0));
                            clockRect = new Rect(clockTopLeft.X, clockTopLeft.Y, ClockHost.ActualWidth, ClockHost.ActualHeight);
                        }
                        catch { }
                    }

                    _globalMouseHook ??= new GlobalMouseHook();
                    _globalMouseHook.ExcludeRect = flyoutRect;
                    _globalMouseHook.ExcludeRect2 = clockRect;
                    _globalMouseHook.MouseDownOutside -= OnGlobalMouseDownOutside;
                    _globalMouseHook.MouseDownOutside += OnGlobalMouseDownOutside;
                    _globalMouseHook.Start();
                }
            }
            catch { }
        }

        private void CalendarPopup_Closed(object? sender, EventArgs e)
        {
            _globalMouseHook?.Stop();
        }

        private void OnGlobalMouseDownOutside(object? sender, Point pt)
        {
            Dispatcher.BeginInvoke(new Action(() =>
            {
                if (_calendarPopup != null && _calendarPopup.IsOpen)
                {
                    _calendarPopup.IsOpen = false;
                    /* v2.61: l'istanza resta in memoria, pronta per il
                     * prossimo clic (prima si buttava via per ricostruirla). */
                    _globalMouseHook?.Stop();
                }
            }));
        }

        private void CalendarPopup_MouseLeave(object sender, MouseEventArgs e)
        {
            var closeTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(500)
            };

            closeTimer.Tick += (s, _) =>
            {
                closeTimer.Stop();

                if (_calendarPopup?.Child?.IsMouseOver == true ||
                    ClockHost?.IsMouseOver == true)
                {
                    return;
                }

                if (_calendarPopup != null)
                {
                    _calendarPopup.IsOpen = false;
                    /* v2.61: l'istanza resta in memoria, pronta per il
                     * prossimo clic (prima si buttava via per ricostruirla). */
                    _globalMouseHook?.Stop();
                }
            };

            closeTimer.Start();
        }

        // v2.4: menu contestuale della barra COME quello vero di Windows 7
        // (vedi riferimento fotografico dell'utente): la voce "Barre degli
        // strumenti" apre il sottomenu con cui si ATTIVANO le bande
        // Desktop/Collegamenti/Indirizzo (nascoste per default, niente di
        // forzato), piu' le voci di disposizione finestre, mostra desktop,
        // gestione attivita', blocca e proprieta'.
        private bool _taskbarLocked = true;

        /// <summary>
        /// v2.5: menu contestuale della barra disegnato da Win32 TrackPopupMenuEx
        /// (ShowContextMenuEx nella DLL), NON un ContextMenu WPF: è lo stesso
        /// disegno nativo dei menu del tray e dei pulsanti, con sottomenu
        /// "Barre degli strumenti" e spunte come la barra vera di Windows 7.
        /// Ordine delle voci selezionabili (l'indice restituito è 1-based
        /// nell'ordine di comparsa, intestazioni di sottomenu escluse):
        /// 1 Desktop, 2 Collegamenti, 3 Indirizzo, 4 Sovrapponi, 5 Pila,
        /// 6 Affiancate, 7 Mostra desktop, 8 Gestione attività,
        /// 9 Blocca la barra, 10 Proprietà.
        /// </summary>
        private void ShowTaskbarContextMenu(FrameworkElement? target)
        {
            Point origin;
            try
            {
                origin = (target ?? (FrameworkElement)this).PointToScreen(
                    Mouse.GetPosition(target ?? (FrameworkElement)this));
            }
            catch (InvalidOperationException)
            {
                origin = Mouse.GetPosition(this);
                try { origin = this.PointToScreen(origin); }
                catch (InvalidOperationException) { return; }
            }

            try
            {
                // Spunte: tre barre visibili + "Blocca la barra" quando bloccata.
                string items =
                    ">" + L("lang_menu_toolbars", "Toolbars") + "\n" +
                    (DesktopBandHost.Visibility == Visibility.Visible ? "*" : "") +
                        L("lang_menu_desktop", "Desktop") + "\n" +
                    (LinksBandHost.Visibility == Visibility.Visible ? "*" : "") +
                        L("lang_menu_links", "Links") + "\n" +
                    (AddressBandHost.Visibility == Visibility.Visible ? "*" : "") +
                        L("lang_menu_address", "Address") + "\n" +
                    "<\n" +
                    "-\n" +
                    L("lang_menu_cascade", "Cascade windows") + "\n" +
                    L("lang_menu_stack", "Show windows stacked") + "\n" +
                    L("lang_menu_sidebyside", "Show windows side by side") + "\n" +
                    L("lang_show_desktop", "Show desktop") + "\n" +
                    "-\n" +
                    L("lang_task_manager", "Start Task Manager") + "\n" +
                    "-\n" +
                    (_taskbarLocked ? "*" : "") +
                        L("lang_menu_lock", "Lock the taskbar") + "\n" +
                    L("lang_properties", "Properties");

                // v2.43: anche il menu semplice della barra (Proprieta',
                // Avvia Gestione attivita'...) si apre dove sta il cursore,
                // esattamente come quello dell'orologio. I menu delle APP
                // (finestra di sistema, gruppo, pin) NON passano da qui e
                // restano ancorati sopra il pulsante.
                int choice = _bridge.ShowContextMenuEx(
                    (int)Math.Round(origin.X),
                    (int)Math.Round(origin.Y),
                    bottomEdge: true,
                    items,
                    anchorAtCursor: true);

                switch (choice)
                {
                    case 1:
                        SetBandVisible(DesktopBandHost, DesktopBandHost.Visibility != Visibility.Visible);
                        break;
                    case 2:
                        SetBandVisible(LinksBandHost, LinksBandHost.Visibility != Visibility.Visible);
                        break;
                    case 3:
                        SetBandVisible(AddressBandHost, AddressBandHost.Visibility != Visibility.Visible);
                        break;
                    case 4:
                        NativeMethods.CascadeWindows(IntPtr.Zero, 0, IntPtr.Zero, 0, null);
                        break;
                    case 5:
                        NativeMethods.TileWindows(IntPtr.Zero, 2 /*MDITILE_HORIZONTAL*/, IntPtr.Zero, 0, null);
                        break;
                    case 6:
                        NativeMethods.TileWindows(IntPtr.Zero, 1 /*MDITILE_VERTICAL*/, IntPtr.Zero, 0, null);
                        break;
                    case 7:
                        _bridge.ToggleShowDesktop();
                        break;
                    case 8:
                        _bridge.ShowTaskManager();
                        break;
                    case 9:
                        _taskbarLocked = !_taskbarLocked;
                        break;
                    case 10:
                        ShowPropertiesWindow();
                        break;
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Menu barra: {ex}");
            }
        }

        private void SetBandVisible(System.Windows.FrameworkElement host, bool visible)
        {
            host.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
            if (visible)
            {
                if (ReferenceEquals(host, DesktopBandHost)) LoadDesktopBand();
                else if (ReferenceEquals(host, LinksBandHost)) LoadLinksBand();
            }

            // v2.5: la scelta resta memorizzata in toolbars.ini (%LocalAppData%).
            SaveToolbarPrefs();
        }

        // v3.0: ricerca applicazioni OPZIONALE (si abilita da Proprietà):
        // reimplementazione ispirata alla ricerca di Windows, non e' la
        // ricerca di sistema ne' un suo sostituto ufficiale.
        private bool _appSearchInit;
        private byte[]? _searchIconPixels;
        private int _searchIconW, _searchIconH;

        /// <summary>v3.3: prepara l'icona della ricerca. I pixel BGRA vanno
        /// al core nativo (icona propria del pannello ricerca), mentre sulla
        /// barra il pulsante mostra la stessa icona dentro una casella 32x32.
        ///
        /// v2.46: la sorgente e' l'icona fornita dall'utente (cartella di
        /// documenti con la lente), incorporata come PNG base64
        /// (EmbeddedAssets.SearchIcon: 64x74, ritagliata sul contenuto reale).
        /// Il ripiego su Resources/win7search.png e' stato rimosso: quel PNG e'
        /// nel GraphicalResourceBundle e non e' piu' un file runtime.</summary>
        private void LoadSearchIconPixels()
        {
            try
            {
                if (!TryLoadSearchPixelsFromEmbedded())
                {
                    return;   /* niente pixel: il core nativo usera' la sua icona */
                }

                /* v2.48: sulla BARRA l'icona e' la lente bianca vettoriale
                 * disegnata in XAML. I pixel caricati qui servono SOLO al
                 * core nativo (icona della finestra di ricerca), quindi non
                 * si costruisce piu' nessun bitmap per il pulsante. */
            }
            catch (Exception ex)
            {
                _bridge.Log($"lente ricerca: {ex.Message}");
            }
        }

        /// <summary>v2.46: sorgente primaria dell'icona ricerca: la risorsa
        /// incorporata (base64). Falsa se la decodifica non riesce.</summary>
        private bool TryLoadSearchPixelsFromEmbedded()
        {
            var embedded = Win7Taskbar.Utilities.EmbeddedAssets.SearchIcon;
            if (embedded == null)
            {
                return false;
            }

            return TryCopySearchPixels(embedded);
        }

        /// <summary>v2.46: ripiego: il PNG su disco accanto all'eseguibile.
        /// Serve solo se la risorsa incorporata non si decodifica.</summary>
        private bool TryLoadSearchPixelsFromFile()
        {
            // The legacy disk fallback was removed with the duplicated PNG;
            // the embedded asset failure path already leaves the native
            // implementation in charge of its own fallback icon.
            return false;
        }

        /// <summary>Estrae i pixel BGRA (top-down) da una sorgente qualsiasi,
        /// convertendo il formato se serve. Tiene le dimensioni in
        /// <c>_searchIconW/H</c>, usate poi dal ridimensionamento.</summary>
        private bool TryCopySearchPixels(System.Windows.Media.Imaging.BitmapSource source)
        {
            var src = source;
            if (src.Format != System.Windows.Media.PixelFormats.Bgra32)
            {
                src = new System.Windows.Media.Imaging.FormatConvertedBitmap(
                    src, System.Windows.Media.PixelFormats.Bgra32, null, 0);
            }

            int w = src.PixelWidth;
            int h = src.PixelHeight;
            if (w <= 0 || h <= 0)
            {
                return false;
            }

            var pixels = new byte[w * h * 4];
            src.CopyPixels(pixels, w * 4, 0);

            _searchIconW = w;
            _searchIconH = h;
            _searchIconPixels = pixels;
            return true;
        }

        /// <summary>v3.4: downscale bicubico (Catmull-Rom) su pixel BGRA:
        /// la lente resta nitida anche a 20 px.</summary>
        private static byte[] BicubicScaleBGRA(byte[] src, int sw, int sh, int dw, int dh)
        {
            byte[] dst = new byte[dw * dh * 4];
            for (int y = 0; y < dh; y++)
            {
                float fy = (y + 0.5f) * sh / dh - 0.5f;
                int iy = (int)Math.Floor(fy);
                for (int x = 0; x < dw; x++)
                {
                    float fx = (x + 0.5f) * sw / dw - 0.5f;
                    int ix = (int)Math.Floor(fx);
                    for (int c = 0; c < 4; c++)
                    {
                        float acc = 0;
                        for (int j = -1; j <= 2; j++)
                        {
                            int sy = Math.Min(sh - 1, Math.Max(0, iy + j));
                            float wy = CubicW(fy - (iy + j));
                            for (int i = -1; i <= 2; i++)
                            {
                                int sx = Math.Min(sw - 1, Math.Max(0, ix + i));
                                acc += src[(sy * sw + sx) * 4 + c] * wy * CubicW(fx - (ix + i));
                            }
                        }
                        dst[(y * dw + x) * 4 + c] =
                            (byte)Math.Min(255, Math.Max(0, (int)Math.Round(acc)));
                    }
                }
            }
            return dst;
        }

        private static float CubicW(float t)
        {
            const float a = -0.5f;   // Catmull-Rom
            t = Math.Abs(t);
            if (t <= 1) return (a + 2) * t * t * t - (a + 3) * t * t + 1;
            if (t < 2) return a * (t * t * t - 5 * t * t + 8 * t - 4);
            return 0;
        }

        /* v2.41: barra di ricerca bianca disegnata a mano (SDF +
         * supersampling 4x), sfondo trasparente: nessun riquadro bianco
         * dentro l'icona. Spazio di disegno 48x48. */
        private static float Clamp01(float v) => v < 0 ? 0 : v > 1 ? 1 : v;

        private static float SdRoundRect(float x, float y, float cx, float cy,
            float hx, float hy, float r)
        {
            float qx = Math.Abs(x - cx) - (hx - r);
            float qy = Math.Abs(y - cy) - (hy - r);
            float ax = qx > 0 ? qx : 0;
            float ay = qy > 0 ? qy : 0;
            float outside = MathF.Sqrt(ax * ax + ay * ay);
            float inside = Math.Max(qx, qy) < 0 ? Math.Max(qx, qy) : 0;
            return outside + inside - r;
        }

        private static float SdSegment(float x, float y,
            float x1, float y1, float x2, float y2)
        {
            float px = x - x1, py = y - y1;
            float bx = x2 - x1, by = y2 - y1;
            float h = Clamp01((px * bx + py * by) / (bx * bx + by * by));
            float dx = px - bx * h, dy = py - by * h;
            return MathF.Sqrt(dx * dx + dy * dy);
        }

        /// <summary>v2.41: glifo BGRA "barra di ricerca bianca": campo
        /// arrotondato bianco con leggera sfumatura verticale e lente con
        /// manico color ardesia sul lato destro. Anti-aliasing tramite
        /// supersampling e riduzione bicubica a outSize.</summary>
        private static byte[] RenderWhiteSearchBar(int outSize)
        {
            const float D = 48f;
            const int SS = 4;
            const int S = 48 * SS;
            var big = new byte[S * S * 4];
            for (int py = 0; py < S; py++)
            {
                for (int px = 0; px < S; px++)
                {
                    float aAcc = 0, rAcc = 0, gAcc = 0, bAcc = 0;
                    for (int sy = 0; sy < SS; sy++)
                    {
                        for (int sx = 0; sx < SS; sx++)
                        {
                            float x = (px + (sx + 0.5f) / SS) * D / S;
                            float y = (py + (sy + 0.5f) / SS) * D / S;

                            /* campo di ricerca arrotondato, bianco con
                             * sfumatura verticale 255->232 */
                            float dBar = SdRoundRect(x, y, 24f, 24f, 20f, 7f, 7f);
                            float aBar = Clamp01(0.5f - dBar);
                            float t = Clamp01((y - 17f) / 14f);
                            float shade = 255f - 23f * t;

                            /* lente: anello + manico color ardesia */
                            float ddx = x - 35.5f, ddy = y - 24f;
                            float rr = MathF.Sqrt(ddx * ddx + ddy * ddy);
                            float aRing = Clamp01(0.5f + (4.6f - rr)) *
                                          Clamp01(0.5f + (rr - 2.8f));
                            float dH = SdSegment(x, y, 38.4f, 27.0f, 41.4f, 30.0f) - 1.3f;
                            float aHandle = Clamp01(0.5f - dH);
                            float aLens = Math.Max(aRing, aHandle) * aBar;

                            float r = shade * (1 - aLens) + 75f * aLens;
                            float g = shade * (1 - aLens) + 95f * aLens;
                            float b = 255f * (1 - aLens) + 120f * aLens;

                            aAcc += aBar; rAcc += r; gAcc += g; bAcc += b;
                        }
                    }
                    int n = SS * SS;
                    int o = (py * S + px) * 4;
                    byte aB = (byte)Math.Min(255, Math.Round(255f * aAcc / n));
                    big[o + 0] = (byte)Math.Min(255, Math.Round(bAcc / n));
                    big[o + 1] = (byte)Math.Min(255, Math.Round(gAcc / n));
                    big[o + 2] = (byte)Math.Min(255, Math.Round(rAcc / n));
                    big[o + 3] = aB;
                }
            }
            return BicubicScaleBGRA(big, S, S, outSize, outSize);
        }

        /// <summary>v2.42: approccio ExplorerPatcher al flyout batteria: la
        /// chiave ImmersiveShell "UseWin32BatteryFlyout" (DWORD) fa usare a
        /// Windows il flyout Win32 classico (stile Windows 7) invece di
        /// quello XAML. La scriviamo in HKCU quando l'opzione e' attiva e
        /// la riportiamo a 0 quando viene disattivata.</summary>
        internal static void EnsureWin32BatteryFlyoutReg(bool enable)
            => WriteImmersiveShellValue("UseWin32BatteryFlyout", enable ? 1 : 0);

        /// <summary>
        /// v2.63 - Una chiave della shell, scritta in un posto solo.
        ///
        /// Sono le stesse tre che usa ExplorerPatcher per far scegliere a
        /// Windows il riquadro di Windows 7 o quello moderno:
        ///   UseWin32TrayClockExperience  1 = orologio classico (Aero)
        ///   UseWin32BatteryFlyout        1 = riquadro batteria Win32 di Win7
        ///   EnableMtcUvc                 0 = mixer volume classico
        /// Explorer le legge quando disegna i SUOI riquadri: scriverle tiene
        /// d'accordo la barra nativa con la nostra scelta (e' anche il modo
        /// in cui il clic sulla batteria ricreata puo' aprire il riquadro
        /// VERO di Windows 7, come chiesto).
        /// </summary>
        internal static void WriteImmersiveShellValue(string name, int value)
        {
            try
            {
                using var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(
                    @"SOFTWARE\Microsoft\Windows\CurrentVersion\ImmersiveShell");
                key?.SetValue(name, value, Microsoft.Win32.RegistryValueKind.DWord);
                _lastShellPrefsError = null;
            }
            catch (Exception ex)
            {
                _lastShellPrefsError = ex.Message;
            }
        }

        private static string? _lastShellPrefsError;

        /// <summary>
        /// v2.63 - LE QUATTRO SCELTE, APPLICATE ALL'AVVIO E A OGNI APPLICA.
        ///
        /// Era qui il difetto di fondo della segnalazione "il programma non
        /// legge bene le impostazioni all'avvio": la scelta dei riquadri
        /// veniva scritta nel registro (e comunicata al core) SOLO quando
        /// l'utente premeva OK o Applica nella finestra Proprieta'. Se la
        /// barra partiva con le impostazioni gia' salvate, il registro
        /// restava com'era e il core non sapeva nulla: il clic apriva il
        /// riquadro sbagliato anche con l'opzione giusta selezionata.
        ///
        /// Ora la lettura della configurazione produce QUESTA chiamata, una
        /// volta sola, all'avvio e a ogni applicazione. Le quattro decisioni
        /// arrivano al core con W7T_SetFlyoutPreferences e le tre chiavi di
        /// sistema vengono allineate: da qui in poi ogni percorso di apertura
        /// (icone ricreate, menu della barra, clic sintetici) usa la stessa
        /// decisione, quindi la tendina "Windows 7" apre il riquadro di
        /// Windows 7 e quella "Windows 10/11" apre quello della shell.
        /// </summary>
        internal void ApplyShellFlyoutPreferences()
        {
            try
            {
                var st = RetroBar.Utilities.Settings.Instance;

                /* Significato di ogni scelta (le stesse parole delle tendine):
                 *   Windows 7       = 1  -> orologio: Aero (ClockFlyoutWindow)
                 *                           rete: riquadro ricreato
                 *                           volume: SndVol
                 *                           batteria: riquadro Win32 di Windows
                 *   Windows 10/11   = 0  -> riquadro della shell               */
                bool clockWin7   = st.UseNativeClockFlyout;      /* tendina: "Windows 7" */
                /* v3.8: la rete ha tre scelte: 1 = Win7 ricreato, 0 =
                 * Windows 10/11 (sistema), 2 = Win8 ricreato. */
                int networkStyle = st.NetworkFlyoutMode == 0 ? 1
                                 : (st.NetworkFlyoutMode == 2 ? 2 : 0);
                bool volumeWin7  = st.UseClassicVolumeMixer;     /* tendina: "Windows 7" */
                bool batteryWin7 = st.UseBatteryFlyout;          /* tendina: "Windows 7" */

                WriteImmersiveShellValue("UseWin32TrayClockExperience", clockWin7 ? 1 : 0);
                WriteImmersiveShellValue("UseWin32BatteryFlyout", batteryWin7 ? 1 : 0);
                WriteImmersiveShellValue("EnableMtcUvc", volumeWin7 ? 0 : 1);

                _bridge.SetFlyoutPreferences(clockWin7, networkStyle, volumeWin7, batteryWin7);

                /* v1.21.7: the extra settings follow the same principle - the
                 * configuration read here IS the decision. The privacy mode
                 * goes to the recreated connection flyouts, the colour to the
                 * recreated Windows 8 one (both of them are part of this
                 * build): see ApplyExtraSettings. */
                ApplyExtraSettings();

                string modern = "?";
                try { modern = _bridge.IsModernFlyoutHostAvailable() ? "si" : "no"; }
                catch { }

                string netStyleName = networkStyle == 1 ? "Windows7"
                                    : (networkStyle == 2 ? "Windows8" : "Windows10/11");
                _bridge.Log(
                    "SETTINGS: orologio=" + (clockWin7 ? "Windows7" : "Windows10/11") +
                    " rete=" + netStyleName +
                    " volume=" + (volumeWin7 ? "Windows7" : "Windows10/11") +
                    " batteria=" + (batteryWin7 ? "Windows7" : "Windows10/11") +
                    " lingua=" + (st.Language ?? Settings.DefaultLanguageCode) +
                    " secondi=" + (st.ShowClockSeconds ? 1 : 0) +
                    " ricerca=" + (st.EnableAppSearch ? 1 : 0) +
                    " riquadri-moderni=" + modern +
                    (_lastShellPrefsError != null ? " registro-errore=" + _lastShellPrefsError : ""));
            }
            catch (Exception ex)
            {
                _bridge.Log("SETTINGS: applicazione preferenze riquadri fallita: " + ex.Message);
            }
        }

        /// <summary>
        /// v1.21.7 - publishes the extra settings to the native core
        /// (W7T_SetExtraSettings).
        ///
        /// This is the single publication point, called at startup (inside
        /// ApplyShellFlyoutPreferences) and after every OK/Apply of the
        /// Properties window: the saved configuration and what the core
        /// applies can therefore never diverge, as with the flyout
        /// preferences.
        ///
        /// Effects, as required:
        ///   - privacy -> changes ONLY the text drawn by the recreated
        ///                connection flyout. No network API, no Windows
        ///                setting.
        ///   - colour  -> drawn by the recreated Windows 8 flyout when it is
        ///                the active one ("system colour" is the accent read
        ///                from Windows every time it paints). No Windows 7
        ///                flyout changes its look and nothing in Windows is
        ///                written.
        ///   - skin    -> the only loadable skin is still Windows 7: the
        ///                choice reaches ThemeLoader at startup (see
        ///                App.xaml.cs).
        /// </summary>
        internal void ApplyExtraSettings()
        {
            try
            {
                var st = RetroBar.Utilities.Settings.Instance;

                int colorMode = st.FlyoutColorMode == 1 ? 1 : 0;
                int colorRgb = st.FlyoutCustomColorRgb;
                int privacyMode = st.ConnectionFlyoutPrivacyMode == 1 ? 1 : 0;

                _bridge.SetExtraSettings(colorMode, colorRgb, privacyMode);

                _bridge.Log(
                    "SETTINGS-EXTRA: colore-flyout=" +
                    (colorMode == 1
                        ? "personalizzato #" + colorRgb.ToString("X6")
                        : "sistema") +
                    " privacy=" + (privacyMode == 1 ? "on" : "off") +
                    " tema=" + (st.ThemeSelection == 0 ? "Windows7" : "Windows8.1") +
                    " ordine-icone=" + st.TaskbarIconOrder.Count);
            }
            catch (Exception ex)
            {
                _bridge.Log("SETTINGS-EXTRA: pubblicazione fallita: " + ex.Message);
            }
        }

        private void UpdateSearchButtonVisibility()
        {
            try
            {
                // v3.3: la lente vive solo mentre gira la nostra taskbar
                // (e solo se la ricerca e' attiva dalle Proprieta').
                SearchButton.Visibility =
                    RetroBar.Utilities.Settings.Instance.EnableAppSearch
                        ? Visibility.Visible
                        : Visibility.Collapsed;
            }
            catch (Exception) { /* ignora */ }
        }

        /// <summary>
        /// v3.5: l'indicatore della lingua di input. Lo stile arriva dalle
        /// Proprieta' (0 nascosta, 1 Windows 7, 2 Windows 8.1, 3 Windows
        /// 10/11); il menu di scelta lingue lo apre il core nativo, come le
        /// altre voci della barra.
        /// </summary>
        private void ApplyInputLanguageMode()
        {
            try
            {
                /* v1.5: ApplyMode e' incondizionata: anche quando il valore
                 * non cambia (il caso che teneva la voce invisibile) la
                 * visibilita' e il layout vengono riapplicati. La sigla e'
                 * disegnata dal controllo (che interroga il core nativo);
                 * il click apre il popup nativo del selettore (port del
                 * mod), non un menu WPF. */
                LanguageBar.ApplyMode(
                    RetroBar.Utilities.Settings.Instance.InputLanguageMode);
            }
            catch (Exception) { /* ignora */ }
        }

        private void SearchButton_Click(object sender, RoutedEventArgs e)
        {
            // v2.37 punto 17: la lente funziona da interruttore. Se la
            // finestra di ricerca e' gia' aperta, un click la chiude;
            // altrimenti la apre (comportamento richiesto).
            try
            {
                if (_appSearchInit && _bridge.AppSearchIsVisible())
                {
                    _bridge.AppSearchHide();
                    return;
                }
            }
            catch (Exception ex)
            {
                _bridge.Log($"ricerca: toggle, controllo visibilita' fallito: {ex.Message}");
            }

            OpenAppSearch();
        }

        internal void OpenAppSearch()
        {
            if (!RetroBar.Utilities.Settings.Instance.EnableAppSearch)
            {
                return;
            }

            IntPtr hwnd = _hwndSource?.Handle ?? IntPtr.Zero;
            if (hwnd == IntPtr.Zero)
            {
                return;
            }

            if (!_appSearchInit)
            {
                _appSearchInit = _bridge.AppSearchInit(hwnd, _searchIconPixels,
                                                       _searchIconW, _searchIconH);
                if (!_appSearchInit)
                {
                    return;
                }
            }

            Point tl = StartButton.PointToScreen(new Point(0, 0));
            /* v1.21.30: la ricerca usa la skin del tema attivo
             * (0 Win7 blu, 1 Win8.1 metro viola). */
            _bridge.AppSearchShow((int)tl.X, (int)tl.Y,
                RetroBar.Utilities.Settings.Instance.ThemeSelection);
        }

        /// <summary>v3.3: Proprietà = vera finestra Win32 nel core nativo
        /// (schede classiche come la mod di riferimento). I valori tornano
        /// indietro via WM_COPYDATA (vedi WndProc).</summary>
        internal void ShowPropertiesWindow()
        {
            try
            {
                IntPtr hwnd = _hwndSource?.Handle ?? IntPtr.Zero;
                if (hwnd == IntPtr.Zero) return;
                var st = RetroBar.Utilities.Settings.Instance;
                GetToolbarStates(out bool tbDesktop, out bool tbLinks, out bool tbAddress);
                _bridge.PropertiesShow(hwnd,
                    Math.Max(0, Array.IndexOf(kLangCodes,
                        st.Language ?? RetroBar.Utilities.Settings.DefaultLanguageCode)),
                    st.ShowClockSeconds ? 1 : 0,
                    st.UseNativeClockFlyout ? 1 : 0,
                    st.EnableAppSearch ? 1 : 0,
                    st.NetworkFlyoutMode,
                    st.UseClassicVolumeMixer ? 1 : 0,
                    st.UseBatteryFlyout ? 1 : 0,
                    st.AeroPeek ? 1 : 0,
                    tbDesktop ? 1 : 0,
                    tbAddress ? 1 : 0,
                    tbLinks ? 1 : 0,
                    st.InputLanguageMode,
                    st.TaskManagerMode,
                    /* v1.21.7: extra settings (fields appended at the end). */
                    st.FlyoutColorMode,
                    st.FlyoutCustomColorRgb,
                    st.ConnectionFlyoutPrivacyMode,
                    st.ThemeSelection,
                    /* v1.21.37: stato corrente dell'avvio automatico con
                     * Windows, letto dal registro con la logica di RetroBar
                     * (LoadAutoStart), per la casella della scheda
                     * Informazioni. */
                    Win7Taskbar.Utilities.AutoStart.IsEnabled() ? 1 : 0);
            }
            catch (Exception ex)
            {
                _bridge.Log($"proprieta': errore apertura: {ex.Message}");
            }
        }
    }
}
