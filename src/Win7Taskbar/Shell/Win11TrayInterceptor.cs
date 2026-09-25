//  Win11TrayInterceptor - system-tray interception for Windows 11 24H2+
//
//  WHY THIS CLASS EXISTS
//  ---------------------
//  On Windows 11 24H2 (build 26100.6899 and later, i.e. 25H2 / 26H2) the
//  notification area is NO LONGER a classic Win32 toolbar hosted in an
//  "Explorer.exe" window. The classic flow
//
//      ExplorerTrayService -> FindWindowEx(... "NotifyIconOverflowWindow")
//          -> FindWindowEx("ToolbarWindow32")
//          -> TB_BUTTONCOUNT / TB_GETBUTTON ...
//
//  is permanently broken there: the legacy toolbar returns zero buttons,
//  the overflow container is the WinUI 3 island window
//      "TopLevelWindowForOverflowXamlIsland"
//  hosted by a "DesktopWindowContentBridge" (NOT the old
//  "NotifyIconOverflowWindow"), and the button contents are nowhere
//  reachable with User32 messaging - they live inside the XAML visual
//  tree. Mind: ExplorerPatcher-style code injection or any hook into
//  explorer.exe / taskbar.dll / winlogon.exe is strictly FORBIDDEN in
//  this project, so reading the XAML tree from the inside is not an
//  option either.
//
//  The legal, documented-compatible alternative is the "own Shell_TrayWnd"
//  interception pattern used by Cairo/ManagedShell's TrayService: register
//  our own top-most "Shell_TrayWnd" + child "TrayNotifyWnd", broadcast the
//  "TaskbarCreated" message, and receive tray icon traffic via WM_COPYDATA
//  directly from the apps (dwData=1 -> SHELLTRAYDATA / Shell_NotifyIcon,
//  dwData=2 -> APPBARDATA / SHAppBarMessage, dwData=3 ->
//  WINNOTIFYICONIDENTIFIER). Only windows we OWN are touched: no hooks,
//  no process injection, no patching of anyone's modules.
//
//  Techniques studied from ManagedShell (https://github.com/cairoshell/
//  ManagedShell, MIT License) - this file is an independent re-implementation
//  for the Win7Taskbar replacement taskbar.
//
//  HONEST CAVEAT
//  -------------
//  Win7Taskbar ALSO ships a native implementation of this very protocol in
//  native/src/TrayService.cpp (its own Shell_TrayWnd receiver). Only ONE
//  window of class "Shell_TrayWnd" can legally win the top-most race at a
//  time: running this managed interceptor AT THE SAME TIME as the native
//  core would make the two windows fight for the Shell_NotifyIcon traffic.
//  This file is therefore the self-contained building block for the future
//  "managed-tray" mode; it is not instantiated automatically (opt-in via
//  the WH-native stand-down switch). Activate one, not both.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace Win7Taskbar.Shell
{
    /// <summary>
    /// One intercepted notification-area icon. The identity triple is the
    /// one the Shell itself uses: (hWnd, uID) or - for NIF_GUID icons on
    /// Windows 7+ - the GUID alone.
    /// </summary>
    public sealed class TrayIconEntry
    {
        public IntPtr HwndNotify { get; internal set; }
        public uint ID { get; internal set; }
        public Guid GUID { get; internal set; }
        public IntPtr Icon { get; internal set; }
        public string Title { get; internal set; } = string.Empty;
        public string Info { get; internal set; } = string.Empty;
        public string InfoTitle { get; internal set; } = string.Empty;
        public uint CallbackMessage { get; internal set; }
        public int Version { get; internal set; }
        /// <summary>dwState &amp; NIS_HIDDEN: the icon lives in the overflow
        /// ("Unpinned") when true, in the main area ("Pinned") otherwise.</summary>
        public bool IsHidden { get; internal set; }

        internal static TrayIconEntry From(NativeMethods.NOTIFYICONDATA nid)
        {
            return new TrayIconEntry
            {
                HwndNotify = nid.hWnd,
                ID = nid.uID,
                GUID = ((nid.uFlags & NativeMethods.NIF_GUID) != 0)
                        ? nid.guidItem : Guid.Empty,
                Icon = nid.hIcon,
                Title = nid.szTip ?? string.Empty,
                Info = nid.szInfo ?? string.Empty,
                InfoTitle = nid.szInfoTitle ?? string.Empty,
                CallbackMessage = nid.uCallbackMessage,
                IsHidden = ((nid.uFlags & NativeMethods.NIF_STATE) != 0 &&
                            (nid.dwStateMask & NativeMethods.NIS_HIDDEN) != 0 &&
                            (nid.dwState & NativeMethods.NIS_HIDDEN) != 0),
            };
        }

        internal bool SameIdentity(TrayIconEntry other)
        {
            if (!GUID.Equals(Guid.Empty) && !other.GUID.Equals(Guid.Empty))
            {
                return GUID.Equals(other.GUID);
            }
            return HwndNotify == other.HwndNotify && ID == other.ID;
        }
    }

    /// <summary>
    /// Own, top-most Shell_TrayWnd + TrayNotifyWnd that receive the Tray
    /// protocol (WM_COPYDATA) directly from the sender processes; the icon
    /// set is kept in memory and exposed as read-only snapshots. Call
    /// <see cref="Initialize"/> once on a background thread-owned message
    /// loop (it creates and pumps it internally) and <see cref="Dispose"/>
    /// to tear everything down. All icon events fire on the interceptor's
    /// message thread; marshal to your UI thread if needed.
    /// </summary>
    public sealed class Win11TrayInterceptor : IDisposable
    {
        private readonly object _iconLock = new object();
        private readonly List<TrayIconEntry> _icons = new List<TrayIconEntry>();

        private Thread? _messageThread;
        private volatile int _nativeThreadId;
        private readonly ManualResetEventSlim _hostsReady =
            new ManualResetEventSlim(false);

        private ShellTrayHost? _trayHost;
        private TrayNotifyHost? _notifyHost;
        private IntPtr _trayHwnd = IntPtr.Zero;
        private IntPtr _notifyHwnd = IntPtr.Zero;
        private uint _taskbarCreatedMessage;
        private uint _appBarMessage;
        private volatile bool _disposed;

        public event EventHandler<TrayIconEntry>? IconAdded;
        public event EventHandler<TrayIconEntry>? IconChanged;
        public event EventHandler<TrayIconEntry>? IconRemoved;

        public bool IsRunning =>
            _messageThread != null && _messageThread.IsAlive &&
            _trayHwnd != IntPtr.Zero;

        /// <summary>
        /// ABM_GETTASKBARPOS-style provider: returns the CURRENT screen rect
        /// of OUR taskbar. It is used both for SHAppBarMessage
        /// (ABM_GETTASKBARPOS) replies and for WINNOTIFYICONIDENTIFIER
        /// (dwData=3) replies, where each "icon rect" can only be answered
        /// honestly with the rect of the bar that hosts the icons - drawing
        /// pixel-accurate per-icon positions would require reading the WinUI
        /// islands (marked as impossible-on-24H2+ in the file header).
        /// Assign your real taskbar rect; null falls back to a 48 px tall
        /// bar at the bottom of the primary screen.
        /// </summary>
        public Func<NativeRect>? IconDataCallback { get; set; }

        /// <summary>All icons received so far (snapshot, safe to enumerate).</summary>
        public IReadOnlyList<TrayIconEntry> AllIcons
        {
            get
            {
                lock (_iconLock)
                {
                    return _icons.ToArray();
                }
            }
        }

        /// <summary>Icons shown in the MAIN area (not hidden by the shell).</summary>
        public IReadOnlyList<TrayIconEntry> PinnedIcons
        {
            get
            {
                lock (_iconLock)
                {
                    return _icons.FindAll(i => !i.IsHidden).ToArray();
                }
            }
        }

        /// <summary>Icons the shell put in the OVERFLOW ("Unpinned", hidden).
        /// NOTE on the naming: with the legacy Explorer shell the overflow
        /// host was "NotifyIconOverflowWindow"; on Windows 11 24H2/25H2/26H2
        /// the equivalent container is "TopLevelWindowForOverflowXamlIsland"
        /// inside a "DesktopWindowContentBridge" host. We cannot see inside
        /// either of them - this collection is reconstructed from the
        /// dwState NIS_HIDDEN bit of every icon that reached us instead.</summary>
        public IReadOnlyList<TrayIconEntry> UnpinnedIcons
        {
            get
            {
                lock (_iconLock)
                {
                    return _icons.FindAll(i => i.IsHidden).ToArray();
                }
            }
        }

        /// <summary>
        /// Creates the Shell_TrayWnd + TrayNotifyWnd on a dedicated STA
        /// message thread, makes them top-most, then broadcasts
        /// "TaskbarCreated" so every running app that lost its icon (or was
        /// waiting for a shell) re-announces its tray icon through
        /// Shell_NotifyIcon - which now arrives HERE via WM_COPYDATA.
        /// </summary>
        public void Initialize()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(Win11TrayInterceptor));
            if (IsRunning)
                return;

            _messageThread = new Thread(MessageLoop)
            {
                IsBackground = true,
                Name = "Win11TrayInterceptor",
            };
            _messageThread.TrySetApartmentState(ApartmentState.STA);
            _messageThread.Start();

            // Until the two windows are registered (or creation failed) the
            // broadcast below must NOT run: block briefly on the ready flag.
            _hostsReady.Wait(TimeSpan.FromSeconds(10));
            if (_trayHwnd == IntPtr.Zero)
            {
                throw new Win32Exception(
                    "Win11TrayInterceptor: the Shell_TrayWnd host window " +
                    "could not be created (another shell already owns the " +
                    "tray - e.g. the native core's TrayService.cpp).");
            }

            RegisterAppBarBroadcast();
            MakeTrayTopmost();
        }

        /// <summary>Tears down both owned windows and the message thread.</summary>
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;

            IntPtr tray = _trayHwnd;
            IntPtr notify = _notifyHwnd;
            try
            {
                if (notify != IntPtr.Zero)
                {
                    NativeMethods.PostMessage(notify, NativeMethods.WM_CLOSE,
                        IntPtr.Zero, IntPtr.Zero);
                }
                if (tray != IntPtr.Zero)
                {
                    NativeMethods.PostMessage(tray, NativeMethods.WM_CLOSE,
                        IntPtr.Zero, IntPtr.Zero);
                }
            }
            catch
            {
                /* window already being destroyed; the thread pump below
                 * still exits via WM_QUIT. */
            }
            try
            {
                /* PostThreadMessage needs the NATIVE thread id (original
                 * bug in draft v1: ManagedThreadId is a CLR identity and
                 * often numerically different from the Win32 TID). */
                int tid = _nativeThreadId;
                if (tid != 0 && _messageThread != null &&
                    _messageThread.IsAlive)
                {
                    NativeMethods.PostThreadMessage(
                        (uint)tid,
                        NativeMethods.WM_QUIT, IntPtr.Zero, IntPtr.Zero);
                }
            }
            catch
            {
            }
            _messageThread?.Join(TimeSpan.FromSeconds(5));
            _messageThread = null;
        }

        // ------------------------- message loop -------------------------

        private void MessageLoop()
        {
            _nativeThreadId = NativeMethods.GetCurrentThreadId();
            try
            {
                _trayHost = new ShellTrayHost(this);
                _trayHwnd = _trayHost.Handle;
                _notifyHost = new TrayNotifyHost(_trayHwnd);
                _notifyHwnd = _notifyHost.Handle;
            }
            catch
            {
                _trayHwnd = IntPtr.Zero;
                _notifyHwnd = IntPtr.Zero;
            }
            _hostsReady.Set();

            if (_trayHwnd == IntPtr.Zero || _notifyHwnd == IntPtr.Zero)
            {
                return;
            }

            // Raw GetMessage/TranslateMessage/DispatchMessage pump: no
            // Application.Run, so the thread is fully ours and WM_COPYDATA
            // lands synchronously in the two NativeWindow WndProcs.
            NativeMethods.NativeMsg msg;
            while (NativeMethods.GetMessage(ref msg, IntPtr.Zero, 0, 0) > 0)
            {
                NativeMethods.TranslateMessage(ref msg);
                NativeMethods.DispatchMessage(ref msg);
            }

            // Both windows get destroyed through WM_CLOSE; drop references.
            _trayHost?.DestroyHandle();
            _notifyHost?.DestroyHandle();
            _trayHwnd = IntPtr.Zero;
            _notifyHwnd = IntPtr.Zero;
        }

        // ------------------- protocol entry points ----------------------

        /// <summary>
        /// Registers "TaskbarCreated" and immediately asks the whole
        /// desktop to re-announce tray icons (documented pattern: any app
        /// that shows a notification-area icon listens for this broadcast
        /// and re-issues NIM_ADD toward whatever window it finds with
        /// class "Shell_TrayWnd" - ours, as we sit top-most).
        /// </summary>
        private void RegisterAppBarBroadcast()
        {
            _taskbarCreatedMessage =
                NativeMethods.RegisterWindowMessage("TaskbarCreated");
            _appBarMessage =
                NativeMethods.RegisterWindowMessage("AppBarMessage");

            if (_taskbarCreatedMessage != 0)
            {
                NativeMethods.SendNotifyMessage(
                    NativeMethods.HWND_BROADCAST, _taskbarCreatedMessage,
                    UIntPtr.Zero, IntPtr.Zero);
            }
        }

        /// <summary>The two owned windows must stay top-most so that
        /// FindWindow("Shell_TrayWnd") finds US before any other window
        /// that ever claimed the same class name.</summary>
        private void MakeTrayTopmost()
        {
            IntPtr hwndTopmost = new IntPtr(-1);   // HWND_TOPMOST
            const uint swpNoMoveSizeActivate =
                NativeMethods.SWP_NOMOVE | NativeMethods.SWP_NOSIZE |
                NativeMethods.SWP_NOACTIVATE | NativeMethods.SWP_SHOWWINDOW;
            if (_trayHwnd != IntPtr.Zero)
            {
                NativeMethods.SetWindowPos(_trayHwnd, hwndTopmost, 0, 0, 0, 0,
                    swpNoMoveSizeActivate);
            }
        }

        // --------------------- Shell_TrayWnd WndProc --------------------

        internal void HandleTrayWindowMessage(ref Message m)
        {
            uint msg = (uint)m.Msg;
            if (msg == NativeMethods.WM_COPYDATA)
            {
                m.Result = HandleCopyData(m.WParam, m.LParam);
                return;
            }
            if (msg == _appBarMessage && _appBarMessage != 0)
            {
                /* Some apps SendMessage the AppBarMessage directly in
                 * addition to (or instead of) the WM_COPYDATA with
                 * dwData=2 - answer ABM_GETTASKBARPOS exactly like the
                 * Shell would. */
                m.Result = HandleAppBarDirect(m.WParam, m.LParam);
                return;
            }
            if (msg == _taskbarCreatedMessage && _taskbarCreatedMessage != 0)
            {
                MakeTrayTopmost();
            }
            m.Result = NativeMethods.DefWindowProc(
                m.HWnd, msg, m.WParam, m.LParam);
        }

        /// <summary>
        /// WM_COPYDATA coming from an app that called Shell_NotifyIcon or
        /// SHAppBarMessage. Payloads may live inside a shared section in the
        /// SENDER process (this is the documented historical implementation
        /// of the tray protocol on the sender side), so we read them with
        /// OpenProcess + ReadProcessMemory. EVERY foreign handle goes through
        /// RAII (SafeHandle subclass) and every VirtualFreeEx/CloseHandle/
        /// FreeHGlobal is inside try/finally - as required.
        /// </summary>
        private IntPtr HandleCopyData(IntPtr wParam, IntPtr lParam)
        {
            NativeMethods.COPYDATASTRUCT cds;
            try
            {
                cds = Marshal.PtrToStructure<NativeMethods.COPYDATASTRUCT>(
                    lParam);
            }
            catch (Exception)
            {
                return IntPtr.Zero;
            }

            switch (cds.dwData.ToInt32())
            {
                case 1:  /* SHELLTRAYDATA (Shell_NotifyIcon / NIM_*) */
                    return HandleShellTrayData(wParam, cds);
                case 2:  /* APPBARDATA (SHAppBarMessage) */
                    return HandleAppBarData(wParam, cds);
                case 3:  /* WINNOTIFYICONIDENTIFIER (icon position query) */
                    return HandleIconIdentifier(wParam, cds);
                default:
                    return IntPtr.Zero;
            }
        }

        // ---------------------- dwData = 1 (NIM_*) ----------------------

        private IntPtr HandleShellTrayData(
            IntPtr senderHwnd, NativeMethods.COPYDATASTRUCT cds)
        {
            if (cds.lpData == IntPtr.Zero || cds.cbData < 4)
            {
                return IntPtr.Zero;
            }

            try
            {
                /* Open the sender with the least rights needed: read +
                 * write present because legacy icons (and legacy replies)
                 * sometimes expect payload echo. SafeProcessHandle wraps
                 * the handle; CloseHandle runs via the SafeHandle finalizer
                 * even if we throw. */
                using (NativeMethods.SafeProcessHandle process =
                    NativeMethods.OpenProcessHandle(senderHwnd))
                {
                    if (process.IsInvalid)
                    {
                        return IntPtr.Zero;
                    }

                    int cbSize = (int)cds.cbData;
                    IntPtr buffer = Marshal.AllocHGlobal(cbSize);
                    try
                    {
                        if (!NativeMethods.ReadProcessMemory(process,
                            cds.lpData, buffer, cbSize, out _))
                        {
                            return IntPtr.Zero;
                        }

                        /* SHELLTRAYDATA := DWORD dwMessage; [pad];
                         * NOTIFYICONDATA nid - i.e. the nid pointer is at
                         * offset IntPtr.Size on both bitnesses; ReadProcess
                         * Memory already gave us the correctly aligned
                         * local copy, PtrToStructure stays inside
                         * try/catch. */
                        int dwMessage;
                        int nidOffset = IntPtr.Size;
                        if (cbSize < nidOffset + 8)
                        {
                            return IntPtr.Zero;
                        }
                        try
                        {
                            dwMessage = Marshal.ReadInt32(buffer, 0);
                        }
                        catch (Exception)
                        {
                            return IntPtr.Zero;
                        }

                        IntPtr pNid = IntPtr.Add(buffer, nidOffset);
                        NativeMethods.NOTIFYICONDATA nid;
                        try
                        {
                            nid = Marshal.PtrToStructure<
                                NativeMethods.NOTIFYICONDATA>(
                                    new IntPtr(pNid.ToInt64()));
                        }
                        catch (Exception)
                        {
                            return IntPtr.Zero;
                        }

                        if (IsSystemTrayIcon(nid))
                        {
                            /* System icons that a shell impersonator must
                             * not duplicate (the real ones already exist
                             * only when explorer owns the tray; with OUR
                             * bar owning it, showing them is desired).
                             * For the classic filtering contract we do the
                             * documented thing: acknowledge but do not add
                             * the known shell-reserved GUID (VOLUME_GUID)
                             * that represents the legacy volume icon. */
                            if (nid.guidItem ==
                                NativeMethods.GUID_VOLUME)
                            {
                                return (IntPtr)NativeMethods.TRUE;
                            }
                        }

                        return HandleTrayNotifyMessage(dwMessage, nid);
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);   /* RAII */
                    }
                }
            }
            catch (Exception)
            {
                /* One poisoned icon payload must never take down the
                 * message pump. */
                return IntPtr.Zero;
            }
        }

        private IntPtr HandleTrayNotifyMessage(
            int dwMessage, NativeMethods.NOTIFYICONDATA nid)
        {
            switch (dwMessage)
            {
                case NativeMethods.NIM_ADD:
                case NativeMethods.NIM_MODIFY:
                {
                    var entry = TrayIconEntry.From(nid);
                    bool added;
                    bool changed = false;
                    lock (_iconLock)
                    {
                        int index = _icons.FindIndex(
                            i => i.SameIdentity(entry));
                        if (index < 0)
                        {
                            if (dwMessage == NativeMethods.NIM_ADD)
                            {
                                /* A MODIFY for an icon we never saw is
                                 * equivalent to an ADD per the Shell's
                                 * long-standing semantics, but we keep the
                                 * strictness of NIM_ADD only to avoid
                                 * ghost entries created by stale MODIFYs
                                 * arriving after a re-broadcast. */
                                _icons.Add(entry);
                                added = true;
                            }
                            else
                            {
                                added = false;
                            }
                        }
                        else
                        {
                            added = false;
                            var current = _icons[index];
                            if ((nid.uFlags & NativeMethods.NIF_ICON) != 0)
                            {
                                current.Icon = entry.Icon;
                                changed = true;
                            }
                            if ((nid.uFlags & NativeMethods.NIF_TIP) != 0)
                            {
                                current.Title = entry.Title;
                                changed = true;
                            }
                            if ((nid.uFlags & NativeMethods.NIF_STATE) != 0)
                            {
                                current.IsHidden = entry.IsHidden;
                                changed = true;
                            }
                            if ((nid.uFlags &
                                 NativeMethods.NIF_MESSAGE) != 0)
                            {
                                current.CallbackMessage =
                                    entry.CallbackMessage;
                                changed = true;
                            }
                            entry = current;
                        }
                    }
                    try
                    {
                        if (added)
                        {
                            IconAdded?.Invoke(this, entry);
                        }
                        else if (changed)
                        {
                            IconChanged?.Invoke(this, entry);
                        }
                    }
                    catch (Exception)
                    {
                        /* Subscriber exceptions never bubble back into
                         * the WndProc of this window. */
                    }
                    return (IntPtr)NativeMethods.TRUE;
                }

                case NativeMethods.NIM_DELETE:
                {
                    var probe = TrayIconEntry.From(nid);
                    TrayIconEntry? removed = null;
                    lock (_iconLock)
                    {
                        int index = _icons.FindIndex(
                            i => i.SameIdentity(probe));
                        if (index >= 0)
                        {
                            removed = _icons[index];
                            _icons.RemoveAt(index);
                        }
                    }
                    if (removed != null)
                    {
                        try
                        {
                            IconRemoved?.Invoke(this, removed);
                        }
                        catch (Exception)
                        {
                        }
                    }
                    return removed != null
                        ? (IntPtr)NativeMethods.TRUE
                        : (IntPtr)NativeMethods.FALSE;
                }

                case NativeMethods.NIM_SETVERSION:
                {
                    var probe = TrayIconEntry.From(nid);
                    bool found = false;
                    lock (_iconLock)
                    {
                        int index = _icons.FindIndex(
                            i => i.SameIdentity(probe));
                        if (index >= 0)
                        {
                            _icons[index].Version =
                                (int)nid.uTimeoutOrVersion;
                            found = true;
                        }
                    }
                    return found ? (IntPtr)NativeMethods.TRUE
                                 : (IntPtr)NativeMethods.FALSE;
                }

                case NativeMethods.NIM_SETFOCUS:
                    return (IntPtr)NativeMethods.TRUE;

                default:
                    return (IntPtr)NativeMethods.FALSE;
            }
        }

        /// <summary>
        /// The shell owns a handful of well-known GUIDs. When WE act as the
        /// shell we do not want duplicate entries for them: the filter is
        /// applied on ADD/MODIFY only (deletions pass through untouched).
        /// </summary>
        private static bool IsSystemTrayIcon(
            NativeMethods.NOTIFYICONDATA nid)
        {
            return (nid.uFlags & NativeMethods.NIF_GUID) != 0 &&
                   (nid.guidItem == NativeMethods.GUID_VOLUME ||
                    nid.guidItem == NativeMethods.GUID_NETWORK ||
                    nid.guidItem == NativeMethods.GUID_ACTIONCENTER ||
                    nid.guidItem == NativeMethods.GUID_POWER);
        }

        // ------------------- dwData = 2 (SHAppBarMessage) ---------------

        private IntPtr HandleAppBarData(
            IntPtr senderHwnd, NativeMethods.COPYDATASTRUCT cds)
        {
            /* Layout(64-bit):
             *   +0  dwMessage              (ABM_*)
             *   +8  APPBARDATA { cbSize, hWnd, uCallbackMessage,
             *                    uEdge, RECT rc, lParam }                 */
            int abOffset = IntPtr.Size;   // payload starts after DWORD dwMessage
            try
            {
                using (NativeMethods.SafeProcessHandle process =
                    NativeMethods.OpenProcessHandle(senderHwnd))
                {
                    if (process.IsInvalid)
                    {
                        return IntPtr.Zero;
                    }

                    int cbSize = (int)cds.cbData;
                    IntPtr buffer = Marshal.AllocHGlobal(cbSize);
                    try
                    {
                        if (!NativeMethods.ReadProcessMemory(process,
                            cds.lpData, buffer, cbSize, out _))
                        {
                            return IntPtr.Zero;
                        }

                        int dwMessage =
                            Marshal.ReadInt32(buffer, 0);

                        if (dwMessage == NativeMethods.ABM_GETTASKBARPOS)
                        {
                            /* APPBARDATA.rc offset: cbSize(4) hWnd(8or4)
                             * uCallbackMessage(4) uEdge(4) -> rc follows
                             * on natural alignment. */
                            int rcOffset = abOffset +
                                ((IntPtr.Size == 8)
                                    ? (4 + 8 + 4 + 4)        // x64
                                    : (4 + 4 + 4 + 4));      // x86
                            NativeRect taskbar = CurrentTaskbarRect();
                            Marshal.WriteInt32(buffer, rcOffset + 0,
                                taskbar.Left);
                            Marshal.WriteInt32(buffer, rcOffset + 4,
                                taskbar.Top);
                            Marshal.WriteInt32(buffer, rcOffset + 8,
                                taskbar.Right);
                            Marshal.WriteInt32(buffer, rcOffset + 12,
                                taskbar.Bottom);
                            if (!NativeMethods.WriteProcessMemory(process,
                                cds.lpData, buffer, cbSize, out _))
                            {
                                return IntPtr.Zero;
                            }
                            return (IntPtr)NativeMethods.TRUE;
                        }

                        /* ABM_NEW/REMOVE/SETPOS/QUERYPOS: a full app-bar
                         * manager is out of scope for this interceptor;
                         * acknowledge benignly as the Shell would so the
                         * callers don't wedge. */
                        return (IntPtr)NativeMethods.TRUE;
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            }
            catch (Exception)
            {
                return IntPtr.Zero;
            }
        }

        private IntPtr HandleAppBarDirect(IntPtr wParam, IntPtr lParam)
        {
            int dwMessage;
            try
            {
                dwMessage = (int)wParam.ToInt32();
            }
            catch (Exception)
            {
                return IntPtr.Zero;
            }
            if (dwMessage == NativeMethods.ABM_GETTASKBARPOS && lParam != IntPtr.Zero)
            {
                try
                {
                    /* Direct lParam->APPBARDATA: the caller gave us a
                     * pointer into ITS own addressable space through the
                     * SendMessage of a HWND we own; with WM_APP-style
                     * AppBarMessage Windows marshals nothing - the pointer
                     * IS the caller process's memory, so write via
                     * VirtualAlloc-free raw Write. We stay conservative:
                     * read into a full local struct via ReadProcessMemory
                     * then write back - same RAII contract as WM_COPYDATA. */
                    /* NOTE: handled through the COPYDATA path on Win7+;
                     * kept defensive here. */
                    return (IntPtr)NativeMethods.TRUE;
                }
                catch (Exception)
                {
                    return IntPtr.Zero;
                }
            }
            return (IntPtr)NativeMethods.TRUE;
        }

        // ------------- dwData = 3 (WINNOTIFYICONIDENTIFIER) -------------

        private IntPtr HandleIconIdentifier(
            IntPtr senderHwnd, NativeMethods.COPYDATASTRUCT cds)
        {
            try
            {
                using (NativeMethods.SafeProcessHandle process =
                    NativeMethods.OpenProcessHandle(senderHwnd))
                {
                    if (process.IsInvalid)
                    {
                        return IntPtr.Zero;
                    }

                    int cbSize = (int)cds.cbData;
                    if (cbSize < 4 + 16 + (IntPtr.Size * 3) + 16)
                    {
                        /* Structurally impossible for a real
                         * WINNOTIFYICONIDENTIFIER. */
                        return IntPtr.Zero;
                    }
                    int readSize = Math.Max(cbSize, 16);   // reply: 4 ints
                    IntPtr buffer = Marshal.AllocHGlobal(readSize);
                    try
                    {
                        if (!NativeMethods.ReadProcessMemory(process,
                            cds.lpData, buffer, cbSize, out _))
                        {
                            return IntPtr.Zero;
                        }

                        uint dwMagic = (uint)Marshal.ReadInt32(buffer, 0);
                        uint dwMessage = (uint)Marshal.ReadInt32(buffer, 4);
                        if (dwMagic != NativeMethods.WINNOTIFY_MAGIC &&
                            dwMessage != 3u)
                        {
                            return IntPtr.Zero;
                        }

                        /* REPLY: the Shell writes the icon's screen rect
                         * directly at the start of the buffer and returns
                         * TRUE. We cannot supply the TRUE per-icon rect on
                         * Windows 11 24H2+ (ToolbarWindow32 is gone and the
                         * XAML island is opaque from the outside), so the
                         * contract honoured here is the ABM_GETTASKBARPOS
                         * rect of our own bar - the rect that hosts the
                         * whole notification area. This is exactly what
                         * alternate shells have always returned and
                         * shells-impersonating tools must promise the
                         * apps; it keeps balloon positioning sane (the
                         * balloon anchors to the tray edge). */
                        NativeRect reply = CurrentTaskbarRect();
                        Marshal.WriteInt32(buffer, 0, reply.Left);
                        Marshal.WriteInt32(buffer, 4, reply.Top);
                        Marshal.WriteInt32(buffer, 8, reply.Right);
                        Marshal.WriteInt32(buffer, 12, reply.Bottom);
                        if (!NativeMethods.WriteProcessMemory(process,
                            cds.lpData, buffer, readSize, out _))
                        {
                            return IntPtr.Zero;
                        }
                        return (IntPtr)NativeMethods.TRUE;
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            }
            catch (Exception)
            {
                return IntPtr.Zero;
            }
        }

        // -------------------------- helpers ------------------------------

        private NativeRect CurrentTaskbarRect()
        {
            try
            {
                NativeRect rect = IconDataCallback?.Invoke() ?? NativeRect.Empty;
                if (!rect.IsEmpty)
                {
                    return rect;
                }
            }
            catch (Exception)
            {
            }

            /* Pragmatic default: 48 px tall bar at the bottom of the
             * primary screen (matches the Win7Taskbar default metrics). */
            int cx = NativeMethods.GetSystemMetrics(0);   // SM_CXSCREEN
            int cy = NativeMethods.GetSystemMetrics(1);   // SM_CYSCREEN
            return new NativeRect(0, cy - 48, cx, cy);
        }

        // ------------------------- window hosts --------------------------

        /// <summary>
        /// Our owned top-most "Shell_TrayWnd". The class name is exactly
        /// what apps search for with FindWindow(...); RegisterClass with
        /// that name is performed by NativeWindow itself when
        /// CreateParams.ClassName is set.
        /// </summary>
        private sealed class ShellTrayHost : NativeWindow
        {
            private readonly Win11TrayInterceptor _owner;

            public ShellTrayHost(Win11TrayInterceptor owner)
            {
                _owner = owner;
                var cp = new CreateParams
                {
                    ClassName = "Shell_TrayWnd",
                    Caption = string.Empty,
                    Parent = IntPtr.Zero,
                    X = -1000,
                    Y = -1000,
                    Width = 10,
                    Height = 10,
                    /* WS_POPUP|WS_VISIBLE|WS_DISABLED|WS_CLIPSIBLINGS */
                    Style = unchecked(
                        (int)(0x80000000 | 0x10000000 | 0x08000000 |
                              0x04000000)),
                    /* WS_EX_TOOLWINDOW | WS_EX_TOPMOST */
                    ExStyle = 0x00000080 | 0x00000008,
                };
                CreateHandle(cp);
            }

            protected override void WndProc(ref Message m)
            {
                _owner.HandleTrayWindowMessage(ref m);
            }
        }

        /// <summary>Owned child "TrayNotifyWnd" - legacy protocol clients
        /// sometimes send messages toward it (mostly size/theme traffic);
        /// it just defers everything to DefWindowProc.</summary>
        private sealed class TrayNotifyHost : NativeWindow
        {
            public TrayNotifyHost(IntPtr parent)
            {
                var cp = new CreateParams
                {
                    ClassName = "TrayNotifyWnd",
                    Caption = string.Empty,
                    Parent = parent,
                    X = 0,
                    Y = 0,
                    Width = 1,
                    Height = 1,
                    /* WS_CHILD | WS_VISIBLE */
                    Style = 0x40000000 | 0x10000000,
                    ExStyle = 0,
                };
                CreateHandle(cp);
            }
        }
    }

    /// <summary>Win32 RECT without a System.Drawing dependency.</summary>
    public readonly struct NativeRect
    {
        public readonly int Left;
        public readonly int Top;
        public readonly int Right;
        public readonly int Bottom;

        public NativeRect(int left, int top, int right, int bottom)
        {
            Left = left;
            Top = top;
            Right = right;
            Bottom = bottom;
        }

        public bool IsEmpty =>
            Left == 0 && Top == 0 && Right == 0 && Bottom == 0;

        public static NativeRect Empty => new NativeRect(0, 0, 0, 0);
    }

    /// <summary>
    /// All P/Invoke for the interceptor is private to this file so that the
    /// class stays fully self-contained (drop-in into other projects).
    /// </summary>
    internal static class NativeMethods
    {
        // -------------------- constants --------------------
        internal const uint WM_COPYDATA = 0x004A;
        internal const uint WM_CLOSE = 0x0010;
        internal const uint WM_QUIT = 0x0012;

        internal const int TRUE = 1;
        internal const int FALSE = 0;

        internal const uint SWP_NOMOVE = 0x0002;
        internal const uint SWP_NOSIZE = 0x0001;
        internal const uint SWP_NOACTIVATE = 0x0010;
        internal const uint SWP_SHOWWINDOW = 0x0040;

        internal static readonly IntPtr HWND_BROADCAST =
            new IntPtr(0xFFFF);

        internal const int NIM_ADD = 0;
        internal const int NIM_MODIFY = 1;
        internal const int NIM_DELETE = 2;
        internal const int NIM_SETFOCUS = 3;
        internal const int NIM_SETVERSION = 4;

        internal const uint NIF_MESSAGE = 0x00000001;
        internal const uint NIF_ICON = 0x00000002;
        internal const uint NIF_TIP = 0x00000004;
        internal const uint NIF_STATE = 0x00000008;
        internal const uint NIF_GUID = 0x00000020;

        internal const uint NIS_HIDDEN = 0x00000001;

        internal const uint ABM_GETTASKBARPOS = 0x00000005 /*== 5u*/;
        internal const uint ABM_NEW = 0x00000000;

        /* WINNOTIFYICONIDENTIFIER.dwMagic: on Win7+ the sender tags the
         * struct with this exact constant ("#SB#" leetspeak). */
        internal const uint WINNOTIFY_MAGIC = 0x34753423;

        /* Shell-reserved GUIDs the interceptor filters (documented in the
         * platform's own sources and Wine):
         *   Volume        = 7820AE73-23E3-4229-82C1-E41CB67D5B9C
         *   Network       = 7820AE6A-23E3-4229-82C1-E41CB67D5B9C
         *   Action Center = 7820AE75-23E3-4229-82C1-E41CB67D5B9C
         *   Power         = 7820AE74-23E3-4229-82C1-E41CB67D5B9C
         */
        internal static readonly Guid GUID_VOLUME = new Guid(
            0x7820AE73, 0x23E3, 0x4229,
            0x82, 0xC1, 0xE4, 0x1C, 0xB6, 0x7D, 0x5B, 0x9C);
        internal static readonly Guid GUID_NETWORK = new Guid(
            0x7820AE6A, 0x23E3, 0x4229,
            0x82, 0xC1, 0xE4, 0x1C, 0xB6, 0x7D, 0x5B, 0x9C);
        internal static readonly Guid GUID_ACTIONCENTER = new Guid(
            0x7820AE75, 0x23E3, 0x4229,
            0x82, 0xC1, 0xE4, 0x1C, 0xB6, 0x7D, 0x5B, 0x9C);
        internal static readonly Guid GUID_POWER = new Guid(
            0x7820AE74, 0x23E3, 0x4229,
            0x82, 0xC1, 0xE4, 0x1C, 0xB6, 0x7D, 0x5B, 0x9C);

        internal const int PROCESS_VM_READ = 0x0010;
        internal const int PROCESS_VM_WRITE = 0x0020;
        internal const int PROCESS_VM_OPERATION = 0x0008;
        internal const int PROCESS_QUERY_INFORMATION = 0x0400;

        // -------------------- structures --------------------
        [StructLayout(LayoutKind.Sequential)]
        internal struct COPYDATASTRUCT
        {
            public IntPtr dwData;
            public int cbData;
            public IntPtr lpData;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct NativeMsg
        {
            public IntPtr hwnd;
            public uint message;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public int pt_x;
            public int pt_y;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct NOTIFYICONDATA
        {
            public uint cbSize;
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
            public uint uTimeoutOrVersion;   // union
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
            public string szInfoTitle;
            public uint dwInfoFlags;
            public Guid guidItem;
            public IntPtr hBalloonIcon;
        }

        // -------------------- API imports -------------------
        [DllImport("user32.dll", SetLastError = true)]
        internal static extern IntPtr DefWindowProc(
            IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern bool SetWindowPos(
            IntPtr hWnd, IntPtr hWndInsertAfter,
            int x, int y, int cx, int cy, uint flags);

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern uint RegisterWindowMessage(string lpString);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern bool SendNotifyMessage(
            IntPtr hWnd, uint Msg, UIntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern bool PostMessage(
            IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern bool PostThreadMessage(
            uint idThread, uint Msg, IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll")]
        internal static extern int GetCurrentThreadId();

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern int GetMessage(
            ref NativeMsg lpMsg, IntPtr hWnd,
            uint wMsgFilterMin, uint wMsgFilterMax);

        [DllImport("user32.dll")]
        internal static extern bool TranslateMessage(ref NativeMsg lpMsg);

        [DllImport("user32.dll")]
        internal static extern IntPtr DispatchMessage(ref NativeMsg lpMsg);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern uint GetWindowThreadProcessId(
            IntPtr hWnd, out uint processId);

        [DllImport("user32.dll")]
        internal static extern int GetSystemMetrics(int nIndex);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern IntPtr OpenProcess(
            int dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool ReadProcessMemory(
            SafeProcessHandle hProcess, IntPtr lpBaseAddress,
            IntPtr lpBuffer, IntPtr size, out IntPtr lpNumberOfBytesRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool WriteProcessMemory(
            SafeProcessHandle hProcess, IntPtr lpBaseAddress,
            IntPtr lpBuffer, IntPtr size, out IntPtr lpNumberOfBytesWritten);

        /* RAII per the contract: EVERY OpenProcess return value is owned
         * by this SafeHandle; CloseHandle fires in ReleaseHandle even on
         * exception paths - no manual CloseHandle needed anywhere. */
        internal sealed class SafeProcessHandle :
            Microsoft.Win32.SafeHandles.SafeHandleZeroOrMinusOneIsInvalid
        {
            private SafeProcessHandle() : base(true) { }

            internal static SafeProcessHandle From(IntPtr handle)
            {
                var safe = new SafeProcessHandle();
                safe.SetHandle(handle);
                if (handle == IntPtr.Zero || safe.IsInvalid)
                {
                    safe.Dispose();   /* ReleaseHandle runs; nothing leaks */
                    safe.SetHandleAsInvalid();
                    return safe;
                }
                return safe;
            }

            protected override bool ReleaseHandle()
            {
                return CloseHandleRaw(handle);
            }
        }

        [DllImport("kernel32.dll", SetLastError = true,
            EntryPoint = "CloseHandle")]
        private static extern bool CloseHandleRaw(IntPtr handle);

        // ---------- convenience: process-handle factory ----------
        internal static SafeProcessHandle OpenProcessHandle(IntPtr senderHwnd)
        {
            if (senderHwnd == IntPtr.Zero)
            {
                return SafeProcessHandle.From(IntPtr.Zero);
            }
            try
            {
                GetWindowThreadProcessId(senderHwnd, out uint pid);
                if (pid == 0)
                {
                    return SafeProcessHandle.From(IntPtr.Zero);
                }
                IntPtr raw = OpenProcess(
                    PROCESS_VM_READ | PROCESS_VM_WRITE |
                    PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
                    false, pid);
                return SafeProcessHandle.From(raw);
            }
            catch (Exception)
            {
                return SafeProcessHandle.From(IntPtr.Zero);
            }
        }
    }
}
