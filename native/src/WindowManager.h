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
 *
 * Logica di grouping/jump-list reimplementata da zero in C++ nativo;
 * ispirata concettualmente al comportamento della Superbar di Windows 7.
 */

#ifndef W7T_WINDOW_MANAGER_H
#define W7T_WINDOW_MANAGER_H

#include "Common.h"
#include <map>
#include <unordered_map>

namespace w7t {

struct TrackedWindow {
    HWND         hwnd         = nullptr;
    DWORD        pid          = 0;
    uint32_t     state        = 0;
    int32_t      monitorIndex = 0;
    uint32_t     iconRevision = 0;
    std::wstring title;
    std::wstring appId;
    std::wstring exePath;
    ArgbBitmap   icon;
    bool         iconLoaded   = false;

    /* Richiesta di attenzione in corso (FlashWindowEx). Non deriva da una
     * proprieta' interrogabile della finestra: Windows la comunica una
     * sola volta con HSHELL_FLASH, quindi va memorizzata qui e azzerata
     * quando l'utente porta la finestra in primo piano. */
    bool         flashing     = false;
};

class WindowManager {
public:
    static WindowManager& Instance();

    bool Start();
    void Stop();

    /* Rilegge l'elenco completo delle finestre. Restituisce il numero di
     * finestre tracciate. */
    int32_t Refresh();

    int32_t GetCount();
    int32_t CopyTo(W7T_WindowInfo* buffer, int32_t capacity);
    bool    GetInfo(HWND hwnd, W7T_WindowInfo* out);

    int32_t GetIconBitmap(HWND hwnd, int32_t desiredSize, int32_t* width, int32_t* height,
                          uint8_t* pixels, int32_t pixelsBytes);

    int32_t ExecuteCommand(HWND hwnd, int32_t cmd);
    int32_t MinimizeGroup(const std::wstring& appId);
    int32_t CloseGroup(const std::wstring& appId);

    bool IsFullScreenAppActive();

    /* Invocato dagli hook WinEvent. */
    void OnWinEvent(DWORD event, HWND hwnd);

    /* Invocato da TrayService quando arriva HSHELL_FLASH: una finestra
     * non attiva sta chiedendo attenzione (FlashWindowEx). */
    void OnWindowFlash(HWND hwnd);

    /* Azzera lo stato di lampeggio: la finestra e' stata aperta. */
    void ClearFlash(HWND hwnd);

private:
    WindowManager() = default;
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    void EnsureIcon(TrackedWindow& win, int32_t desiredSize);
    bool BuildTracked(HWND hwnd, TrackedWindow& out);
    static uint32_t ComputeState(HWND hwnd);

    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM param);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                      LONG idObject, LONG idChild,
                                      DWORD idEventThread, DWORD dwmsEventTime);

    std::recursive_mutex                 m_mutex;
    std::vector<HWND>                    m_order;   /* ordine stabile di inserimento */
    std::unordered_map<HWND, TrackedWindow> m_windows;
    std::vector<HWINEVENTHOOK>           m_hooks;
    bool                                 m_running = false;
};

} /* namespace w7t */

#endif /* W7T_WINDOW_MANAGER_H */
