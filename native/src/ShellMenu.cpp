/*
 * Win7Taskbar - Menu contestuali Win32 nativi
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
 */

#include "ShellMenu.h"
#include "Strings.h"
#include <vector>
#include <string>

namespace w7t {

namespace {

/* ID sintetici per il menu di gruppo: fuori dall'intervallo SC_*. */
constexpr UINT kGroupMinimizeId = 0xF100;
constexpr UINT kGroupCloseId    = 0xF101;
constexpr UINT_PTR kMenuPriorityTimer = 0x574D;

/* ========================================================================
 * v4.6: ICONE PER I MENU CONTESTUALI DI SISTEMA
 *
 * Disegna le icone del menu di sistema (Ripristina, Riduci a icona,
 * Ingrandisci, Chiudi) con GDI puro, senza dipendenze esterne.
 * Ispirato a ExplorerPatcher e alle mod Windhawk per i menu di Windows 7.
 *
 * Le bitmap sono create una volta (statiche) e riutilizzate per tutti i
 * menu. SetMenuItemBitmaps richiede due bitmap: una per lo stato normale
 * e una per lo stato selezionato (highlight). Per semplicita', usiamo la
 * stessa bitmap per entrambi gli stati (le icone di Windows 7 non cambiano
 * colore al hover).
 *
 * Dimensioni: 16x16 pixel, formato DIB a 32 bit con canale alpha.
 * ======================================================================== */
namespace MenuIcons {

/* Le 4 icone del menu di sistema. Sposta e Ridimensiona non hanno icona
 * (come in Windows 7 originale). */
static HBITMAP s_restore  = nullptr;
static HBITMAP s_minimize = nullptr;
static HBITMAP s_maximize = nullptr;
static HBITMAP s_close    = nullptr;
static bool    s_initialized = false;

/* Crea una DIB section 16x16 a 32 bit con canale alpha. Ritorna l'HBITMAP
 * e un puntatore ai pixel (per il disegno diretto se necessario). */
HBITMAP CreateAlphaBitmap32(void** pixels = nullptr) {
    BITMAPINFOHEADER bih = {};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = 16;
    bih.biHeight      = -16;   /* top-down */
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dcScreen = GetDC(nullptr);
    HBITMAP hbm = CreateDIBSection(dcScreen, reinterpret_cast<BITMAPINFO*>(&bih),
                                    DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dcScreen);
    if (pixels) *pixels = bits;

    /* Inizializza a trasparente (alpha=0). */
    if (bits) {
        DWORD* p = static_cast<DWORD*>(bits);
        for (int i = 0; i < 16 * 16; ++i) {
            p[i] = 0x00000000;   /* BGRA: alpha=0 */
        }
    }
    return hbm;
}

/* Disegna un pixel con alpha blending su una DIB a 32 bit.
 * Color e' RGB (0x00BBGGRR), alpha e' 0-255. */
void SetPixelAlpha(DWORD* pixels, int x, int y, DWORD color, BYTE alpha) {
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return;
    DWORD& pixel = pixels[y * 16 + x];

    /* Premultiplied alpha: il colore viene moltiplicato per l'alpha.
     * Per semplicita', usiamo alpha diretto (non premultiplied) perche'
     * SetMenuItemBitmaps gestisce il blending. */
    BYTE r = (color >> 16) & 0xFF;
    BYTE g = (color >>  8) & 0xFF;
    BYTE b = (color >>  0) & 0xFF;
    pixel = (static_cast<DWORD>(alpha) << 24) |
            (static_cast<DWORD>(r) << 16) |
            (static_cast<DWORD>(g) <<  8) |
            (static_cast<DWORD>(b) <<  0);
}

/* Disegna una linea orizzontale con spessore specificato. */
void DrawHLine(DWORD* pixels, int x1, int x2, int y, DWORD color, BYTE alpha, int thickness = 1) {
    for (int t = 0; t < thickness; ++t) {
        for (int x = x1; x <= x2; ++x) {
            SetPixelAlpha(pixels, x, y + t, color, alpha);
        }
    }
}

/* Disegna una linea verticale con spessore specificato. */
void DrawVLine(DWORD* pixels, int x, int y1, int y2, DWORD color, BYTE alpha, int thickness = 1) {
    for (int t = 0; t < thickness; ++t) {
        for (int y = y1; y <= y2; ++y) {
            SetPixelAlpha(pixels, x + t, y, color, alpha);
        }
    }
}

/* Disegna un rettangolo vuoto (bordo) con spessore specificato. */
void DrawRect(DWORD* pixels, int x1, int y1, int x2, int y2, DWORD color, BYTE alpha, int thickness = 1) {
    DrawHLine(pixels, x1, x2, y1, color, alpha, thickness);
    DrawHLine(pixels, x1, x2, y2 - thickness + 1, color, alpha, thickness);
    DrawVLine(pixels, x1, y1, y2, color, alpha, thickness);
    DrawVLine(pixels, x2 - thickness + 1, y1, y2, color, alpha, thickness);
}

/* Inizializza le 4 icone. Chiamata una volta sola (lazy init). */
void Initialize() {
    if (s_initialized) return;
    s_initialized = true;

    /* Colore: grigio scuro come le icone di Windows 7 (0x404040).
     * Le icone Win7 sono piu' scure e bold rispetto a quelle classiche. */
    const DWORD kColor = 0x00404040;   /* RGB: 0x40, 0x40, 0x40 */
    const BYTE  kAlpha = 0xE0;         /* ~88% opaco */

    /* --- Ripristina (Restore): due quadrati sovrapposti con bordo spesso ---
     * Stile Windows 7: bordi spessi 2px, quadrati ben definiti.
     * Quadrato grande: (2,6) a (11,14) - bordo 2px
     * Quadrato piccolo: (5,2) a (13,10) - bordo 2px, in alto a destra */
    {
        void* pixels = nullptr;
        s_restore = CreateAlphaBitmap32(&pixels);
        if (pixels) {
            DWORD* p = static_cast<DWORD*>(pixels);
            DrawRect(p, 2, 6, 11, 14, kColor, kAlpha, 2);
            DrawRect(p, 5, 2, 13, 10, kColor, kAlpha, 2);
        }
    }

    /* --- Riduci a icona (Minimize): linea orizzontale spessa ---
     * Stile Windows 7: barra spessa 3px, centrata verticalmente in basso.
     * Linea: da (3,11) a (12,13) - spessore 3px */
    {
        void* pixels = nullptr;
        s_minimize = CreateAlphaBitmap32(&pixels);
        if (pixels) {
            DWORD* p = static_cast<DWORD*>(pixels);
            for (int y = 11; y <= 13; ++y) {
                DrawHLine(p, 3, 12, y, kColor, kAlpha);
            }
        }
    }

    /* --- Ingrandisci (Maximize): quadrato con bordo spesso ---
     * Stile Windows 7: bordo spesso 2px, riempie buona parte dell'area.
     * Rettangolo: (3,3) a (12,12) - bordo 2px */
    {
        void* pixels = nullptr;
        s_maximize = CreateAlphaBitmap32(&pixels);
        if (pixels) {
            DWORD* p = static_cast<DWORD*>(pixels);
            DrawRect(p, 3, 3, 12, 12, kColor, kAlpha, 2);
        }
    }

    /* --- Chiudi (Close): X con linee spesse ---
     * Stile Windows 7: linee spesse 2px, diagonali ben visibili.
     * Due diagonali con spessore: (3,3)-(12,12) e (12,3)-(3,12) */
    {
        void* pixels = nullptr;
        s_close = CreateAlphaBitmap32(&pixels);
        if (pixels) {
            DWORD* p = static_cast<DWORD*>(pixels);
            for (int i = 0; i < 10; ++i) {
                /* Diagonale principale: spessore 2px (pixel + pixel adiacente) */
                SetPixelAlpha(p, 3 + i, 3 + i, kColor, kAlpha);
                SetPixelAlpha(p, 4 + i, 3 + i, kColor, kAlpha);
                /* Anti-diagonale: spessore 2px */
                SetPixelAlpha(p, 12 - i, 3 + i, kColor, kAlpha);
                SetPixelAlpha(p, 12 - i, 4 + i, kColor, kAlpha);
            }
        }
    }
}

/* Applica le icone al menu per le voci SC_* corrispondenti.
 * Chiamata dopo aver costruito il menu (sia fallback che copiato). */
void ApplyToMenu(HMENU menu) {
    Initialize();

    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask  = MIIM_ID | MIIM_FTYPE;
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info)) {
            continue;
        }
        if ((info.fType & MFT_SEPARATOR) != 0) {
            continue;
        }

        HBITMAP icon = nullptr;
        switch (info.wID) {
            case SC_RESTORE:   icon = s_restore;  break;
            case SC_MINIMIZE:  icon = s_minimize; break;
            case SC_MAXIMIZE:  icon = s_maximize; break;
            case SC_CLOSE:     icon = s_close;    break;
            default:           continue;
        }

        if (icon != nullptr) {
            /* SetMenuItemBitmaps: la stessa bitmap per normale e highlight.
             * Le bitmap sono statiche e non vengono distrutte (riutilizzo). */
            SetMenuItemBitmaps(menu, static_cast<UINT>(i), MF_BYPOSITION,
                               icon, icon);
        }
    }
}

} /* namespace MenuIcons */

/* Keep the real #32768 menu at the front for the entire modal tracking
 * loop. The WPF AppBar guard periodically reasserts the taskbar's own
 * topmost position; a one-shot CBT promotion can therefore be undone. */
void CALLBACK MenuPriorityTimerProc(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    if (id != kMenuPriorityTimer || hwnd == nullptr || !IsWindow(hwnd)) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                 SWP_NOOWNERZORDER);
}

/*
 * Historical TrackPopupMenu quirk: if the owner window is not in the
 * foreground, the menu stays open even after a click outside. Microsoft
 * documents SetForegroundWindow before and PostMessage(WM_NULL) after.
 *
 * v1.7.6 - THE MENU OUTRANKS THE TASKBAR. The popup menu window is
 * placed directly above its owner window in the z-order, INSIDE the owner's
 * band: with a non-topmost owner the menu sits in the normal band while
 * the taskbar (WS_EX_TOPMOST, always-on-top by design) paints over it -
 * the menu looked "cut off" by the bar and clicks on covered items hit the
 * buttons instead. Context menus therefore get PRIORITY over the taskbar:
 * the owner is created topmost (see GetMenuOwnerWindow) and is pushed to
 * the front of the topmost band right before every TrackPopupMenuEx,
 * exactly like the tray drag-ghost does against the overflow panel. The
 * owner window is a 0x0, never-visible popup, so its topmost flag has no
 * other effect; the menu dies with the tracking call and never outlives
 * the scope.
 */
LRESULT CALLBACK MenuPriorityCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_CREATEWND || code == HCBT_ACTIVATE) {
        HWND hwnd = reinterpret_cast<HWND>(wParam);
        wchar_t className[32] = {};
        if (hwnd != nullptr &&
            GetClassNameW(hwnd, className, static_cast<int>(std::size(className))) > 0 &&
            lstrcmpW(className, L"#32768") == 0) {
            /* A tracked menu is its own #32768 window. Put that actual
             * window, not only its invisible owner, at the front of the
             * topmost band. This outranks the WS_EX_TOPMOST taskbar even
             * when its AppBar guard reasserts the taskbar during tracking. */
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                         SWP_NOOWNERZORDER);
            /* TrackPopupMenu runs a modal message loop, so this timer keeps
             * firing even while the menu is open. It disappears with the
             * menu HWND and cannot outlive the tracking call. */
            SetTimer(hwnd, kMenuPriorityTimer, 15, MenuPriorityTimerProc);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

class ForegroundMenuScope {
public:
    explicit ForegroundMenuScope(HWND owner) : m_owner(owner) {
        /* Track the menu window creation on this thread. Merely making the
         * hidden owner topmost is insufficient: Windows can create #32768
         * below a taskbar which has just reasserted its own z priority. */
        m_cbtHook = SetWindowsHookExW(WH_CBT, MenuPriorityCbtProc, nullptr,
                                      GetCurrentThreadId());
        SetForegroundWindow(m_owner);
        SetWindowPos(m_owner, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
    }

    ~ForegroundMenuScope() {
        if (m_cbtHook != nullptr) {
            UnhookWindowsHookEx(m_cbtHook);
        }
        PostMessageW(m_owner, WM_NULL, 0, 0);
    }

private:
    HWND m_owner;
    HHOOK m_cbtHook = nullptr;
};

/*
 * Finestra nascosta di appoggio: TrackPopupMenuEx vuole una finestra del
 * NOSTRO thread come proprietaria. Non possiamo passare la finestra
 * bersaglio, che appartiene a un altro processo.
 */
HWND GetMenuOwnerWindow() {
    static HWND s_owner = nullptr;
    if (s_owner != nullptr && IsWindow(s_owner)) {
        return s_owner;
    }

    static ATOM s_atom = 0;
    if (s_atom == 0) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Win7TaskbarMenuOwner";
        s_atom = RegisterClassExW(&wc);
        if (s_atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return nullptr;
        }
    }

    /* v1.7.6: WS_EX_TOPMOST carries the menu into the topmost band, so a
     * context menu always opens ABOVE the taskbar window itself (see
     * ForegroundMenuScope for the full story). */
    s_owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                              L"Win7TaskbarMenuOwner", L"",
                              WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return s_owner;
}

/* Ricostruisce il menu di finestra standard di Windows 7 quando quello vero
 * non e' raggiungibile. Le regole di abilitazione sono quelle di Explorer:
 *   Ripristina      attivo se la finestra e' massimizzata o ridotta a icona
 *   Sposta          solo a finestra normale (non ridotta a icona)
 *   Ridimensiona    solo a finestra normale e se ha WS_THICKFRAME
 *   Riduci a icona  se ha WS_MINIMIZEBOX e non e' gia' ridotta
 *   Ingrandisci     se ha WS_MAXIMIZEBOX e non e' gia' massimizzata
 *   Chiudi          sempre
 * Gli identificatori sono le costanti SC_* di sistema, cosi' la finestra
 * bersaglio esegue il comando esattamente come dal proprio frame. */
void BuildFallbackWindowMenu(HMENU popup, HWND target) {
    const LONG_PTR style = GetWindowLongPtrW(target, GWL_STYLE);

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);

    bool minimized = IsIconic(target) != FALSE;
    bool maximized = IsZoomed(target) != FALSE;
    if (GetWindowPlacement(target, &placement)) {
        minimized = placement.showCmd == SW_SHOWMINIMIZED;
        maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    }

    const bool normal = !minimized && !maximized;

    auto append = [popup](UINT id, const wchar_t* text, bool enabled, bool isDefault) {
        UINT flags = MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED);
        if (isDefault) {
            flags |= MF_DEFAULT;
        }
        AppendMenuW(popup, flags, id, text);
    };

    /* v2.59: i testi arrivano dalla tabella unica delle stringhe, nella
     * lingua scelta dall'utente (prima erano italiani fissi: su un sistema
     * spagnolo il ripiego appariva in italiano).
     * La voce predefinita e' quella che Windows esegue col doppio clic. */
    append(SC_RESTORE,  S(StrId::SysRestore),  minimized || maximized, minimized || maximized);
    append(SC_MOVE,     S(StrId::SysMove),     !minimized,             false);
    append(SC_SIZE,     S(StrId::SysSize),     normal && (style & WS_THICKFRAME) != 0, false);
    append(SC_MINIMIZE, S(StrId::SysMinimize),
           (style & WS_MINIMIZEBOX) != 0 && !minimized, false);
    append(SC_MAXIMIZE, S(StrId::SysMaximize),
           (style & WS_MAXIMIZEBOX) != 0 && !maximized, false);

    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    append(SC_CLOSE, S(StrId::SysClose), true, !(minimized || maximized));
}

UINT CommonFlags(bool bottomEdge) {
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN;
    /* Barra in basso: il menu deve crescere verso l'alto. */
    flags |= bottomEdge ? TPM_BOTTOMALIGN : TPM_TOPALIGN;
    return flags;
}

/* v2.42: i menu ancorati alla barra devono aprirsi SOPRA i pulsanti,
 * mai fluttuanti a meta' schermo: per l'ancoraggio verticale si usa il
 * bordo superiore dell'area di lavoro del monitor che contiene il
 * pulsante (coincide con la taskbar a fondo schermo), qualunque sia lo
 * spazio coordinate da cui arriva y. La x resta quella del pulsante.
 *
 * v2.43: la barra (e l'orologio) NON passano piu' di qui: quei due menu
 * chiedono anchorAtCursor e si aprono sul punto esatto del cursore, come
 * un menu contestuale normale. Questa funzione resta per i menu delle APP
 * (finestra di sistema, gruppo, pin), che devono continuare ad aprirsi
 * sopra il pulsante della Superbar qualunque sia la y ricevuta. */
int32_t AnchorYToTaskbarTop(int32_t x, int32_t y, bool bottomEdge) {
    if (!bottomEdge) return y;
    POINT pt{ x, y };
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (hm == nullptr) return y;
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(hm, &mi)) return y;
    return mi.rcWork.bottom;
}

} /* namespace */

int32_t ShellMenu::ShowWindowSystemMenu(HWND ownerHwnd, int32_t x, int32_t y,
                                        bool bottomEdge) {
    y = AnchorYToTaskbarTop(x, y, bottomEdge);   /* v2.42 */
    if (ownerHwnd == nullptr || !IsWindow(ownerHwnd)) {
        return W7T_ERR_NOT_FOUND;
    }

    HWND menuOwner = GetMenuOwnerWindow();
    if (menuOwner == nullptr) {
        return W7T_ERR_CREATE_WINDOW;
    }

    /* false = restituisce il menu in uso; lo cloniamo per non alterarlo. */
    HMENU systemMenu = GetSystemMenu(ownerHwnd, FALSE);

    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) {
        return W7T_ERR_APPBAR;
    }

    /* GetSystemMenu puo' fallire quando la finestra appartiene a un altro
     * processo con integrita' piu' alta (app elevate) oppure quando la
     * finestra non ha un menu di sistema proprio. In quel caso Windows 7
     * mostra comunque il menu: lo ricostruiamo con le voci standard e lo
     * stato corretto, invece di non mostrare nulla. */
    if (systemMenu == nullptr) {
        BuildFallbackWindowMenu(popup, ownerHwnd);
    } else {
        /* Copia voce per voce: cosi' rispettiamo esattamente cio' che
         * l'applicazione espone, comprese le voci personalizzate. */
        const int count = GetMenuItemCount(systemMenu);
        for (int i = 0; i < count; ++i) {
            wchar_t text[256] = {};
            MENUITEMINFOW info = {};
            info.cbSize     = sizeof(info);
            info.fMask      = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_STRING;
            info.dwTypeData = text;
            info.cch        = static_cast<UINT>(std::size(text));

            if (!GetMenuItemInfoW(systemMenu, static_cast<UINT>(i), TRUE, &info)) {
                continue;
            }

            if ((info.fType & MFT_SEPARATOR) != 0) {
                AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
                continue;
            }

            UINT flags = MF_STRING;
            if ((info.fState & MFS_DISABLED) != 0 ||
                (info.fState & MFS_GRAYED) != 0) {
                flags |= MF_GRAYED;
            }
            if ((info.fState & MFS_CHECKED) != 0) {
                flags |= MF_CHECKED;
            }
            if ((info.fState & MFS_DEFAULT) != 0) {
                flags |= MF_DEFAULT;
            }

            /* info.cch viene azzerato per le voci senza testo. */
            info.dwTypeData = text;
            AppendMenuW(popup, flags, info.wID, text);
        }
    }

    /* v1.7.1: a copied system menu can come back EMPTY or with every item
     * disabled (packaged/UWP frame windows such as Snipping Tool on
     * Windows 11 24H2 expose a stub menu). Windows 7 always shows
     * something there: rebuild the standard menu instead of showing
     * nothing, so Close/Minimize are always available. */
    bool usable = false;
    const int copiedCount = GetMenuItemCount(popup);
    for (int i = 0; i < copiedCount && !usable; ++i) {
        MENUITEMINFOW state = {};
        state.cbSize = sizeof(state);
        state.fMask  = MIIM_STATE | MIIM_FTYPE;
        if (GetMenuItemInfoW(popup, static_cast<UINT>(i), TRUE, &state) &&
            (state.fType & MFT_SEPARATOR) == 0 &&
            (state.fState & (MFS_DISABLED | MFS_GRAYED)) == 0) {
            usable = true;
        }
    }
    if (!usable) {
        DestroyMenu(popup);
        popup = CreatePopupMenu();
        if (popup == nullptr) {
            return W7T_ERR_APPBAR;
        }
        BuildFallbackWindowMenu(popup, ownerHwnd);
    }

    /* v4.6: icone per le voci del menu di sistema (Ripristina, Riduci,
     * Ingrandisci, Chiudi). Ispirato a ExplorerPatcher e mod Windhawk. */
    MenuIcons::ApplyToMenu(popup);

    int32_t chosen = 0;
    {
        ForegroundMenuScope scope(menuOwner);
        chosen = static_cast<int32_t>(TrackPopupMenuEx(
            popup, CommonFlags(bottomEdge), x, y, menuOwner, nullptr));
    }

    DestroyMenu(popup);

    if (chosen == 0) {
        return W7T_OK; /* annullato: non e' un errore */
    }

    /* Il comando va alla finestra bersaglio, che lo esegue come se il menu
     * fosse stato aperto dal suo stesso frame. */
    PostMessageW(ownerHwnd, WM_SYSCOMMAND, static_cast<WPARAM>(chosen),
                 MAKELPARAM(x, y));
    return W7T_OK;
}

int32_t ShellMenu::ShowGroupMenu(HWND ownerHwnd, int32_t x, int32_t y,
                                 bool bottomEdge,
                                 const wchar_t* minimizeText,
                                 const wchar_t* closeText) {
    y = AnchorYToTaskbarTop(x, y, bottomEdge);   /* v2.42 */
    HWND menuOwner = GetMenuOwnerWindow();
    if (menuOwner == nullptr) {
        return W7T_ERR_CREATE_WINDOW;
    }

    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) {
        return W7T_ERR_APPBAR;
    }

    /* v2.59: il managed manda le sue stringhe (gia' nella lingua scelta);
     * se non le manda - o non le ha - il testo viene dalla tabella unica,
     * che conosce tutte e 11 le lingue: mai italiano per omissione. */
    AppendMenuW(popup, MF_STRING, kGroupMinimizeId,
                minimizeText != nullptr ? minimizeText : S(StrId::GroupMinimize));
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, kGroupCloseId,
                closeText != nullptr ? closeText : S(StrId::GroupClose));

    /* v4.6: icone anche per il menu di gruppo (Riduci a icona gruppo,
     * Chiudi gruppo). Riutilizza le stesse icone del menu di sistema. */
    {
        MenuIcons::Initialize();
        /* kGroupMinimizeId usa l'icona minimize, kGroupCloseId usa close.
         * Cerchiamo per ID (MF_BYCOMMAND) invece che per posizione. */
        if (MenuIcons::s_minimize) {
            SetMenuItemBitmaps(popup, kGroupMinimizeId, MF_BYCOMMAND,
                               MenuIcons::s_minimize, MenuIcons::s_minimize);
        }
        if (MenuIcons::s_close) {
            SetMenuItemBitmaps(popup, kGroupCloseId, MF_BYCOMMAND,
                               MenuIcons::s_close, MenuIcons::s_close);
        }
    }

    int32_t chosen = 0;
    {
        ForegroundMenuScope scope(menuOwner);
        chosen = static_cast<int32_t>(TrackPopupMenuEx(
            popup, CommonFlags(bottomEdge), x, y, menuOwner, nullptr));
    }

    DestroyMenu(popup);
    (void)ownerHwnd;

    if (chosen == static_cast<int32_t>(kGroupMinimizeId)) {
        return 1;
    }
    if (chosen == static_cast<int32_t>(kGroupCloseId)) {
        return 2;
    }
    return 0;
}

int32_t ShellMenu::ShowContextMenu(int32_t x, int32_t y, bool bottomEdge,
                                   const wchar_t* itemsSeparatedByNewline) {
    y = AnchorYToTaskbarTop(x, y, bottomEdge);   /* v2.42 */
    if (itemsSeparatedByNewline == nullptr) {
        return 0;
    }

    HWND menuOwner = GetMenuOwnerWindow();
    if (menuOwner == nullptr) {
        return 0;
    }

    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) {
        return 0;
    }

    /* Gli identificatori partono da 1: TrackPopupMenuEx restituisce 0
     * quando l'utente annulla, quindi lo zero non e' utilizzabile. */
    UINT nextId = 1;

    std::wstring all(itemsSeparatedByNewline);
    size_t start = 0;

    while (start <= all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = all.size();
        }

        std::wstring item = all.substr(start, end - start);
        start = end + 1;

        if (item == L"-") {
            AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
            continue;
        }

        if (item.empty()) {
            if (end >= all.size()) {
                break;
            }
            continue;
        }

        UINT flags = MF_STRING;
        if (item[0] == L'!') {
            flags |= MF_GRAYED;
            item.erase(0, 1);
        }

        AppendMenuW(popup, flags, nextId++, item.c_str());
    }

    int32_t chosen = 0;
    {
        ForegroundMenuScope scope(menuOwner);
        chosen = static_cast<int32_t>(TrackPopupMenuEx(
            popup, CommonFlags(bottomEdge), x, y, menuOwner, nullptr));
    }

    DestroyMenu(popup);
    return chosen;
}

int32_t ShellMenu::ShowContextMenuEx(int32_t x, int32_t y, bool bottomEdge,
                                     const wchar_t* itemsSeparatedByNewline,
                                     bool anchorAtCursor) {
    /* v2.43: con anchorAtCursor il punto ricevuto e' il cursore e va
     * usato COSI' COM'E' (menu della barra/orologio, che devono aprirsi
     * dove si trova il puntatore); per i menu delle app resta l'ancoraggio
     * al bordo superiore dell'area di lavoro come in v2.42. */
    if (!anchorAtCursor) {
        y = AnchorYToTaskbarTop(x, y, bottomEdge);
    }
    if (itemsSeparatedByNewline == nullptr) {
        return 0;
    }

    HWND menuOwner = GetMenuOwnerWindow();
    if (menuOwner == nullptr) {
        return 0;
    }

    HMENU root = CreatePopupMenu();
    if (root == nullptr) {
        return 0;
    }

    /* Pila dei menu aperti: in cima c'e' sempre quello in cui si inserisce
     * la prossima voce. root resta sempre in fondo alla pila: una "<" di
     * troppo nel testo non deve mai farla sparire, altrimenti le voci
     * successive finirebbero perse nel vuoto invece che nel menu giusto. */
    std::vector<HMENU> stack;
    stack.push_back(root);

    UINT nextId = 1;
    std::wstring all(itemsSeparatedByNewline);
    size_t start = 0;

    while (start <= all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = all.size();
        }

        std::wstring item = all.substr(start, end - start);
        start = end + 1;

        if (item.empty()) {
            if (end >= all.size()) {
                break;
            }
            continue;
        }

        HMENU current = stack.back();

        if (item == L"<") {
            if (stack.size() > 1) {
                stack.pop_back();
            }
            continue;
        }

        if (item == L"-") {
            AppendMenuW(current, MF_SEPARATOR, 0, nullptr);
            continue;
        }

        if (item[0] == L'>') {
            std::wstring label = item.substr(1);
            HMENU sub = CreatePopupMenu();
            if (sub != nullptr) {
                AppendMenuW(current, MF_STRING | MF_POPUP,
                            reinterpret_cast<UINT_PTR>(sub), label.c_str());
                stack.push_back(sub);
            }
            continue;
        }

        UINT flags = MF_STRING;
        /* '!' e '*' possono comparire in qualsiasi ordine prima del testo. */
        while (!item.empty() && (item[0] == L'!' || item[0] == L'*')) {
            if (item[0] == L'!') {
                flags |= MF_GRAYED;
            } else {
                flags |= MF_CHECKED;
            }
            item.erase(0, 1);
        }

        AppendMenuW(current, flags, nextId++, item.c_str());
    }

    int32_t chosen = 0;
    {
        ForegroundMenuScope scope(menuOwner);
        chosen = static_cast<int32_t>(TrackPopupMenuEx(
            root, CommonFlags(bottomEdge), x, y, menuOwner, nullptr));
    }

    /* DestroyMenu distrugge ricorsivamente anche i sottomenu agganciati
     * con MF_POPUP: non serve distruggerli uno per uno. */
    DestroyMenu(root);
    return chosen;
}

} /* namespace w7t */
