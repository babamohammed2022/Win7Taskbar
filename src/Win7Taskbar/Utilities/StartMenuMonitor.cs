// Win7Taskbar - Start menu monitor (a eventi, non a polling)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Rileva apertura/chiusura del menu Start per mantenere corretto lo stato
// "premuto" del pulsante (tre stati: idle / hover / pressed).
//
// PRECEDENTE (fino alla 1.9.17): un DispatcherTimer a 100 ms con confronti
// "stato precedente" e un auto-reset a timeout a indovinare la chiusura:
// esattamente la classe di euristiche vietata dalle regole del progetto, e
// la causa del pulsante_START_ che partiva o restava premuto senza un vero
// evento. Sostituito dal meccanismo nativo: gli stessi eventi di sistema a
// cui si aggancia la shell (EVENT_SYSTEM_FOREGROUND / EVENT_OBJECT_HIDE /
// EVENT_OBJECT_DESTROY via SetWinEventHook, API pubblica user32).
//
// Regole:
//  - stato iniziale: IDLE garantito, nessuna inferenza all'avvio;
//  - pressed = SOLO quando la finestra di primo piano E' l'host del menu
//    Start (classi note, verificate anche sul processo proprietario);
//  - un click sul pulsante imposta pressed in via OPTIMISTICA perche' e' un
//    evento reale dell'utente; se entro il tempo di apertura nessun
//    foreground lo conferma, si rilascia (un solo one-shot, non un loop);
//  - hide/destroy dell'host corrente chiude lo stato, qualunque cosa
//    accada in giro.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Threading;

namespace Win7Taskbar.Utilities
{
    public class StartMenuVisibilityEventArgs : EventArgs
    {
        public bool Visible { get; set; }
    }

    /// <summary>
    /// Monitor a eventi del menu Start: pubblica solo transizioni confermate
    /// da un evento di sistema reale. Nessun polling.
    /// </summary>
    public sealed class StartMenuMonitor : IDisposable
    {
        // --- user32: WinEvent hooks (documentati) ---

        private delegate void WinEventDelegate(
            IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
            int idObject, int idChild, uint dwEventThread, uint dwmsEventTime);

        [DllImport("user32.dll")]
        private static extern IntPtr SetWinEventHook(
            uint eventMin, uint eventMax, IntPtr hmodWinEventProc,
            WinEventDelegate lpfnWinEventProc, uint idProcess, uint idThread,
            uint dwFlags);

        [DllImport("user32.dll")]
        private static extern bool UnhookWinEvent(IntPtr hWinEventHook);

        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWindowVisible(IntPtr hWnd);

        // Costanti WinEvent (winuser.h, valoresi SDK):
        //   EVENT_OBJECT_CREATE 0x8000, DESTROY 0x8001, SHOW 0x8002, HIDE 0x8003
        private const uint EVENT_SYSTEM_FOREGROUND = 0x0003;
        private const uint EVENT_OBJECT_DESTROY    = 0x8001;
        private const uint EVENT_OBJECT_HIDE       = 0x8003;
        private const uint WINEVENT_OUTOFCONTEXT   = 0x0000;

        private static readonly HashSet<string> StartHostClasses = new(StringComparer.Ordinal)
        {
            "DV2ControlHost",                  // Win7 classico e i menu start ricreati
            "OpenShell.CMenuContainer",        // Open-Shell
            "XamlExplorerHostIslandWindow",    // Win11 (StartMenuExperienceHost)
            "Windows.UI.Core.CoreWindow",      // Win10 (ShellExperienceHost / SearchApp)
        };

        private static readonly HashSet<string> StartHostProcesses = new(StringComparer.OrdinalIgnoreCase)
        {
            "explorer.exe",
            "StartMenuExperienceHost.exe",
            "ShellExperienceHost.exe",
            "SearchApp.exe",
            "SearchHost.exe",
        };

        // --- stato ---

        private readonly WinEventDelegate _winEventProc;   // roots the delegate
        private IntPtr _hookForeground = IntPtr.Zero;
        private IntPtr _hookHideDestroy = IntPtr.Zero;
        private IntPtr _currentHost = IntPtr.Zero;
        private bool _pressed;

        /// <summary>
        /// v2.46: stato confermato del menu Start ("premuto" = menu visibile),
        /// ricavato solo da eventi di sistema reali. Il pulsante della barra lo
        /// usa come fonte di verita' per decidere se il click deve APRIRE o
        /// CHIUDERE: prima quella decisione veniva presa da un flag aggiornato
        /// al MouseDown, che con i click ravvicinati finiva fuori fase e
        /// faceva riaprire il menu invece di chiuderlo.
        /// </summary>
        public bool IsPressed => _pressed && !_disposed;

        /// <summary>
        /// v2.47: il menu Start e' DAVVERO a schermo in questo momento?
        ///
        /// Serve al pulsante della barra per NON riaprire un menu gia' aperto:
        /// lo stato "premuto" e' una conferma che puo' arrivare in ritardo (o
        /// non arrivare affatto, se un hook o una policy filtrano il tasto
        /// Win), mentre qui si guarda la finestra dell'host: se e' visibile il
        /// menu c'e', punto. Il ripiego di apertura parte solo se questa
        /// risposta e' "no".
        /// </summary>
        public bool IsMenuVisible()
        {
            if (_disposed)
            {
                return false;
            }

            try
            {
                IntPtr host = _currentHost;
                if (host != IntPtr.Zero && IsWindowVisible(host))
                {
                    return true;
                }

                // L'host puo' essere cambiato senza che l'evento sia arrivato
                // (menu riaperto da tastiera o da un altro ingresso): in quel
                // caso il primo piano e' la fonte piu' fresca.
                IntPtr foreground = GetForegroundWindow();
                if (foreground != IntPtr.Zero && IsWindowVisible(foreground) &&
                    IsStartHost(foreground))
                {
                    _currentHost = foreground;
                    return true;
                }
            }
            catch
            {
                /* la lettura dello stato non deve mai rompere la barra */
            }

            return false;
        }
        private DispatcherTimer? _openConfirm;             // one-shot, no polling
        private DateTime _openingUntilUtc = DateTime.MinValue; // v2.1
        private bool _disposed;

        public event EventHandler<StartMenuVisibilityEventArgs>? StartMenuVisibilityChanged;

        [DllImport("user32.dll")]
        private static extern bool PostMessageW(IntPtr hWnd, uint msg, UIntPtr wParam, IntPtr lParam);

        /// <summary>v2.7: chiude il menu mandando ESC alla sola finestra host
        /// aperta (mirato, nessuna iniezione globale): e' il tasto che ogni
        /// implementazione del menu start (Win7/10/11, Open-Shell e simili)
        /// usa per chiudersi.</summary>
        [DllImport("user32.dll")]
        private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

        [StructLayout(LayoutKind.Sequential)]
        private struct KEYBDINPUT
        {
            public ushort wVk;
            public ushort wScan;
            public uint dwFlags;
            public uint time;
            public IntPtr dwExtraInfo;
        }

        [StructLayout(LayoutKind.Explicit)]
        private struct INPUT
        {
            [FieldOffset(0)] public uint type;
            [FieldOffset(4)] public KEYBDINPUT ki;
            [FieldOffset(28)] public int _pad; /* union INPUT >= sizeof(MOUSEINPUT) */
        }

        /// <summary>v2.26: tap del tasto Windows: e' il toggle ufficiale del
        /// menu Start su Win10/11 e chiude anche gli host immersivi che
        /// ignorano l'ESC recapitato via PostMessage.</summary>
        private static void SendWinKeyTap()
        {
            const uint INPUT_KEYBOARD = 1;
            const ushort VK_LWIN = 0x5B;
            const uint KEYEVENTF_KEYUP = 0x0002;

            var inputs = new INPUT[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_LWIN;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_LWIN;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, Marshal.SizeOf<INPUT>());
        }

        public void TryCloseStartMenu()
        {
            IntPtr host = _currentHost;
            if (host == IntPtr.Zero || !IsWindowVisible(host))
            {
                return;
            }

            // 1) ESC mirato al solo host aperto: chiude Win7 classico,
            //    Open-Shell e i menu Start "classici".
            const uint WM_KEYDOWN = 0x0100;
            const uint WM_KEYUP = 0x0101;
            const uint VK_ESCAPE = 0x1B;
            PostMessageW(host, WM_KEYDOWN, new UIntPtr(VK_ESCAPE), new IntPtr(0x00010001));
            PostMessageW(host, WM_KEYUP, new UIntPtr(VK_ESCAPE), new IntPtr(0xC0010001));
            _currentHost = IntPtr.Zero;

            // 2) v2.26: gli host immersivi (Win11 XamlExplorerHostIslandWindow)
            //    spesso ignorano l'ESC recapitato via PostMessage. Se dopo
            //    un breve intervallo il menu e' ancora visibile, un tap del
            //    tasto Windows lo chiude (toggle nativo del menu Start).
            IntPtr target = host;
            var check = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(220)
            };
            check.Tick += (_, _) =>
            {
                check.Stop();
                try
                {
                    if (IsWindowVisible(target))
                    {
                        SendWinKeyTap();
                    }
                }
                catch
                {
                    // la chiusura del menu non deve mai rompere la barra
                }
            };
            check.Start();
        }

        public StartMenuMonitor()
        {
            _winEventProc = OnWinEvent;

            // WINEVENT_OUTOFCONTEXT: la callback arriva pompano i messaggi
            // del thread che ha installato l'hook (il thread UI di WPF):
            // nessuna iniezione, nessuna coda globale.
            _hookForeground = SetWinEventHook(
                EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, IntPtr.Zero,
                _winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

            // Un solo hook a RANGE per destroy+hide dell'host: l'host del
            // menu che viene distrutto o nascosto (Win10/11 chiudono e
            // ricreano le isole) chiude lo stato in modo deterministico.
            // Il filtro OBJID_WINDOW + CHILDID_SELF nella callback tiene
            // fuori il rumore degli oggetti non finestra.
            _hookHideDestroy = SetWinEventHook(
                EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, IntPtr.Zero,
                _winEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        }

        private void OnWinEvent(IntPtr hook, uint eventType, IntPtr hwnd,
                               int idObject, int idChild, uint thread, uint time)
        {
            if (_disposed || hwnd == IntPtr.Zero)
            {
                return;
            }

            switch (eventType)
            {
                case EVENT_SYSTEM_FOREGROUND:
                    // Lo stato "premuto" e' la visibilita' del menu, e la
                    // sua visibilita' si misura dalla finestra di primo
                    // piano: se E' l'host Start, premuto; altrimenti no.
                    bool isHost = IsStartHost(hwnd);
                    if (isHost)
                    {
                        _currentHost = hwnd;
                        CancelConfirm();
                        _openingUntilUtc = DateTime.MinValue; // v2.1: confermato
                        SetPressed(true);
                    }
                    else if (_pressed)
                    {
                        // v2.1: durante l'APERTURA (click sull'orb -> tasto
                        // Win -> l'host prende il primo piano) passano per il
                        // primo piano altre finestre (la barra stessa, la
                        // ricerca...). Quei foreground transitori non sono la
                        // chiusura del menu: ignorarli evita la sequenza
                        // IDLE->HOVER->IDLE->PRESSED che si vedeva premendo
                        // l'orb. Decidera' la one-shot di conferma se il menu
                        // non si apre davvero.
                        if (DateTime.UtcNow < _openingUntilUtc)
                        {
                            break;
                        }
                        // Primo piano altrove con Start "aperto": per la
                        // semantica Win7 il menu si sta chiudendo.
                        _currentHost = IntPtr.Zero;
                        SetPressed(false);
                    }
                    break;

                case EVENT_OBJECT_HIDE:
                case EVENT_OBJECT_DESTROY:
                    if (idObject == 0 /*OBJID_WINDOW*/ && idChild == 0 /*CHILDID_SELF*/
                        && _currentHost != IntPtr.Zero && hwnd == _currentHost)
                    {
                        _currentHost = IntPtr.Zero;
                        SetPressed(false);
                    }
                    break;
            }
        }

        private static bool IsStartHost(IntPtr hwnd)
        {
            var sb = new StringBuilder(256);
            if (GetClassName(hwnd, sb, sb.Capacity) <= 0)
            {
                return false;
            }
            string cls = sb.ToString();
            if (!StartHostClasses.Contains(cls))
            {
                return false;
            }

            // Le due classi generiche (CoreWindow / isola XAML) sono usate
            // anche da altro: si richiede il processo proprietario giusto
            // e la visibilita' reale della finestra.
            if (cls == "Windows.UI.Core.CoreWindow" || cls == "XamlExplorerHostIslandWindow")
            {
                if (!IsWindowVisible(hwnd))
                {
                    return false;
                }
                try
                {
                    GetWindowThreadProcessId(hwnd, out uint pid);
                    if (pid == 0)
                    {
                        return false;
                    }
                    using Process p = Process.GetProcessById((int)pid);
                    return StartHostProcesses.Contains(p.ProcessName + ".exe");
                }
                catch (ArgumentException)
                {
                    return false;   // processo appena terminato
                }
                catch (InvalidOperationException)
                {
                    return false;
                }
            }
            return true;
        }

        /// <summary>
        /// Il click sul pulsante e' un evento reale dell'utente: pressed va
        /// mostrato subito; se l'apertura non avviene, il foreground (o il
        /// timeout di conferma) lo rilascia.
        /// </summary>
        public void NotifyStartMenuOpened()
        {
            if (_disposed)
            {
                return;
            }
            SetPressed(true);

            // v2.1: finestra di apertura durante la quale i foreground
            // transitori non contano come chiusura (vedi OnWinEvent).
            _openingUntilUtc = DateTime.UtcNow.AddMilliseconds(1200);

            // One-shot di sola conferma, non un poll: se in 900 ms il
            // primo piano non e' diventato l'host Start, il menu non si e'
            // aperto (tasto disabilitato, policy, host morto) e si torna
            // idle da soli.
            _openConfirm ??= new DispatcherTimer(DispatcherPriority.Background)
            {
                Interval = TimeSpan.FromMilliseconds(900),
            };
            _openConfirm.Tick -= OnConfirmTick;
            _openConfirm.Tick += OnConfirmTick;
            _openConfirm.Stop();
            _openConfirm.Start();
        }

        private void OnConfirmTick(object? sender, EventArgs e)
        {
            CancelConfirm();
            _openingUntilUtc = DateTime.MinValue;   // v2.1: fine finestra
            if (!IsStartHost(GetForegroundWindow()))
            {
                _currentHost = IntPtr.Zero;
                SetPressed(false);
            }
        }

        private void CancelConfirm()
        {
            if (_openConfirm != null)
            {
                _openConfirm.Stop();
                _openConfirm.Tick -= OnConfirmTick;
            }
        }

        private void SetPressed(bool pressed)
        {
            // Nessuna euristica "stato precedente": si pubblica solo la
            // transizione reale. Se premuto non cambia, nessun evento e
            // nessun refresh a vuoto.
            if (pressed == _pressed)
            {
                return;
            }
            _pressed = pressed;
            StartMenuVisibilityChanged?.Invoke(this, new StartMenuVisibilityEventArgs { Visible = pressed });
        }

        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            CancelConfirm();
            if (_hookForeground != IntPtr.Zero)
            {
                UnhookWinEvent(_hookForeground);
                _hookForeground = IntPtr.Zero;
            }
            if (_hookHideDestroy != IntPtr.Zero)
            {
                UnhookWinEvent(_hookHideDestroy);
                _hookHideDestroy = IntPtr.Zero;
            }
        }
    }
}
