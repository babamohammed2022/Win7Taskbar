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
#include "RaiiWrappers.h"
#include "ScopeGuards.h"
#include <vector>
#include <string>

namespace w7t {

namespace {

/* ID sintetici per il menu di gruppo: fuori dall'intervallo SC_*. */
constexpr UINT kGroupMinimizeId = 0xF100;
constexpr UINT kGroupCloseId    = 0xF101;
constexpr UINT_PTR kMenuPriorityTimer = 0x574D;

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

/* ------------------------------------------------------------------ */
/*  Native menu icons, for the program-icon (Superbar) menus ONLY.       */
/*                                                                     */
/*  Rules:                                                             */
/*   - ONLY icons drawn or supplied by Windows itself: the documented    */
/*     HBMMENU_POPUP_* glyph handles for Restore/Minimize/Maximize/     */
/*     Close (the menu code draws them themed, like the caption         */
/*     buttons - no bitmap is invented here). The pin-menu rows stay    */
/*     text-only: no app icon next to the launch entry.                 */
/*   - Everything icon-related is best effort and exception-guarded:     */
/*     any failure degrades to the plain menu, which keeps working       */
/*     exactly as before.                                                */
/*   - The tray/bar/clock/generic menus never pass through here.         */
/* ------------------------------------------------------------------ */

/* The four caption glyphs Windows itself draws in menus. Shared system
 * handles: never destroyed, never modified. Move/Size have no native
 * menu glyph, so they intentionally stay icon-less. */
HBITMAP SystemGlyphForCommand(UINT commandId) noexcept {
    switch (commandId) {
    case SC_RESTORE:  return HBMMENU_POPUP_RESTORE;
    case SC_MINIMIZE: return HBMMENU_POPUP_MINIMIZE;
    case SC_MAXIMIZE: return HBMMENU_POPUP_MAXIMIZE;
    case SC_CLOSE:    return HBMMENU_POPUP_CLOSE;
    default:          return nullptr;
    }
}

/* Attaches a bitmap to one menu item, looked up by command id. Best
 * effort: failures are swallowed on purpose, the menu works with or
 * without the icon. */
void SetItemBitmapByCommand(HMENU menu, UINT commandId,
                            HBITMAP bitmap) noexcept {
    if (menu == nullptr || bitmap == nullptr) {
        return;
    }
    try {
        MENUITEMINFOW info = {};
        info.cbSize   = sizeof(info);
        info.fMask    = MIIM_BITMAP;
        info.hbmpItem = bitmap;
        SetMenuItemInfoW(menu, commandId, FALSE, &info);
    } catch (...) {
        /* icon unavailable: the row stays text-only */
    }
}

/* Walks a window system menu (cloned or rebuilt - both use SC_* ids) and
 * attaches the native glyph where Windows has one. Custom application
 * rows keep whatever id they came with and are never touched. */
void ApplySystemMenuGlyphs(HMENU menu) noexcept {
    if (menu == nullptr) {
        return;
    }
    try {
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
            HBITMAP glyph = SystemGlyphForCommand(info.wID);
            if (glyph != nullptr) {
                MENUITEMINFOW set = {};
                set.cbSize   = sizeof(set);
                set.fMask    = MIIM_BITMAP;
                set.hbmpItem = glyph;
                SetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &set);
            }
        }
    } catch (...) {
        /* icon pass failed: the menu below is still the correct one */
    }
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

    /* Native caption glyphs (Restore/Minimize/Maximize/Close) drawn by
     * Windows itself. Best effort: without them this is still the exact
     * menu built above. */
    ApplySystemMenuGlyphs(popup);

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

    /* Native minimize/close glyphs drawn by Windows itself. Best effort. */
    SetItemBitmapByCommand(popup, kGroupMinimizeId, HBMMENU_POPUP_MINIMIZE);
    SetItemBitmapByCommand(popup, kGroupCloseId, HBMMENU_POPUP_CLOSE);

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

int32_t ShellMenu::ShowPinMenu(int32_t x, int32_t y, bool bottomEdge,
                               const wchar_t* launchText,
                               const wchar_t* pinText,
                               const wchar_t* lnkPath,
                               const wchar_t* targetPath) {
    y = AnchorYToTaskbarTop(x, y, bottomEdge);
    HWND menuOwner = GetMenuOwnerWindow();
    if (menuOwner == nullptr) {
        return 0;
    }

    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) {
        return 0;
    }

    /* Same two rows, same ids (1/2) the generic menu produced for the pin:
     * the managed switch on the result does not change. */
    AppendMenuW(popup, MF_STRING, 1,
                launchText != nullptr ? launchText : L"");
    AppendMenuW(popup, MF_STRING, 2,
                pinText != nullptr ? pinText : L"");

    /* Text-only rows: Windows 7 draws no icon next to the launch entry,
     * and exposing one was a regression. lnkPath/targetPath stay in the
     * signature (the managed caller keeps passing them) but are unused. */
    (void)lnkPath;
    (void)targetPath;

    int32_t chosen = 0;
    {
        ForegroundMenuScope scope(menuOwner);
        chosen = static_cast<int32_t>(TrackPopupMenuEx(
            popup, CommonFlags(bottomEdge), x, y, menuOwner, nullptr));
    }

    DestroyMenu(popup);
    return chosen;
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
