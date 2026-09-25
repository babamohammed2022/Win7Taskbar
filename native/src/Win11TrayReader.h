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
 * Windows 11 normally draws the visible tray through XAML
 * (Taskbar.View/Taskbar.dll inside explorer.exe, hosted in the taskbar
 * island and, for overflow, TopLevelWindowForOverflowXamlIsland). A legacy
 * toolbar may remain as a hidden compatibility layer, but it is not a
 * complete or stable source on those builds; the classic reader can
 * legitimately find no existing icon and a process-start snapshot must not
 * assume that an empty toolbar means an empty tray.
 *
 * UI Automation is the only documented, out-of-process observation path
 * that can expose parts of those XAML islands. It is not a Microsoft contract
 * for a complete notification-area enumerator: the provider may omit an icon,
 * report no name, or disappear while Explorer rebuilds the island. The reader
 * therefore treats UIA as a best-effort snapshot, never as proof from one
 * read that an absent element was deleted. Registration/update/delete traffic is handled
 * separately by the real Shell_TrayWnd shim through the legacy shell protocol
 * where that protocol is available; that protocol is explicitly non-public
 * and is documented as a compatibility fallback in TrayService.cpp.
 *
 * XAML elements normally expose a SystemTray.NotifyIconView or
 * SystemTray.IconView class. CurrentProcessId is the provider process
 * (normally explorer.exe), not the application that registered the icon, so
 * this reader does not pretend it knows an owner HWND or HICON. It uses the
 * element's Invoke/LegacyIAccessible patterns for clicks, a generic bitmap
 * when no public bitmap is exposed, and Windows 7 artwork only for known
 * system icons recreated by the product.
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
    uint32_t     pid     = 0;      /* processo del provider UIA, non owner */
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

    /* True when this session exposes a Windows 11 XAML taskbar bridge.
     * A legacy toolbar may coexist on some builds; its presence does not
     * disable this reader. Cheap: only window lookups, no COM. */
    static bool Detect();

    /* Where the worker posts "snapshot ready" (the tray service window). */
    void SetNotify(HWND wnd, UINT message);

    bool Start();
    void Stop();

    /* Asks for a fresh snapshot; returns immediately. */
    void RequestRead();

    /* Copy of the last successful snapshot. Check IsLastReadValid() before
     * treating it as the result of the most recent request. */
    std::vector<Win11TrayItem> TakeSnapshot();

    /* System icon class of a snapshot entry (Network/Volume/Battery/None). */
    SystemIconKind KindOf(uint32_t uid) const;

    /* Left click (invoke) or right click (context menu) on an entry. */
    bool RequestClick(uint32_t uid, bool rightButton);

    /* v2.63: uid della prima icona di sistema di quel tipo che la shell
     * espone (0 se non c'e'). Serve a inoltrare il clic al pulsante VERO
     * della shell quando una delle nostre icone ricreate deve aprire un
     * riquadro Win32 di Windows (batteria: e' la via di ExplorerPatcher).
     * L'icona puo' benissimo non essere nel modello: il filtro delle icone
     * di sistema avviene dopo, nel servizio della tray. */
    uint32_t UidOfKind(SystemIconKind kind) const;

    /* Opens the real Windows 11 overflow flyout (our own overflow panel has
     * nothing to show when the hidden icons cannot be enumerated) and places
     * it above the anchor rectangle. */
    /* Apre il flyout reale della shell. silent=true serve alla raccolta
     * periodica: il lettore lo porta fuori schermo, attraversa l'isola UIA
     * materializzata e lo richiude senza mostrare il pannello all'utente. */
    bool RequestOverflowFlyout(const RECT& anchor, bool silent = false);

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
    bool IsRunning() const {
        return m_started.load() && m_threadId.load() != 0;
    }

    /**
     * Vero se l'ULTIMA lettura ha davvero attraversato la tray della shell.
     *
     * v2.61: distingue "Explorer non ha risposto / isola non ancora pronta"
     * da "l'utente non ha icone". Serve a decidere se ritentare in backoff:
     * una lettura non valida non deve mai essere interpretata come assenza.
     */
    bool IsLastReadValid() const { return m_lastReadValid.load(); }

    /* Vero se l'ultima lettura ha attraversato almeno una radice della barra
     * principale. Un'eventuale lettura valida della sola isola overflow non
     * autorizza a rimuovere le voci visibili della barra. */
    bool IsLastReadMainValid() const { return m_lastReadMainValid.load(); }

    /* Vero se l'ultima lettura ha anche attraversato l'isola dell'overflow.
     * La finestra XAML dell'overflow puo' non esistere finche' il flyout non
     * viene aperto: in quel caso una fotografia valida della barra principale
     * non autorizza a cancellare dal nostro modello le voci nascoste viste in
     * una passata precedente. */
    bool IsLastReadOverflowValid() const {
        return m_lastReadOverflowValid.load();
    }

private:
    /* v2.64: constructor/destructor live in Win11TrayReaderResilience.cpp so
     * the reader can install a small, best-effort Explorer/XAML rebuild hook
     * only when the singleton is actually created (not from DllMain). */
    Win11TrayReader();
    ~Win11TrayReader();
    Win11TrayReader(const Win11TrayReader&) = delete;
    Win11TrayReader& operator=(const Win11TrayReader&) = delete;

    struct Request;   /* defined in the .cpp */

    void WorkerMain();
    void ReadNow();               /* worker only */
    void HandleClick(const Request& request);
    void HandleOverflow(const Request& request);

    std::thread        m_thread;
    std::atomic<DWORD> m_threadId{ 0 };
    std::atomic<bool>  m_running{ false };
    std::atomic<bool>  m_started{ false };
    std::atomic<bool>  m_lastReadValid{ false };
    std::atomic<bool>  m_lastReadMainValid{ false };
    std::atomic<bool>  m_lastReadOverflowValid{ false };
    std::atomic<bool>  m_threadDone{ false };

    mutable std::mutex         m_mutex;
    std::vector<Win11TrayItem> m_snapshot;
    HWND                       m_notifyWnd = nullptr;
    UINT                       m_notifyMsg = 0;
};

} /* namespace w7t */

#endif /* W7T_WIN11_TRAY_READER_H */
