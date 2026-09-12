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

#ifndef W7T_APPBAR_SERVICE_H
#define W7T_APPBAR_SERVICE_H

#include "Common.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace w7t {

class AppBarService {
public:
    static AppBarService& Instance();

    int32_t Register(HWND hwnd, int32_t edge, int32_t sizePx);
    int32_t SetPos(HWND hwnd, int32_t edge, int32_t sizePx, RECT* out);
    int32_t Unregister(HWND hwnd);

    int32_t SetNativeTaskbarHidden(bool hidden);
    bool    IsNativeTaskbarHidden() const { return m_nativeHidden; }

    /// Rinasconde la taskbar di Explorer se e' ricomparsa.
    /// Explorer la rimostra da solo in diverse occasioni (apertura del menu
    /// Start, cambio di risoluzione, riavvio della shell): questo metodo e'
    /// pensato per essere richiamato spesso ed e' economico se non serve.
    void    ReassertNativeTaskbarHidden();

    /// Rimostra la barra di Explorer e ne ripristina lo stato salvato, se
    /// era nascosta. Pubblico perche' lo usano anche il filtro di ultima
    /// istanza e atexit(), che vivono fuori dalla classe.
    void    RestoreNativeTaskbarNow();

    static int32_t GetPrimaryWorkArea(RECT* out);

    UINT CallbackMessage() const { return m_callbackMessage; }

private:
    AppBarService() = default;
    AppBarService(const AppBarService&) = delete;
    AppBarService& operator=(const AppBarService&) = delete;

    static HWND FindNativeTaskbar();
    static HWND FindStartOrb();
    static HWND FindSecondaryTaskbar(HWND after);

    /* Meccanismo di nascondimento copiato da ManagedShell/ExplorerHelper
     * (il motore di RetroBar): stato iniziale salvato, SetWindowPos con
     * HWND_BOTTOM, e un monitor che rinasconde la barra di Explorer ogni
     * volta che ricompare. */
    static UINT GetNativeTaskbarState();
    static void SetNativeTaskbarState(UINT state);
    void SetNativeTaskbarVisibility(bool hide);
    void DoHideNativeTaskbar();
    void MonitorLoop();

    /* Ripristina la barra nativa se un fault uccide il processo: gestore
     * vettoriale SENZA longjmp e senza unwind, che rimostra la barra di
     * Explorer prima di lasciare morire il processo. Una barra nascosta
     * dopo un crash e' esattamente la fascia nera che si vedeva. */
    void InstallCrashRestorer();
    static LONG CALLBACK CrashRestorerHandler(PEXCEPTION_POINTERS info);

    HWND m_hwnd             = nullptr;
    UINT m_callbackMessage  = 0;
    bool m_registered       = false;
    std::atomic<bool> m_nativeHidden{false};
    std::mutex m_hideMutex;
    bool m_stateSaved       = false;
    UINT m_startupState     = ABS_ALWAYSONTOP;
    std::atomic<bool> m_monitorRun{false};
    std::thread m_monitor;
    int32_t m_edge          = W7T_EDGE_BOTTOM;
    int32_t m_size          = 40;
};

} /* namespace w7t */

#endif /* W7T_APPBAR_SERVICE_H */
