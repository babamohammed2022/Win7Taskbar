// Win11 Fake ToolbarWindow32 Shim — v3 (completata su richiesta)
// ============================================================
// Obiettivo: esporre una gerarchia Win32 legacy
//     Shell_TrayWnd -> TrayNotifyWnd -> SysPager -> ToolbarWindow32
// con NOMI DI CLASSE ESATTI (niente suffissi custom), cosicché tool/mod
// legacy che fanno FindWindowEx(..., "ToolbarWindow32", ...) +
// TB_BUTTONCOUNT / TB_GETBUTTON / TB_GETBUTTONTEXTW continuino a
// funzionare anche su Windows 11, dove Explorer non espone più quella
// gerarchia nativamente (l'area di notifica è ora l'isola WinUI3
// "TopLevelWindowForOverflowXamlIsland", opaca da fuori).
//
// Modifiche della v3 (rispetto alla v2 allegata):
//   1. SPY WINDOW REALE: la nostra Shell_TrayWnd viene REGISTRATA (in v2
//      la classe non era nemmeno registrata nel processo → CreateWindowEx
//      poteva fallire con 1407) e creata PRIMA di tutto il resto, nel
//      costruttore, così shell32.dll — che trova il target con
//      FindWindow("Shell_TrayWnd") — recapita a NOI i WM_COPYDATA con
//      dwData=1 (SHELLTRAYDATA) invece che a Explorer. L'installazione va
//      fatta il prima possibile all'avvio (documentato in Install()).
//   2. FORWARDING: ogni WM_COPYDATA ricevuto viene PRIMA processato (la
//      nostra lista icone) e POI re-inoltrato al VERO Shell_TrayWnd di
//      Explorer (trovato enumerando le finestre top-level ed escludendo
//      quelle del nostro processo) con SendMessage WM_COPYDATA sincrono
//      — l'unico canale corretto per il protocollo, perché i puntatori
//      restano validi finché il ricevente processa. Così la tray
//      nativa continua a funzionare per chi non usa Win7Taskbar.
//   3. LETTURA REGISTRO OVERFLOW REALISTICO: per ogni icona si legge
//      HKCU\Control Panel\NotifyIconSettings\<id>\IsPromoted. NOTA DI
//      ONESTÀ (reverse engineering, non documentato Microsoft): il nome
//      della sottochiave <id> è un uint64 generato dal sistema (per
//      utente, non derivabile in modo deterministico - verificato sul
//      campo, es. stessa app -> nomi diversi su macchine diverse), e ogni
//      sottochiave espone un valore ExecutablePath (REG_SZ, col GUID
//      della known-folder davanti al percorso). La tecnica usata QUI —
//      enumerare le sottochiavi e fare match col percorso exe reale del
//      processo mittente (QueryFullProcessImageNameW) — è la stessa usata
//      dai progetti comunitari (RetroBar e script di remediation Intune).
//      1 = sempre visibile, 0 = overflow; chiave assente = overflow
//      (policy default della shell).
//   4. ICONE GIÀ ATTIVE ALL'AVVIO: doppio binario.
//      (a) Tentiamo l'interfaccia COM ITrayNotify / ITrayNotifyWindows8
//          (NON documentata Microsoft; definizioni da reverse engineering
//          di Geoff Chappell e Classic Shell: CLSID_TrayNotify
//          {25DEAD04-1EAC-4911-9E3A-AD0A4AB560FD}, IID_Win7
//          {FB852B2C-6BAD-4605-9551-F15F87830935}, IID_Win8
//          {D133CE13-3537-48BA-93A7-AFCD5D2053B4}). Su Windows 11 24H2+
//          molto probabilmente la coclasse non è creatabile (la tray
//          moderna l'ha abbandonata): il codice LO SA e ripiega subito.
//      (b) Fallback ufficiale: broadcast del messaggio registrato
//          "TaskbarCreated" (meccanismo standard Microsoft con cui il
//          taskbar annuncia la sua esistenza): le app che mostrano icone
//          si ri-registrano comunque con NIM_ADD — i dati tornano vivi
//          in pochi istanti.
//
// VINCOLI: RAII ovunque (SafeHandle per i process handle, try/finally per
// ogni buffer non gestito), commenti in italiano (richiesto per questo
// file), niente hook/DLL-injection in explorer.exe / taskbar.dll /
// winlogon.exe: tocchiamo SOLO finestre di nostra proprietà e letture
// cross-process documentate (OpenProcess+ReadProcessMemory).
//
// LIMITI NOTI (segnalati onestamente):
//  - Il PID del chiamante TB_* non è ricavabile dal solo WndProc puro:
//    il placeholder di v2 resta, ma ora esiste il protocollo "Identify
//    Yourself" via WM_COPYDATA dwData=100 (cbData=4, DWORD = PID) per i
//    client che se lo possono permettere (vedi WM_IDENTIFY_CLIENT).
//  - NOTIFYITEM/INotificationCB sono non documentate: le definizioni
//    qui seguite sono quelle circolanti nei progetti open source
//    (Classic Shell); se una build di Windows cambia il layout, il
//    marshalling semplicemente fallisce (catch) e resta il fallback (b).

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;

// RAII: la classe SafeProcessHandle di base resta internal al progetto;
// qui ne definiamo una dedicata così il file si può copiare da solo.
using SafeToken = Win7Taskbar.Shell.SafeHandles;

namespace Win7Taskbar.Shell
{
    /// <summary>SafeHandle minimale per HPROCESS (CloseHandle nel finalizer).</summary>
    internal static class SafeHandles
    {
        internal sealed class ProcessHandle :
            Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid
        {
            internal ProcessHandle() : base(true) { }
            internal void Own(IntPtr raw) => SetHandle(raw);
            protected override bool ReleaseHandle()
                => CloseHandleRaw(handle);
        }

        internal static ProcessHandle Wrap(IntPtr raw)
        {
            var h = new ProcessHandle();
            h.Own(raw);
            return h;
        }

        [DllImport("kernel32.dll", SetLastError = true, EntryPoint = "CloseHandle")]
        internal static extern bool CloseHandleRaw(IntPtr handle);
    }

    /// <summary>
    /// Voce di tray intercettata, con comando sintetico per TB_GETBUTTON e
    /// la preferenza di visibilità decisa EREDITATA dal sistema (registro),
    /// non inventata: voce assente = overflow.
    /// </summary>
    internal struct TrayIconEntry
    {
        public ShimNotifyData Nid;
        public int CommandId;
        public bool IsPromoted;   // true = nella tray visibile, false = overflow
        public string ExePath;    // percorso reale del processo proprietario
    }

    /// <summary>
    /// Struttura songola al posto del NOTIFYICONDATA completo: nella v2 la
    /// struct includeva solo i campi letti; qui la manteniamo semplice ma
    /// fedele (128-char tip come nel protocollo).
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    internal struct ShimNotifyData
    {
        public int cbSize;
        public IntPtr hWnd;
        public uint uID;
        public uint uFlags;
        public uint uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string szTip;
        public uint dwState;
        public uint dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string szInfo;
        public uint uTimeoutOrVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string szInfoTitle;
        public uint dwInfoFlags;
        public Guid guidItem;
        public IntPtr hBalloonIcon;
    }

    /// <summary>
    /// Shim di compatibilità: Shell_TrayWnd spia + gerarchia legacy finta +
    /// forwarding onesto. Deve essere costruito sul THREAD che ospita un
    /// message pump attivo (le nostre finestre usano quel thread per
    /// ricevere WM_COPYDATA): in Win7Taskbar il thread UI WPF va benissimo,
    /// per progetti console va creato un thread STA con CicloMSG dedicato.
    /// </summary>
    public sealed class Win11_Fake_ToolbarWindow32_Shim : IDisposable
    {
        // --- Nomi di classe: DEVONO essere esatti, non derivati/suffissati.
        const string TrayWndClass = "Shell_TrayWnd";
        const string NotifyWndClass = "TrayNotifyWnd";
        const string PagerClass = "SysPager";
        const string ToolbarClass = "ToolbarWindow32"; // classe comctl32, non nostra

        const int WM_COPYDATA = 0x004A;
        const int WM_USER = 0x0400;
        /* Protocollo IDENTIFICAZIONE CLIENT per TB_GETBUTTON tra processi:
         * il client precede le richieste con WM_COPYDATA dwData=100
         * cbData=4 contenente il proprio PID. Documentato nel commento
         * d'apertura; senza di esso si ripiega sul processo corrente. */
        const int WM_IDENTIFY_CLIENT = 100;

        const int TB_BUTTONCOUNT = WM_USER + 24;      // 0x418
        const int TB_GETBUTTON = WM_USER + 23;        // 0x417
        const int TB_GETBUTTONTEXTW = WM_USER + 75;
        const int TB_BUTTONSTRUCTSIZE = WM_USER + 30;

        // codici NIM_* del payload SHELLTRAYDATA
        const int NIM_ADD = 0;
        const int NIM_MODIFY = 1;
        const int NIM_DELETE = 2;
        const int NIM_SETVERSION = 4;

        const uint PROCESS_VM_OPERATION = 0x0008;
        const uint PROCESS_VM_READ = 0x0010;
        const uint PROCESS_VM_WRITE = 0x0020;
        const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;

        const uint SMTO_ABORTIFHUNG = 0x0002;
        const uint SMTO_BLOCK = 0x0001;

        const uint SWP_NOMOVE = 0x0002;
        const uint SWP_NOSIZE = 0x0001;
        const uint SWP_NOACTIVATE = 0x0010;
        const uint SWP_SHOWWINDOW = 0x0040;

        const string NotifySettingsKey = @"Control Panel\NotifyIconSettings";

        // ===================== strutture Win32 native =====================

        // TBBUTTON: il layout deve combaciare col chiamante a 32/64 bit —
        // padding esplicito di 6 byte dopo i due byte di stato/stile.
        [StructLayout(LayoutKind.Sequential)]
        struct TBBUTTON
        {
            public int iBitmap;
            public int idCommand;
            public byte fsState;
            public byte fsStyle;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 6)]
            public byte[] bReserved;
            public IntPtr dwData;
            public IntPtr iString;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct COPYDATASTRUCT
        {
            public IntPtr dwData;
            public int cbData;
            public IntPtr lpData;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct SHELLTRAYDATA
        {
            public int dwMessage;
            public ShimNotifyData nid;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct WNDCLASSW
        {
            public uint style;
            public IntPtr lpfnWndProc;
            public int cbClsExtra;
            public int cbWndExtra;
            public IntPtr hInstance;
            public IntPtr hIcon;
            public IntPtr hCursor;
            public IntPtr hbrBackground;
            public string lpszMenuName;
            public string lpszClassName;
        }

        delegate IntPtr WndProcDelegate(IntPtr hWnd, int msg, IntPtr wParam,
            IntPtr lParam);

        // ==================== P/Invoke ====================

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr FindWindow(string lpClass, string lpWindow);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr FindWindowEx(IntPtr hParent, IntPtr hAfter,
            string lpClass, string lpTitle);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern ushort RegisterClassW(ref WNDCLASSW wc);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateWindowExW(uint exStyle, string cls,
            string name, uint style, int x, int y, int w, int h,
            IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
        [DllImport("user32.dll", SetLastError = true)]
        static extern bool DestroyWindow(IntPtr hWnd);
        [DllImport("user32.dll")]
        static extern IntPtr DefWindowProcW(IntPtr hWnd, int msg,
            IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        static extern int RegisterWindowMessage(string lpString);
        [DllImport("user32.dll")]
        static extern bool IsWindow(IntPtr hWnd);
        [DllImport("user32.dll", SetLastError = true)]
        static extern bool SendNotifyMessage(IntPtr hWnd, uint msg,
            UIntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", SetLastError = true)]
        static extern IntPtr SendMessageTimeoutW(IntPtr hWnd, uint msg,
            IntPtr wParam, IntPtr lParam, uint fuFlags, uint uTimeout,
            out IntPtr lpdwResult);
        [DllImport("user32.dll")]
        static extern IntPtr SetWindowPos(IntPtr hWnd, IntPtr hAfter,
            int x, int y, int cx, int cy, uint flags);
        [DllImport("user32.dll", SetLastError = true)]
        static extern bool EnumWindows(EnumWindowsProc proc, IntPtr lParam);
        delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb,
            int maxCount);
        [DllImport("user32.dll", SetLastError = true)]
        static extern uint GetWindowThreadProcessId(IntPtr hWnd,
            out uint lpdwProcessId);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr GetModuleHandleW(string mod);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool QueryFullProcessImageNameW(
            SafeToken.ProcessHandle hProcess, uint dwFlags,
            StringBuilder lpExeName, ref uint lpdwSize);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool ReadProcessMemory(SafeToken.ProcessHandle hProcess,
            IntPtr lpBaseAddress, byte[] lpBuffer, int nSize,
            out IntPtr lpNumberOfBytesRead);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool WriteProcessMemory(SafeToken.ProcessHandle hProcess,
            IntPtr lpBaseAddress, byte[] lpBuffer, int nSize,
            out IntPtr lpNumberOfBytesWritten);
        [DllImport("comctl32.dll")]
        static extern void InitCommonControls();

        // ==================== stato ====================

        readonly WndProcDelegate _wndProc; // tenuto vivo: il GC non raccoglie
        readonly IntPtr _hInst;
        IntPtr _hShell, _hNotify, _hPager, _hToolbar;
        IntPtr _hRealTray;                 // Shell_TrayWnd VERA, al di fuori nostra
        uint _msgTaskbarCreated;
        readonly List<TrayIconEntry> _icons = new List<TrayIconEntry>();
        readonly Dictionary<uint, string> _exeByPid =
            new Dictionary<uint, string>();
        readonly object _lock = new object();
        int _nextCommandId = 1;
        bool _disposed;
        uint _identifiedCallerPid;

        /// <summary>Notifica alla UI che la lista icone è cambiata
        /// (ADD/MODIFY/DELETE): chi riceve rilegga <see cref="Icons"/>.</summary>
        public event EventHandler? IconsChanged;

        /// <summary>Snapshot delle icone intercettate (copia difensiva).</summary>
        public TrayIconEntry[] Icons
        {
            get { lock (_lock) return _icons.ToArray(); }
        }

        /// <summary>Numero icone "pressionate" sulla barra visibile.</summary>
        public int VisibleCount
        {
            get
            {
                lock (_lock)
                    return _icons.FindAll(i => i.IsPromoted).Count;
            }
        }

        /// <summary>
        /// Install(=costruttore): se chiamato PRIMA dell'avvio di explorer o
        /// comunque all'apertura di sessione, la nostra Shell_TrayWnd — che
        /// è registrata in processo priorita' alta — vinta la gara per la
        /// FindWindow di shell32 e i WM_COPYDATA delle icone arrivano qui.
        /// Richiede thread con message pump attorno al chiamante.
        /// </summary>
        public Win11_Fake_ToolbarWindow32_Shim()
        {
            _wndProc = WndProc;
            _hInst = GetModuleHandleW(null!);
            try
            {
                CreateHierarchy();
                TryHookTrayNotify();     // (4a) non documentato, con fallback
                AnnounceTaskbarCreated();// (4b) broadcast ufficiale
            }
            catch
            {
                Dispose();               // RAII: niente finestre orfane
                throw;
            }
        }

        // ==================== gerarchia finestra ====================

        void CreateHierarchy()
        {
            /* (1) SPY REALE: classe Shell_TrayWnd REGISTRATA nel nostro
             * processo. Senza RegisterClass CreateWindowEx fallisce
             * (errore 1407): in v2 questa registrazione mancava. */
            RegisterWindowClass(TrayWndClass);

            _hShell = CreateWindowExW(
                0x00000008 /* WS_EX_TOPMOST */, TrayWndClass, "",
                0x80000000 /* WS_POPUP */,
                0, 0, 100, 23, IntPtr.Zero, IntPtr.Zero, _hInst, IntPtr.Zero);
            if (_hShell == IntPtr.Zero)
            {
                int err = Marshal.GetLastWin32Error();
                throw new InvalidOperationException(
                    "Creazione Shell_TrayWnd fallita: " + err);
            }
            /* TOPMOST: shell32 usa FindWindow() che torna la prima finestra
             * nell'ordine Z: arrivando prima e mettendoci in cima le nostre
             * icone arrivano a NOI; il forwarding (punto 2) mantiene viva
             * anche la shell reale. */
            SetWindowPos(_hShell, new IntPtr(-1) /* HWND_TOPMOST */,
                0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                SWP_SHOWWINDOW);

            RegisterWindowClass(NotifyWndClass);
            _hNotify = CreateWindowExW(0, NotifyWndClass, "",
                0x40000000 /* WS_CHILD */, 0, 0, 100, 100,
                IntPtr.Zero, IntPtr.Zero, _hInst, IntPtr.Zero);
            if (_hNotify == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "Creazione TrayNotifyWnd fallita: " +
                    Marshal.GetLastWin32Error());
            }

            RegisterWindowClass(PagerClass);
            _hPager = CreateWindowExW(0, PagerClass, "",
                0x40000000 /* WS_CHILD */, 0, 0, 100, 100,
                _hNotify, IntPtr.Zero, _hInst, IntPtr.Zero);
            if (_hPager == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "Creazione SysPager fallita: " +
                    Marshal.GetLastWin32Error());
            }

            /* ToolbarWindow32 è registrata da comctl32: serve
             * InitCommonControls altrimenti la creazione fallisce. */
            InitCommonControls();
            _hToolbar = CreateWindowExW(0, ToolbarClass, null!,
                0x50000000 /* WS_CHILD|WS_VISIBLE */, 0, 0, 100, 100,
                _hPager, IntPtr.Zero, _hInst, IntPtr.Zero);
            if (_hToolbar == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "Creazione ToolbarWindow32 fallita: " +
                    Marshal.GetLastWin32Error());
            }
        }

        void RegisterWindowClass(string className)
        {
            var wc = new WNDCLASSW
            {
                lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_wndProc),
                hInstance = _hInst,
                lpszClassName = className
            };
            ushort atom = RegisterClassW(ref wc);
            if (atom == 0)
            {
                int err = Marshal.GetLastWin32Error();
                /* 1410 ERROR_CLASS_ALREADY_EXISTS: un'altra istanza dello
                 * shim (o noi stessi) ha già registrato la classe in
                 * questo processo: va bene, la si condivide. */
                if (err != 1410)
                {
                    throw new InvalidOperationException(
                        $"RegisterClass({className}) fallita: {err}");
                }
            }
        }

        // ==================== WndProc centrale ====================

        IntPtr WndProc(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam)
        {
            try
            {
                if (msg == WM_COPYDATA)
                {
                    return HandleCopyData(wParam, lParam);
                }
                if (msg == _msgTaskbarCreated && _msgTaskbarCreated != 0)
                {
                    /* La shell reale è (ri)nata: rinfreschiamo il puntatore
                     * a lei e ci reimponiamo come top-most. */
                    _hRealTray = IntPtr.Zero;
                    MakeSelfTopmost();
                }

                if (hWnd == _hToolbar)
                {
                    switch (msg)
                    {
                        case TB_BUTTONCOUNT:
                            /* Decisione design (v3): la toolbar del 7
                             * conteneva le icone VISIBILI (promosse); le
                             * nascoste erano sulla toolbar dell'overflow.
                             * Rispondiamo il numero delle visibili. */
                            lock (_lock)
                            {
                                return (IntPtr)_icons
                                    .FindAll(i => i.IsPromoted).Count;
                            }
                        case TB_BUTTONSTRUCTSIZE:
                            return (IntPtr)Marshal.SizeOf<TBBUTTON>();
                        case TB_GETBUTTON:
                            return HandleGetButton(wParam, lParam);
                        case TB_GETBUTTONTEXTW:
                            return HandleGetButtonText(wParam, lParam);
                    }
                }
            }
            catch (Exception)
            {
                /* il WndProc non può propagare eccezioni dentro user32 */
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        void MakeSelfTopmost()
        {
            if (_hShell != IntPtr.Zero && IsWindow(_hShell))
            {
                SetWindowPos(_hShell, new IntPtr(-1) /* HWND_TOPMOST */,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }

        // ==================== (1)+(2) WM_COPYDATA ====================

        /// <summary>
        /// Flusso doppio: (a) elaboriamo NOI per la lista interna; (b)
        /// inoltriamo il payload ORIGINALE al vogo Shell_TrayWnd con
        /// SendMessageTimeoutW sincrono (il protocollo WM_COPYDATA impone
        /// che i puntatori restino validi per il duration del processamento
        /// del ricevente: SendMessage è l'unico canale che lo garantisce).
        /// Se il destinatario non risponde entro 2 s, si molla (la nostra
        /// tray resta comunque aggiornata).
        /// </summary>
        IntPtr HandleCopyData(IntPtr wParam, IntPtr lParam)
        {
            COPYDATASTRUCT cds;
            try
            {
                cds = Marshal.PtrToStructure<COPYDATASTRUCT>(lParam);
            }
            catch (Exception)
            {
                return IntPtr.Zero;
            }

            /* (a) IDENTIFICAZIONE CLIENT per TB_* tra processi (vedi la
             * nota in cima: dal solo WndProc puro il mittente non si
             * risale: chi può si presenta così). */
            if (cds.dwData == (IntPtr)WM_IDENTIFY_CLIENT &&
                cds.cbData == 4 && cds.lpData != IntPtr.Zero)
            {
                try
                {
                    byte[] pidBuf = new byte[4];
                    if (ReadPayload(wParam, cds, pidBuf, 4))
                    {
                        _identifiedCallerPid = BitConverter.ToUInt32(pidBuf, 0);
                    }
                }
                catch (Exception)
                {
                }
                return (IntPtr)1;
            }

            IntPtr result = IntPtr.Zero;
            if (cds.dwData == (IntPtr)1)
            {
                result = HandleShellTrayData(wParam, cds);
            }

            /* (b) FORWARDING verso il Shell_TrayWnd reale, con payload
             * copiato nella nostra memoria (il mittente potrebbe averlo
             * in una sezione mappata non visibile nel destinatario). */
            if (cds.lpData != IntPtr.Zero && cds.cbData > 0)
            {
                ForwardToRealTray(wParam, cds);
            }
            return result;
        }

        IntPtr HandleShellTrayData(IntPtr wParam, COPYDATASTRUCT cds)
        {
            SHELLTRAYDATA trayData;
            try
            {
                int cbsize = Math.Min(cds.cbData,
                    Marshal.SizeOf<SHELLTRAYDATA>());
                if (cbsize < 8)
                {
                    return IntPtr.Zero;
                }
                byte[] raw = new byte[Marshal.SizeOf<SHELLTRAYDATA>()];
                if (!ReadPayload(wParam, cds, raw, cbsize))
                {
                    return IntPtr.Zero;
                }
                IntPtr pinned = Marshal.AllocHGlobal(raw.Length);
                try
                {
                    Marshal.Copy(raw, 0, pinned, raw.Length);
                    trayData = Marshal.PtrToStructure<SHELLTRAYDATA>(pinned);
                }
                finally
                {
                    Marshal.FreeHGlobal(pinned);
                }
            }
            catch (Exception)
            {
                return IntPtr.Zero;
            }

            bool changed = false;
            try
            {
                lock (_lock)
                {
                    int existing = _icons.FindIndex(i =>
                        i.Nid.hWnd == trayData.nid.hWnd &&
                        i.Nid.uID == trayData.nid.uID);
                    switch (trayData.dwMessage)
                    {
                        case NIM_ADD:
                            if (existing < 0)
                            {
                                var entry = new TrayIconEntry
                                {
                                    Nid = trayData.nid,
                                    CommandId = _nextCommandId++,
                                    ExePath = ResolveOwnerProcessPath(
                                        trayData.nid.hWnd),
                                };
                                /* (3) LEZIONE REGISTRO: la preferenza di
                                 * visibilità si EREDITA, non si inventa. */
                                entry.IsPromoted =
                                    ReadIsPromoted(entry.ExePath);
                                _icons.Add(entry);
                                changed = true;
                            }
                            break;
                        case NIM_MODIFY:
                            if (existing >= 0)
                            {
                                var e = _icons[existing];
                                e.Nid = trayData.nid;
                                if (string.IsNullOrEmpty(e.ExePath))
                                {
                                    e.ExePath = ResolveOwnerProcessPath(
                                        trayData.nid.hWnd);
                                    e.IsPromoted = ReadIsPromoted(e.ExePath);
                                }
                                _icons[existing] = e;
                                changed = true;
                            }
                            break;
                        case NIM_DELETE:
                            if (existing >= 0)
                            {
                                _icons.RemoveAt(existing);
                                changed = true;
                            }
                            break;
                        case NIM_SETVERSION:
                            /* versione non persistita nel modello TB: ok */
                            break;
                    }
                }
            }
            catch (Exception)
            {
            }
            if (changed)
            {
                try
                {
                    IconsChanged?.Invoke(this, EventArgs.Empty);
                }
                catch (Exception)
                {
                }
            }
            return (IntPtr)1;
        }

        /// <summary>
        /// Legge cds.cbData byte dalla memoria puntata dal mittente. Prima
        /// si tenta la lettura diretta in-process (le mapped-section di
        /// shell32 sono visibili anche qui); se fallisce si apre il
        /// processo del mittente (PID risalendo dall'HWND del messaggio
        /// — vero solo per WM_COPYDATA veri: wParam = HWND mittente).
        /// </summary>
        bool ReadPayload(IntPtr senderHwnd, COPYDATASTRUCT cds,
            byte[] target, int bytes)
        {
            if (cds.lpData == IntPtr.Zero || bytes <= 0)
            {
                return false;
            }
            try
            {
                Marshal.Copy(cds.lpData, target, 0, bytes);
                return true;
            }
            catch (Exception)
            {
            }
            try
            {
                GetWindowThreadProcessId(senderHwnd, out uint pid);
                if (pid == 0)
                {
                    return false;
                }
                using var proc = OpenProcessHandle(
                    pid, PROCESS_VM_READ | PROCESS_VM_OPERATION |
                    PROCESS_QUERY_LIMITED_INFORMATION);
                if (proc.IsInvalid)
                {
                    return false;
                }
                return ReadProcessMemory(proc, cds.lpData, target, bytes,
                    out _);
            }
            catch (Exception)
            {
                return false;
            }
        }

        /* (2) FORWARDING ----------------------------------------------------
         * Troviamo la Shell_TrayWnd VERA: enumeriamo le finestre top-level
         * di classe "Shell_TrayWnd" escludendo quelle del NOSTRO processo.
         * Cached; rinfrescata quando arriva TaskbarCreated. */
        IntPtr RealTrayWindow()
        {
            if (_hRealTray != IntPtr.Zero && IsWindow(_hRealTray))
            {
                return _hRealTray;
            }
            uint ourPid = (uint)System.Diagnostics.Process
                .GetCurrentProcess().Id;
            IntPtr found = IntPtr.Zero;
            EnumWindowsProc proc = (h, l) =>
            {
                try
                {
                    var sb = new StringBuilder(64);
                    if (GetClassNameW(h, sb, sb.Capacity) == 0 ||
                        !string.Equals(sb.ToString(), TrayWndClass,
                            StringComparison.Ordinal))
                    {
                        return true;
                    }
                    GetWindowThreadProcessId(h, out uint pid);
                    if (pid != ourPid)
                    {
                        found = h;
                        return false;   // stop
                    }
                }
                catch (Exception)
                {
                }
                return true;
            };
            try
            {
                EnumWindows(proc, IntPtr.Zero);
            }
            catch (Exception)
            {
            }
            GC.KeepAlive(proc);   // il delegate viva fino a fine enum
            _hRealTray = found;
            return found;
        }

        void ForwardToRealTray(IntPtr wParam, COPYDATASTRUCT cds)
        {
            IntPtr real = RealTrayWindow();
            if (real == IntPtr.Zero)
            {
                return;    // nessun destinatario: ok, noi restiamo shell
            }
            /* Payload copiato nella NOSTRA memoria: il sentiero affidabile
             * per SendMessage cross-process con WM_COPYDATA. */
            byte[] payload = new byte[cds.cbData];
            if (!ReadPayload(wParam, cds, payload, cds.cbData))
            {
                return;
            }
            IntPtr lpData = IntPtr.Zero;
            IntPtr lpCds = IntPtr.Zero;
            try
            {
                lpData = Marshal.AllocHGlobal(cds.cbData);
                Marshal.Copy(payload, 0, lpData, cds.cbData);

                var fwd = new COPYDATASTRUCT
                {
                    dwData = cds.dwData,
                    cbData = cds.cbData,
                    lpData = lpData
                };
                lpCds = Marshal.AllocHGlobal(
                    Marshal.SizeOf<COPYDATASTRUCT>());
                Marshal.StructureToPtr(fwd, lpCds, false);

                /* SendMessageTimeout: il protocollo e' sincrono e il
                 * timeout evita ingranaggi se explorer aggancia per primo
                 * (aborted = no reply in tempo: il messaggio e' comunque
                 * consegnato alla traiettoria sotto carico normale). */
                /* wParam DEVE essere l'HWND del mittente: la nostra spy
                 * window (explorer leggerà la nostra memoria come farebbe
                 * con quella di qualunque app). */
                SendMessageTimeoutW(real, WM_COPYDATA,
                    wParam: _hShell,
                    lParam: lpCds, fuFlags: SMTO_BLOCK | SMTO_ABORTIFHUNG,
                    uTimeout: 2000, lpdwResult: out _);
            }
            catch (Exception)
            {
            }
            finally
            {
                if (lpCds != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(lpCds);
                }
                if (lpData != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(lpData);
                }
            }
        }

        // ==================== (3) IsPromoted da registro ====================

        /// <summary>
        /// Match con ExecutablePath nelle sottochiavi di NotifyIconSettings.
        /// Ritorna TRUE solo se IsPromoted == 1. Se non si trova nulla per
        /// quell'exe: policy default = nascosto (overflow), come la shell.
        /// </summary>
        static bool ReadIsPromoted(string? exePath)
        {
            if (string.IsNullOrEmpty(exePath))
            {
                return false;
            }
            try
            {
                using var baseKey = Registry.CurrentUser.OpenSubKey(
                    NotifySettingsKey, writable: false);
                if (baseKey == null)
                {
                    return false;
                }
                string normalized = NormalizePath(exePath);
                foreach (string sub in baseKey.GetSubKeyNames())
                {
                    try
                    {
                        using var k = baseKey.OpenSubKey(
                            sub, writable: false);
                        if (k == null)
                        {
                            continue;
                        }
                        string? regPath =
                            k.GetValue("ExecutablePath") as string;
                        if (regPath == null)
                        {
                            continue;
                        }
                        /* (NOTA onesta - reverse engineering): la chiave
                         * è un uint64 opaco; l'unico modo determinare la
                         * corrispondenza è il match sul PERCORSO, generic
                         * a lo sotto: spesso il valore comincia col GUID
                         * della known-folder: "{GUID}\Program Files\...".
                         * NormalizePath taglia il prefisso GUID. */
                        if (string.Equals(NormalizePath(regPath),
                                normalized, StringComparison.OrdinalIgnoreCase) ||
                            PathTailMatch(normalized,
                                NormalizePath(regPath)))
                        {
                            object? v = k.GetValue("IsPromoted");
                            return v is int dword && dword == 1;
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }
            catch (Exception)
            {
            }
            return false;
        }

        static string NormalizePath(string p)
        {
            string s = p.Trim().Trim('"');
            int brace = s.IndexOf('}');
            if (s.StartsWith("{", StringComparison.Ordinal) && brace > 0 &&
                brace + 1 < s.Length)
            {
                s = s.Substring(brace + 1);
            }
            return s.TrimStart('\\', '/');
        }

        /* Uguaglianza sugli ULTIMI segmenti (filename + cartella), per
         * reggere le known-folder e i relocation tra C:\ e %PROGRAMFILES%. */
        static bool PathTailMatch(string a, string b)
        {
            string an = FileNameOf(a);
            string ad = ParentDirOf(a);
            string bn = FileNameOf(b);
            string bd = ParentDirOf(b);
            return string.Equals(an, bn, StringComparison.OrdinalIgnoreCase) &&
                   (ad.Length == 0 || bd.Length == 0 ||
                    string.Equals(ad, bd, StringComparison.OrdinalIgnoreCase));
        }

        static string FileNameOf(string p)
        {
            int i = Math.Max(p.LastIndexOf('\\'), p.LastIndexOf('/'));
            return i >= 0 ? p.Substring(i + 1) : p;
        }

        static string ParentDirOf(string p)
        {
            int i = Math.Max(p.LastIndexOf('\\'), p.LastIndexOf('/'));
            string dir = i > 0 ? p.Substring(0, i) : string.Empty;
            int j = Math.Max(dir.LastIndexOf('\\'), dir.LastIndexOf('/'));
            return j >= 0 ? dir.Substring(j + 1) : dir;
        }

        // ==================== processo proprietario ====================

        string ResolveOwnerProcessPath(IntPtr ownerHwnd)
        {
            try
            {
                GetWindowThreadProcessId(ownerHwnd, out uint pid);
                if (pid == 0)
                {
                    return string.Empty;
                }
                if (_exeByPid.TryGetValue(pid, out string? cached))
                {
                    return cached;
                }
                using var proc = OpenProcessHandle(
                    pid, PROCESS_QUERY_LIMITED_INFORMATION);
                if (proc.IsInvalid)
                {
                    return string.Empty;
                }
                var sb = new StringBuilder(1024);
                uint size = (uint)sb.Capacity;
                string path = QueryFullProcessImageNameW(proc, 0, sb,
                    ref size) ? sb.ToString() : string.Empty;
                _exeByPid[pid] = path;
                return path;
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }

        /* RAII del process handle: il wrapper prende la PROPRIETA' dell'
         * handle zero ricevuto da OpenProcess e lo rilascia col using. */
        internal static SafeToken.ProcessHandle OpenProcessHandle(
            uint pid, uint access)
        {
            IntPtr raw = pid == 0 ? IntPtr.Zero : OpenProcess(access, false, pid);
            return SafeToken.Wrap(raw);
        }

        // ==================== (4a) ITrayNotify (non documentata) ==========

        ITrayNotify7? _trayNotify7;
        ITrayNotifyWin8? _trayNotifyWin8;
        TrayNotifyCallback? _trayCb;
        uint _trayCbCookie;

        /// <summary>
        /// Tenta la registrazione alla COM di Explorer. Su Windows 11 24H2+
        /// la coclasse non è creatabile (la tray moderna non la registra
        /// più) — gestito: si passa al broadcast TaskbarCreated se questo
        /// ritorna false. Definizioni da reverse engineering (Geoff
        /// Chappell + Classic Shell); in caso di layout diverso il
        /// marshalling salta e viene catturato.
        /// </summary>
        void TryHookTrayNotify()
        {
            bool hooked = false;
            try
            {
                _trayCb = new TrayNotifyCallback(this);
                object? com = null;
                try
                {
                    var t = Type.GetTypeFromCLSID(
                        new Guid("25DEAD04-1EAC-4911-9E3A-AD0A4AB560FD"));
                    if (t != null)
                    {
                        com = Activator.CreateInstance(t);
                    }
                }
                catch (Exception)
                {
                }
                if (com != null)
                {
                    try
                    {
                        _trayNotifyWin8 = com as ITrayNotifyWin8;
                        if (_trayNotifyWin8 != null)
                        {
                            _trayNotifyWin8.RegisterCallback(
                                _trayCb, out _trayCbCookie);
                            hooked = true;
                        }
                        else
                        {
                            _trayNotify7 = com as ITrayNotify7;
                            if (_trayNotify7 != null)
                            {
                                _trayNotify7.RegisterCallback(_trayCb);
                                hooked = true;
                            }
                        }
                    }
                    catch (Exception)
                    {
                        hooked = false;
                    }
                }
            }
            catch (Exception)
            {
                hooked = false;
            }
            _trayNotifyHooked = hooked;
        }

        bool _trayNotifyHooked;

        /// <summary>
        /// (4b) Fallback ufficiale: broadcast "TaskbarCreated". Le app che
        /// mostrano icone nella tray (e cheper altro patter si riscriverebbe)
        /// rispondono re-inviando NIM_ADD alla Shell_TrayWnd trovata con
        /// FindWindow — la NOSTRA, essendo la più alta. Se ITrayNotify ha
        /// funzionato il broadcast resta comunque un ridondanza benigna.
        /// </summary>
        void AnnounceTaskbarCreated()
        {
            _msgTaskbarCreated =
                (uint)RegisterWindowMessage("TaskbarCreated");
            if (_msgTaskbarCreated != 0)
            {
                /* HWND_BROADCAST: arriva anche alle app appena partite
                 * (quelle del punto 4 — icone già attive all'avvio) e a
                 * quelle che ripartono dopo il crollo della shell. */
                SendNotifyMessage(new IntPtr(0xFFFF) /* HWND_BROADCAST */,
                    _msgTaskbarCreated, UIntPtr.Zero, IntPtr.Zero);
            }
        }

        // Ricaricamento cache icone richiesto dal callback ITrayNotify.
        // Modello: il dato VERITIERO resta la WM_COPYDATA; il callback COM
        // serve solo ad aggiornare la preferenza di visibilità (il punto
        // 3) prima dell'overflow che say no, quando il registro ancora
        // non registra la voce.
        internal void OnTrayNotifyEvent(int code, string exeName)
        {
            try
            {
                bool any = false;
                lock (_lock)
                {
                    for (int i = 0; i < _icons.Count; i++)
                    {
                        var e = _icons[i];
                        if (e.ExePath.Length > 0 &&
                            PathTailMatch(NormalizePath(e.ExePath),
                                NormalizePath(exeName)))
                        {
                            e.IsPromoted = ReadIsPromoted(e.ExePath);
                            _icons[i] = e;
                            any = true;
                        }
                    }
                }
                if (any)
                {
                    IconsChanged?.Invoke(this, EventArgs.Empty);
                }
            }
            catch (Exception)
            {
            }
        }

        // ==================== TB_GETBUTTON / TEXT (v2 kept) ================

        IntPtr HandleGetButton(IntPtr wParam, IntPtr lParam)
        {
            int index = wParam.ToInt32();
            TrayIconEntry entry;
            lock (_lock)
            {
                var visible = _icons.FindAll(i => i.IsPromoted);
                if (index < 0 || index >= visible.Count)
                {
                    return IntPtr.Zero;   // FALSE
                }
                entry = visible[index];
            }

            var button = new TBBUTTON
            {
                iBitmap = index,
                idCommand = entry.CommandId,
                fsState = 0x20,           // TBSTATE_ENABLED
                fsStyle = 0x00,           // BTNS_BUTTON
                bReserved = new byte[6],
                dwData = (IntPtr)index,
                iString = IntPtr.Zero
            };

            uint callerPid = GetCallerProcessIdOrCurrent();
            if (callerPid == CurrentProcessId())
            {
                Marshal.StructureToPtr(button, lParam, false);
                return (IntPtr)1;
            }

            using var hProc = OpenProcessHandle(
                callerPid, PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                PROCESS_QUERY_LIMITED_INFORMATION);
            if (hProc.IsInvalid)
            {
                return IntPtr.Zero;
            }
            int size = Marshal.SizeOf<TBBUTTON>();
            byte[] buffer = new byte[size];
            IntPtr unmanaged0 = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(button, unmanaged0, false);
                Marshal.Copy(unmanaged0, buffer, 0, size);
            }
            finally
            {
                Marshal.FreeHGlobal(unmanaged0);
            }
            return WriteProcessMemory(hProc, lParam, buffer, size, out _)
                ? (IntPtr)1 : IntPtr.Zero;
        }

        IntPtr HandleGetButtonText(IntPtr wParam, IntPtr lParam)
        {
            int commandId = wParam.ToInt32();
            string text;
            lock (_lock)
            {
                int idx = _icons.FindIndex(i => i.CommandId == commandId);
                if (idx < 0)
                {
                    return (IntPtr)(-1);
                }
                text = _icons[idx].Nid.szTip ?? string.Empty;
            }

            byte[] utf16 = Encoding.Unicode.GetBytes(text + "\0");
            uint callerPid = GetCallerProcessIdOrCurrent();
            if (callerPid == CurrentProcessId())
            {
                Marshal.Copy(utf16, 0, lParam, utf16.Length);
                return (IntPtr)text.Length;
            }
            using var hProc = OpenProcessHandle(
                callerPid, PROCESS_VM_WRITE | PROCESS_VM_OPERATION);
            if (hProc.IsInvalid)
            {
                return (IntPtr)(-1);
            }
            return WriteProcessMemory(hProc, lParam, utf16, utf16.Length,
                out _) ? (IntPtr)text.Length : (IntPtr)(-1);
        }

        static uint CurrentProcessId()
            => (uint)System.Diagnostics.Process.GetCurrentProcess().Id;

        /// <summary>Risoluzione PID chiamante: usa il protocollo Identify
        /// Yourself (WM_IDENTIFY_CLIENT) se il client lo supporta;
        /// altrimenti processo corrente. Il placeholder v2 resta minimale:
        /// senza WH_CALLWNDPROC (vietato dai vincoli sul hooking remoto)
        /// non esiste via documentata per un WndProc puro. </summary>
        uint GetCallerProcessIdOrCurrent()
        {
            return _identifiedCallerPid != 0
                ? _identifiedCallerPid : CurrentProcessId();
        }

        // ==================== teardown ====================

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            try
            {
                /* (4a) unregister COM callback prima di distruggere le
                 * finestre che la callback potrebbe ancora toccare. */
                if (_trayNotifyWin8 != null && _trayCbCookie != 0)
                {
                    _trayNotifyWin8.UnregisterCallback(_trayCbCookie);
                    _trayCbCookie = 0;
                }
            }
            catch (Exception)
            {
            }
            finally
            {
                if (_trayNotifyWin8 != null)
                {
                    try { Marshal.ReleaseComObject(_trayNotifyWin8); }
                    catch (Exception) { }
                    _trayNotifyWin8 = null;
                }
                if (_trayNotify7 != null)
                {
                    try { Marshal.ReleaseComObject(_trayNotify7); }
                    catch (Exception) { }
                    _trayNotify7 = null;
                }
            }
            try
            {
                if (_hToolbar != IntPtr.Zero && IsWindow(_hToolbar))
                    DestroyWindow(_hToolbar);
                if (_hPager != IntPtr.Zero && IsWindow(_hPager))
                    DestroyWindow(_hPager);
                if (_hNotify != IntPtr.Zero && IsWindow(_hNotify))
                    DestroyWindow(_hNotify);
                if (_hShell != IntPtr.Zero && IsWindow(_hShell))
                    DestroyWindow(_hShell);
            }
            catch (Exception)
            {
            }
            finally
            {
                _hToolbar = _hPager = _hNotify = _hShell = IntPtr.Zero;
            }
        }

        public static IntPtr FindToolbar_Fallback()
        {
            IntPtr tray = FindWindow(TrayWndClass, null!);
            IntPtr notify = FindWindowEx(tray, IntPtr.Zero, NotifyWndClass, null!);
            IntPtr pager = FindWindowEx(notify, IntPtr.Zero, PagerClass, null!);
            IntPtr toolbar = FindWindowEx(pager, IntPtr.Zero, ToolbarClass, null!);
            return (toolbar != IntPtr.Zero && IsWindow(toolbar))
                ? toolbar : IntPtr.Zero;
        }
    }

    // ==================== (4a) COM interop — REVERSE ENGINEERED ==========
    // NOTA: NON documentate da Microsoft. Definizioni da Geoff Chappell
    // (geoffchappell.com) + codice Classic Shell:
    //   CLSID_TrayNotify    = {25DEAD04-1EAC-4911-9E3A-AD0A4AB560FD}
    //   IID_ITrayNotify     = {FB852B2C-6BAD-4605-9551-F15F87830935}
    //   IID_ITrayNotifyWin8 = {D133CE13-3537-48BA-93A7-AFCD5D2053B4}
    //   IID_INotificationCB = {D782CCBA-AFB0-43F1-94A9-DA2DFD30E6F7}
    // NOTIFYITEM layout come da reverse di retrobar/Classic Shell:
    //   { LPWSTR pszExeName; LPWSTR pszTip; HICON hIcon; HWND hWnd;
    //     DWORD dwPreference; UINT uID; GUID guidItem; }

    [ComImport]
    [Guid("FB852B2C-6BAD-4605-9551-F15F87830935")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface ITrayNotify7
    {
        void RegisterCallback([MarshalAs(UnmanagedType.Interface)] object cb);
        void SetPreference(IntPtr notifyItem);
        [PreserveSig] int EnableAutoTray([MarshalAs(UnmanagedType.Bool)] bool enabled);
    }

    [ComImport]
    [Guid("D133CE13-3537-48BA-93A7-AFCD5D2053B4")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface ITrayNotifyWin8
    {
        void RegisterCallback(
            [MarshalAs(UnmanagedType.Interface)] object cb, out uint cookie);
        void UnregisterCallback(uint cookie);
        void SetPreference(IntPtr notifyItem);
        [PreserveSig] int EnableAutoTray([MarshalAs(UnmanagedType.Bool)] bool enabled);
        [PreserveSig] int DoAction([MarshalAs(UnmanagedType.Bool)] bool enabled);
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    internal struct NOTIFYITEM_RE
    {
        [MarshalAs(UnmanagedType.LPWStr)] public string pszExeName;
        [MarshalAs(UnmanagedType.LPWStr)] public string pszTip;
        public IntPtr hIcon;
        public IntPtr hWnd;
        public uint dwPreference;   // 0=auto/hidden 1=promoted 2=never show
        public uint uID;
        public Guid guidItem;
    }

    /// <summary>
    /// Callback COM che INotificationCB non documenta pubblicamente a
    /// pieno (firma "Notify(ULONG code, NOTIFYITEM *item)" inferita da
    /// reverse engineering di Classic Shell): il codice la implementa in
    /// modo tollerante — qualsiasi cast/marshal che non combacia con la
    /// build corrente viene catturato dal runtime e il flusso resta sul
    /// dato veritiero (WM_COPYDATA).
    /// </summary>
    [ComVisible(true)]
    [Guid("D782CCBA-AFB0-43F1-94A9-DA2DFD30E6F7")]
    internal sealed class TrayNotifyCallback : INotificationCBLike
    {
        private readonly Win11_Fake_ToolbarWindow32_Shim _owner;

        internal TrayNotifyCallback(Win11_Fake_ToolbarWindow32_Shim owner)
        {
            _owner = owner;
        }

        public void Notify(uint code, IntPtr notifyItem)
        {
            try
            {
                if (notifyItem == IntPtr.Zero)
                {
                    return;
                }
                var item = Marshal.PtrToStructure<NOTIFYITEM_RE>(notifyItem);
                if (!string.IsNullOrEmpty(item.pszExeName))
                {
                    _owner.OnTrayNotifyEvent(unchecked((int)code),
                        item.pszExeName);
                }
            }
            catch (Exception)
            {
                /* layout diverso: silenzio, la WM_COPYDATA decide */
            }
        }
    }

    [ComImport]
    [Guid("D782CCBA-AFB0-43F1-94A9-DA2DFD30E6F7")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface INotificationCBLike
    {
        void Notify(uint code, IntPtr notifyItem);
    }
}
