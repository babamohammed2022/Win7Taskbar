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
#include "ScopeGuards.h"   /* v1.21.51: UniqueWinEventHook */

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

    /// True dopo una Register riuscita e finche' non arriva la Unregister.
    bool IsRegistered() const { return m_registered; }

    /// Notifica ABN_* ricevuta dalla finestra della barra (il messaggio di
    /// callback registrato con ABM_NEW). Restituisce true se gestita.
    bool HandleCallback(uint32_t wParam, int32_t lParam);

    /* ---------------- v1.21.51: guardia "Aero Flip 3D" ----------------
     *
     * CON CHE COSO SI HA A CHE FARE
     * -----------------------------
     * La mod Windhawk "Aero Flip 3D Recreation"
     * (ramensoftware/windhawk-mods, mods/aero-flip3d-recreation.wh.cpp)
     * apre il suo switcher con una finestra overlay a TUTTO SCHERMO sul
     * monitor primario (CreateOverlayWindow: WS_POPUP, WS_EX_TOPMOST |
     * WS_EX_TOOLWINDOW, classe "Flip3DOverlayWndClass"), la alza in cima
     * alla fascia topmost e la porta in primo piano per tutta la durata
     * dell'animazione.
     *
     * Per il protocollo AppBar quella e' una "app a schermo intero": la
     * shell ce lo comunica con ABN_FULLSCREENAPP (MSDN: "Notifies an
     * appbar when a full-screen application is opening or closing") e il
     * vecchio gestore rispondeva nascondendo la barra
     * (ShowWindow(SW_HIDE)): risultato, con la mod attiva la taskbar
     * SPARIVA per tutta l'animazione. La barra vera di Vista/7 durante il
     * Flip 3D restava invece VISTA e inerte.
     *
     * COSA FA LA GUARDIA
     * ------------------
     * Appena si vede la finestra overlay della mod (evento di sistema
     * EVENT_OBJECT_SHOW, oppure la ABN_FULLSCREENAPP stessa) la barra:
     *   1. resta/si fa visibile e si riafferma SOPRA l'overlay (entrambe
     *      sono topmost: SetWindowPos(HWND_TOPMOST) senza attivazione);
     *   2. smette di essere interagibile: EnableWindow(FALSE) toglie
     *      mouse e tastiera (MSDN) e la salta nell'hit-test (MSDN,
     *      WindowFromPoint: "does not retrieve a handle to a hidden or
     *      disabled window"), quindi clic e rotellina cadono
     *      sull'overlay della mod, che continua a rispondere;
     *   3. alla chiusura dell'overlay (EVENT_OBJECT_DESTROY, oppure il
     *      ripiego SW_HIDE della mod, oppure ABN_FULLSCREENAPP con
     *      lParam FALSE, oppure qualunque cambio di primo piano) torna
     *      interagibile. Nessuna via d'uscita puo' lasciare la barra
     *      disabilitata: il rilascio e' idempotente e ripetuto su piu'
     *      segnali indipendenti.
     *
     * Le app a schermo intero VERE (video, giochi) non c'entrano: per
     * loro il comportamento storico (nascondersi) resta esattamente
     * com'era. */
    static void CALLBACK FlipWatchProc(HWINEVENTHOOK hook, DWORD event,
                                       HWND hwnd, LONG idObject, LONG idChild,
                                       DWORD thread, DWORD time);
    static bool LooksLikeFlip3dOverlay(HWND hwnd);
    void StartFlipWatch();
    void StopFlipWatch();
    void EngageFlip3dGuard(HWND overlay);
    void ReleaseFlip3dGuard();

    /// ABM_WINDOWPOSCHANGED: comunica alla shell che il rettangolo della
    /// nostra AppBar e' cambiato (le altre AppBar si riposizionano).
    void NotifyWindowPosChanged(HWND hwnd);

    /// ABM_ACTIVATE: la barra ha ricevuto WM_ACTIVATE (flusso ManagedShell).
    void Activate(HWND hwnd);

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
     * (il motore di RetroBar): stato iniziale salvato e SetWindowPos con
     * HWND_BOTTOM.
     *
     * v2.60: la barra di Explorer che ricompare non si scopre piu' con un
     * ciclo di controllo a 100 ms (era lui a produrre il lampeggio: tre
     * secondi di ri-nascondi mentre Explorer, premendo Start, rimostra la
     * sua barra). Ora il ritorno della barra e' un EVENTO di sistema:
     * SetWinEventHook su EVENT_OBJECT_SHOW / EVENT_SYSTEM_FOREGROUND,
     * filtrato sulle classi della barra, sveglia un thread addormentato
     * che rinasconde. Nessun polling, e il lampo dura quanto un evento. */
    static UINT GetNativeTaskbarState();
    static UINT SetNativeTaskbarState(UINT state);
    void SetNativeTaskbarVisibility(bool hide);
    void DoHideNativeTaskbar();

    /* Work-area reservation. ABM_SETPOS is the documented path; on
     * Windows 11 the XAML taskbar can ignore ABS_AUTOHIDE and restore
     * SPI_GETWORKAREA, so a reversible SPI_SETWORKAREA fallback is used
     * only when the negotiated bar still overlaps the work area. */
    void CaptureOriginalWorkArea(HWND hwnd);
    void EnsureWorkAreaReserved(HWND hwnd, int32_t edge, const RECT& barRect);
    void RestoreWorkArea();
    static bool BarOverlapsWorkArea(const RECT& work, const RECT& bar);

    static void CALLBACK HideWatcherProc(HWINEVENTHOOK hook, DWORD event,
                                         HWND hwnd, LONG idObject, LONG idChild,
                                         DWORD thread, DWORD time);
    void StartHideWatcher();
    void StopHideWatcher();
    void HideWatcherLoop();

    /* v1.21.51 - interni della guardia Flip 3D. Il mutex e' ricorsivo
     * perche' le transizioni chiamano API finestra (SetWindowPos,
     * EnableWindow) che possono rientrare nel WndProc della barra e
     * quindi di nuovo qui (WM_WINDOWPOSCHANGED -> AppBarNotify ->
     * HandleCallback): lo stato viene sempre aggiornato PRIMA delle
     * chiamate che possono rientrare, cosi' il rientro vede la guardia
     * gia' coerente. m_flipGuardActive e' anche uno specchio atomico
     * per i controlli rapidi nei callback ad alta frequenza. */
    void ReassertBarOverFlipOverlay();
    bool FlipGuardOverlayAlive();
    bool FlipGuardOverlayAliveLocked() const;

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
    std::atomic<bool> m_watchRun{false};
    std::thread       m_watchThread;
    HANDLE            m_watchEvent = nullptr;
    HWINEVENTHOOK     m_hideHook   = nullptr;
    HWINEVENTHOOK     m_fgHook     = nullptr;
    int32_t m_edge          = W7T_EDGE_BOTTOM;
    int32_t m_size          = 40;

    bool      m_workAreaCaptured = false;
    bool      m_workAreaOwned    = false;
    RECT      m_savedWorkArea    = {};
    RECT      m_savedMonitorRect = {};
    HMONITOR  m_savedMonitor     = nullptr;

    /* v3.15 - anti ping-pong AppBar (barra che si congela a CPU alta,
     * soprattutto sui bordi verticali):
     *
     * Ogni SetPos terminava con ABM_WINDOWPOSCHANGED, la shell puo'
     * riinviarci ABN_POSCHANGED, e il nostro HandleCallback rieseguiva
     * SetPos: un ciclo chiuso app<->shell, un messaggio ogni passata.
     * Bastano tre regole (le stesse di ManagedShell/RetroBar):
     *   1. SetPos non e' rientrante (m_inSetPos);
     *   2. si notifica ABM_WINDOWPOSCHANGED SOLO se il rettangolo e'
     *      davvero cambiato (m_lastRect) - il rettangolo identico non
     *      deve generare nuovi broadcast;
     *   3. un ABN_POSCHANGED che arriva mentre noi stiamo posando o nei
     *      300 ms dopo e' per definizione l'ECO della nostra stessa
     *      richiesta: si scarta (eco soppresso). */
    bool      m_inSetPos      = false;
    RECT      m_lastRect      = {};
    bool      m_haveLastRect  = false;
    ULONGLONG m_lastSetPosTick = 0;

    /* v1.21.51 - stato della guardia Flip 3D. Gli hook vivono in guardie
     * RAII (UniqueWinEventHook di ScopeGuards.h): qualunque via d'uscita
     * li sgancia, mai un hook orfano che continui a ricevere eventi. */
    std::recursive_mutex m_flipMutex;
    std::atomic<bool>    m_flipGuardActive{false};
    HWND                 m_flipOverlay = nullptr;
    UniqueWinEventHook   m_flipStateHook;      /* DESTROY / SHOW / HIDE  */
    UniqueWinEventHook   m_flipForegroundHook; /* EVENT_SYSTEM_FOREGROUND */
};

} /* namespace w7t */

#endif /* W7T_APPBAR_SERVICE_H */
