// Win7Taskbar - main taskbar window
// English: Main window handling taskbar logic, start button states, flyout anchoring, clock, tray, battery monitoring
// Italiano: Finestra principale della barra
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

using RetroBar.Utilities;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Windows.Data;
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
        private bool _appBarRegistered;
        private bool _shuttingDown;

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
            HasOverflowIcons = _viewModel.NotificationArea.UnpinnedIcons.Count > 0;

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

        private void TaskButton_DragOver(object sender, DragEventArgs e)
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
                ? DragDropEffects.Link
                : DragDropEffects.None;
            e.Handled = true;
        }

        private void TaskButton_Drop(object sender, DragEventArgs e)
        {
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
                _balloonHost.Show(balloon.Title, balloon.Text,
                                  balloon.InfoFlags, balloon.Timeout);
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
            int sizePx = (int)Math.Round(ThemeTaskbarHeightDip * scale);

            _appBarRegistered = _bridge.RegisterAppBar(
                _hwndSource.Handle, AppBarEdgeValue.Bottom, sizePx);
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
                st.NetworkFlyoutMode = netFlyout;
                st.UseClassicVolumeMixer = classicVolume == 1;
                st.UseBatteryFlyout = batteryFlyout == 1;
                st.AeroPeek = aeroPeek == 1;
                if (hasToolbars)
                {
                    /* Le caselle della scheda "Barre degli strumenti" sono le
                     * stesse barre che si accendono dal menu contestuale della
                     * barra: qui si applica e si salva la scelta. */
                    SetToolbarStates(tbDesktop == 1, tbLinks == 1, tbAddress == 1);
                }
                /* v2.42: approccio ExplorerPatcher al flyout batteria. */
                EnsureWin32BatteryFlyoutReg(st.UseBatteryFlyout);
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

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
        {
            const int WM_DISPLAYCHANGE = 0x007E;
            const int WM_DPICHANGED = 0x02E0;
            const int WM_MOUSEACTIVATE = 0x0021;
            const int WM_COPYDATA = 0x004A;
            const int WM_SIZE = 0x0005;
            const int WM_SYSCOMMAND = 0x0112;
            const int SC_MINIMIZE = 0xF020;
            const int SIZE_MINIMIZED = 1;
            const int SW_SHOWNOACTIVATE = 4;
            const int MA_ACTIVATE = 1;

            switch (msg)
            {
                case WM_COPYDATA:
                    handled = HandlePropsCopyData(lParam);
                    return IntPtr.Zero;
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
                    OverflowPopup.IsOpen = false;
                    _bridge.ReanchorFlyouts();
                    UpdateDpiScaling();
                    PositionOnScreen();
                    _bridge.ReassertNativeTaskbarHidden();
                    if (_appBarRegistered && _hwndSource != null)
                    {
                        double scale = _hwndSource.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
                        _bridge.SetAppBarPos(
                            _hwndSource.Handle,
                            AppBarEdgeValue.Bottom,
                            (int)Math.Round(ThemeTaskbarHeightDip * scale),
                            out _);
                    }
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

            // v2.40: arma il riconoscimento del trascinamento verso l'alto
            // (apre la Jump List); un clic senza movimento resta un clic.
            _taskDragStart = e.GetPosition(null);
            _taskDragArmed = true;
            _taskDragOpened = false;
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
            // v2.40: se il "clic" e' in realta' la coda del trascinamento che
            // ha aperto la Jump List, non eseguire l'azione di clic.
            if (_taskDragOpened)
            {
                _taskDragOpened = false;
                e.Handled = true;
                return;
            }

            if (sender is not FrameworkElement element || element.DataContext is not TaskGroup group)
            {
                return;
            }

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
        /* ================================================================
         * v2.56 - WINDOW PREVIEWS ARE TEMPORARILY DISABLED.
         *
         * Both preview implementations produced unwanted rectangles inside
         * the popup (an empty grey/white box, the same for every window), so
         * the feature is parked instead of shipped half-broken: the popup is
         * never opened, hovering a task button shows only the app-name
         * tooltip, which is the part that is guaranteed to work.
         *
         * This is the single switch for the whole feature. Everything else
         * (the popup, the item template, the close button, the placement
         * callback) is still in place and untouched: turning this to true is
         * the only change needed to bring the previews back, once the
         * rendering has been properly reimplemented (see
         * Controls/TaskThumbnail.cs for what has to be proven first and the
         * README for the user-facing statement).
         * ================================================================ */
        // A static readonly field (not a const) on purpose: a compile-time
        // constant would make the rest of ShowTaskPreview unreachable code.
        private static readonly bool TaskPreviewsEnabled = false;

        private const int PreviewShowDelayMs = 400;

        /// <summary>Ogni quanto si controlla se il mouse e' ancora sul
        /// pulsante o sull'anteprima.</summary>
        private const int PreviewWatchIntervalMs = 200;

        /// <summary>Distacco fra barra e anteprima (come gli altri riquadri).</summary>
        private const int PreviewGapPx = 4;

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

                /* Il mouse arriva da un altro pulsante: l'anteprima di quello
                 * non serve piu' (nella Superbar se ne vede una sola per
                 * volta). */
                if (!ReferenceEquals(_previewAnchor, button))
                {
                    CloseTaskPreview();
                }

                if (group.Windows.Count == 0)
                {
                    return;   /* app non avviata: nessuna anteprima */
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

            try
            {
                /* Chiusura e riapertura: e' il modo affidabile per far
                 * riposizionare il popup quando cambia il pulsante sotto il
                 * mouse (spostare il PlacementTarget di un popup gia' aperto
                 * non lo fa spostare). Le miniature vengono ricreate e ogni
                 * controllo TaskThumbnail fa la sua cattura alla Loaded
                 * (v2.55: cattura statica, non piu' thumbnail DWM), quindi
                 * non resta nessuna risorsa orfana (la chiusura azzera
                 * l'ItemsSource, vedi TaskPreviewPopup_Closed). */
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

            double dx = (targetSize.Width - popupSize.Width) / 2.0;
            double dy;

            switch (edge)
            {
                case TaskbarEdge.Top:
                    dy = targetSize.Height + PreviewGapPx;
                    break;

                case TaskbarEdge.Left:
                    dx = targetSize.Width + PreviewGapPx;
                    dy = (targetSize.Height - popupSize.Height) / 2.0;
                    break;

                case TaskbarEdge.Right:
                    dx = -popupSize.Width - PreviewGapPx;
                    dy = (targetSize.Height - popupSize.Height) / 2.0;
                    break;

                default:   /* barra in basso: anteprima SOPRA il pulsante */
                    dy = -popupSize.Height - PreviewGapPx;
                    break;
            }

            return new[]
            {
                new CustomPopupPlacement(new Point(dx, dy), PopupPrimaryAxis.Horizontal)
            };
        }

        private void PreviewWatchTimer_Tick(object? sender, EventArgs e)
        {
            try
            {
                if (TaskPreviewPopup?.IsOpen != true)
                {
                    _previewWatchTimer?.Stop();
                    return;
                }

                bool overAnchor = _previewAnchor?.IsMouseOver == true;
                bool overPopup = TaskPreviewPopup.Child is FrameworkElement child &&
                                 child.IsMouseOver;


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

        private void TaskPreviewPopup_Opened(object? sender, EventArgs e)
        {
            try
            {
                /* v2.45: punto unico di controllo dopo che il popup e' davvero a
                 * schermo. v2.50: non c'e' piu' nessun fondo da preparare (il
                 * popup ha una tinta piena del tema): qui si riavvia solo il
                 * timer che decide la chiusura, cosi' la permanenza non
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
                _previewAnchor = null;
                _previewGroup = null;
                _openButtonTip = null;

                /* Sgancia le miniature: senza questo i controlli TaskThumbnail
                 * (e le immagini catturate che tengono in memoria) resterebbero
                 * vivi anche a popup chiuso. */
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

        /// <summary>
        /// Clic sulla miniatura: la finestra va DAVVERO in primo piano, come
        /// nella Superbar di Windows 7 (che, a differenza di Windows 10/11,
        /// non ha il pulsante "anteprima" separato: si clicca la miniatura).
        /// </summary>
        private void PreviewThumbnail_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            try
            {
                if (sender is not FrameworkElement element ||
                    element.DataContext is not TaskWindow window)
                {
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
        // verso l'alto del clic SINISTRO (vedi TaskButton_MouseMove), come fa
        // la Superbar originale: cosi' il destro non perde mai "Chiudi" & co.
        private void TaskButton_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            if (sender is not FrameworkElement element || element.DataContext is not TaskGroup group)
            {
                return;
            }

            e.Handled = true;

            Point origin;
            try { origin = element.PointToScreen(new Point(0, element.ActualHeight)); }
            catch { origin = element.PointToScreen(new Point(0, 0)); }
            int x = (int)Math.Round(origin.X);
            int y = (int)Math.Round(origin.Y);

            if (group.Windows.Count == 0)
            {
                // Pin non avviato: avvia / fissa-rimuovi.
                string pinText = group.IsPinned
                    ? L("lang_menu_unpin",
                        "Unpin this program from taskbar")
                    : L("lang_menu_pin",
                        "Pin this program to taskbar");
                int choice = _bridge.ShowContextMenu(
                    x, y, bottomEdge: true,
                    L("lang_start_tip", "Start"), pinText);
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
                        dynamic sh = Activator.CreateInstance(t);
                        var sc = sh.CreateShortcut(lnk);
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

        // v2.40: trascinamento verso l'alto col clic sinistro = Jump List,
        // come la Superbar di Windows 7. Un semplice clic resta un clic.
        private Point _taskDragStart;
        private bool _taskDragArmed;
        private bool _taskDragOpened;

        private void TaskButton_MouseMove(object sender, MouseEventArgs e)
        {
            if (sender is not FrameworkElement element || element.DataContext is not TaskGroup group)
                return;

            if (e.LeftButton != MouseButtonState.Pressed)
            {
                _taskDragArmed = false;
                return;
            }

            if (!_taskDragArmed) return;

            Point pos = e.GetPosition(null);
            Vector moved = pos - _taskDragStart;
            // Solo verso l'alto, oltre la soglia di drag di sistema.
            if (moved.Y < -SystemParameters.MinimumVerticalDragDistance &&
                Math.Abs(moved.Y) > Math.Abs(moved.X))
            {
                _taskDragArmed = false;
                _taskDragOpened = true;
                ShowTaskButtonJumpList(element, group);
            }
        }

        private void TaskButton_MouseLeftButtonUpReset(object sender, MouseButtonEventArgs e)
        {
            _taskDragArmed = false;
        }

        /// <summary>v2.38: converte un ImageSource in pixel BGRA dritti
        /// (non premoltiplicati) top-down, il formato che il nativo
        /// MakeHBitmapFromArgb si aspetta. null se l'icona non e'
        /// disponibile.</summary>
        private static uint[]? ExtractBgra32(ImageSource? source,
            out int width, out int height)
        {
            width = 0;
            height = 0;
            if (source == null) return null;
            try
            {
                if (source is not System.Windows.Media.Imaging.BitmapSource bmp)
                {
                    return null;
                }
                if (bmp.Format != System.Windows.Media.PixelFormats.Bgra32)
                {
                    bmp = new System.Windows.Media.Imaging.FormatConvertedBitmap(
                        bmp, System.Windows.Media.PixelFormats.Bgra32, null, 0);
                }
                width = bmp.PixelWidth;
                height = bmp.PixelHeight;
                if (width <= 0 || height <= 0)
                {
                    width = 0;
                    height = 0;
                    return null;
                }
                var bytes = new byte[width * height * 4];
                bmp.CopyPixels(bytes, width * 4, 0);
                var pixels = new uint[width * height];
                Buffer.BlockCopy(bytes, 0, pixels, 0, bytes.Length);
                return pixels;
            }
            catch
            {
                width = 0;
                height = 0;
                return null;
            }
        }

        /// <summary>v2.38: apre la Jump List nativa ancorata al rettangolo del
        /// pulsante. Il nativo gestisce: riga app (nuova istanza), pin/unpin;
        //  la sezione recenti e' disattivata in questa versione.</summary>
        private void ShowTaskButtonJumpList(FrameworkElement element, TaskGroup group)
        {
            try
            {
                double scale = _hwndSource?.CompositionTarget?.TransformToDevice.M11 ?? 1.0;
                if (scale <= 0) scale = 1.0;

                Point tl = element.PointToScreen(new Point(0, 0));
                int left = (int)Math.Round(tl.X * scale);
                int top = (int)Math.Round(tl.Y * scale);
                int right = left + (int)Math.Round(element.ActualWidth * scale);
                int bottom = top + (int)Math.Round(element.ActualHeight * scale);

                // Nome dell'applicazione (senza estensione) come Windows 7.
                string title = group.AppId;
                if (!string.IsNullOrEmpty(group.ExePath))
                {
                    try { title = System.IO.Path.GetFileNameWithoutExtension(group.ExePath); }
                    catch (ArgumentException) { title = group.AppId; }
                }
                if (string.IsNullOrWhiteSpace(title))
                {
                    title = group.DisplayTitle;
                }

                string launchPath = !string.IsNullOrEmpty(group.LaunchPath)
                    ? group.LaunchPath
                    : (group.ExePath ?? string.Empty);
                string pinnedLnk = group.LaunchPath ?? string.Empty;

                uint[]? icon = ExtractBgra32(group.Icon, out int iconW, out int iconH);

                int lang = Math.Max(0, Array.IndexOf(kLangCodes,
                    RetroBar.Utilities.Settings.Instance.Language ?? RetroBar.Utilities.Settings.DefaultLanguageCode));

                _bridge.JumpListShow(left, top, right, bottom, title,
                    launchPath, pinnedLnk, group.IsPinned, icon, iconW, iconH, lang);
            }
            catch (Exception ex)
            {
                _bridge.Log($"jump list: {ex.Message}");
            }
        }

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

            return fallback;
        }

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

                // Cattura subito sull'elemento: niente ciclo OLE, il tracking
                // del mouse resta sul thread UI come in TrayUI::WndProc reale.
                element.CaptureMouse();
                element.PreviewMouseMove += TrayIcon_CapturedMouseMove;
                element.PreviewMouseLeftButtonUp += TrayIcon_CapturedMouseUp;
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
            if (sender is FrameworkElement element)
            {
                element.ReleaseMouseCapture();
                element.PreviewMouseMove -= TrayIcon_CapturedMouseMove;
                element.PreviewMouseLeftButtonUp -= TrayIcon_CapturedMouseUp;
            }

            if (_trayDragging && _trayDragCandidate != null)
            {
                CommitTrayDrag();
                _trayDragJustFinished = true;
            }

            _trayDragging = false;
            _trayDragCandidate = null;
            _trayDragElement = null;
            HideTrayDragGhost();   // v3.1
            ClearTrayDropAdorner();
        }

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
                    Point ttl = OverflowToggle.PointToScreen(new Point(0, 0));
                    _overflowMouseHook.ExcludeRect2 = new Rect(
                        ttl.X * scale, ttl.Y * scale,
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

                Point tl = child.PointToScreen(new Point(0, 0));
                Rect popupRect = new Rect(tl.X * scale, tl.Y * scale,
                                          child.ActualWidth * scale,
                                          child.ActualHeight * scale);

                Rect toggleRect = Rect.Empty;
                if (OverflowToggle != null)
                {
                    Point ttl = OverflowToggle.PointToScreen(new Point(0, 0));
                    toggleRect = new Rect(ttl.X * scale, ttl.Y * scale,
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
            if (RetroBar.Utilities.Settings.Instance.NetworkFlyoutMode == 0 &&
                IsNetworkTrayIcon(icon))
            {
                if (!_netFlyoutInit)
                {
                    _netFlyoutInit = _bridge.NetFlyoutInit();
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
                    OpenNotificationAreaIconsApplet();
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

            /* v2.61 - UN SOLO RIQUADRO, E QUELLO DI WINDOWS 7.
             *
             * Su Windows 11 il riquadro nativo del calendario e' un'isola
             * XAML che la shell apre quando decide lei: chiedendolo, il
             * nativo compariva prima del nostro e i due si sovrapponevano.
             * Li' quindi il percorso nativo non si usa piu': il calendario
             * di Windows 7 (gia' creato e tenuto in memoria, vedi
             * EnsureCalendarFlyout) si limita a comparire. Su Windows 10 e
             * precedenti l'impostazione "riquadro nativo" resta valida e
             * funzionante. */
            if (Settings.Instance.UseNativeClockFlyout && !IsWindows11Host())
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

            OpenNotificationAreaIconsApplet();
        }

        private void OpenNotificationAreaIconsApplet()
        {
            // 1) Meccanismo nativo del core (ShellExecuteEx sul namespace).
            try
            {
                if (_bridge.OpenNotificationIconsSettings())
                {
                    return;
                }
                Debug.WriteLine("Apertura nativa dell'applet icone rifiutata dalla shell: ripiego 1");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"Apertura nativa dell'applet icone non riuscita: {ex.Message}");
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
                Debug.WriteLine($"Apertura applet icone non riuscita: {ex.Message}");
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

        /// <summary>Applica la posizione voluta (in unita' del Popup).</summary>
        private void PlaceCalendarFlyout()
        {
            if (_calendarPopup == null ||
                !TryGetCalendarFlyoutTarget(out int x, out int y))
            {
                return;
            }

            double scale = GetDpiScale(_calendarPopup.Child);
            _calendarPopup.HorizontalOffset = x / scale;
            _calendarPopup.VerticalOffset = y / scale;
        }

        /// <summary>
        /// v2.62: dopo l'apertura si controlla DOVE il riquadro e' finito
        /// davvero e, se non e' dove deve stare, lo si sposta della
        /// differenza. Serve perche' la conversione fra unita' del Popup e
        /// pixel dipende dal DPI del monitor e dal contesto in cui la finestra
        /// del riquadro viene creata: misurarlo e' piu' affidabile che
        /// calcolarlo. Si ripete al massimo due volte, poi si lascia stare.
        /// </summary>
        private void CorrectCalendarPlacement(int attempt = 0)
        {
            if (_calendarPopup?.Child is not FrameworkElement child ||
                !_calendarPopup.IsOpen ||
                !TryGetCalendarFlyoutTarget(out int wantedX, out int wantedY))
            {
                return;
            }

            double actualX, actualY;
            try
            {
                Point screen = child.PointToScreen(new Point(0, 0));
                actualX = screen.X;
                actualY = screen.Y;
            }
            catch (InvalidOperationException)
            {
                return;
            }

            double dx = wantedX - actualX;
            double dy = wantedY - actualY;
            if (Math.Abs(dx) < 1 && Math.Abs(dy) < 1)
            {
                return;
            }

            double scale = GetDpiScale(child);
            _calendarPopup.HorizontalOffset += dx / scale;
            _calendarPopup.VerticalOffset += dy / scale;

            if (attempt < 2)
            {
                Dispatcher.BeginInvoke(DispatcherPriority.Loaded,
                    new Action(() => CorrectCalendarPlacement(attempt + 1)));
            }
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
        /// Il file Resources/win7search.png resta come ripiego, cosi' una
        /// risorsa incorporata illeggibile non lascia il pulsante vuoto.</summary>
        private void LoadSearchIconPixels()
        {
            try
            {
                if (!TryLoadSearchPixelsFromEmbedded() && !TryLoadSearchPixelsFromFile())
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
            try
            {
                string path = System.IO.Path.Combine(
                    AppContext.BaseDirectory, "Resources", "win7search.png");
                if (!System.IO.File.Exists(path))
                {
                    return false;
                }

                var bmp = new System.Windows.Media.Imaging.BitmapImage();
                bmp.BeginInit();
                bmp.UriSource = new Uri(path);
                bmp.CacheOption = System.Windows.Media.Imaging.BitmapCacheOption.OnLoad;
                bmp.EndInit();
                return TryCopySearchPixels(bmp);
            }
            catch (Exception ex)
            {
                _bridge.Log($"lente ricerca (file): {ex.Message}");
                return false;
            }
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
        {
            try
            {
                using var key = Microsoft.Win32.Registry.CurrentUser.CreateSubKey(
                    @"SOFTWARE\Microsoft\Windows\CurrentVersion\ImmersiveShell");
                key?.SetValue("UseWin32BatteryFlyout", enable ? 1 : 0,
                              Microsoft.Win32.RegistryValueKind.DWord);
            }
            catch (Exception) { /* senza registro: nessun flyout classico */ }
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
            _bridge.AppSearchShow((int)tl.X, (int)tl.Y);
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
                    tbLinks ? 1 : 0);
            }
            catch (Exception ex)
            {
                _bridge.Log($"proprieta': errore apertura: {ex.Message}");
            }
        }
    }
}
