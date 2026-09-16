// Win7Taskbar - wrapper gestito sopra il core nativo
// Copyright (c) 2026 Win7Taskbar contributors
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

using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace Win7Taskbar.Interop
{
    /// <summary>
    /// Una notifica a fumetto inviata da un'applicazione con NIF_INFO.
    /// </summary>
    internal readonly struct BalloonNotification
    {
        public BalloonNotification(ulong ownerHwnd, uint uid, string title,
                                   string text, uint infoFlags, uint timeout)
        {
            OwnerHwnd = ownerHwnd;
            Uid = uid;
            Title = title;
            Text = text;
            InfoFlags = infoFlags;
            Timeout = timeout;
        }

        public ulong OwnerHwnd { get; }
        public uint Uid { get; }
        public string Title { get; }
        public string Text { get; }

        /// <summary>Flag NIIF_*: scelgono l'icona del fumetto.</summary>
        public uint InfoFlags { get; }

        /// <summary>Durata richiesta in millisecondi, 0 se non specificata.</summary>
        public uint Timeout { get; }
    }

    /// <summary>
    /// Facciata gestita del core C++. Incapsula il P/Invoke, mantiene viva la
    /// callback e pompa gli eventi sul thread UI.
    /// </summary>
    internal sealed class NativeBridge : IDisposable
    {
        // Il delegate va tenuto in un campo: se lo raccoglie il GC mentre il
        // nativo lo possiede ancora, il processo salta.
        private readonly W7TEventCallback _callback;
        private readonly DispatcherTimer _pumpTimer;
        private bool _initialized;
        private bool _disposed;

        public event EventHandler<CoreEventArgs>? CoreEventRaised;

        public NativeBridge()
        {
            _callback = OnNativeEvent;

            // Il core accoda gli eventi; qui li consegniamo al thread UI a
            // intervalli regolari, senza mai chiamare managed dal thread nativo.
            _pumpTimer = new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(100)
            };
        }

        public bool Initialize() => TryInitialize(out _);

        /// <summary>
        /// Inizializza il core nativo traducendo ogni possibile fallimento in
        /// un messaggio leggibile: il P/Invoke non restituisce semplicemente
        /// "falso", puo' esplodere in modi diversi a seconda di cosa manca, e
        /// ognuno di quei modi ha una causa e una soluzione diverse.
        /// </summary>
        public bool TryInitialize(out string? error)
        {
            error = null;

            if (_initialized)
            {
                return true;
            }

            int result;
            try
            {
                result = NativeMethods.W7T_Initialize(_callback);
            }
            catch (DllNotFoundException)
            {
                error = "Win7TaskbarCore.dll non è stata trovata.\n\n" +
                        "Deve stare nella stessa cartella di Win7Taskbar.exe. " +
                        "Se hai estratto lo ZIP, verifica di aver mantenuto tutti i file.";
                return false;
            }
            catch (BadImageFormatException)
            {
                error = "Win7TaskbarCore.dll non è compatibile con questo processo.\n\n" +
                        "L'eseguibile è a 64 bit: serve la DLL nativa x64 nella stessa " +
                        "cartella. Un antivirus o un file corrotto possono causarlo.";
                return false;
            }
            catch (EntryPointNotFoundException ex)
            {
                error = "La DLL nativa è di una versione diversa dall'eseguibile " +
                        $"(manca {ex.Message.Split('\n')[0]}).\n\n" +
                        "Sostituisci entrambi i file con quelli dello stesso pacchetto.";
                return false;
            }
            catch (Exception ex)
            {
                error = $"Inizializzazione del core nativo fallita: {ex.GetType().Name}: {ex.Message}";
                return false;
            }

            if (result != W7TResult.Ok && result != W7TResult.ErrAlreadyInit)
            {
                error = $"Il core nativo ha rifiutato l'inizializzazione (codice {result}).";
                return false;
            }

            _initialized = true;

            // Il timer di pompaggio gira per tutta la vita del programma: una
            // eccezione qui dentro, non gestita, terminerebbe il processo a
            // barra gia' visibile. Dopo tre errori di fila il timer si ferma da
            // solo invece di continuare a sollevare eccezioni.
            _pumpTimer.Tick += OnPumpTick;
            _pumpTimer.Start();
            return true;
        }

        private int _pumpFailures;

        private void OnPumpTick(object? sender, EventArgs e)
        {
            try
            {
                NativeMethods.W7T_PumpEvents(64);
                _pumpFailures = 0;
            }
            catch (Exception ex)
            {
                _pumpFailures++;
                Win7Taskbar.StartupGuard.Report(ex, $"W7T_PumpEvents (errore {_pumpFailures})");

                if (_pumpFailures >= 3)
                {
                    // Circuito aperto: meglio una barra che non aggiorna le
                    // icone di un processo che continua a cadere.
                    _pumpTimer.Stop();
                }
            }
        }

        public uint GetVersion() => NativeMethods.W7T_GetVersion();

        private void OnNativeEvent(int evt, ulong a, ulong b)
        {
            CoreEventRaised?.Invoke(this, new CoreEventArgs(evt, a, b));
        }

        // ---------------------------------------------------------------
        //  Finestre
        // ---------------------------------------------------------------

        /// <summary>v2.25: pin normalizzati dal core (fonte autoritativa).</summary>
        /// <summary>v2.28: avvio robusto (retry nativo). Se la shell
        /// rifiuta, il chiamante ripiega su Process.Start.</summary>
        public bool ShellOpen(string path)
        {
            try
            {
                return NativeMethods.W7T_ShellOpen(path);
            }
            catch
            {
                return false;
            }
        }

        public IReadOnlyList<NativeMethods.W7TPinnedInfo> GetPinnedApps()
        {
            try
            {
                int n = NativeMethods.W7T_GetPinnedCount();
                if (n <= 0) return Array.Empty<NativeMethods.W7TPinnedInfo>();
                var buffer = new NativeMethods.W7TPinnedInfo[n];
                int written = NativeMethods.W7T_GetPinnedApps(buffer, n);
                if (written < 0) return Array.Empty<NativeMethods.W7TPinnedInfo>();
                if (written < n) Array.Resize(ref buffer, written);
                return buffer;
            }
            catch
            {
                return Array.Empty<NativeMethods.W7TPinnedInfo>();
            }
        }

        public void PinnedRefresh()
        {
            try { NativeMethods.W7T_PinnedRefresh(); } catch { }
        }

        public IReadOnlyList<W7TWindowInfo> GetWindows()
        {
            NativeMethods.W7T_RefreshWindows();

            int count = NativeMethods.W7T_GetWindowCount();
            if (count <= 0)
            {
                return Array.Empty<W7TWindowInfo>();
            }

            // Margine sulla capacita': una finestra puo' apparire fra le due
            // chiamate e il core rifiuterebbe il buffer.
            var buffer = new W7TWindowInfo[count + 8];
            int written = NativeMethods.W7T_GetWindows(buffer, buffer.Length);
            if (written <= 0)
            {
                return Array.Empty<W7TWindowInfo>();
            }

            var result = new List<W7TWindowInfo>(written);
            for (int i = 0; i < written; i++)
            {
                result.Add(buffer[i]);
            }
            return result;
        }

        public ImageSource? GetWindowIcon(ulong hwnd, int desiredSize = 32)
        {
            int needed = NativeMethods.W7T_GetWindowIconBitmap(
                hwnd, desiredSize, out int width, out int height, null, 0);

            if (needed <= 0 || width <= 0 || height <= 0)
            {
                return null;
            }

            var pixels = new byte[needed];
            int copied = NativeMethods.W7T_GetWindowIconBitmap(
                hwnd, desiredSize, out width, out height, pixels, pixels.Length);

            return copied <= 0 ? null : CreateBitmap(pixels, width, height);
        }

        public void ExecuteCommand(ulong hwnd, int command)
            => NativeMethods.W7T_ExecuteWindowCommand(hwnd, command);

        public void MinimizeGroup(string appId) => NativeMethods.W7T_MinimizeGroup(appId);

        public void CloseGroup(string appId) => NativeMethods.W7T_CloseGroup(appId);

        public bool IsFullScreenAppActive() => NativeMethods.W7T_IsFullScreenAppActive() != 0;

        // ---------------------------------------------------------------
        //  Tray
        // ---------------------------------------------------------------

        public bool StartTray() => NativeMethods.W7T_TrayStart() == W7TResult.Ok;

        public void StopTray() => NativeMethods.W7T_TrayStop();

        public IReadOnlyList<W7TTrayIconInfo> GetTrayIcons()
        {
            int count = NativeMethods.W7T_GetTrayIconCount();
            if (count <= 0)
            {
                return Array.Empty<W7TTrayIconInfo>();
            }

            var buffer = new W7TTrayIconInfo[count + 8];
            int written = NativeMethods.W7T_GetTrayIcons(buffer, buffer.Length);
            if (written <= 0)
            {
                return Array.Empty<W7TTrayIconInfo>();
            }

            var result = new List<W7TTrayIconInfo>(written);
            for (int i = 0; i < written; i++)
            {
                result.Add(buffer[i]);
            }
            return result;
        }

        public ImageSource? GetTrayIcon(ulong ownerHwnd, uint uid)
        {
            int needed = NativeMethods.W7T_GetTrayIconBitmap(
                ownerHwnd, uid, out int width, out int height, null, 0);

            if (needed <= 0 || width <= 0 || height <= 0)
            {
                return null;
            }

            var pixels = new byte[needed];
            int copied = NativeMethods.W7T_GetTrayIconBitmap(
                ownerHwnd, uid, out width, out height, pixels, pixels.Length);

            return copied <= 0 ? null : CreateBitmap(pixels, width, height);
        }

        public void SendTrayClick(ulong ownerHwnd, uint uid, int clickType, int x, int y)
            => NativeMethods.W7T_SendTrayIconClick(ownerHwnd, uid, clickType, x, y);

        /// <summary>
        /// Recupera l'ultima notifica a fumetto ricevuta dal core.
        /// Restituisce false se non ce n'e' nessuna in attesa.
        /// </summary>
        public bool TryGetLastBalloon(out BalloonNotification balloon)
        {
            balloon = default;

            if (NativeMethods.W7T_GetLastBalloon(out W7TBalloonInfo info) != W7TResult.Ok)
            {
                return false;
            }

            balloon = new BalloonNotification(
                info.OwnerHwnd,
                info.Uid,
                info.Title ?? string.Empty,
                info.Text ?? string.Empty,
                info.InfoFlags,
                info.Timeout);

            return true;
        }

        public void SetTrayIconPinned(ulong ownerHwnd, uint uid, bool pinned)
            => NativeMethods.W7T_SetTrayIconPinned(ownerHwnd, uid, pinned ? 1 : 0);

        /// <summary>
        /// Propaga al core (e al ToolbarWindow32 reale del modello) il
        /// riordino fatto dall'utente col trascinamento: l'icona sorgente
        /// viene spostata prima o dopo il bersaglio con TB_MOVEBUTTON.
        /// </summary>
        public bool TrayMoveIcon(ulong sourceHwnd, uint sourceUid,
                                 ulong targetHwnd, uint targetUid, bool insertAfter)
            => NativeMethods.W7T_TrayMoveIcon(sourceHwnd, sourceUid, targetHwnd, targetUid,
                                              insertAfter ? 1 : 0) == W7TResult.Ok;

        // ---------------------------------------------------------------
        //  AppBar
        // ---------------------------------------------------------------

        public bool RegisterAppBar(IntPtr hwnd, int edge, int sizePx)
            => NativeMethods.W7T_AppBarRegister((ulong)hwnd.ToInt64(), edge, sizePx) == W7TResult.Ok;

        public bool SetAppBarPos(IntPtr hwnd, int edge, int sizePx, out Rect reserved)
        {
            int result = NativeMethods.W7T_AppBarSetPos(
                (ulong)hwnd.ToInt64(), edge, sizePx,
                out int left, out int top, out int right, out int bottom);

            reserved = result == W7TResult.Ok
                ? new Rect(left, top, Math.Max(0, right - left), Math.Max(0, bottom - top))
                : Rect.Empty;

            return result == W7TResult.Ok;
        }

        public void UnregisterAppBar(IntPtr hwnd)
            => NativeMethods.W7T_AppBarUnregister((ulong)hwnd.ToInt64());

        // v3.4: protocollo AppBar completo (flusso ManagedShell/RetroBar).
        // Il messaggio di callback e' lo stesso che il core ha passato ad
        // ABM_NEW: la finestra lo riceve nel WndProc e lo gira qui.
        // Ogni chiamata regge un core piu' vecchio (EntryPointNotFound):
        // con una DLL non allineata le novita' si spengono, la barra resta.

        /// <summary>Identificatore del messaggio di callback AppBar
        /// (0 se la registrazione non e' mai avvenuta o il core e' vecchio).</summary>
        public int AppBarCallbackMessage()
        {
            try { return NativeMethods.W7T_AppBarCallbackMessage(); }
            catch (EntryPointNotFoundException) { return 0; }
        }

        /// <summary>True mentre l'AppBar risulta registrato al core.</summary>
        public bool IsAppBarRegistered
        {
            get
            {
                try { return NativeMethods.W7T_AppBarIsRegistered() != 0; }
                catch (EntryPointNotFoundException) { return false; }
            }
        }

        /// <summary>Consegna al core una notifica ABN_*: gestisce il
        /// ricalcolo di QUERYPOS/SETPOS e lo spostamento della finestra.
        /// Restituisce true se la notifica era una di quelle gestite.</summary>
        public bool AppBarNotify(uint wParam, int lParam)
        {
            try { return NativeMethods.W7T_AppBarNotify(wParam, lParam) != 0; }
            catch (EntryPointNotFoundException) { return false; }
        }

        /// <summary>ABM_ACTIVATE: la barra e' stata attivata.</summary>
        public void AppBarActivate(IntPtr hwnd)
        {
            try { NativeMethods.W7T_AppBarActivate((ulong)hwnd.ToInt64()); }
            catch (EntryPointNotFoundException) { }
        }

        public void SetNativeTaskbarHidden(bool hidden)
            => NativeMethods.W7T_SetNativeTaskbarHidden(hidden ? 1 : 0);

        /// <summary>True se la taskbar di Explorer risulta nascosta da noi.</summary>
        public bool IsNativeTaskbarHidden()
            => NativeMethods.W7T_IsNativeTaskbarHidden() != 0;

        /// <summary>
        /// Rinasconde la taskbar di Explorer se e' ricomparsa. Da chiamare
        /// periodicamente: Explorer la rimostra da solo in varie occasioni.
        /// </summary>
        public void ReassertNativeTaskbarHidden()
            => NativeMethods.W7T_ReassertNativeTaskbarHidden();

        /// <summary>Comunica al core il rettangolo a schermo di un'icona:
        /// serve a chi chiama Shell_NotifyIconGetRect per ancorare i propri
        /// flyout (volume, rete...) sopra la propria icona.</summary>
        public void SetIconRect(ulong ownerHwnd, uint uid,
                                int left, int top, int right, int bottom)
            => NativeMethods.W7T_SetIconRect(ownerHwnd, uid, left, top, right, bottom);

        /// <summary>Sincronizza il rettangolo della nostra barra (e dell'area
        /// di notifica) sulle finestre fantasma Shell_TrayWnd/TrayNotifyWnd:
        /// i flyout che si ancorano li' (volume di Windows 7) altrimenti si
        /// aprono in alto a sinistra.</summary>
        public void SetShellRects(int barLeft, int barTop, int barRight, int barBottom,
                                  int notifyLeft, int notifyTop,
                                  int notifyRight, int notifyBottom)
            => NativeMethods.W7T_SetShellRects(barLeft, barTop, barRight, barBottom,
                                               notifyLeft, notifyTop,
                                               notifyRight, notifyBottom);

        /// <summary>Ri-ancora il flyout di sistema aperto sopra la sua icona
        /// (cambio DPI/monitor/posizione della barra).</summary>
        public void ReanchorFlyouts()
            => NativeMethods.W7T_ReanchorFlyouts();

        /// <summary>Rettangolo della freccetta dell'overflow: la shell lo usa
        /// come rettangolo delle icone nascoste (semantica S_FALSE di
        /// Shell_NotifyIconGetRect).</summary>
        public void SetChevronRect(int left, int top, int right, int bottom)
            => NativeMethods.W7T_SetChevronRect(left, top, right, bottom);

        /// <summary>
        /// Apre il calendario vero di Windows (Aero Clock).
        /// Restituisce false se il sistema non lo mette a disposizione.
        /// </summary>
        public bool ShowClockFlyout(IntPtr taskbarHwnd)
            => NativeMethods.W7T_ShowClockFlyout((ulong)taskbarHwnd.ToInt64()) == W7TResult.Ok;

        /// <summary>
        /// Menu contestuale nativo. Le voci sono separate da '\n'; "-" e' un
        /// separatore e il prefisso '!' rende la voce disabilitata.
        /// Restituisce l'indice della voce scelta partendo da 1, 0 se
        /// l'utente ha annullato.
        /// </summary>
        public int ShowContextMenu(int x, int y, bool bottomEdge, params string[] items)
            => NativeMethods.W7T_ShowContextMenu(x, y, bottomEdge ? 1 : 0,
                                                 string.Join("\n", items));

        /// <summary>v2.5: menu Win32 con sottomenu e spunte.
        /// v2.43: con anchorAtCursor il menu si apre esattamente sul punto
        /// (x, y) ricevuto - il cursore - invece che ancorato all'area di
        /// lavoro: e' quello che serve ai menu della barra e dell'orologio.
        /// I menu delle APP lasciano il valore predefinito (false) e
        /// continuano ad aprirsi sopra il pulsante della Superbar.</summary>
        public int ShowContextMenuEx(int x, int y, bool bottomEdge, string items,
                                     bool anchorAtCursor = false)
            => NativeMethods.W7T_ShowContextMenuEx(x, y, bottomEdge ? 1 : 0,
                                                   items, anchorAtCursor ? 1 : 0);

        // v2.7: overflow nativo.
        public bool OverflowInit(IntPtr taskbarHwnd) => NativeMethods.W7T_OverflowInit((ulong)taskbarHwnd) == 1;
        public void OverflowShow(int l, int t, int r, int b) => NativeMethods.W7T_OverflowShow(l, t, r, b);
        public void OverflowHide() => NativeMethods.W7T_OverflowHide();
        public void OverflowRefresh() => NativeMethods.W7T_OverflowRefresh();
        public bool OverflowIsVisible() => NativeMethods.W7T_OverflowIsVisible() == 1;
        public bool OverflowGetRect(out int l, out int t, out int r, out int b)
            => NativeMethods.W7T_OverflowGetRect(out l, out t, out r, out b) == 1;

        /// <summary>v2.60: su Windows 11 il clic sulla freccetta apre il flyout
        /// di sistema. Non c'e' nessun pannello nostro da nascondere e nessun
        /// rettangolo da escludere dall'hook dei clic esterni.</summary>
        public bool OverflowUsesShellFlyout() => NativeMethods.W7T_OverflowUsesShellFlyout() == 1;

        /// <summary>v2.61: Windows 11 secondo il core (RtlGetVersion).</summary>
        public bool IsWindows11() => NativeMethods.W7T_IsWindows11() == 1;

        /// <summary>v2.62: chiude il riquadro dell'orologio della shell se
        /// e' aperto (non lo apre mai).</summary>
        public void HideClockFlyout()
        {
            try
            {
                NativeMethods.W7T_HideClockFlyout();
            }
            catch
            {
                /* La chiusura e' una precauzione: se il core non risponde si
                 * prosegue e il riquadro nostro si apre comunque. */
            }
        }

        // v3.0: ricerca app opzionale.
        public bool AppSearchInit(IntPtr taskbarHwnd, byte[]? argbPixels, int iconW, int iconH)
            => NativeMethods.W7T_AppSearchInit((ulong)taskbarHwnd, argbPixels, iconW, iconH) == 1;
        public void PropertiesShow(IntPtr owner, int lang, int seconds, int nativeFlyout,
            int enableSearch, int netFlyout, int classicVolume, int batteryFlyout,
            int aeroPeek, int toolbarDesktop, int toolbarAddress, int toolbarLinks,
            int inputLanguageMode)
            => NativeMethods.W7T_PropertiesShow((ulong)owner, lang, seconds, nativeFlyout,
                enableSearch, netFlyout, classicVolume, batteryFlyout,
                aeroPeek, toolbarDesktop, toolbarAddress, toolbarLinks,
                inputLanguageMode);

        /// <summary>v2.36: flyout di rete Windows 7 (porting MIT mod Windhawk).</summary>
        public bool NetFlyoutInit() => NativeMethods.W7T_NetFlyoutInit() == 1;
        public void NetFlyoutUninit() => NativeMethods.W7T_NetFlyoutUninit();

        /// <summary>v2.62: dichiara al core se il riquadro di rete di
        /// Windows 7 e' pronto all'uso (vedi W7T_NetFlyoutInit).</summary>
        public void SetWin7NetworkFlyout(bool ready)
            => NativeMethods.W7T_SetWin7NetworkFlyout(ready ? 1 : 0);

        /// <summary>v2.63: pubblica le preferenze dei quattro riquadri al
        /// core, che da solo decide quale percorso usare per ognuno.</summary>
        public void SetFlyoutPreferences(bool clockWin7, bool networkWin7,
                                         bool volumeWin7, bool batteryWin7)
            => NativeMethods.W7T_SetFlyoutPreferences(clockWin7 ? 1 : 0,
                networkWin7 ? 1 : 0, volumeWin7 ? 1 : 0, batteryWin7 ? 1 : 0);

        /// <summary>v2.63: vero se questa build ha i riquadri moderni della
        /// shell (Windows 11 con l'infrastruttura immersiva presente).</summary>
        public bool IsModernFlyoutHostAvailable()
            => NativeMethods.W7T_IsModernFlyoutHostAvailable() == 1;
        public void NetFlyoutToggleAt(int left, int top, int right, int bottom)
        {
            var rc = new NativeMethods.RECT { Left = left, Top = top, Right = right, Bottom = bottom };
            NativeMethods.W7T_NetFlyoutToggleAt(ref rc);
        }
        public bool IsNetworkTrayOwner(ulong ownerHwnd)
            => NativeMethods.W7T_IsNetworkTrayOwner(ownerHwnd) == 1;
    public void AppSearchShow(int x, int y) => NativeMethods.W7T_AppSearchShow(x, y);
    public void AppSearchHide() => NativeMethods.W7T_AppSearchHide();

    /// <summary>v2.37 punto 17: visibilita' della ricerca (per il toggle
    /// del pulsante lente).</summary>
    public bool AppSearchIsVisible() => NativeMethods.W7T_AppSearchIsVisible() == 1;

    /// <summary>v2.37 punto 16: comunica al flyout di rete la lingua
    /// dell'app (indice 0=it..9=zh).</summary>
    public void NetFlyoutSetLanguage(int appLanguageIndex)
        => NativeMethods.W7T_NetFlyoutSetLanguage(appLanguageIndex);

        // ---------------------------------------------------------------
        //  v2.38: modulo proprietario icone tray, mixer volume classico,
        //  Jump List e flyout batteria ricreato.
        // ---------------------------------------------------------------

        /// <summary>v2.38: true se l'icona tray appartiene al modulo dato
        /// (es. SndVolSSO.dll per il volume, stobject.dll per la batteria).
        /// Usata dal managed per decidere quale flyout ricreato aprire.</summary>
        public bool TrayOwnerModuleMatch(ulong ownerHwnd, string moduleName)
        {
            try { return NativeMethods.W7T_TrayOwnerModuleMatch(ownerHwnd, moduleName) == 1; }
            catch { return false; }
        }

        /// <summary>v2.38 punto 1: avvia il mixer volume classico
        /// (SndVol.exe -f) ancorato alle coordinate schermo dell'icona.
        /// Fire-and-forget; ritorna false per ripiegare sul flyout moderno.</summary>
        public bool LaunchClassicVolume(int x, int y)
        {
            try { return NativeMethods.W7T_LaunchClassicVolume(x, y) == 1; }
            catch { return false; }
        }

        /// <summary>v2.41: chiude il mixer classico (SndVol) se aperto:
        /// un clic sulla nostra barra chiude i riquadri come in Win7.</summary>
        public void CloseClassicVolume()
        {
            try { NativeMethods.W7T_CloseClassicVolume(); } catch { }
        }

        // ---------------------------------------------------------------
        //  Jump List (sistema del gesto: clic sinistro + trascinamento
        //  verso l'alto). Coordinate: PIXEL FISICI DELLO SCHERMO.
        //
        //  I metodi sono sottili di proposito: chi li chiama
        //  (TaskbarWindow.JumpList.cs) e' il punto che conosce il gesto e
        //  puo' annullarlo, quindi e' li' che ogni chiamata e' avvolta in
        //  try/catch con log e CancelJumpList(). Qui non si convertono le
        //  eccezioni in nessun comportamento: si lasciano salire.
        //  L'unica eccezione e' JumpListHide, chiamato dai percorsi di
        //  pulizia: un secondo errore durante l'annullamento non deve
        //  nascondere il primo.
        // ---------------------------------------------------------------

        /// <summary>Carica dalla shell le voci reali dell'applicazione e
        /// mostra il popup ancorato al rettangolo del pulsante. Ritorna il
        /// numero di voci (0 = solo righe standard) o un codice negativo di
        /// fallimento. appId riceve l'AppUserModelID risolta (vuota se la
        /// shell non ne espone una).</summary>
        public int JumpListOpen(NativeMethods.RECT buttonRect, int edge,
            string title, string launchPath, string pinnedLnk, bool isPinned,
            ulong hwnd, string exePath, uint[]? iconArgb, int iconW, int iconH,
            int lang, out string appId)
        {
            var id = new System.Text.StringBuilder(512);
            int entries = NativeMethods.W7T_JumpListOpen(ref buttonRect, edge,
                title ?? string.Empty, launchPath ?? string.Empty,
                pinnedLnk ?? string.Empty, isPinned ? 1 : 0, hwnd,
                exePath ?? string.Empty, iconArgb, iconW, iconH, lang,
                id, id.Capacity);
            appId = id.ToString();
            return entries;
        }

        /// <summary>Vero finche' il cursore resta nell'area di interazione
        /// del gesto (popup + pulsante + corridoio fra i due).</summary>
        public bool JumpListSetHover(int screenX, int screenY)
            => NativeMethods.W7T_JumpListSetHover(screenX, screenY) == 1;

        /// <summary>Rilascio del gesto: attiva la riga sotto il cursore e
        /// chiude il popup. bits: 1 documento, 2 riga applicazione,
        /// 4 pin invertito. Ritorna falso se il popup non era aperto.</summary>
        public bool JumpListActivateAt(int screenX, int screenY, out int bits)
            => NativeMethods.W7T_JumpListActivateAt(screenX, screenY, out bits) == 1;

        public void JumpListHide()
        {
            try { NativeMethods.W7T_JumpListHide(); }
            catch (Exception ex)
            {
                // Cleanup path only: logged, never thrown (an error here
                // must not mask the failure that is being handled).
                System.Diagnostics.Debug.WriteLine(
                    "JumpList hide: " + ex.Message);
            }
        }

        /// <summary>v2.38: flyout batteria ricreato, ancorato al rettangolo
        /// schermo dell'icona batteria.</summary>
        public void BatteryFlyoutShowAt(int left, int top, int right, int bottom)
        {
            try { NativeMethods.W7T_BatteryFlyoutShowAt(left, top, right, bottom); } catch { }
        }

        public void BatteryFlyoutHide()
        {
            try { NativeMethods.W7T_BatteryFlyoutHide(); } catch { }
        }

        public void BatteryFlyoutSetLanguage(int lang)
        {
            try { NativeMethods.W7T_BatteryFlyoutSetLanguage(lang); } catch { }
        }

        /// <summary>
        /// Importa le icone gia' presenti nell'area di notifica di Explorer.
        /// </summary>
        public int ImportExplorerTrayIcons()
            => NativeMethods.W7T_TrayImportExplorerIcons();

        /// <summary>
        /// v2.1: apre la pagina NATIVA di Windows per la personalizzazione
        /// delle icone dell'area di notifica (il link "Personalizza..." del
        /// riquadro di overflow). Il core usa ShellExecuteEx sul namespace
        /// shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}; su Windows 10/11
        /// il sistema reindirizza alla pagina Impostazioni equivalente.
        /// Restituisce false se la shell ha rifiutato l'apertura: il
        /// chiamante deve prevedere un ripiego.
        /// </summary>
        public bool OpenNotificationIconsSettings()
            => NativeMethods.W7T_OpenNotificationIconsSettings() == W7TResult.Ok;

        /// <summary>
        /// v2.2: scrive una riga in log-core.txt (diagnostica dei percorsi
        /// interattivi che non si possono verificare in compilazione).
        /// </summary>
        public void Log(string line)
        {
            try { NativeMethods.W7T_Log(line); } catch { }
        }

        /// <summary>Legge il volume di sistema (0..100).</summary>
        public bool TryGetVolume(out int level, out bool muted)
        {
            int rawLevel = 0;
            int rawMuted = 0;

            bool ok = NativeMethods.W7T_GetVolume(ref rawLevel, ref rawMuted) == W7TResult.Ok;

            level = rawLevel;
            muted = rawMuted != 0;
            return ok;
        }

        /// <summary>Imposta il volume di sistema (0..100).</summary>
        public void SetVolume(int level)
            => NativeMethods.W7T_SetVolume(level);

        /// <summary>Attiva o disattiva l'audio.</summary>
        public void SetVolumeMuted(bool muted)
            => NativeMethods.W7T_SetVolumeMuted(muted ? 1 : 0);

        /// <summary>
        /// Apre il vero riquadro del volume di Windows 10/11.
        /// Restituisce false se il sistema non lo mette a disposizione.
        /// </summary>
        public bool ShowVolumeFlyout(IntPtr taskbarHwnd)
            => NativeMethods.W7T_ShowVolumeFlyout((ulong)taskbarHwnd.ToInt64()) == W7TResult.Ok;

        /// <summary>Apre il mixer volume classico (SndVol.exe).</summary>
        public bool ShowVolumeMixer()
            => NativeMethods.W7T_ShowVolumeMixer() == W7TResult.Ok;

        // ---------------------------------------------------------------
        //  Riquadri immersivi di Windows 10/11: rete, orologio, batteria e
        //  volume, cioe' i quattro riquadri che Windows 7 aggancia alle
        //  proprie icone di sistema.
        //
        //  La sequenza nativa (ImmersiveShell -> ShellExperienceManager
        //  Factory -> GetExperienceManager -> ShowFlyout/HideFlyout) e'
        //  adattata da ExplorerPatcher/ImmersiveFlyouts.c di valinet
        //  (GPL-2.0-or-later). Vedere native/src/ImmersiveFlyouts.h.
        // ---------------------------------------------------------------

        /// <summary>
        /// Mostra o nasconde un riquadro immersivo di sistema.
        /// Restituisce false se questo Windows non lo mette a disposizione
        /// (Windows 7/8, oppure riquadro assente: la batteria su un desktop
        /// senza UPS, per esempio): il chiamante deve prevedere un ripiego.
        /// </summary>
        public bool InvokeFlyout(FlyoutKind kind, bool show, IntPtr taskbarHwnd)
            => NativeMethods.W7T_InvokeFlyout(
                   (ulong)taskbarHwnd.ToInt64(),
                   (int)kind,
                   show ? FlyoutActionValue.Show : FlyoutActionValue.Hide) == W7TResult.Ok;


        /// <summary>Chiude un riquadro gia' aperto.</summary>
        public bool HideFlyout(FlyoutKind kind)
            => InvokeFlyout(kind, false, IntPtr.Zero);


        // ---------------------------------------------------------------
        //  Shell
        // ---------------------------------------------------------------

        public void ToggleShowDesktop() => NativeMethods.W7T_ToggleShowDesktop();

        /// <summary>v2.32: apertura Start senza input iniettato
        /// (SC_TASKLIST mirato a Shell_TrayWnd, come Open-Shell).</summary>
        public void OpenStartFallback()
        {
            try { NativeMethods.W7T_OpenStartFallback(); } catch { }
        }

        public void ShowStartMenu() => NativeMethods.W7T_ShowStartMenu();

        public void ShowTaskManager() => NativeMethods.W7T_ShowTaskManager();

        /// <summary>
        /// Apre il menu di sistema REALE della finestra (quello di Windows,
        /// non un ContextMenu WPF): le voci riflettono lo stato effettivo e
        /// il comando scelto viene eseguito dall'applicazione bersaglio.
        /// </summary>
        public void ShowWindowSystemMenu(ulong hwnd, int x, int y, bool bottomEdge = true)
            => NativeMethods.W7T_ShowWindowSystemMenu(hwnd, x, y, bottomEdge ? 1 : 0);

        /// <summary>
        /// Menu di gruppo nativo. Restituisce 1 = minimizza gruppo,
        /// 2 = chiudi gruppo, 0 = annullato.
        /// </summary>
        public int ShowGroupMenu(ulong hwnd, int x, int y, string minimizeText,
                                 string closeText, bool bottomEdge = true)
            => NativeMethods.W7T_ShowGroupMenu(hwnd, x, y, bottomEdge ? 1 : 0,
                                               minimizeText, closeText);

        // ---------------------------------------------------------------

        private static ImageSource CreateBitmap(byte[] pixels, int width, int height)
        {
            // Il core produce BGRA premoltiplicato top-down: corrisponde a Pbgra32.
            var bitmap = BitmapSource.Create(
                width, height, 96, 96,
                PixelFormats.Pbgra32, null,
                pixels, width * 4);

            bitmap.Freeze(); // condivisibile fra thread e piu' economico da renderizzare
            return bitmap;
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;

            _pumpTimer.Stop();

            if (_initialized)
            {
                NativeMethods.W7T_Shutdown();
                _initialized = false;
            }

            GC.SuppressFinalize(this);
        }
    }

    internal sealed class CoreEventArgs : EventArgs
    {
        public CoreEventArgs(int eventType, ulong a, ulong b)
        {
            EventType = eventType;
            A = a;
            B = b;
        }

        public int EventType { get; }
        public ulong A { get; }
        public ulong B { get; }
    }
}
