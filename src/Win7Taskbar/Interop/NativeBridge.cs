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
using Win7Taskbar.Utilities;

namespace Win7Taskbar.Interop
{
    /// <summary>
    /// Una notifica a fumetto inviata da un'applicazione con NIF_INFO.
    ///
    /// Il tipo e' public perche' Controls.NotificationBalloon (public, per via
    /// di TrayIconModel.MissedNotifications) lo espone come Data: un tipo meno
    /// accessibile del membro che lo usa non compila (CS0052/CS0051). Il
    /// costruttore resta internal, quindi fuori dall'assembly il tipo si puo'
    /// leggere ma non fabbricare: le istanze nascono solo qui, dal core.
    /// </summary>
    public readonly struct BalloonNotification
    {
        internal BalloonNotification(ulong ownerHwnd, uint uid, string title,
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
        ///
        /// Prima del primo P/Invoke passa dal bootstrap di
        /// <see cref="NativeCore.EnsureLoaded"/>: verifica la DLL accanto
        /// all'eseguibile e, se manca, e' diversa dalla copia incorporata
        /// oppure non si lascia caricare, ripristina la copia che viaggia
        /// DENTRO l'eseguibile e la carica col percorso completo. Il
        /// DllNotFoundException del blocco try e' quindi l'ultimo ripiego,
        /// non la norma.
        /// </summary>
        public bool TryInitialize(out string? error)
        {
            error = null;

            if (_initialized)
            {
                return true;
            }

            NativeCoreStatus coreStatus = NativeCore.EnsureLoaded();

            int result;
            try
            {
                result = NativeMethods.W7T_Initialize(_callback);
            }
            catch (DllNotFoundException)
            {
                error = "Win7TaskbarCore.dll non è stata trovata.\n\n" +
                        "Deve stare nella stessa cartella di Win7Taskbar.exe. " +
                        "Se hai estratto lo ZIP, verifica di aver mantenuto tutti i file.\n\n" +
                        NativeCore.DescribeRecoveryAttempt();
                return false;
            }
            catch (BadImageFormatException)
            {
                error = "Win7TaskbarCore.dll non è compatibile con questo processo.\n\n" +
                        "L'eseguibile è a 64 bit: serve la DLL nativa x64 nella stessa " +
                        "cartella. Un antivirus o un file corrotto possono causarlo.\n\n" +
                        NativeCore.DescribeRecoveryAttempt();
                return false;
            }
            catch (EntryPointNotFoundException ex)
            {
                error = "La DLL nativa è di una versione diversa dall'eseguibile " +
                        $"(manca {ex.Message.Split('\n')[0]}).\n\n" +
                        "Sostituisci entrambi i file con quelli dello stesso pacchetto.\n\n" +
                        NativeCore.DescribeRecoveryAttempt();
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

            if (coreStatus.Repaired)
            {
                // Il riparo e' un evento raro e merita una riga nel rapporto
                // di avvio: se riappare a ogni avvio c'e' qualcosa (di solito
                // un antivirus) che continua a togliere la DLL.
                StartupGuard.Note("core-nativo: la DLL e' stata ripristinata dalla copia " +
                                  "incorporata (" + (coreStatus.RepairReason ?? "motivo non registrato") +
                                  ") -> " + coreStatus.LoadedPath);
            }

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
            try
            {
                NativeMethods.W7T_RefreshWindows();

                int count = NativeMethods.W7T_GetWindowCount();
                if (count <= 0)
                {
                    return Array.Empty<W7TWindowInfo>();
                }

                // Margine sulla capacita': una finestra puo' apparire fra le
                // due chiamate e il core rifiuterebbe il buffer.
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
            catch
            {
                // Interop boundary: a failed enumeration degrades to "nothing
                // to show right now"; the next refresh re-syncs the truth.
                return Array.Empty<W7TWindowInfo>();
            }
        }

        /// <summary>
        /// Snapshot of a single window, for the async resolution worker (which
        /// runs off the UI thread and must not pull the whole enumeration).
        /// False when the core no longer tracks the window.
        /// </summary>
        public bool TryGetWindow(ulong hwnd, out W7TWindowInfo info)
        {
            info = default;
            if (hwnd == 0)
            {
                return false;
            }

            try
            {
                return NativeMethods.W7T_GetWindowInfo(hwnd, out info) == W7TResult.Ok;
            }
            catch
            {
                return false;
            }
        }

        public ImageSource? GetWindowIcon(ulong hwnd, int desiredSize = 32)
        {
            try
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
            catch
            {
                // No icon this time (boundary guard): the entry keeps the one
                // it has, the resolver retries later.
                return null;
            }
        }

        /// <summary>v2.62-alpha: the user's own preview configuration
        /// (read-only; the core caches it and drops the cache on
        /// WM_SETTINGCHANGE). Null when the loaded core predates the
        /// export: the caller keeps the project defaults, so an old core
        /// never changes the behaviour. A delay is null when the user value
        /// is absent.</summary>
        public (bool WindowThumbs, bool DesktopPeek, int? ThumbHoverMs, int? PeekHoverMs)? GetPreviewPolicy()
        {
            try
            {
                if (NativeMethods.W7T_GetPreviewPolicy(out int thumbs, out int peek,
                        out int thumbMs, out int peekMs) != 0)
                {
                    return null;
                }
                return (thumbs != 0, peek != 0,
                    thumbMs >= 0 ? (int?)thumbMs : null,
                    peekMs >= 0 ? (int?)peekMs : null);
            }
            catch (EntryPointNotFoundException)
            {
                return null;
            }
        }

        /// <summary>v2.62-alpha (G6): canonical pin/unpin. 1 = on-disk
        /// state changed, 0 = no change, negative = failure or core without
        /// the export (the caller keeps its own path).</summary>
        public int ToggleTaskbarPin(string exePath, string baseName, int pin)
        {
            try
            {
                return NativeMethods.W7T_ToggleTaskbarPin(exePath, baseName, pin);
            }
            catch (EntryPointNotFoundException)
            {
                return -1;
            }
        }

        /// <summary>v2.62-alpha (G4): the executable's own icon (null when
        /// it cannot be resolved or the core predates the export).</summary>
        public ImageSource? GetExeIcon(string exePath, int desiredSize = 32)
        {
            if (string.IsNullOrEmpty(exePath))
            {
                return null;
            }
            try
            {
                int needed = NativeMethods.W7T_GetExeIconBitmap(
                    exePath, desiredSize, out int width, out int height, null, 0);
                if (needed <= 0 || width <= 0 || height <= 0)
                {
                    return null;
                }
                var pixels = new byte[needed];
                int copied = NativeMethods.W7T_GetExeIconBitmap(
                    exePath, desiredSize, out width, out height, pixels, pixels.Length);
                return copied <= 0 ? null : CreateBitmap(pixels, width, height);
            }
            catch (EntryPointNotFoundException)
            {
                return null;
            }
        }

        public void ExecuteCommand(ulong hwnd, int command)
        {
            try { NativeMethods.W7T_ExecuteWindowCommand(hwnd, command); } catch { }
        }

        public void MinimizeGroup(string appId)
        {
            try { NativeMethods.W7T_MinimizeGroup(appId); } catch { }
        }

        public void CloseGroup(string appId)
        {
            try { NativeMethods.W7T_CloseGroup(appId); } catch { }
        }

        public bool IsFullScreenAppActive()
        {
            try { return NativeMethods.W7T_IsFullScreenAppActive() != 0; }
            catch { return false; }
        }

        // ---------------------------------------------------------------
        //  Tray
        // ---------------------------------------------------------------

        public bool StartTray()
        {
            try { return NativeMethods.W7T_TrayStart() == W7TResult.Ok; }
            catch { return false; }
        }

        public void StopTray()
        {
            try { NativeMethods.W7T_TrayStop(); } catch { }
        }

        public IReadOnlyList<W7TTrayIconInfo> GetTrayIcons()
        {
            try
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
            catch
            {
                // Boundary guard: same contract as GetWindows.
                return Array.Empty<W7TTrayIconInfo>();
            }
        }

        public ImageSource? GetTrayIcon(ulong ownerHwnd, uint uid)
        {
            try
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
            catch
            {
                return null;
            }
        }

        public void SendTrayClick(ulong ownerHwnd, uint uid, int clickType, int x, int y)
        {
            try { NativeMethods.W7T_SendTrayIconClick(ownerHwnd, uid, clickType, x, y); }
            catch { }
        }

        /// <summary>
        /// Recupera l'ultima notifica a fumetto ricevuta dal core.
        /// Restituisce false se non ce n'e' nessuna in attesa.
        /// </summary>
        public bool TryGetLastBalloon(out BalloonNotification balloon)
        {
            balloon = default;
            try
            {
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
            catch
            {
                // Boundary guard: a lost balloon is a missing notification,
                // never a dead bar.
                return false;
            }
        }

        public void SetTrayIconPinned(ulong ownerHwnd, uint uid, bool pinned)
        {
            try { NativeMethods.W7T_SetTrayIconPinned(ownerHwnd, uid, pinned ? 1 : 0); }
            catch { }
        }

        /// <summary>
        /// Propaga al core (e al ToolbarWindow32 reale del modello) il
        /// riordino fatto dall'utente col trascinamento: l'icona sorgente
        /// viene spostata prima o dopo il bersaglio con TB_MOVEBUTTON.
        /// </summary>
        public bool TrayMoveIcon(ulong sourceHwnd, uint sourceUid,
                                 ulong targetHwnd, uint targetUid, bool insertAfter)
        {
            try
            {
                return NativeMethods.W7T_TrayMoveIcon(sourceHwnd, sourceUid, targetHwnd,
                    targetUid, insertAfter ? 1 : 0) == W7TResult.Ok;
            }
            catch { return false; }
        }

        // ---------------------------------------------------------------
        //  AppBar
        // ---------------------------------------------------------------

        public bool RegisterAppBar(IntPtr hwnd, int edge, int sizePx)
        {
            try
            {
                return NativeMethods.W7T_AppBarRegister(
                    (ulong)hwnd.ToInt64(), edge, sizePx) == W7TResult.Ok;
            }
            catch { return false; }
        }

        public bool SetAppBarPos(IntPtr hwnd, int edge, int sizePx, out Rect reserved)
        {
            reserved = Rect.Empty;
            try
            {
                int result = NativeMethods.W7T_AppBarSetPos(
                    (ulong)hwnd.ToInt64(), edge, sizePx,
                    out int left, out int top, out int right, out int bottom);

                reserved = result == W7TResult.Ok
                    ? new Rect(left, top, Math.Max(0, right - left), Math.Max(0, bottom - top))
                    : Rect.Empty;

                return result == W7TResult.Ok;
            }
            catch
            {
                reserved = Rect.Empty;
                return false;
            }
        }

        public void UnregisterAppBar(IntPtr hwnd)
        {
            try { NativeMethods.W7T_AppBarUnregister((ulong)hwnd.ToInt64()); } catch { }
        }

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
        {
            try
            {
                return NativeMethods.W7T_ShowContextMenuEx(x, y, bottomEdge ? 1 : 0,
                                                          items, anchorAtCursor ? 1 : 0);
            }
            catch { return 0; }
        }

        // v2.7: overflow nativo. Interop boundary guard on every call
        // (same contract as the AppBar block above).
        public bool OverflowInit(IntPtr taskbarHwnd)
        {
            try { return NativeMethods.W7T_OverflowInit((ulong)taskbarHwnd) == 1; }
            catch { return false; }
        }
        public void OverflowShow(int l, int t, int r, int b)
        {
            try { NativeMethods.W7T_OverflowShow(l, t, r, b); } catch { }
        }
        public void OverflowHide()
        {
            try { NativeMethods.W7T_OverflowHide(); } catch { }
        }
        public void OverflowRefresh()
        {
            try { NativeMethods.W7T_OverflowRefresh(); } catch { }
        }
        public bool OverflowIsVisible()
        {
            try { return NativeMethods.W7T_OverflowIsVisible() == 1; }
            catch { return false; }
        }
        public bool OverflowGetRect(out int l, out int t, out int r, out int b)
        {
            l = t = r = b = 0;
            try { return NativeMethods.W7T_OverflowGetRect(out l, out t, out r, out b) == 1; }
            catch { l = t = r = b = 0; return false; }
        }

        /// <summary>v2.60: su Windows 11 il clic sulla freccetta apre il flyout
        /// di sistema. Non c'e' nessun pannello nostro da nascondere e nessun
        /// rettangolo da escludere dall'hook dei clic esterni.</summary>
        public bool OverflowUsesShellFlyout()
        {
            try { return NativeMethods.W7T_OverflowUsesShellFlyout() == 1; }
            catch { return false; }
        }

        /// <summary>v2.61: Windows 11 secondo il core (RtlGetVersion).</summary>
        public bool IsWindows11()
        {
            try { return NativeMethods.W7T_IsWindows11() == 1; }
            catch { return false; }
        }

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
        {
            try
            {
                return NativeMethods.W7T_AppSearchInit(
                    (ulong)taskbarHwnd, argbPixels, iconW, iconH) == 1;
            }
            catch { return false; }
        }
        public void PropertiesShow(IntPtr owner, int lang, int seconds, int nativeFlyout,
            int enableSearch, int netFlyout, int classicVolume, int batteryFlyout,
            int aeroPeek, int toolbarDesktop, int toolbarAddress, int toolbarLinks,
            int inputLanguageMode, int taskManagerMode,
            int flyoutColorMode, int flyoutColorRgb,
            int connectionPrivacyMode, int themeSelection,
            int autoStart, int taskbarPosition, int lockTaskbar,
            int windowsKeyOpensOurMenu, int killXamlTrayOverlay)
        {
            try
            {
                NativeMethods.W7T_PropertiesShow((ulong)owner, lang, seconds, nativeFlyout,
                    enableSearch, netFlyout, classicVolume, batteryFlyout,
                    aeroPeek, toolbarDesktop, toolbarAddress, toolbarLinks,
                    inputLanguageMode, taskManagerMode,
                    flyoutColorMode, flyoutColorRgb,
                    connectionPrivacyMode, themeSelection,
                    autoStart, taskbarPosition, lockTaskbar,
                    windowsKeyOpensOurMenu, killXamlTrayOverlay);
            }
            catch { }
        }

        public int StartMenuScan()
        {
            try { return NativeMethods.W7T_StartMenuScan(); }
            catch { return W7TResult.ErrNotFound; }
        }

        public IReadOnlyList<NativeMethods.W7TStartMenuEntry> StartMenuGetEntries()
        {
            try
            {
                int n = NativeMethods.W7T_StartMenuGetCount();
                if (n <= 0)
                {
                    return Array.Empty<NativeMethods.W7TStartMenuEntry>();
                }
                var list = new List<NativeMethods.W7TStartMenuEntry>(n);
                for (int i = 0; i < n; i++)
                {
                    if (NativeMethods.W7T_StartMenuGetEntry(i, out NativeMethods.W7TStartMenuEntry e)
                        == W7TResult.Ok)
                    {
                        list.Add(e);
                    }
                }
                return list;
            }
            catch
            {
                return Array.Empty<NativeMethods.W7TStartMenuEntry>();
            }
        }

        public int[] StartMenuQuery(string query, int capacity)
        {
            try
            {
                var indices = new int[Math.Max(1, capacity)];
                int written = NativeMethods.W7T_StartMenuQuery(query ?? string.Empty,
                    indices, indices.Length);
                if (written <= 0)
                {
                    return Array.Empty<int>();
                }
                if (written < indices.Length)
                {
                    Array.Resize(ref indices, written);
                }
                return indices;
            }
            catch
            {
                return Array.Empty<int>();
            }
        }

        public bool StartMenuLaunch(string path)
        {
            try { return NativeMethods.W7T_StartMenuLaunch(path) == W7TResult.Ok; }
            catch { return false; }
        }

        public bool StartMenuHasJumpList(string path)
        {
            try { return NativeMethods.W7T_StartMenuHasJumpList(path) != 0; }
            catch { return false; }
        }

        public void StartMenuPower(int action)
        {
            try { NativeMethods.W7T_StartMenuPower(action); } catch { }
        }

        public bool StartMenuFileSearchStart(string query)
        {
            try { return NativeMethods.W7T_StartMenuFileSearchStart(query) == W7TResult.Ok; }
            catch { return false; }
        }

        public string? StartMenuFileSearchPoll()
        {
            try
            {
                var buffer = new char[4096];
                int n = NativeMethods.W7T_StartMenuFileSearchPoll(buffer, buffer.Length);
                if (n < 0)
                {
                    return null; /* not ready */
                }
                if (n == 0)
                {
                    return string.Empty;
                }
                int end = Array.IndexOf(buffer, '\0');
                if (end < 0) end = buffer.Length;
                return new string(buffer, 0, end);
            }
            catch
            {
                return null;
            }
        }

        public void StartMenuFileSearchCancel()
        {
            try { NativeMethods.W7T_StartMenuFileSearchCancel(); } catch { }
        }

        /// <summary>
        /// v1.21.7: settings of the extra section published to the core (a
        /// single call). The configuration stays in the managed layer: what
        /// crosses this boundary is only what has to be applied.
        /// </summary>
        public void SetExtraSettings(int flyoutColorMode, int flyoutColorRgb,
                                     int connectionPrivacyMode)
        {
            try
            {
                NativeMethods.W7T_SetExtraSettings(flyoutColorMode, flyoutColorRgb,
                    connectionPrivacyMode);
            }
            catch { }
        }

        public void SetKillXamlTrayOverlay(bool enabled)
        {
            try
            {
                NativeMethods.W7T_SetKillXamlTrayOverlay(enabled ? 1 : 0);
            }
            catch { }
        }

        /// <summary>
        /// v1.21.7: the colour the recreated Windows 8-style flyout would use
        /// right now (system accent or chosen colour). The core resolves it by
        /// asking the system every time.
        /// </summary>
        public bool GetExtraFlyoutColor(out uint rgb)
        {
            rgb = 0;
            try { return NativeMethods.W7T_GetExtraFlyoutColor(out rgb) == 1; }
            catch { rgb = 0; return false; }
        }

        /// <summary>v2.36: flyout di rete Windows 7 (porting MIT mod Windhawk).</summary>
        public bool NetFlyoutInit()
        {
            try { return NativeMethods.W7T_NetFlyoutInit() == 1; }
            catch { return false; }
        }
        public void NetFlyoutUninit()
        {
            try { NativeMethods.W7T_NetFlyoutUninit(); } catch { }
        }

        /// <summary>v2.62: dichiara al core se il riquadro di rete di
        /// Windows 7 e' pronto all'uso (vedi W7T_NetFlyoutInit).</summary>
        public void SetWin7NetworkFlyout(bool ready)
        {
            try { NativeMethods.W7T_SetWin7NetworkFlyout(ready ? 1 : 0); } catch { }
        }

        /// <summary>v2.63: pubblica le preferenze dei quattro riquadri al
        /// core, che da solo decide quale percorso usare per ognuno.
        /// v3.8: per la rete esiste una terza scelta: networkStyle 1 =
        /// "Windows 7 (ricreato)", 0 = "Windows 10/11" (sistema), 2 =
        /// "Windows 8 (ricreato)". Gli altri riquadri restano binari.</summary>
        public void SetFlyoutPreferences(bool clockWin7, int networkStyle,
                                         bool volumeWin7, bool batteryWin7)
            => NativeMethods.W7T_SetFlyoutPreferences(clockWin7 ? 1 : 0,
                networkStyle, volumeWin7 ? 1 : 0, batteryWin7 ? 1 : 0);

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
    public void AppSearchShow(int x, int y, int theme)
        => NativeMethods.W7T_AppSearchShow(x, y, theme);
    public void AppSearchHide() => NativeMethods.W7T_AppSearchHide();

    /// <summary>v2.37 punto 17: visibilita' della ricerca (per il toggle
    /// del pulsante lente).</summary>
    public bool AppSearchIsVisible() => NativeMethods.W7T_AppSearchIsVisible() == 1;

    /// <summary>v2.37 punto 16: comunica al flyout di rete la lingua
    /// dell'app (indice 0=it..9=zh).</summary>
    public void NetFlyoutSetLanguage(int appLanguageIndex)
        => NativeMethods.W7T_NetFlyoutSetLanguage(appLanguageIndex);

    /// <summary>v3.8: flyout di rete variante Windows 8 (implementazione:
    /// Administratox). Inizializza la stessa logica di rete del modulo
    /// Windows 7 e prepara il riquadro grafico; il core deve saperlo con
    /// SetWin8NetworkFlyout.</summary>
    public bool Net8FlyoutInit() => NativeMethods.W7T_Net8FlyoutInit() == 1;
    public void Net8FlyoutUninit() => NativeMethods.W7T_Net8FlyoutUninit();
    /// <summary>v3.8: chiude il riquadro Windows 8 (es. la modalita' di rete
    /// e' appena cambiata a un'altra voce: non deve restare visibile).</summary>
    public void Net8FlyoutHide() => NativeMethods.W7T_Net8FlyoutHide();
    public void Net8FlyoutSetLanguage(int appLanguageIndex)
        => NativeMethods.W7T_Net8FlyoutSetLanguage(appLanguageIndex);

    /// <summary>v3.8: dichiara al core se la variante Windows 8 del riquadro
    /// di rete e' pronta all'uso (vedi Net8FlyoutInit).</summary>
    public void SetWin8NetworkFlyout(bool ready)
        => NativeMethods.W7T_SetWin8NetworkFlyout(ready ? 1 : 0);

    /// <summary>v3.8: apre/chiude il riquadro di rete in stile Windows 8
    /// ancorato al monitor del rettangolo icona (pixel fisici).</summary>
    public void Net8FlyoutToggleAt(int left, int top, int right, int bottom)
    {
        var rc = new NativeMethods.RECT { Left = left, Top = top, Right = right, Bottom = bottom };
        NativeMethods.W7T_Net8FlyoutToggleAt(ref rc);
    }

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
        //  Jump List (trascinamento lontano dalla barra / freccetta).
        //  Coordinate: PIXEL FISICI DELLO SCHERMO.
        //
        //  I metodi sono sottili di proposito: chi li chiama
        //  (TaskbarWindow.JumpList.cs) e' il punto che conosce l'interazione
        //  e puo' annullarla, quindi e' li' che ogni chiamata e' avvolta in
        //  try/catch con log e HideJumpList(). Qui non si convertono le
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
        /// del gesto (popup + pulsante + corridoio fra i due). Il popup
        /// NON si sposta: resta ancorato al pulsante alla posizione
        /// canonica di Windows 7; si aggiorna solo la riga evidenziata.</summary>
        public bool JumpListSetHover(int screenX, int screenY)
            => NativeMethods.W7T_JumpListSetHover(screenX, screenY) == 1;

        /// <summary>v2.62: indice della riga sotto il punto schermo
        /// (>=0), -1 se nessuna; senza effetti collaterali. Lancia
        /// EntryPointNotFoundException su un core senza l'export: il
        /// chiamante ripiega sul rettangolo del popup.</summary>
        public int JumpListHitRow(int screenX, int screenY)
            => NativeMethods.W7T_JumpListHitRow(screenX, screenY);

        /// <summary>v3.17: drag-up entrance animation for the next open.
        /// Old cores without the export no-op silently.</summary>
        public void JumpListSetAnimateFromBelow(bool yes)
        {
            try
            {
                NativeMethods.W7T_JumpListSetAnimateFromBelow(yes ? 1 : 0);
            }
            catch (EntryPointNotFoundException)
            {
                /* Old core: no animation, everything else unchanged. */
            }
        }

        /// <summary>Attiva la riga sotto il punto schermo nel popup
        /// (la chiude dopo); bits riporta l'esito per il chiamante
        /// (1 = documento aperto, 2 = app avviata, 4 = pin commutato).</summary>
        public void JumpListActivateAt(int screenX, int screenY,
                                       out int bits)
        {
            bits = 0;
            NativeMethods.W7T_JumpListActivateAt(screenX, screenY, out bits);
        }

        /// <summary>Il rilascio del gesto lascia aperto il popup e gli
        /// trasferisce focus/input ordinario; la scelta richiede un nuovo
        /// clic e un clic esterno lo chiude per deactivation.</summary>
        public void JumpListMakeInteractive()
            => NativeMethods.W7T_JumpListMakeInteractive();

        // ---------------------------------------------------------------
        //  v2.61: visibility of the native Jump List popup.
        //
        //  The popup is a top-level window of class W7T_JumpList created
        //  IN-PROCESS by the native core, so an ordinary window lookup
        //  answers "is the list on screen" without adding a native export
        //  (a dist/Win7TaskbarCore.dll older than this frontend keeps
        //  working). The process check keeps a foreign window that happens
        //  to register the same class name out of the answer.
        // ---------------------------------------------------------------

        /// <summary>Class name of the native popup: keep in sync with
        /// kClassName in native/src/JumpListWindow.cpp.</summary>
        private const string JumpListPopupClass = "W7T_JumpList";

        /// <summary>The popup window of THIS process, visible or not;
        /// IntPtr.Zero when there is none.</summary>
        public IntPtr FindJumpListPopupWindow()
        {
            IntPtr hwnd = NativeMethods.FindWindowW(JumpListPopupClass, null);
            if (hwnd == IntPtr.Zero)
            {
                return IntPtr.Zero;
            }
            NativeMethods.GetWindowThreadProcessId(hwnd, out uint pid);
            return pid == NativeMethods.GetCurrentProcessId() ? hwnd : IntPtr.Zero;
        }

        /// <summary>True while the Jump List popup is on screen.</summary>
        public bool IsJumpListPopupVisible()
        {
            IntPtr hwnd = FindJumpListPopupWindow();
            return hwnd != IntPtr.Zero && NativeMethods.IsWindowVisible(hwnd);
        }

        /// <summary>Screen-physical-pixel rectangle of the visible popup
        /// (false when there is none): what the click-outside fallback of
        /// an older core needs as its exclusion area.</summary>
        public bool TryGetJumpListPopupRect(out NativeMethods.RECT rect)
        {
            rect = default;
            IntPtr hwnd = FindJumpListPopupWindow();
            return hwnd != IntPtr.Zero &&
                   NativeMethods.IsWindowVisible(hwnd) &&
                   NativeMethods.GetWindowRect(hwnd, out rect);
        }

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

        /// <summary>OPZIONE B: ripristino esplicito (idempotente) della
        /// chiave legacy batteria, chiamato in chiusura pulita.</summary>
        public void BatteryFlyoutRestoreLegacyKey()
        {
            try { NativeMethods.W7T_BatteryFlyoutRestoreLegacyKey(); } catch { }
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

        /// <summary>Avvia una passata manuale di ripopolamento della pagina legacy.</summary>
        public bool NotificationPageBackfill()
            => NativeMethods.W7T_NotificationPageBackfill() == W7TResult.Ok;

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

        public void ShowTaskManager()
            => NativeMethods.W7T_ShowTaskManagerMode(
                RetroBar.Utilities.Settings.Instance.TaskManagerMode);

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

        /// <summary>
        /// Native menu of an idle pin (pinned but not running app): the
        /// "launch" / "pin-unpin" rows. Returns 1 = launch, 2 = pin toggle,
        /// 0 = cancelled. The launch row shows the app's real shell icon;
        /// when the icon is unavailable the row stays text-only and the
        /// menu works all the same.
        /// </summary>
        public int ShowPinMenu(int x, int y, string launchText,
                               string pinText, string lnkPath,
                               string targetPath, bool bottomEdge = true)
            => NativeMethods.W7T_ShowPinMenu(x, y, bottomEdge ? 1 : 0,
                                             launchText, pinText,
                                             lnkPath, targetPath);

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
