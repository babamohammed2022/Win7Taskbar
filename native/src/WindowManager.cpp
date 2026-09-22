/*
 * Win7Taskbar - Core nativo - Superbar / gestione finestre
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

#include "WindowManager.h"
#include "TaskbarButtonNotify.h"
#include <algorithm>
#include <objbase.h>
#include <shlobj.h>

namespace w7t {

WindowManager& WindowManager::Instance() {
    static WindowManager instance;
    return instance;
}

/* ------------------------------------------------------------------ */
/*  Hook WinEvent                                                      */
/* ------------------------------------------------------------------ */

void CALLBACK WindowManager::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                                          LONG idObject, LONG idChild,
                                          DWORD, DWORD) {
    /* Interessano solo gli eventi a livello di finestra. */
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == nullptr) {
        return;
    }
    Instance().OnWinEvent(event, hwnd);
}

bool WindowManager::Start() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_running) {
        return true;
    }

    struct HookRange {
        DWORD min;
        DWORD max;
    };
    const HookRange ranges[] = {
        { EVENT_SYSTEM_FOREGROUND,   EVENT_SYSTEM_FOREGROUND },
        { EVENT_OBJECT_CREATE,       EVENT_OBJECT_HIDE },
        { EVENT_OBJECT_CLOAKED,      EVENT_OBJECT_UNCLOAKED },
        { EVENT_OBJECT_NAMECHANGE,   EVENT_OBJECT_NAMECHANGE },
        { EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND },
    };

    for (const HookRange& range : ranges) {
        HWINEVENTHOOK hook = SetWinEventHook(
            range.min, range.max, nullptr, WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook != nullptr) {
            m_hooks.push_back(hook);
        }
    }

    m_running = true;
    Refresh();
    return !m_hooks.empty();
}

void WindowManager::Stop() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (HWINEVENTHOOK hook : m_hooks) {
        UnhookWinEvent(hook);
    }
    m_hooks.clear();
    m_windows.clear();
    m_order.clear();
    m_running = false;
}

/* v3.6 - CINGHIA DI SICUREZZA. L'utente ha avuto un crash della barra
 * aprendo il Centro connessioni (una finestra CabinetWClass di explorer):
 * il sospetto e' il percorso "nuova finestra" (identita', icona, stato).
 * Ogni evento passa da qui: un fault in quel percorso toglie una finestra
 * dalla barra, non tutta la barra. La funzione con __try non puo' tenere
 * oggetti C++ (C2712): il corpo vero sta in OnWinEventImpl. */
void WindowManager::OnWinEvent(DWORD event, HWND hwnd) {
    __try {
        OnWinEventImpl(event, hwnd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* Niente azioni da un contesto potenzialmente corrotto: il giro
         * di sicurezza periodico rimette in ordine il modello. */
    }
}

void WindowManager::OnWinEventImpl(DWORD event, HWND hwnd) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const bool tracked = m_windows.find(hwnd) != m_windows.end();
    const bool eligible = IsTaskbarWindow(hwnd);

    switch (event) {
        case EVENT_OBJECT_DESTROY:
        case EVENT_OBJECT_HIDE:
        case EVENT_OBJECT_CLOAKED: {
            if (tracked && !eligible) {
                m_windows.erase(hwnd);
                m_order.erase(std::remove(m_order.begin(), m_order.end(), hwnd), m_order.end());
                CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_REMOVED,
                                                 reinterpret_cast<uint64_t>(hwnd), 0);
            }
            break;
        }
        case EVENT_OBJECT_CREATE:
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_UNCLOAKED: {
            if (!tracked && eligible) {
                TrackedWindow win;
                if (BuildTracked(hwnd, win)) {
                    m_windows[hwnd] = std::move(win);
                    m_order.push_back(hwnd);
                    CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_ADDED,
                                                     reinterpret_cast<uint64_t>(hwnd), 0);
                    w7t::NotifyTaskbarButton(hwnd, win.pid);
                }
            }
            break;
        }
        case EVENT_OBJECT_NAMECHANGE: {
            /* v1.21.32: a window published without a title (or with a title
             * that did not satisfy the filter yet) joins the bar the moment
             * its name arrives. This branch used to update ONLY windows that
             * were already tracked, so the button appeared only at the next
             * safety enumeration - up to ten seconds later, or never when no
             * enumeration happened in the meantime. */
            if (!tracked && eligible) {
                TrackedWindow win;
                if (BuildTracked(hwnd, win)) {
                    m_windows[hwnd] = std::move(win);
                    m_order.push_back(hwnd);
                    CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_ADDED,
                                                     reinterpret_cast<uint64_t>(hwnd), 0);
                    w7t::NotifyTaskbarButton(hwnd, win.pid);
                }
                break;
            }
            if (tracked) {
                wchar_t title[W7T_MAX_TITLE] = {};
                GetWindowTextW(hwnd, title, W7T_MAX_TITLE);
                TrackedWindow& win = m_windows[hwnd];
                if (win.title != title) {
                    win.title = title;

                    /* CabinetWClass can navigate in-place from an ordinary
                     * folder to Control Panel (and back). Re-evaluate only on
                     * navigation/title changes so that a reused Explorer HWND
                     * cannot retain the old taskbar identity or icon. */
                    const std::wstring appId =
                        ComputeAppId(hwnd, win.pid, win.exePath);
                    if (win.appId != appId) {
                        win.appId = appId;
                        win.icon.clear();
                        win.iconLoaded = false;
                    }

                    CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_CHANGED,
                                                     reinterpret_cast<uint64_t>(hwnd), 0);
                }
            }
            break;
        }
        case EVENT_SYSTEM_FOREGROUND: {
            /* Aggiorna lo stato attivo di tutte le finestre tracciate. */
            for (auto& entry : m_windows) {
                uint32_t state = ComputeState(entry.first);

                /* Una finestra portata in primo piano smette di chiedere
                 * attenzione: l'utente l'ha vista. Per tutte le altre il
                 * flag va riportato, perche' ComputeState lo azzererebbe. */
                if ((state & W7T_WS_ACTIVE) != 0) {
                    entry.second.flashing = false;
                } else if (entry.second.flashing) {
                    state |= W7T_WS_FLASHING;
                }

                entry.second.state = state;
            }
            if (!tracked && eligible) {
                TrackedWindow win;
                if (BuildTracked(hwnd, win)) {
                    m_windows[hwnd] = std::move(win);
                    m_order.push_back(hwnd);
                    CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_ADDED,
                                                     reinterpret_cast<uint64_t>(hwnd), 0);
                    w7t::NotifyTaskbarButton(hwnd, win.pid);
                }
            }
            CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_ACTIVATED,
                                             reinterpret_cast<uint64_t>(hwnd), 0);
            break;
        }
        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_SYSTEM_MINIMIZEEND: {
            if (tracked) {
                auto& win = m_windows[hwnd];
                win.state = ComputeState(hwnd)
                          | (win.flashing ? W7T_WS_FLASHING : 0u);
                CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_CHANGED,
                                                 reinterpret_cast<uint64_t>(hwnd), 0);
            }
            break;
        }
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  Enumerazione                                                       */
/* ------------------------------------------------------------------ */

void WindowManager::OnWindowFlash(HWND hwnd) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_windows.find(hwnd);
    if (it == m_windows.end()) {
        return;
    }

    /* Una finestra gia' in primo piano non lampeggia: Windows invia
     * comunque HSHELL_FLASH, ma mostrarlo sarebbe solo rumore. */
    if ((ComputeState(hwnd) & W7T_WS_ACTIVE) != 0) {
        return;
    }

    if (it->second.flashing) {
        return; /* gia' segnalata */
    }

    it->second.flashing = true;
    it->second.state |= W7T_WS_FLASHING;

    CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_FLASH,
                                     reinterpret_cast<uint64_t>(hwnd), 0);
}

void WindowManager::ClearFlash(HWND hwnd) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_windows.find(hwnd);
    if (it == m_windows.end() || !it->second.flashing) {
        return;
    }

    it->second.flashing = false;
    it->second.state &= ~static_cast<uint32_t>(W7T_WS_FLASHING);

    CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_CHANGED,
                                     reinterpret_cast<uint64_t>(hwnd), 0);
}

/* v1.21.32: taskbar-list protocol (ITaskbarList::AddTab/DeleteTab), served
 * by TrayService on the window that answers WM_USER + 236. The real body
 * sits under the SEH strap because it touches the identity and the style of
 * a window owned by somebody else. */
bool WindowManager::ApplyTaskbarListCall(HWND hwnd, bool add) {
    __try {
        return ApplyTaskbarListCallImpl(hwnd, add);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WindowManager::ApplyTaskbarListCallImpl(HWND hwnd, bool add) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }

    /* The request also holds for the enumerations that follow: it is the
     * registry IsTaskbarWindow consults. */
    SetTaskbarListOverride(hwnd, add ? 1 : -1);

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    auto it = m_windows.find(hwnd);
    if (add) {
        if (it == m_windows.end() && IsTaskbarWindow(hwnd)) {
            TrackedWindow win;
            if (BuildTracked(hwnd, win)) {
                m_windows[hwnd] = std::move(win);
                m_order.push_back(hwnd);
                CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_ADDED,
                                                 reinterpret_cast<uint64_t>(hwnd), 0);
                w7t::NotifyTaskbarButton(hwnd, win.pid);
                return true;
            }
        }
        return false;
    }

    if (it != m_windows.end()) {
        m_windows.erase(it);
        m_order.erase(std::remove(m_order.begin(), m_order.end(), hwnd),
                      m_order.end());
        CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_REMOVED,
                                         reinterpret_cast<uint64_t>(hwnd), 0);
        return true;
    }
    return false;
}

uint32_t WindowManager::ComputeState(HWND hwnd) {
    uint32_t state = 0;
    if (GetForegroundWindow() == hwnd) {
        state |= W7T_WS_ACTIVE;
    }
    if (IsIconic(hwnd)) {
        state |= W7T_WS_MINIMIZED;
    }
    if (IsZoomed(hwnd)) {
        state |= W7T_WS_MAXIMIZED;
    }
    return state;
}

bool WindowManager::BuildTracked(HWND hwnd, TrackedWindow& out) {
    if (!IsTaskbarWindow(hwnd)) {
        return false;
    }

    wchar_t title[W7T_MAX_TITLE] = {};
    GetWindowTextW(hwnd, title, W7T_MAX_TITLE);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    out.hwnd         = hwnd;
    out.pid          = pid;
    out.title        = title;
    out.exePath      = GetProcessImagePath(pid);
    out.appId        = ComputeAppId(hwnd, pid, out.exePath);
    out.state        = ComputeState(hwnd);
    out.monitorIndex = GetMonitorIndexForWindow(hwnd);
    out.iconRevision = 0;
    out.iconLoaded   = false;
    return true;
}

BOOL CALLBACK WindowManager::EnumProc(HWND hwnd, LPARAM param) {
    auto* found = reinterpret_cast<std::vector<HWND>*>(param);
    if (IsTaskbarWindow(hwnd)) {
        found->push_back(hwnd);
    }
    return TRUE;
}

int32_t WindowManager::Refresh() {
    /* v3.6: anche il giro di enumeration passa dalla cinghia (vedi
     * OnWinEvent): BuildTracked tocca identita' e icone di finestre
     * estranee, meglio non farlo cadere addosso al processo. */
    __try {
        return RefreshImpl();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int32_t WindowManager::RefreshImpl() {
    /* v1.21.32: dead windows must not leave entries behind in the override
     * registry of the taskbar-list protocol. */
    PruneTaskbarOverrides();

    std::vector<HWND> found;
    found.reserve(64);
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&found));

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    /* Rimuovi le finestre sparite. */
    for (auto it = m_windows.begin(); it != m_windows.end();) {
        if (std::find(found.begin(), found.end(), it->first) == found.end()) {
            HWND dead = it->first;
            it = m_windows.erase(it);
            m_order.erase(std::remove(m_order.begin(), m_order.end(), dead), m_order.end());
            CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_REMOVED,
                                             reinterpret_cast<uint64_t>(dead), 0);
        } else {
            ++it;
        }
    }

    /* Aggiungi le nuove e aggiorna quelle esistenti. */
    for (HWND hwnd : found) {
        auto it = m_windows.find(hwnd);
        if (it == m_windows.end()) {
            TrackedWindow win;
            if (BuildTracked(hwnd, win)) {
                m_windows[hwnd] = std::move(win);
                m_order.push_back(hwnd);
                CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_ADDED,
                                                 reinterpret_cast<uint64_t>(hwnd), 0);
                w7t::NotifyTaskbarButton(hwnd, win.pid);
            }
        } else {
            wchar_t title[W7T_MAX_TITLE] = {};
            GetWindowTextW(hwnd, title, W7T_MAX_TITLE);
            const uint32_t state = ComputeState(hwnd);
            const bool titleChanged = it->second.title != title;

            /* v3.7: la finestra CabinetWClass del Pannello di controllo
             * compare spesso con titolo e classe già corretti fin dalla
             * creazione (EVENT_OBJECT_CREATE), ma AppUserModelID e
             * RelaunchCommand vengono pubblicati da Explorer con un
             * piccolo ritardo asincrono rispetto al titolo. Se a quel
             * primo istante ComputeAppId non trova ancora la proprietà,
             * la finestra resta erroneamente identificata come Explorer
             * per sempre, perché qui sotto si ricalcolava l'identità SOLO
             * quando cambiava il titolo (che a quel punto non cambia più).
             * Per le finestre explorer.exe si ricontrolla quindi ad ogni
             * refresh finché non si stabilizza su un'identità diversa da
             * quella "grezza" di Explorer, così l'icona/il raggruppamento
             * "Pannello di controllo" arriva anche quando il titolo era
             * già corretto al primo giro. */
            const bool isExplorerHost =
                it->second.exePath.size() >= 12 &&
                _wcsicmp(it->second.exePath.c_str() +
                             it->second.exePath.size() - 12,
                         L"explorer.exe") == 0;

            bool identityChanged = false;
            if (titleChanged || isExplorerHost) {
                const std::wstring appId =
                    ComputeAppId(hwnd, it->second.pid, it->second.exePath);
                if (it->second.appId != appId) {
                    it->second.appId = appId;
                    it->second.icon.clear();
                    it->second.iconLoaded = false;
                    identityChanged = true;
                }
            }
            if (titleChanged || identityChanged || it->second.state != state) {
                it->second.title = title;
                it->second.state = state;
                CoreState::Instance().QueueEvent(W7T_EVT_WINDOW_CHANGED,
                                                 reinterpret_cast<uint64_t>(hwnd), 0);
            }
        }
    }

    return static_cast<int32_t>(m_windows.size());
}

int32_t WindowManager::GetCount() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return static_cast<int32_t>(m_windows.size());
}

int32_t WindowManager::CopyTo(W7T_WindowInfo* buffer, int32_t capacity) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const int32_t total = static_cast<int32_t>(m_windows.size());
    if (buffer == nullptr) {
        return total;
    }
    if (capacity < total) {
        return W7T_ERR_BUFFER_TOO_SMALL;
    }

    int32_t index = 0;
    for (HWND hwnd : m_order) {
        auto it = m_windows.find(hwnd);
        if (it == m_windows.end()) {
            continue;
        }
        const TrackedWindow& win = it->second;
        W7T_WindowInfo& info = buffer[index++];
        ZeroMemory(&info, sizeof(info));
        info.hwnd         = reinterpret_cast<uint64_t>(win.hwnd);
        info.processId    = win.pid;
        info.state        = win.state;
        info.monitorIndex = win.monitorIndex;
        info.iconRevision = win.iconRevision;
        CopyToFixed(info.title,   W7T_MAX_TITLE, win.title);
        CopyToFixed(info.appId,   W7T_MAX_APPID, win.appId);
        CopyToFixed(info.exePath, W7T_MAX_PATH_, win.exePath);
    }
    return index;
}

bool WindowManager::GetInfo(HWND hwnd, W7T_WindowInfo* out) {
    if (out == nullptr) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_windows.find(hwnd);
    if (it == m_windows.end()) {
        return false;
    }
    const TrackedWindow& win = it->second;
    ZeroMemory(out, sizeof(*out));
    out->hwnd         = reinterpret_cast<uint64_t>(win.hwnd);
    out->processId    = win.pid;
    out->state        = win.state;
    out->monitorIndex = win.monitorIndex;
    out->iconRevision = win.iconRevision;
    CopyToFixed(out->title,   W7T_MAX_TITLE, win.title);
    CopyToFixed(out->appId,   W7T_MAX_APPID, win.appId);
    CopyToFixed(out->exePath, W7T_MAX_PATH_, win.exePath);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Icone                                                              */
/* ------------------------------------------------------------------ */

void WindowManager::EnsureIcon(TrackedWindow& win, int32_t desiredSize) {
    if (win.iconLoaded && !win.icon.empty()) {
        return;
    }

    const bool large = desiredSize > 16;
    HICON icon = nullptr;
    bool destroyIcon = false;

    /* Explorer-hosted Control Panel pages often expose Explorer's window
     * icon as well as its AUMID. Once ComputeAppId has positively identified
     * such a page, ask the canonical shell namespace for its own icon. This
     * is deliberately identity-gated, so ordinary Explorer folders retain
     * their existing icon path. */
    if (_wcsicmp(win.appId.c_str(), L"w7t:control-panel") == 0) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(
                L"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}",
                nullptr, &pidl, 0, nullptr)) && pidl != nullptr) {
            SHFILEINFOW sfi{};
            const UINT flags = SHGFI_PIDL | SHGFI_ICON |
                (large ? SHGFI_LARGEICON : SHGFI_SMALLICON);
            if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &sfi,
                               sizeof(sfi), flags) != 0) {
                icon = sfi.hIcon;
                destroyIcon = icon != nullptr;
            }
            CoTaskMemFree(pidl);
        }
    }

    /* WM_GETICON con timeout: una finestra bloccata non deve bloccare noi. */
    DWORD_PTR result = 0;
    const UINT type = large ? ICON_BIG : ICON_SMALL;
    if (icon == nullptr &&
        SendMessageTimeoutW(win.hwnd, WM_GETICON, type, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result) && result != 0) {
        icon = reinterpret_cast<HICON>(result);
    }

    if (icon == nullptr) {
        if (SendMessageTimeoutW(win.hwnd, WM_GETICON, large ? ICON_SMALL : ICON_BIG, 0,
                                SMTO_ABORTIFHUNG | SMTO_BLOCK, 250, &result) && result != 0) {
            icon = reinterpret_cast<HICON>(result);
        }
    }

    if (icon == nullptr) {
        icon = reinterpret_cast<HICON>(
            GetClassLongPtrW(win.hwnd, large ? GCLP_HICON : GCLP_HICONSM));
    }
    if (icon == nullptr) {
        icon = reinterpret_cast<HICON>(
            GetClassLongPtrW(win.hwnd, large ? GCLP_HICONSM : GCLP_HICON));
    }

    /* v1.7.2: icone reali delle app PACCHETTIZZATE (UWP). Le loro
     * finestre vivono in ApplicationFrameHost.exe: WM_GETICON e la classe
     * consegnano il glifo generico dell'host. Se l'host e' quello (o se
     * non e' arrivata nessuna icona) si chiede l'icona al pacchetto via
     * AppUserModelID + cartella shell:AppsFolder (API pubbliche). */
    {
        const size_t slash = win.exePath.find_last_of(L"\\/");
        const std::wstring exeName = (slash == std::wstring::npos)
            ? win.exePath : win.exePath.substr(slash + 1);
        const bool hostedFrame =
            _wcsicmp(exeName.c_str(), L"applicationframehost.exe") == 0;
        if (icon == nullptr || hostedFrame) {
            HICON packaged = GetWindowPackagedIcon(win.hwnd, large ? 48 : 32);
            if (packaged != nullptr) {
                icon = packaged;
                destroyIcon = true;
            }
        }
    }

    /* Fallback finale: icona associata all'eseguibile. */
    if (icon == nullptr && !win.exePath.empty()) {
        HICON extracted = nullptr;
        if (large) {
            ExtractIconExW(win.exePath.c_str(), 0, &extracted, nullptr, 1);
        } else {
            ExtractIconExW(win.exePath.c_str(), 0, nullptr, &extracted, 1);
        }
        if (extracted != nullptr) {
            icon = extracted;
            destroyIcon = true;
        }
    }

    /* Ultima spiaggia: l'icona generica di applicazione.
     *
     * Windows non lascia mai un pulsante vuoto nella barra; capita con le
     * finestre che non definiscono alcuna icona ne' a livello di finestra
     * ne' di classe (tipicamente programmi minimali o scritti a mano).
     * Senza questo ripiego il pulsante resterebbe un rettangolo vuoto. */
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
        /* Le icone predefinite sono condivise: non vanno distrutte. */
    }

    if (icon != nullptr) {
        ArgbBitmap bmp;
        if (IconToArgb(icon, bmp) && BitmapSane(bmp)) {
            win.icon = std::move(bmp);
            win.iconRevision++;
        }
        if (destroyIcon) {
            DestroyIcon(icon);
        }
    }
    win.iconLoaded = true;
}

int32_t WindowManager::GetIconBitmap(HWND hwnd, int32_t desiredSize,
                                     int32_t* width, int32_t* height,
                                     uint8_t* pixels, int32_t pixelsBytes) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_windows.find(hwnd);
    if (it == m_windows.end()) {
        return W7T_ERR_NOT_FOUND;
    }
    EnsureIcon(it->second, desiredSize);
    return EmitBitmap(it->second.icon, width, height, pixels, pixelsBytes);
}

/* ------------------------------------------------------------------ */
/*  Comandi finestra (jump-list)                                       */
/* ------------------------------------------------------------------ */

namespace {
/* v2.30: sblocco del foreground-lock nello stesso modo usato dai
 * progetti taskbar open source (ExplorerPatcher/RetroBar): un tap del
 * tasto Alt fa credere al sistema che l'utente abbia appena premuto un
 * tasto, condizione che consente SetForegroundWindow da un processo non
 * attivo. Serve per i programmi minimizzati che "non si aprono". */
void ForegroundUnlock() {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_MENU;
    SendInput(1, &in, sizeof(INPUT));
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}
} /* namespace */

int32_t WindowManager::ExecuteCommand(HWND hwnd, int32_t cmd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return W7T_ERR_NOT_FOUND;
    }

    switch (cmd) {
        case W7T_CMD_RESTORE:
            ShowWindow(hwnd, SW_RESTORE);
            ForegroundUnlock();
            SetForegroundWindow(hwnd);
            BringWindowToTop(hwnd);
            return W7T_OK;

        case W7T_CMD_MINIMIZE:
            ShowWindow(hwnd, SW_MINIMIZE);
            return W7T_OK;

        case W7T_CMD_MAXIMIZE:
            ShowWindow(hwnd, SW_MAXIMIZE);
            SetForegroundWindow(hwnd);
            return W7T_OK;

        case W7T_CMD_CLOSE:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return W7T_OK;

        case W7T_CMD_MOVE:
            /* Come Windows 7: attiva e avvia il move modale del menu di sistema. */
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_MOVE, 0);
            return W7T_OK;

        case W7T_CMD_SIZE:
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_SIZE, 0);
            return W7T_OK;

        case W7T_CMD_ACTIVATE: {
            /* Toggle in stile Superbar: se e' gia' in primo piano, minimizza. */
            if (GetForegroundWindow() == hwnd) {
                ShowWindow(hwnd, SW_MINIMIZE);
                return W7T_OK;
            }
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }

            ForegroundUnlock();   /* v2.30: vedi sopra */

            /* AttachThreadInput consente il cambio di foreground anche quando
             * il nostro processo non e' quello attivo. */
            const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
            const DWORD currentThread = GetCurrentThreadId();
            bool attached = false;
            if (targetThread != currentThread) {
                attached = AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
            }
            SetForegroundWindow(hwnd);
            BringWindowToTop(hwnd);
            if (attached) {
                AttachThreadInput(currentThread, targetThread, FALSE);
            }
            return W7T_OK;
        }

        default:
            return W7T_ERR_INVALID_ARG;
    }
}

int32_t WindowManager::MinimizeGroup(const std::wstring& appId) {
    std::vector<HWND> targets;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const auto& entry : m_windows) {
            if (entry.second.appId == appId) {
                targets.push_back(entry.first);
            }
        }
    }
    for (HWND hwnd : targets) {
        ShowWindow(hwnd, SW_MINIMIZE);
    }
    return static_cast<int32_t>(targets.size());
}

int32_t WindowManager::CloseGroup(const std::wstring& appId) {
    std::vector<HWND> targets;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const auto& entry : m_windows) {
            if (entry.second.appId == appId) {
                targets.push_back(entry.first);
            }
        }
    }
    for (HWND hwnd : targets) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return static_cast<int32_t>(targets.size());
}

/* ------------------------------------------------------------------ */
/*  Rilevamento fullscreen (per nascondere la barra)                   */
/* ------------------------------------------------------------------ */

bool WindowManager::IsFullScreenAppActive() {
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }

    /* Il desktop e la shell non contano come fullscreen. */
    if (foreground == GetDesktopWindow() || foreground == GetShellWindow()) {
        return false;
    }

    RECT windowRect = {};
    if (!GetWindowRect(foreground, &windowRect)) {
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }

    return windowRect.left   <= mi.rcMonitor.left
        && windowRect.top    <= mi.rcMonitor.top
        && windowRect.right  >= mi.rcMonitor.right
        && windowRect.bottom >= mi.rcMonitor.bottom;
}

} /* namespace w7t */
