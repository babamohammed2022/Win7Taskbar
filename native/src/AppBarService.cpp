/*
 * Win7Taskbar - Core nativo - AppBar
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

#include "AppBarService.h"
#include "TaskbarButtonNotify.h"
#include "SehGuard.h"

#include <cstdlib>

namespace w7t {

namespace {
/* v1.21.51: classe della finestra overlay della mod Windhawk "Aero Flip
 * 3D Recreation" (CreateOverlayWindow in mods/aero-flip3d-recreation.wh.cpp,
 * ramensoftware/windhawk-mods): e' il contratto pubblico della mod, il
 * solo modo documentato di riconoscere il suo switcher. Il controller
 * ("Flip3DControllerWndClass") e' una finestra message-only (HWND_MESSAGE)
 * e non compare mai a schermo: non serve sorvegliarla. */
constexpr wchar_t kFlip3dOverlayClass[] = L"Flip3DOverlayWndClass";

/* A hard process termination can bypass both the C# cleanup and atexit().
 * Keep the native taskbar state in our own HKCU key before changing it, so a
 * later process can restore the user's original auto-hide/always-on-top
 * choice instead of inheriting the temporary ABS_AUTOHIDE state. */
constexpr wchar_t kTaskbarRecoveryKey[] = L"Software\\Win7Taskbar";
constexpr wchar_t kTaskbarRecoveryValue[] = L"NativeTaskbarStateBeforeReplacement";

bool ReadPersistedTaskbarState(UINT* state) {
    if (state == nullptr) {
        return false;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kTaskbarRecoveryKey, 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    const LONG status = RegQueryValueExW(
        key, kTaskbarRecoveryValue, nullptr, &type,
        reinterpret_cast<LPBYTE>(&value), &bytes);
    RegCloseKey(key);

    constexpr DWORD kKnownStateBits = ABS_AUTOHIDE | ABS_ALWAYSONTOP;
    if (status != ERROR_SUCCESS || type != REG_DWORD ||
        bytes != sizeof(value) || (value & ~kKnownStateBits) != 0) {
        return false;
    }

    *state = static_cast<UINT>(value);
    return true;
}

bool PersistTaskbarState(UINT state) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kTaskbarRecoveryKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &key, &disposition) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD value = static_cast<DWORD>(state & (ABS_AUTOHIDE | ABS_ALWAYSONTOP));
    const LONG status = RegSetValueExW(
        key, kTaskbarRecoveryValue, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

void ClearPersistedTaskbarState() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kTaskbarRecoveryKey, 0,
                      KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    RegDeleteValueW(key, kTaskbarRecoveryValue);
    RegCloseKey(key);
}

/* Diagnostica soltanto: l'area di lavoro viene letta con l'API pubblica
 * SPI_GETWORKAREA, mai scritta implicitamente. SPI_SETWORKAREA altererebbe
 * anche le riserve di altre AppBar e per questo non fa parte del percorso
 * normale di Win7Taskbar. */
void LogAppBarDiagnostics(const wchar_t* phase, HWND appbar,
                         const RECT* negotiated) {
    RECT windowRect = {};
    RECT workArea = {};
    const bool haveWindow = appbar != nullptr && GetWindowRect(appbar, &windowRect);
    const bool haveWork = SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) != FALSE;
    wchar_t line[320] = {};
    swprintf(line, ARRAYSIZE(line),
             L"appbar: %ls hwnd=%p negotiated=(%ld,%ld)-(%ld,%ld) "
             L"window=%ls(%ld,%ld)-(%ld,%ld) workarea=%ls(%ld,%ld)-(%ld,%ld)",
             phase != nullptr ? phase : L"state",
             appbar,
             negotiated != nullptr ? negotiated->left : 0L,
             negotiated != nullptr ? negotiated->top : 0L,
             negotiated != nullptr ? negotiated->right : 0L,
             negotiated != nullptr ? negotiated->bottom : 0L,
             haveWindow ? L"yes" : L"no",
             haveWindow ? windowRect.left : 0L,
             haveWindow ? windowRect.top : 0L,
             haveWindow ? windowRect.right : 0L,
             haveWindow ? windowRect.bottom : 0L,
             haveWork ? L"yes" : L"no",
             haveWork ? workArea.left : 0L,
             haveWork ? workArea.top : 0L,
             haveWork ? workArea.right : 0L,
             haveWork ? workArea.bottom : 0L);
    AppendCoreLog(line);
}
} /* namespace */

AppBarService& AppBarService::Instance() {
    static AppBarService instance;
    return instance;
}

/* ------------------------------------------------------------------ */
/*  Ricerca delle finestre di Explorer                                 */
/* ------------------------------------------------------------------ */

HWND AppBarService::FindNativeTaskbar() {
    /* Cerchiamo la Shell_TrayWnd che NON e' la nostra: il nostro server tray
     * registra una classe con lo stesso nome, quindi filtriamo per processo. */
    const DWORD ourPid = GetCurrentProcessId();
    HWND candidate = nullptr;
    while ((candidate = FindWindowExW(nullptr, candidate, L"Shell_TrayWnd", nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(candidate, &pid);
        if (pid != ourPid) {
            return candidate;
        }
    }
    return nullptr;
}

HWND AppBarService::FindStartOrb() {
    HWND orb = FindWindowExW(nullptr, nullptr, L"Button", L"Start");
    if (orb == nullptr) {
        orb = FindWindowExW(nullptr, nullptr, L"Start", nullptr);
    }
    return orb;
}

HWND AppBarService::FindSecondaryTaskbar(HWND after) {
    return FindWindowExW(nullptr, after, L"Shell_SecondaryTrayWnd", nullptr);
}

/* ------------------------------------------------------------------ */
/*  Registrazione AppBar                                               */
/* ------------------------------------------------------------------ */

int32_t AppBarService::Register(HWND hwnd, int32_t edge, int32_t sizePx) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return W7T_ERR_INVALID_ARG;
    }
    if (m_registered && m_hwnd == hwnd) {
        return W7T_OK;
    }
    if (m_registered) {
        /* La finestra della barra e' stata ricreata (riavvio di Explorer,
         * ricostruzione del frontend): la registrazione vecchia punta a un
         * HWND che non serve piu', va rimossa prima della nuova, altrimenti
         * restano due prenotazioni sullo stesso bordo. */
        Unregister(nullptr);
    }

    m_callbackMessage = RegisterWindowMessageW(L"Win7TaskbarAppBarMessage");
    if (m_callbackMessage == 0) {
        return W7T_ERR_APPBAR;
    }

    APPBARDATA abd = {};
    abd.cbSize           = sizeof(abd);
    abd.hWnd             = hwnd;
    abd.uCallbackMessage = m_callbackMessage;

    /* v3.15: ABM_NEW e' una SendMessage verso la shell - SEH anche qui,
     * una shell morta a meta' handshake non deve portarci giu'. */
    int32_t registered = 0;
    W7T_SEH_TRY {
        registered = (int32_t)(SHAppBarMessage(ABM_NEW, &abd) != 0);
    } W7T_SEH_CATCH {
        registered = 0;
    } W7T_SEH_END
    if (registered == 0) {
        return W7T_ERR_APPBAR;
    }

    m_hwnd       = hwnd;
    m_registered = true;
    m_edge       = edge;
    m_size       = sizePx;

    RECT reserved = {};
    const int32_t positioned = SetPos(hwnd, edge, sizePx, &reserved);

    /* v1.21.51: con la barra registrata parte anche la sorveglianza
     * dell'overlay della mod Flip 3D (vedere il blocco commentato in
     * AppBarService.h). Si installa solo a registrazione riuscita: senza
     * una nostra finestra la guardia non ha nulla da proteggere. */
    if (positioned == W7T_OK) {
        StartFlipWatch();
    }

    return positioned;
}

int32_t AppBarService::SetPos(HWND hwnd, int32_t edge, int32_t sizePx, RECT* out) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return W7T_ERR_INVALID_ARG;
    }
    if (!m_registered) {
        return W7T_ERR_APPBAR;
    }
    /* v3.15: SetPos MAI rientrante. Catena evitata:
     * ABN_POSCHANGED -> SetPos -> ABM_WINDOWPOSCHANGED -> (eco shell)
     * ABN_POSCHANGED -> SetPos -> ... = barra congelata, CPU al massimo.
     * Se arriviamo da un eco interno ci fermiamo subito e restituiamo
     * l'ultimo rettangolo confermato. */
    if (m_inSetPos) {
        if (out != nullptr && m_haveLastRect) {
            *out = m_lastRect;
        }
        return W7T_OK;
    }
    /* Debounce: due SetPos allo stesso bordo/misura in meno di 120 ms
     * sono la stessa rinegoziazione ripetuta (broadcast multipli della
     * shell mentre risistema le AppBar) - si serve l'ultimo risultato. */
    if (m_haveLastRect &&
        GetTickCount64() - m_lastSetPosTick < 120ULL &&
        m_edge == edge && m_size == sizePx) {
        if (out != nullptr) {
            *out = m_lastRect;
        }
        return W7T_OK;
    }
    /* RAII: ogni ritorno anticipato, compreso un fault SEH nella chiamata
     * alla shell, deve sbloccare la posa rientrante. */
    ScopeFlag setPosGuard(m_inSetPos);

    /* Rettangolo FISICO del monitor su cui vive la barra.
     *
     * Prima si usava GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN): in un
     * processo Per-Monitor-V2 quei valori non garantiscono i pixel reali
     * del monitor in tutti i contesti DPI, e la barra e' per progetto
     * confinata al monitor primario. MonitorFromWindow + GetMonitorInfo
     * danno il rettangolo esatto, nel sistema di coordinate fisiche che
     * e' quello del protocollo AppBar (approccio di ManagedShell, che
     * usa i bounds del monitor e mai le metriche di sistema). */
    RECT monitor = {};
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (mon != nullptr && GetMonitorInfoW(mon, &mi)) {
        monitor = mi.rcMonitor;
    } else {
        /* Ripiego: metriche del monitor primario. */
        monitor.left   = 0;
        monitor.top    = 0;
        monitor.right  = GetSystemMetrics(SM_CXSCREEN);
        monitor.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    if (sizePx <= 0) {
        sizePx = m_size > 0 ? m_size : 40;
    }

    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = hwnd;
    abd.uEdge  = static_cast<UINT>(edge);

    switch (edge) {
        case W7T_EDGE_TOP:
            abd.rc.left   = monitor.left;
            abd.rc.top    = monitor.top;
            abd.rc.right  = monitor.right;
            abd.rc.bottom = monitor.top + sizePx;
            break;
        case W7T_EDGE_LEFT:
            abd.rc.left   = monitor.left;
            abd.rc.top    = monitor.top;
            abd.rc.right  = monitor.left + sizePx;
            abd.rc.bottom = monitor.bottom;
            break;
        case W7T_EDGE_RIGHT:
            abd.rc.left   = monitor.right - sizePx;
            abd.rc.top    = monitor.top;
            abd.rc.right  = monitor.right;
            abd.rc.bottom = monitor.bottom;
            break;
        case W7T_EDGE_BOTTOM:
        default:
            abd.rc.left   = monitor.left;
            abd.rc.top    = monitor.bottom - sizePx;
            abd.rc.right  = monitor.right;
            abd.rc.bottom = monitor.bottom;
            break;
    }

    /* v3.15: rete SEH su OGNI negoziazione con la shell. SHAppBarMessage
     * e' una SendMessage verso Explorer: un crash del target dentro il
     * nostro processo (shell sostituita a meta' handshake, tabbed shell
     * in chiusura) non deve poter far saltare anche noi. In caso di
     * fault si esce sbloccando il flag rientrante e lasciando la
     * registrazione coerente. */
    W7T_SEH_TRY {
    /* Protocollo AppBar pubblico Microsoft:
     *
     *   1. ABM_QUERYPOS riceve il rettangolo candidato e restituisce nella
     *      stessa APPBARDATA l'area disponibile sul bordo richiesto;
     *   2. si applica a QUEL rettangolo lo spessore desiderato;
     *   3. ABM_SETPOS negozia la prenotazione finale e puo' modificare ancora
     *      rc per tenere conto di altre AppBar;
     *   4. il rc uscito da ABM_SETPOS e' l'unico rettangolo approvato: non
     *      va ricostruito dal monitor dopo la chiamata.
     *
     * In precedenza il risultato di QUERYPOS veniva ignorato e rc veniva
     * riscritto dopo SETPOS: la finestra visibile poteva quindi divergere
     * dall'area che la shell aveva davvero riservato, soprattutto cambiando
     * fra Basso e Alto. */
    const UINT_PTR queryResult = SHAppBarMessage(ABM_QUERYPOS, &abd);
    if (queryResult == 0 || abd.rc.right <= abd.rc.left ||
        abd.rc.bottom <= abd.rc.top) {
        m_lastSetPosTick = GetTickCount64();
        if (out != nullptr && m_haveLastRect) {
            *out = m_lastRect;
        }
        return W7T_ERR_APPBAR;
    }

    /* QUERYPOS ha gia' scelto il rettangolo disponibile sul monitor e sul
     * bordo. Si modifica solo la dimensione ortogonale, conservando i
     * limiti approvati dalla shell e l'eventuale coordinata non-zero di un
     * monitor secondario. */
    switch (edge) {
        case W7T_EDGE_TOP:
            abd.rc.bottom = abd.rc.top + sizePx;
            break;
        case W7T_EDGE_LEFT:
            abd.rc.right = abd.rc.left + sizePx;
            break;
        case W7T_EDGE_RIGHT:
            abd.rc.left = abd.rc.right - sizePx;
            break;
        case W7T_EDGE_BOTTOM:
        default:
            abd.rc.top = abd.rc.bottom - sizePx;
            break;
    }

    if (abd.rc.right <= abd.rc.left || abd.rc.bottom <= abd.rc.top) {
        m_lastSetPosTick = GetTickCount64();
        if (out != nullptr && m_haveLastRect) {
            *out = m_lastRect;
        }
        return W7T_ERR_APPBAR;
    }

    /* ABM_SETPOS e' in/out: il rettangolo che rimane in APPBARDATA dopo
     * questa chiamata e' il rettangolo approvato dalla shell. */
    if (SHAppBarMessage(ABM_SETPOS, &abd) == 0 ||
        abd.rc.right <= abd.rc.left || abd.rc.bottom <= abd.rc.top) {
        m_lastSetPosTick = GetTickCount64();
        if (out != nullptr && m_haveLastRect) {
            *out = m_lastRect;
        }
        return W7T_ERR_APPBAR;
    }

    m_edge = edge;
    m_size = sizePx;

    /* IL PASSO CHE MANCAVA: la finestra si SPOSTA sul rettangolo che la
     * shell ha confermato.
     *
     * Prima il rettangolo riservato (ABM_SETPOS) e la posizione della
     * finestra WPF (calcolata a parte in DIP) erano due contabilita'
     * separate: se la shell spostava la nostra AppBar (perche' la barra di
     * Explorer era ancora registrata sul bordo, o dopo un cambio
     * DPI/monitor) il lavoro di Windows (work area) e la barra visibile
     * divergevano, e restava una fascia inutilizzata fra le finestre
     * massimizzate e la barra. Con lo spostamento qui la finestra e il
     * work area NON possono piu' divergere: e' l'invariante con cui
     * ManagedShell/RetroBar (AppBarWindow.SetWindowPosition(abd.rc))
     * tengono barra e area riservata sempre coincidenti. */
    SetWindowPos(hwnd, nullptr,
                 abd.rc.left, abd.rc.top,
                 abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    LogAppBarDiagnostics(L"dopo SetPos", hwnd, &abd.rc);
    } W7T_SEH_CATCH {
        /* Fault dentro la negoziazione: meglio una posa rimandata che una
         * barra (o una shell) in crash. ScopeFlag sblocca il flag anche
         * quando il cammino normale non viene raggiunto. */
        m_lastSetPosTick  = GetTickCount64();
        if (out != nullptr && m_haveLastRect) {
            *out = m_lastRect;
        }
        return W7T_ERR_APPBAR;
    } W7T_SEH_END

    /* v3.15: si notifica la shell SOLO se il rettangolo e' davvero
     * cambiato rispetto all'ultima posa. La chiamata incondizionata di
     * ABM_WINDOWPOSCHANGED faceva partire un nuovo giro di
     * ABN_POSCHANGED alla volta (concentrato ai cambi di bordo, e in
     * verticale a ogni rinegoziazione di larghezza del menu' tray):
     * identico schema di ManagedShell (AppBarWindow notifica da
     * WM_WINDOWPOSCHANGED vero, non a ogni SetPos interno). */
    const bool rectChanged =
        !m_haveLastRect ||
        m_lastRect.left   != abd.rc.left   ||
        m_lastRect.top    != abd.rc.top    ||
        m_lastRect.right  != abd.rc.right  ||
        m_lastRect.bottom != abd.rc.bottom;
    m_lastRect      = abd.rc;
    m_haveLastRect  = true;
    m_lastSetPosTick = GetTickCount64();
    if (rectChanged) {
        /* La shell deve sapere che il nostro rettangolo e' cambiato: le
         * altre AppBar ricalcolano la loro posizione rispetto alla nostra
         * (stessa chiamata che ManagedShell fa da WM_WINDOWPOSCHANGED). */
        NotifyWindowPosChanged(hwnd);
    }

    if (out != nullptr) {
        *out = abd.rc;
    }
    return W7T_OK;
}

void AppBarService::NotifyWindowPosChanged(HWND hwnd) {
    if (!m_registered) {
        return;
    }
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        hwnd = m_hwnd;
    }
    if (hwnd == nullptr) {
        return;
    }
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = hwnd;
    /* v3.15: anche questa singola chiamata verso la shell resta dentro
     * la rete SEH (e' ancora una SendMessage verso la shell). */
    W7T_SEH_TRY {
        SHAppBarMessage(ABM_WINDOWPOSCHANGED, &abd);
    } W7T_SEH_CATCH {
        /* notifica persa: ci pensano i controlli successivi */
    } W7T_SEH_END
}

void AppBarService::Activate(HWND hwnd) {
    if (!m_registered) {
        return;
    }
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        hwnd = m_hwnd;
    }
    if (hwnd == nullptr) {
        return;
    }
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = hwnd;
    abd.lParam = 1;   /* TRUE: la barra e' attiva */
    SHAppBarMessage(ABM_ACTIVATE, &abd);
}

bool AppBarService::HandleCallback(uint32_t wParam, int32_t lParam) {
    if (!m_registered) {
        return false;
    }

    /* v1.21.51 - rete di sicurezza su OGNI notifica della shell: se la
     * guardia Flip 3D risulta ancora attiva ma l'overlay della mod non
     * esiste piu' (eventi persi per un thread bloccato, una mod aggiornata
     * a meta' animazione, qualunque caso non previsto), qui la barra torna
     * interagibile. La barra non puo' MAI restare disabilitata: il costo
     * e' un IsWindow+IsWindowVisible ogni tanto, sempre fuori dal path
     * caldo della barra. */
    if (m_flipGuardActive.load(std::memory_order_acquire) &&
        !FlipGuardOverlayAlive()) {
        ReleaseFlip3dGuard();
    }

    /* Notifiche ABN_* del protocollo AppBar.
     *
     * Il messaggio di callback era registrato ma MAI gestito: la shell
     * ci avvisa qui quando lo spazio riservato va ricalcolato (ABN_POSCHANGED
     * arriva quando un'altra AppBar compare/scompare, quando la barra di
     * Explorer cambia stato, quando cambiano i monitor). Ignorarlo lasciava
     * la prenotazione stantia: esattamente il "buco" fra le finestre
     * massimizzate e la barra. Lo stesso flusso e' quello di
     * ManagedShell (AppBarWindow.WndProc, AppBarNotifications.PosChanged). */
    switch (wParam) {
        case ABN_POSCHANGED:
            /* v3.15 - soppressione dell'ECO: un ABN_POSCHANGED che arriva
             * mentre un nostro SetPos e' in corso, o nei 300 ms dalla sua
             * fine, e' quasi certamente scatenato dalla nostra stessa
             * richiesta ABM_WINDOWPOSCHANGED: rispondere con un altro
             * SetPos aprirebbe il ciclo infinito app<->shell (barra
             * congelata, CPU al massimo - la regressione verticale).
             * Le notifiche genuine (altre AppBar che compaiono/spariscono,
             * cambio monitor) arrivano al di fuori di questo ingresso. */
            if (m_inSetPos ||
                (m_haveLastRect &&
                 GetTickCount64() - m_lastSetPosTick < 300ULL)) {
                return true;
            }
            /* Riesegue la sequenza QUERYPOS/SETPOS e risistema la finestra
             * sul rettangolo confermato (vedi SetPos). */
            SetPos(m_hwnd, m_edge, m_size, nullptr);
            return true;

        case ABN_WINDOWARRANGE:
            /* Prima che la shell disponga le finestre (lParam TRUE) la barra
             * si toglie di mezzo; a disposizione finita (FALSE) torna.
             * v1.21.51: con la guardia Flip 3D attiva la barra resta
             * visibile e inerte qualunque cosa disponga la shell:
             * l'animazione della mod possiede lo schermo. */
            if (m_flipGuardActive.load(std::memory_order_acquire)) {
                return true;
            }
            ShowWindow(m_hwnd, lParam ? SW_HIDE : SW_SHOW);
            return true;

        case ABN_FULLSCREENAPP:
            /* App a schermo intero: la shell passa in lParam TRUE quando
             * un'app fullscreen si affaccia, FALSE quando finisce. Stesso
             * trattamento di ABN_WINDOWARRANGE qui sopra: la barra si toglie
             * di mezzo subito, senza passare dal C# (nessuna animazione).
             *
             * E' l'unico segnale affidabile per questo caso: un video che
             * va fullscreen resta nella stessa HWND gia' in primo piano
             * (il browser), quindi non scatta EVENT_SYSTEM_FOREGROUND e non
             * si puo' rilevare da li'.
             *
             * v1.21.51 - L'OVERLAY DELLA MOD FLIP 3D NON E' UNA APP
             * FULLSCREEN AI FINI DELLA BARRA: la mod Windhawk "Aero Flip
             * 3D Recreation" apre il suo switcher proprio con una finestra
             * popup topmost a tutto schermo (classe Flip3DOverlayWndClass,
             * vedere AppBarService.h), e questa notifica arrivava con
             * lParam TRUE per tutta la durata dell'animazione: la barra si
             * nascondeva e "spariva" finche' la mod restava aperta. Se il
             * fullscreen e' l'overlay della mod, la guardia Flip 3D prende
             * il posto del vecchio ShowWindow(SW_HIDE): barra VISIBILE
             * sopra l'overlay e NON interagibile finche' l'animazione dura,
             * come la taskbar vera di Vista/7 durante il Flip 3D. Per le
             * app fullscreen vere (video, giochi) resta il comportamento
             * storico. */
            if (lParam) {
                /* L'hook su EVENT_OBJECT_SHOW potrebbe non essere ancora
                 * arrivato quando la shell ci avvisa: si riconosce
                 * l'overlay dal primo piano o, in ripiego, per classe. */
                HWND candidate = GetForegroundWindow();
                if (!LooksLikeFlip3dOverlay(candidate)) {
                    candidate = FindWindowW(kFlip3dOverlayClass, nullptr);
                    if (candidate != nullptr && !IsWindowVisible(candidate)) {
                        candidate = nullptr;
                    }
                }
                if (candidate != nullptr) {
                    EngageFlip3dGuard(candidate);
                    return true;   /* niente SW_HIDE: la barra resta visibile */
                }
                ShowWindow(m_hwnd, SW_HIDE);
            } else {
                if (m_flipGuardActive.load(std::memory_order_acquire)) {
                    /* La mod ha chiuso: la barra torna interagibile. E'
                     * gia' visibile (non ci siamo mai nascosti), quindi
                     * niente ShowWindow. */
                    ReleaseFlip3dGuard();
                    return true;
                }
                ShowWindow(m_hwnd, SW_SHOW);
            }
            return true;

        case ABN_STATECHANGE:
            return true;

        default:
            return false;
    }
}

int32_t AppBarService::Unregister(HWND hwnd) {
    if (!m_registered) {
        return W7T_OK;
    }

    /* v1.21.51: prima di mollare la finestra la guardia Flip 3D si
     * scioglie (la barra torna interagibile PRIMA che la registrazione
     * sparisca) e gli hook si staccano con le loro guardie RAII: nessun
     * WinEvent hook puo' sopravvivere alla barra che sorvegliava. Vale
     * anche per il ciclo unregister/register del riavvio di Explorer. */
    ReleaseFlip3dGuard();
    StopFlipWatch();

    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = (hwnd != nullptr) ? hwnd : m_hwnd;

    /* v3.15: SEH anche in uscita - una shell gia' morta (riavvio di
     * Explorer) fa fallire ABM_REMOVE dentro un processo host assente. */
    W7T_SEH_TRY {
        SHAppBarMessage(ABM_REMOVE, &abd);
    } W7T_SEH_CATCH {
    } W7T_SEH_END

    m_registered    = false;
    m_hwnd          = nullptr;
    m_haveLastRect  = false;  /* rettangolo stantio: non riusarlo */
    m_inSetPos      = false;
    return W7T_OK;
}

/* ------------------------------------------------------------------ */
/*  Taskbar nativa                                                     */
/* ------------------------------------------------------------------ */

namespace {
/* Atomo di classe del pulsante Start, come in ExplorerHelper. */
const LPWSTR kStartButtonAtom = MAKEINTATOM(0xC017);
}

UINT AppBarService::GetNativeTaskbarState() {
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = FindNativeTaskbar();
    if (abd.hWnd == nullptr) {
        return ABS_ALWAYSONTOP;
    }
    return SHAppBarMessage(ABM_GETSTATE, &abd);
}

void AppBarService::SetNativeTaskbarState(UINT state) {
    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = FindNativeTaskbar();
    if (abd.hWnd == nullptr) {
        return;
    }
    abd.lParam = static_cast<LPARAM>(state);
    SHAppBarMessage(ABM_SETSTATE, &abd);
}

void AppBarService::SetNativeTaskbarVisibility(bool hide) {
    const UINT swp = hide ? SWP_HIDEWINDOW : SWP_SHOWWINDOW;
    const bool wantHidden = hide;

    HWND taskbar = FindNativeTaskbar();
    if (taskbar != nullptr && (wantHidden == (IsWindowVisible(taskbar) != FALSE))) {
        SetWindowPos(taskbar, HWND_BOTTOM, 0, 0, 0, 0,
                     swp | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    HWND start = FindWindowExW(nullptr, nullptr, kStartButtonAtom, nullptr);
    if (start == nullptr) {
        start = FindStartOrb();
    }
    if (start != nullptr && (wantHidden == (IsWindowVisible(start) != FALSE))) {
        SetWindowPos(start, HWND_BOTTOM, 0, 0, 0, 0,
                     swp | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    HWND secondary = nullptr;
    while ((secondary = FindSecondaryTaskbar(secondary)) != nullptr) {
        if (wantHidden == (IsWindowVisible(secondary) != FALSE)) {
            SetWindowPos(secondary, HWND_BOTTOM, 0, 0, 0, 0,
                         swp | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

void AppBarService::DoHideNativeTaskbar() {
    const HWND taskbar = FindNativeTaskbar();
    LogAppBarDiagnostics(L"prima DoHideNativeTaskbar", taskbar, nullptr);
    SetNativeTaskbarState(ABS_AUTOHIDE);
    SetNativeTaskbarVisibility(true);
    LogAppBarDiagnostics(L"dopo DoHideNativeTaskbar", FindNativeTaskbar(), nullptr);
}

namespace {
volatile LONG g_restorerInstalled = 0;

bool IsUnrecoverable(DWORD code) {
    /* SOLO fault che nessun __except potra' mai gestire: stack overflow e
     * fast-fail. Tutto il resto, 0xC0000005 compreso, puo' essere un'
     * eccezione di primo livello che un gestore SEH piu' in alto smaltira'
     * da solo (user32 lo fa per i WndProc: mostra un dialogo e continua).
     * Agire al primo passaggio era peggio del fault: rimostrava la barra e
     * rientrava in user32 con i suoi lock gia' presi, producendo il dialogo
     * "Exception Processing Message 0xc0000005 - Unexpected parameters". */
    return code == EXCEPTION_STACK_OVERFLOW || code == 0xC0000409u;
}

/* Ultimo appello: passa di qui solo cio' che SEH ha gia' deciso di non
 * gestire, cioe' la morte reale del processo. Nessun effetto collaterale
 * sulle eccezioni recuperate. */
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

LONG CALLBACK LastChanceFilter(PEXCEPTION_POINTERS info) {
    AppBarService::Instance().RestoreNativeTaskbarNow();
    if (g_previousFilter != nullptr) {
        return g_previousFilter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}

void AppBarService::InstallCrashRestorer() {
    if (InterlockedCompareExchange(&g_restorerInstalled, 1, 0) != 0) {
        return;
    }
    AddVectoredExceptionHandler(1, CrashRestorerHandler);
    g_previousFilter = SetUnhandledExceptionFilter(LastChanceFilter);

    /* Uscita "normale ma brusca" (Environment.Exit, exit()): anche li' la
     * barra nativa va restituita a Windows. */
    std::atexit([] {
        AppBarService::Instance().RestoreNativeTaskbarNow();
    });
}

LONG CALLBACK AppBarService::CrashRestorerHandler(PEXCEPTION_POINTERS info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!IsUnrecoverable(info->ExceptionRecord->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    AppBarService& self = Instance();
    if (self.m_nativeHidden.load()) {
        /* Niente longjmp, niente mutex, niente allocazioni: si rimostra la
         * barra e si lascia che il processo faccia il suo corso. */
        self.RestoreNativeTaskbarNow();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void AppBarService::RestoreNativeTaskbarNow() {
    if (!m_nativeHidden.exchange(false)) {
        return;  /* gia' visibile: niente da fare */
    }
    SetNativeTaskbarState(m_stateSaved ? m_startupState : ABS_ALWAYSONTOP);
    SetNativeTaskbarVisibility(false);
}

/* ------------------------------------------------------------------ */
/*  v2.60 - Ritorno della barra nativa: solo eventi                     */
/*                                                                      */
/*  Explorer rimostra la sua barra premendo Start, cambiando risoluzione */
/*  o riavviando la shell. Prima lo si scopriva con un ciclo a 100 ms    */
/*  che chiamava ABM_SETSTATE e SetWindowPos(HIDE) per 3 secondi: da      */
/*  fuori il risultato era la barra nativa che lampeggiava sopra la      */
/*  nostra. Ora la comparsa e' un evento di sistema (EVENT_OBJECT_SHOW,   */
/*  EVENT_SYSTEM_FOREGROUND) e il ri-nascondi avviene una volta sola,     */
/*  fuori dal callback, su un thread che dorme finche' non serve.        */
/* ------------------------------------------------------------------ */

void CALLBACK AppBarService::HideWatcherProc(HWINEVENTHOOK, DWORD,
                                             HWND hwnd, LONG idObject,
                                             LONG idChild, DWORD, DWORD) {
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != 0) {
        return;
    }

    AppBarService& self = Instance();

    /* Do not promote our taskbar to the front of the topmost band on every
     * foreground change. AppBar reservation controls the work area; a
     * foreground-wide HWND_TOPMOST reassertion only races other windows and
     * can paint the replacement over a newly activated application. */

    if (!self.m_nativeHidden.load() || self.m_watchEvent == nullptr) {
        return;
    }

    /* Solo le barre di Explorer: i nostri stessi oggetti hanno la stessa
     * classe (il server tray registra Shell_TrayWnd), quindi il processo
     * proprietario deve essere un altro. */
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return;
    }

    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) == 0) {
        return;
    }
    const bool isBar = _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
                       _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
    if (!isBar) {
        return;
    }

    /* Un solo evento per volta: finche' il thread non ha finito, gli
     * eventi successivi non accodano lavoro (auto-reset). */
    SetEvent(self.m_watchEvent);
}

void AppBarService::HideWatcherLoop() {
    while (m_watchRun.load()) {
        /* v3.5: il timeout NON ripiega piu' su "continue". Prima il loop
         * ri-nascondeva la barra solo quando arrivava un evento (SHOW o
         * FOREGROUND); durante una cattura dello Strumento di cattura,
         * pero', la barra nativa viene rimessa a schermo da Windows senza
         * che quegli eventi arrivino a noi (la cattura la mostra in un
         * composizione dedicata), e il vecchio ripiego la lasciava
         * lampeggiare nelle foto. Ora ogni 500 ms, evento o no, il loop
         * controlla la visibilita' e la ri-nasconde se serve: e' il
         * ripiego che RetroBar ottiene col suo monitor continuo. Il
         * controllo resta leggero (FindWindow + IsWindowVisible) e non fa
         * nulla quando la barra e' gia' nascosta. */
        const DWORD wait = WaitForSingleObject(m_watchEvent, 500);
        if (!m_watchRun.load()) {
            break;
        }
        (void)wait;
        if (!m_nativeHidden.load()) {
            continue;
        }
        /* Rinasconde una volta; se Explorer la rimostra, arriva un altro
         * evento. Il controllo di visibilita' evita lavoro inutile. */
        HWND taskbar = FindNativeTaskbar();
        if (taskbar != nullptr && IsWindowVisible(taskbar)) {
            DoHideNativeTaskbar();
        }
        HWND secondary = nullptr;
        while ((secondary = FindSecondaryTaskbar(secondary)) != nullptr) {
            if (IsWindowVisible(secondary)) {
                DoHideNativeTaskbar();
                break;
            }
        }
    }
}

void AppBarService::StartHideWatcher() {
    if (m_watchThread.joinable()) {
        return;
    }
    if (m_watchEvent == nullptr) {
        m_watchEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (m_watchEvent == nullptr) {
            return;
        }
    }

    m_watchRun.store(true);
    m_watchThread = std::thread([this] { HideWatcherLoop(); });

    if (m_hideHook == nullptr) {
        m_hideHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                                     nullptr, HideWatcherProc, 0, 0,
                                     WINEVENT_OUTOFCONTEXT);
    }
    if (m_fgHook == nullptr) {
        m_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                                   EVENT_SYSTEM_FOREGROUND,
                                   nullptr, HideWatcherProc, 0, 0,
                                   WINEVENT_OUTOFCONTEXT);
    }
}

void AppBarService::StopHideWatcher() {
    if (m_hideHook != nullptr) {
        UnhookWinEvent(m_hideHook);
        m_hideHook = nullptr;
    }
    if (m_fgHook != nullptr) {
        UnhookWinEvent(m_fgHook);
        m_fgHook = nullptr;
    }

    m_watchRun.store(false);
    if (m_watchEvent != nullptr) {
        SetEvent(m_watchEvent);   /* sveglia il thread per l'uscita */
    }
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
    if (m_watchEvent != nullptr) {
        CloseHandle(m_watchEvent);
        m_watchEvent = nullptr;
    }
}

int32_t AppBarService::SetNativeTaskbarHidden(bool hidden) {
    std::lock_guard<std::mutex> lock(m_hideMutex);

    auto recoverPersistedState = [this]() -> bool {
        UINT savedState = 0;
        if (!ReadPersistedTaskbarState(&savedState)) {
            return true;  /* no interrupted replacement run recorded */
        }
        if (FindNativeTaskbar() == nullptr) {
            return false; /* leave the value for Explorer's next appearance */
        }

        SetNativeTaskbarState(savedState);
        SetNativeTaskbarVisibility(false);
        ClearPersistedTaskbarState();
        AppendCoreLog(L"appbar: restored taskbar state left by an interrupted Win7Taskbar process");
        return true;
    };

    /* v2.62-alpha (G7): the bar is not the taskbar again: drop the
     * per-pid dedup so the next ownership period notifies cleanly. */
    if (!hidden) {
        w7t::ResetTaskbarButtonNotify();
    }

    if (hidden) {
        if (!m_stateSaved) {
            /* A killed/failed previous process may have left Explorer in
             * ABS_AUTOHIDE. Restore that run's saved user state before taking
             * a fresh snapshot; never overwrite the only recovery copy. */
            if (!recoverPersistedState()) {
                AppendCoreLog(L"appbar: deferred hide because the previous taskbar state could not yet be recovered");
                return W7T_ERR_APPBAR;
            }

            m_startupState = GetNativeTaskbarState();
            m_stateSaved   = true;
            if (!PersistTaskbarState(m_startupState)) {
                m_stateSaved = false;
                AppendCoreLog(L"appbar: refusing to hide the native taskbar because its original state could not be persisted");
                return W7T_ERR_APPBAR;
            }
        }

        InstallCrashRestorer();

        m_nativeHidden.store(true);
        DoHideNativeTaskbar();
        StartHideWatcher();
    } else {
        StopHideWatcher();
        if (m_nativeHidden.load()) {
            RestoreNativeTaskbarNow();
            ClearPersistedTaskbarState();
            m_stateSaved = false;
        } else if (!recoverPersistedState()) {
            AppendCoreLog(L"appbar: previous taskbar state remains pending until Explorer is available");
            return W7T_ERR_APPBAR;
        } else {
            m_stateSaved = false;
        }
    }

    return W7T_OK;
}

void AppBarService::ReassertNativeTaskbarHidden() {
    /* Chiamata esplicita per gli eventi della nostra barra (menu Start,
     * cambio risoluzione, riavvio della shell): il caso "Explorer la
     * rimostra da sola" lo coprono gli hook di StartHideWatcher. */
    if (m_nativeHidden.load()) {
        DoHideNativeTaskbar();
    }
}

int32_t AppBarService::GetPrimaryWorkArea(RECT* out) {
    if (out == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, out, 0)) {
        return W7T_ERR_APPBAR;
    }
    return W7T_OK;
}

/* ------------------------------------------------------------------ */
/*  v1.21.51 - Guardia "Aero Flip 3D Recreation" (mod Windhawk)        */
/*                                                                     */
/*  IL PROBLEMA: la mod apre il suo switcher con un popup TOPMOST a    */
/*  tutto schermo sul monitor primario (classe Flip3DOverlayWndClass)  */
/*  e ce lo lascia per tutta l'animazione. Per il protocollo AppBar    */
/*  quella e' un'app a schermo intero: la shell invia                  */
/*  ABN_FULLSCREENAPP(lParam TRUE) e il vecchio gestore nascondeva la  */
/*  barra con ShowWindow(SW_HIDE): la taskbar spariva appena si        */
/*  premeva Win+Tab con la mod installata.                             */
/*                                                                     */
/*  LA SOLUZIONE (richiesta dell'utente): durante l'animazione la      */
/*  barra deve restare PRESENTE ma NON INTERAGIBILE, e tornare         */
/*  normale alla fine. Riconoscimento: la classe dell'overlay e' il    */
/*  contratto pubblico della mod (CreateOverlayWindow,                 */
/*  mods/aero-flip3d-recreation.wh.cpp di ramensoftware/windhawk-mods  */
/*  - codice consultato per questa modifica). Segnali di sistema,      */
/*  tutti API documentate Microsoft:                                   */
/*    - EVENT_OBJECT_SHOW dell'overlay           -> ingaggio guardia;  */
/*    - EVENT_OBJECT_DESTROY / HIDE dell'overlay -> rilascio (la mod   */
/*      distrugge l'overlay in SafeDestroyOverlayWindow, oppure lo     */
/*      nasconde nel ripiego);                                         */
/*    - EVENT_SYSTEM_FOREGROUND: la mod porta l'overlay in primo piano */
/*      DOPO averlo alzato in cima alla fascia topmost                 */
/*      (ActivateFlip3DImpl: SetWindowPos(HWND_TOPMOST) ->             */
/*      SetForegroundWindow): e' il momento in cui si riafferma la     */
/*      barra SOPRA l'overlay; e' anche la rete di sicurezza se il     */
/*      primo piano torna altrove;                                     */
/*    - ABN_FULLSCREENAPP stesso: riconosciuta l'overlay, niente       */
/*      SW_HIDE; alla chiusura (lParam FALSE) rilascio.                */
/*  Ogni transizione e' protetta: mutex ricorsivo (le chiamate         */
/*  finestra rientrano nel WndProc della barra), stato scritto PRIMA   */
/*  delle chiamate che rientrano, try/catch nei callback (niente puo'  */
/*  uscire da un WinEventProc) e hook in guardie RAII.                 */
/* ------------------------------------------------------------------ */

bool AppBarService::LooksLikeFlip3dOverlay(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }
    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) == 0) {
        return false;
    }
    /* Le classi di finestra Win32 non distinguono maiuscole/minuscole. */
    return _wcsicmp(cls, kFlip3dOverlayClass) == 0;
}

void CALLBACK AppBarService::FlipWatchProc(HWINEVENTHOOK, DWORD event,
                                           HWND hwnd, LONG idObject,
                                           LONG idChild, DWORD, DWORD) {
    /* Barriera assoluta: da un WinEventProc non esce nulla. Il callback
     * arriva sul thread che ha registrato l'hook e che pompa i messaggi,
     * cioe' il thread UI del frontend WPF che ha eseguito ABM_NEW (MSDN,
     * SetWinEventHook: "The client thread that calls SetWinEventHook must
     * have a message loop in order to receive events" e "the event is
     * delivered on the same thread that called SetWinEventHook"): le
     * chiamate finestra che seguono avvengono quindi dal thread che
     * possiede la barra. */
    try {
        if (idObject != OBJID_WINDOW || idChild != 0) {
            return;
        }

        AppBarService& self = Instance();

        switch (event) {
            case EVENT_OBJECT_SHOW:
                /* L'unico punto d'ingresso della guardia: la classe e' il
                 * contratto della mod. Ogni altra finestra mostrata nel
                 * sistema passa di qui ed e' ignorata con un solo
                 * GetClassNameW (lo stesso costo del HideWatcherProc). */
                if (hwnd != nullptr && LooksLikeFlip3dOverlay(hwnd)) {
                    self.EngageFlip3dGuard(hwnd);
                }
                break;

            case EVENT_OBJECT_HIDE:
            case EVENT_OBJECT_DESTROY: {
                /* Fine dell'animazione. Si rilascia SOLO se l'evento e'
                 * della finestra overlay registrata: gli eventi
                 * HIDE/DESTROY di tutte le altre finestre del sistema
                 * passano senza toccare lo stato. Il controllo atomico
                 * tiene il path caldo (ogni finestra del sistema che
                 * chiude) fuori dal mutex. */
                if (!self.m_flipGuardActive.load(std::memory_order_acquire)) {
                    break;
                }
                bool mine = false;
                {
                    std::lock_guard<std::recursive_mutex> lk(self.m_flipMutex);
                    mine = hwnd == self.m_flipOverlay;
                }
                if (mine) {
                    self.ReleaseFlip3dGuard();
                }
                break;
            }

            case EVENT_SYSTEM_FOREGROUND: {
                if (!self.m_flipGuardActive.load(std::memory_order_acquire)) {
                    break;
                }
                std::lock_guard<std::recursive_mutex> lk(self.m_flipMutex);
                if (hwnd != nullptr && hwnd == self.m_flipOverlay) {
                    /* L'overlay e' arrivato in primo piano: la mod lo ha
                     * appena alzato in cima alla fascia topmost, quindi
                     * ORA si riafferma la barra sopra di lui (farlo
                     * prima della sua SetWindowPos sarebbe inutile). */
                    self.ReassertBarOverFlipOverlay();
                } else if (!self.FlipGuardOverlayAliveLocked()) {
                    /* Il primo piano e' tornato a un'altra finestra e
                     * l'overlay non c'e' piu': rete di sicurezza. */
                    self.ReleaseFlip3dGuard();
                } else {
                    /* Sessione ancora aperta con un'altra finestra in
                     * primo piano: la barra resta al suo posto, sopra. */
                    self.ReassertBarOverFlipOverlay();
                }
                break;
            }

            default:
                break;
        }
    } catch (...) {
        /* mai propagare fuori da un callback di sistema */
    }
}

void AppBarService::StartFlipWatch() {
    std::lock_guard<std::recursive_mutex> lk(m_flipMutex);
    /* Un solo intervallo per i tre segnali di stato della finestra:
     * EVENT_OBJECT_DESTROY (0x8001), EVENT_OBJECT_SHOW (0x8002) e
     * EVENT_OBJECT_HIDE (0x8003) sono contigui, quindi UN hook copre
     * distruzione, comparsa e ripiego-nascondi dell'overlay. Il secondo
     * hook e' solo il cambio di primo piano. RAII: se SetWinEventHook
     * fallisse (ritorna nullptr) la guardia resta vuota e la barra si
     * comporta come ha sempre fatto: mai un hook a meta'. */
    if (!m_flipStateHook.valid()) {
        m_flipStateHook.reset(SetWinEventHook(
            EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE,
            nullptr, FlipWatchProc, 0, 0, WINEVENT_OUTOFCONTEXT));
    }
    if (!m_flipForegroundHook.valid()) {
        m_flipForegroundHook.reset(SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, FlipWatchProc, 0, 0, WINEVENT_OUTOFCONTEXT));
    }

    /* Se la mod fosse GIA' aperta (avvio della barra durante
     * un'animazione, oppure ri-registrazione dopo un riavvio di
     * Explorer) l'evento SHOW l'abbiamo perso: la protezione parte
     * subito, senza aspettare la prossima animazione. */
    HWND existing = FindWindowW(kFlip3dOverlayClass, nullptr);
    if (existing != nullptr && IsWindowVisible(existing)) {
        EngageFlip3dGuard(existing);
    }
}

void AppBarService::StopFlipWatch() {
    std::lock_guard<std::recursive_mutex> lk(m_flipMutex);
    /* RAII: reset() sgancia gli hook solo se erano davvero installati. */
    m_flipStateHook.reset();
    m_flipForegroundHook.reset();
}

void AppBarService::EngageFlip3dGuard(HWND overlay) {
    HWND bar = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lk(m_flipMutex);
        if (overlay == nullptr || !IsWindow(overlay)) {
            return;
        }
        if (!m_registered || m_hwnd == nullptr || !IsWindow(m_hwnd)) {
            return;
        }
        const bool already = m_flipGuardActive.load(std::memory_order_acquire);
        /* Lo stato cambia PRIMA delle chiamate che possono rientrare
         * (SetWindowPos -> WM_WINDOWPOSCHANGED -> WndProc della barra ->
         * AppBarNotify -> HandleCallback): rientrando, la guardia deve
         * gia' risultare attiva e coerente. */
        m_flipOverlay = overlay;
        m_flipGuardActive.store(true, std::memory_order_release);
        bar = m_hwnd;

        if (!already) {
            try {
                AppendCoreLog(L"appbar: mod Flip 3D aperta, barra visibile "
                              L"ma non interagibile finche' l'animazione dura");
            } catch (...) {
                /* la diagnostica non e' mai un requisito */
            }
        }
    }

    try {
        /* 1) PRESENTE: se qualcosa ci avesse gia' nascosti (la
         *    ABN_FULLSCREENAPP vinta in corsa con l'hook), si torna
         *    visibili SENZA rubare il primo piano all'overlay. */
        if (!IsWindowVisible(bar)) {
            ShowWindow(bar, SW_SHOWNOACTIVATE);
        }

        /* 2) SOPRA l'overlay: barra e overlay stanno entrambi nella
         *    fascia topmost; HWND_TOPMOST ci riporta in testa
         *    (MSDN, SetWindowPos). SWP_NOACTIVATE: il primo piano resta
         *    all'overlay, che e' la finestra che comanda. */
        SetWindowPos(bar, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        /* 3) NON INTERAGIBILE: EnableWindow(FALSE) toglie mouse e
         *    tastiera alla finestra (MSDN: "the window does not receive
         *    input such as mouse clicks and key presses") e la fa
         *    saltare del tutto dall'hit-test (MSDN, WindowFromPoint:
         *    "does not retrieve a handle to a hidden or disabled window,
         *    even if the point is within the window"): clic e rotellina
         *    sulla fascia della barra cadono sull'overlay della mod, che
         *    continua a rispondere (navigazione con la rotellina sopra
         *    tutta la fascia). La barra resta disegnata: e' esattamente
         *    "presente ma non interagibile". Il sistema invia anche
         *    WM_CANCELMODE (MSDN, EnableWindow): un eventuale
         *    trascinamento d'icona in corso si chiude in modo pulito. */
        if (IsWindowEnabled(bar)) {
            EnableWindow(bar, FALSE);
        }

        /* 4) Gli stati hover di WPF (tessera della tray, spillo) non
         *    riceveranno piu' messaggi mouse finche' la finestra e'
         *    disabilitata: un WM_MOUSELEAVE sintetico spegne subito
         *    l'hover rimasto acceso, cosi' la barra appare ferma e
         *    neutra per tutta l'animazione. */
        PostMessageW(bar, WM_MOUSELEAVE, 0, 0);
    } catch (...) {
        /* Qualsiasi cosa sia andata storto, la guardia si scioglie: una
         * barra interattiva e' sempre meglio di una barra disabilitata
         * a meta'. */
        ReleaseFlip3dGuard();
    }
}

void AppBarService::ReleaseFlip3dGuard() {
    HWND bar = nullptr;
    {
        std::lock_guard<std::recursive_mutex> lk(m_flipMutex);
        if (!m_flipGuardActive.load(std::memory_order_acquire)) {
            return;   /* idempotente: rilasciare due volte non fa nulla */
        }
        m_flipGuardActive.store(false, std::memory_order_release);
        m_flipOverlay = nullptr;
        bar = m_hwnd;
        try {
            AppendCoreLog(L"appbar: Flip 3D chiuso, barra di nuovo "
                          L"interagibile");
        } catch (...) {
            /* la diagnostica non e' mai un requisito */
        }
    }

    try {
        if (bar != nullptr && IsWindow(bar)) {
            /* Prima l'input, poi lo z-order: la barra torna pienamente
             * interagibile e resta in testa alla fascia topmost, pronta
             * per la prossima animazione o per il normale uso. */
            EnableWindow(bar, TRUE);
            SetWindowPos(bar, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    } catch (...) {
        /* La guardia e' gia' spenta e lo stato e' coerente: ogni segnale
         * successivo (foreground, ABN, hook) riprova l'EnableWindow. */
    }
}

void AppBarService::ReassertBarOverFlipOverlay() {
    /* Contratto: chiamata con m_flipMutex gia' presa. Niente logica di
     * stato qui, solo la geometria: la barra torna in testa alla fascia
     * topmost senza attivazione (il primo piano resta all'overlay). */
    if (m_hwnd == nullptr || !IsWindow(m_hwnd)) {
        return;
    }
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool AppBarService::FlipGuardOverlayAlive() {
    std::lock_guard<std::recursive_mutex> lk(m_flipMutex);
    return FlipGuardOverlayAliveLocked();
}

bool AppBarService::FlipGuardOverlayAliveLocked() const {
    return m_flipOverlay != nullptr &&
           IsWindow(m_flipOverlay) &&
           IsWindowVisible(m_flipOverlay);
}

} /* namespace w7t */
