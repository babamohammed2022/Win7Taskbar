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

    /* Lavoriamo sul monitor primario: e' dove vive la taskbar principale. */
    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    APPBARDATA abd = {};
    abd.cbSize = sizeof(abd);
    abd.hWnd   = hwnd;
    abd.uEdge  = static_cast<UINT>(edge);

    switch (edge) {
        case W7T_EDGE_TOP:
            abd.rc.left   = 0;
            abd.rc.top    = 0;
            abd.rc.right  = screenWidth;
            abd.rc.bottom = sizePx;
            break;
        case W7T_EDGE_LEFT:
            abd.rc.left   = 0;
            abd.rc.top    = 0;
            abd.rc.right  = sizePx;
            abd.rc.bottom = screenHeight;
            break;
        case W7T_EDGE_RIGHT:
            abd.rc.left   = screenWidth - sizePx;
            abd.rc.top    = 0;
            abd.rc.right  = screenWidth;
            abd.rc.bottom = screenHeight;
            break;
        case W7T_EDGE_BOTTOM:
        default:
            abd.rc.left   = 0;
            abd.rc.top    = screenHeight - sizePx;
            abd.rc.right  = screenWidth;
            abd.rc.bottom = screenHeight;
            break;
    }

    /* ABM_QUERYPOS lascia che il sistema aggiusti il rettangolo tenendo
     * conto delle altre AppBar gia' registrate. */
    SHAppBarMessage(ABM_QUERYPOS, &abd);

    /* Dopo la query ricomponiamo lo spessore richiesto sul bordo scelto. */
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

    if (out != nullptr) {
        *out = abd.rc;
    }
    return W7T_OK;
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
        const DWORD wait = WaitForSingleObject(m_watchEvent, 500);
        if (!m_watchRun.load()) {
            break;
        }
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
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
