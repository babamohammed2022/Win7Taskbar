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

#include <cstdlib>

namespace w7t {

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

    if (SHAppBarMessage(ABM_NEW, &abd) == 0) {
        return W7T_ERR_APPBAR;
    }

    m_hwnd       = hwnd;
    m_registered = true;
    m_edge       = edge;
    m_size       = sizePx;

    RECT reserved = {};
    return SetPos(hwnd, edge, sizePx, &reserved);
}

int32_t AppBarService::SetPos(HWND hwnd, int32_t edge, int32_t sizePx, RECT* out) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return W7T_ERR_INVALID_ARG;
    }
    if (!m_registered) {
        return W7T_ERR_APPBAR;
    }

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

    /* ABM_QUERYPOS lascia che il sistema aggiusti il rettangolo tenendo
     * conto delle altre AppBar gia' registrate. */
    SHAppBarMessage(ABM_QUERYPOS, &abd);

    /* Dopo la query ricomponiamo lo spessore richiesto sul bordo scelto
     * (l'aggiustamento del sistema sposta il lato opposto: lo riportiamo,
     * come fa anche ManagedShell::ABSetPos). */
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

    SHAppBarMessage(ABM_SETPOS, &abd);

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

    /* La shell deve sapere che il nostro rettangolo e' cambiato: le altre
     * AppBar ricalcolano la loro posizione rispetto alla nostra
     * (stessa chiamata che ManagedShell fa da WM_WINDOWPOSCHANGED). */
    NotifyWindowPosChanged(hwnd);

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
    SHAppBarMessage(ABM_WINDOWPOSCHANGED, &abd);
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
            /* Riesegue la sequenza QUERYPOS/SETPOS e risistema la finestra
             * sul rettangolo confermato (vedi SetPos). */
            SetPos(m_hwnd, m_edge, m_size, nullptr);
            return true;

        case ABN_WINDOWARRANGE:
            /* Prima che la shell disponga le finestre (lParam TRUE) la barra
             * si toglie di mezzo; a disposizione finita (FALSE) torna. */
            ShowWindow(m_hwnd, lParam ? SW_HIDE : SW_SHOW);
            return true;

        case ABN_FULLSCREENAPP:
            /* App a schermo intero: la barra deve farsi da parte.
             * Il frontend ha gia' il suo percorso (W7T_EVT_FULLSCREEN_CHANGED
             * da WindowManager); qui si dichiara solo gestita, come fanno
             * le shell che non vogliono l'animazione di default. */
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

    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = (hwnd != nullptr) ? hwnd : m_hwnd;

    SHAppBarMessage(ABM_REMOVE, &abd);

    m_registered = false;
    m_hwnd       = nullptr;
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
    SetNativeTaskbarState(ABS_AUTOHIDE);
    SetNativeTaskbarVisibility(true);
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

void CALLBACK AppBarService::HideWatcherProc(HWINEVENTHOOK, DWORD event,
                                             HWND hwnd, LONG idObject,
                                             LONG idChild, DWORD, DWORD) {
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != 0) {
        return;
    }

    AppBarService& self = Instance();

    /* v1.7.4: a ogni cambio di finestra in primo piano riafferma la NOstra
     * barra nella fascia topmost, come fa explorer.exe con la propria.
     * Senza questo, un'app che torna in primo piano (Chrome a schermo
     * intero in primis) puo' finire SOPRA la barra perche' nessuno
     * riacquista lo z-order per noi. Manutenzione best-effort: HWND
     * invalido o SetWindowPos fallito non propagano nulla (si ritenta al
     * prossimo cambio di foreground). SWP_NOACTIVATE: si aggiorna solo la
     * posizione nella fascia topmost, il focus resta dove e'. */
    if (event == EVENT_SYSTEM_FOREGROUND &&
        self.m_hwnd != nullptr && IsWindow(self.m_hwnd)) {
        SetWindowPos(self.m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

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

    if (hidden) {
        if (!m_stateSaved) {
            m_startupState = GetNativeTaskbarState();
            m_stateSaved   = true;
        }

        InstallCrashRestorer();

        m_nativeHidden.store(true);
        DoHideNativeTaskbar();
        StartHideWatcher();
    } else {
        StopHideWatcher();
        RestoreNativeTaskbarNow();
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

} /* namespace w7t */
