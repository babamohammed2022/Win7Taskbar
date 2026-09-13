/*
 * Win7Taskbar - Native core - Windows 11 notification area reader
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
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS
 *
 * Windows 7 and Windows 10 keep the notification area in a real Win32
 * toolbar (Shell_TrayWnd -> TrayNotifyWnd -> SysPager -> ToolbarWindow32),
 * and ExplorerTrayReader enumerates its buttons.
 *
 * Windows 11 removed that toolbar: the tray is drawn by XAML
 * (Taskbar.View/Taskbar.dll inside explorer.exe, hosted in the windows
 * TopLevelWindowForOverflowXamlIsland and in the taskbar island). There is
 * no window to enumerate, so on Windows 11 the classic reader legitimately
 * finds nothing and every icon that was already registered before our
 * process started stayed invisible.
 *
 * The one public, documented interface that still describes every tray icon
 * of Windows 11 is UI Automation: the XAML islands expose their elements
 * through the accessibility bridge, and the tray icons are ordinary buttons
 * (ClassName "SystemTray.SystemTrayIcon"). Each element gives us the
 * tooltip text, the owning process and the element identity; the icon
 * bitmap comes from the owner executable (or from our own Windows 7
 * artwork for the system icons we recreate), and clicks are delivered by
 * the element's own Invoke/LegacyIAccessible patterns, so no synthetic
 * input and no registry change is involved.
 *
 * The reader owns a worker thread with its own COM apartment: UIA calls are
 * cross-process and must not run on the taskbar UI thread. Everything is
 * request/response: the caller asks for a snapshot or for a click, the
 * worker answers by posting a message to a window chosen by the caller.
 * ---------------------------------------------------------------------------
 */

#ifndef W7T_WIN11_TRAY_READER_H
#define W7T_WIN11_TRAY_READER_H

#include "Common.h"
#include "TrayFallbackIcons.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace w7t {

/* One tray icon as seen through the accessibility tree. */
struct Win11TrayItem {
    uint32_t     uid     = 0;      /* identity inside our tray model        */
    uint32_t     pid     = 0;      /* owning process                        */
    int          order   = 0;      /* enumeration order in the bar          */
    bool         hidden  = false;  /* true: it lives in the overflow flyout */
    bool         systemOwned = false; /* owner is a shell process           */
    SystemIconKind kind  = SystemIconKind::None;  /* nothing = generic app icon */
    std::wstring token;            /* stable identity (uid is derived from it) */
    std::wstring name;             /* tooltip shown by the shell            */
    std::wstring exePath;          /* owner image (for the generic icon)    */
    ArgbBitmap   bitmap;
};

class Win11TrayReader {
public:
    static Win11TrayReader& Instance();

    /* True when this session uses the Windows 11 tray: Explorer has no
     * Win32 notification toolbar and the taskbar hosts the XAML bridge.
     * Cheap: only window lookups, no COM, safe on any thread. */
    static bool Detect();

    /* Where the worker posts "snapshot ready" (the tray service window). */
    void SetNotify(HWND wnd, UINT message);

    bool Start();
    void Stop();

    /* Asks for a fresh snapshot; returns immediately. */
    void RequestRead();

    /* Copy of the last successful snapshot (empty when the read failed). */
    std::vector<Win11TrayItem> TakeSnapshot();

    /* System icon class of a snapshot entry (Network/Volume/Battery/None). */
    SystemIconKind KindOf(uint32_t uid) const;

    /* Left click (invoke) or right click (context menu) on an entry. */
    bool RequestClick(uint32_t uid, bool rightButton);

    /* Opens the real Windows 11 overflow flyout (our own overflow panel has
     * nothing to show when the hidden icons cannot be enumerated) and places
     * it above the anchor rectangle. */
    bool RequestOverflowFlyout(const RECT& anchor);

    /**
     * Vero se il lettore ha un thread vivo che sta consegnando snapshot.
     *
     * v2.61: serve a NON considerare "lettore avviato" una cosa sola.
     * Prima bastava che EnableWin11Tray() fosse stata chiamata una volta
     * per non riprovare mai piu': se Start() falliva (thread non partito,
     * shell sotto stress all'avvio) la tray restava senza nessuna lettura
     * per sempre, ed e' uno dei motivi per cui le icone comparivano solo
     * dopo molti minuti, se comparivano.
     */
    bool IsRunning() const { return m_started.load() && m_threadId != 0; }

    /**
     * Vero se l'ULTIMA lettura ha davvero attraversato la tray della shell.
     *
     * v2.61: distingue "Explorer non ha risposto / isola non ancora pronta"
     * da "l'utente non ha icone". Serve a decidere se ritentare in backoff:
     * una lettura non valida non deve mai essere interpretata come assenza.
     */
    bool IsLastReadValid() const { return m_lastReadValid.load(); }

private:
    Win11TrayReader() = default;
    Win11TrayReader(const Win11TrayReader&) = delete;
    Win11TrayReader& operator=(const Win11TrayReader&) = delete;

    struct Request;   /* defined in the .cpp */

    void WorkerMain();
    void ReadNow();               /* worker only */
    void HandleClick(const Request& request);
    void HandleOverflow(const Request& request);

    std::thread        m_thread;
    DWORD              m_threadId = 0;
    std::atomic<bool>  m_running{ false };
    std::atomic<bool>  m_started{ false };
    std::atomic<bool>  m_lastReadValid{ false };
    std::atomic<bool>  m_threadDone{ false };

    mutable std::mutex         m_mutex;
    std::vector<Win11TrayItem> m_snapshot;
    HWND                       m_notifyWnd = nullptr;
    UINT                       m_notifyMsg = 0;
};

} /* namespace w7t */

#endif /* W7T_WIN11_TRAY_READER_H */
