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

#include "ShellMenu.h"\n#include <vector>
#include <string>

namespace w7t {

namespace {

/* ID sintetici per il menu di gruppo: fuori dall'intervallo SC_*. */
constexpr UINT kGroupMinimizeId = 0xF100;
constexpr UINT kGroupCloseId    = 0xF101;

/*
 * TrackPopupMenu ha una stranezza storica: se la finestra proprietaria non
 * e' in primo piano, il menu resta aperto anche dopo un click fuori. La
 * soluzione documentata da Microsoft e' SetForegroundWindow prima e un
 * PostMessage(WM_NULL) dopo.
 */
class ForegroundMenuScope {
public:
    explicit ForegroundMenuScope(HWND owner) : m_owner(owner) {
        SetForegroundWindow(m_owner);
    }

    ~ForegroundMenuScope() {
        PostMessageW(m_owner, WM_NULL, 0, 0);
    }

private:
    HWND m_owner;
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

    s_owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"Win7TaskbarMenuOwner", L"",
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

    /* La voce predefinita e' quella che Windows esegue col doppio clic. */
    append(SC_RESTORE,  L"&Ripristina",   minimized || maximized, minimized || maximized);
    append(SC_MOVE,     L"&Sposta",       !minimized,             false);
    append(SC_SIZE,     L"&Ridimensiona", normal && (style & WS_THICKFRAME) != 0, false);
    append(SC_MINIMIZE, L"R&iduci a icona",
           (style & WS_MINIMIZEBOX) != 0 && !minimized, false);
    append(SC_MAXIMIZE, L"I&ngrandisci",
           (style & WS_MAXIMIZEBOX) != 0 && !maximized, false);

    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    append(SC_CLOSE, L"&Chiudi", true, !(minimized || maximized));
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
     * processo con integrita' piu' alta (app elevate, UWP) oppure quando la
     * finestra non ha un menu di sistema proprio. In quel caso Windows 7
     * mostra comunque il menu: lo ricostruiamo con le voci standard e lo
     * stato corretto, invece di non mostrare nulla. */
    if (systemMenu == nullptr) {
        BuildFallbackWindowMenu(popup, ownerHwnd);

        if (GetMenuItemCount(popup) == 0) {
            DestroyMenu(popup);
            return W7T_ERR_NOT_FOUND;
        }

        int32_t picked = 0;
        {
            ForegroundMenuScope scope(menuOwner);
            picked = static_cast<int32_t>(TrackPopupMenuEx(
                popup, CommonFlags(bottomEdge), x, y, menuOwner, nullptr));
        }

        DestroyMenu(popup);

        if (picked != 0) {
            PostMessageW(ownerHwnd, WM_SYSCOMMAND, static_cast<WPARAM>(picked),
                         MAKELPARAM(x, y));
        }
        return W7T_OK;
    }

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
        if ((info.fState & MFS_DISABLED) != 0 || (info.fState & MFS_GRAYED) != 0) {
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

    if (GetMenuItemCount(popup) == 0) {
        DestroyMenu(popup);
        return W7T_ERR_NOT_FOUND;
    }

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

    AppendMenuW(popup, MF_STRING, kGroupMinimizeId,
                minimizeText != nullptr ? minimizeText : L"Minimize group");
    AppendMenuW(popup, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(popup, MF_STRING, kGroupCloseId,
                closeText != nullptr ? closeText : L"Close group");

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
