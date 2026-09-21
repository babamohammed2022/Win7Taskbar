// Win7Taskbar - P/Invoke verso il core nativo
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
using System.Runtime.InteropServices;

namespace Win7Taskbar.Interop
{
    /// <summary>Codici di ritorno del core nativo.</summary>
    internal static class W7TResult
    {
        public const int Ok = 0;
        public const int ErrAlreadyInit = -1;
        public const int ErrNotInit = -2;
        public const int ErrInvalidArg = -3;
        public const int ErrBufferTooSmall = -7;
        public const int ErrNotFound = -8;
        public const int ErrTrayTaken = -9;
    }

    /// <summary>Stato di una finestra (bitmask).</summary>
    [Flags]
    internal enum WindowStateFlags : uint
    {
        None = 0,
        Active = 0x0001,
        Minimized = 0x0002,
        Maximized = 0x0004,
        Flashing = 0x0008,
        Progress = 0x0010
    }

    /// <summary>Eventi inviati dal core.</summary>
    internal static class CoreEvent
    {
        public const int WindowAdded = 1;
        public const int WindowRemoved = 2;
        public const int WindowChanged = 3;
        public const int WindowActivated = 4;
        public const int WindowFlash = 5;
        public const int TrayAdd = 10;
        public const int TrayModify = 11;
        public const int TrayDelete = 12;
        public const int OverflowHidden = 21;   // v3.2
        public const int PinnedChanged = 22;    // v2.25
        public const int TrayBalloon = 13;
        public const int FullScreenChanged = 20;
    }

    /// <summary>Comandi della jump-list.</summary>
    internal static class WindowCommand
    {
        public const int Restore = 1;
        public const int Move = 2;
        public const int Size = 3;
        public const int Minimize = 4;
        public const int Maximize = 5;
        public const int Close = 6;
        public const int Activate = 7;
    }

    internal static class AppBarEdgeValue
    {
        public const int Left = 0;
        public const int Top = 1;
        public const int Right = 2;
        public const int Bottom = 3;
    }

    internal static class TrayClick
    {
        public const int Left = 1;
        public const int Right = 2;
        public const int Double = 3;
        public const int Middle = 4;
        public const int LeftDown = 5;
    }

    /// <summary>
    /// Riquadri (flyout) immersivi di Windows 10/11 esposti dal core nativo:
    /// rete, orologio, batteria e volume, cioe' i quattro riquadri che
    /// Windows 7 aggancia alle proprie icone di sistema.
    /// I valori coincidono con <c>W7T_FLYOUT_*</c> di Win7TaskbarCore.h e con
    /// <c>INVOKE_FLYOUT_*</c> di ExplorerPatcher, da cui la sequenza di
    /// invocazione nativa e' adattata: l'intero attraversa il confine della
    /// DLL senza tabelle di traduzione.
    /// </summary>
    internal enum FlyoutKind
    {
        Network = 1,
        Clock = 2,
        Battery = 3,
        Sound = 4
    }

    /// <summary>Mostra o nasconde un riquadro immersivo.</summary>
    internal static class FlyoutActionValue
    {
        public const int Show = 1;
        public const int Hide = 2;
    }

    // I layout devono combaciare esattamente con quelli di Win7TaskbarCore.h
    // (#pragma pack(push, 8)).

    [StructLayout(LayoutKind.Sequential, Pack = 8, CharSet = CharSet.Unicode)]
    internal struct W7TWindowInfo
    {
        public ulong Hwnd;
        public uint ProcessId;
        public uint State;
        public int MonitorIndex;
        public uint IconRevision;
        public uint Reserved;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string Title;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string AppId;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string ExePath;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8, CharSet = CharSet.Unicode)]
    internal struct W7TTrayIconInfo
    {
        public ulong OwnerHwnd;
        public uint Uid;
        public uint CallbackMessage;
        public uint IconRevision;
        public int IsPinned;
        public int IsHidden;
        public uint Version;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Tooltip;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string GuidKey;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 8, CharSet = CharSet.Unicode)]
    internal struct W7TBalloonInfo
    {
        public ulong OwnerHwnd;
        public uint Uid;
        public uint InfoFlags;
        public uint Timeout;
        public uint Reserved;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Title;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string Text;
    }

    /// <summary>
    /// Callback invocata dal core durante <see cref="NativeMethods.PumpEvents"/>.
    /// Deve essere mantenuta viva dal chiamante per evitare che il GC la sposti.
    /// </summary>
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void W7TEventCallback(int evt, ulong a, ulong b);

    /// <summary>Import diretti da Win7TaskbarCore.dll.</summary>
    internal static class NativeMethods
    {
        private const string Dll = "Win7TaskbarCore.dll";

        // --- ciclo di vita ---

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_Initialize(W7TEventCallback? cb);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_Shutdown();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern uint W7T_GetVersion();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_PumpEvents(int maxEvents);

        // --- superbar ---

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_RefreshWindows();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetWindowCount();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetWindows([Out] W7TWindowInfo[]? buffer, int capacity);

        // v2.25: Pinned Application Model nativo (scoperta/normalizzazione
        // in C++/Win32/Shell; la UI riceve solo il modello normalizzato).
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct W7TPinnedInfo
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string Identity;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string LnkPath;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string Target;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
            public string DisplayName;
            public int Order;
            public int Reserved;
        }

        [DllImport(Dll)]
        public static extern int W7T_GetPinnedCount();

        [DllImport(Dll)]
        public static extern int W7T_GetPinnedApps([Out] W7TPinnedInfo[]? buffer, int capacity);

        [DllImport(Dll)]
        public static extern void W7T_PinnedRefresh();

        /// <summary>v2.26: icona dell'app pinnata SENZA freccia di
        /// collegamento: stesso resolver nativo della ricerca. HICON di
        /// proprieta' del chiamante (DestroyIcon).</summary>
        [DllImport(Dll, CharSet = CharSet.Unicode)]
        public static extern IntPtr W7T_GetLinkIcon(string? lnk, string? target, int large);

        /// <summary>v2.28: avvia lnk/exe via shell con retry nativo.
        /// Ritorna true se la shell ha accettato l'avvio.</summary>
        [DllImport(Dll, CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool W7T_ShellOpen(string path);

        /* DestroyIcon e' gia' dichiarata piu' sotto in questa classe. */

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetWindowInfo(ulong hwnd, out W7TWindowInfo info);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetWindowIconBitmap(
            ulong hwnd, int desiredSize, out int width, out int height,
            [Out] byte[]? pixels, int pixelsBytes);

        /// <summary>Aero preview frame drawn by the core's 9-slice renderer:
        /// premultiplied BGRA, top-down, stride = width * 4, i.e. exactly a
        /// Pbgra32 bitmap. Pass <c>pixels = null</c> and
        /// <c>pixelsBytes = 0</c> to query the needed byte count.
        /// <c>accentArgb</c> is a 0x00RRGGBB tint (0 = no tint: the slices as
        /// they are on disk). Returns a negative <see cref="W7TResult"/> code
        /// when the frame is not applicable, in which case the caller keeps
        /// the frame its XAML template draws.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_RenderAeroThumbnailFrame(
            int width, int height, uint accentArgb,
            [Out] byte[]? pixels, int pixelsBytes);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ExecuteWindowCommand(ulong hwnd, int cmd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
                   CharSet = CharSet.Unicode)]
        public static extern int W7T_MinimizeGroup(
            [MarshalAs(UnmanagedType.LPWStr)] string appId);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
                   CharSet = CharSet.Unicode)]
        public static extern int W7T_CloseGroup(
            [MarshalAs(UnmanagedType.LPWStr)] string appId);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_IsFullScreenAppActive();

        // --- system tray ---

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_TrayStart();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_TrayStop();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetTrayIconCount();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetTrayIcons([Out] W7TTrayIconInfo[]? buffer, int capacity);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetTrayIconBitmap(
            ulong ownerHwnd, uint uid, out int width, out int height,
            [Out] byte[]? pixels, int pixelsBytes);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SendTrayIconClick(
            ulong ownerHwnd, uint uid, int clickType, int x, int y);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetTrayIconPinned(ulong ownerHwnd, uint uid, int pinned);

        /* Riordino del modello dal trascinamento: il core muove il pulsante
         * reale della ToolbarWindow32 con TB_MOVEBUTTON e allinea l'ordine
         * restituito da W7T_GetTrayIcons. */
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_TrayMoveIcon(ulong sourceHwnd, uint sourceUid,
                                                  ulong targetHwnd, uint targetUid,
                                                  int insertAfter);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetLastBalloon(out W7TBalloonInfo info);

        // --- appbar ---

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarRegister(ulong hwnd, int edge, int sizePx);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarSetPos(
            ulong hwnd, int edge, int sizePx,
            out int left, out int top, out int right, out int bottom);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarUnregister(ulong hwnd);

        // v3.4: protocollo AppBar completo (notifiche ABN_*, stato, attivazione).
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarCallbackMessage();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarIsRegistered();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarNotify(uint wParam, int lParam);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppBarActivate(ulong hwnd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetNativeTaskbarHidden(int hidden);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_IsNativeTaskbarHidden();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ReassertNativeTaskbarHidden();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetIconRect(ulong ownerHwnd, uint uid,
                                                 int left, int top,
                                                 int right, int bottom);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetShellRects(int barLeft, int barTop,
                                                   int barRight, int barBottom,
                                                   int notifyLeft, int notifyTop,
                                                   int notifyRight, int notifyBottom);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ReanchorFlyouts();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetChevronRect(int left, int top,
                                                    int right, int bottom);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        public static extern int W7T_ShowContextMenu(int x, int y, int bottomEdge, string items);

        /// <summary>v2.5: menu Win32 con sottomenu e spunte.
        /// v2.43: anchorAtCursor = 1 apre il menu sul punto esatto del
        /// cursore (menu della barra e dell'orologio); con 0 il menu resta
        /// ancorato all'area di lavoro, sopra i pulsanti (menu delle app).</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Unicode)]
        public static extern int W7T_ShowContextMenuEx(int x, int y, int bottomEdge,
                                                       string items, int anchorAtCursor);

        // v2.7: pannello overflow nativo con vetro Aero.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_OverflowInit(ulong ownerTaskbar);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_OverflowShow(int left, int top, int right, int bottom);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_OverflowHide();
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_OverflowIsVisible();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_OverflowRefresh();
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_OverflowGetRect(out int left, out int top, out int right, out int bottom);

        // v2.61: sempre 0 - la freccetta apre il pannello nostro su ogni
        // sistema. Sulle build di Windows 11 24H2 la freccetta della shell
        // non risponde all'invoke UI Automation, quindi il flyout di sistema
        // non si apriva e il clic non faceva nulla.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_OverflowUsesShellFlyout();

        /// <summary>
        /// v2.61: vero se il sistema e' Windows 11 (build >= 22000). Letto
        /// dal core con RtlGetVersion: la versione gestita non e'
        /// affidabile, perche' il manifest dell'app non dichiara Windows 10.
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_IsWindows11();

        /// <summary>
        /// v2.63: pubblica al core la scelta dei quattro riquadri. Da questo
        /// momento la decisione "Windows 7 oppure Windows 10/11" vive in un
        /// posto solo (il core) e tutti i percorsi di apertura la
        /// interrogano: prima ogni launcher aveva la propria copia della
        /// regola, ed e' per questo che la tendina "Windows 10/11" poteva
        /// aprire il riquadro di Windows 7 e viceversa.
        /// 1 = Windows 7 (classico), 0 = Windows 10/11 (shell).
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_SetFlyoutPreferences(
            int clockWin7, int networkWin7, int volumeWin7, int batteryWin7);

        /// <summary>
        /// v2.63: la porta dei riquadri moderni di questa build, decisa dal
        /// core (build via RtlGetVersion + infrastruttura della shell).
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_IsModernFlyoutHostAvailable();

        /// <summary>
        /// v2.62: chiude il riquadro dell'orologio DELLA SHELL, se aperto.
        /// Non lo apre mai: su Windows 11 il riquadro mostrato e' sempre
        /// quello ricreato da Win7Taskbar, e se il sistema ha aperto il suo
        /// per conto i due non devono convivere.
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_HideClockFlyout();

        // v3.0: optional app search panel.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppSearchInit(ulong ownerTaskbar, byte[]? argbPixels, int iconW, int iconH);

        // v1.4: selettore della lingua (port del mod switcher). Il testo
        // nella tray lo disegna il controllo gestito con la sigla che il
        // core legge dal thread col primo piano; il click apre il popup
        // nativo (finestra Win32 GDI del core).
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_LangSwitcherShow(ulong ownerHwnd,
            ulong foregroundHwnd, int styleMode);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_LangSwitcherHide();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
            CharSet = CharSet.Unicode)]
        public static extern void W7T_LangSwitcherGetActive(ref uint langId,
            [Out] char[] threeLetter, int threeCap,
            [Out] char[] twoLetter, int twoCap);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        public delegate void W7TLangChangedCallback(uint langId);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_LangSwitcherSetChangedCallback(
            W7TLangChangedCallback callback);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_PropertiesShow(ulong ownerTaskbar, int lang,
            int seconds, int nativeFlyout, int enableSearch, int netFlyout,
            int classicVolume, int batteryFlyout,
            int aeroPeek, int toolbarDesktop, int toolbarAddress, int toolbarLinks,
            int inputLanguageMode, int taskManagerMode,
            // v1.21.7: extra settings section.
            int flyoutColorMode, int flyoutColorRgb,
            int connectionPrivacyMode, int themeSelection,
            // v1.21.37: current autostart state (RetroBar logic, AutoStart.cs).
            int autoStart);

        /// <summary>
        /// v1.21.7: publishes the extra settings. flyoutColorMode 0 = system
        /// colour, 1 = custom; flyoutColorRgb = 0x00RRGGBB;
        /// connectionPrivacyMode 0 = normal, 1 = privacy (presentation only,
        /// in the recreated flyout).
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_SetExtraSettings(int flyoutColorMode,
            int flyoutColorRgb, int connectionPrivacyMode);

        /// <summary>v1.21.7: resolved colour of the recreated Windows
        /// 8-style flyout (system accent or chosen colour). 1 = ok.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetExtraFlyoutColor(out uint rgb);

        // v2.36: flyout di rete Windows 7 (porting MIT mod Windhawk).
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_NetFlyoutInit();

        // v2.62: comunica al core se il riquadro di rete di Windows 7 e' pronto.
        // Le icone di rete RICREATE (quelle della tray di Windows 11) vengono
        // gestite dal core: senza questo avviso il core non sa se puo' aprire
        // il riquadro ricreato o deve ripiegare su quello della shell.
        [DllImport("Win7TaskbarCore.dll", CallingConvention = CallingConvention.StdCall,
            EntryPoint = "W7T_SetWin7NetworkFlyout")]
        public static extern void W7T_SetWin7NetworkFlyout(int ready);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_NetFlyoutUninit();
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_NetFlyoutToggleAt(ref RECT rcIcon);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_IsNetworkTrayOwner(ulong ownerHwnd);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_AppSearchShow(int x, int y, int theme);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_AppSearchHide();

        // v2.37 punto 17: la lente funziona da interruttore (toggle).
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_AppSearchIsVisible();

        // v2.37 punto 16: lingua del flyout di rete = lingua dell'app.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_NetFlyoutSetLanguage(int appLanguageIndex);

        // v3.8: flyout di rete variante Windows 8 (riquadro ricreato;
        // implementazione: Administratox). Usa la STESSA logica di rete del
        // modulo Windows 7: qui si controlla solo il nuovo riquadro.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_Net8FlyoutInit();
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_Net8FlyoutUninit();
        // v3.8: chiude il riquadro Windows 8 senza smontare il modulo
        // (usata quando la modalita' di rete passa a un'altra voce).
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_Net8FlyoutHide();
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_Net8FlyoutToggleAt(ref RECT rcIcon);
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_Net8FlyoutSetLanguage(int appLanguageIndex);
        // v3.8: comunica al core se la variante Windows 8 e' pronta (stesso
        // patto di W7T_SetWin7NetworkFlyout per le icone di rete ricreate).
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_SetWin8NetworkFlyout(int ready);

        // --- v2.38: modulo proprietario icone, mixer classico, Jump List, ---
        // --- flyout batteria ricreato.                                    ---

        /// <summary>1 se l'icona tray appartiene al modulo indicato
        /// (SndVolSSO.dll = volume, stobject.dll = batteria).</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
                   CharSet = CharSet.Unicode)]
        public static extern int W7T_TrayOwnerModuleMatch(ulong ownerHwnd,
            [MarshalAs(UnmanagedType.LPWStr)] string moduleName);

        /// <summary>Avvia SndVol.exe -f (mixer classico). 1 = partito.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_LaunchClassicVolume(int x, int y);

        /// <summary>v2.41: chiude il mixer classico (SndVol) aperto.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_CloseClassicVolume();

        // Jump List stile Windows 7 - sistema del gesto (clic sinistro +
        // trascinamento verso l'alto,vedi TaskbarWindow.JumpList.cs).
        // TUTTE le coordinate (rettangolo pulsante, punti hover/release)
        // sono PIXEL FISICI DELLO SCHERMO: PointToScreen del WPF le produce
        // gia' in quel sistema su un processo Per-Monitor-V2, quindi qui
        // NON si moltiplica nessuna scala. La geometria del popup e' scalata
        // dal nativo sul DPI del monitor del pulsante.

        /// <summary>Apre il popup ancorato al pulsante e carica le voci
        /// reali dalla shell. Ritorna il numero di voci (>=0) o un codice
        /// negativo di fallimento. iconArgb = BGRA dritto, top-down (puo'
        /// essere null). outAppId riceve l'AppUserModelID risolta.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
                   CharSet = CharSet.Unicode)]
        public static extern int W7T_JumpListOpen(ref RECT buttonRect,
            int edge,
            [MarshalAs(UnmanagedType.LPWStr)] string title,
            [MarshalAs(UnmanagedType.LPWStr)] string launchPath,
            [MarshalAs(UnmanagedType.LPWStr)] string pinnedLnk,
            int isPinned, ulong hwnd,
            [MarshalAs(UnmanagedType.LPWStr)] string exePath,
            [In] uint[]? iconArgb, int iconW, int iconH, int lang,
            [Out] System.Text.StringBuilder? outAppId, int outAppIdCap);

        /// <summary>1 se il punto schermo e' ancora nell'area di
        /// interazione del gesto (popup + pulsante + corridoio).</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_JumpListSetHover(int screenX, int screenY);

        /// <summary>Trasferisce il popup dal gesto catturato all'input
        /// ordinario; il rilascio non attiva alcuna riga.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_JumpListMakeInteractive();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_JumpListActivateAt(int screenX, int screenY,
            out int bits);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_JumpListHide();

        /// <summary>Flyout batteria ricreato, ancorato al rettangolo icona.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_BatteryFlyoutShowAt(int left, int top,
            int right, int bottom);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_BatteryFlyoutHide();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_BatteryFlyoutSetLanguage(int lang);

        /// <summary>OPZIONE B: ripristino esplicito (idempotente) della
        /// chiave legacy batteria, chiamato in chiusura pulita.</summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_BatteryFlyoutRestoreLegacyKey();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_TrayImportExplorerIcons();

        /// <summary>
        /// v2.1: apre la pagina nativa di Windows per le icone dell'area di
        /// notifica (shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}).
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_OpenNotificationIconsSettings();

        /// <summary>
        /// v2.2: scrive una riga in log-core.txt dal lato gestito.
        /// </summary>
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern void W7T_Log([MarshalAs(UnmanagedType.LPWStr)] string line);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetVolume(ref int level, ref int muted);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetVolume(int level);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_SetVolumeMuted(int muted);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowClockFlyout(ulong taskbarHwnd);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowVolumeFlyout(ulong taskbarHwnd);

        // Riquadri immersivi di sistema: un solo import per tutti e quattro.
        // La sequenza nativa e' adattata da ExplorerPatcher/ImmersiveFlyouts.c
        // (valinet, GPL-2.0-or-later); vedere native/src/ImmersiveFlyouts.h.

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_InvokeFlyout(ulong taskbarHwnd, int kind, int action);





        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowVolumeMixer();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_GetPrimaryWorkArea(
            out int left, out int top, out int right, out int bottom);

        // --- shell ---

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ToggleShowDesktop();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowStartMenu();

        [DllImport(Dll)]
        public static extern int W7T_OpenStartFallback();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowTaskManager();

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowTaskManagerMode(int mode);

        // --- stili finestra (user32) ---
        //
        // Una taskbar non deve MAI diventare la finestra attiva: se lo fa,
        // ruba il fuoco all'applicazione su cui si sta cliccando e i comandi
        // arrivano alla finestra sbagliata. Serve WS_EX_NOACTIVATE.

        public const int GWL_EXSTYLE = -20;
        public const int WS_EX_TOOLWINDOW = 0x00000080;
        public const int WS_EX_NOACTIVATE = 0x08000000;

        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
        public static extern IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex);

        /// <summary>Inviare WM_APPCOMMAND (volume con la rotella, tecnica
        /// di RetroBar/VolumeChanger) e messaggi analoghi a una finestra.</summary>
        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW", SetLastError = true)]
        public static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong);

        [DllImport("user32.dll")]
        public static extern IntPtr GetForegroundWindow();

        // ---------------- v1.7.4: language bar via shell menu ----------------
        // The ITA indicator now opens a plain Win32 menu (the same
        // W7T_ShowContextMenuEx path the clock and the bar use, which never
        // took the process down) and applies the picked layout with the
        // canonical WM_INPUTLANGCHANGEREQUEST post. Public Win32 only; the
        // dedicated popup thread and its managed callback stay untouched in
        // the core but are no longer exercised.
        internal const uint WM_INPUTLANGCHANGEREQUEST = 0x0050;
        private const uint LOCALE_SLOCALIZEDDISPLAYNAME = 0x00000002;

        [DllImport("user32.dll")]
        public static extern uint GetKeyboardLayoutList(int nBuff,
            [Out] IntPtr[]? lpList);

        [DllImport("user32.dll")]
        public static extern IntPtr GetKeyboardLayout(uint idThread);

        [DllImport("user32.dll")]
        public static extern uint GetWindowThreadProcessId(IntPtr hWnd,
            out uint processId);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetLocaleInfoW(uint locale, uint lcType,
            [Out] System.Text.StringBuilder data, int size);

        [DllImport("user32.dll")]
        public static extern bool PostMessageW(IntPtr hWnd, uint msg,
            IntPtr wParam, IntPtr lParam);

        /// <summary>Installed HKLs (may contain duplicates for different
        /// keyboards of the same language: they are all offered).</summary>
        internal static IntPtr[] GetInstalledKeyboardLayouts()
        {
            try
            {
                uint count = GetKeyboardLayoutList(0, null);
                if (count == 0 || count > 64)
                {
                    return Array.Empty<IntPtr>();
                }
                var list = new IntPtr[count];
                uint filled = GetKeyboardLayoutList((int)count, list);
                if (filled == 0)
                {
                    return Array.Empty<IntPtr>();
                }
                Array.Resize(ref list, (int)filled);
                return list;
            }
            catch (Exception)
            {
                return Array.Empty<IntPtr>();
            }
        }

        /// <summary>Localized language name for an HKL (low word = language
        /// identifier), e.g. "Italiano" for 0x0410. Empty on failure.</summary>
        internal static string GetLanguageDisplayName(IntPtr hkl)
        {
            try
            {
                uint langId = (uint)(hkl.ToInt64() & 0xFFFF);
                var sb = new System.Text.StringBuilder(128);
                int n = GetLocaleInfoW(langId, LOCALE_SLOCALIZEDDISPLAYNAME,
                    sb, sb.Capacity);
                return n > 0 ? sb.ToString() : ("0x" + langId.ToString("X4"));
            }
            catch (Exception)
            {
                return string.Empty;
            }
        }

        // --- DWM: anteprime live delle finestre ---
        //
        // Stesse API usate dalla Superbar di Windows 7 e da RetroBar
        // (RetroBar/Controls/TaskThumbnail.xaml.cs, Apache 2.0, (c) dremin):
        // il compositore ridisegna la finestra sorgente dentro un rettangolo
        // della nostra finestra. Non e' uno screenshot, e' live.

        public const int DWM_TNP_RECTDESTINATION = 0x00000001;
        public const int DWM_TNP_RECTSOURCE = 0x00000002;
        public const int DWM_TNP_VISIBLE = 0x00000008;
        public const int DWM_TNP_SOURCECLIENTAREAONLY = 0x00000010;

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SIZE
        {
            public int cx;
            public int cy;
        }

        /// <summary>Parametro WM_WINDOWPOSCHANGED/WM_WINDOWPOSCHANGING:
        /// posizione e flag dell'operazione di spostamento in corso
        /// (serve a capire CHI ha mosso la finestra della barra).</summary>
        [StructLayout(LayoutKind.Sequential)]
        public class WINDOWPOS
        {
            public IntPtr hwnd;
            public IntPtr hwndInsertAfter;
            public int x;
            public int y;
            public int cx;
            public int cy;
            public uint flags;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        // v2.61: probes for the native Jump List popup (class W7T_JumpList,
        // created in-process by Win7TaskbarCore.dll). The managed side needs
        // to know whether the popup is on screen WITHOUT a new native
        // export, so a core whose dist/ DLL predates this feature keeps
        // working: an ordinary top-level window lookup answers it.
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr FindWindowW(string? lpClassName,
            string? lpWindowName);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool IsWindowVisible(IntPtr hWnd);

        /// <summary>Win32 MSG, layout-compatible with the native one; only
        /// what the jump list purge reads (hwnd/message).</summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct MSG
        {
            public IntPtr hwnd;
            public uint message;
            public IntPtr wParam;
            public IntPtr lParam;
            public uint time;
            public int ptX;
            public int ptY;
        }

        /// <summary>PM_REMOVE peel of one filtered message, used to drop a
        /// dismissal the previous jump list posted to the (reused) popup
        /// window before this one opened.</summary>
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool PeekMessageW(out MSG lpMsg, IntPtr hWnd,
            uint wMsgFilterMin, uint wMsgFilterMax, uint wRemoveMsg);

        [DllImport("kernel32.dll")]
        public static extern uint GetCurrentProcessId();

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

        [DllImport("user32.dll")]
        public static extern IntPtr WindowFromPoint(POINT point);

        [DllImport("user32.dll")]
        public static extern IntPtr GetAncestor(IntPtr hWnd, uint flags);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetCursorPos(out POINT lpPoint);

        /* =================================================================
         * v2.56: z-order helpers.
         *
         * Used by the tray drag ghost: it has to stay in front of the native
         * overflow panel, which is topmost as well. SetWindowPos with
         * HWND_TOPMOST moves a window to the front of the topmost band
         * without moving or resizing it.
         * ================================================================= */

        public static readonly IntPtr HWND_TOPMOST = new(-1);
        public static readonly IntPtr HWND_NOTOPMOST = new(-2);

        public const uint SWP_NOSIZE = 0x0001;
        public const uint SWP_NOMOVE = 0x0002;
        public const uint SWP_NOZORDER = 0x0004;
        public const uint SWP_NOACTIVATE = 0x0010;
        public const uint SWP_SHOWWINDOW = 0x0040;

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter,
                                               int X, int Y, int cx, int cy, uint uFlags);

        /// <summary>Messaggi registrati a livello di sessione ("TaskbarCreated",
        /// il messaggio di callback della nostra AppBar): il valore e' lo stesso
        /// per tutti i processi fino al riavvio della sessione.</summary>
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern uint RegisterWindowMessage(string messageName);

        [DllImport("gdi32.dll")]
        public static extern IntPtr CreateCompatibleDC(IntPtr hdc);

        [DllImport("gdi32.dll")]
        public static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int nWidth, int nHeight);

        [DllImport("gdi32.dll", SetLastError = true)]
        public static extern IntPtr SelectObject(IntPtr hdc, IntPtr hObject);

        public const uint SRCCOPY = 0x00CC0020;

        [DllImport("gdi32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool BitBlt(IntPtr destination, int xDest, int yDest,
                                         int width, int height, IntPtr source,
                                         int xSource, int ySource, uint rasterOperation);

        [DllImport("gdi32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool DeleteObject(IntPtr hObject);

        [DllImport("gdi32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool DeleteDC(IntPtr hdc);

        // Wrapper pubblici distinti da GetDC/ReleaseDC (privati, usati piu'
        // sotto per EnumFontFamiliesEx): stessa API di Windows, nome diverso
        // per non collidere con le firme private della stessa classe.
        [DllImport("user32.dll", EntryPoint = "GetDC")]
        public static extern IntPtr GetWindowClientDC(IntPtr hWnd);

        [DllImport("user32.dll", EntryPoint = "ReleaseDC")]
        public static extern int ReleaseWindowClientDC(IntPtr hWnd, IntPtr hDC);

        /* v2.2: per spingere il pannello overflow a 8 px dai bordi del
         * monitor come i flyout Aero (AdjustWindowPosForTaskbar del mod
         * "Aero Tray" di aubymori, riscritto senza hooking). */
        [StructLayout(LayoutKind.Sequential)]
        public struct POINT
        {
            public int x;
            public int y;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct MONITORINFO
        {
            public int cbSize;
            public RECT rcMonitor;
            public RECT rcWork;
            public uint dwFlags;
        }

        public const int MONITOR_DEFAULTTONEAREST = 2;

        [DllImport("user32.dll")]
        public static extern IntPtr MonitorFromPoint(POINT pt, int dwFlags);

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetMonitorInfoW(IntPtr hMonitor, ref MONITORINFO lpmi);

        /* v2.3: icone reali di file/cartelle per le bande desktop/collegamenti
         * (stessa fonte della shell). */
        public const uint SHGFI_ICON = 0x000000100;
        public const uint SHGFI_SMALLICON = 0x000000001;
        /* v2.6.1: riempie SHFILEINFOW.szDisplayName col nome che mostra
         * Explorer (nasconde le estensioni registrate: "File.lnk" -> "File"). */
        public const uint SHGFI_DISPLAYNAME = 0x000000200;
        public const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
        public const uint FILE_ATTRIBUTE_DIRECTORY = 0x00000010;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct SHFILEINFOW
        {
            public IntPtr hIcon;
            public int iIcon;
            public uint dwAttributes;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
            public string szDisplayName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)]
            public string szTypeName;
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr SHGetFileInfoW(string pszPath, uint dwFileAttributes,
                                                   ref SHFILEINFOW psfi, uint cbFileInfo, uint uFlags);

        /* v2.4: disposizione finestre del menu contestuale della barra,
         * come le voci equivalenti del menu vero di Windows 7. */
        [DllImport("user32.dll")]
        public static extern ushort CascadeWindows(IntPtr hwndParent, uint wHow,
                                                   IntPtr lpRect, uint cWnds, IntPtr[]? lpWnds);

        [DllImport("user32.dll")]
        public static extern ushort TileWindows(IntPtr hwndParent, uint wHow,
                                                IntPtr lpRect, uint cWnds, IntPtr[]? lpWnds);

        [StructLayout(LayoutKind.Sequential)]
        public struct DWM_THUMBNAIL_PROPERTIES
        {
            public int dwFlags;
            public RECT rcDestination;
            public RECT rcSource;
            public byte opacity;
            [MarshalAs(UnmanagedType.Bool)] public bool fVisible;
            [MarshalAs(UnmanagedType.Bool)] public bool fSourceClientAreaOnly;
        }

        [DllImport("dwmapi.dll", PreserveSig = true)]
        public static extern int DwmRegisterThumbnail(IntPtr dest, IntPtr src, out IntPtr thumb);

        [DllImport("dwmapi.dll", PreserveSig = true)]
        public static extern int DwmUnregisterThumbnail(IntPtr thumb);

        [DllImport("dwmapi.dll", PreserveSig = true)]
        public static extern int DwmUpdateThumbnailProperties(
            IntPtr hThumb, ref DWM_THUMBNAIL_PROPERTIES props);

        [DllImport("dwmapi.dll", PreserveSig = true)]
        public static extern int DwmQueryThumbnailSourceSize(IntPtr thumb, out SIZE size);

        // Documented desktop API. The returned DWORD is 0xAARRGGBB (not the
        // COLORREF 0x00BBGGRR layout used by many older Win32 functions).
        [DllImport("dwmapi.dll", PreserveSig = true)]
        public static extern int DwmGetColorizationColor(
            out uint colorization,
            [MarshalAs(UnmanagedType.Bool)] out bool opaqueBlend);

        // La firma vera e' HRESULT DwmIsCompositionEnabled(BOOL *pfEnabled):
        // il risultato torna nel parametro di uscita, non come valore di
        // ritorno. Dichiararla senza argomenti corrompe lo stack e provoca
        // una AccessViolationException.
        [DllImport("dwmapi.dll", PreserveSig = true)]
        private static extern int DwmIsCompositionEnabled(out bool enabled);

        // --- Aero Peek (anteprima del desktop) ---
        //
        // DwmpActivateLivePreview NON e' documentata e non ha un nome
        // esportato: si importa per ORDINALE 113. E' la stessa funzione che
        // usa la taskbar di Windows 7 quando si passa sopra al pulsante
        // "Mostra desktop": rende trasparenti tutte le finestre lasciando
        // solo i contorni.
        //
        // La firma cambia con la versione di Windows: da 8.1 in poi accetta
        // un parametro in piu'. Dichiariamo entrambe le varianti e scegliamo
        // a runtime, altrimenti su un sistema si corromperebbe lo stack.

        public enum AeroPeekType : uint
        {
            Desktop = 1,
            Window = 3
        }

        [DllImport("dwmapi.dll", EntryPoint = "#113", SetLastError = true)]
        private static extern int DwmpActivateLivePreview_Win7(
            uint enable, IntPtr hWndExclude, IntPtr hWndInsertBefore, AeroPeekType type);

        [DllImport("dwmapi.dll", EntryPoint = "#113", SetLastError = true)]
        private static extern int DwmpActivateLivePreview_Win81(
            uint enable, IntPtr hWndExclude, IntPtr hWndInsertBefore, AeroPeekType type,
            IntPtr reserved);

        private static readonly bool IsWindows81OrBetter =
            Environment.OSVersion.Version >= new Version(6, 3);

        // Una volta accertato che l'anteprima non e' disponibile non si
        // riprova piu'. Senza questo, ogni passaggio del mouse sul pulsante
        // ripeterebbe una chiamata che fallisce: su Wine la funzione non
        // esiste e ogni tentativo costa un'eccezione nativa.
        private static bool _livePreviewUnavailable;

        /// <summary>
        /// Verifica che l'ordinale 113 di dwmapi.dll esista davvero, PRIMA di
        /// tentare la chiamata.
        ///
        /// Il controllo non e' ridondante rispetto al try/catch piu' sotto:
        /// se l'ordinale e' presente ma non implementato -- e' il caso di
        /// Wine, che lo esporta come segnaposto -- la chiamata non solleva
        /// un'eccezione gestita, bensi' termina l'intero processo prima che
        /// il codice C# possa intercettare alcunche'. L'unica difesa e'
        /// non chiamarla affatto.
        /// </summary>
        private static bool IsLivePreviewPresent()
        {
            if (_livePreviewChecked)
            {
                return _livePreviewPresent;
            }

            _livePreviewChecked = true;
            _livePreviewPresent = false;

            IntPtr module = LoadLibraryW("dwmapi.dll");
            if (module == IntPtr.Zero)
            {
                return false;
            }

            try
            {
                // GetProcAddress con un ordinale: la parte alta del puntatore
                // deve essere zero, il numero sta nella parte bassa.
                IntPtr proc = GetProcAddress(module, new IntPtr(113));
                if (proc == IntPtr.Zero)
                {
                    return false;
                }

                // Su Wine l'ordinale risolve a un segnaposto condiviso da
                // tutte le funzioni non implementate. Lo si riconosce dal
                // fatto che la libreria dichiara di provenire da Wine.
                if (GetModuleHandleW("wine_get_version") != IntPtr.Zero
                    || IsRunningUnderWine())
                {
                    return false;
                }

                _livePreviewPresent = true;
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        /// <summary>
        /// True quando il programma gira su Wine invece che su Windows: la
        /// funzione wine_get_version esiste solo nella ntdll di Wine.
        /// </summary>
        private static bool IsRunningUnderWine()
        {
            IntPtr ntdll = GetModuleHandleW("ntdll.dll");
            return ntdll != IntPtr.Zero
                && GetProcAddress(ntdll, "wine_get_version") != IntPtr.Zero;
        }

        private static bool _livePreviewChecked;
        private static bool _livePreviewPresent;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string fileName);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr GetModuleHandleW(string moduleName);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, IntPtr ordinal);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true,
                   BestFitMapping = false)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        /// <summary>
        /// Attiva o disattiva l'anteprima del desktop (Aero Peek).
        /// Silenziosa in caso di errore: e' un effetto estetico, non deve mai
        /// impedire il funzionamento della barra.
        /// </summary>
        public static void ActivateLivePreview(bool enable, IntPtr excludeHwnd,
                                               AeroPeekType type = AeroPeekType.Desktop)
        {
            if (_livePreviewUnavailable || !IsCompositionEnabled()
                || !IsLivePreviewPresent())
            {
                return;
            }

            try
            {
                uint flag = enable ? 1u : 0u;

                if (IsWindows81OrBetter)
                {
                    DwmpActivateLivePreview_Win81(flag, excludeHwnd, IntPtr.Zero, type,
                                                  IntPtr.Zero);
                }
                else
                {
                    DwmpActivateLivePreview_Win7(flag, excludeHwnd, IntPtr.Zero, type);
                }
            }
            catch (Exception)
            {
                // L'ordinale 113 non e' documentato: puo' mancare del tutto
                // (accade sotto Wine, dove la chiamata viene interrotta) o
                // cambiare in una versione futura di Windows. In quel caso si
                // rinuncia all'effetto una volta per tutte.
                _livePreviewUnavailable = true;
            }
        }

        /// <summary>
        /// True se l'anteprima del desktop e' risultata non disponibile su
        /// questo sistema. Usato dal menu contestuale per non offrire
        /// un'opzione che non farebbe nulla.
        /// </summary>
        public static bool IsLivePreviewUnavailable => _livePreviewUnavailable;

        /// <summary>
        /// True se la composizione DWM e' attiva (quindi le anteprime live
        /// sono disponibili). In caso di errore risponde false senza lanciare.
        /// </summary>
        public static bool IsCompositionEnabled()
        {
            try
            {
                return DwmIsCompositionEnabled(out bool enabled) == 0 && enabled;
            }
            catch (Exception)
            {
                return false;
            }
        }

        // --- input: sblocco del foreground ---
        //
        // Trucco preso da RetroBar (TaskButton.AppButton_OnMouseWheel):
        // SetForegroundWindow fallisce se il processo non ha i diritti di
        // foreground. Un evento di tastiera sintetico con un codice non
        // assegnato (0xE8) fa si' che Windows ce li conceda, senza che
        // nessuna applicazione reagisca al tasto.

        public const int INPUT_KEYBOARD = 1;
        public const uint KEYEVENTF_KEYUP = 0x0002;

        [StructLayout(LayoutKind.Sequential)]
        public struct KEYBDINPUT
        {
            public ushort wVk;
            public ushort wScan;
            public uint dwFlags;
            public uint time;
            public IntPtr dwExtraInfo;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct INPUT
        {
            public int type;
            public INPUTUNION u;
        }

        [StructLayout(LayoutKind.Explicit)]
        public struct INPUTUNION
        {
            [FieldOffset(0)] public KEYBDINPUT ki;
            // Il campo mouse (MOUSEINPUT) e' piu' grande: riserviamo lo spazio
            // perche' la dimensione della struttura deve combaciare.
            [FieldOffset(0)] public MOUSEINPUT mi;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct MOUSEINPUT
        {
            public int dx;
            public int dy;
            public uint mouseData;
            public uint dwFlags;
            public uint time;
            public IntPtr dwExtraInfo;
        }

        [DllImport("user32.dll", SetLastError = true)]
        public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

        // --- enumerazione dei font (GDI) ---
        //
        // Serve per sapere se un font e' installato SENZA chiedere a WPF: le
        // API WPF (FontFamily.GetTypefaces, Typeface.TryGetGlyphTypeface)
        // abortiscono il processo con FailFast quando la famiglia non si
        // risolve, quindi non sono utilizzabili come test. EnumFontFamiliesEx
        // legge soltanto l'elenco di GDI e non puo' fallire in quel modo.

        private const int LF_FACESIZE = 32;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct LOGFONT
        {
            public int lfHeight;
            public int lfWidth;
            public int lfEscapement;
            public int lfOrientation;
            public int lfWeight;
            public byte lfItalic;
            public byte lfUnderline;
            public byte lfStrikeOut;
            public byte lfCharSet;
            public byte lfOutPrecision;
            public byte lfClipPrecision;
            public byte lfQuality;
            public byte lfPitchAndFamily;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = LF_FACESIZE)]
            public string lfFaceName;
        }

        private delegate int EnumFontExDelegate(IntPtr logFont, IntPtr textMetric,
                                                uint fontType, IntPtr lParam);

        [DllImport("gdi32.dll", CharSet = CharSet.Unicode)]
        private static extern int EnumFontFamiliesEx(IntPtr hdc, ref LOGFONT lpLogfont,
                                                     EnumFontExDelegate lpProc,
                                                     IntPtr lParam, uint dwFlags);

        [DllImport("user32.dll")]
        private static extern IntPtr GetDC(IntPtr hWnd);

        [DllImport("user32.dll")]
        private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

        /// <summary>
        /// True se la famiglia di font e' installata nel sistema.
        /// </summary>
        public static bool IsFontInstalled(string familyName)
        {
            if (string.IsNullOrWhiteSpace(familyName) || familyName.Length >= LF_FACESIZE)
            {
                return false;
            }

            IntPtr hdc = GetDC(IntPtr.Zero);
            if (hdc == IntPtr.Zero)
            {
                return false;
            }

            try
            {
                var logFont = new LOGFONT
                {
                    lfCharSet = 1, // DEFAULT_CHARSET
                    lfFaceName = familyName
                };

                bool found = false;
                EnumFontExDelegate callback = (_, _, _, _) =>
                {
                    found = true;
                    return 0; // basta il primo riscontro
                };

                EnumFontFamiliesEx(hdc, ref logFont, callback, IntPtr.Zero, 0);
                GC.KeepAlive(callback);

                return found;
            }
            finally
            {
                ReleaseDC(IntPtr.Zero, hdc);
            }
        }

        // --- memoria ---
        // Restituisce al sistema le pagine non in uso. Per un processo che
        // resta acceso per giorni facendo pochissimo, e' la differenza fra
        // un working set che cresce all'infinito e uno stabile.

        [DllImport("psapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool EmptyWorkingSet(IntPtr hProcess);

        [DllImport("kernel32.dll")]
        public static extern IntPtr GetCurrentProcess();

        // --- menu contestuali Win32 nativi ---
        // Apre il vero menu di sistema della finestra bersaglio (GetSystemMenu)
        // ed esegue la voce scelta con WM_SYSCOMMAND, come fa la shell.

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
        public static extern int W7T_ShowWindowSystemMenu(
            ulong hwnd, int x, int y, int bottomEdge);

        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
                   CharSet = CharSet.Unicode)]
        public static extern int W7T_ShowGroupMenu(
            ulong hwnd, int x, int y, int bottomEdge,
            [MarshalAs(UnmanagedType.LPWStr)] string minimizeText,
            [MarshalAs(UnmanagedType.LPWStr)] string closeText);

        // Pinned (idle) app menu: launch / pin-unpin rows with the app's
        // real shell icon on the launch row. Returns 1 = launch,
        // 2 = pin toggle, 0 = cancelled.
        [DllImport(Dll, CallingConvention = CallingConvention.StdCall,
                   CharSet = CharSet.Unicode)]
        public static extern int W7T_ShowPinMenu(
            int x, int y, int bottomEdge,
            [MarshalAs(UnmanagedType.LPWStr)] string launchText,
            [MarshalAs(UnmanagedType.LPWStr)] string pinText,
            [MarshalAs(UnmanagedType.LPWStr)] string lnkPath,
            [MarshalAs(UnmanagedType.LPWStr)] string targetPath);

        // -----------------------------------------------------------------
        //  Icone di sistema (fumetti di notifica)
        // -----------------------------------------------------------------

        // Identificatori IDI_* di user32: sono valori interi passati come
        // puntatore, non stringhe.
        public static readonly IntPtr IDI_ERROR = new IntPtr(32513);
        public static readonly IntPtr IDI_QUESTION = new IntPtr(32514);
        public static readonly IntPtr IDI_WARNING = new IntPtr(32515);
        public static readonly IntPtr IDI_INFORMATION = new IntPtr(32516);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr LoadIconW(IntPtr hInstance, IntPtr lpIconName);

        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool DestroyIcon(IntPtr hIcon);

        /// <summary>
        /// Carica una delle icone standard di Windows (informazione, avviso,
        /// errore) e la converte in una ImageSource utilizzabile da WPF.
        /// Restituisce null se l'icona non e' disponibile.
        /// </summary>
        public static System.Windows.Media.ImageSource? LoadStockIcon(IntPtr iconId)
        {
            // Le icone predefinite sono condivise dal sistema: LoadIconW non
            // ne crea una copia e DestroyIcon su di esse non serve. Lo
            // gestiamo comunque in modo sicuro non distruggendo l'handle.
            IntPtr hIcon = LoadIconW(IntPtr.Zero, iconId);

            if (hIcon == IntPtr.Zero)
            {
                return null;
            }

            try
            {
                var source = System.Windows.Interop.Imaging
                    .CreateBitmapSourceFromHIcon(
                        hIcon,
                        System.Windows.Int32Rect.Empty,
                        System.Windows.Media.Imaging.BitmapSizeOptions.FromEmptyOptions());

                source.Freeze();
                return source;
            }
            catch (Exception)
            {
                // Sotto Wine o con temi incompleti la conversione puo' fallire:
                // il fumetto viene mostrato senza icona.
                return null;
            }
        }

        // -----------------------------------------------------------------
        //  Fumetti di notifica: durata di sistema, suono e risposta all'app
        // -----------------------------------------------------------------

        /* v1.21.39: tre API di sistema usate dai fumetti, tutte nello
         * spirito di RetroBar/ManagedShell e della documentazione
         * NOTIFYICONDATA:
         *
         *  - SPI_GETMESSAGEDURATION: da Vista in poi uTimeout e' deprecato
         *    e la durata visibile segue l'impostazione di accessibilita'
         *    del sistema ("Notification display times are now based on
         *    system accessibility settings"). ManagedShell legge proprio
         *    questo valore quando l'app non chiede una durata valida.
         *  - PlaySound con l'alias "SystemNotification": il suono che
         *    Windows 7 (e RetroBar, e l'updater di Open-Shell) associano
         *    all'apertura del fumetto, salvo NIIF_NOSOUND.
         *  - SendNotifyMessage: recapita i codici NIN_BALLOON* alla
         *    finestra proprietaria senza bloccarsi se l'applicazione e'
         *    appesa; e' il canale documentato con cui la shell dice
         *    all'app "il tuo fumetto e' stato mostrato / chiuso / scaduto
         *    / cliccato". */

        private const uint SPI_GETMESSAGEDURATION = 0x200E;

        [DllImport("user32.dll", EntryPoint = "SystemParametersInfoW",
                   SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SystemParametersInfo(uint uiAction, uint uiParam,
                                                        ref uint pvParam, uint fWinIni);

        /// <summary>
        /// Durata standard dei messaggi di notifica del sistema, in secondi
        /// (impostazione di accessibilita'; di norma 5). 0 se non disponibile.
        /// </summary>
        public static uint GetMessageDurationSeconds()
        {
            uint seconds = 0;
            try
            {
                SystemParametersInfo(SPI_GETMESSAGEDURATION, 0, ref seconds, 0);
            }
            catch (Exception)
            {
                return 0;
            }
            return seconds;
        }

        // SND_ASYNC | SND_NODEFAULT | SND_APPLICATION | SND_ALIAS
        private const uint SND_NOTIFY_FLAGS = 0x0001 | 0x0002 | 0x0080 | 0x10000;

        [DllImport("winmm.dll", EntryPoint = "PlaySoundW", SetLastError = true,
                   CharSet = CharSet.Unicode)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool PlaySound(string? pszSound, IntPtr hmod, uint fdwSound);

        /// <summary>Suona l'evento di sistema "Notification" (alias).</summary>
        public static void PlayNotificationSound()
        {
            try
            {
                PlaySound("SystemNotification", IntPtr.Zero, SND_NOTIFY_FLAGS);
            }
            catch (Exception)
            {
                // winmm assente o schema audio senza l'evento: il fumetto
                // resta semplicemente silenzioso.
            }
        }

        [DllImport("user32.dll", EntryPoint = "SendNotifyMessageW",
                   SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool SendNotifyMessage(IntPtr hWnd, uint msg,
                                                    IntPtr wParam, IntPtr lParam);
    }
}
