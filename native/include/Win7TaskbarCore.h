/*
 * Win7Taskbar - Core nativo (Win32) - ABI pubblica
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Questo file fa parte di un progetto la cui struttura dei temi XAML e'
 * ispirata/derivata da RetroBar (https://github.com/dremin/RetroBar),
 * Copyright (c) dremin, Apache License 2.0, e la cui sezione
 * W7T_*Flyout* e' adattata da ExplorerPatcher
 * (https://github.com/valinet/ExplorerPatcher), Copyright (c) valinet,
 * GPL-2.0-or-later. Vedere CREDITS.txt e THIRD-PARTY-NOTICES.md.
 * Nessuna riga di codice sorgente C# di RetroBar e' stata copiata: la
 * logica e' stata reimplementata da zero in C++ nativo.
 */

#ifndef WIN7TASKBAR_CORE_H
#define WIN7TASKBAR_CORE_H

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef W7T_BUILDING_DLL
#define W7T_API __declspec(dllexport)
#else
#define W7T_API __declspec(dllimport)
#endif

#define W7T_CALL __stdcall

/* ------------------------------------------------------------------ */
/*  Costanti                                                           */
/* ------------------------------------------------------------------ */

#define W7T_MAX_TITLE   260
#define W7T_MAX_APPID   260
#define W7T_MAX_PATH_   260
#define W7T_MAX_TOOLTIP 256

/* Codici di ritorno */
#define W7T_OK                    0
#define W7T_ERR_ALREADY_INIT     -1
#define W7T_ERR_NOT_INIT         -2
#define W7T_ERR_INVALID_ARG      -3
#define W7T_ERR_REGISTER_CLASS   -4
#define W7T_ERR_CREATE_WINDOW    -5
#define W7T_ERR_APPBAR           -6
#define W7T_ERR_BUFFER_TOO_SMALL -7
#define W7T_ERR_NOT_FOUND        -8
#define W7T_ERR_TRAY_TAKEN       -9

/* Stato finestra (bitmask) */
#define W7T_WS_ACTIVE       0x0001
#define W7T_WS_MINIMIZED    0x0002
#define W7T_WS_MAXIMIZED    0x0004
#define W7T_WS_FLASHING     0x0008
#define W7T_WS_PROGRESS     0x0010

/* Eventi notificati al managed layer */
#define W7T_EVT_WINDOW_ADDED       1
#define W7T_EVT_WINDOW_REMOVED     2
#define W7T_EVT_WINDOW_CHANGED     3
#define W7T_EVT_WINDOW_ACTIVATED   4
#define W7T_EVT_WINDOW_FLASH       5
#define W7T_EVT_TRAY_ADD           10
#define W7T_EVT_TRAY_MODIFY        11
#define W7T_EVT_TRAY_DELETE        12
#define W7T_EVT_TRAY_BALLOON       13
#define W7T_EVT_FULLSCREEN_CHANGED 20
#define W7T_EVT_OVERFLOW_HIDDEN    21   /* v3.2: il pannello overflow si e' chiuso da solo */
#define W7T_EVT_PINNED_CHANGED     22   /* v2.25: cambiata la cartella dei pin */

/* Comandi jump-list / finestra */
#define W7T_CMD_RESTORE     1
#define W7T_CMD_MOVE        2
#define W7T_CMD_SIZE        3
#define W7T_CMD_MINIMIZE    4
#define W7T_CMD_MAXIMIZE    5
#define W7T_CMD_CLOSE       6
#define W7T_CMD_ACTIVATE    7

/* Bordi AppBar (coerenti con ABE_*) */
#define W7T_EDGE_LEFT   0
#define W7T_EDGE_TOP    1
#define W7T_EDGE_RIGHT  2
#define W7T_EDGE_BOTTOM 3

/* Click tray inoltrati all'owner */
#define W7T_TRAY_CLICK_LEFT        1
#define W7T_TRAY_CLICK_RIGHT       2
#define W7T_TRAY_CLICK_DOUBLE      3
#define W7T_TRAY_CLICK_MIDDLE      4
#define W7T_TRAY_CLICK_LEFT_DOWN   5

/* ------------------------------------------------------------------ */
/*  Riquadri (flyout) immersivi di sistema                             */
/*                                                                     */
/*  Stessi valori di INVOKE_FLYOUT_* di ExplorerPatcher, da cui la      */
/*  sequenza di invocazione e' adattata. Il frontend passa l'intero     */
/*  cosi' com'e', senza tabelle di traduzione.                          */
/* ------------------------------------------------------------------ */

#define W7T_FLYOUT_NETWORK        1
#define W7T_FLYOUT_CLOCK          2
#define W7T_FLYOUT_BATTERY        3
#define W7T_FLYOUT_SOUND          4
#define W7T_FLYOUT_ACTION_CENTER  5

#define W7T_FLYOUT_SHOW           1
#define W7T_FLYOUT_HIDE           2

/* ------------------------------------------------------------------ */
/*  Strutture (layout sequenziale, allineamento naturale)              */
/* ------------------------------------------------------------------ */

#pragma pack(push, 8)

typedef struct W7T_WindowInfo {
    uint64_t hwnd;                    /* HWND come intero a 64 bit      */
    uint32_t processId;
    uint32_t state;                   /* bitmask W7T_WS_*               */
    int32_t  monitorIndex;
    uint32_t iconRevision;            /* cambia quando l'icona cambia   */
    uint32_t reserved;
    wchar_t  title[W7T_MAX_TITLE];
    wchar_t  appId[W7T_MAX_APPID];    /* chiave di grouping             */
    wchar_t  exePath[W7T_MAX_PATH_];
} W7T_WindowInfo;

typedef struct W7T_TrayIconInfo {
    uint64_t ownerHwnd;
    uint32_t uid;
    uint32_t callbackMessage;
    uint32_t iconRevision;
    int32_t  isPinned;                /* 1 = visibile in tray, 0 = overflow */
    int32_t  isHidden;                /* NIS_HIDDEN                     */
    uint32_t version;
    wchar_t  tooltip[W7T_MAX_TOOLTIP];
    wchar_t  guidKey[64];             /* GUID string, se presente       */
} W7T_TrayIconInfo;

typedef struct W7T_BalloonInfo {
    uint64_t ownerHwnd;
    uint32_t uid;
    uint32_t infoFlags;
    uint32_t timeout;
    uint32_t reserved;
    wchar_t  title[64];
    wchar_t  text[256];
} W7T_BalloonInfo;

/* v2.25: Pinned Application Model (scoperta/normalizzazione nel core). */
typedef struct W7T_PinnedInfo {
    wchar_t identity[W7T_MAX_APPID];  /* chiave dedup/grouping            */
    wchar_t lnkPath[W7T_MAX_TITLE];   /* primo .lnk valido per identita'  */
    wchar_t target[W7T_MAX_TITLE];    /* target normalizzato (o vuoto)    */
    wchar_t displayName[128];         /* nome mostrato                    */
    int32_t order;                    /* posizione nella Superbar         */
    int32_t reserved;
} W7T_PinnedInfo;

#pragma pack(pop)

/* Callback verso il managed layer.
 * evt   : uno dei W7T_EVT_*
 * a / b : dipendono dall'evento (hwnd, uid, ...)
 * Chiamata sempre dal thread che ha creato il core (marshalled via PostMessage).
 */
typedef void (W7T_CALL *W7T_EventCallback)(int32_t evt, uint64_t a, uint64_t b);

/* ------------------------------------------------------------------ */
/*  Ciclo di vita                                                      */
/* ------------------------------------------------------------------ */

W7T_API int32_t  W7T_CALL W7T_Initialize(W7T_EventCallback cb);
W7T_API void     W7T_CALL W7T_Shutdown(void);
W7T_API uint32_t W7T_CALL W7T_GetVersion(void);

/* Pompa gli eventi accodati; da chiamare sul thread UI (es. da un DispatcherTimer
 * o dal message hook). Restituisce il numero di eventi processati. */
W7T_API int32_t  W7T_CALL W7T_PumpEvents(int32_t maxEvents);

/* ------------------------------------------------------------------ */
/*  Superbar: enumerazione finestre e grouping                         */
/* ------------------------------------------------------------------ */

W7T_API int32_t  W7T_CALL W7T_RefreshWindows(void);
W7T_API int32_t  W7T_CALL W7T_GetWindowCount(void);
W7T_API int32_t  W7T_CALL W7T_GetWindows(W7T_WindowInfo* buffer, int32_t capacity);
W7T_API int32_t  W7T_CALL W7T_GetWindowInfo(uint64_t hwnd, W7T_WindowInfo* out);

/* Icona finestra come BGRA premoltiplicato, top-down.
 * Passare pixels=NULL per interrogare solo width/height. */
W7T_API int32_t  W7T_CALL W7T_GetWindowIconBitmap(uint64_t hwnd, int32_t desiredSize,
                                                  int32_t* width, int32_t* height,
                                                  uint8_t* pixels, int32_t pixelsBytes);

/* Aero preview frame drawn by the core's 9-slice renderer
 * (native/src/AeroThumbnailFrame.cpp) instead of by the frontend's XAML
 * frame template. Output: premultiplied BGRA, top-down, stride = width * 4 -
 * the same layout W7T_GetWindowIconBitmap uses, so the frontend wraps it in a
 * Pbgra32 BitmapSource with no conversion. The four corners keep their pixel
 * size, the four edges stretch only along their own direction and the centre
 * cell stays fully transparent for the live DWM thumbnail.
 *
 * accentArgb: 0x00RRGGBB tint applied to the grayscale slices (its alpha is
 * ignored, the slices keep their own, modulated by luminance exactly like the
 * frontend's derived mask). 0 leaves the slices as they are on disk.
 *
 * Call with pixels=NULL and pixelsBytes=0 to query: the return value is then
 * the number of bytes the render needs (width * height * 4). A real call
 * returns the number of bytes written, or a negative W7T_ERR_* code when the
 * 9-slice set is not applicable (slices missing next to the executable,
 * size smaller than the border sum, DC/DIB failure). On any error the
 * frontend keeps its XAML frame: this export is an opportunity, never a
 * requirement. */
W7T_API int32_t  W7T_CALL W7T_RenderAeroThumbnailFrame(int32_t width, int32_t height,
                                                       uint32_t accentArgb,
                                                       uint8_t* pixels,
                                                       int32_t pixelsBytes);

W7T_API int32_t  W7T_CALL W7T_ExecuteWindowCommand(uint64_t hwnd, int32_t cmd);
W7T_API int32_t  W7T_CALL W7T_MinimizeGroup(const wchar_t* appId);
W7T_API int32_t  W7T_CALL W7T_CloseGroup(const wchar_t* appId);
W7T_API int32_t  W7T_CALL W7T_IsFullScreenAppActive(void);

/* ------------------------------------------------------------------ */
/*  System tray (server Shell_TrayWnd)                                 */
/* ------------------------------------------------------------------ */

W7T_API int32_t  W7T_CALL W7T_TrayStart(void);
W7T_API void     W7T_CALL W7T_TrayStop(void);
W7T_API int32_t  W7T_CALL W7T_GetTrayIconCount(void);
W7T_API int32_t  W7T_CALL W7T_GetTrayIcons(W7T_TrayIconInfo* buffer, int32_t capacity);
W7T_API int32_t  W7T_CALL W7T_GetTrayIconBitmap(uint64_t ownerHwnd, uint32_t uid,
                                                int32_t* width, int32_t* height,
                                                uint8_t* pixels, int32_t pixelsBytes);
W7T_API int32_t  W7T_CALL W7T_SendTrayIconClick(uint64_t ownerHwnd, uint32_t uid,
                                                int32_t clickType, int32_t x, int32_t y);
W7T_API int32_t  W7T_CALL W7T_SetTrayIconPinned(uint64_t ownerHwnd, uint32_t uid, int32_t pinned);

/* Riordino del modello dopo il trascinamento nella barra: sposta l'icona
 * (ownerHwnd,uid) accanto a (targetHwnd,targetUid) nel toolbar reale con
 * TB_MOVEBUTTON, come fa la shell. insertAfter: 1 per dopo il bersaglio. */
W7T_API int32_t  W7T_CALL W7T_TrayMoveIcon(uint64_t sourceHwnd, uint32_t sourceUid,
                                           uint64_t targetHwnd, uint32_t targetUid,
                                           int32_t insertAfter);

W7T_API int32_t  W7T_CALL W7T_GetLastBalloon(W7T_BalloonInfo* out);

/* ------------------------------------------------------------------ */
/*  AppBar                                                             */
/* ------------------------------------------------------------------ */

W7T_API int32_t  W7T_CALL W7T_AppBarRegister(uint64_t hwnd, int32_t edge, int32_t sizePx);
W7T_API int32_t  W7T_CALL W7T_AppBarSetPos(uint64_t hwnd, int32_t edge, int32_t sizePx,
                                           int32_t* outLeft, int32_t* outTop,
                                           int32_t* outRight, int32_t* outBottom);
W7T_API int32_t  W7T_CALL W7T_AppBarUnregister(uint64_t hwnd);
/* v3.4: protocollo AppBar completo (notifiche ABN_*, stato, attivazione). */
W7T_API int32_t  W7T_CALL W7T_AppBarCallbackMessage(void);
W7T_API int32_t  W7T_CALL W7T_AppBarIsRegistered(void);
W7T_API int32_t  W7T_CALL W7T_AppBarNotify(uint32_t wParam, int32_t lParam);
W7T_API int32_t  W7T_CALL W7T_AppBarWindowPosChanged(uint64_t hwnd);
W7T_API int32_t  W7T_CALL W7T_AppBarActivate(uint64_t hwnd);
W7T_API int32_t  W7T_CALL W7T_SetNativeTaskbarHidden(int32_t hidden);
W7T_API int32_t  W7T_CALL W7T_IsNativeTaskbarHidden(void);
W7T_API int32_t  W7T_CALL W7T_GetPrimaryWorkArea(int32_t* left, int32_t* top,
                                                 int32_t* right, int32_t* bottom);

/* ------------------------------------------------------------------ */
/*  Shell / desktop                                                    */
/* ------------------------------------------------------------------ */

W7T_API int32_t  W7T_CALL W7T_ToggleShowDesktop(void);
W7T_API int32_t  W7T_CALL W7T_ShowStartMenu(void);
W7T_API int32_t W7T_CALL W7T_OpenStartFallback(void);

/* Menu contestuale generico: voci separate da '\n', "-" = separatore,
 * prefisso '!' = voce disabilitata. Restituisce l'indice della voce scelta
 * a partire da 1 (i separatori non contano), 0 se annullato. */
W7T_API int32_t  W7T_CALL W7T_ShowContextMenu(int32_t x, int32_t y,
                                              int32_t bottomEdge,
                                              const wchar_t* items);

/* v2.5: come W7T_ShowContextMenu ma con sottomenu (">nome" ... "<") e
 * voci spuntate ("*nome"): serve al menu contestuale della barra, che
 * deve avere il sottomenu "Barre degli strumenti" come quello vero di
 * Windows 7. Vedere native/src/ShellMenu.h per la sintassi.
 *
 * v2.43: anchorAtCursor = 1 fa aprire il menu ESATTAMENTE sul punto
 * (x, y) ricevuto, cioe' dove sta il cursore: e' il comportamento del
 * menu della barra e dell'orologio. Con 0 il menu viene ancorato al
 * bordo superiore dell'area di lavoro del monitor (menu delle app, che
 * devono aprirsi sopra il pulsante della Superbar). */
W7T_API int32_t  W7T_CALL W7T_ShowContextMenuEx(int32_t x, int32_t y,
                                                int32_t bottomEdge,
                                                const wchar_t* items,
                                                int32_t anchorAtCursor);

/* Importa le icone gia' presenti nell'area di notifica di Explorer.
 * Restituisce quante ne sono state importate. */
W7T_API int32_t  W7T_CALL W7T_TrayImportExplorerIcons(void);

/* Volume del dispositivo audio predefinito (0..100). */
W7T_API int32_t  W7T_CALL W7T_GetVolume(int32_t* level, int32_t* muted);
W7T_API int32_t  W7T_CALL W7T_SetVolume(int32_t level);
W7T_API int32_t  W7T_CALL W7T_SetVolumeMuted(int32_t muted);

/* Riquadri di sistema: calendario Aero e volume.
 * Ricevono l'HWND della NOSTRA barra, usato come proprietario e per il
 * calcolo della posizione. */
W7T_API int32_t  W7T_CALL W7T_ShowClockFlyout(uint64_t taskbarHwnd);
W7T_API int32_t  W7T_CALL W7T_ShowVolumeFlyout(uint64_t taskbarHwnd);

/* Riquadri immersivi di Windows 10/11: rete, orologio, batteria, volume.
 *
 * Un solo export per tutti e quattro, cosi' l'ABI non si moltiplica: kind
 * e' uno dei W7T_FLYOUT_* qui sopra, action e' W7T_FLYOUT_SHOW oppure
 * W7T_FLYOUT_HIDE. Con action = W7T_FLYOUT_HIDE l'HWND e' ignorato.
 * L'HWND della NOSTRA barra serve da ancora per collocare il riquadro.
 *
 * La sequenza di invocazione e' adattata da ExplorerPatcher
 * (ImmersiveFlyouts.c di valinet, GPL-2.0-or-later); differenze e dettagli
 * stanno in native/src/ImmersiveFlyouts.h.
 *
 * W7T_ShowClockFlyout e W7T_ShowVolumeFlyout restano per non rompere il
 * codice che gia' le usa: l'orologio in piu' ha il ripiego Aero Clock. */
W7T_API int32_t  W7T_CALL W7T_InvokeFlyout(uint64_t taskbarHwnd, int32_t kind,
                                           int32_t action);

/* Mixer volume classico (SndVol.exe). */
W7T_API int32_t  W7T_CALL W7T_ShowVolumeMixer(void);

/* Rinasconde la taskbar di Explorer se e' ricomparsa. Explorer la rimostra
 * da solo (menu Start, cambio risoluzione, riavvio della shell). */
W7T_API int32_t  W7T_CALL W7T_ReassertNativeTaskbarHidden(void);
W7T_API int32_t  W7T_CALL W7T_ShowTaskManager(void);
/* 0 automatico, 1 Windows 11 moderno, 2 legacy 32-bit. Su Windows 10
 * viene sempre forzato 0. */
W7T_API int32_t  W7T_CALL W7T_ShowTaskManagerMode(int32_t mode);

/* v2.1: apre la pagina NATIVA di Windows per le icone dell'area di
 * notifica: il bersaglio del link "Personalizza..." del riquadro di
 * overflow. Usa il namespace shell
 *   shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}
 * che su Windows 7 apre l'applet classica "Icone dell'area di notifica"
 * e su Windows 10/11 viene reindirizzato dal sistema alla pagina
 * Impostazioni corrispondente (nessuna finestra sostitutiva: apre cio'
 * che il sistema offre). Restituisce W7T_OK se la shell ha accettato la
 * richiesta, altrimenti un codice d'errore (il frontend ha un ripiego). */
W7T_API int32_t  W7T_CALL W7T_OpenNotificationIconsSettings(void);

/* v2.2: scrive una riga in log-core.txt dal lato gestito. Serve a
 * tracciare, su Windows vero, i percorsi interattivi (drag&drop, apertura
 * overflow, Personalizza) che non si possono eseguire in compilazione. */
W7T_API void     W7T_CALL W7T_Log(const wchar_t* line);

/* ------------------------------------------------------------------ */
/*  Menu contestuali Win32 nativi                                      */
/*                                                                     */
/*  Windows 7 non disegna una finta jump-list: apre il VERO menu di     */
/*  sistema della finestra (GetSystemMenu) ed esegue le voci con        */
/*  WM_SYSCOMMAND. Queste due funzioni fanno esattamente questo, cosi'  */
/*  il menu ha l'aspetto del sistema e interagisce davvero con l'app.   */
/* ------------------------------------------------------------------ */

/* Menu di sistema reale della finestra. bottomEdge=1 lo apre verso l'alto. */
W7T_API int32_t  W7T_CALL W7T_ShowWindowSystemMenu(uint64_t hwnd, int32_t x, int32_t y,
                                                   int32_t bottomEdge);

/* Menu di gruppo nativo. Ritorna 1=minimizza gruppo, 2=chiudi gruppo, 0=annullato. */
W7T_API int32_t  W7T_CALL W7T_ShowGroupMenu(uint64_t hwnd, int32_t x, int32_t y,
                                            int32_t bottomEdge,
                                            const wchar_t* minimizeText,
                                            const wchar_t* closeText);

/* Pinned (idle) app menu: "launch" / "pin-unpin" rows, with the app's real
 * shell icon on the launch row. Ritorna 1=launch, 2=pin toggle, 0=annullato.
 * The generic W7T_ShowContextMenu* menus are untouched by the icon work:
 * only program-icon (Superbar) menus gain native icons. */
W7T_API int32_t  W7T_CALL W7T_ShowPinMenu(int32_t x, int32_t y,
                                          int32_t bottomEdge,
                                          const wchar_t* launchText,
                                          const wchar_t* pinText,
                                          const wchar_t* lnkPath,
                                          const wchar_t* targetPath);


/* v2.7: pannello overflow nativo (vetro Aero vero). Se W7T_OverflowInit
 * ritorna 0 il managed layer usa il fallback WPF. */
W7T_API int32_t W7T_CALL W7T_OverflowInit(uint64_t ownerTaskbar);
W7T_API void    W7T_CALL W7T_OverflowShow(int32_t left, int32_t top,
                                          int32_t right, int32_t bottom);
W7T_API void    W7T_CALL W7T_OverflowHide(void);
W7T_API int32_t W7T_CALL W7T_ShellOpen(const wchar_t* path);
W7T_API int32_t W7T_CALL W7T_OverflowIsVisible(void);
W7T_API void    W7T_CALL W7T_OverflowRefresh(void);
W7T_API int32_t W7T_CALL W7T_OverflowGetRect(int32_t* left, int32_t* top,
                                            int32_t* right, int32_t* bottom);

/* v3.0: ricerca applicazioni opzionale. */
W7T_API int32_t W7T_CALL W7T_AppSearchInit(uint64_t ownerTaskbar,
        const uint32_t* argbPixels, int32_t iconW, int32_t iconH);
W7T_API void    W7T_CALL W7T_PropertiesShow(uint64_t ownerTaskbar,
        int32_t lang, int32_t seconds, int32_t nativeFlyout,
        int32_t enableSearch, int32_t netFlyout, int32_t classicVolume,
        int32_t batteryFlyout, int32_t aeroPeek, int32_t toolbarDesktop,
        int32_t toolbarAddress, int32_t toolbarLinks,
        int32_t inputLanguageMode, int32_t taskManagerMode,
        /* v1.21.7: extra settings section (fields appended at the end, like
         * all the others added over the years). */
        int32_t flyoutColorMode, int32_t flyoutColorRgb,
        int32_t connectionPrivacyMode, int32_t themeSelection);

/* v1.21.7: secondary settings published by the frontend (which stores them
 * in its own configuration: the core writes no file). The privacy mode
 * changes only the text drawn by the recreated connection flyout; the colour
 * stays available to the Windows 8-style flyout, not implemented yet, and
 * touches no Windows 7 flyout. */
W7T_API void    W7T_CALL W7T_SetExtraSettings(int32_t flyoutColorMode,
        uint32_t flyoutColorRgb, int32_t connectionPrivacyMode);
W7T_API int32_t W7T_CALL W7T_GetExtraFlyoutColor(uint32_t* outRgb);
W7T_API void    W7T_CALL W7T_AppSearchShow(int32_t x, int32_t y);
W7T_API void    W7T_CALL W7T_AppSearchHide(void);
W7T_API int32_t W7T_CALL W7T_AppSearchIsVisible(void);
/* ------------------------------------------------------------------ */
/*  v2.36: flyout di rete Windows 7 (porting MIT della mod Windhawk   */
/*  "Windows 7 Network Flyout Recreation" v5.0.0). L'icona di rete    */
/*  nella taskbar resta quella originale di Windows: questi export    */
/*  controllano solo QUALE flyout si apre al click.                   */
/* ------------------------------------------------------------------ */

/* Inizializza il modulo (una volta). 1 = ok. */
W7T_API int32_t W7T_CALL W7T_NetFlyoutInit(void);

/* Teardown completo. */
W7T_API void     W7T_CALL W7T_NetFlyoutUninit(void);

/* Apre/chiude il flyout ancorandolo al rettangolo (coordinate schermo)
 * dell'icona di rete nel tray di Win7Taskbar. */
W7T_API void     W7T_CALL W7T_NetFlyoutToggleAt(const RECT* rcIcon);

/* Ritorna 1 se la finestra proprietaria dell'icona tray appartiene a
 * pnidui.dll (l'icona di rete di Windows). */
W7T_API int32_t W7T_CALL W7T_IsNetworkTrayOwner(uint64_t ownerHwnd);

/* v2.38: il managed chiede se l'icona tray appartiene a un modulo di
 * sistema specifico (es. SndVolSSO.dll per il volume, stobject.dll per
 * la batteria) per decidere quale flyout ricreato aprire. */
W7T_API int32_t W7T_CALL W7T_TrayOwnerModuleMatch(uint64_t ownerHwnd,
                                                  const wchar_t* moduleName);

/* v2.38 punto 1: mixer volume classico (SndVol.exe -f). Fire-and-forget;
 * ritorna 1 se il processo e' partito, 0 per ripiegare sul flyout moderno. */
W7T_API int32_t W7T_CALL W7T_LaunchClassicVolume(int32_t x, int32_t y);

/* ------------------------------------------------------------------ */
/*  Jump List stile Windows 7 - sistema del gesto (clic sinistro +      */
/*  trascinamento verso l'alto). TUTTE le coordinate (rettangolo del    */
/*  pulsante, punti di hover/attivazione) sono PIXEL FISICI DELLO       */
/*  SCHERMO; la geometria del popup e' scalata sul DPI del monitor      */
/*  del pulsante. iconArgb = BGRA dritto (non premoltiplicato),         */
/*  top-down. I dati vengono solo dalle API pubbliche della shell       */
/*  (IApplicationDocumentLists + property store della finestra/.lnk):   */
/*  nessuna voce inventata.                                             */
/* ------------------------------------------------------------------ */

/* Costruisce e mostra il popup ancorato al pulsante. Ritorna il numero
 * di voci reali lette dalla shell (>=0), o un codice di fallimento
 * negativo (-1 COM/Shell, -2 creazione finestra, -3 argomento nullo):
 * il lato gestito annulla il gesto in modo controllato. outAppId riceve
 * l'AppUserModelID risolta per il log gestito (puo' restare vuota). */
W7T_API int32_t W7T_CALL W7T_JumpListOpen(const RECT* buttonRect,
        int32_t edge, const wchar_t* title, const wchar_t* launchPath,
        const wchar_t* pinnedLnk, int32_t isPinned, uint64_t hwnd,
        const wchar_t* exePath, const uint32_t* iconArgb,
        int32_t iconW, int32_t iconH, int32_t lang,
        wchar_t* outAppId, int32_t outAppIdCap);

/* 1 se il punto schermo e' ancora nell'area di interazione del gesto
 * (popup + pulsante + corridoio fra i due), 0 se l'ha lasciata. */
W7T_API int32_t W7T_CALL W7T_JumpListSetHover(int32_t screenX,
        int32_t screenY);

/* Al rilascio del gesto trasferisce focus e input ordinario al popup,
 * senza attivare la riga sotto il cursore. */
W7T_API void W7T_CALL W7T_JumpListMakeInteractive(void);

/* Attiva una riga da un clic ordinario nel popup persistente. Conservato
 * anche come ABI per client precedenti. */
W7T_API int32_t W7T_CALL W7T_JumpListActivateAt(int32_t screenX,
        int32_t screenY, int32_t* outBits);

W7T_API void W7T_CALL W7T_JumpListHide(void);

/* ------------------------------------------------------------------ */
/*  v1.4: selettore della lingua in stile Windows 7/8.1 (port del mod */
/*  "win7-language-switcher-restorer"). ownerHwnd = la barra;          */
/*  foregroundHwnd = la finestra che aveva il primo piano prima del    */
/*  clic (0 = scoperta interna); styleMode: 1 = menu classico Win7,    */
/*  2/3 = targhetta Win8.1. GetActive ritorna la sigla della lingua    */
/*  attiva per il testo nella tray; il callback avvisa dei cambi.      */
/* ------------------------------------------------------------------ */
W7T_API void W7T_CALL W7T_LangSwitcherShow(uint64_t ownerHwnd,
        uint64_t foregroundHwnd, int32_t styleMode);
W7T_API void W7T_CALL W7T_LangSwitcherHide(void);
W7T_API void W7T_CALL W7T_LangSwitcherGetActive(uint32_t* langId,
        wchar_t* threeLetter, int32_t threeCap,
        wchar_t* twoLetter, int32_t twoCap);
typedef void (__stdcall* W7T_LangChangedCallback)(uint32_t langId);
W7T_API void W7T_CALL W7T_LangSwitcherSetChangedCallback(
        W7T_LangChangedCallback callback);

/* ------------------------------------------------------------------ */
/*  v2.38: flyout batteria ricreato (stile Windows 7).                */
/* ------------------------------------------------------------------ */
W7T_API void W7T_CALL W7T_BatteryFlyoutShowAt(int32_t left, int32_t top,
        int32_t right, int32_t bottom);
W7T_API void W7T_CALL W7T_BatteryFlyoutHide(void);
W7T_API void W7T_CALL W7T_BatteryFlyoutSetLanguage(int32_t lang);

/* ------------------------------------------------------------------ */
/*  Lingua (v2.59)                                                    */
/*                                                                    */
/*  Una sola sorgente per le lingue supportate: l'elenco qui sotto    */
/*  e' quello che il selettore della finestra Proprieta' scorre e     */
/*  che il managed usa per parlare al nativo. Indici stabili: le      */
/*  lingue nuove si aggiungono IN CODA.                               */
/*                                                                    */
/*  0=it 1=en 2=es 3=fr 4=de 5=pt 6=pl 7=ru 8=ja 9=zh 10=ar          */
/*                                                                    */
/*  Codice fuori elenco -> INGLESE (mai italiano per omissione).      */
/* ------------------------------------------------------------------ */

/* Cambia la lingua di TUTTO il nativo (stringhe, Proprieta', ricerca,
 * menu, flyout batteria e rete). Codice a due lettere ("it", "ar"); un
 * codice sconosciuto vale "en". */
W7T_API void W7T_CALL W7T_SetLanguage(const char* twoLetterCode);

/* Elenco delle lingue: numero di lingue, codice a due lettere e nome
 * nativo (il nome mostrato nel selettore, nella lingua stessa). */
W7T_API int32_t    W7T_CALL W7T_GetLanguageCount(void);
W7T_API const char* W7T_CALL W7T_GetLanguageCode(int32_t index);
W7T_API const wchar_t* W7T_CALL W7T_GetLanguageName(int32_t index);

/* Indice della lingua attiva nel core e indice della lingua
 * dell'interfaccia di Windows (gia' filtrata sulle lingue supportate). */
W7T_API int32_t W7T_CALL W7T_GetLanguageIndex(void);
W7T_API int32_t W7T_CALL W7T_DetectSystemLanguageIndex(void);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WIN7TASKBAR_CORE_H */
