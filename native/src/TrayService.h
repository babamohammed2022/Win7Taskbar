/*
 * Win7Taskbar - Core nativo - System tray (server Shell_TrayWnd)
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
 * Reimplementazione completa dell'area di notifica: registriamo noi le
 * window class "Shell_TrayWnd" / "TrayNotifyWnd" e riceviamo direttamente
 * i WM_COPYDATA che shell32!Shell_NotifyIconW invia alla shell. Ogni
 * SHELLTRAYDATA (dwData == 1) viene prima applicato al modello locale e poi
 * inoltrato alla Shell_TrayWnd reale di Explorer, esclusa questa finestra,
 * così la tray nativa non perde le registrazioni.
 *
 * Il modello dei pulsanti vive in un ToolbarWindow32 reale (TrayToolbar.*):
 * ordine, TBSTATE_HIDDEN per l'overflow e rettangoli a schermo nascono dai
 * messaggi TB_*, come nella shell. La riconciliazione con la tray vera di
 * Explorer (icone gia' presenti all'avvio, icone di sistema aggiornate
 * dentro la sua toolbar) e' GUIDATA DAGLI EVENTI:
 *
 *   - WM_COPYDATA delle applicazioni        (NIM_ADD/MODIFY/DELETE);
 *   - WM_POWERBROADCAST e Power Settings    (AC/DC, percentuale);
 *   - Network List Manager (Advise)         (rete su/giu');
 *   - RegNotifyChangeKeyValue su Explorer   (EnableAutoTray, preferenze);
 *   - EVENT_OBJECT_DESTROY via WinEventHook (processo proprietario morto);
 *   - TaskbarCreated                        (Explorer ripartito);
 *   - passata di sicurezza a 30 s           (solo diff, senza catture).
 *
 * Niente tempeste di rilettura: la cattura dei pixel (PrintWindow sulla
 * toolbar altrui) avviene solo per le icone di cui qualcosa e' cambiato, e
 * la revisione del bitmap aumenta solo se l'hash dei pixel cambia davvero.
 * Le icone appartenute a Explorer seguono lo stato reale della SUA
 * toolbar con isteresi a due letture (niente saltelli su letture
 * transitorie) e spariscono solo dopo due assenze consecutive confermate
 * da una lettura VALIDA: una lettura fallita non cancella mai nulla.
 */

#ifndef W7T_TRAY_SERVICE_H
#define W7T_TRAY_SERVICE_H

#include "Common.h"
#include "TrayFallbackIcons.h"
#include "TrayNotifyReader.h"
#include "../include/RaiiWrappers.h"
#include <thread>
#include <atomic>
#include <map>
#include <set>
#include <vector>

namespace w7t {

/* Chiave di fallback dell'icona: (finestra proprietaria, uid).
 * Quando NIF_GUID e' presente, m_byGuid/guidItem è l'identità primaria e
 * questa coppia viene riagganciata alla nuova registrazione. */
struct TrayIconKey {
    uint64_t ownerHwnd;
    uint32_t uid;

    bool operator<(const TrayIconKey& other) const {
        if (ownerHwnd != other.ownerHwnd) {
            return ownerHwnd < other.ownerHwnd;
        }
        return uid < other.uid;
    }
};

struct TrayIconEntry {
    TrayIconKey  key             = { 0, 0 };
    uint32_t     callbackMessage = 0;
    uint32_t     state           = 0;
    uint32_t     stateMask       = 0;
    uint32_t     version         = 0;
    uint32_t     iconRevision    = 0;
    bool         isPinned        = true;
    std::wstring tooltip;
    std::wstring guidKey;
    /* GUID reale dichiarato con NIF_GUID. guidKey resta l'indice testuale
     * compatibile con le voci sintetiche/UIA; questo campo conserva invece
     * l'identificatore wire originale per la diagnostica e il riaggancio. */
    GUID         guidItem       = {};
    bool         hasGuid        = false;
    /* Stato osservato dall'interfaccia TrayNotify privata. E' metadato e
     * fallback iniziale, non sostituisce la fotografia visuale della toolbar
     * o dell'overflow reale. */
    int32_t      shellPreference = -1;
    bool         fromTrayNotify  = false;
    std::wstring lastUpdateSource;
    /* Percorso exe del proprietario al momento della registrazione:
     * metadato diagnostico e supporto all'associazione della shell; non è
     * una chiave di identità né una regola di deduplicazione. */
    std::wstring ownerPath;
    ArgbBitmap   bitmap;

    /* --- sincronizzazione 1:1 con la shell (modello a riconciliazione) --- */
    bool     fromExplorer    = false;  /* nata dalla lettura della toolbar  */
    bool     fromCopyData    = false;  /* registrazione live dell'app        */
    bool     fromWin11Uia    = false;  /* v2.60: nata dalla lettura UI
                                        * Automation della tray di Windows 11
                                        * (nessun HWND proprietario: la chiave
                                        * e' (0, uid sintetico))              */
    bool     ownerIsExplorer = false;  /* il proprietario e' explorer.exe:
                                        * le sue uniche fonti di aggiornamento
                                        * sono la toolbar e gli eventi di
                                        * sistema, mai i copia-dati          */
    uint64_t pixelHash       = 0;      /* hash dell'ultimo bitmap consegnato */
    int     hiddenPending    = 0;      /* isteresi: letture concordi prima
                                        * di adottare un cambio di stato      */
    bool    hiddenDesired    = false;  /* stato visto all'ultima passata    */
    int     missCount        = 0;      /* assenze consecutive da Explorer   */
    bool    lastReadFailed   = false;  /* l'ultima lettura non era valida    */
    uint32_t toolbarId       = 0;      /* idCommand del pulsante reale      */

    /* Persisted explicit visibility choice for this icon:
     * -1 never chosen, 0 show icon and notifications, 1 only show
     * notifications (icon lives in the overflow), 2 hide icon and
     * notifications. Lives in TrayPrefsStore (a file, never the
     * registry) and outranks everything except the "Always show all
     * icons" checkbox. */
    int32_t userBehavior = -1;

    /* v2.38 punto 3: isteresi anti-sfarfallio sull'icona di RETE. Un
     * cambio di pixel (es. roaming Wi-Fi tra access point) si adotta solo
     * dopo kConfirmReads letture concordi, stesso principio di
     * hiddenPending. Evita falsi lampeggi su disconnessioni lampo. */
    bool    netChecked       = false;  /* isNetwork gia' risolto via psapi  */
    bool    isNetwork        = false;  /* proprietario = pnidui.dll         */
    uint64_t netPendingHash  = 0;      /* hash candidato non ancora accolto */
    int     netPendingCount  = 0;      /* letture concordi del candidato    */

    /* v2.59: ICONA DI RIPIEGO. Alcune macchine non lasciano leggere la
     * bitmap delle icone di sistema (CopyIcon rifiutata, cattura fallita) e
     * il posto restava vuoto. Per rete/volume/batteria, riconosciute dal
     * proprietario (GUID della shell o modulo), si usa un'icona nostra che
     * segue lo stato corrente. Il ripiego entra SOLO quando manca la
     * bitmap vera e viene abbandonato appena ne arriva una leggibile:
     * `usingFallback` dice quale delle due sta disegnando il modello. */
    bool           sysChecked    = false;
    SystemIconKind systemKind    = SystemIconKind::None;
    bool           usingFallback = false;

    /* v2.61: la voce NON viene dalla shell ma e' stata ricreata da noi
     * perche' la tray di Windows 11 non espone quel tipo (volume, rete,
     * o batteria dove la shell non la mostra). Il clic non ha nessun
     * elemento UI Automation da invocare: apre direttamente il riquadro
     * nativo del tipo. */
    SystemIconKind syntheticKind = SystemIconKind::None;

    /* Quando UIA vede la stessa voce gia' acquisita da TrayNotify, il suo
     * runtime id resta un alias per il clic e non crea una seconda cache.
     * Zero significa che non esiste un alias UIA. */
    uint32_t     uiaUid         = 0;
};

/* v2.7: istantanea delle icone non fissate per il pannello overflow nativo. */
struct OverflowSnapshot {
    TrayIconKey  key{};
    ArgbBitmap   bitmap;
    std::wstring tooltip;
};

/* Fonti che possono richiedere una passata di riconciliazione. */
enum TrayReconcileSource : uint32_t {
    kReconcileInitial   = 1u << 0,   /* avvio / Explorer ripartito        */
    kReconcilePower     = 1u << 1,   /* WM_POWERBROADCAST / Power Setting */
    kReconcileNetwork   = 1u << 2,   /* Network List Manager              */
    kReconcileSettings  = 1u << 3,   /* chiave di registro Explorer       */
    kReconcileBackstop  = 1u << 4,   /* passata di sicurezza a 30 s       */
    kReconcileExplorer  = 1u << 5,   /* TaskbarCreated: reimporta e ridecina */
    kReconcilePixels    = 1u << 6,   /* v2.1: ricattura i pixel delle icone
                                      * rimaste senza bitmap (CopyIcon
                                      * negata all'import iniziale)        */
    kReconcileUiaTray   = 1u << 7,   /* v2.60: tray XAML di Windows 11 (la
                                      * lettura arriva dal lettore UI
                                      * Automation, non da una toolbar)     */
};

class TrayService {
public:
    static TrayService& Instance();

    int32_t Start();
    void    Stop();

    /* Avvia la prima riconciliazione con la tray di Explorer (idempotente). */
    int32_t ImportExplorerIcons();

    /* Backfill manuale della pagina legacy reale di Windows. Non crea una
     * pagina sostitutiva: prepara la cache che Explorer rilegge. */
    int32_t NotificationPageBackfill();

    /* Una passata di riconciliazione: aggiunge chi manca, aggiorna lo
     * stato di cio' che appartiene a Explorer (con isteresi), rimuove cio'
     * che Explorer non mostra piu' (dopo conferme), allinea il toolbar. */
    void ReconcileWithExplorer(uint32_t sources);

    /* Snapshot opzionale del callback ITrayNotify/ITrayNotifyWin8. Il
     * risultato si fonde nella stessa cache, senza diventare una fonte
     * esclusiva e senza scrivere preferenze di Explorer. */
    void ImportTrayNotifySnapshot(bool force);
    void MergeTrayNotifySnapshot(
        const std::vector<TrayNotifySnapshotItem>& items);

    /* Applica la regola di visibilita' della shell (EnableAutoTray) alle
     * icone che non hanno una preferenza salvata dall'utente. */
    void ApplyShellVisibilityDefaults();

    /* Rettangolo a schermo di un'icona, riferito dal livello gestito e
     * restituito a chi chiama Shell_NotifyIconGetRect. Il ripiego e' il
     * rettangolo reale del pulsante nel ToolbarWindow32 del modello. */
    void SetIconRect(uint64_t ownerHwnd, uint32_t uid, const RECT& rect);
    bool GetIconRect(uint32_t hWnd32, uint32_t uid, const GUID& guid, RECT& out);
    void SetChevronRect(const RECT& rect);

    /* Il toolbar del modello: creazione (dopo le finestre) e dimensionamento. */
    void EnsureToolbarModel();
    void SyncToolbarModel();

    /* Mirror comctl32 opt-in: viene richiamato dal medesimo punto che
     * notifica il pannello overflow, ma non è mai una fonte UI o Shell. */
    void SyncLegacyToolbarShim();

    /* Passata periodica di sola verifica proprietari vivi + diff leggero. */
    void WatchdogLoop();

    /* La finestra fantasma registrata come Shell_TrayWnd (e la figlia
     * TrayNotifyWnd) deve occupare lo stesso rettangolo della nostra
     * barra: i flyout che si ancorano li' (volume di Windows 7, SndVol)
     * altrimenti si aprono a (0,0), in alto a sinistra. */
    void SetShellRects(const RECT& bar, const RECT& notify);

    /* Ancoraggio dei flyout di sistema sopra la propria icona. */
    void StartFlyoutWatcher(const TrayIconKey& key);
    void ReanchorFlyouts();
    void PlaceFlyout(HWND flyout, const RECT& iconRect);

    int32_t GetCount();
    int32_t CopyTo(W7T_TrayIconInfo* buffer, int32_t capacity);
    std::vector<OverflowSnapshot> GetUnpinnedSnapshot();
    int32_t GetIconBitmap(uint64_t ownerHwnd, uint32_t uid, int32_t* width, int32_t* height,
                          uint8_t* pixels, int32_t pixelsBytes);
    int32_t SendClick(uint64_t ownerHwnd, uint32_t uid, int32_t clickType, int32_t x, int32_t y);
    int32_t SetPinned(uint64_t ownerHwnd, uint32_t uid, int32_t pinned);

    /* Persisted three-state behavior used by tray drag and overflow. */
    int32_t SetBehavior(uint64_t ownerHwnd, uint32_t uid, int32_t behavior);

    /* v2.62: il frontend dichiara pronta (o no) l'esperienza del riquadro di
     * rete di Windows 7: vedi SendClick. */
    void SetWin7NetworkFlyout(bool ready);
    /* v3.8: stessa cosa per il riquadro di rete variante Windows 8. */
    void SetWin8NetworkFlyout(bool ready);
    /* OPZIONE B: ripristino esplicito della chiave legacy batteria,
     * chiamato dal livello gestito in chiusura pulita (oltre al restore
     * gia' fatto da Stop). Idempotente: senza tentativi pendenti e'
     * un no-op. */
    static void RestoreBatteryFlyoutKey();

    /* Riordino del modello dal trascinamento del livello gestito: sposta
     * l'icona accanto a un'altra e muove il pulsante reale con
     * TB_MOVEBUTTON, come fa la shell. */
    int32_t MoveIcon(uint64_t sourceHwnd, uint32_t sourceUid,
                     uint64_t targetHwnd, uint32_t targetUid, int32_t insertAfter);

    bool    GetLastBalloon(W7T_BalloonInfo* out);

    /* Gestisce i messaggi privati posted dai watcher (registro, rete,
     * owner morti): li tratta TrayWndProc. */
    void OnWatcherMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    /* Segnale dal WinEventHook EVENT_OBJECT_DESTROY: se l'HWND distrutto
     * e' il proprietario di qualche icona, ne chiede la rimozione (dal
     * thread dei messaggi del servizio, mai dal callback directly). */
    void NotifyOwnerDiedAsync(HWND owner);

    /* v2.60: attiva il percorso Windows 11 (lettore UIA + hook sulle
     * finestre che ospitano la tray XAML). Idempotente. */
    void EnableWin11Tray();

    /* v2.61: crea/aggiorna le tre icone di sistema che la tray di Windows 11
     * non espone (volume, rete, batteria). Non dipende da nessuna lettura di
     * Explorer: esiste appena la modalita' Windows 11 e' attiva, e da quel
     * momento resta nel modello. `shellExposed` (se non nullo) sono i tipi
     * che la shell fornisce in QUESTA lettura: per quelli la voce sintetica
     * non viene rinnovata e sparisce da sola. `presentUids` (se non nullo)
     * riceve gli uid sintetici, cosi' la passata di rimozione non li tocca. */
    void EnsureSyntheticSystemIcons(const std::set<SystemIconKind>* shellExposed,
                                    std::set<uint32_t>* presentUids,
                                    int* added, int* updated,
                                    bool* bitmapChanged);

    /* v2.61: rettangolo CORRENTE di un'icona, riportato dal frontend
     * (W7T_SetIconRect) a ogni movimento reale: layout, DPI, monitor,
     * apertura/chiusura dell'overflow. Serve ad ancorare i flyout alla
     * posizione ATTUALE dell'icona, mai a quella dell'importazione. */
    bool CurrentIconRect(const TrayIconKey& key, RECT& out);

    /* v2.60: la tray di Windows 11 si legge dall'albero di accessibilita'
     * (vedi Win11TrayReader.h). Questa passata fonde quel risultato nel
     * modello: aggiunge, aggiorna e rimuove SOLO le voci nate da li'. */
    void ApplyWin11TraySnapshot();

    /* Tipo di icona di sistema di una voce: per le voci normali e' quello
     * riconosciuto dal proprietario/GUID, per quelle della tray di Windows
     * 11 e' quello classificato dal nome accessibile. */
    SystemIconKind KindOf(uint64_t ownerHwnd, uint32_t uid) const;

    /* true quando questa sessione usa la tray XAML di Windows 11. */
    bool IsWin11Tray() const { return m_win11Tray.load(); }

    /* true dopo la prima richiesta di importazione. */
    bool m_importStarted = false;

private:
    TrayService() = default;
    TrayService(const TrayService&) = delete;
    TrayService& operator=(const TrayService&) = delete;

    void ThreadMain();
    bool CreateWindows();
    void DestroyWindows();
    void MaintainTrayTopmost();

    static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK TrayWndProcInner(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    /* v1.21.32: responder of the taskbar-list protocol.
     *
     * WM_USER + 236 (TWM_GETTASKSWITCH in the ReactOS reimplementation of
     * CTaskbarList, which reproduces the Explorer sequence) asks the window
     * of class "Shell_TrayWnd" for the handle of the window that receives
     * the AddTab/DeleteTab/ActivateTab notifications. The client side is:
     * CoCreateInstance(CLSID_TaskbarList) -> FindWindowW(L"Shell_TrayWnd")
     * -> SendMessage(TWM_GETTASKSWITCH) -> SendMessage(SHELLHOOK, HSHELL_*,
     * hwnd); HrInit fails outright when the query answers zero.
     *
     * Because this program registers that class name for its own
     * notification area, the lookup can land here, so the answer has to be
     * a live window: a client that receives zero - tao/Tauri (Windhawk) and
     * Chromium/Electron (VS Codium and the other editors) both go through
     * this at window-creation time - is left with a taskbar list that does
     * nothing and can park inside its own window build. */
    static LRESULT CALLBACK TaskSwitchWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static constexpr UINT kTWMGetTaskSwitch = WM_USER + 236;

    /* v2.60: la tray di Windows 11 cambia quando la sua isola XAML crea,
     * mostra o nasconde qualcosa. L'hook e' filtrato per classe e processo,
     * quindi costa una GetClassNameW per evento e nient'altro. */
    static void CALLBACK TrayHostChangedProc(HWINEVENTHOOK hook, DWORD event,
                                             HWND hwnd, LONG idObject,
                                             LONG idChild, DWORD thread,
                                             DWORD time);
    LRESULT HandleCopyData(HWND hwnd, WPARAM sender,
                           const COPYDATASTRUCT* cds);
    LRESULT HandleCopyDataLocal(const COPYDATASTRUCT* cds);
    bool ForwardCopyDataToExplorer(WPARAM sender,
                                   const COPYDATASTRUCT* cds) const;

    /* SetShellRects può essere chiamata dal thread WPF, mentre le finestre
     * della tray e i controlli comctl32 appartengono al thread del servizio.
     * Il lavoro reale viene quindi sempre eseguito qui, sul thread proprietario
     * delle finestre, per evitare SendMessage/MoveWindow concorrenti. */
    void ApplyShellRectsOnThread(const RECT& bar, const RECT& notify);

    /* Vista normalizzata di NOTIFYICONDATAW, indipendente dal bitness
     * del processo mittente. */
    struct NormalizedNid {
        uint64_t hWnd            = 0;
        uint32_t uID             = 0;
        uint32_t uFlags          = 0;
        uint32_t uCallbackMessage = 0;
        uint64_t hIcon           = 0;
        uint32_t dwState         = 0;
        uint32_t dwStateMask     = 0;
        uint32_t uVersion        = 0;
        uint32_t dwInfoFlags     = 0;
        uint32_t uTimeout        = 0;
        GUID     guidItem        = {};
        std::wstring szTip;
        std::wstring szInfo;
        std::wstring szInfoTitle;

        /* Pixel dell'icona quando il mittente e' Wine (vedi
         * NormalizeNidWine): li' non arriva un HICON utilizzabile. */
        ArgbBitmap   wineBitmap;

        bool     valid           = false;
    };

    static bool NormalizeNid(const uint8_t* data, size_t size, NormalizedNid& out);

    /* Formato alternativo usato da Wine (vedi TrayService.cpp). */
    static bool NormalizeNidWine(const uint8_t* data, size_t size, NormalizedNid& out);
    void ApplyMessage(uint32_t message, const NormalizedNid& nid);

    /* --- helpers del modello --- */
    void CheckPowerStatusAndRefresh(bool force);
    void ScheduleReconcile(uint32_t sources, DWORD delayMs);
    void RunDeferredReconciles();
    uint32_t EnsureToolbarId(const TrayIconKey& key);
    /* v2.6: compatibilita' con la mod Windhawk "Windows 7 Network Flyout
     * Recreation" (>= 5.0.0): se presente e l'icona cliccata e' quella di
     * rete, sintetizza il clic sulla toolbar vera di explorer e lascia che
     * la mod apra il suo flyout classico. TRUE = clic gia' gestito. */
    bool TryWindhawkNetFlyoutClick(uint64_t ownerHwnd, uint32_t uid);
    void RemoveEntryLocked(const TrayIconKey& key);

    /* v1.7.6: the SINGLE resolution of an entry's visibility: saved
     * choice + "Always show all icons" + the system-icon switches.
     * barVisible = the icon shows on the bar; presentSomewhere = the icon
     * lives in any view (bar or overflow): false means invisible
     * everywhere, and its balloons stay silent too. Requires m_mutex. */
    void ResolveVisibilityLocked(const TrayIconEntry& entry,
                                  bool& barVisible,
                                  bool& presentSomewhere) const;
    void RebindByGuid(const TrayIconKey& oldKey, const TrayIconKey& newKey);
    bool HasSavedPreference(const TrayIconKey& key) const;

    mutable std::recursive_mutex    m_mutex;   /* mutabile: anche i getter const leggono il modello */
    std::map<TrayIconKey, TrayIconEntry> m_icons;
    std::map<TrayIconKey, RECT>          m_iconRects;
    /* v2.7: flyout orologio a dimensione fissa (bordi Aero conservati):
     * il watcher ne riafferma posizione E size finche' e' visibile. */
    RECT m_clockFixedRect{};
    bool m_haveClockFixed = false;
    RECT                                 m_chevronRect = {};

    /* v2.61: attesa corrente fra due tentativi di lettura della tray di
     * Windows 11 quando la shell non risponde (1 s, 2 s, 4 s, 5 s). Torna a
     * 1 s appena una lettura e' valida: nessun polling continuo, solo un
     * ritentativo in backoff guidato dagli eventi. */
    unsigned long                        m_uiaRetryDelayMs = 1000;

    /* v2.62: quanti tentativi "rapidi" (400/800/1600/3200 ms) sono gia'
     * stati fatti all'avvio. Si azzera alla prima lettura valida: da quel
     * momento vale il backoff normale e non si sonda piu' di frequente. */
    int                                  m_uiaFastRetries = 0;

    /* v2.62: il riquadro di rete di Windows 7 e' pronto all'uso (modulo
     * inizializzato dal frontend). */
    void StartBatteryOpenWatch(const RECT& anchor);
    void FinishBatteryOpenWatch();
    /* v3.5: cerca nel modello un'icona VERA di stobject.dll (la batteria
     * reale importata dalla tray di Explorer). Ritorna true e riempie i
     * parametri quando la trova. */
    bool FindRealStobjectIcon(uint64_t* owner, uint32_t* uid,
                              uint32_t* callback, uint32_t* version);
    /* v3.6: il pulsante batteria della tray di Windows 11 via UI
     * Automation. A volte lo snapshot della lettura e' vuoto proprio al
     * momento del clic (le letture oscillano 0<->3 icone): invece di
     * rinunciare subito, si ordina una rilettura e si riprova con un
     * timer finche' il pulsante compare (o finiscono i tentativi: solo
     * allora parte il riquadro ricreato). */
    void StartBatteryUiARetry(const RECT& anchor);
    void StopBatteryUiARetry();

    bool                                 m_win7NetworkFlyoutReady = false;
    /* v3.8: riquadro di rete Windows 8 pronto (frontend lo ha inizializzato
     * e la modalita' scelta e' "Windows 8 (ricreato)"). */
    bool                                 m_win8NetworkFlyoutReady = false;

    /* v2.63: batteria - stato della verifica differita. */
    RECT                                 m_pendingBatteryAnchor = {};
    int                                  m_pendingBatteryPopups = 0;
    /* v3.5: istantanea delle finestre esterne visibili PRIMA del clic
     * sulla batteria. Il confronto e' per insieme, non per numero: il
     * riquadro Win32 di Windows 7 a volte arriva senza lo stile WS_POPUP
     * (o dentro una finestra gia' contata), quindi contare solo i popup
     * faceva credere che la shell non avesse aperto niente e il riquadro
     * ricreato si impilava sopra quello vero. */
    std::set<uint64_t>                   m_pendingBatteryWindows;
    /* v3.6: tentativi del timer UIA per la batteria (vedi
     * StartBatteryUiARetry). */
    RECT                                 m_batteryUiARetryAnchor = {};
    int                                  m_batteryUiARetryTicks = 0;
    std::vector<TrayIconKey>        m_order;

    /* Indice GUID -> chiave, per il riaggancio delle re-registrazioni:
     * la shell identifica le icone anche per GUID; quando un'applicazione
     * si ri-registra con uid diverso dopo un riavvio, l'icona NON viene
     * duplicata: la voce esistente cambia chiave. E' l'unico modo di
     * restare 1:1 con un'unica istanza per icona reale. */
    std::map<std::wstring, TrayIconKey> m_byGuid;

    std::thread        m_thread;
    std::atomic<bool>  m_explorerRestarted{ false };
    std::atomic<bool>  m_running{ false };
    std::atomic<bool>  m_startOk{ false };
    /* v2.37 punto 15: segnala che ThreadMain ha davvero finito, cosi'
     * Stop() puo' fare un join CON LIMITE invece di aspettare all'infinito
     * un thread rimasto appeso su un Explorer che non risponde. */
    std::atomic<bool>  m_threadDone{ false };
    std::atomic<DWORD> m_threadId{ 0 };
    HWND               m_trayWnd   = nullptr;
    HWND               m_notifyWnd = nullptr;
    /* Proprietà esplicita delle classi locali: si annullano solo quelle
     * registrate da questa istanza, mai una classe riutilizzata. */
    HINSTANCE          m_windowInstance = nullptr;
    bool               m_trayClassOwned = false;
    bool               m_notifyClassOwned = false;
    bool               m_taskSwitchClassOwned = false;
    /* v1.21.32: hidden window (owned by m_trayWnd) that answers the
     * taskbar-list protocol; see kTWMGetTaskSwitch. */
    HWND               m_taskSwitchWnd = nullptr;
    UINT               m_taskbarCreatedMsg = 0;

    bool               m_trayNotifySnapshotImported = false;

    /* Messaggi privati dei watcher (WM_APP+...), gestiti in TrayWndProc. */
    static constexpr UINT kMsgSettings      = WM_APP + 102; // registro
    static constexpr UINT kMsgNetwork       = WM_APP + 103; // NLM
    static constexpr UINT kMsgOwnerDied     = WM_APP + 104; // WinEventHook
    static constexpr UINT kMsgRetryImport   = WM_APP + 105; // secondo giro import
    static constexpr UINT kMsgToolbarSync   = WM_APP + 106; // sync rinviato al thread dei messaggi
    static constexpr UINT kMsgUiaTray       = WM_APP + 107; // v2.60: snapshot tray Win11 pronto
    static constexpr UINT kMsgLegacyShim    = WM_APP + 109; // mirror opt-in coalescente
    static constexpr UINT kMsgShellRects    = WM_APP + 110; // layout cross-thread
    static constexpr UINT kTimerDebounce    = 0xB1;
    static constexpr UINT kTimerBackstop    = 0xB2;
    /* v2.61: risveglio leggero (10 s) delle sole icone sintetiche mentre si
     * e' su Windows 11. Non legge nulla di Explorer: ridisegna il glifo del
     * volume (che non ha eventi), della rete e della batteria dallo stato
     * corrente, cosi' il livello del volume si aggiorna anche senza eventi
     * della tray. */
    static constexpr UINT kTimerSynthetic   = 0xB3;
    /* v2.63: verifica differita dell'apertura del riquadro batteria di
     * Windows (vedi SendClick). Un solo colpo: se la shell non ha aperto
     * nulla, il riquadro ricreato compare lo stesso. */
    static constexpr UINT kTimerBatteryFallback = 0xB4;
    /* v3.6: tentativi UIA del clic sul pulsante batteria di Windows 11. */
    static constexpr UINT kTimerBatteryUiARetry = 0xB5;
    /* Sorveglianza minima dello z-order del ricevitore Shell_TrayWnd.
     * Serve solo a restare davanti alla tray reale per ricevere WM_COPYDATA;
     * si arresta insieme alle finestre del servizio. */
    static constexpr UINT kTimerTrayMonitor = 0xB6;

    std::atomic<uint32_t> m_pendingSources{ 0 };
    std::atomic<bool>     m_importDone{ false };
    std::atomic<bool>     m_legacyShimPosted{ false };

    /* Ultimo layout richiesto dal thread gestito. Un solo messaggio pendente
     * accorpa i molti passaggi di layout WPF senza toccare comctl32 fuori dal
     * suo thread proprietario. */
    RECT                  m_pendingBarRect{};
    RECT                  m_pendingNotifyRect{};
    bool                  m_shellRectsPosted = false;

    /* Proprietari in uscita rilevati dal WinEventHook: rimossi con un
     * piccolo ritardo per dare tempo a eventuali NIM_DELETE di arrivare. */
    std::set<HWND> m_dyingOwners;

    HWINEVENTHOOK m_ownerHook = nullptr;
    HWINEVENTHOOK m_trayHostHook = nullptr;   /* v2.60: isole della tray Win11 */
    std::atomic<bool> m_win11Tray{ false };

    /* v3.8: ripiego "icone sparite" (idea dalla mod Disappearing Tray
     * Icons Fix): il broadcast TaskbarCreated a meta' sessione viene
     * mandato AL MASSIMO una volta, solo quando un'icona e' confermata
     * assente dalla toolbar completa mentre il suo owner e' vivo. */
    bool          m_disappearedIconBroadcastDone = false;

    /* Flyout di sistema attualmente agganciato a un'icona. */
    struct FlyoutAnchor {
        HWND        flyout = nullptr;
        TrayIconKey key{};
    };
    std::mutex         m_anchorMutex;
    SIZE             m_anchorSize{0, 0};   /* v2.30: dimensione fissa flyout */
    FlyoutAnchor       m_anchor;
    std::atomic<bool>  m_watcherRunning{ false };
    std::atomic<int>   m_watcherGeneration{ 0 };

    /* Hook della shell: codice del messaggio SHELLHOOK e stato della
     * registrazione (serve per annullarla alla chiusura). */
    UINT               m_shellHookMsg = 0;
    bool               m_shellHookRegistered = false;

    // Power monitoring con API pubbliche: GetSystemPowerStatus +
    // RegisterPowerSettingNotification (AC/DC e percentuale residua).
    raii::PowerNotifyHandle m_powerNotifyAc;
    raii::PowerNotifyHandle m_powerNotifyBattery;
    SYSTEM_POWER_STATUS m_lastPowerStatus = {};
    bool m_hasLastPowerStatus = false;

    uint32_t           m_nextToolbarId = 1;

    W7T_BalloonInfo    m_lastBalloon = {};
    bool               m_hasBalloon  = false;

    /* --- v2.1: affidabilita' della sincronizzazione con Explorer ---------
     *
     * m_startTick / m_explorerRestartedTick: finestre di GARANZIA in cui
     * le icone importate NON vengono rimosse per assenza. Subito dopo
     * l'avvio (o il riavvio di Explorer) la toolbar della shell e' in
     * costruzione e una lettura — anche formalmente valida — puo' non
     * vedere ancora tutte le icone: rimuoverle in quella finestra era la
     * causa delle icone (batteria in testa) che sparivano all'avvio.
     *
     * m_pixelRetryTick: le icone rimaste senza pixel all'import (CopyIcon
     * rifiutata da Explorer) vengono ricatturate con una passata mirata,
     * non piu' spesso di questo intervallo. */
    ULONGLONG          m_startTick             = 0;
    ULONGLONG          m_explorerRestartedTick = 0;
    ULONGLONG          m_pixelRetryTick        = 0;
    /* v2.2: fino a questo tick le passate kReconcilePower che non vedono
     * pixel nuovi riprogrammano se' stesse (stobject ridisegna l'icona
     * batteria 1-2 s dopo il broadcast; una cattura sola arrivava prima). */
    ULONGLONG          m_powerCatchupUntilTick = 0;
    bool               m_pixelRetryPending     = false;
    DWORD              m_pixelRetryDelayMs     = 2000; /* backoff: 2s->30s */

    /* True se siamo dentro la finestra in cui la toolbar di Explorer puo'
     * non essere ancora al regime (avvio nostro o riavvio di Explorer):
     * in quella finestra le icone importate NON si rimuovono per assenza. */
    bool InRemovalGracePeriod() const;

    /* Chiede una passata di ricattura pixel per le icone rimaste vuote,
     * rispettando l'intervallo minimo m_pixelRetryMinMs. */
    void SchedulePixelRetryIfNeeded(bool anyEmptyBitmap, bool wasCapturePass);

    static constexpr ULONGLONG m_pixelRetryMinMs = 3000;

    /* Raccolta silenziosa della tray moderna: una richiesta all'avvio e poi
     * non più spesso di circa venti secondi. Il tick è letto solo sul thread
     * del servizio, quindi non servono lock aggiuntivi né timer duplicati. */
    std::atomic<ULONGLONG> m_lastWin11OverflowHarvestTick{ 0 };
    static constexpr ULONGLONG kWin11OverflowHarvestDebounceMs = 20000;
};

} /* namespace w7t */

#endif /* W7T_TRAY_SERVICE_H */
