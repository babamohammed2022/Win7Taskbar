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
 */

#include <mutex>

#include "TrayService.h"
#include "TrayOverflowWindow.h"   /* v3.1: refresh conservativo del pannello */
#include "../include/RaiiWrappers.h"
#include "ExplorerTrayReader.h"
#include "Win11TrayReader.h"
#include "TrayToolbar.h"
#include "TrayFallbackIcons.h"
#include "SystemEventsWatch.h"
#include <powrprof.h>
#include <windows.h>
#include <wtsapi32.h>
#include "WindowManager.h"  /* per segnalare il lampeggio delle finestre */
#include "AppBarService.h"  /* per ri-nascondere la barra dopo un riavvio di Explorer */
#include "FlyoutLauncher.h"  /* ApplyAeroFlyoutStyle: bordi Aero dei flyout */
#include "Strings.h"         /* PropStringsFor: etichette dei tipi di sistema */
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <thread>
#include <initguid.h>  /* definisce GUID_NULL e gli altri GUID di sistema */

namespace w7t {

// Power setting GUIDs for battery monitoring - public APIs
// English: Use public power status notifications to detect AC/DC changes
// Italiano: Usa notifiche pubbliche per rilevare i cambi AC/DC
#include <initguid.h>
DEFINE_GUID(GUID_ACDC_POWER_SOURCE_LOCAL, 0x5D3E9A59, 0xE9D5, 0x4B00, 0xA6, 0xBD, 0xFF, 0x34, 0xFF, 0x51, 0x65, 0x48);
DEFINE_GUID(GUID_BATTERY_PERCENTAGE_REMAINING_LOCAL, 0xA7AD8041, 0xB45A, 0x4CAE, 0x87, 0xA3, 0xEE, 0xCB, 0xB4, 0x68, 0xA9, 0xE1);


namespace {

/* Firma che shell32 antepone ai dati della tray. */
/* dwData di WM_COPYDATA usati dalla shell. */
const ULONG_PTR kCopyDataAppBar   = 0;
const ULONG_PTR kCopyDataTrayIcon = 1;

/* Quante letture concordi prima di adottare un cambio di visibilita'
 * letto dalla toolbar di Explorer (isteresi anti-sfarfallio), e quante
 * assenze confermate prima di rimuovere un'icona importata. */
namespace {
/* v2.31: come gestiscono la tray i progetti tipo RetroBar/ManagedShell:
 * l'identita' di un'icona e' legata al PROCESSO proprietario, non alla
 * singola registrazione (uid/hwnd cambiano a ogni ri-registro). */
std::wstring OwnerPathOf(uint64_t ownerHwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(
        reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd)), &pid);
    std::wstring p = GetProcessImagePath(pid);
    std::transform(p.begin(), p.end(), p.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return p;
}
} /* namespace */

constexpr int kConfirmReads  = 2;
constexpr int kConfirmGone   = 2;

/* v2.1: finestre di garanzia per le rimozioni per assenza. All'avvio del
 * servizio (o dopo un riavvio di Explorer) la toolbar della shell viene
 * ricostruita piu' volte nel primo secondo/i e le letture — anche quelle
 * che tornano "valide" — possono fotografare stati parziali: rimuovere
 * icone in quella finestra le faceva sparire definitivamente (batteria,
 * volume, rete). Durante la garanzia le rimozioni per assenza sono
 * sospese; quelle per proprietario morto restano immediate perche' sono
 * un fatto certo, non un'inferenza dalla lettura. */
constexpr ULONGLONG kRemovalGraceAfterStartMs   = 60000;
constexpr ULONGLONG kRemovalGraceAfterRestartMs = 30000;

/* Ritardo di coalescenza delle passate di riconciliazione: piu' eventi
 * ravvicinati (rete + batteria + registro) producono UNA passata sola. */
constexpr DWORD kDebounceMs = 350;

/* ------------------------------------------------------------------ */
/*  Preferenze di visibilita' per icona                                */
/*                                                                     */
/*  Windows tiene l'elenco delle icone "promosse" fuori dall'overflow   */
/*  in IconStreams/PromotedIconStreams, sotto TrayNotify: un blob       */
/*  binario offuscato e diverso fra le build. Qui serve lo stesso       */
/*  concetto, non lo stesso formato: la scelta dell'utente (icona sulla */
/*  barra o nell'overflow) va ricordata fra un avvio e l'altro, perche' */
/*  le applicazioni mandano NIM_ADD una volta sola e senza memoria      */
/*  tornerebbero tutte al valore predefinito.                           */
/*                                                                     */
/*  Chiave = exe dell'applicazione + uid, come fa Windows (che aggiunge */
/*  anche il GUID). Valore DWORD: 1 = visibile, 0 = nell'overflow.      */
/* ------------------------------------------------------------------ */

const wchar_t* const kTrayPrefsKeyPath = L"SOFTWARE\\Win7Taskbar\\TrayIconPrefs";

std::wstring MakePreferenceName(uint64_t ownerHwnd, uint32_t uid) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd));

    std::wstring identity;

    DWORD pid = 0;
    if (hwnd != nullptr) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
    if (pid != 0) {
        identity = GetProcessImagePath(pid);
    }
    if (identity.empty() && hwnd != nullptr) {
        /* Processo gia' uscito: la classe della finestra resta comunque
         * un identificatore stabile dell'applicazione. */
        wchar_t className[128] = {0};
        if (GetClassNameW(hwnd, className, 128) > 0) {
            identity = className;
        }
    }
    if (identity.empty()) {
        identity = L"hwnd";
    }

    /* I nomi di valore non possono contenere '\': la sostituiamo. Tutto in
     * minuscolo cosi' maiuscole/minuscole del percorso non contano. */
    for (wchar_t& c : identity) {
        if (c == L'\\') c = L'/';
        c = static_cast<wchar_t>(towlower(c));
    }

    wchar_t suffix[32] = {0};
    _snwprintf_s(suffix, _TRUNCATE, L"#%u", uid);
    return identity + suffix;
}

bool LoadPinPreference(const std::wstring& name, bool& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kTrayPrefsKeyPath, 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD value = 0;
    DWORD size  = sizeof(value);
    DWORD type  = REG_DWORD;
    const LONG result = RegQueryValueExW(key, name.c_str(), nullptr, &type,
                                        reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS || type != REG_DWORD) {
        return false;
    }
    out = (value != 0);
    return true;
}

void SavePinPreference(const std::wstring& name, bool pinned) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kTrayPrefsKeyPath, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }

    const DWORD value = pinned ? 1 : 0;
    RegSetValueExW(key, name.c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
}

/* Preferenza salvata, oppure la regola di Windows se non ce n'e' una. */
bool ResolveInitialPinned(uint64_t ownerHwnd, uint32_t uid, bool windowsDefault) {
    bool saved = false;
    if (LoadPinPreference(MakePreferenceName(ownerHwnd, uid), saved)) {
        return saved;
    }
    return windowsDefault;
}

/* Messaggi NIN_* (shellapi.h non li definisce tutti su ogni SDK). */
#ifndef NIS_HIDDEN
#define NIS_HIDDEN         0x00000001
#endif

#ifndef NIN_SELECT
#define NIN_SELECT          (WM_USER + 0)
#endif
#ifndef NINF_KEY
#define NINF_KEY            0x1
#endif
#ifndef NIN_BALLOONSHOW
#define NIN_BALLOONSHOW     (WM_USER + 2)
#endif
#ifndef NIN_POPUPOPEN
#define NIN_POPUPOPEN       (WM_USER + 6)
#endif
#ifndef NIN_POPUPCLOSE
#define NIN_POPUPCLOSE      (WM_USER + 7)
#endif

/* Layout a 64 bit di NOTIFYICONDATAW (sizeof == 976). */
#pragma pack(push, 8)
struct NidLayout64 {
    uint32_t cbSize;
    uint32_t _pad0;
    uint64_t hWnd;
    uint32_t uID;
    uint32_t uFlags;
    uint32_t uCallbackMessage;
    uint32_t _pad1;
    uint64_t hIcon;
    wchar_t  szTip[128];
    uint32_t dwState;
    uint32_t dwStateMask;
    wchar_t  szInfo[256];
    uint32_t uVersion;
    wchar_t  szInfoTitle[64];
    uint32_t dwInfoFlags;
    GUID     guidItem;
    uint64_t hBalloonIcon;
};
#pragma pack(pop)

/* Layout a 32 bit di NOTIFYICONDATAW (sizeof == 956). */
#pragma pack(push, 4)
struct NidLayout32 {
    uint32_t cbSize;
    uint32_t hWnd;
    uint32_t uID;
    uint32_t uFlags;
    uint32_t uCallbackMessage;
    uint32_t hIcon;
    wchar_t  szTip[128];
    uint32_t dwState;
    uint32_t dwStateMask;
    wchar_t  szInfo[256];
    uint32_t uVersion;
    wchar_t  szInfoTitle[64];
    uint32_t dwInfoFlags;
    GUID     guidItem;
    uint32_t hBalloonIcon;
};
#pragma pack(pop)

/* Copia una stringa wide a lunghezza fissa che potrebbe non essere
 * NUL-terminata. */
std::wstring FromFixed(const wchar_t* buffer, size_t maxCount) {
    size_t length = 0;
    while (length < maxCount && buffer[length] != L'\0') {
        ++length;
    }
    return std::wstring(buffer, length);
}

/* Gli handle USER restano a 32 bit anche su x64: estensione con segno. */
inline uint64_t HandleFrom32(uint32_t value) {
    return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value)));
}

/* Confronto con il GUID nullo senza dipendere dal simbolo GUID_NULL,
 * che non e' esportato da tutte le toolchain (MinGW incluso). */
inline bool IsNullGuid(const GUID& guid) {
    if (guid.Data1 != 0 || guid.Data2 != 0 || guid.Data3 != 0) {
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (guid.Data4[i] != 0) {
            return false;
        }
    }
    return true;
}

std::wstring GuidToString(const GUID& guid) {
    if (IsNullGuid(guid)) {
        return std::wstring();
    }
    wchar_t buffer[64] = {};
    swprintf(buffer, 64,
             L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3,
             guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
             guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::wstring(buffer);
}

} /* namespace */

TrayService& TrayService::Instance() {
    static TrayService instance;
    return instance;
}

/* ------------------------------------------------------------------ */
/*  Avvio / arresto                                                    */
/* ------------------------------------------------------------------ */

int32_t TrayService::Start() {
    if (m_running.load()) {
        return W7T_OK;
    }

    m_running.store(true);
    m_startOk.store(false);
    m_threadDone.store(false);        /* v2.37 punto 15 */
    m_startTick = GetTickCount64();   /* v2.1: inizio della finestra di garanzia */

    m_thread = std::thread(&TrayService::ThreadMain, this);

    /* Attendi l'esito della creazione delle finestre (max ~3 s). */
    for (int i = 0; i < 300; ++i) {
        if (m_startOk.load()) {
            return W7T_OK;
        }
        if (!m_running.load()) {
            break;
        }
        Sleep(10);
    }

    if (!m_startOk.load()) {
        m_running.store(false);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return W7T_ERR_TRAY_TAKEN;
    }
    return W7T_OK;
}

int32_t TrayService::ImportExplorerIcons() {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_importStarted) {
            return W7T_OK;  /* gia' in corso, o gia' fatta */
        }
        m_importStarted = true;
    }

    /* La prima lettura della toolbar di Explorer vive su un thread suo:
     * SendMessageTimeoutW puo' aspettare Explorer quanto vuole (all'avvio
     * del sistema e' normale che sia occupato) ed e' l'unica fase che
     * legge memoria altrui. Poi il modello e' pilotato dagli EVENTI:
     * niente piu' cicli di reimport a 8/25 secondi. Il ripiego a +2,5 s
     * copre solo la finestra in cui Explorer sta ancora riempiendo la
     * tray subito dopo il logon. */
    /* v2.1: il ripiego e' DIFFERITO di 2,5 s (timer di coalescenza), non
     * piu' un secondo giro immediato: un reimport a pochi millisecondi dal
     * primo fotografava la toolbar di Explorer ancora in ricostruzione e
     * faceva accumulare "assenze" alle icone lette bene la prima volta. */
    std::thread([this] {
        /* v2.37 punto 15: questo thread e' detached, quindi non puo'
         * essere joinato in Stop(). Deve pero' accorgersi da solo della
         * chiusura e smettere di toccare la toolbar di Explorer: se no il
         * processo resta "vivo" per colpa sua durante l'uscita. */
        if (!m_running.load() || ReadsAbortRequested()) {
            m_importDone.store(true);
            return;
        }
        ReconcileWithExplorer(kReconcileInitial);
        m_importDone.store(true);
        if (m_running.load() && !ReadsAbortRequested()) {
            ScheduleReconcile(kReconcileInitial, 2500);
        }
    }).detach();

    return W7T_OK;
}

bool TrayService::InRemovalGracePeriod() const {
    const ULONGLONG now = GetTickCount64();
    if (now - m_startTick < kRemovalGraceAfterStartMs) {
        return true;
    }
    if (m_explorerRestartedTick != 0
        && now - m_explorerRestartedTick < kRemovalGraceAfterRestartMs) {
        return true;
    }
    return false;
}

void TrayService::SchedulePixelRetryIfNeeded(bool anyEmptyBitmap,
                                             bool wasCapturePass) {
    /* Le icone senza bitmap (CopyIcon rifiutata da Explorer all'import,
     * PrintWindow non riuscita) vengono ricatturate da una passata con
     * pixel. Ritmo limitato + backoff: se anche la passata con cattura non
     * risolve, il tentativo successivo raddoppia l'attesa (tetto 30 s),
     * cosi' un'icona davvero non catturabile non genera tempeste di
     * rilettura (era il difetto opposto, gia' vietato dal progetto). */
    if (!anyEmptyBitmap) {
        m_pixelRetryPending = false;
        m_pixelRetryDelayMs = 2000;
        return;
    }
    if (wasCapturePass) {
        if (!m_pixelRetryPending) {
            m_pixelRetryPending = true;
            m_pixelRetryDelayMs = 2000;
        } else if (m_pixelRetryDelayMs < 30000) {
            m_pixelRetryDelayMs = (std::min)(30000ul,
                                             m_pixelRetryDelayMs * 2ul);
        }
    }
    const ULONGLONG now = GetTickCount64();
    if (now - m_pixelRetryTick < m_pixelRetryMinMs) {
        return;   /* una richiesta recente e' gia' in volo */
    }
    m_pixelRetryTick = now;
    ScheduleReconcile(kReconcilePixels, m_pixelRetryDelayMs);
}

/* ------------------------------------------------------------------ */
/*  Riconciliazione 1:1 con la tray reale di Explorer                  */
/* ------------------------------------------------------------------ */

void TrayService::ScheduleReconcile(uint32_t sources, DWORD delayMs) {
    m_pendingSources.fetch_or(sources);
    if (m_trayWnd == nullptr || !IsWindow(m_trayWnd)) {
        m_pendingSources.store(0);
        return;
    }
    /* Un solo timer alla volta: gli eventi vicini nel tempo si fondono. */
    if (!SetTimer(m_trayWnd, kTimerDebounce,
                  delayMs == 0 ? kDebounceMs : delayMs, nullptr)) {
        m_pendingSources.store(0);
    }
}

void TrayService::RunDeferredReconciles() {
    KillTimer(m_trayWnd, kTimerDebounce);
    const uint32_t sources = m_pendingSources.exchange(0);
    if (sources == 0) {
        return;
    }
    if (sources & kReconcileExplorer) {
        /* Explorer ripartito: riapplica subito il nascondiglio della barra
         * nativa (lo perde quando si ricrea), poi la passata sotto
         * rimettera' il modello in pari con le sue nuove toolbar.
         * v2.1: da questo istante decorre la finestra di garanzia in cui
         * le icone non si rimuovono per assenza (le nuove toolbar della
         * shell si riempiono gradualmente). */
        AppBarService::Instance().ReassertNativeTaskbarHidden();
        m_explorerRestartedTick = GetTickCount64();
        m_explorerRestarted.store(false);
    }
    ReconcileWithExplorer(sources);
    if (sources & kReconcileSettings) {
        ApplyShellVisibilityDefaults();
    }
}

namespace {

/* Vero se la finestra appartiene a explorer.exe: sono le icone di shell
 * (batteria, volume, rete) che Explorer aggiorna solo nella propria
 * toolbar e mai verso di noi. Il pid viene memoizzato nella passata, per
 * non riaprire il processo a ogni icona. */
bool IsOwnerExplorerCached(HWND owner, std::map<DWORD, bool>& cache) {
    if (owner == nullptr || !IsWindow(owner)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    if (pid == 0) {
        return false;
    }
    auto it = cache.find(pid);
    if (it != cache.end()) {
        return it->second;
    }

    bool isExplorer = false;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc != nullptr) {
        wchar_t path[MAX_PATH] = {};
        DWORD len = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(proc, 0, path, &len) != FALSE && len > 0) {
            const std::wstring image(path);
            const size_t slash = image.find_last_of(L"\\/");
            const std::wstring name = (slash == std::wstring::npos)
                                    ? image : image.substr(slash + 1);
            isExplorer = _wcsicmp(name.c_str(), L"explorer.exe") == 0;
        }
        CloseHandle(proc);
    }
    cache[pid] = isExplorer;
    return isExplorer;
}

} /* namespace */

void TrayService::ReconcileWithExplorer(uint32_t sources) {
    /* Windows 11 non ha nessuna toolbar della tray da leggere: la passata
     * classica finirebbe in "lettura non valida" a ogni giro. Il modello lo
     * riempie il lettore UI Automation, che risponde in modo asincrono su
     * kMsgUiaTray (vedi ApplyWin11TraySnapshot). */
    if (m_win11Tray) {
        Win11TrayReader::Instance().RequestRead();
        return;
    }

    /* Cosa catturare (PrintWindow sulla toolbar di Explorer):
     * - alla prima passata e dopo un riavvio di Explorer: tutto;
     * - dopo un evento di sistema (alimentazione, rete): solo le icone di
     *   proprieta' di Explorer, che e' li' che cambia il disegno;
     * - alla passata di sicurezza: niente pixel (solo stati, tooltip,
     *   callback, versione, aggiunte).
     * E' il punto esatto in cui prima si sprecava un giro completo ogni
     * 6 secondi; ora i pixel si riprendono solo quando un evento dice che
     * possono essere cambiati. */
    const bool needPixels =
        (sources & (kReconcileInitial | kReconcileExplorer
                    | kReconcilePower | kReconcileNetwork
                    | kReconcilePixels)) != 0;
    ExplorerTrayReadResult read = ExplorerTrayReader::ReadAllEx(needPixels);

    if (!read.okVisible) {
        /* Lettura non valida (Explorer appeso, toolbar in ricostruzione):
         * nessuna icona viene rimossa e nessuno stato viene "corretto": una
         * lettura fallita non deve mai cancellare icone (era questa la
         * causa delle "icone che spariscono a cambio AC/DC"). */
        AppendCoreLog(L"reconcile: lettura toolbar non valida, nessuna rimozione");

        /* Se pero' la toolbar della tray non esiste proprio, questo e' un
         * Windows 11: si passa al lettore di accessibilita' e si lascia che
         * sia lui a rispondere. Prima di questa deviazione l'avvio su
         * Windows 11 restava senza nessuna icona preesistente. */
        EnableWin11Tray();
        if (m_win11Tray) {
            Win11TrayReader::Instance().RequestRead();
            return;
        }
    }

    std::map<DWORD, bool> pidCache;
    std::set<std::pair<uint64_t, uint32_t>> present;
    for (const ExplorerTrayItem& item : read.items) {
        present.insert(std::make_pair(item.ownerHwnd, item.uid));
    }

    int added = 0;
    int updated = 0;
    int removed = 0;
    bool anyEmptyBitmap = false;
    bool anyPixelChanged = false;
    bool needNetConfirm = false;   /* v2.38 punto 3: serve una seconda lettura */

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        /* 1) Aggiunte: cio' che Explorer mostra e noi non abbiamo. Se la
         *    chiave esiste gia' perche' l'applicazione ci ha parlato coi
         *    copia-dati, QUELLA voce e' piu' aggiornata: non si tocca. */
        for (const ExplorerTrayItem& item : read.items) {
            const TrayIconKey key{ item.ownerHwnd, item.uid };
            if (m_icons.find(key) != m_icons.end()) {
                continue;
            }

            TrayIconEntry entry;
            entry.key             = key;
            entry.callbackMessage = item.callbackMessage;
            entry.version         = item.version;
            entry.fromExplorer    = true;
            entry.ownerIsExplorer = IsOwnerExplorerCached(
                reinterpret_cast<HWND>(static_cast<uintptr_t>(item.ownerHwnd)),
                pidCache);
            entry.tooltip         = item.tooltip;
            /* v2.4: come pixel iniziali vince l'hIcon vivo (dichiarazione
             * dell'app/Explorer), col disegno della toolbar di ripiego. */
            entry.bitmap          = item.hasIconBitmap ? item.iconBitmap : item.bitmap;
            entry.pixelHash       = ArgbHash(entry.bitmap);
            entry.iconRevision    = entry.bitmap.empty() ? 0 : 1;

            /* In Windows le icone "nascoste" di Explorer finiscono nel
             * riquadro delle icone nascoste, non sulla barra. Se pero'
             * l'utente ha gia' espresso una preferenza nella nostra tray,
             * quella e' piu' recente e vince. */
            entry.isPinned      = ResolveInitialPinned(key.ownerHwnd, key.uid,
                                                       !item.hidden);
            entry.hiddenDesired = item.hidden;

            if (!IsNullGuid(item.guidItem)) {
                entry.guidKey = GuidToString(item.guidItem);
                m_byGuid[entry.guidKey] = key;
            }

            entry.ownerPath = OwnerPathOf(key.ownerHwnd);
            entry.toolbarId = EnsureToolbarId(key);
            PurgeDuplicateIdentityLocked(key, entry.tooltip,
                                         entry.ownerPath);
            m_icons[key] = std::move(entry);
            m_order.push_back(key);
            ++added;
            CoreState::Instance().QueueEvent(W7T_EVT_TRAY_ADD, key.ownerHwnd, key.uid);
            TrayOverflowWindow::NotifyTrayChanged();   /* v3.1 */
        }

        /* 2) Aggiornamenti di cio' che appartiene a Explorer. Le icone
         * vive di processi diversi da Explorer (che ci parlano coi
         * copia-dati) NON si toccano: la loro fonte di verita' e' il
         * messaggio dell'applicazione. Le icone di Explorer invece vanno
         * rinfrescate perche' lui aggiorna solo la PROPRIA toolbar. */
        for (const ExplorerTrayItem& item : read.items) {
            const TrayIconKey key{ item.ownerHwnd, item.uid };
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                continue;
            }
            TrayIconEntry& entry = it->second;
            entry.missCount = 0;
            entry.lastReadFailed = false;

            if (IsOwnerExplorerCached(
                    reinterpret_cast<HWND>(static_cast<uintptr_t>(key.ownerHwnd)),
                    pidCache)) {
                entry.ownerIsExplorer = true;
            }
            /* Si aggiorna qualsiasi voce presente nella lettura, viva o
             * importata che sia: l'obiettivo dichiarato e' il pari ESATTO
             * con lo stato che la shell mostra. La bitmap viene sovrascritta
             * solo nei giri con cattura e solo se l'hash cambia: nelle
             * passate leggere (backstop) i copia-dati dell'applicazione
             * restano l'unica fonte dei pixel, e l'override non avviene. */

            bool changed = false;

            /* Isteresi sulla zona nascosta: lo stato della toolbar di
             * Explorer e' autorevole ma una lettura puo' essere transitoria
             * (chevron aperto/chiuso tra una passata e l'altra, pager in
             * rimpasto). Un cambio si adotta solo dopo kConfirmReads letture
             * concordi, e SOLO dove l'utente non ha una preferenza nostra:
             * la sua scelta vince, sempre. */
            if (!HasSavedPreference(key)) {
                if (item.hidden == entry.hiddenDesired) {
                    if (entry.hiddenPending > 0) {
                        ++entry.hiddenPending;
                        if (entry.hiddenPending >= kConfirmReads) {
                            const bool pinned = !item.hidden;
                            if (entry.isPinned != pinned) {
                                entry.isPinned = pinned;
                                changed = true;
                            }
                            entry.hiddenPending = 0;
                        }
                    }
                } else {
                    entry.hiddenDesired = item.hidden;
                    entry.hiddenPending = 1;
                }
            }

            if (entry.callbackMessage != item.callbackMessage) {
                entry.callbackMessage = item.callbackMessage;
                changed = true;
            }
            if (entry.version != item.version) {
                entry.version = item.version;
                changed = true;
            }
            if (!item.tooltip.empty() && entry.tooltip != item.tooltip) {
                entry.tooltip = item.tooltip;   /* es. batteria sotto carica */
                changed = true;
            }
            /* v2.31: appena l'identita' (tooltip) e' nota, collassa le
             * ri-registrazioni duplicate dello stesso processo. */
            if (entry.ownerPath.empty()) {
                entry.ownerPath = OwnerPathOf(key.ownerHwnd);
            }
            PurgeDuplicateIdentityLocked(key, entry.tooltip,
                                         entry.ownerPath);

            /* Pixel v2.4: DUE fonti reali e complementari.
             *  - iconBitmap: l'hIcon VIVO della NOTIFYICONDATA (CopyIcon
             *    dalla memoria di Explorer): e' cio' che stobject aggiorna
             *    con NIM_MODIFY al cambio AC/DC della batteria;
             *  - bitmap: il disegno PrintWindow della toolbar di Explorer.
             * Si adotta la prima fonte il cui hash cambia rispetto ai
             * pixel ricordati: cosi' la batteria si aggiorna davvero (era
             * il buco delle versioni precedenti, che adottavano SOLO il
             * PrintWindow), e le icone che cambiano solo nel disegno
             * continuano ad aggiornarsi. Nessun bump "preventivo": la
             * ricarica avviene solo al cambio reale dell'hash. */
            const bool hasIcon = item.hasIconBitmap && !item.iconBitmap.empty();
            const uint64_t hashIcon = hasIcon ? ArgbHash(item.iconBitmap) : 0;
            const bool hasCrop = item.capturedPixels && !item.bitmap.empty();
            const uint64_t hashCrop = hasCrop ? ArgbHash(item.bitmap) : 0;

            /* v2.38 punto 3: identifica l'icona di rete UNA volta via psapi
             * (proprietario pnidui.dll, dentro explorer.exe) e memorizza il
             * risultato nell'entry, cosi' non si riapre il processo a ogni
             * passata. Le icone non-explorer non possono essere pnidui e
             * vengono saltate. */
            if (!entry.netChecked && entry.ownerIsExplorer) {
                /* OwnerModuleIs e' gia' difensiva (OpenProcess + psapi con
                 * controlli nulli); non serve SEH qui. */
                entry.isNetwork  = OwnerModuleIs(reinterpret_cast<HWND>(
                    static_cast<uintptr_t>(key.ownerHwnd)), L"pnidui.dll");
                entry.netChecked = true;
            }

            /* v2.59: che tipo di icona di sistema e' questa? (rete, volume,
             * batteria, oppure nessuna). Un solo giro di identificazione per
             * voce: GUID della shell, poi modulo proprietario. Serve sia per
             * il ripiego dei pixel sia per le decisioni del managed. */
            if (!entry.sysChecked) {
                entry.systemKind = TrayFallbackIcons::Identify(key.ownerHwnd,
                                                               item.guidItem);
                entry.sysChecked = true;
                if (entry.systemKind == SystemIconKind::Network) {
                    /* Il riconoscimento per GUID rende superfluo quello per
                     * modulo: non si riapre il processo una seconda volta. */
                    entry.isNetwork  = true;
                    entry.netChecked = true;
                }
            }

            /* Quale fonte vincerebbe in questa passata (stessa priorita' di
             * sempre: hIcon vivo dichiarato dall'app, poi disegno della
             * toolbar di Explorer). */
            const ArgbBitmap* newBmp = nullptr;
            uint64_t newHash = 0;
            if (hasIcon && hashIcon != entry.pixelHash) {
                newBmp  = &item.iconBitmap;
                newHash = hashIcon;
            } else if (hasCrop && hashCrop != entry.pixelHash) {
                newBmp  = &item.bitmap;
                newHash = hashCrop;
            }

            if (newBmp != nullptr) {
                if (entry.isNetwork) {
                    /* v2.38 punto 3: isteresi anti-sfarfallio. L'icona di
                     * rete, durante il roaming Wi-Fi o un cambio di rete,
                     * puo' passare per stati transitori di una frazione di
                     * secondo. Adottiamo un cambio di pixel solo dopo
                     * kConfirmReads letture CONCORDE dello stesso candidato,
                     * esattamente il principio gia' usato per hiddenPending.
                     * Nessun nuovo timer: la seconda lettura arriva o da un
                     * successivo evento NLM oppure da un'unica passata di
                     * rincorsa (ScheduleReconcile, il debounce gia' usato). */
                    if (entry.netPendingHash == newHash) {
                        ++entry.netPendingCount;
                    } else {
                        entry.netPendingHash  = newHash;
                        entry.netPendingCount = 1;
                    }
                    if (entry.netPendingCount >= kConfirmReads) {
                        entry.bitmap          = *newBmp;
                        entry.pixelHash       = newHash;
                        entry.iconRevision++;
                        entry.netPendingHash  = 0;
                        entry.netPendingCount = 0;
                        entry.usingFallback   = false;
                        changed = true;
                        anyPixelChanged = true;
                    } else if (!needNetConfirm) {
                        needNetConfirm = true;   /* una sola rilettura mirata */
                    }
                } else {
                    entry.bitmap       = *newBmp;
                    entry.pixelHash    = newHash;
                    entry.iconRevision++;
                    entry.usingFallback = false;
                    changed = true;
                    anyPixelChanged = true;
                }
            } else if (entry.isNetwork && entry.netPendingCount > 0) {
                /* La lettura e' tornata stabile sul valore gia' adottato:
                 * il candidato transitorio non si e' confermato, si scorda. */
                entry.netPendingHash  = 0;
                entry.netPendingCount = 0;
            }

            /* v2.59 - RIPIEGO per le icone di sistema senza pixel.
             *
             * Se questa passata non ha portato NESSUNA bitmap reale e la voce
             * e' una di rete/volume/batteria (identificata per proprietario),
             * il posto resterebbe vuoto: si disegna la nostra icona dello
             * stato corrente. Tre regole, in quest'ordine:
             *
             *  1. se la voce ha gia' pixel veri, non si tocca nulla;
             *  2. il ripiego si aggiorna quando lo STATO cambia (batteria che
             *     scende, rete che cade): `usingFallback` dice che i pixel in
             *     `bitmap` sono nostri, non di Explorer;
             *  3. appena Explorer torna a fornire pixel, l'adozione qui sopra
             *     azzera `usingFallback` e l'icona vera riprende il posto.
             */
            if (newBmp == nullptr && entry.systemKind != SystemIconKind::None &&
                (entry.bitmap.empty() || entry.usingFallback)) {
                ArgbBitmap fallback;
                if (TrayFallbackIcons::Render(entry.systemKind, fallback)) {
                    const uint64_t hashFallback = ArgbHash(fallback);
                    if (entry.bitmap.empty() || hashFallback != entry.pixelHash) {
                        entry.bitmap        = fallback;
                        entry.pixelHash     = hashFallback;
                        entry.usingFallback = true;
                        entry.iconRevision++;
                        changed = true;
                        anyPixelChanged = true;
                        TrayFallbackIcons::LogFirstUse(
                            entry.systemKind,
                            (!item.hasIconBitmap && !item.capturedPixels)
                                ? L"Explorer non ha fornito ne' icona ne' pixel"
                                : L"la bitmap fornita non e' utilizzabile");
                    }
                }
            }

            if (changed) {
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY,
                                                 entry.key.ownerHwnd,
                                                 entry.key.uid);
                ++updated;
            }
        }

        /* 3) Rimozioni.
         *
         * v2.1 - la regola e' piu' stretta di prima, perche' la causa
         * reale delle icone che sparivano era proprio questa fase:
         *
         * a) proprietario MORTO: rimozione immediata e certa (fatto, non
         *    inferenza); vale anche con letture non valide o in garanzia;
         *
         * b) assenza dalla toolbar di Explorer: SOLO se ENTRAMBE le
         *    toolbar (visibile E overflow) sono state lette in modo
         *    COMPLETO (okVisible && okOverflow: una lettura incompleta,
         *    con pulsanti non letti, rende false entrambi i flag), la
         *    toolbar visibile non e' vuota, e non siamo nella finestra di
         *    garanzia post-avvio/post-riavvio-Explorer. Le assenze si
         *    contano solo li': una lettura parziale non muove il
         *    contatore (prima bastava un TB_GETBUTTON andato in timeout
         *    per mezzo secondo e due passate consecutive cancellavano
         *    l'icona). */
        const bool readsComplete = read.okVisible && read.okOverflow;
        const bool graceActive   = InRemovalGracePeriod();

        std::vector<TrayIconKey> toRemove;
        for (auto& pair : m_icons) {
            TrayIconEntry& entry = pair.second;
            const bool mirrored = entry.fromExplorer || entry.ownerIsExplorer;
            if (!mirrored) {
                continue;
            }
            if (present.count(std::make_pair(entry.key.ownerHwnd, entry.key.uid)) != 0) {
                continue;
            }
            const HWND owner = reinterpret_cast<HWND>(
                static_cast<uintptr_t>(entry.key.ownerHwnd));
            if (owner != nullptr && !IsWindow(owner)) {
                toRemove.push_back(entry.key);   /* caso (a) */
                continue;
            }
            if (!readsComplete || graceActive) {
                /* Lettura inaffidabile o finestra di garanzia: l'assenza
                 * non si conta nemmeno; il contatore precedente non deve
                 * pero' sopravvivere a una lettura tornata completa con
                 * l'icona di nuovo presente (caso gia' coperto sopra dal
                 * missCount=0 nella fase di aggiornamento). */
                entry.lastReadFailed = true;
                continue;
            }
            if (++entry.missCount >= kConfirmGone) {
                toRemove.push_back(entry.key);   /* caso (b) */
            }
        }
        for (const TrayIconKey& key : toRemove) {
            RemoveEntryLocked(key);
            ++removed;
        }

        /* v2.1: rilevamento icone senza pixel (CopyIcon rifiutata da
         * Explorer all'import, cattura non riuscita). Se questa passata
         * non li ha ricatturati, si chiede una passata mirata. */
        for (const auto& pair : m_icons) {
            if (pair.second.bitmap.empty()) {
                anyEmptyBitmap = true;
                break;
            }
        }

        /* 4) Ordine come lo mostra la toolbar di Explorer: le icone che
         * legge dalla SUA toolbar seguono il SUO ordine di pulsanti; le
         * icone vive che Explorer non conosce restano in coda nell'ordine
         * di arrivo. Si applica solo alla prima passata e dopo un riavvio
         * di Explorer: nel frattempo l'ordine puo' essere stato cambiato
         * dall'utente col trascinamento e non va calpestato. */
        if (sources & (kReconcileInitial | kReconcileExplorer)) {
            std::vector<TrayIconKey> synced;
            synced.reserve(m_order.size());
            for (const ExplorerTrayItem& item : read.items) {
                const TrayIconKey key{ item.ownerHwnd, item.uid };
                if (m_icons.count(key) != 0) {
                    synced.push_back(key);
                }
            }
            for (const TrayIconKey& key : m_order) {
                if (m_icons.count(key) == 0) {
                    continue;
                }
                bool already = false;
                for (const TrayIconKey& placed : synced) {
                    if (!(placed < key) && !(key < placed)) { already = true; break; }
                }
                if (!already) {
                    synced.push_back(key);
                }
            }
            bool same = synced.size() == m_order.size();
            if (same) {
                for (size_t i = 0; i < synced.size(); ++i) {
                    if ((synced[i] < m_order[i]) || (m_order[i] < synced[i])) {
                        same = false;
                        break;
                    }
                }
            }
            if (!same) {
                m_order.swap(synced);
            }
        }
    }

    if (added > 0 || removed > 0) {
        wchar_t line[160];
        wsprintfW(line, L"reconcile: +%u ~%u -%u (icone totali %d)",
                  static_cast<unsigned>(added), static_cast<unsigned>(updated),
                  static_cast<unsigned>(removed),
                  static_cast<int>(m_icons.size()));
        AppendCoreLog(line);
    }

    /* v2.38 punto 3: se un'icona di rete ha visto un candidato nuovo ma non
     * ha ancora raggiunto kConfirmReads letture concordi, pianifica UN'UNICA
     * passata di rincorsa (~1,2 s) per la seconda lettura. Riutilizza il
     * meccanismo ScheduleReconcile/kTimerDebounce gia' esistente: non e' un
     * nuovo timer e non e' polling, e' una singola rilettura mirata. */
    if (needNetConfirm) {
        ScheduleReconcile(kReconcileNetwork, 1200);
    }

    /* v2.1: se qualche icona e' rimasta senza bitmap e questa passata non
     * ha catturato pixel, pianifica una ricattura mirata (con backoff).
     * Se invece ha catturato e ancora mancano, il backoff aumenta. Se non
     * ne mancano piu', il backoff si azzera. */
    SchedulePixelRetryIfNeeded(anyEmptyBitmap, needPixels);

    /* v2.2: rincorsa batteria/alimentazione. Se la passata power non ha
     * visto pixel nuovi ma siamo dentro la finestra aperta dal broadcast,
     * riprogramma se' stessa: stobject ridisegna l'icona 1-2 s dopo
     * l'evento e la prima cattura poteva arrivare prima. Appena i pixel
     * cambiano la finestra si chiude. */
    if ((sources & kReconcilePower) != 0) {
        if (anyPixelChanged) {
            m_powerCatchupUntilTick = 0;
        } else if (GetTickCount64() < m_powerCatchupUntilTick) {
            ScheduleReconcile(kReconcilePower, 1200);
        }
    }

    SyncToolbarModel();
}

void TrayService::ApplyShellVisibilityDefaults() {
    /* La regola di Windows per le icone SENZA preferenza espressa:
     * EnableAutoTray attivo  ->  nuova icona nasce nell'overflow;
     * EnableAutoTray spento  ->  nasce sulla barra.
     * E' la stessa chiave che legge Explorer, riletta qui quando il
     * watcher sul registro segnola un cambiamento, non in polling.
     * Le icone che arrivano da Explorer mantengono lo stato della SUA
     * toolbar; la regola si applica a quelle che si sono registrate da
     * noi senza che l'utente abbia mai scelto nulla. */
    const bool autoTray = IsAutoTrayEnabled();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool anyChanged = false;
    for (auto& pair : m_icons) {
        TrayIconEntry& entry = pair.second;
        if (entry.fromExplorer || entry.ownerIsExplorer) {
            continue;
        }
        if (HasSavedPreference(entry.key)) {
            continue;
        }
        const bool appHides = (entry.state & entry.stateMask & NIS_HIDDEN) != 0;
        if (appHides) {
            continue;
        }
        const bool pinned = !autoTray;
        if (entry.isPinned != pinned) {
            entry.isPinned = pinned;
            anyChanged = true;
            CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY,
                                             entry.key.ownerHwnd, entry.key.uid);
        }
    }
    if (anyChanged) {
        AppendCoreLog(autoTray ? L"raggruppamento attivo: le nuove senza scelta vanno in overflow"
                               : L"raggruppamento spento: tutte visibili sulla barra");
        SyncToolbarModel();
    }
}

bool TrayService::HasSavedPreference(const TrayIconKey& key) const {
    bool unused = false;
    return LoadPinPreference(MakePreferenceName(key.ownerHwnd, key.uid), unused);
}

uint32_t TrayService::EnsureToolbarId(const TrayIconKey& key) {
    auto it = m_icons.find(key);
    if (it != m_icons.end() && it->second.toolbarId != 0) {
        return it->second.toolbarId;
    }
    /* idCommand crescenti e mai riusati: come nella shell, dove
     * l'identificatore del pulsante sopravvive alla posizione. */
    return m_nextToolbarId++;
}

void TrayService::RemoveEntryLocked(const TrayIconKey& key) {
    auto it = m_icons.find(key);
    if (it == m_icons.end()) {
        return;
    }
    if (!it->second.guidKey.empty()) {
        auto g = m_byGuid.find(it->second.guidKey);
        if (g != m_byGuid.end() && !(g->second < key) && !(key < g->second)) {
            m_byGuid.erase(g);
        }
    }
    /* NB: nessun SendMessage qui: la rimozione del pulsante orfano la
     * fa ApplyOrder del prossimo SyncToolbarModel (fuori dal lock), che
     * toglie tutto cio' che non compare piu' nell'ordine del modello. */
    m_icons.erase(it);
    m_iconRects.erase(key);
    m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
                                 [&key](const TrayIconKey& k) {
                                     return !(k < key) && !(key < k);
                                 }),
                  m_order.end());
    CoreState::Instance().QueueEvent(W7T_EVT_TRAY_DELETE, key.ownerHwnd, key.uid);
    TrayOverflowWindow::NotifyTrayChanged();   /* v3.1 */
}

void TrayService::RebindByGuid(const TrayIconKey& oldKey, const TrayIconKey& newKey) {
    /* Un'icona che si ri-registra con hWnd/uid diversi ma stesso GUID e'
     * STESSA icona: la shell lo sa perche' il GUID e' l'identificatore
     * pubblico. Senza questo riaggancio la re-registrazione lascerebbe due
     * pulsanti per lo stesso programma: era la fonte principale del
     * "Win7Taskbar mostra piu' icone della tray reale". */
    auto from = m_icons.find(oldKey);
    if (from == m_icons.end() || m_icons.find(newKey) != m_icons.end()) {
        return;   /* niente da riagganciare, o la chiave nuova esiste gia' */
    }
    TrayIconEntry moved = from->second;
    const std::wstring guidKey = moved.guidKey;
    m_icons.erase(from);
    moved.key = newKey;
    m_icons[newKey] = std::move(moved);

    for (TrayIconKey& k : m_order) {
        if (!(k < oldKey) && !(oldKey < k)) {
            k = newKey;
            break;
        }
    }
    auto r = m_iconRects.find(oldKey);
    if (r != m_iconRects.end()) {
        m_iconRects[newKey] = r->second;
        m_iconRects.erase(r);
    }
    if (!guidKey.empty()) {
        m_byGuid[guidKey] = newKey;
    }
    AppendCoreLog(L"icona riagganciata per GUID (nessun duplicato)");
}

/* ------------------------------------------------------------------ */
/*  Modello a toolbar reale                                            */
/* ------------------------------------------------------------------ */

void TrayService::EnsureToolbarModel() {
    TrayToolbar::Instance().Create(m_notifyWnd, GetModuleHandleW(nullptr));
}

void TrayService::SyncToolbarModel() {
    /* Il toolbar vive sul thread del servizio. Chiamate da altri thread
     * (la prima riconciliazione gira sul worker di import) NON devono
     * fare SendMessage mentre il mutex e' preso: il thread dei messaggi
     * potrebbe aspettarlo proprio in ApplyMessage e si incepperebbe. Si
     * rimanda il lavoro a se' stesso con un PostMessage coalescente. */
    if (m_threadId != 0 && GetCurrentThreadId() != m_threadId) {
        if (m_trayWnd != nullptr && IsWindow(m_trayWnd)) {
            PostMessageW(m_trayWnd, kMsgToolbarSync, 0, 0);
        }
        return;
    }

    TrayToolbar& tb = TrayToolbar::Instance();

    std::vector<uint32_t> ids;
    std::vector<uint32_t> hiddenIds;
    std::vector<std::pair<uint32_t, bool>> flags;
    std::map<uint32_t, ArgbBitmap> images;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        ids.reserve(m_order.size());
        for (const TrayIconKey& key : m_order) {
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                continue;
            }
            TrayIconEntry& entry = it->second;
            if (entry.toolbarId == 0) {
                entry.toolbarId = EnsureToolbarId(key);
            }
            const bool hidden = !entry.isPinned
                || (entry.state & entry.stateMask & NIS_HIDDEN) != 0;

            /* Doppio specchio, doppia fedelta' alla shell:
             * - il modello 9x/2003: la toolbar principale contiene TUTTI i
             *   pulsanti e i nascosti portano TBSTATE_HIDDEN (e' cosi' che
             *   il pager sfoltiva la vista senza toccare il modello);
             * - il modello Vista+: le icone nascoste vivono anche nel
             *   secondo toolbar, dentro NotifyIconOverflowWindow, che e' la
             *   fonte della lista che il riquadro a disegni mostra.
             * L'icona non esce MAI dal modello: cambia solo visibilita'. */
            ids.push_back(entry.toolbarId);
            if (hidden) {
                hiddenIds.push_back(entry.toolbarId);
            }
            flags.emplace_back(entry.toolbarId, hidden);
            if (!entry.bitmap.empty()) {
                images[entry.toolbarId] = entry.bitmap;
            }
        }
    }

    /* Fuori dal mutex: le SendMessage TB_* non devono mai girare mentre
     * il modello e' chiuso a chiave (inversione lock/ SendMessage). */
    tb.ApplyOrder(ids);
    tb.ApplyOverflowOrder(hiddenIds);
    for (const auto& f : flags) {
        tb.SetButtonHidden(f.first, f.second);
    }
    for (const auto& img : images) {
        tb.SetButtonImage(img.first, img.second);
    }
}

/* ------------------------------------------------------------------ */
/*  Stato di alimentazione: un solo confronto, nessuna tempesta       */
/* ------------------------------------------------------------------ */

void TrayService::CheckPowerStatusAndRefresh(bool force) {
    // API pubblica GetSystemPowerStatus. La revisione precedente confrontava
    // ANCHE BatteryLifeTime (secondi residui): cambia a ogni secondo, quindi
    // OGNI controllo risultava "cambiato" e innescava una reimport completa
    // con cattura dei pixel: era la tempesta visibile in log-core.txt e la
    // causa primaria del lampeggio. Si confrontano solo i campi che
    // descrivono lo STATO: linea, flag, percentuale, banner.
    SYSTEM_POWER_STATUS sps = {};
    if (!GetSystemPowerStatus(&sps)) {
        return;
    }

    bool changed = false;
    if (!m_hasLastPowerStatus) {
        changed = true;
    } else {
        if (m_lastPowerStatus.ACLineStatus       != sps.ACLineStatus
            || m_lastPowerStatus.BatteryFlag        != sps.BatteryFlag
            || m_lastPowerStatus.BatteryLifePercent != sps.BatteryLifePercent
            || m_lastPowerStatus.SystemStatusFlag   != sps.SystemStatusFlag) {
            changed = true;
        }
    }

    if (changed || force) {
        m_lastPowerStatus = sps;
        m_hasLastPowerStatus = true;

        wchar_t log[256];
        wsprintfW(log, L"power status: AC=%u Battery=%u%% Flag=%u -> passata mirata",
                  (unsigned)sps.ACLineStatus, (unsigned)sps.BatteryLifePercent,
                  (unsigned)sps.BatteryFlag);
        AppendCoreLog(log);

        /* Passata mirata + finestra di rincorsa: quando arriva il
         * broadcast anche Explorer deve ancora ridisegnare la SUA icona
         * batteria nella toolbar (stobject si aggiorna sul proprio timer,
         * tipicamente 1-2 s dopo l'evento). Una sola cattura a +300 ms
         * fotografava spesso il bitmap ANCORA vecchio e, non cambiando
         * l'hash, niente riprovava piu': l'icona restava "sotto carica" a
         * spina staccata (segnalazione utente). Per 8 s dopo il cambio,
         * ogni passata power che non vede pixel nuovi riprogramma se'
         * stessa (vedi ReconcileWithExplorer); ogni passata costa poco
         * perche' la revisione sale solo se l'hash cambia davvero. */
        m_powerCatchupUntilTick = GetTickCount64() + 8000;
        ScheduleReconcile(kReconcilePower, 300);
    }
}

void TrayService::WatchdogLoop() {
    /* Chiamato dal WM_TIMER di sicurezza (30 s) sul thread del servizio.
     * Verifica i proprietari morti (il WinEventHook li vede di norma prima;
     * questa e' la rete per sistemi dove l'hook non arriva) e chiede una
     * passata di solo diff, senza cattura pixel. */
    std::vector<TrayIconKey> dead;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const TrayIconKey& key : m_order) {
            HWND owner = reinterpret_cast<HWND>(
                static_cast<uintptr_t>(key.ownerHwnd));
            if (owner != nullptr && !IsWindow(owner)) {
                dead.push_back(key);
            }
        }
        for (const TrayIconKey& key : dead) {
            RemoveEntryLocked(key);
        }
    }
    if (!dead.empty()) {
        wchar_t line[120];
        wsprintfW(line, L"watchdog: rimosse %u icone con proprietario morto",
                  static_cast<unsigned>(dead.size()));
        AppendCoreLog(line);
        SyncToolbarModel();
    }

    ScheduleReconcile(kReconcileBackstop, 0);
}

/* ------------------------------------------------------------------ */
/*  Owner morti: il gancio giusto e' l'evento, non il giro di vite     */
/* ------------------------------------------------------------------ */

namespace {

void CALLBACK OwnerDestroyedProc(HWINEVENTHOOK hook, DWORD event,
                                 HWND hwnd, LONG idObject, LONG idChild,
                                 DWORD idEventThread, DWORD dwmsEventTime) {
    (void)hook; (void)event; (void)idEventThread; (void)dwmsEventTime;
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == nullptr) {
        return;
    }
    TrayService::Instance().NotifyOwnerDiedAsync(hwnd);
}

} /* namespace */

void TrayService::NotifyOwnerDiedAsync(HWND owner) {
    bool matched = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const uint64_t value = static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(owner));
        for (const auto& pair : m_icons) {
            if (pair.first.ownerHwnd == value) {
                matched = true;
                break;
            }
        }
    }
    if (!matched) {
        return;
    }
    if (m_trayWnd != nullptr && IsWindow(m_trayWnd)) {
        PostMessageW(m_trayWnd, kMsgOwnerDied,
                     reinterpret_cast<WPARAM>(owner), 0);
    }
}

void TrayService::OnWatcherMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
        case kMsgSettings:
            /* Qualcosa e' cambiato nella chiave Explorer: la sola cosa che
             * ci riguarda qui e' EnableAutoTray (regola di visibilita' per
             * le icone senza preferenza). */
            AppendCoreLog(L"chiave Explorer cambiata: riapplico la regola di visibilita'");
            ScheduleReconcile(kReconcileSettings, 0);
            break;

        case kMsgNetwork:
            /* NLM: connettivita' cambiata. La rete della shell cambia
             * immagine nella SUA toolbar: passata mirata su di essa. */
            ScheduleReconcile(kReconcileNetwork, 0);
            break;

        case kMsgRetryImport:
            /* v2.1: non piu' usato dal percorso di import (il ripiego ora
             * passa dal debounce a +2,5 s); resta come richiesta manuale
             * differita, mai immediata. */
            if (m_importDone.load()) {
                ScheduleReconcile(kReconcileInitial, kDebounceMs);
            }
            break;

        case kMsgOwnerDied: {
            HWND owner = reinterpret_cast<HWND>(wParam);
            if (owner == nullptr || IsWindow(owner)) {
                break;   /* ancora vivo: lasciamo che sia lui a NIM_DELETE */
            }
            std::vector<TrayIconKey> toRemove;
            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                const uint64_t value = static_cast<uint64_t>(
                    reinterpret_cast<uintptr_t>(owner));
                for (const auto& pair : m_icons) {
                    if (pair.first.ownerHwnd == value) {
                        toRemove.push_back(pair.first);
                    }
                }
                for (const TrayIconKey& key : toRemove) {
                    RemoveEntryLocked(key);
                }
            }
            if (!toRemove.empty()) {
                wchar_t line[120];
                wsprintfW(line, L"owner distrutto: rimosse %u icone",
                          static_cast<unsigned>(toRemove.size()));
                AppendCoreLog(line);
                SyncToolbarModel();
            }
            break;
        }

        default:
            break;
    }
}

void TrayService::Stop() {
    if (!m_running.load()) {
        return;
    }
    /* v2.37 punto 15: interruzione cooperativa. Prima si chiede alle
     * letture della toolbar di Explorer di fermarsi (la passata in corso
     * cede al pulsante successivo), poi si spegne il flag e si invia
     * WM_QUIT al thread della tray. */
    RequestAbortReads();
    m_running.store(false);
    if (m_threadId != 0) {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    }

    bool joined = false;
    if (m_thread.joinable()) {
        /* v2.37 punto 15: join CON LIMITE. ThreadMain potrebbe essere
         * dentro un handler che aspetta un Explorer appeso; non possiamo
         * bloccare la chiusura per sempre. Se il thread non finisce nel
         * budget, lo stacchiamo: il processo sta uscendo e l'uscita del
         * processo (ExitProcess) terminera' comunque ogni thread residuo. */
        constexpr ULONGLONG kJoinBudgetMs = 4000;
        const ULONGLONG t0 = GetTickCount64();
        bool done = false;
        while (GetTickCount64() - t0 < kJoinBudgetMs) {
            if (m_threadDone.load()) { done = true; break; }
            Sleep(25);
        }
        if (done) {
            m_thread.join();
            joined = true;
        } else {
            m_thread.detach();
        }
    } else {
        joined = true;
    }
    m_threadId = 0;

    /* Svuotiamo il modello solo se il thread e' davvero finito: se e'
     * ancora vivo potrebbe star leggendo queste mappe, e tanto il processo
     * sta uscendo (la memoria la reclama l'OS). Evitiamo una race. */
    if (joined) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_icons.clear();
        m_order.clear();
        m_byGuid.clear();
    }
}

bool TrayService::CreateWindows() {
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = instance;
    wc.lpszClassName = L"Shell_TrayWnd";
    wc.style         = CS_DBLCLKS;

    /* Se la classe esiste gia' nel nostro processo la riusiamo. */
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    WNDCLASSEXW notifyWc = {};
    notifyWc.cbSize        = sizeof(notifyWc);
    notifyWc.lpfnWndProc   = DefWindowProcW;
    notifyWc.hInstance     = instance;
    notifyWc.lpszClassName = L"TrayNotifyWnd";
    if (RegisterClassExW(&notifyWc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    m_trayWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"Shell_TrayWnd", nullptr,
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, instance, nullptr);

    if (m_trayWnd == nullptr) {
        return false;
    }

    /* Finestra figlia attesa da alcune applicazioni che cercano
     * TrayNotifyWnd per posizionare i propri popup. */
    m_notifyWnd = CreateWindowExW(
        0, L"TrayNotifyWnd", nullptr,
        WS_CHILD,
        0, 0, 0, 0,
        m_trayWnd, nullptr, instance, nullptr);

    /* Modello a toolbar reale dentro la gerarchia della shell:
     * TrayNotifyWnd -> SysPager -> ToolbarWindow32 (vedi TrayToolbar.h). */
    EnsureToolbarModel();

    /* Registrazione come finestra di hook della shell.
     *
     * Serve per ricevere HSHELL_FLASH, l'unico modo in cui Windows comunica
     * che un'applicazione sta chiamando FlashWindowEx: non esiste una
     * proprieta' della finestra da interrogare a posteriori.
     *
     * Il messaggio arriva con un codice dinamico ottenuto da
     * RegisterWindowMessage("SHELLHOOK"), non con una costante fissa. */
    m_shellHookMsg = RegisterWindowMessageW(L"SHELLHOOK");
    if (m_shellHookMsg != 0) {
        /* RegisterShellHookWindow non e' nell'import library di MinGW:
         * si risolve a runtime. Se fallisce si perde solo il lampeggio. */
        using RegisterShellHookWindowFn = BOOL(WINAPI*)(HWND);

        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            auto registerHook = reinterpret_cast<RegisterShellHookWindowFn>(
                reinterpret_cast<void*>(
                    GetProcAddress(user32, "RegisterShellHookWindow")));

            if (registerHook != nullptr) {
                m_shellHookRegistered = registerHook(m_trayWnd) != FALSE;
            }
        }
    }

    /* Registra notifiche di alimentazione usando API pubbliche:
     * GUID_ACDC_POWER_SOURCE (spina inserita/staccata) e
     * GUID_BATTERY_PERCENTAGE_REMAINING. WM_POWERBROADCAST copre anche
     * resume/suspend. Niente polling dello stato: l'evento arriva. */
    {
        HPOWERNOTIFY hAc = RegisterPowerSettingNotification(
            m_trayWnd, &GUID_ACDC_POWER_SOURCE_LOCAL, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (hAc) m_powerNotifyAc.reset(hAc);
        HPOWERNOTIFY hBat = RegisterPowerSettingNotification(
            m_trayWnd, &GUID_BATTERY_PERCENTAGE_REMAINING_LOCAL, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (hBat) m_powerNotifyBattery.reset(hBat);
        CheckPowerStatusAndRefresh(true);
    }

    /* Gli eventi di sistema che sostituiscono il polling della tray:
     * registro (regola di raggruppamento), rete (NLM), sessione (lock),
     * morte dei proprietari (WinEventHook). */
    SystemEventsWatch::StartRegistryWatch(m_trayWnd, kMsgSettings);
    SystemEventsWatch::StartNetworkWatch(m_trayWnd, kMsgNetwork);
    SystemEventsWatch::StartSessionWatch(m_trayWnd);

    m_ownerHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY,
                                  nullptr, OwnerDestroyedProc,
                                  0, 0,
                                  WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    /* v2.60: su Windows 11 la tray non e' una toolbar Win32. Se il sistema
     * e' quello, il modello si legge dall'albero di accessibilita'. */
    EnableWin11Tray();

    /* Passata di sicurezza rara: diff leggero senza cattura pixel e
     * verifica dei proprietari morti. Non sostituisce gli eventi: li
     * copre se il sistema non li consegna (sessioni remote, shell pazze). */
    SetTimer(m_trayWnd, kTimerBackstop, 30000, nullptr);

    /* Il messaggio TaskbarCreated si ASCOLTA e basta. Non si manda piu'
     * all'avvio: il broadcast di andata innescava la doppia registrazione
     * (l'icona resta in Explorer E arriva a noi come voce nuova), cioe'
     * l'esatto "piu' icone della tray reale" che l'utente denuncia. Le
     * icone gia' presenti le prende la prima passata di riconciliazione
     * dalla toolbar di Explorer; quelle nuove nascono da NIM_ADD e basta.
     * All'uscita il broadcast torna: serve a restituire le registrazioni
     * alla shell vera. */
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    return true;
}

void TrayService::DestroyWindows() {
    /* L'hook va tolto prima di distruggere la finestra, altrimenti il
     * sistema resta con un riferimento a un HWND non piu' valido. */
    if (m_shellHookRegistered && m_trayWnd != nullptr) {
        using DeregisterShellHookWindowFn = BOOL(WINAPI*)(HWND);

        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            auto deregisterHook = reinterpret_cast<DeregisterShellHookWindowFn>(
                reinterpret_cast<void*>(
                    GetProcAddress(user32, "DeregisterShellHookWindow")));

            if (deregisterHook != nullptr) {
                deregisterHook(m_trayWnd);
            }
        }
        m_shellHookRegistered = false;
    }

    if (m_ownerHook != nullptr) {
        UnhookWinEvent(m_ownerHook);
        m_ownerHook = nullptr;
    }
    if (m_trayHostHook != nullptr) {
        UnhookWinEvent(m_trayHostHook);
        m_trayHostHook = nullptr;
    }
    if (m_win11Tray) {
        Win11TrayReader::Instance().Stop();
    }
    SystemEventsWatch::StopRegistryWatch();
    SystemEventsWatch::StopNetworkWatch();
    SystemEventsWatch::StopSessionWatch();

    TrayToolbar::Instance().Destroy();

    if (m_trayWnd != nullptr) {
        KillTimer(m_trayWnd, kTimerBackstop);
        KillTimer(m_trayWnd, kTimerDebounce);
    }

    // Unregister power notifications (RAII handles will auto-unregister)
    m_powerNotifyAc.reset();
    m_powerNotifyBattery.reset();

    if (m_notifyWnd != nullptr) {
        DestroyWindow(m_notifyWnd);
        m_notifyWnd = nullptr;
    }
    if (m_trayWnd != nullptr) {
        DestroyWindow(m_trayWnd);
        m_trayWnd = nullptr;
    }
}

void TrayService::ThreadMain() {
    m_threadId = GetCurrentThreadId();

    if (!CreateWindows()) {
        m_running.store(false);
        DestroyWindows();
        m_threadDone.store(true);   /* v2.37 punto 15 */
        return;
    }
    m_startOk.store(true);

    MSG msg;
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindows();

    /* Alla chiusura avvisiamo: Explorer potra' riprendersi la tray. */
    if (m_taskbarCreatedMsg != 0) {
        SendNotifyMessageW(HWND_BROADCAST, m_taskbarCreatedMsg, 0, 0);
    }
    m_threadDone.store(true);       /* v2.37 punto 15 */
}

/* ------------------------------------------------------------------ */
/*  Window procedure                                                   */
/* ------------------------------------------------------------------ */

/* v2.6: hardening (dall'analisi della mod Windhawk): un'eccezione non
 * gestita che risale da un window procedure termina il processo che ospita
 * la finestra. Tutto il corpo vive in TrayWndProcInner; qui si cattura
 * qualsiasi cosa e si delega al comportamento di default. */
LRESULT CALLBACK TrayService::TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    try {
        return TrayWndProcInner(hwnd, msg, wParam, lParam);
    } catch (...) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK TrayService::TrayWndProcInner(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        return Instance().HandleCopyData(hwnd, reinterpret_cast<const COPYDATASTRUCT*>(lParam));
    }

    TrayService& self = Instance();

    /* Timer di coalescenza delle riconciliazioni e timer di sicurezza. */
    if (msg == WM_TIMER) {
        if (static_cast<UINT_PTR>(wParam) == kTimerDebounce) {
            self.RunDeferredReconciles();
            return 0;
        }
        if (static_cast<UINT_PTR>(wParam) == kTimerBackstop) {
            self.WatchdogLoop();
            return 0;
        }
    }

    if (msg >= WM_APP + 100 && msg <= WM_APP + 120) {
        if (msg == kMsgToolbarSync) {
            self.SyncToolbarModel();   /* ora siamo sul thread giusto */
            return 0;
        }
        if (msg == kMsgUiaTray) {
            /* v2.60: il lettore UIA ha finito una lettura: lo snapshot si
             * fonde nel modello qui, sul thread che lo possiede. */
            self.ApplyWin11TraySnapshot();
            return 0;
        }
        self.OnWatcherMessage(msg, wParam, lParam);
        return 0;
    }

    if (msg == WM_WTSSESSION_CHANGE) {
        /* Unlock/remote-reconnect: la shell ricostruisce pezzi della sua UI;
         * una passata di sola verifica costa nulla e rimette tutto in pari
         * se qualcosa e' rimasto indietro. */
        if (wParam == WTS_SESSION_UNLOCK || wParam == WTS_CONSOLE_CONNECT) {
            self.ScheduleReconcile(kReconcileBackstop, 1500);
        }
        return 0;
    }

    /* Messaggi della shell: codice registrato dinamicamente, quindi il
     * confronto non puo' stare in uno switch con costanti. */
    if (self.m_shellHookMsg != 0 && msg == self.m_shellHookMsg) {
        const int code = static_cast<int>(wParam & 0x7FFF);
        HWND target = reinterpret_cast<HWND>(lParam);

        if (code == HSHELL_FLASH) {
            WindowManager::Instance().OnWindowFlash(target);
        } else if (code == HSHELL_WINDOWACTIVATED
                   || code == HSHELL_RUDEAPPACTIVATED) {
            /* La finestra e' stata aperta: il lampeggio si spegne. */
            WindowManager::Instance().ClearFlash(target);
        }
        return 0;
    }

    if (msg == WM_POWERBROADCAST) {
        if (wParam == PBT_APMPOWERSTATUSCHANGE || wParam == PBT_POWERSETTINGCHANGE) {
            // AC/DC o percentuale cambiati: una passata mirata, non una
            // reimport completa (vedi CheckPowerStatusAndRefresh).
            self.CheckPowerStatusAndRefresh(false);
            return 0;
        }
    }

    if (self.m_taskbarCreatedMsg != 0 && msg == self.m_taskbarCreatedMsg) {
        /* Explorer (ri)avviato: le sue toolbar sono nuove. Si alza la
         * bandierina e si fa tutto tra poco su questo thread: il wndproc
         * non deve bloccarsi qui dentro. */
        self.m_explorerRestarted.store(true);
        self.ScheduleReconcile(kReconcileExplorer, 2500);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TrayService::HandleCopyData(HWND, const COPYDATASTRUCT* cds) {
    if (cds == nullptr || cds->lpData == nullptr) {
        return FALSE;
    }

    const auto* bytes = static_cast<const uint8_t*>(cds->lpData);

    /* Protocollo reale di Shell_NotifyIcon (verificato contro il decompilato
     * di shell32!Shell_NotifyIconA di Windows 98, che fa
     * FindWindow("Shell_TrayWnd") + SendMessage(WM_COPYDATA, owner,
     * COPYDATASTRUCT{ dwData=1, ... })):
     *   dwData = 0  messaggio AppBar;
     *   dwData = 1  SHELLTRAYDATA;
     *   dwData = 3  Shell_NotifyIconGetRect.
     * Nella forma moderna (Vista+) il payload e'
     * { dwUnknown, dwMessage, NOTIFYICONDATA } SENZA magic; nella forma 9x
     * il magic 0x34753423 esiste, ma vive nel messaggio GetRect (sotto). */
    if (cds->dwData == kCopyDataAppBar) {
        /* AppBar di altre applicazioni: accettato e ignorato, la gestione
         * dello spazio riservato e' nostra. Dimensione stretta come in
         * ManagedShell, per non bere pacchetti che non c'entrano. */
        return (cds->cbData >= sizeof(uint32_t) * 2) ? TRUE : FALSE;
    }

    /* SHELLTRAYDATA reale (ManagedShell.Interop/NativeMethods.Shell32.cs):
     * { DWORD dwUnknown; DWORD dwMessage; NOTIFYICONDATA nid; } - la nid
     * comincia a offset 8, non a 4: leggerla a 4 (come facevamo fino alla
     * 1.9.11) produceva chiavi e campi spostati, cioe' icone fantasma
     * mescolate a quelle vere: le "posizioni errate". */
    if (cds->dwData == kCopyDataTrayIcon
        && cds->cbData >= 2 * sizeof(uint32_t) + sizeof(NidLayout32)) {
        uint32_t message = 0;
        uint32_t declared = 0;
        memcpy(&message, bytes + sizeof(uint32_t), sizeof(message));
        memcpy(&declared, bytes + 2 * sizeof(uint32_t), sizeof(declared));

        /* Forma Windows: codice NIM_* plausibile e cbSize dichiarato
         * coerente con i byte presenti. shell32 inoltra tanti byte quanti ne
         * dichiara il mittente: le applicazioni possono passare una
         * NOTIFYICONDATA estesa (nel diario dell'utente: 1480 byte), quindi
         * un intervallo fisso 932..1024 era FALSO e buttava ogni pacchetto
         * vero nel ramo Wine. La prova giusta e' declared <= payload
         * (tutto il copia-dati e' la struttura), con tolleranza per i
         * mittenti che dichiarano meno di quanto mandano. */
        const size_t payloadSize = cds->cbData - 2 * sizeof(uint32_t);
        const bool windowsShape = message <= 4
                               && declared >= 932
                               && (declared == payloadSize
                                   || declared <= payloadSize);
        if (windowsShape) {
            NormalizedNid nid;
            if (NormalizeNid(bytes + 2 * sizeof(uint32_t), payloadSize, nid)) {
                wchar_t line[160];
                wsprintfW(line, L"copydata msg=%u cbData=%u hwnd=%016I64X uid=%u",
                          static_cast<unsigned>(message),
                          static_cast<unsigned>(cds->cbData),
                          static_cast<unsigned long long>(nid.hWnd),
                          static_cast<unsigned>(nid.uID));
                AppendCoreLog(line);
                ApplyMessage(message, nid);
                return TRUE;
            }
        }
    }

    /* dwData = 3: Shell_NotifyIconGetRect. L'applicazione chiede al tray
     * "dov'e' la mia icona?" per ancorarci i propri flyout (il pannello del
     * volume, quello della rete...): senza risposta il clic sul volume non
     * apriva nulla e i flyout si aprivano lontani dalle proprie icone.
     * Pacchetto WINNOTIFYICONIDENTIFIER (pack 4, 40 byte) con il magic
     * 0x34753423 - il famoso magic che fino alla 1.9.6 cercavamo nel posto
     * sbagliato: esiste, ma vive in QUESTO messaggio, non in quello delle
     * icone. La RECT torna al mittente scritta subito dopo l'identificatore
     * nel buffer del copia-dati (gia' mappato dal sistema): nessuna
     * scrittura in memoria altrui. */
    if (cds->dwData == 3 && cds->cbData >= 40) {
        uint32_t magic = 0;
        memcpy(&magic, bytes, sizeof(magic));
        if (magic == 0x34753423u) {
            uint32_t hWnd32 = 0;
            uint32_t uID = 0;
            GUID guid = {};
            memcpy(&hWnd32, bytes + 16, sizeof(hWnd32));
            memcpy(&uID,   bytes + 20, sizeof(uID));
            memcpy(&guid,  bytes + 24, sizeof(guid));

            RECT rect = {};
            if (Instance().GetIconRect(hWnd32, uID, guid, rect)) {
                /* La RECT torna al mittente SOLO tramite il buffer del
                 * copia-dati: durante WM_COPYDATA il sistema lo mappa nel
                 * nostro spazio e la scrittura arriva gia' al mittente.
                 * (Storico: qui c'era una WriteProcessMemory che usava
                 * cds->lpData - un indirizzo del NOSTRO spazio - come
                 * destinazione nello spazio altrui: rimosso, era sia
                 * sbagliato sia pericoloso.) */
                if (cds->cbData >= 40 + sizeof(RECT)) {
                    memcpy(const_cast<uint8_t*>(bytes) + 40, &rect, sizeof(rect));
                }
                return TRUE;
            }
            return FALSE;
        }
    }

    /* Formato Wine: dwData contiene direttamente il codice NIM_* e non c'e'
     * il dwMessage in testa (Wine non puo' passare HICON: i pixel arrivano
     * in coda alla struttura). */
    if (cds->dwData <= 2) {
        NormalizedNid wineNid;
        if (NormalizeNidWine(bytes, cds->cbData, wineNid)) {
            ApplyMessage(static_cast<uint32_t>(cds->dwData), wineNid);
            return TRUE;
        }
    }

    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Formato Wine                                                       */
/* ------------------------------------------------------------------ */

/* Wine non inoltra la NOTIFYICONDATA di Windows: la sua shell32
 * (dlls/shell32/systray.c) costruisce una propria struttura a campi fissi
 * e la manda con dwData = codice NIM_* invece del dwMessage in testa.
 *
 * Il tracciato qui sotto e' stato ricavato per misura diretta sotto Wine 10
 * (offset delle stringhe e valori numerici confermati: szTip=32, szInfo=296,
 * szInfoTitle=812, uTimeout=808, dwInfoFlags=940, totale 976 byte).
 *
 * Serve solo per poter provare il programma sotto Wine: su Windows vero
 * arriva il formato SHELLTRAYDATA, gestito da NormalizeNid. */
#pragma pack(push, 1)
struct WineTrayIconData {
    uint32_t hWnd;              /*   0 */
    uint32_t uID;               /*   4 */
    uint32_t uFlags;            /* 8 */
    uint32_t uCallbackMessage;  /*  12 */
    int32_t  iconWidth;         /*  16 */
    int32_t  iconHeight;        /*  20 */
    uint32_t iconPlanes;        /*  24 */
    uint32_t iconBpp;           /*  28 */
    wchar_t  szTip[128];        /*  32 */
    uint32_t dwState;           /* 288 */
    uint32_t dwStateMask;       /* 292 */
    wchar_t  szInfo[256];       /* 296 */
    uint32_t uTimeout;          /* 808 */
    wchar_t  szInfoTitle[64];   /* 812 */
    uint32_t dwInfoFlags;       /* 940 */
    GUID     guidItem;          /* 944 */
};                              /* 960 + eventuali bitmap in coda */
#pragma pack(pop)

static_assert(sizeof(WineTrayIconData) == 960,
              "il tracciato Wine deve restare di 960 byte");

bool TrayService::NormalizeNidWine(const uint8_t* data, size_t size, NormalizedNid& out) {
    if (data == nullptr || size < sizeof(WineTrayIconData)) {
        return false;
    }

    WineTrayIconData nid;
    memcpy(&nid, data, sizeof(nid));

    /* Wine non puo' passare un HICON: gli handle non attraversano i
     * processi. Accoda invece i pixel dell'icona subito dopo la struttura,
     * in formato BGRA gia' pronto, seguiti dalla maschera 1bpp che qui non
     * serve (il canale alfa e' gia' corretto). */
    if ((nid.uFlags & NIF_ICON) && nid.iconWidth > 0 && nid.iconHeight > 0) {
        const size_t pixelBytes =
            static_cast<size_t>(nid.iconWidth) *
            static_cast<size_t>(nid.iconHeight) * 4u;

        if (size >= sizeof(WineTrayIconData) + pixelBytes) {
            out.wineBitmap.width  = nid.iconWidth;
            out.wineBitmap.height = nid.iconHeight;
            out.wineBitmap.pixels.assign(
                data + sizeof(WineTrayIconData),
                data + sizeof(WineTrayIconData) + pixelBytes);
            if (!BitmapSane(out.wineBitmap)) {
                out.wineBitmap = ArgbBitmap{};
            }
        }
    }

    out.hWnd             = nid.hWnd;
    out.uID              = nid.uID;
    out.uFlags           = nid.uFlags;
    out.uCallbackMessage = nid.uCallbackMessage;
    out.hIcon            = 0;   /* i pixel arrivano in coda, non come handle */
    out.dwState          = nid.dwState;
    out.dwStateMask      = nid.dwStateMask;
    out.uVersion         = 0;
    out.dwInfoFlags      = nid.dwInfoFlags;
    out.uTimeout         = nid.uTimeout;
    out.guidItem         = nid.guidItem;
    out.szTip            = FromFixed(nid.szTip, 128);
    out.szInfo           = FromFixed(nid.szInfo, 256);
    out.szInfoTitle      = FromFixed(nid.szInfoTitle, 64);
    out.valid            = true;
    return true;
}

bool TrayService::NormalizeNid(const uint8_t* data, size_t size, NormalizedNid& out) {
    if (data == nullptr || size < sizeof(uint32_t)) {
        return false;
    }

    uint32_t cbSize = 0;
    memcpy(&cbSize, data, sizeof(cbSize));

    /* Distinguiamo il bitness del mittente dal cbSize dichiarato. */
    const bool is64 = (cbSize == sizeof(NidLayout64)) || (size >= sizeof(NidLayout64)
                       && cbSize != sizeof(NidLayout32));

    if (is64 && size >= sizeof(NidLayout64)) {
        NidLayout64 nid;
        memcpy(&nid, data, sizeof(nid));
        out.hWnd             = nid.hWnd;
        out.uID              = nid.uID;
        out.uFlags           = nid.uFlags;
        out.uCallbackMessage = nid.uCallbackMessage;
        out.hIcon            = nid.hIcon;
        out.dwState          = nid.dwState;
        out.dwStateMask      = nid.dwStateMask;
        out.uVersion         = nid.uVersion;
        out.dwInfoFlags      = nid.dwInfoFlags;
        /* In NOTIFYICONDATA uTimeout e uVersion sono una union: lo stesso
         * campo vale come durata del fumetto solo insieme a NIF_INFO. */
        out.uTimeout         = (nid.uFlags & NIF_INFO) ? nid.uVersion : 0;
        out.guidItem         = nid.guidItem;
        out.szTip            = FromFixed(nid.szTip, 128);
        out.szInfo           = FromFixed(nid.szInfo, 256);
        out.szInfoTitle      = FromFixed(nid.szInfoTitle, 64);
        out.valid            = true;
        return true;
    }

    if (size >= sizeof(NidLayout32)) {
        NidLayout32 nid;
        memcpy(&nid, data, sizeof(nid));
        out.hWnd             = HandleFrom32(nid.hWnd);
        out.uID              = nid.uID;
        out.uFlags           = nid.uFlags;
        out.uCallbackMessage = nid.uCallbackMessage;
        out.hIcon            = HandleFrom32(nid.hIcon);
        out.dwState          = nid.dwState;
        out.dwStateMask      = nid.dwStateMask;
        out.uVersion         = nid.uVersion;
        out.dwInfoFlags      = nid.dwInfoFlags;
        /* In NOTIFYICONDATA uTimeout e uVersion sono una union: lo stesso
         * campo vale come durata del fumetto solo insieme a NIF_INFO. */
        out.uTimeout         = (nid.uFlags & NIF_INFO) ? nid.uVersion : 0;
        out.guidItem         = nid.guidItem;
        out.szTip            = FromFixed(nid.szTip, 128);
        out.szInfo           = FromFixed(nid.szInfo, 256);
        out.szInfoTitle      = FromFixed(nid.szInfoTitle, 64);
        out.valid            = true;
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/*  Applicazione dei messaggi NIM_*                                    */
/* ------------------------------------------------------------------ */

void TrayService::ApplyMessage(uint32_t message, const NormalizedNid& nid) {
    /* L'intera applicazione del messaggio vive sotto il mutex del modello:
     * il riaggancio per GUID legge e riscrive m_icons/m_byGuid PRIMA dello
     * switch, e la prima riconciliazione (thread worker) puo' girare in
     * parallelo alle registrazioni in arrivo. */
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    TrayIconKey key{ nid.hWnd, nid.uID };

    /* Il riaggancio per GUID avviene FUORI dallo switch perche' serve sia
     * ad ADD che a MODIFY: se l'applicazione si ri-registra con
     * hWnd/uid diversi ma stesso GUID (dopo un riavvio della shell, un suo
     * crash, un cambio di finestra proprietaria) l'icona reale e' la
     * STESSA e la voce del modello va ricollocata, non duplicata. */
    const bool hasGuid = (nid.uFlags & NIF_GUID) != 0 && !IsNullGuid(nid.guidItem);
    std::wstring nidGuidString;
    if (hasGuid) {
        nidGuidString = GuidToString(nid.guidItem);
    }
    if (hasGuid && (message == NIM_ADD || message == NIM_MODIFY)
        && m_icons.find(key) == m_icons.end()) {
        auto gi = m_byGuid.find(nidGuidString);
        if (gi != m_byGuid.end()) {
            const TrayIconKey oldKey = gi->second;
            if (m_icons.find(oldKey) != m_icons.end() && m_icons.find(key) == m_icons.end()) {
                RebindByGuid(oldKey, key);
            }
        }
    }

    switch (message) {
        case NIM_ADD:
        case NIM_MODIFY: {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);

            auto it = m_icons.find(key);
            const bool isNew = (it == m_icons.end());
            if (isNew) {
                /* NIM_MODIFY su un'icona sconosciuta: la trattiamo come ADD,
                 * come fa la shell dopo un riavvio. */
                TrayIconEntry entry;
                entry.key = key;

                /* Visibilita' iniziale con la REGOLA DI WINDOWS:
                 * EnableAutoTray (HKCU\...\Explorer, la stessa chiave che
                 * legge Explorer) decide se una nuova icona nasce
                 * nell'overflow o sulla barra; NIS_HIDDEN vince su tutto e
                 * una preferenza salvata dall'utente vince sulla regola. */
                entry.isPinned = ResolveInitialPinned(
                    key.ownerHwnd, key.uid, !IsAutoTrayEnabled());
                entry.hiddenDesired = !entry.isPinned;

                entry.ownerPath = OwnerPathOf(key.ownerHwnd);
                PurgeDuplicateIdentityLocked(key, entry.tooltip,
                                             entry.ownerPath);
                entry.toolbarId = EnsureToolbarId(key);
                m_icons[key]   = entry;
                m_order.push_back(key);
                it = m_icons.find(key);
            }

            TrayIconEntry& entry = it->second;
            /* Da ora l'icona e' viva: si aggiorna coi copia-dati. La
             * riconciliazione della toolbar la tocchera' solo se il
             * proprietario e' Explorer (batteria/rete/volume: quelli
             * cambiano solo dentro la toolbar della shell). */
            entry.fromExplorer = false;

            if (nid.uFlags & NIF_MESSAGE) {
                entry.callbackMessage = nid.uCallbackMessage;
            }
            if (nid.uFlags & NIF_TIP) {
                entry.tooltip = nid.szTip;
                /* v2.31: collassa le ri-registrazioni duplicate dello
                 * stesso processo non appena la tooltip e' nota. */
                if (entry.ownerPath.empty()) {
                    entry.ownerPath = OwnerPathOf(key.ownerHwnd);
                }
                PurgeDuplicateIdentityLocked(key, entry.tooltip,
                                             entry.ownerPath);
            }
            if (nid.uFlags & NIF_STATE) {
                const bool wasHidden = (entry.state & NIS_HIDDEN) != 0;
                entry.state = (entry.state & ~nid.dwStateMask) | (nid.dwState & nid.dwStateMask);
                entry.stateMask = nid.dwStateMask;
                const bool nowHidden = (entry.state & NIS_HIDDEN) != 0;
                if (wasHidden != nowHidden) {
                    /* Show/hide richiesto dall'applicazione: sposta l'icona
                     * tra barra e overflow SENZA toccare la preferenza
                     * salvata dall'utente (che riguarda l'overflow, non la
                     * volonta' del programma). */
                    entry.isPinned = !nowHidden;
                }
            }
            if (hasGuid && entry.guidKey != nidGuidString) {
                if (!entry.guidKey.empty()) {
                    m_byGuid.erase(entry.guidKey);
                }
                entry.guidKey = nidGuidString;
                m_byGuid[entry.guidKey] = key;
            }
            if (nid.uFlags & NIF_ICON) {
                HICON source = reinterpret_cast<HICON>(static_cast<uintptr_t>(nid.hIcon));

                if (source == nullptr && !nid.wineBitmap.empty()) {
                    /* Formato Wine: i pixel sono gia' pronti. */
                    const uint64_t hash = ArgbHash(nid.wineBitmap);
                    if (hash != entry.pixelHash) {
                        entry.bitmap = nid.wineBitmap;
                        entry.pixelHash = hash;
                        entry.iconRevision++;
                    }
                } else if (source != nullptr) {
                    /* L'handle appartiene al processo mittente, che potrebbe
                     * distruggerlo: ne prendiamo una copia nostra. */
                    HICON owned = CopyIcon(source);
                    if (owned != nullptr) {
                        ArgbBitmap bmp;
                        if (IconToArgb(owned, bmp) && BitmapSane(bmp)) {
                            const uint64_t hash = ArgbHash(bmp);
                            if (hash != entry.pixelHash) {
                                entry.bitmap = std::move(bmp);
                                entry.pixelHash = hash;
                                entry.iconRevision++;
                            }
                        }
                        DestroyIcon(owned);
                    }
                }
                /* hIcon == NULL senza bitmap Wine: l'applicazione ha mandato
                 * un MODIFY "senza icona". La shell conserva l'ultima icona;
                 * anche noi. CANCELLARLA (comportamento nostro fino alla
                 * 1.9.17) era il difetto che faceva SPARIRE le icone al
                 * cambio AC/DC: molti provider mandano NIF_ICON con handle
                 * nullo proprio in quei toggle. */
            }

            /* Il balloon arriva insieme a un ADD/MODIFY con NIF_INFO. */
            if ((nid.uFlags & NIF_INFO) && !nid.szInfo.empty()) {
                ZeroMemory(&m_lastBalloon, sizeof(m_lastBalloon));
                m_lastBalloon.ownerHwnd = nid.hWnd;
                m_lastBalloon.uid       = nid.uID;
                m_lastBalloon.infoFlags = nid.dwInfoFlags;
                m_lastBalloon.timeout   = nid.uTimeout;
                CopyToFixed(m_lastBalloon.title, 64,  nid.szInfoTitle);
                CopyToFixed(m_lastBalloon.text,  256, nid.szInfo);
                m_hasBalloon = true;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_BALLOON, key.ownerHwnd, key.uid);
            }

            CoreState::Instance().QueueEvent(
                isNew ? W7T_EVT_TRAY_ADD : W7T_EVT_TRAY_MODIFY, key.ownerHwnd, key.uid);
            break;
        }

        case NIM_DELETE: {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            auto it = m_icons.find(key);
            if (it == m_icons.end() && hasGuid) {
                /* DELETE per GUID: la voce potrebbe essere importata con
                 * una chiave diversa. Si elimina QUELLA voce. */
                auto gi = m_byGuid.find(nidGuidString);
                if (gi != m_byGuid.end()) {
                    key = gi->second;
                    it  = m_icons.find(key);
                }
            }
            if (it != m_icons.end()) {
                RemoveEntryLocked(key);
            }
            break;
        }

        case NIM_SETVERSION: {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            auto it = m_icons.find(key);
            if (it != m_icons.end()) {
                it->second.version = nid.uVersion;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY,
                                                 key.ownerHwnd, key.uid);
            }
            break;
        }

        default:
            break;
    }

    SyncToolbarModel();
}

/* ------------------------------------------------------------------ */
/*  Query dal managed layer                                            */
/* ------------------------------------------------------------------ */

void TrayService::SetIconRect(uint64_t ownerHwnd, uint32_t uid, const RECT& rect) {
    TrayIconKey key{ ownerHwnd, uid };
    HWND flyout = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_anchorMutex);
        if (m_anchor.flyout != nullptr && IsWindowVisible(m_anchor.flyout)
            && !(m_anchor.key < key) && !(key < m_anchor.key)) {
            flyout = m_anchor.flyout;
        }
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto prev = m_iconRects.find(key);
        if (prev != m_iconRects.end()
            && prev->second.left == rect.left && prev->second.top == rect.top
            && prev->second.right == rect.right && prev->second.bottom == rect.bottom) {
            /* Nessun movimento: niente rilocazione del flyout a vuoto. */
            return;
        }
        m_iconRects[key] = rect;
    }
    if (flyout != nullptr) {
        PlaceFlyout(flyout, rect);   /* resta agganciato anche se la barra
                                      * si sposta o cambia DPI/monitor */
    }
}

void TrayService::SetChevronRect(const RECT& rect) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_chevronRect = rect;
}

bool TrayService::GetIconRect(uint32_t hWnd32, uint32_t uid, const GUID& guid,
                              RECT& out) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    /* Semantica della shell (Shell_NotifyIconGetRect di Explorer e la mod
     * Windhawk win7-action-center): un'icona nascosta nell'overflow ha come
     * rettangolo quello del pulsante di overflow, non il suo. Qui il
     * pulsante di overflow e' la nostra freccetta. */
    const GUID zeroGuid = {};
    const bool hasGuid = memcmp(&guid, &zeroGuid, sizeof(GUID)) != 0;

    TrayIconKey key{ static_cast<uint64_t>(hWnd32), uid };
    auto entryIt = m_icons.find(key);

    if (hasGuid && entryIt == m_icons.end()) {
        const std::wstring guidKey = GuidToString(guid);
        for (const auto& pair : m_icons) {
            if (pair.second.guidKey == guidKey) {
                entryIt = m_icons.find(pair.first);
                break;
            }
        }
    }

    if (entryIt != m_icons.end()) {
        const TrayIconEntry& entry = entryIt->second;
        if (!entry.isPinned) {
            if (m_chevronRect.right > m_chevronRect.left) {
                out = m_chevronRect;
                return true;
            }
        }
        /* Ripiego geometrico: il rettangolo reale del pulsante nella
         * toolbar del modello (TB_GETITEMRECT + ClientToScreen), quando il
         * livello gestito non l'ha ancora riferito o l'icona e' appena
         * arrivata. */
        if (entry.toolbarId != 0
            && TrayToolbar::Instance().GetItemScreenRect(entry.toolbarId, out)) {
            return true;
        }
        key = entryIt->first;   /* puo' essere cambiata nel confronto per GUID */
    }

    auto it = m_iconRects.find(key);
    if (it == m_iconRects.end()) {
        return false;
    }
    out = it->second;
    return true;
}

/* v2.30: durante l'avvio (o un riavvio di Explorer) la stessa icona puo'
 * essere ri-registrata con uid diversi: senza cleanup si accumulano N
 * copie (es. 15 taskmgr.exe nell'overflow). Identita' vera di una
 * registrazione tray = proprietario + tooltip dichiarata dall'app: se
 * arriva una nuova uid con la stessa identita', la vecchia e' un
 * residuo e va tolta subito (la shell fa lo stesso). */


void TrayService::PurgeDuplicateIdentityLocked(const TrayIconKey& key,
                                               const std::wstring& tooltip,
                                               const std::wstring& ownerPath) {
    if (tooltip.empty() || ownerPath.empty()) return;
    for (auto it = m_icons.begin(); it != m_icons.end(); ++it) {
        if (it->first.uid != key.uid &&
            it->second.tooltip == tooltip &&
            !it->second.ownerPath.empty() &&
            it->second.ownerPath == ownerPath) {
            const TrayIconKey old = it->first;
            m_icons.erase(it);
            m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
                                         [&old](const TrayIconKey& k) {
                                             return k.ownerHwnd == old.ownerHwnd &&
                                                    k.uid == old.uid;
                                         }),
                          m_order.end());
            wchar_t line[180];
            wsprintfW(line,
                      L"tray: uid rinnovato per '%.100s': residuo rimosso",
                      tooltip.c_str());
            AppendCoreLog(line);
            break;
        }
    }
}

std::vector<OverflowSnapshot> TrayService::GetUnpinnedSnapshot() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<OverflowSnapshot> out;
    std::vector<TrayIconKey> dead;
    for (const auto& key : m_order) {
        auto it = m_icons.find(key);
        if (it == m_icons.end() || it->second.isPinned) {
            continue;
        }
        /* v2.29: icone il cui proprietario e' morto non devono restare
         * nell'overflow (taskmgr.exe e simili che chiudono senza che il
         * NIM_DELETE arrivi in tempo): la shell fa lo stesso cleanup.
         * v2.60: le voci della tray di Windows 11 non hanno un HWND
         * proprietario (ownerHwnd = 0): la loro esistenza la decide la
         * lettura UI Automation, non IsWindow. */
        if (!it->second.fromWin11Uia &&
            !IsWindow(reinterpret_cast<HWND>(
                static_cast<uintptr_t>(key.ownerHwnd)))) {
            dead.push_back(key);
            continue;
        }
        OverflowSnapshot snap;
        snap.key     = key;
        snap.bitmap  = it->second.bitmap;
        snap.tooltip = it->second.tooltip;
        out.push_back(std::move(snap));
    }
    for (const TrayIconKey& key : dead) {
        RemoveEntryLocked(key);
    }
    if (!dead.empty()) {
        wchar_t line[140];
        wsprintfW(line, L"overflow: rimosse %u icone con owner morto",
                  static_cast<unsigned>(dead.size()));
        AppendCoreLog(line);
        TrayOverflowWindow::NotifyTrayChanged();
    }
    return out;
}

int32_t TrayService::GetCount() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return static_cast<int32_t>(m_icons.size());
}

/* ------------------------------------------------------------------ */
/*  v2.60 - Tray XAML di Windows 11: attivazione del percorso UIA      */
/* ------------------------------------------------------------------ */

void TrayService::EnableWin11Tray() {
    if (m_win11Tray || m_trayWnd == nullptr) {
        return;
    }
    if (!Win11TrayReader::Detect()) {
        return;
    }

    m_win11Tray = true;
    Win11TrayReader::Instance().SetNotify(m_trayWnd, kMsgUiaTray);
    if (Win11TrayReader::Instance().Start()) {
        AppendCoreLog(L"tray: Windows 11, lettura UI Automation attiva");
    } else {
        AppendCoreLog(L"tray: Windows 11, lettura UI Automation non partita");
    }

    /* Le isole XAML non sono finestre della tray: quando il flyout delle
     * icone nascoste si apre, o quando una qualunque finestra della shell
     * con quella classe compare/scompare, si rilegge. Filtro per classe:
     * l'hook e' globale ma costa una GetClassNameW per evento. */
    if (m_trayHostHook == nullptr) {
        m_trayHostHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                                         nullptr, TrayHostChangedProc,
                                         0, 0,
                                         WINEVENT_OUTOFCONTEXT |
                                         WINEVENT_SKIPOWNPROCESS);
    }
}

/* ------------------------------------------------------------------ */
/*  v2.60 - Tray XAML di Windows 11 (UI Automation)                    */
/*                                                                     */
/*  Il lettore non parla mai col modello: consegna uno snapshot e      */
/*  posta kMsgUiaTray alla finestra del servizio. Questa passata gira  */
/*  quindi sul thread dei messaggi, dove vive il resto del modello.    */
/* ------------------------------------------------------------------ */

void TrayService::ApplyWin11TraySnapshot() {
    if (!m_win11Tray) {
        return;
    }

    const std::vector<Win11TrayItem> items =
        Win11TrayReader::Instance().TakeSnapshot();
    if (items.empty()) {
        /* Nessuna lettura valida: non si tocca niente. Un desktop senza
         * icone di sistema e' possibile solo se l'utente le ha nascoste
         * tutte dalle impostazioni, e in quel caso la passata precedente
         * ha gia' scritto lo stato giusto. */
        return;
    }

    std::set<uint32_t> present;
    std::set<SystemIconKind> presentKinds;
    for (const Win11TrayItem& item : items) {
        present.insert(item.uid);
        if (item.kind != SystemIconKind::None) {
            presentKinds.insert(item.kind);
        }
    }

    /* uid riservati alle icone sintetiche: in cima allo spazio dei 32 bit,
     * lontano dagli hash FNV-1a delle voci UI Automation (0x77000000|hash)
     * e da qualunque ownerHwnd reale. */
    constexpr uint32_t kSyntheticSystemUidBase = 0x7F000000u;

    int added = 0;
    int updated = 0;
    int removed = 0;
    bool anyBitmapChange = false;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        for (const Win11TrayItem& item : items) {
            const TrayIconKey key{ 0, item.uid };
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                TrayIconEntry entry;
                entry.key          = key;
                entry.fromWin11Uia = true;
                entry.fromExplorer = false;
                entry.tooltip      = item.name;
                entry.ownerPath    = item.exePath;
                entry.bitmap       = item.bitmap;
                entry.pixelHash    = ArgbHash(entry.bitmap);
                entry.iconRevision = entry.bitmap.empty() ? 0 : 1;
                entry.isPinned     = !item.hidden;
                entry.hiddenDesired = item.hidden;
                /* Lo stato viaggia nello stesso campo di NIM_SETVERSION:
                 * il frontend legge isHidden da qui. */
                entry.state        = item.hidden ? NIS_HIDDEN : 0;
                entry.sysChecked   = true;
                entry.systemKind   = item.kind;
                /* Il tipo viaggia anche nel campo guidKey: e' l'unico
                 * campo di W7T_TrayIconInfo che il livello gestito puo'
                 * leggere per sapere che si tratta del volume, della rete
                 * o della batteria ricreati. */
                if (item.kind == SystemIconKind::Network) {
                    entry.guidKey = L"uia:network";
                } else if (item.kind == SystemIconKind::Volume) {
                    entry.guidKey = L"uia:volume";
                } else if (item.kind == SystemIconKind::Battery) {
                    entry.guidKey = L"uia:battery";
                }
                entry.toolbarId = EnsureToolbarId(key);
                m_icons[key] = std::move(entry);
                m_order.push_back(key);
                ++added;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_ADD, 0, key.uid);
            } else {
                TrayIconEntry& entry = it->second;
                entry.missCount = 0;
                entry.lastReadFailed = false;

                bool changed = false;
                if (entry.tooltip != item.name) {
                    entry.tooltip = item.name;
                    changed = true;
                }
                if (entry.isPinned != !item.hidden) {
                    /* La barra di Windows 11 ha l'ultima parola su dove sta
                     * l'icona: il pin locale non la sposta. */
                    entry.isPinned = !item.hidden;
                    changed = true;
                }
                if (entry.bitmap.empty() && !item.bitmap.empty()) {
                    entry.bitmap = item.bitmap;
                    entry.pixelHash = ArgbHash(entry.bitmap);
                    ++entry.iconRevision;
                    anyBitmapChange = true;
                    changed = true;
                }
                if (changed) {
                    ++updated;
                    CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, 0,
                                                     key.uid);
                }
            }
        }

        /* ------------------------------------------------------------------ */
        /*  v2.61 - LE TRE ICONE DI SISTEMA CHE LA SHELL NON ESPONE            */
        /*                                                                    */
        /*  Da Windows 11 22H2 (e ancora su 24H2) volume e rete non sono piu'  */
        /*  pulsanti separati della tray: la shell ne disegna UNO solo, il     */
        /*  centro delle notifiche rapide, e i tre stati vivono dentro il suo  */
        /*  riquadro. La lettura UI Automation riporta quindi solo cio' che    */
        /*  esiste davvero - per esempio la batteria - e la tray restava con   */
        /*  una sola icona.                                                    */
        /*                                                                    */
        /*  Qui i tipi che la shell non espone vengono DISEGNATI da noi con    */
        /*  gli stessi glifi del ripiego di Windows 10 (TrayFallbackIcons      */
        /*  segue lo stato corrente: volume, connessione, batteria) e il clic  */
        /*  apre il riquadro nativo corrispondente (vedi SendClick).           */
        /*                                                                    */
        /*  Regola di precedenza: appena la shell espone QUEL tipo, la voce    */
        /*  sintetica non viene piu' rinnovata e sparisce da sola dopo due     */
        /*  letture (la shell ha sempre l'ultima parola). Limite dichiarato    */
        /*  nella documentazione (docs/Windows11.md): su Windows 11 la tray    */
        /*  mostra le icone che la shell fornisce PIU' le nostre tre.          */
        /* ------------------------------------------------------------------ */
        static const SystemIconKind kSyntheticKinds[] = {
            SystemIconKind::Volume,
            SystemIconKind::Network,
            SystemIconKind::Battery,
        };
        const PropStrings& sysNames = PropStringsFor(CurrentLanguage());
        auto syntheticLabel = [&sysNames](SystemIconKind kind) {
            const wchar_t* raw = (kind == SystemIconKind::Volume) ? sysNames.lblVolume
                               : (kind == SystemIconKind::Network) ? sysNames.lblNetwork
                                                                   : sysNames.lblBattery;
            std::wstring word(raw != nullptr ? raw : L"");
            /* Le etichette delle Proprieta' finiscono con ':' ("Volume:\"). */
            while (!word.empty() && (word.back() == L':' || word.back() == L' ')) {
                word.pop_back();
            }
            return word;
        };
        auto syntheticGuidKey = [](SystemIconKind kind) -> const wchar_t* {
            switch (kind) {
                case SystemIconKind::Volume:  return L"uia:volume";
                case SystemIconKind::Network: return L"uia:network";
                default:                      return L"uia:battery";
            }
        };

        for (SystemIconKind kind : kSyntheticKinds) {
            if (presentKinds.count(kind) != 0) {
                continue;   /* la shell la espone: vince la sua */
            }

            const uint32_t uid = kSyntheticSystemUidBase |
                                 static_cast<uint32_t>(kind);
            const TrayIconKey key{ 0, uid };
            /* Inserita tra le "presenti": la passata di rimozione qui sotto
             * non deve portarla via. */
            present.insert(uid);

            ArgbBitmap glyph;
            const bool drawn = TrayFallbackIcons::Render(kind, glyph);

            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                if (!drawn) {
                    continue;   /* niente stato da disegnare: meglio il vuoto */
                }
                TrayIconEntry entry;
                entry.key           = key;
                entry.fromWin11Uia  = true;    /* nessun HWND proprietario   */
                entry.syntheticKind = kind;    /* il clic apre il riquadro   */
                entry.tooltip       = syntheticLabel(kind);
                entry.bitmap        = std::move(glyph);
                entry.pixelHash     = ArgbHash(entry.bitmap);
                entry.iconRevision  = 1;
                entry.usingFallback = true;
                entry.isPinned      = true;
                entry.hiddenDesired = false;
                entry.state         = 0;
                entry.sysChecked    = true;
                entry.systemKind    = kind;
                entry.guidKey       = syntheticGuidKey(kind);
                entry.toolbarId     = EnsureToolbarId(key);
                m_icons[key] = std::move(entry);
                m_order.push_back(key);
                ++added;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_ADD, 0, uid);
                continue;
            }

            TrayIconEntry& entry = it->second;
            entry.missCount = 0;
            entry.lastReadFailed = false;

            /* Cambio di lingua: l'etichetta della voce sintetica e' nostra,
             * quindi si riallinea qui (le voci della shell portano il nome
             * che da' lei). */
            const std::wstring label = syntheticLabel(kind);
            if (entry.tooltip != label) {
                entry.tooltip = label;
                ++updated;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, 0, uid);
            }

            if (!drawn) {
                continue;
            }
            const uint64_t hash = ArgbHash(glyph);
            if (hash == entry.pixelHash) {
                continue;
            }
            entry.bitmap        = std::move(glyph);
            entry.pixelHash     = hash;
            entry.usingFallback = true;
            ++entry.iconRevision;
            anyBitmapChange = true;
            ++updated;
            CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, 0, uid);
        }

        /* Rimozione delle voci della tray di Windows 11 sparite. Due
         * assenze consecutive, come per le icone di Explorer: una lettura
         * transitoria non fa sparire nulla. */
        std::vector<TrayIconKey> toRemove;
        for (auto& pair : m_icons) {
            TrayIconEntry& entry = pair.second;
            if (!entry.fromWin11Uia) {
                continue;
            }
            if (present.count(pair.first.uid) != 0) {
                continue;
            }
            if (++entry.missCount >= 2) {
                toRemove.push_back(pair.first);
            }
        }
        for (const TrayIconKey& key : toRemove) {
            RemoveEntryLocked(key);
            ++removed;
        }
    }

    if (added != 0 || updated != 0 || removed != 0 || anyBitmapChange) {
        size_t total = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            total = m_icons.size();
        }
        wchar_t line[160] = {};
        swprintf(line, 160,
                 L"tray Win11: +%d ~%d -%d voci (modello a %u)",
                 added, updated, removed, static_cast<unsigned>(total));
        AppendCoreLog(line);
        SyncToolbarModel();
        TrayOverflowWindow::NotifyTrayChanged();
    }
}

SystemIconKind TrayService::KindOf(uint64_t ownerHwnd, uint32_t uid) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_icons.find(TrayIconKey{ ownerHwnd, uid });
    if (it == m_icons.end()) {
        return SystemIconKind::None;
    }
    return it->second.systemKind;
}

void CALLBACK TrayService::TrayHostChangedProc(HWINEVENTHOOK, DWORD,
                                               HWND hwnd, LONG idObject,
                                               LONG idChild, DWORD, DWORD) {
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != 0) {
        return;
    }

    TrayService& self = Instance();
    if (!self.m_win11Tray || !self.m_running.load()) {
        return;
    }

    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, 128) == 0) {
        return;
    }
    const bool isTrayHost =
        wcsstr(cls, L"TopLevelWindowForOverflowXamlIsland") != nullptr ||
        wcsstr(cls, L"DesktopWindowContentBridge") != nullptr ||
        _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
        _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
    if (!isTrayHost) {
        return;
    }

    /* Il debounce e' gia' quello delle riconciliazioni: piu' eventi vicini
     * diventano una sola lettura. */
    self.ScheduleReconcile(kReconcileUiaTray, 250);
}

int32_t TrayService::CopyTo(W7T_TrayIconInfo* buffer, int32_t capacity) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    const int32_t total = static_cast<int32_t>(m_icons.size());
    if (buffer == nullptr) {
        return total;
    }
    if (capacity < total) {
        return W7T_ERR_BUFFER_TOO_SMALL;
    }

    int32_t index = 0;
    for (const TrayIconKey& key : m_order) {
        auto it = m_icons.find(key);
        if (it == m_icons.end()) {
            continue;
        }
        const TrayIconEntry& entry = it->second;
        W7T_TrayIconInfo& info = buffer[index++];
        ZeroMemory(&info, sizeof(info));
        info.ownerHwnd       = entry.key.ownerHwnd;
        info.uid             = entry.key.uid;
        info.callbackMessage = entry.callbackMessage;
        info.iconRevision    = entry.iconRevision;
        info.isPinned        = entry.isPinned ? 1 : 0;
        info.isHidden        = (entry.state & NIS_HIDDEN) ? 1 : 0;
        info.version         = entry.version;
        CopyToFixed(info.tooltip, W7T_MAX_TOOLTIP, entry.tooltip);
        CopyToFixed(info.guidKey, 64, entry.guidKey);
    }
    return index;
}

int32_t TrayService::GetIconBitmap(uint64_t ownerHwnd, uint32_t uid,
                                   int32_t* width, int32_t* height,
                                   uint8_t* pixels, int32_t pixelsBytes) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_icons.find(TrayIconKey{ ownerHwnd, uid });
    if (it == m_icons.end()) {
        return W7T_ERR_NOT_FOUND;
    }
    return EmitBitmap(it->second.bitmap, width, height, pixels, pixelsBytes);
}

int32_t TrayService::SetPinned(uint64_t ownerHwnd, uint32_t uid, int32_t pinned) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_icons.find(TrayIconKey{ ownerHwnd, uid });
        if (it == m_icons.end()) {
            return W7T_ERR_NOT_FOUND;
        }
        it->second.isPinned = (pinned != 0);
        it->second.hiddenPending = 0;

        /* Ricorda la scelta: l'applicazione non rimandera' NIM_ADD, quindi
         * senza questa memoria al riavvio l'icona tornerebbe al predefinito.
         * E' l'equivalente di cio' che Windows fa con PromotedIconStreams. */
        SavePinPreference(MakePreferenceName(ownerHwnd, uid), pinned != 0);

        CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, ownerHwnd, uid);
    }
    /* Il pulsante reale segue il modello: TBSTATE_HIDDEN nella nostra
     * ToolbarWindow32, esattamente come fa la toolbar di Explorer. */
    SyncToolbarModel();
    TrayOverflowWindow::NotifyTrayChanged();   /* v3.1: aggiorna il pannello */
    return W7T_OK;
}

int32_t TrayService::MoveIcon(uint64_t sourceHwnd, uint32_t sourceUid,
                              uint64_t targetHwnd, uint32_t targetUid,
                              int32_t insertAfter) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        const TrayIconKey source{ sourceHwnd, sourceUid };
        const TrayIconKey target{ targetHwnd, targetUid };

        auto srcIt = std::find_if(m_order.begin(), m_order.end(),
                                  [&](const TrayIconKey& k) {
                                      return !(k < source) && !(source < k);
                                  });
        auto tgtIt = std::find_if(m_order.begin(), m_order.end(),
                                  [&](const TrayIconKey& k) {
                                      return !(k < target) && !(target < k);
                                  });
        if (srcIt == m_order.end() || tgtIt == m_order.end() || srcIt == tgtIt) {
            return W7T_ERR_NOT_FOUND;
        }

        const TrayIconKey moved = *srcIt;
        m_order.erase(srcIt);
        tgtIt = std::find_if(m_order.begin(), m_order.end(),
                             [&](const TrayIconKey& k) {
                                 return !(k < target) && !(target < k);
                             });
        const auto where = insertAfter != 0 ? std::next(tgtIt) : tgtIt;
        m_order.insert(where, moved);

        CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, sourceHwnd, sourceUid);
    }
    SyncToolbarModel();   /* TB_MOVEBUTTON: il pulsante reale cambia posto */
    TrayOverflowWindow::NotifyTrayChanged();   /* v3.1 */
    return W7T_OK;
}

bool TrayService::GetLastBalloon(W7T_BalloonInfo* out) {
    if (out == nullptr) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_hasBalloon) {
        return false;
    }
    memcpy(out, &m_lastBalloon, sizeof(W7T_BalloonInfo));
    return true;
}

/* ------------------------------------------------------------------ */
/*  Inoltro dei click al proprietario dell'icona                       */
/* ------------------------------------------------------------------ */

namespace {

/* Processi che ospitano i flyout di sistema: SndVol (volume di Win7),
 * Explorer (rete, batteria), ShellExperienceHost (Win10/11). */
/* v3.0: congela il resize del flyout orologio con API PUBBLICHE:
 * SetWindowsHookEx(WH_CALLWNDPROC, thread bersaglio) fa caricare la
 * minuscola W7TInject.dll nel processo del flyout; la callback, girando
 * li' dentro, applica SetWindowSubclass e risponde HTBORDER ai lati in
 * WM_NCHITTEST: bordi Aero conservati, resize disattivato. Se la DLL non
 * si carica resta comunque lo snap-back del watcher come ripiego. */
namespace {
HINSTANCE s_hInjectDll = nullptr;
HHOOK     s_hFreezeHook = nullptr;
HWND      s_frozenFlyout = nullptr;

HHOOK s_hFreezeHookProbe() { return s_hFreezeHook; }

void UninstallFlyoutFreeze() {
    if (s_frozenFlyout != nullptr && IsWindow(s_frozenFlyout)) {
        const UINT u = RegisterWindowMessageW(L"W7T_UnfreezeSize");
        if (u != 0) {
            SendMessageW(s_frozenFlyout, u, 0, 0);  /* rimuove il subclass nel bersaglio */
        }
    }
    if (s_hFreezeHook != nullptr) {
        UnhookWindowsHookEx(s_hFreezeHook);
        s_hFreezeHook = nullptr;
    }
    s_frozenFlyout = nullptr;
}

void InstallFlyoutFreeze(HWND flyout);   /* fwd decl */

void RetryFlyoutFreeze(HWND flyout) {
    extern HHOOK s_hFreezeHookProbe();
    /* probe dichiarato sotto: evita di esporre lo stato */
    if (s_hFreezeHookProbe() == nullptr) {
        InstallFlyoutFreeze(flyout);
    }
}

void InstallFlyoutFreeze(HWND flyout) {
    if (flyout == nullptr || s_frozenFlyout == flyout) return;
    UninstallFlyoutFreeze();
    SetPropW(flyout, L"W7T_FreezeSize", reinterpret_cast<HANDLE>(1));
    if (s_hInjectDll == nullptr) {
        s_hInjectDll = LoadLibraryW(L"W7TInject.dll");
    }
    if (s_hInjectDll == nullptr) return;   /* ripiego: snap-back del watcher */
    auto proc = reinterpret_cast<HOOKPROC>(
        GetProcAddress(s_hInjectDll, "W7TInject_CallWndProc"));
    if (proc == nullptr) return;
    const DWORD tid = GetWindowThreadProcessId(flyout, nullptr);
    if (tid == 0) return;
    s_hFreezeHook = SetWindowsHookExW(WH_CALLWNDPROC, proc, s_hInjectDll, tid);
    s_frozenFlyout = flyout;
}
} /* namespace */

bool IsFlyoutProcess(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return false;
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) {
        return false;
    }
    wchar_t path[MAX_PATH] = {};
    DWORD len = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(proc, 0, path, &len) != FALSE;
    CloseHandle(proc);
    if (!ok || len == 0) {
        return false;
    }
    const std::wstring image(path);
    const size_t slash = image.find_last_of(L"\\/");
    const std::wstring name = (slash == std::wstring::npos)
                            ? image : image.substr(slash + 1);
    return _wcsicmp(name.c_str(), L"sndvol.exe") == 0
        || _wcsicmp(name.c_str(), L"explorer.exe") == 0
        || _wcsicmp(name.c_str(), L"ShellExperienceHost.exe") == 0
        || _wcsicmp(name.c_str(), L"StartMenuExperienceHost.exe") == 0;
}

BOOL CALLBACK CollectTopWindows(HWND hwnd, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<HWND>*>(lParam);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (!IsWindowVisible(hwnd) || (style & WS_CHILD) != 0) {
        return TRUE;
    }
    /* I flyout sono spesso popup OWNED (SndVol, Explorer): il vecchio
     * filtro li scartava e l'osservatore non li vedeva mai, lasciando il
     * flyout dove si era aperto da solo. Restano esclusi i menu (#32768),
     * i tooltip e le finestre IME, che sono owned anch'essi. */
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)) - 1);
    if (_wcsicmp(cls, L"#32768") == 0
        || _wcsicmp(cls, L"tooltips_class32") == 0
        || _wcsicmp(cls, L"MSCTFIME UI") == 0) {
        return TRUE;
    }
    RECT r = {};
    if (!GetWindowRect(hwnd, &r) || (r.right - r.left) <= 0 || (r.bottom - r.top) <= 0) {
        return TRUE;
    }
    list->push_back(hwnd);
    return TRUE;
}

/* Gap fra icona e flyout scalato col DPI, stile Aero Flyout Fix. */
int FlyoutGapDpi() {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    UINT dpi = 96;
    if (fn != nullptr) {
        if (HWND desk = GetDesktopWindow()) {
            const UINT d = fn(desk);
            if (d >= 96) {
                dpi = d;
            }
        }
    }
    return MulDiv(4, static_cast<int>(dpi), 96);
}

} /* namespace */

void TrayService::SetShellRects(const RECT& bar, const RECT& notify) {
    if (m_trayWnd == nullptr) {
        return;
    }
    SetWindowPos(m_trayWnd, nullptr,
                 static_cast<int>(bar.left), static_cast<int>(bar.top),
                 static_cast<int>(bar.right - bar.left),
                 static_cast<int>(bar.bottom - bar.top),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_notifyWnd != nullptr) {
        /* La figlia e' in coordinate rispetto alla padre. */
        SetWindowPos(m_notifyWnd, nullptr,
                     static_cast<int>(notify.left - bar.left),
                     static_cast<int>(notify.top - bar.top),
                     static_cast<int>(notify.right - notify.left),
                     static_cast<int>(notify.bottom - notify.top),
                     SWP_NOZORDER | SWP_NOACTIVATE);

        /* Il toolbar del modello occupa la stessa area: i suoi
         * TB_GETITEMRECT + ClientToScreen sono gia' in coordinate schermo
         * coerenti con la barra. */
        RECT local = { 0, 0,
                       notify.right - notify.left,
                       notify.bottom - notify.top };
        TrayToolbar::Instance().SetArea(local);
    }
}

void TrayService::PlaceFlyout(HWND flyout, const RECT& iconRect) {
    if (flyout == nullptr || !IsWindow(flyout)) {
        return;
    }
    RECT fr = {};
    if (!GetWindowRect(flyout, &fr)) {
        return;
    }
    const LONG w = fr.right - fr.left;
    const LONG h = fr.bottom - fr.top;
    if (w <= 0 || h <= 0) {
        return;
    }

    /* Come Windows: centrato in orizzontale sull'icona e subito sopra,
     * mai fuori dall'area di lavoro del monitor (multi-monitor, DPI). */
    HMONITOR mon = MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) {
        return;
    }
    const RECT wa = mi.rcWork;
    /* Il flyout deve agganciarsi completamente all'icona: gap minimo
     * (2 px a 96 DPI), scalato col DPI come la mod Aero Flyout Fix. */
    const int gap = FlyoutGapDpi() / 2;

    /* Bordo della barra dedotto dalla posizione dell'icona nell'area di
     * lavoro: il flyout si apre dal lato opposto alla barra, centrato
     * sull'icona. */
    const LONG iconCx = iconRect.left + (iconRect.right - iconRect.left) / 2;
    const LONG iconCy = iconRect.top + (iconRect.bottom - iconRect.top) / 2;
    LONG x, y;
    if (iconRect.top >= wa.bottom - 2) {          /* barra in basso */
        x = iconCx - w / 2;
        y = iconRect.top - h - gap;
    } else if (iconRect.bottom <= wa.top + 2) {   /* barra in alto */
        x = iconCx - w / 2;
        y = iconRect.bottom + gap;
    } else if (iconRect.left >= wa.right - 2) {   /* barra a destra */
        x = iconRect.left - w - gap;
        y = iconCy - h / 2;
    } else if (iconRect.right <= wa.left + 2) {   /* barra a sinistra */
        x = iconRect.right + gap;
        y = iconCy - h / 2;
    } else {                                      /* icona lontana dai bordi */
        x = iconCx - w / 2;
        y = iconRect.top - h - gap;
    }

    if (x + w > wa.right  - gap) x = wa.right  - w - gap;
    if (y + h > wa.bottom - gap) y = wa.bottom - h - gap;
    if (x < wa.left + gap) x = wa.left + gap;
    if (y < wa.top  + gap) y = wa.top  + gap;
    SetWindowPos(flyout, nullptr, static_cast<int>(x), static_cast<int>(y),
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* v2.29: un ancoraggio valido deve essere un rettangolo plausibile:
 * all'avvio con la taskbar non ancora pronta i rettangoli icona possono
 * essere degeneri (0,0) e il flyout finiva "in alto". Con ancora non
 * valida il posizionamento viene saltato finche' non arriva un
 * rettangolo vero (il loop di manutenzione riprova a ogni giro). */
namespace {
bool ValidAnchorRect(const RECT& r) {
    return (r.right - r.left) >= 4 && (r.bottom - r.top) >= 4;
}
} /* namespace */

void TrayService::StartFlyoutWatcher(const TrayIconKey& key) {
    /* Se un altro flyout e' aperto (es. orologio) e l'utente clicca il
     * volume, il watcher vecchio va annullato e uno nuovo parte: cosi' il
     * volume si aggancia comunque. Il contatore di generazione invalida
     * il thread precedente. */
    const int myGen = ++m_watcherGeneration;

    {
        std::lock_guard<std::mutex> lk(m_anchorMutex);
        if (m_anchor.flyout != nullptr && IsWindowVisible(m_anchor.flyout)
            && !(m_anchor.key < key) && !(key < m_anchor.key)) {
            return;   /* flyout gia' agganciato su quest'icona: niente doppioni */
        }
        if (m_anchor.flyout != nullptr && (m_anchor.key < key || key < m_anchor.key)) {
            m_anchor.flyout = nullptr;
        }
    }
    m_watcherRunning.exchange(true);

    RECT iconRect = {};
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto r = m_iconRects.find(key);
        if (r == m_iconRects.end()) {
            m_watcherRunning.store(false);
            return;
        }
        iconRect = r->second;
    }

    std::vector<HWND> before;
    EnumWindows(CollectTopWindows, reinterpret_cast<LPARAM>(&before));

    std::thread([this, key, iconRect, before, myGen] {
        for (int i = 0; i < 25 && m_running.load(); ++i) {
            if (myGen != m_watcherGeneration.load()) {
                m_watcherRunning.store(false);
                return;
            }
            Sleep(100);
            std::vector<HWND> now;
            EnumWindows(CollectTopWindows, reinterpret_cast<LPARAM>(&now));
            HWND found = nullptr;
            for (HWND h : now) {
                if (std::find(before.begin(), before.end(), h) == before.end()
                    && IsFlyoutProcess(h)) {
                    found = h;
                    break;
                }
            }
            if (found != nullptr) {
                {
                    std::lock_guard<std::mutex> lk(m_anchorMutex);
                    m_anchor.flyout = found;
                    m_anchor.key    = key;
                }

                /* v2.29: rileggi il rettangolo icona ADESSO (poteva essere
                 * degenero al momento della cattura) e non piazzare il
                 * flyout su un'ancora non valida. */
                RECT fresh = iconRect;
                {
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    auto rr = m_iconRects.find(key);
                    if (rr != m_iconRects.end()) {
                        fresh = rr->second;
                    }
                }
                if (!ValidAnchorRect(fresh)) {
                    RECT tb{};
                    std::lock_guard<std::recursive_mutex> lock(m_mutex);
                    auto e = m_icons.find(key);
                    if (e != m_icons.end() && e->second.toolbarId != 0 &&
                        TrayToolbar::Instance().GetItemScreenRect(
                            e->second.toolbarId, tb) &&
                        ValidAnchorRect(tb)) {
                        m_iconRects[key] = tb;
                        fresh = tb;
                    }
                }
                const RECT placeRect = fresh;
                ApplyAeroFlyoutStyle(found);
                if (ValidAnchorRect(placeRect)) {
                    PlaceFlyout(found, placeRect);
                } else {
                    AppendCoreLog(
                        L"flyout: ancora non valida, il loop riprovera'");
                }
                /* v2.30: memorizza la dimensione d'apertura: il loop la
                 * riafferma, cosi' il flyout non e' ridimensionabile. */
                RECT fr{};
                if (GetWindowRect(found, &fr)) {
                    std::lock_guard<std::mutex> lk(m_anchorMutex);
                    m_anchorSize.cx = fr.right - fr.left;
                    m_anchorSize.cy = fr.bottom - fr.top;
                }
                m_haveClockFixed = false;   /* v2.7: ricattura sotto */
                AppendCoreLog(L"flyout di sistema agganciato alla propria icona");
                break;
            }
        }

        /* Manutenzione dell'ancoraggio: molti flyout (SndVol, Explorer)
         * si riposizionano da soli anche DOPO essersi aperti: finche' il
         * flyout e' visibile si riapplica l'ancoraggio, cosi' vince sempre
         * l'aggancio all'icona, su qualsiasi build. */
        /* v2.7: il ciclo dura finche' il flyout e' visibile (non piu' solo
         * ~19 s): serve anche a riaffermare la dimensione fissa del flyout
         * orologio contro i tentativi di resize dell'utente. */
        int clockGuardIter = 0;
        for (int i = 0; i < 7200 && m_running.load(); ++i) {
            if (myGen != m_watcherGeneration.load()) {
                UninstallFlyoutFreeze();
                m_watcherRunning.store(false);
                return;
            }
            Sleep(150);
            HWND flyout = nullptr;
            TrayIconKey anchorKey{};
            {
                std::lock_guard<std::mutex> lk(m_anchorMutex);
                flyout    = m_anchor.flyout;
                anchorKey = m_anchor.key;
            }
            if (flyout == nullptr || !IsWindowVisible(flyout)) {
                UninstallFlyoutFreeze();
                std::lock_guard<std::mutex> lk(m_anchorMutex);
                if (m_anchor.flyout == flyout) {
                    m_anchor.flyout = nullptr;
                }
                break;
            }
            RECT live = {};
            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                auto r = m_iconRects.find(anchorKey);
                if (r == m_iconRects.end()) {
                    break;
                }
                live = r->second;
            }
            /* v2.29: niente riposizionamento su ancora degenera; se il
             * rettangolo manca, prova a ricavarlo dalla toolbar modello. */
            if (!ValidAnchorRect(live)) {
                RECT tb{};
                auto e = m_icons.find(anchorKey);
                if (e != m_icons.end() && e->second.toolbarId != 0 &&
                    TrayToolbar::Instance().GetItemScreenRect(
                        e->second.toolbarId, tb) &&
                    ValidAnchorRect(tb)) {
                    m_iconRects[anchorKey] = tb;
                    live = tb;
                }
            }
            /* v2.30: riafferma la dimensione fissa: se l'utente (o un
             * layout XAML) l'ha cambiata, si torna a quella d'apertura. */
            {
                SIZE want{};
                {
                    std::lock_guard<std::mutex> lk(m_anchorMutex);
                    want = m_anchorSize;
                }
                if (want.cx > 0 && want.cy > 0) {
                    RECT cur{};
                    if (GetWindowRect(flyout, &cur)) {
                        const LONG cw = cur.right - cur.left;
                        const LONG ch = cur.bottom - cur.top;
                        if (cw != want.cx || ch != want.cy) {
                            SetWindowPos(flyout, nullptr, 0, 0, want.cx,
                                         want.cy,
                                         SWP_NOMOVE | SWP_NOZORDER |
                                         SWP_NOACTIVATE);
                        }
                    }
                }
            }
            ApplyAeroFlyoutStyle(flyout);
            if (ValidAnchorRect(live)) {
                PlaceFlyout(flyout, live);
            }
            ApplyAeroFlyoutStyle(flyout);

            /* v2.7: flyout orologio NON ridimensionabile ma con i bordi Aero
             * (WS_THICKFRAME) conservati, come fa la mod del flyout di
             * connessione col suo WM_NCHITTEST: qui non possiamo subclassare
             * una finestra di un altro processo, quindi riaffermiamo la
             * dimensione catturata a ogni ciclo - il resize dell'utente
             * non attecchisce. La cattura avviene dopo qualche ciclo, quando
             * l'animazione d'apertura si e' conclusa. */
            wchar_t cls[64] = {};
            GetClassNameW(flyout, cls, static_cast<int>(std::size(cls)));
            if (wcscmp(cls, L"ClockFlyoutWindow") == 0) {
                ++clockGuardIter;
                RECT r{};
                if (!m_haveClockFixed && clockGuardIter >= 3 && GetWindowRect(flyout, &r)) {
                    m_clockFixedRect = r;
                    m_haveClockFixed = true;
                    InstallFlyoutFreeze(flyout);   /* v3.0: API pubblica */
                } else if (m_haveClockFixed && GetWindowRect(flyout, &r)) {
                    /* v3.3: se l'iniezione era fallita la prima volta
                     * (DLL bloccata, processo appena partito) si riprova:
                     * il flyout NON deve restare ridimensionabile. Lo
                     * snap-back qui sotto resta comunque attivo. */
                    RetryFlyoutFreeze(flyout);
                    const LONG lw = r.right - r.left;
                    const LONG lh = r.bottom - r.top;
                    const LONG fw = m_clockFixedRect.right - m_clockFixedRect.left;
                    const LONG fh = m_clockFixedRect.bottom - m_clockFixedRect.top;
                    if (lw != fw || lh != fh) {
                        SetWindowPos(flyout, nullptr, r.left, r.top, fw, fh,
                                     SWP_NOZORDER | SWP_NOACTIVATE);
                    }
                }
            }
        }
        m_watcherRunning.store(false);
    }).detach();
}

void TrayService::ReanchorFlyouts() {
    FlyoutAnchor anchor;
    {
        std::lock_guard<std::mutex> lk(m_anchorMutex);
        anchor = m_anchor;
        if (anchor.flyout == nullptr || !IsWindowVisible(anchor.flyout)) {
            m_anchor.flyout = nullptr;
            return;
        }
    }
    RECT rect = {};
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_iconRects.find(anchor.key);
        if (it == m_iconRects.end()) {
            return;
        }
        rect = it->second;
    }
    PlaceFlyout(anchor.flyout, rect);
}

/* ------------------------------------------------------------------ */
/*  v2.6: compatibilita' con la mod Windhawk "Windows 7 Network         */
/*  Flyout Recreation" (>= 5.0.0, quella con il supporto RetroBar).     */
/*                                                                      */
/*  Come funziona la mod: la sua istanza dentro explorer.exe mette in   */
/*  subclass la toolbar vera delle notifiche e, su WM_LBUTTONUP sul     */
/*  pulsante di rete, apre il flyout classico al posto di quello        */
/*  moderno; inoltre pubblica owner+uID dell'icona di rete su una       */
/*  finestra "Win7NetFlyout_TrayInfoWnd" che risponde al messaggio      */
/*  registrato "Win7NetFlyout_QueryNetworkIcon" (wParam 0 = HWND        */
/*  owner, 2 = uID), lo stesso protocollo che usa l'istanza della mod   */
/*  dentro RetroBar.exe.                                                */
/*                                                                      */
/*  Se noi inoltrassimo il clic come al solito (SendNotifyMessageW      */
/*  all'owner), explorer proverebbe ad aprire il flyout MODERNO e la    */
/*  mod lo sopprimerebbe: risultato, non si apre nulla. Quindi: se la   */
/*  mod e' presente e l'icona cliccata e' proprio quella di rete        */
/*  pubblicata, sintetizziamo un clic VERO sulla toolbar di explorer    */
/*  (WM_LBUTTONDOWN+WM_LBUTTONUP al centro del pulsante): il subclass   */
/*  della mod lo intercetta e apre il flyout classico ancorato          */
/*  all'icona, esattamente come fa con i clic di RetroBar.              */
/* ------------------------------------------------------------------ */
bool TrayService::TryWindhawkNetFlyoutClick(uint64_t ownerHwnd, uint32_t uid) {
    const UINT queryMsg = RegisterWindowMessageW(L"Win7NetFlyout_QueryNetworkIcon");
    if (queryMsg == 0) {
        return false;   /* nessuno ha registrato il messaggio: mod assente */
    }

    bool isModNetworkIcon = false;
    for (HWND hInfo = FindWindowExW(nullptr, nullptr, L"Win7NetFlyout_TrayInfoWnd", nullptr);
         hInfo != nullptr && !isModNetworkIcon;
         hInfo = FindWindowExW(nullptr, hInfo, L"Win7NetFlyout_TrayInfoWnd", nullptr)) {
        DWORD_PTR rHwnd = 0;
        DWORD_PTR rUid = 0;
        if (!SendMessageTimeoutW(hInfo, queryMsg, 0 /*TRAYINFO_HWND*/, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &rHwnd) ||
            rHwnd == 0 || rHwnd != ownerHwnd) {
            continue;
        }
        if (!SendMessageTimeoutW(hInfo, queryMsg, 2 /*TRAYINFO_UID*/, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &rUid) ||
            rUid == 0) {
            continue;
        }
        isModNetworkIcon = (static_cast<uint32_t>(rUid) == uid);
    }

    if (!isModNetworkIcon) {
        return false;
    }

    /* Toolbar vera delle notifiche di explorer (stessa catena della mod). */
    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    HWND hNotify = hTray ? FindWindowExW(hTray, nullptr, L"TrayNotifyWnd", nullptr) : nullptr;
    HWND hPager = hNotify ? FindWindowExW(hNotify, nullptr, L"SysPager", nullptr) : nullptr;
    HWND hToolbar = nullptr;
    if (hPager) {
        hToolbar = FindWindowExW(hPager, nullptr, L"ToolbarWindow32", nullptr);
    }
    if (hToolbar == nullptr && hNotify != nullptr) {
        hToolbar = FindWindowExW(hNotify, nullptr, L"ToolbarWindow32", nullptr);
    }
    if (hToolbar == nullptr) {
        return false;
    }

    /* Rettangolo reale a schermo del pulsante di questa icona, dal nostro
     * modello della toolbar (TB_GETITEMRECT + ClientToScreen). */
    RECT rc{};
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_icons.find(TrayIconKey{ ownerHwnd, uid });
        if (it == m_icons.end() || it->second.toolbarId == 0) {
            return false;
        }
        if (!TrayToolbar::Instance().GetItemScreenRect(it->second.toolbarId, rc)) {
            return false;
        }
    }
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return false;
    }

    POINT pt{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    ScreenToClient(hToolbar, &pt);
    const LPARAM lp = MAKELPARAM(pt.x, pt.y);
    PostMessageW(hToolbar, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    PostMessageW(hToolbar, WM_LBUTTONUP, 0, lp);
    return true;
}

int32_t TrayService::SendClick(uint64_t ownerHwnd, uint32_t uid, int32_t clickType,
                               int32_t x, int32_t y) {
    uint32_t callbackMessage = 0;
    uint32_t version = 0;
    bool uiaEntry = false;
    SystemIconKind syntheticKind = SystemIconKind::None;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_icons.find(TrayIconKey{ ownerHwnd, uid });
        if (it == m_icons.end()) {
            return W7T_ERR_NOT_FOUND;
        }
        callbackMessage = it->second.callbackMessage;
        version = it->second.version;
        uiaEntry = it->second.fromWin11Uia;
        syntheticKind = it->second.syntheticKind;
    }

    /* v2.61 - Icone di sistema ricreate da noi (la tray di Windows 11 non
     * le espone: vedi ApplyWin11TraySnapshot). Non c'e' nessun elemento UI
     * Automation da invocare: il clic apre il riquadro nativo del tipo,
     * esattamente come se l'icona fosse quella di Explorer. Il tasto
     * centrale resta senza azione, come per le altre voci della tray. */
    if (syntheticKind != SystemIconKind::None) {
        if (clickType == W7T_TRAY_CLICK_MIDDLE) {
            return W7T_ERR_INVALID_ARG;
        }
        if (syntheticKind == SystemIconKind::Volume) {
            return FlyoutLauncher::ShowVolumeFlyout(m_trayWnd);
        }
        const FlyoutKind flyout = (syntheticKind == SystemIconKind::Network)
            ? FlyoutKind::Network : FlyoutKind::Battery;
        return FlyoutLauncher::InvokeFlyout(flyout, FlyoutAction::Show, m_trayWnd);
    }

    /* v2.60 - Voci della tray di Windows 11: non esiste nessun proprietario
     * a cui mandare il messaggio di callback (l'icona e' disegnata da
     * explorer.exe dentro un'isola XAML). Il clic va al pattern di
     * accessibilita' dell'elemento: e' il meccanismo con cui lo apre anche
     * la tastiera di sistema. */
    if (uiaEntry) {
        const bool right = clickType == W7T_TRAY_CLICK_RIGHT;
        const bool middle = clickType == W7T_TRAY_CLICK_MIDDLE;
        if (middle) {
            /* Il tasto centrale non ha un pattern equivalente: si lascia
             * stare, meglio di un'azione sbagliata. */
            return W7T_ERR_INVALID_ARG;
        }
        return Win11TrayReader::Instance().RequestClick(uid, right)
             ? W7T_OK : W7T_ERR_NOT_FOUND;
    }

    HWND owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd));
    if (owner == nullptr || !IsWindow(owner)) {
        return W7T_ERR_NOT_FOUND;
    }
    if (callbackMessage == 0) {
        return W7T_ERR_INVALID_ARG;
    }

    /* Fedele alla semantica ManagedShell NotifyIcon.SendMessage (il codice
     * che fa funzionare i clic in RetroBar):
     *   SendNotifyMessage(hwnd, callbackMessage,
     *                     Version > 3 ? mouse : UID,
     *                     message | (Version > 3 ? UID << 16 : 0))
     * con "mouse" = coordinate schermo impacchettate (x in LOWORD, y in
     * HIWORD). */
    const uint32_t mouse = static_cast<uint32_t>(static_cast<uint16_t>(x))
                         | (static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16);
    auto send = [&](UINT mouseMsg) {
        if (version > 3) {
            SendNotifyMessageW(owner, callbackMessage,
                               static_cast<WPARAM>(mouse),
                               static_cast<LPARAM>(mouseMsg | (uid << 16)));
        } else {
            SendNotifyMessageW(owner, callbackMessage,
                               static_cast<WPARAM>(uid),
                               static_cast<LPARAM>(mouseMsg));
        }
    };

    switch (clickType) {
        case W7T_TRAY_CLICK_LEFT_DOWN: {
            /* Pressione del tasto sinistro: ManagedShell (IconMouseDown)
             * distingue singolo/doppio clic con GetDoubleClickTime. */
            static std::mutex downMutex;
            static std::map<TrayIconKey, DWORD> lastDown;
            const DWORD now = GetTickCount();
            bool dbl = false;
            {
                std::lock_guard<std::mutex> lk(downMutex);
                DWORD& last = lastDown[TrayIconKey{ ownerHwnd, uid }];
                dbl = (last != 0)
                   && (now - last <= static_cast<DWORD>(GetDoubleClickTime()));
                last = now;
            }
            send(dbl ? WM_LBUTTONDBLCLK : WM_LBUTTONDOWN);
            break;
        }

        case W7T_TRAY_CLICK_LEFT:
            /* v2.6: se c'e' la mod Windhawk del flyout di rete classico,
             * lasciamo che sia la mod a gestire il clic (clic sintetizzato
             * sulla toolbar vera) e NON tocchiamo l'owner: altrimenti
             * explorer aprirebbe il flyout moderno e la mod lo
             * sopprimerebbe, quindi non si aprirebbe nulla. */
            if (TryWindhawkNetFlyoutClick(ownerHwnd, uid)) {
                break;
            }
            /* ManagedShell (IconMouseUp): SEMPRE WM_LBUTTONUP, piu'
             * NIN.SELECT dalla versione 3 ("documented as version 4, but
             * Explorer does this for version 3 as well"). */
            send(WM_LBUTTONUP);
            if (version >= 3) {
                send(NIN_SELECT);
            }
            /* Il flyout che il clic fa aprire (volume, rete, batteria...)
             * va agganciato sopra la propria icona: osserviamo per un paio
             * di secondi le finestre nuove dei processi flyout. */
            StartFlyoutWatcher(TrayIconKey{ ownerHwnd, uid });
            break;

        case W7T_TRAY_CLICK_RIGHT:
            /* ManagedShell: SEMPRE WM_RBUTTONUP, piu' WM_CONTEXTMENU dalla
             * versione 3. */
            send(WM_RBUTTONUP);
            if (version >= 3) {
                send(WM_CONTEXTMENU);
            }
            break;

        case W7T_TRAY_CLICK_DOUBLE:
            send(WM_LBUTTONDBLCLK);
            break;

        case W7T_TRAY_CLICK_MIDDLE:
            send(WM_MBUTTONUP);
            break;

        default:
            return W7T_ERR_INVALID_ARG;
    }

    return W7T_OK;
}

} /* namespace w7t */
