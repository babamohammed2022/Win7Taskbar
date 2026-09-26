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

#include "RegistryPolicy.h"   /* Regola README: write con backup rilevato */

/* v2.62: i riquadri di Windows 7 per le icone di sistema ricreate. */
#include "BatteryFlyout.h"
#include "Win7NetworkFlyout.h"
#include "Win8NetworkFlyout.h"   /* v3.8: variante Windows 8 del flyout rete */
#include "NetLogicBridge.h"      /* v3.8: esclusione reciproca coi riquadri  */

#include "TrayOverflowWindow.h"   /* v3.1: refresh conservativo del pannello */
#include "../include/RaiiWrappers.h"
#include "ExplorerTrayReader.h"
#include "Win11TrayReader.h"
#include "TrayToolbar.h"
#include "NotificationPageSync.h"
#include "LegacyToolbarShim.h"
#include "SehGuard.h"   /* v3.15: reti SEH sui confini verso la shell */
#include "TrayFallbackIcons.h"
#include "TrayPrefsStore.h"
#include "SystemEventsWatch.h"
#include "JumpListWindow.h"  /* cached jump list section cap invalidation */
#include "PreviewPolicy.h"   /* cached user preview policy invalidation   */
#include "TaskbarButtonNotify.h" /* G7: probe of the shell TaskbarButtonCreated message */
#include <powrprof.h>
#include <windows.h>
#include <wtsapi32.h>
#include "WindowManager.h"  /* per segnalare il lampeggio delle finestre */
#include "AppBarService.h"  /* per ri-nascondere la barra dopo un riavvio di Explorer */
#include "FlyoutLauncher.h"  /* ApplyAeroFlyoutStyle: bordi Aero dei flyout */
#include "ShellMenu.h"       /* v3.5: menu contestuali delle icone ricreate */
#include "Strings.h"         /* PropStringsFor: etichette dei tipi di sistema */
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <iterator>
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
/* Il percorso del processo serve solo per diagnostica e per associare dati
 * della shell. L'identita' della cache resta GUID quando dichiarato, altrimenti
 * la coppia hWnd+uID; due registrazioni dello stesso processo non si fondono
 * per nome o tooltip. */
std::wstring OwnerPathOf(uint64_t ownerHwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(
        reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd)), &pid);
    std::wstring p = GetProcessImagePath(pid);
    std::transform(p.begin(), p.end(), p.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return p;
}

/* QueryFullProcessImageName può essere negata da integrità/UAC. Il nome del
 * processo resta però la prova minima necessaria per non inviare
 * WM_COPYDATA a una finestra omonima di un altro shell replacement. */
bool IsExplorerPid(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    raii::GenericHandle process(OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) {
        return false;
    }
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (QueryFullProcessImageNameW(process.get(), 0, path, &length) == FALSE ||
        length == 0) {
        return false;
    }
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* name = slash == nullptr ? path : slash + 1;
    return _wcsicmp(name, L"explorer.exe") == 0;
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

/* Lo shim ToolbarWindow32 è deliberatamente spento nella build distribuita.
 * Per una verifica locale si può abilitarlo senza modificare il percorso
 * della tray con WIN7TASKBAR_ENABLE_LEGACY_TOOLBAR_SHIM=1, oppure compilando
 * con W7T_ENABLE_LEGACY_TOOLBAR_SHIM=1. Non esiste un'attivazione implicita. */
#ifndef W7T_ENABLE_LEGACY_TOOLBAR_SHIM
#define W7T_ENABLE_LEGACY_TOOLBAR_SHIM 0
#endif

bool LegacyToolbarShimEnabled() noexcept {
#if W7T_ENABLE_LEGACY_TOOLBAR_SHIM
    return true;
#else
    wchar_t value[16] = {};
    const DWORD length = GetEnvironmentVariableW(
        L"WIN7TASKBAR_ENABLE_LEGACY_TOOLBAR_SHIM", value,
        static_cast<DWORD>(std::size(value)));
    if (length == 0 || length >= std::size(value)) {
        return false;
    }
    return _wcsicmp(value, L"1") == 0 || _wcsicmp(value, L"true") == 0 ||
           _wcsicmp(value, L"yes") == 0;
#endif
}

/* ------------------------------------------------------------------ */
/*  Per-icon visibility preferences                                    */
/*                                                                     */
/*  Windows keeps its "promoted out of the overflow" list in           */
/*  IconStreams/PromotedIconStreams under TrayNotify: an obfuscated    */
/*  binary blob that differs between builds. What belongs here is the  */
/*  SAME CONCEPT, not the same format: the user's choice must survive  */
/*  restarts, because applications send NIM_ADD exactly once and,      */
/*  without memory, every icon would fall back to its default.        */
/*                                                                     */
/*  v1.7.6 - THE STORAGE IS NO LONGER THE REGISTRY. Choices live in    */
/*  trayicons.ini (TrayPrefsStore) under %LOCALAPPDATA%\Win7Taskbar,   */
/*  the same folder as toolbars.ini that the user deletes by hand to   */
/*  reset everything. Each saved per-icon preference has three states */
/*  (show / only notifications / hide), so the                         */
/*  per-icon value is no longer a bool "on the bar or not" but the     */
/*  full behavior. The old HKCU\SOFTWARE\Win7Taskbar\TrayIconPrefs2   */
/*  key is imported ONCE on the first start of the new format and then */
/*  DELETED: from here on the program leaves no persistent registry    */
/*  trace for this feature at all.                                    */
/*                                                                     */
/*  Key = application exe + uid, like Windows does (which also adds    */
/*  the GUID).                                                         */
/* ------------------------------------------------------------------ */

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
    /* La formattazione CRT con suffisso di sicurezza non è disponibile in
     * tutti i toolchain MinGW usati dal progetto. Il formato contiene al
     * massimo dieci cifre per uint32_t e il buffer è sovradimensionato. */
    wsprintfW(suffix, L"#%u", static_cast<unsigned>(uid));
    return identity + suffix;
}

/* v1.7.6: every single access to the preference goes through the
 * TrayPrefsStore (the trayicons.ini file). No registry access is left in
 * this file: not here, not in the dialog, not in the restore - the
 * project's "zero-footprint" contract for this feature. */

int32_t SavedBehaviorForName(const std::wstring& name) {
    return TrayPrefsStore::Instance().Behavior(name);
}

bool HasSavedPreferenceName(const std::wstring& name) {
    return TrayPrefsStore::Instance().Has(name);
}

/* Persist one entry's three-state choice and mirror it in the model
 * (the caller already holds m_mutex; persistence is immediate because
 * the stored set is tiny). */
void PersistBehavior(uint64_t ownerHwnd, uint32_t uid, int32_t behavior,
                     TrayIconEntry& entry) {
    entry.userBehavior = behavior;
    /* The three-state choice also redefines the legacy "in bar" bool:
     * only the first of the three positions shows the icon on the bar. */
    entry.isPinned = (behavior == kBehaviorShow);
    TrayPrefsStore::Instance().SetBehavior(
        MakePreferenceName(ownerHwnd, uid), behavior);
}

/* The saved preference (show / only notifications / hide), or the
 * Windows rule when there is none. Fills BOTH isPinned and userBehavior
 * of a freshly created entry: the two must never diverge. */
void ApplySavedBehavior(uint64_t ownerHwnd, uint32_t uid,
                        bool windowsDefault, TrayIconEntry& entry) {
    const int32_t saved = SavedBehaviorForName(MakePreferenceName(ownerHwnd, uid));
    if (saved >= kBehaviorShow && saved <= kBehaviorHide) {
        entry.userBehavior = saved;
        entry.isPinned = (saved == kBehaviorShow);
        return;
    }
    entry.userBehavior = kBehaviorNone;
    entry.isPinned = windowsDefault;
}

/* EnableAutoTray is READ (never written) and can only change from the
 * Windows tray panel: the resolver consumes it for every icon on every
 * pass, so it lives here behind a cache that the registry watcher
 * invalidates (ApplyShellVisibilityDefaults calls Reset). */
std::atomic<int> g_autoTrayCache{-1};

bool AutoTrayEnabledCached() {
    int v = g_autoTrayCache.load(std::memory_order_relaxed);
    if (v < 0) {
        v = IsAutoTrayEnabled() ? 1 : 0;
        g_autoTrayCache.store(v, std::memory_order_relaxed);
    }
    return v != 0;
}

void ResetAutoTrayCache() {
    g_autoTrayCache.store(-1, std::memory_order_relaxed);
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

/* v2.63 - Quanti popup visibili ha aperto qualcun altro adesso.
 *
 * Serve alla verifica differita del riquadro batteria: quando il clic viene
 * inoltrato al pulsante vero della shell, Windows apre il riquadro Win32 di
 * Windows 7 (chiave UseWin32BatteryFlyout). Se non lo apre - puo' succedere
 * su una build in cui quel percorso non risponde - l'utente non deve restare
 * con un clic senza effetto: si confronta questo numero prima e dopo e, se
 * non e' comparso nulla, si mostra il riquadro ricreato. Le finestre nostre
 * non contano: il confronto e' fra "prima del clic" e "dopo il clic". */
struct PopupCountContext { int count; };

static BOOL CALLBACK CountPopupEnumProc(HWND hwnd, LPARAM param) {
    PopupCountContext* ctx = reinterpret_cast<PopupCountContext*>(param);
    if (ctx == nullptr) {
        return FALSE;
    }
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return TRUE;   /* le nostre finestre non sono il riquadro di Windows */
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_POPUP) != 0 || (style & WS_DLGFRAME) != 0) {
        ++ctx->count;
    }
    return TRUE;
}

int CountVisibleForeignPopups() {
    PopupCountContext ctx{ 0 };
    EnumWindows(CountPopupEnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.count;
}

/* v3.5 - Insieme delle finestre esterne (altri processi) VISIBILI adesso.
 * Serve alla verifica differita della batteria: se dopo il clic compare
 * una finestra che prima non c'era, la shell ha aperto qualcosa (il
 * riquadro Win32 di Windows 7) e il ricreato non deve partire. Non si
 * guarda lo stile: il riquadro vero a volte e' una finestra senza
 * WS_POPUP/WS_DLGFRAME e il vecchio conteggio lo perdeva. */
static BOOL CALLBACK CollectForeignVisibleProc(HWND hwnd, LPARAM param) {
    auto* set = reinterpret_cast<std::set<uint64_t>*>(param);
    if (set == nullptr) {
        return FALSE;
    }
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return TRUE;   /* le nostre finestre non sono il riquadro di Windows */
    }
    set->insert(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(hwnd)));
    return TRUE;
}

static void CollectForeignVisibleWindows(std::set<uint64_t>& out) {
    out.clear();
    EnumWindows(CollectForeignVisibleProc,
                reinterpret_cast<LPARAM>(&out));
}

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
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_trayNotifySnapshotImported = false;
    }
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

int32_t TrayService::NotificationPageBackfill() {
    if (!m_running.load()) {
        AppendCoreLog(L"[notification-page] servizio tray non attivo");
        return W7T_ERR_NOT_INIT;
    }
    if (!IsWindows11OrBetter()) {
        AppendCoreLog(L"[notification-page] Windows 11 non rilevato, nessun reset");
        return W7T_ERR_NOT_FOUND;
    }

    std::vector<NotificationPageIcon> icons;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        icons.reserve(m_order.size());
        for (const TrayIconKey& key : m_order) {
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                continue;
            }

            TrayIconEntry& entry = it->second;
            bool barVisible = false;
            bool presentSomewhere = false;
            ResolveVisibilityLocked(entry, barVisible, presentSomewhere);

            const HWND owner = reinterpret_cast<HWND>(
                static_cast<uintptr_t>(key.ownerHwnd));
            DWORD pid = 0;
            if (owner != nullptr) {
                GetWindowThreadProcessId(owner, &pid);
            }

            NotificationPageIcon icon;
            icon.exePath = entry.ownerPath;
            if (icon.exePath.empty() && pid != 0) {
                icon.exePath = GetProcessImagePath(pid);
            }
            icon.displayName = entry.tooltip;
            icon.promoted = presentSomewhere && barVisible &&
                (entry.state & entry.stateMask & NIS_HIDDEN) == 0;
            icon.pid = pid;
            icon.ownerHwnd = key.ownerHwnd;
            icon.uid = key.uid;
            icon.order = static_cast<uint32_t>(icons.size());
            icons.push_back(std::move(icon));
        }
    }

    const std::wstring pageLog =
        L"[notification-page] modello tray fotografato: " +
        std::to_wstring(icons.size()) + L" voci";
    AppendCoreLog(pageLog.c_str());
    return NotificationPageSync::BackfillLegacyPage(icons);
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
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            /* Una nuova istanza di Explorer ha una nuova fotografia COM:
             * il callback iniziale va acquisito di nuovo, senza cancellare
             * la cache esistente se il nuovo backend fallisce. */
            m_trayNotifySnapshotImported = false;
        }
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
    raii::GenericHandle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                          FALSE, pid));
    if (proc) {
        wchar_t path[MAX_PATH] = {};
        DWORD len = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(proc.get(), 0, path, &len) != FALSE &&
            len > 0) {
            const std::wstring image(path);
            const size_t slash = image.find_last_of(L"\\/");
            const std::wstring name = (slash == std::wstring::npos)
                                    ? image : image.substr(slash + 1);
            isExplorer = _wcsicmp(name.c_str(), L"explorer.exe") == 0;
        }
    }
    cache[pid] = isExplorer;
    return isExplorer;
}

} /* namespace */

void TrayService::ImportTrayNotifySnapshot(bool force) {
    if (!force || !m_running.load()) {
        return;
    }

    bool shouldRead = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!m_trayNotifySnapshotImported) {
            /* Si marca prima della chiamata per evitare due callback COM
             * contemporanei. In caso di errore il flag torna falso e il
             * prossimo giro puo' riprovare. */
            m_trayNotifySnapshotImported = true;
            shouldRead = true;
        }
    }
    if (!shouldRead) {
        return;
    }

    std::vector<TrayNotifySnapshotItem> snapshot;
    if (!TrayNotifyReader::ReadSnapshot(snapshot)) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_trayNotifySnapshotImported = false;
        return;
    }

    MergeTrayNotifySnapshot(snapshot);
}

void TrayService::MergeTrayNotifySnapshot(
        const std::vector<TrayNotifySnapshotItem>& items) {
    int added = 0;
    int updated = 0;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (const TrayNotifySnapshotItem& item : items) {
            if (item.ownerHwnd == 0 || !IsWindow(
                    reinterpret_cast<HWND>(static_cast<uintptr_t>(item.ownerHwnd)))) {
                continue;
            }

            TrayIconKey key{ item.ownerHwnd, item.uid };
            std::wstring guidKey;
            if (!IsNullGuid(item.guidItem)) {
                guidKey = GuidToString(item.guidItem);
                auto known = m_byGuid.find(guidKey);
                if (known != m_byGuid.end() &&
                    m_icons.find(known->second) != m_icons.end() &&
                    m_icons.find(key) == m_icons.end()) {
                    RebindByGuid(known->second, key);
                }
            }
            LogTagged(L"TRAY",
                      L"event=%u source=TrayNotify callback identity=%s hwnd=%016I64X uid=%u pref=%d",
                      static_cast<unsigned>(item.event),
                      guidKey.empty() ? L"hWnd+uID" : guidKey.c_str(),
                      static_cast<unsigned long long>(item.ownerHwnd),
                      static_cast<unsigned>(item.uid),
                      static_cast<int>(item.preference));

            auto found = m_icons.find(key);
            if (found == m_icons.end()) {
                TrayIconEntry entry;
                entry.key = key;
                entry.fromTrayNotify = true;
                entry.shellPreference = item.preference;
                entry.tooltip = item.tooltip;
                entry.ownerPath = OwnerPathOf(key.ownerHwnd);
                if (entry.ownerPath.empty()) {
                    /* exeName è solo metadato, ma è utile quando il processo
                     * non consente QueryFullProcessImageName. */
                    entry.ownerPath = item.exeName;
                }
                entry.bitmap = item.bitmap;
                entry.pixelHash = ArgbHash(entry.bitmap);
                entry.iconRevision = entry.bitmap.empty() ? 0 : 1;

                /* La preferenza COM è un default iniziale; non viene trattata
                 * come prova assoluta della posizione visuale. Toolbar reale,
                 * overflow aperto e UIA possono correggerla; una preferenza
                 * locale esplicita ha sempre precedenza. */
                const bool defaultPinned =
                    item.preference == 2 ||
                    (item.preference == 0 && !AutoTrayEnabledCached());
                ApplySavedBehavior(key.ownerHwnd, key.uid, defaultPinned, entry);
                entry.hiddenDesired = !defaultPinned;
                entry.lastUpdateSource = L"TrayNotify callback";
                if (!guidKey.empty()) {
                    entry.guidKey = guidKey;
                    m_byGuid[guidKey] = key;
                }
                entry.toolbarId = EnsureToolbarId(key);
                m_icons[key] = std::move(entry);
                m_order.push_back(key);
                ++added;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_ADD,
                                                 key.ownerHwnd, key.uid);
                continue;
            }

            TrayIconEntry& entry = found->second;
            entry.fromTrayNotify = true;
            entry.shellPreference = item.preference;
            if (!guidKey.empty() && entry.guidKey.empty()) {
                entry.guidKey = guidKey;
                m_byGuid[guidKey] = key;
            }
            /* WM_COPYDATA è la fonte live per tooltip e HICON; il callback
             * iniziale non deve sovrascriverla con una fotografia piu'
             * vecchia. Si completa solo ciò che manca. */
            bool changed = false;
            if (entry.tooltip.empty() && !item.tooltip.empty()) {
                entry.tooltip = item.tooltip;
                changed = true;
            }
            if (entry.ownerPath.empty()) {
                entry.ownerPath = OwnerPathOf(key.ownerHwnd);
                if (entry.ownerPath.empty()) {
                    entry.ownerPath = item.exeName;
                }
            }
            if (entry.bitmap.empty() && !item.bitmap.empty()) {
                entry.bitmap = item.bitmap;
                entry.pixelHash = ArgbHash(entry.bitmap);
                entry.iconRevision = 1;
                changed = true;
            }
            entry.lastUpdateSource = L"TrayNotify callback";
            if (changed) {
                ++updated;
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY,
                                                 key.ownerHwnd, key.uid);
            }
        }
    }

    if (added != 0 || updated != 0) {
        wchar_t line[160] = {};
        swprintf(line, 160,
                 L"tray notify: cache +%d ~%d (unica cache, voci=%u)",
                 added, updated,
                 static_cast<unsigned>(items.size()));
        AppendCoreLog(line);
        SyncToolbarModel();
        TrayOverflowWindow::NotifyTrayChanged();
    }
}

void TrayService::ReconcileWithExplorer(uint32_t sources) {
    /* Il callback COM fornisce una fotografia iniziale di identita', tooltip
     * e preferenze anche quando la toolbar legacy è vuota. Viene acquisito
     * solo all'avvio o dopo TaskbarCreated: non è polling e non sostituisce
     * gli eventi WM_COPYDATA/UIA. */
    const bool requestTrayNotify =
        (sources & (kReconcileInitial | kReconcileExplorer)) != 0;
    ImportTrayNotifySnapshot(requestTrayNotify);

    /* Windows 11 non ha nessuna toolbar della tray da leggere: la passata
     * classica finirebbe in "lettura non valida" a ogni giro. Il modello lo
     * riempie il lettore UI Automation, che risponde in modo asincrono su
     * kMsgUiaTray (vedi ApplyWin11TraySnapshot). La detection viene ripetuta
     * anche quando la toolbar legacy risponde validamente: all'avvio il
     * bridge XAML puo' essere creato dopo la prima fotografia. */
    if (!m_win11Tray) {
        EnableWin11Tray();
    }
    if (m_win11Tray) {
        /* v2.61: alimentazione e rete cambiano il DISEGNO delle nostre tre
         * icone. Si aggiornano subito, senza aspettare la lettura della
         * shell (che comunque parte qui sotto). */
        if (sources & (kReconcilePower | kReconcileNetwork | kReconcileInitial
                       | kReconcileExplorer)) {
            int added = 0, updated = 0;
            bool pixel = false;
            EnsureSyntheticSystemIcons(nullptr, nullptr, &added, &updated,
                                       &pixel);
            if (added != 0 || updated != 0 || pixel) {
                SyncToolbarModel();
                TrayOverflowWindow::NotifyTrayChanged();
            }
        }
        Win11TrayReader::Instance().RequestRead();

        /* Il flyout Windows 11 è la sola fonte pubblica osservabile delle
         * icone già confinate nell'overflow. Si apre tramite il controllo UIA
         * reale, ma in modalità silenziosa: il lettore sposta la finestra XAML
         * fuori schermo, raccoglie l'isola e la chiude con ESC. Una richiesta
         * riuscita avvia il debounce di circa 20 s; le riconciliazioni ravvicinate
         * (incluso il secondo giro d'avvio) non aprono altri flyout. */
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG last = m_lastWin11OverflowHarvestTick.load();
        if (last == 0 || now - last >= kWin11OverflowHarvestDebounceMs) {
            RECT anchor = {};
            {
                std::lock_guard<std::recursive_mutex> lock(m_mutex);
                anchor = m_chevronRect;
            }
            if (anchor.right <= anchor.left || anchor.bottom <= anchor.top) {
                if (m_notifyWnd != nullptr) {
                    GetWindowRect(m_notifyWnd, &anchor);
                }
            }
            if (Win11TrayReader::Instance().RequestOverflowFlyout(anchor, true)) {
                m_lastWin11OverflowHarvestTick.store(now);
                AppendCoreLog(L"tray Win11: raccolta overflow silenziosa richiesta");
            }
        }
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
    /* v3.8: un'icona confermata sparita dalla toolbar completa mentre il
     * suo owner e' ancora vivo chiede il ripiego broadcast FUORI dal lock. */
    bool vanishedWhileAlive = false;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        /* 1) Aggiunte: cio' che Explorer mostra e noi non abbiamo. Se la
         *    chiave esiste gia' perche' l'applicazione ci ha parlato coi
         *    copia-dati, QUELLA voce e' piu' aggiornata: non si tocca. */
        for (const ExplorerTrayItem& item : read.items) {
            TrayIconKey key{ item.ownerHwnd, item.uid };
            if (!IsNullGuid(item.guidItem)) {
                const std::wstring guidKey = GuidToString(item.guidItem);
                auto known = m_byGuid.find(guidKey);
                if (known != m_byGuid.end() &&
                    m_icons.find(known->second) != m_icons.end() &&
                    m_icons.find(key) == m_icons.end()) {
                    RebindByGuid(known->second, key);
                }
            }
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

            /* In Windows, Explorer's "hidden" icons live in the hidden
             * icons panel, not on the bar. But if the user already made a
             * choice in OUR tray, that one is newer and wins (v1.7.6: the
             * choice can also be "hide everything", not just bar/overflow). */
            ApplySavedBehavior(key.ownerHwnd, key.uid, !item.hidden, entry);
            entry.hiddenDesired = item.hidden;

            if (!IsNullGuid(item.guidItem)) {
                entry.guidKey = GuidToString(item.guidItem);
                m_byGuid[entry.guidKey] = key;
            }

            entry.ownerPath = OwnerPathOf(key.ownerHwnd);
            entry.toolbarId = EnsureToolbarId(key);
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
            TrayIconKey key{ item.ownerHwnd, item.uid };
            if (!IsNullGuid(item.guidItem)) {
                const std::wstring guidKey = GuidToString(item.guidItem);
                auto known = m_byGuid.find(guidKey);
                if (known != m_byGuid.end() &&
                    m_icons.find(known->second) != m_icons.end() &&
                    m_icons.find(key) == m_icons.end()) {
                    RebindByGuid(known->second, key);
                }
            }
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                continue;
            }
            TrayIconEntry& entry = it->second;
            /* WM_COPYDATA è la fonte live per le applicazioni. La toolbar
             * Explorer può confermare l'esistenza, ma non deve riscrivere
             * tooltip, bitmap, stato o versione di una voce già acquisita
             * dal percorso applicativo. Le voci della shell (owner Explorer)
             * restano invece aggiornabili dalla toolbar. */
            if (entry.fromCopyData && !entry.ownerIsExplorer) {
                entry.missCount = 0;
                entry.lastReadFailed = false;
                continue;
            }
            entry.missCount = 0;
            entry.lastReadFailed = false;
            entry.lastUpdateSource = L"Explorer toolbar";

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
                    /* Anche quando il registro contiene IsPromoted, la
                     * decisione visuale viene dalla toolbar/overflow. Il
                     * valore privato resta solo metadato e non salta
                     * l'isteresi. */
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
            /* Il percorso resta metadato: la cache non fonde registrazioni
             * sulla base della tooltip o del processo. */
            if (entry.ownerPath.empty()) {
                entry.ownerPath = OwnerPathOf(key.ownerHwnd);
            }

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
                /* v3.8: owner VIVO ma icona confermata assente dalla
                 * lettura completa: candidato al ripiego broadcast. */
                vanishedWhileAlive = true;
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

    /* v3.8 - ripiego "icone sparite" (ispirazione dalla mod "Disappearing
     * Tray Icons Fix" della collezione Windhawk, MIT; solo l'idea: niente
     * codice Windhawk e nessun hook): un'icona e' stata CONFERMATA assente
     * dalla toolbar letta completamente mentre la finestra del suo owner e'
     * ancora viva. E' il caso classico della shell che perde le
     * registrazioni: le applicazioni rispondono a TaskbarCreated
     * ri-registrando le icone (e' cio' che devono gia' fare al riavvio di
     * Explorer), quindi il broadcast le fa tornare; la riconciliazione per
     * GUID ricuce le ri-registrazioni.
     *
     * Rispetta la storia di questo file: MAI all'avvio (il broadcast di
     * andata causava doppie registrazioni: la regola li' resta
     * ascolto-soltanto), MAI all'uscita del servizio (il broadcast finale
     * non si tocca), una sola volta per sessione, e fuori dal lock perche'
     * la destinazione e' l'intero sistema. */
    if (vanishedWhileAlive && !m_disappearedIconBroadcastDone &&
        m_taskbarCreatedMsg != 0) {
        m_disappearedIconBroadcastDone = true;
        AppendCoreLog(L"reconcile: icona confermata sparita con owner vivo: "
                      L"broadcast TaskbarCreated di ripiego (una volta)");
        SendNotifyMessageW(HWND_BROADCAST, m_taskbarCreatedMsg, 0, 0);
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
    /* v1.7.6: this IS the registry watcher's entry point: drop the rule
     * cache first, then read it back so every consumer sees the fresh
     * value. */
    ResetAutoTrayCache();
    /* La regola di Windows per le icone SENZA preferenza espressa:
     * EnableAutoTray attivo  ->  nuova icona nasce nell'overflow;
     * EnableAutoTray spento  ->  nasce sulla barra.
     * E' la stessa chiave che legge Explorer, riletta qui quando il
     * watcher sul registro segnola un cambiamento, non in polling.
     * Le icone che arrivano da Explorer mantengono lo stato della SUA
     * toolbar; la regola si applica a quelle che si sono registrate da
     * noi senza che l'utente abbia mai scelto nulla. */
    const bool autoTray = AutoTrayEnabledCached();

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
    /* v1.7.6: a saved choice is now the three-state behavior in the
     * TrayPrefsStore (file): ANY explicit state, "hide" included, outranks
     * the shell layout. */
    return HasSavedPreferenceName(MakePreferenceName(key.ownerHwnd, key.uid));
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
    TrayToolbar& model = TrayToolbar::Instance();
    const bool modelReady = model.Create(m_notifyWnd, GetModuleHandleW(nullptr));
    if (!modelReady) {
        LegacyToolbarShim::Instance().Destroy();
        AppendCoreLog(L"tray: ToolbarWindow32 principale non creata");
        return;
    }

    /* La compatibilità è opt-in e il controllo è una vera finestra figlia
     * del SysPager già creato dal modello. Se la creazione fallisce, il
     * percorso principale resta intatto e ogni handle parziale viene pulito
     * dal distruttore/RAII dello shim. */
    if (LegacyToolbarShimEnabled()) {
        if (!LegacyToolbarShim::Instance().Create(
                model.PagerHandle(), GetModuleHandleW(nullptr))) {
            LegacyToolbarShim::Instance().Destroy();
            AppendCoreLog(L"tray: shim ToolbarWindow32 opt-in non creato");
        } else {
            AppendCoreLog(L"tray: shim ToolbarWindow32 opt-in attivo");
        }
    } else {
        LegacyToolbarShim::Instance().Destroy();
    }
}

void TrayService::SyncToolbarModel() {
    /* Il toolbar vive sul thread del servizio. Chiamate da altri thread
     * (la prima riconciliazione gira sul worker di import) NON devono
     * fare SendMessage mentre il mutex e' preso: il thread dei messaggi
     * potrebbe aspettarlo proprio in ApplyMessage e si incepperebbe. Si
     * rimanda il lavoro a se' stesso con un PostMessage coalescente. */
    if (m_threadId.load() != 0 && GetCurrentThreadId() != m_threadId.load()) {
        if (m_trayWnd != nullptr && IsWindow(m_trayWnd)) {
            PostMessageW(m_trayWnd, kMsgToolbarSync, 0, 0);
        }
        return;
    }

    TrayToolbar& tb = TrayToolbar::Instance();

    std::vector<uint32_t> ids;
    std::vector<uint32_t> hiddenIds;
    std::vector<std::pair<uint32_t, bool>> flags;
    std::vector<std::pair<uint32_t, std::wstring>> texts;
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
            /* v1.7.6: real visibility comes from the single resolver
             * (saved choice + Always show all + the system switches).
             * NIS_HIDDEN stays the APPLICATION's request and keeps
             * counting on its own. */
            bool barVisible = false, present = false;
            ResolveVisibilityLocked(entry, barVisible, present);
            const bool hidden = !present || !barVisible
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
            texts.emplace_back(entry.toolbarId, entry.tooltip);
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
    for (const auto& text : texts) {
        tb.SetButtonText(text.first, text.second);
    }
}

void TrayService::SyncLegacyToolbarShim() {
    if (!LegacyToolbarShimEnabled()) {
        return;
    }

    /* Tutte le SendMessage TB_* devono restare sul thread proprietario delle
     * finestre. Il punto che notifica il pannello overflow può invece essere
     * raggiunto da un worker: si accoda una sola sincronizzazione e non si
     * crea un trigger parallelo per ogni icona. */
    if (m_threadId.load() != 0 && GetCurrentThreadId() != m_threadId.load()) {
        if (m_trayWnd != nullptr && IsWindow(m_trayWnd) &&
            !m_legacyShimPosted.exchange(true)) {
            if (!PostMessageW(m_trayWnd, kMsgLegacyShim, 0, 0)) {
                m_legacyShimPosted.store(false);
            }
        }
        return;
    }
    m_legacyShimPosted.store(false);
    /* Se un worker aveva già accodato la richiesta e nel frattempo il punto
     * overflow è arrivato sul thread proprietario, elimina il messaggio
     * residuo: una sola fotografia deve produrre una sola sync. */
    if (m_trayWnd != nullptr && GetCurrentThreadId() == m_threadId.load()) {
        MSG pending = {};
        while (PeekMessageW(&pending, m_trayWnd, kMsgLegacyShim,
                            kMsgLegacyShim, PM_REMOVE) != FALSE) {
        }
    }

    std::vector<LegacyToolbarShimItem> items;
    std::set<uint32_t> usedCommands;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        items.reserve(m_order.size());
        for (const TrayIconKey& key : m_order) {
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                continue;
            }
            const TrayIconEntry& entry = it->second;
            uint32_t command = key.uid;
            if (command == 0 || usedCommands.count(command) != 0) {
                /* uID è il command primario. Se due proprietari usano lo
                 * stesso uID, il valore di ripiego è un hash deterministico
                 * della coppia TrayIconKey: il controllo conserva comunque
                 * un idCommand stabile e non confonde i due pulsanti. */
                uint64_t value = key.ownerHwnd ^
                    (static_cast<uint64_t>(key.uid) * 0x9E3779B97F4A7C15ull);
                value ^= value >> 33;
                value *= 0xFF51AFD7ED558CCDull;
                value ^= value >> 33;
                command = static_cast<uint32_t>(value) | 0x40000000u;
                if (command == 0) {
                    command = 0x40000001u;
                }
                while (usedCommands.count(command) != 0) {
                    ++command;
                    if (command == 0) {
                        command = 0x40000001u;
                    }
                }
            }
            usedCommands.insert(command);
            LegacyToolbarShimItem item;
            item.ownerHwnd = key.ownerHwnd;
            item.uid = key.uid;
            item.command = command;
            bool barVisible = false;
            bool present = false;
            ResolveVisibilityLocked(entry, barVisible, present);
            item.hidden = !present || !barVisible ||
                (entry.state & entry.stateMask & NIS_HIDDEN) != 0;
            item.bitmap = entry.bitmap;
            items.push_back(std::move(item));
        }
    }
    LegacyToolbarShim::Instance().Sync(items);
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
    /* v3.15: il WinEvent entra nel dispatch di events del sistema: un
     * fault qui (lettura icone, lock) blocca la catena degli eventi
     * accessibilita' per TUTTE le finestre, incluse quelle di Explorer.
     * Doppia rete come sugli altri confini. */
    W7T_SEH_TRY {
        try {
            TrayService::Instance().NotifyOwnerDiedAsync(hwnd);
        } catch (...) {
        }
    } W7T_SEH_CATCH {
    } W7T_SEH_END
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
            /* Il watcher osserva sia Explorer sia il ramo Control Panel che
             * contiene NotifyIconSettings. Una modifica a IsPromoted deve
             * rileggere la toolbar e la configurazione Windows 11, non una
             * euristica locale. */
            AppendCoreLog(L"configurazione tray cambiata: riapplico la regola di visibilita'");
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

/* ------------------------------------------------------------------ */
/*  v3.10.1 - Battery legacy-key hardening (forward decls).
 *  Le funzioni sono definite piu' in basso, nel blocco dedicato ai
 *  riquadri di sistema, ma Stop() e ThreadMain() - che le chiamano -
 *  vivono sopra e hanno bisogno di vederle. */
static void EnsureWin32BatteryFlyoutValue();
static void RestoreWin32BatteryFlyoutValue();
static void RecoverBatteryFlyoutKeyFromBackup();
static void InstallBatteryKeyCrashGuard();
static void OnBatteryKeySessionEnding();

void TrayService::Stop() {
    /* OPZIONE B: ripristino INCONDIZIONATO della chiave legacy
     * UseWin32BatteryFlyout, PRIMA del controllo su m_running: anche se il
     * servizio risulta non avviato (Start fallita, Stop doppia, ...) una
     * chiusura regolare mentre un clic-batteria e' ancora in volo
     * (timer/UIA/watch) non deve mai lasciare la chiave a 1. Il core
     * nativo e' l'UNICO scrittore di questa chiave: il livello gestito
     * non la scrive mai. */
    RestoreWin32BatteryFlyoutValue();
    if (!m_running.load()) {
        return;
    }

    /* v2.37 punto 15: interruzione cooperativa. Prima si chiede alle
     * letture della toolbar di Explorer di fermarsi (la passata in corso
     * cede al pulsante successivo), poi si spegne il flag e si invia
     * WM_QUIT al thread della tray. */
    RequestAbortReads();
    m_running.store(false);
    if (m_threadId.load() != 0) {
        PostThreadMessageW(m_threadId.load(), WM_QUIT, 0, 0);
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
    m_threadId.store(0);

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

    /* v1.21.32: responder of the taskbar-list protocol.
     *
     * Microsoft implements CLSID_TaskbarList/ITaskbarList inside the shell,
     * whose AddTab/DeleteTab/ActivateTab reach the taskbar through the
     * window found by class name ("Shell_TrayWnd") - the same lookup
     * Shell_NotifyIcon performs. Since this program registers that class
     * name for its own notification area (see TrayService.h), ITaskbarList
     * calls can land here as well: the query that resolves the taskbar
     * window (WM_USER + 236) must find a real, answering window, otherwise
     * the application that issued it stays with a taskbar list that does
     * nothing and can wait forever on a reply that never comes.
     *
     * This window IS that reply: hidden, owned by the tray window (so it
     * dies with it), and its procedure only translates shell-hook codes
     * into AddTab/DeleteTab on the window model. */
    WNDCLASSEXW switchWc = {};
    switchWc.cbSize        = sizeof(switchWc);
    switchWc.lpfnWndProc   = TaskSwitchWndProc;
    switchWc.hInstance     = instance;
    switchWc.lpszClassName = L"W7T_TaskSwitch";

    /* Optional by design: when it cannot be created, the answer to
     * WM_USER + 236 goes back to zero (the previous behaviour) and the tray
     * keeps working. */
    const bool switchClassReady =
        RegisterClassExW(&switchWc) != 0 ||
        GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    if (switchClassReady) {
        m_taskSwitchWnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            L"W7T_TaskSwitch", nullptr,
            WS_POPUP,
            0, 0, 0, 0,
            m_trayWnd, nullptr, instance, nullptr);
    }
    if (m_taskSwitchWnd == nullptr) {
        LogTagged(L"TABPROT",
                  L"taskbar-list responder window not created: clients not served");
    }

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

    /* Prima del SysPager: il figlio ToolbarWindow32 opt-in viene sempre
     * distrutto nello stesso lifecycle e non può restare orfano dopo un
     * riavvio di Explorer o un errore di ricreazione. */
    m_legacyShimPosted.store(false);
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_shellRectsPosted = false;
        m_pendingBarRect = RECT{};
        m_pendingNotifyRect = RECT{};
    }
    LegacyToolbarShim::Instance().Destroy();
    TrayToolbar::Instance().Destroy();

    if (m_trayWnd != nullptr) {
        KillTimer(m_trayWnd, kTimerBackstop);
        KillTimer(m_trayWnd, kTimerDebounce);
        KillTimer(m_trayWnd, kTimerSynthetic);
        KillTimer(m_trayWnd, kTimerBatteryFallback);
    }

    // Unregister power notifications (RAII handles will auto-unregister)
    m_powerNotifyAc.reset();
    m_powerNotifyBattery.reset();

    if (m_taskSwitchWnd != nullptr) {
        DestroyWindow(m_taskSwitchWnd);
        m_taskSwitchWnd = nullptr;
    }
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
    m_threadId.store(GetCurrentThreadId());

    /* v3.10.1 hardening: se il run precedente e' terminato in modo
     * anomalo mentre un clic-batteria era pendente, il file di backup
     * esiste ancora. Lo leggiamo PRIMA di qualsiasi altra cosa per
     * ripristinare subito la chiave legacy al suo valore precedente. */
    RecoverBatteryFlyoutKeyFromBackup();
    /* v3.10.1: filtro eccezioni di ultima istanza: best-effort restore
     * della chiave batteria anche in caso di crash prima del prossimo
     * avvio. */
    InstallBatteryKeyCrashGuard();

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
    /* v3.15: doppia rete - C++ try/catch (c'era gia') + SEH. Il fault
     * hardware qui dentro lascerebbe il mittente di una SendMessage
     * (spesso una finestra della shell, vedi il broadcast
     * TaskbarButtonCreated) appeso sul nostro thread: con la guardia la
     * risposta arriva sempre. */
    LRESULT result = 0;
    bool handled = false;
    W7T_SEH_TRY {
        try {
            result = TrayWndProcInner(hwnd, msg, wParam, lParam);
            handled = true;
        } catch (...) {
        }
    } W7T_SEH_CATCH {
    } W7T_SEH_END
    return handled ? result : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK TrayService::TrayWndProcInner(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    /* PROBE (diagnostic, v2.62-alpha): with W7T_TASKBAND_PROBE=1 in the
     * environment, the tray window logs (tag PROBE) the registered
     * TaskbarButtonCreated id at start-up and every time that exact id is
     * received. Log-only, no reply: the point is to check, on a machine
     * where the real taskbar is running, whether the shell itself sends
     * that message and via which id. */
    if (w7t::TaskbandProbeEnabled()) {
        static std::atomic<int> probeStart{ 0 };
        if (probeStart.fetch_add(1) == 0) {
            LogTagged(L"PROBE",
                      L"taskband probe enabled, registered TaskbarButtonCreated id=0x%x",
                      (unsigned)w7t::GetTaskbarButtonMessageId());
        }
        if (msg == w7t::GetTaskbarButtonMessageId() && msg != 0) {
            LogTagged(L"PROBE",
                      L"TaskbarButtonCreated received (msg=0x%x, w=%lu, l=%ld)",
                      (unsigned)msg, (unsigned long)wParam, (long)lParam);
        }
    }

    /* v1.21.32: the taskbar-list protocol comes before any other work on
     * this thread. The caller is the window build of ANOTHER process
     * (tao/Tauri, Chromium/Electron): the reply has to be immediate and
     * constant, with no lock taken and no model touched. See TrayService.h
     * (kTWMGetTaskSwitch) and TaskSwitchWndProc. */
    if (msg == kTWMGetTaskSwitch) {
        static std::atomic<int> logged{ 0 };
        if (logged.fetch_add(1) == 0) {
            LogTagged(L"TABPROT",
                      L"taskbar-list: WM_USER+236 answered (an application is looking for the bar)");
        }
        return reinterpret_cast<LRESULT>(Instance().m_taskSwitchWnd);
    }

    if (msg == WM_COPYDATA) {
        /* wParam e' l'HWND del mittente originale. Il messaggio viene
         * prima applicato al modello locale e poi inoltrato alla vera
         * Shell_TrayWnd di Explorer dal wrapper, mai ricorsivamente alla
         * finestra dello shim. */
        return Instance().HandleCopyData(
            hwnd, wParam, reinterpret_cast<const COPYDATASTRUCT*>(lParam));
    }

    if (msg == WM_SETTINGCHANGE) {
        /* The user's shell configuration can change at any time (the
         * jump list section cap among the rest): drop the cached reads so
         * the next consumer resolves the current value. The invalidation
         * is cheap; the registry is touched only when a list is actually
         * opened, never on this message path. */
        w7t::InvalidateJumpListCapCache();
        w7t::InvalidatePreviewPolicy();
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    TrayService& self = Instance();

    /* Timer di coalescenza delle riconciliazioni e timer di sicurezza. */
    if (msg == WM_TIMER) {
        if (static_cast<UINT_PTR>(wParam) == kTimerDebounce) {
            self.RunDeferredReconciles();
            return 0;
        }
        if (static_cast<UINT_PTR>(wParam) == kTimerSynthetic) {
            /* v2.61: solo il disegno delle icone nostre. Nessuna lettura di
             * Explorer, nessuna finestra toccata: se lo stato e' cambiato
             * (volume, rete, batteria) la voce si aggiorna, altrimenti
             * questa passata non produce nulla. */
            if (self.m_win11Tray) {
                int added = 0, updated = 0;
                bool pixel = false;
                self.EnsureSyntheticSystemIcons(nullptr, nullptr, &added,
                                                &updated, &pixel);
                if (added != 0 || updated != 0 || pixel) {
                    self.SyncToolbarModel();
                    TrayOverflowWindow::NotifyTrayChanged();
                }
            }
            return 0;
        }
        if (static_cast<UINT_PTR>(wParam) == kTimerBatteryFallback) {
            /* v2.63: un solo colpo (il timer non viene riarmato). */
            self.FinishBatteryOpenWatch();
            return 0;
        }
        if (static_cast<UINT_PTR>(wParam) == kTimerBatteryUiARetry) {
            /* v3.6: riprova il clic sul pulsante batteria vero della tray
             * di Windows 11. Il valore atteso e' che una rilettura UIA
             * (ordine in StartBatteryUiARetry) riporti l'elemento nel
             * modello. Fino a 5 tentativi, poi il ricreato: il clic resta
             * sempre con un effetto. */
            const uint32_t uid = Win11TrayReader::Instance()
                                     .UidOfKind(SystemIconKind::Battery);
            if (uid != 0) {
                self.StopBatteryUiARetry();
                /* v1.5: StopBatteryUiARetry ripristina la chiave legacy
                 * (transitoria): prima di consegnare un ALTRO clic la
                 * chiave va riasserita, altrimenti explorer leggerebbe il
                 * valore vecchio proprio per il clic che conta. */
                EnsureWin32BatteryFlyoutValue();
                if (Win11TrayReader::Instance().RequestClick(uid, false)) {
                    self.StartBatteryOpenWatch(self.m_batteryUiARetryAnchor);
                    /* Il riquadro vero che la shell apre va ancorato sopra
                     * la NOstra icona (altrimenti compare in alto a
                     * sinistra, com'e' successo all'utente col clic
                     * sull'icona vera importata). */
                    self.StartFlyoutWatcher(TrayIconKey{ 0, 0x7F000000u |
                        static_cast<uint32_t>(SystemIconKind::Battery) });
                    LogTagged(L"GATE",
                              L"batteria: clic consegnato al pulsante vero della shell (UIA)");
                } else {
                    LogTagged(L"GATE",
                              L"batteria: pulsante vero non cliccabile, uso il ricreato");
                    BatteryFlyout::Instance().ShowAt(self.m_batteryUiARetryAnchor);
                }
                return 0;
            }
            ++self.m_batteryUiARetryTicks;
            if (self.m_batteryUiARetryTicks >= 5) {
                self.StopBatteryUiARetry();
                LogTagged(L"GATE",
                          L"batteria: pulsante vero mai comparso dopo 5 tentativi, uso il ricreato");
                BatteryFlyout::Instance().ShowAt(self.m_batteryUiARetryAnchor);
                return 0;
            }
            Win11TrayReader::Instance().RequestRead();
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
        if (msg == kMsgLegacyShim) {
            self.SyncLegacyToolbarShim();
            return 0;
        }
        if (msg == kMsgShellRects) {
            RECT bar = {};
            RECT notify = {};
            {
                std::lock_guard<std::recursive_mutex> lock(self.m_mutex);
                bar = self.m_pendingBarRect;
                notify = self.m_pendingNotifyRect;
                self.m_shellRectsPosted = false;
            }
            self.ApplyShellRectsOnThread(bar, notify);
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

    /* v3.10.1 hardening: WM_QUERYENDSESSION/WM_ENDSESSION sono l'unico
     * punto in cui Windows ci avvisa in modo sincrono che logoff/shutdown
     * e' imminente (con un budget di ~5s per processo). Ripristiniamo
     * subito la chiave batteria, non aspettiamo il pump dei messaggi
     * che il GetMessage successivo potrebbe non vedere mai. WM_ENDSESSION
     * con wParam=TRUE conferma che la sessione sta effettivamente
     * finendo (ENDSESSION_CLOSEAPP e' il caso che ci interessa per
     * l'app in esecuzione). */
    if (msg == WM_QUERYENDSESSION
        || (msg == WM_ENDSESSION && wParam != 0)) {
        /* wParam==TRUE a WM_ENDSESSION = la sessione sta realmente
         * finendo (logoff/shutdown). wParam==0 = un'altra app ha
         * annullato: non facciamo nulla, un eventuale Ensure pendente
         * riusera' il file di backup al prossimo clic. */
        OnBatteryKeySessionEnding();
        /* Lasciamo passare il messaggio a DefWindowProc: non neghiamo
         * MAI la chiusura della sessione. */
    }

    if (self.m_taskbarCreatedMsg != 0 && msg == self.m_taskbarCreatedMsg) {
        /* Explorer (ri)avviato: le sue toolbar sono nuove. Si alza la
         * bandierina e si fa tutto tra poco su questo thread: il wndproc
         * non deve bloccarsi qui dentro. */
        self.m_explorerRestarted.store(true);
        /* WORKAROUND: la forma della tray puo' cambiare dopo il riavvio di
         * Explorer. La cache "Classic" del rilevatore Win11 deve quindi
         * essere invalidata prima della prossima riconciliazione. */
        Win11TrayReader::Instance().NoteExplorerRestart();
        self.ScheduleReconcile(kReconcileExplorer, 2500);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* v1.21.32: procedure of the window that answers the taskbar-list protocol.
 * It receives only the shell-hook notification that ITaskbarList clients
 * send to the "task switch window" obtained with WM_USER + 236, and the
 * three documented methods map to the three codes (AddTab ->
 * HSHELL_WINDOWCREATED, DeleteTab -> HSHELL_WINDOWDESTROYED, ActivateTab ->
 * HSHELL_WINDOWACTIVATED; the same translation the ReactOS reimplementation
 * of CTaskbarList performs).
 *
 * No painting, no lock taken up front, no waiting: the sender is another
 * application busy creating a window. The code is masked with 0x7FFF the way
 * the shell does it (the high bit of HSHELL_RUDEAPPACTIVATED is not part of
 * the code). */
LRESULT CALLBACK TrayService::TaskSwitchWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                                LPARAM lParam) {
    /* v3.15: anche la finestra task-switch riceve il broadcast SHELLHOOK
     * (HSHELL_* da Explorer): un fault qui dentro blocca la coda di chi
     * ha inviato. SEH + try/catch anche su questo confine. */
    LRESULT result = 0;
    bool handled = false;
    W7T_SEH_TRY {
    try {
        /* Same registered message the tray window already uses for
         * HSHELL_FLASH; RegisterWindowMessageW is cheap and returns the
         * shared value. */
        static const UINT shellHook = RegisterWindowMessageW(L"SHELLHOOK");
        if (shellHook != 0 && msg == shellHook) {
            const int code = static_cast<int>(wParam & 0x7FFF);
            HWND target = reinterpret_cast<HWND>(lParam);
            if (target != nullptr && IsWindow(target)) {
                bool add = false;
                bool known = true;
                switch (code) {
                    case HSHELL_WINDOWCREATED:      /* AddTab */
                    case HSHELL_WINDOWACTIVATED:    /* ActivateTab */
                        add = true;
                        break;
                    case HSHELL_WINDOWDESTROYED:    /* DeleteTab */
                        add = false;
                        break;
                    default:
                        known = false;
                        break;
                }
                if (known) {
                    const bool changed =
                        WindowManager::Instance().ApplyTaskbarListCall(target, add);
                    if (changed) {
                        LogTagged(L"TABPROT",
                                  add ? L"taskbar-list: AddTab honoured for a foreign window"
                                      : L"taskbar-list: DeleteTab honoured for a foreign window");
                    }
                }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    } catch (...) {
        result = DefWindowProcW(hwnd, msg, wParam, lParam);
        handled = true;
    }
    } W7T_SEH_CATCH {
    } W7T_SEH_END
    return handled ? result : DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool TrayService::ForwardCopyDataToExplorer(
    WPARAM sender, const COPYDATASTRUCT* cds) const {
    if (cds == nullptr || cds->dwData != kCopyDataTrayIcon) {
        return false;
    }

    bool delivered = false;
    W7T_SEH_TRY {
        try {
            const DWORD ourPid = GetCurrentProcessId();
            HWND explorerTray = nullptr;
            HWND explorerFallback = nullptr;
            HWND candidate = nullptr;
            while ((candidate = FindWindowExW(
                        nullptr, candidate, L"Shell_TrayWnd", nullptr))
                   != nullptr) {
                if (candidate == m_trayWnd) {
                    continue;
                }
                DWORD pid = 0;
                GetWindowThreadProcessId(candidate, &pid);
                if (pid == 0 || pid == ourPid) {
                    continue;
                }
                if (!IsExplorerPid(pid)) {
                    continue;
                }
                if (explorerFallback == nullptr) {
                    explorerFallback = candidate;
                }
                /* Una Shell_TrayWnd vera normalmente ha il figlio
                 * TrayNotifyWnd: preferiamo quella per evitare finestre
                 * omonime, ma il processo Explorer resta la prova decisiva.
                 * Su build XAML dove il figlio non esiste, il fallback evita
                 * di perdere il forwarding obbligatorio. */
                if (FindWindowExW(candidate, nullptr,
                                  L"TrayNotifyWnd", nullptr) != nullptr) {
                    explorerTray = candidate;
                    break;
                }
            }
            if (explorerTray == nullptr) {
                explorerTray = explorerFallback;
            }

            if (explorerTray == nullptr) {
                AppendCoreLog(L"copydata: Shell_TrayWnd di Explorer non trovata,"
                              L" nessun inoltro eseguito");
            } else {
                DWORD_PTR response = 0;
                const LRESULT sent = SendMessageTimeoutW(
                    explorerTray, WM_COPYDATA, sender,
                    reinterpret_cast<LPARAM>(cds),
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 1500, &response);
                delivered = sent != 0;
                if (!delivered) {
                    wchar_t line[160];
                    wsprintfW(line,
                              L"copydata: inoltro a Explorer fallito err=%lu",
                              static_cast<unsigned long>(GetLastError()));
                    AppendCoreLog(line);
                }
            }
        } catch (...) {
            AppendCoreLog(L"copydata: eccezione durante l'inoltro a Explorer");
            delivered = false;
        }
    } W7T_SEH_CATCH {
        AppendCoreLog(L"copydata: fault durante l'inoltro a Explorer");
        delivered = false;
    } W7T_SEH_END
    return delivered;
}

LRESULT TrayService::HandleCopyData(HWND hwnd, WPARAM sender,
                                    const COPYDATASTRUCT* cds) {
    (void)hwnd;
    bool localHandled = false;
    bool shouldForward = false;

    /* Il forwarding non e' nel ramo locale: anche un payload corrotto o una
     * eccezione dell'applicazione deve lasciare arrivare a Explorer il
     * WM_COPYDATA originale, dopo il tentativo di elaborazione locale. */
    W7T_SEH_TRY {
        try {
            shouldForward = cds != nullptr && cds->dwData == kCopyDataTrayIcon;
            localHandled = HandleCopyDataLocal(cds) != FALSE;
        } catch (...) {
            AppendCoreLog(L"copydata: eccezione nell'elaborazione locale");
            localHandled = false;
        }
    } W7T_SEH_CATCH {
        AppendCoreLog(L"copydata: fault nell'elaborazione locale");
        localHandled = false;
        /* Se la lettura del campo dwData ha causato un fault, non si può
         * affermare che il pacchetto sia SHELLTRAYDATA: nessun inoltro
         * inventato a Explorer. */
        shouldForward = false;
    } W7T_SEH_END

    bool forwarded = false;
    if (shouldForward) {
        forwarded = ForwardCopyDataToExplorer(sender, cds);
    }
    /* Un risultato non-zero significa che almeno il nostro percorso locale o
     * il destinatario Explorer ha ricevuto il pacchetto. Non si dichiara
     * successo quando entrambi hanno fallito. */
    return (localHandled || forwarded) ? TRUE : FALSE;
}

LRESULT TrayService::HandleCopyDataLocal(const COPYDATASTRUCT* cds) {
    if (cds == nullptr || cds->lpData == nullptr) {
        return FALSE;
    }

    const auto* bytes = static_cast<const uint8_t*>(cds->lpData);

    /* Protocollo non documentato da Microsoft di Shell_NotifyIcon
     * (verificato contro il decompilato di shell32!Shell_NotifyIconA di
     * Windows 98 e reimplementazioni open source, quindi soggetto a
     * variazioni per build): shell32 fa
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
                /* v1.0.0-alpha: WM_COPYDATA viene da qualunque processo
                 * del desktop; un'eccezione C++ risalita da ApplyMessage
                 * (lock, mappa icone, copia HICON tramite GDI+) non deve
                 * MAI terminare il thread della tray di Explorer, che e'
                 * quello che pumpa il wndproc Shell_TrayWnd. */
                try {
                    ApplyMessage(message, nid);
                } catch (const std::exception& e) {
                    (void)e;
                    AppendCoreLog(L"copydata: ApplyMessage ha sollevato un'eccezione, pacchetto ignorato");
                    return FALSE;
                } catch (...) {
                    AppendCoreLog(L"copydata: ApplyMessage ha sollevato un'eccezione sconosciuta, pacchetto ignorato");
                    return FALSE;
                }
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
            try {
                ApplyMessage(static_cast<uint32_t>(cds->dwData), wineNid);
            } catch (const std::exception& e) {
                (void)e;
                AppendCoreLog(L"copydata-wine: ApplyMessage ha sollevato un'eccezione, pacchetto ignorato");
                return FALSE;
            } catch (...) {
                AppendCoreLog(L"copydata-wine: eccezione sconosciuta, pacchetto ignorato");
                return FALSE;
            }
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
     * serve (il canale alfa e' gia' corretto).
     * v3.10.1 hardening: clamp width/height a 4096 (stesso limite di
     * BitmapSane) PRIMA del prodotto w*h*4, cosi' il calcolo di pixelBytes
     * non puo' wrappare neanche su size_t a 32 bit e una lettura fuori
     * dal buffer e' impossibile per costruzione. */
    if ((nid.uFlags & NIF_ICON) && nid.iconWidth > 0 && nid.iconHeight > 0
        && nid.iconWidth <= 4096 && nid.iconHeight <= 4096) {
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
    LogTagged(L"TRAY",
              L"event=%u source=WM_COPYDATA identity=%s hwnd=%016I64X uid=%u",
              static_cast<unsigned>(message),
              hasGuid ? nidGuidString.c_str() : L"hWnd+uID",
              static_cast<unsigned long long>(nid.hWnd),
              static_cast<unsigned>(nid.uID));
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
    if (hasGuid && message == NIM_SETVERSION &&
        m_icons.find(key) == m_icons.end()) {
        /* NIM_SETVERSION non cambia l'identità: quando un'app ha
         * ri-registrato HWND/UID ma conserva il GUID, aggiorniamo la voce
         * già presente invece di perdere la versione callback. */
        auto gi = m_byGuid.find(nidGuidString);
        if (gi != m_byGuid.end()) {
            key = gi->second;
        }
    }

    switch (message) {
        case NIM_ADD:
        case NIM_MODIFY: {
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
                ApplySavedBehavior(key.ownerHwnd, key.uid,
                                   !AutoTrayEnabledCached(), entry);
                entry.hiddenDesired = !entry.isPinned;

                entry.ownerPath = OwnerPathOf(key.ownerHwnd);
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
            entry.fromCopyData = true;
            entry.lastUpdateSource = L"WM_COPYDATA";

            if (nid.uFlags & NIF_MESSAGE) {
                entry.callbackMessage = nid.uCallbackMessage;
            }
            if (nid.uFlags & NIF_TIP) {
                entry.tooltip = nid.szTip;
                /* Tooltip e percorso completano il metadato, ma non
                 * sostituiscono la coppia hWnd+uID o il GUID. */
                if (entry.ownerPath.empty()) {
                    entry.ownerPath = OwnerPathOf(key.ownerHwnd);
                }
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
                    raii::IconHandle owned(CopyIcon(source));
                    if (owned) {
                        ArgbBitmap bmp;
                        if (IconToArgb(owned.get(), bmp) && BitmapSane(bmp)) {
                            const uint64_t hash = ArgbHash(bmp);
                            if (hash != entry.pixelHash) {
                                entry.bitmap = std::move(bmp);
                                entry.pixelHash = hash;
                                entry.iconRevision++;
                            }
                        }
                    }
                }
                /* hIcon == NULL senza bitmap Wine: l'applicazione ha mandato
                 * un MODIFY "senza icona". La shell conserva l'ultima icona;
                 * anche noi. CANCELLARLA (comportamento nostro fino alla
                 * 1.9.17) era il difetto che faceva SPARIRE le icone al
                 * cambio AC/DC: molti provider mandano NIF_ICON con handle
                 * nullo proprio in quei toggle. */
            }

            /* The balloon arrives with an ADD/MODIFY carrying NIF_INFO.
             * v1.7.6: an icon whose saved behavior is "Hide icon and
             * notifications" does not even surface the balloon (it is the
             * third state: "and notifications"
             * is gone). The entry STAYS in the model - the application
             * keeps sending its updates - only the output stays silent.
             * "Only show notifications" is the mirror case: icon hidden,
             * balloon shown - present, so it passes. */
            if ((nid.uFlags & NIF_INFO) && !nid.szInfo.empty()) {
                bool barVisibleBalloon = false, presentBalloon = false;
                ResolveVisibilityLocked(entry, barVisibleBalloon, presentBalloon);
                if (!presentBalloon) {
                    LogTagged(L"TRAY",
                              L"balloon suppressed by saved behavior (uid %u)",
                              key.uid);
                } else {
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
            }

            CoreState::Instance().QueueEvent(
                isNew ? W7T_EVT_TRAY_ADD : W7T_EVT_TRAY_MODIFY, key.ownerHwnd, key.uid);
            break;
        }

        case NIM_DELETE: {
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

bool TrayService::CurrentIconRect(const TrayIconKey& key, RECT& out) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_iconRects.find(key);
    if (it == m_iconRects.end()) {
        return false;
    }

    const RECT r = it->second;
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) {
        return false;
    }
    /* Un rettangolo che non appartiene a nessun monitor non e' una
     * posizione: e' memoria di un layout che non esiste piu' (monitor
     * staccato, barra spostata). Meglio il ripiego del chiamante. */
    if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL) == nullptr) {
        return false;
    }
    out = r;
    return true;
}

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

std::vector<OverflowSnapshot> TrayService::GetUnpinnedSnapshot() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<OverflowSnapshot> out;
    std::vector<TrayIconKey> dead;
    for (const auto& key : m_order) {
        auto it = m_icons.find(key);
        if (it == m_icons.end()) {
            continue;
        }
        /* v1.7.6: the overflow panel shows "only notifications" (the
         * right home for anyone who wants no icon in the bar) but NEVER
         * a fully hidden icon nor one whose system switch is off: for
         * those the page said "nothing, anywhere". */
        bool barVisibleSnap = false, presentSnap = false;
        ResolveVisibilityLocked(it->second, barVisibleSnap, presentSnap);
        if (!presentSnap || barVisibleSnap) {
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
    if (m_trayWnd == nullptr) {
        return;
    }

    if (!m_win11Tray) {
        if (!Win11TrayReader::Detect()) {
            /* Isola non ancora pronta (avvio, Explorer che si ricrea): si
             * riprova alla prossima passata, senza latitare niente. */
            return;
        }
        m_win11Tray = true;
        Win11TrayReader::Instance().SetNotify(m_trayWnd, kMsgUiaTray);
    }

    /* v2.61 - IL LETTORE SI AVVIA SEMPRE, E SE NON E' PARTITO SI RIPROVA.
     *
     * Prima bastava una chiamata andata male una volta (shell occupata
     * all'avvio, thread non ancora pubblicato) per non riprovare mai piu':
     * m_win11Tray restava true e questa funzione usciva subito. Il
     * risultato, sulla macchina dell'utente, era una tray senza letture per
     * minuti interi: le icone comparivano solo se e quando qualcos'altro
     * faceva ripartire il lettore. */
    if (!Win11TrayReader::Instance().IsRunning()) {
        if (Win11TrayReader::Instance().Start()) {
            AppendCoreLog(L"tray: Windows 11, lettura UI Automation attiva");
        } else {
            AppendCoreLog(L"tray: Windows 11, lettura non partita, si riprova");
            ScheduleReconcile(kReconcileUiaTray, m_uiaRetryDelayMs);
            m_uiaRetryDelayMs = (std::min)(15000ul, m_uiaRetryDelayMs * 2);
            return;
        }
    }

    /* v2.61: le tre icone di sistema che la shell non espone entrano nel
     * modello ADESSO, non al primo giro di lettura riuscito. Da questo
     * momento la tray ha sempre volume, rete e batteria, qualunque cosa
     * faccia Explorer. */
    int added = 0, updated = 0;
    bool pixel = false;
    EnsureSyntheticSystemIcons(nullptr, nullptr, &added, &updated, &pixel);
    if (added != 0 || updated != 0 || pixel) {
        SyncToolbarModel();
        TrayOverflowWindow::NotifyTrayChanged();
    }

    /* Risveglio leggero dello stato (il volume non manda eventi alla tray):
     * non tocca Explorer, non apre nulla, non muove finestre. */
    SetTimer(m_trayWnd, kTimerSynthetic, 10000, nullptr);

    /* Le isole XAML non sono finestre della tray: quando il flyout delle
     * icone nascoste si apre, o quando una qualunque finestra della shell
     * con quella classe compare/scompare, si rilegge. Filtro per classe:
     * l'hook e' globale ma costa una GetClassNameW per evento. */
    if (m_trayHostHook == nullptr) {
        m_trayHostHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
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

/* ------------------------------------------------------------------ */
/*  v2.61 - LE TRE ICONE DI SISTEMA CHE LA SHELL DI WINDOWS 11 NON     */
/*  ESPONE (volume, rete, batteria)                                    */
/*                                                                     */
/*  Perche' e' una funzione a se': queste voci NON sono il risultato    */
/*  di una lettura di Explorer. Devono esistere appena la modalita'     */
/*  Windows 11 e' attiva, prima di qualunque lettura, e restare nel     */
/*  modello anche quando Explorer non risponde: erano proprio i due     */
/*  casi in cui l'utente non le vedeva mai (o le vedeva dopo dieci      */
/*  minuti, quando una lettura andava finalmente a buon fine).          */
/*                                                                     */
/*  Chi la chiama: EnableWin11Tray (subito), ApplyWin11TraySnapshot     */
/*  (anche a lettura vuota), il timer leggero kTimerSynthetic (stato),  */
/*  gli eventi di alimentazione e di rete.                              */
/* ------------------------------------------------------------------ */
void TrayService::EnsureSyntheticSystemIcons(
        const std::set<SystemIconKind>* shellExposed,
        std::set<uint32_t>* presentUids,
        int* added, int* updated, bool* bitmapChanged) {
    /* uid riservati alle icone sintetiche: in cima allo spazio dei 32 bit,
     * lontano dagli hash FNV-1a delle voci UI Automation (0x77000000|hash)
     * e da qualunque ownerHwnd reale. */
    constexpr uint32_t kSyntheticSystemUidBase = 0x7F000000u;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

            /* v3.6: se una batteria VERA (importata dalla shell) e' nel
             * modello, quella sintetica non va creata: l'utente la vedeva
             * DOPPIA (la nostra accanto a quella vera, che tra l'altro
             * apriva il riquadro Win32 di Windows 7). La vera ha sempre la
             * precedenza: e' lei che parla con la shell. */
            auto realKindExists = [&](SystemIconKind kind) {
                for (const auto& pair : m_icons) {
                    const TrayIconEntry& e = pair.second;
                    if (e.fromExplorer && !e.fromWin11Uia
                        && e.systemKind == kind) {
                        return true;
                    }
                }
                return false;
            };
            bool realBatteryLogged = false;

            for (SystemIconKind kind : kSyntheticKinds) {
                if (kind == SystemIconKind::Battery && realKindExists(kind)) {
                    if (!realBatteryLogged) {
                        realBatteryLogged = true;
                        LogTagged(L"GATE",
                                  L"batteria: icona vera presente nel modello, la sintetica non si crea");
                    }
                    continue;
                }
                if (shellExposed != nullptr && shellExposed->count(kind) != 0) {
                    /* La shell espone questo tipo.
                     *
                     * v2.62: non succede piu' - le icone di sistema che la
                     * shell espone non entrano nel modello (vedi il filtro in
                     * ApplyWin11TraySnapshot), quindi qui "esposta" resta
                     * falso e le tre icone ricreate sono sempre le nostre.
                     * Il ramo resta come rete di sicurezza per il percorso
                     * classico (Windows 10 e precedenti), dove la shell le
                     * disegna davvero ed e' giusto che vinca la sua. */
                    continue;
                }

                const uint32_t uid = kSyntheticSystemUidBase |
                                     static_cast<uint32_t>(kind);
                const TrayIconKey key{ 0, uid };
                /* Inserita tra le "presenti": la passata di rimozione del
                 * chiamante non deve portarla via. */
                if (presentUids != nullptr) {
                    presentUids->insert(uid);
                }

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
                    entry.lastUpdateSource = L"fallback icon";
                    entry.guidKey       = syntheticGuidKey(kind);
                    entry.toolbarId     = EnsureToolbarId(key);
                    m_icons[key] = std::move(entry);
                    m_order.push_back(key);
                    if (added != nullptr) {
                        ++added;
                    }
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
                    if (updated != nullptr) {
                        ++updated;
                    }
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
                if (bitmapChanged != nullptr) {
                    *bitmapChanged = true;
                }
                if (updated != nullptr) {
                    ++updated;
                }
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, 0, uid);
            }
}


void TrayService::ApplyWin11TraySnapshot() {
    if (!m_win11Tray) {
        return;
    }
    const bool readValid = Win11TrayReader::Instance().IsLastReadValid();
    const std::vector<Win11TrayItem> raw = readValid
        ? Win11TrayReader::Instance().TakeSnapshot()
        : std::vector<Win11TrayItem>();
    const bool mainRead = readValid &&
        Win11TrayReader::Instance().IsLastReadMainValid();
    const bool overflowRead = readValid &&
        Win11TrayReader::Instance().IsLastReadOverflowValid();
    /* v2.62 - LE ICONE DI SISTEMA CHE RICREIAMO NON SI IMPORTANO.
     *
     * Volume, rete e batteria sono le tre icone che questa barra ridisegna
     * (vedi EnsureSyntheticSystemIcons): se la shell ne espone una - su
     * Windows 11 la batteria compare nell'angolo della tray, volume e rete
     * no - importarla significava mostrarla DUE volte: la nostra e quella
     * di Windows. L'utente le vedeva affiancate.
     *
     * Qui la voce della shell viene ignorata all'origine: resta la nostra,
     * una sola, che parla la lingua di Windows 7 (icona e riquadro), mentre
     * una voce importata aprirebbe il riquadro della shell.
     *
     * Il resto della tray (le icone delle applicazioni, comprese quelle nel
     * pannello nascosto) si importa come sempre. */
    std::vector<Win11TrayItem> items;
    items.reserve(raw.size());
    size_t skippedShellKinds = 0;
    for (const Win11TrayItem& item : raw) {
        if (item.kind != SystemIconKind::None) {
            ++skippedShellKinds;
            continue;
        }
        items.push_back(item);
    }
    if (skippedShellKinds != 0) {
        wchar_t line[160] = {};
        swprintf(line, 160,
                 L"tray Win11: %zu icone di sistema ignorate (le ricreiamo noi)",
                 skippedShellKinds);
        AppendCoreLog(line);
    }

    if (items.empty()) {
        /* Nessuna icona dalla shell in questa lettura: il modello non si
         * tocca (una lettura incompleta non e' una sparizione), ma le NOSTRE
         * tre ci sono lo stesso: sono l'unica cosa che possiamo disegnare
         * senza chiedere niente a Explorer. */
        int added = 0, updated = 0;
        bool pixel = false;
        EnsureSyntheticSystemIcons(nullptr, nullptr, &added, &updated, &pixel);
        if (added != 0 || updated != 0 || pixel) {
            SyncToolbarModel();
            TrayOverflowWindow::NotifyTrayChanged();
        }

        /* Nessuna icona della shell. Due casi diversi, e vanno trattati
         * diversamente:
         *
         *  - lettura NON valida (isola assente, Explorer sotto stress,
         *    lettore non partito): si ritenta in backoff 1 s -> 2 s -> 4 s ->
         *    8 s -> 15 s, e appena una lettura riesce si torna a 1 s. Il
         *    risveglio di sicurezza a 30 s resta comunque attivo.
         *  - lettura valida ma vuota: non c'e' nulla da leggere (l'utente ha
         *    nascosto tutto), quindi non si ritenta: si aspetta un evento.
         *
         * Il log dice quale dei due casi si e' verificato: senza questa riga
         * un utente che segnala "le icone non si vedono" non lascia nessuna
         * traccia di cosa e' successo nel core. */
        if (!Win11TrayReader::Instance().IsLastReadValid()) {
            /* v2.62 - I PRIMI TENTATIVI SONO RAPIDI.
             *
             * All'avvio l'isola della tray puo' non essere pronta per qualche
             * centinaio di millisecondi: con il solo backoff lento si
             * aspettava un secondo, poi due, poi quattro..., e l'utente
             * vedeva le icone arrivare con calma. I primi quattro tentativi
             * sono a 400/800/1600/3200 ms; da li' in poi vale il backoff
             * (1 s -> 15 s), che non e' un sondaggio continuo. */
            unsigned long delayMs = m_uiaRetryDelayMs;
            if (m_uiaFastRetries < 4) {
                delayMs = 400ul << m_uiaFastRetries;
                ++m_uiaFastRetries;
            }

            wchar_t line[160] = {};
            swprintf(line, 160,
                     L"tray Win11: lettura non valida (lettore %s), riprovo fra %lu ms",
                     Win11TrayReader::Instance().IsRunning() ? L"attivo" : L"fermo",
                     delayMs);
            AppendCoreLog(line);

            /* Un lettore fermo non si rianima da solo: si riavvia qui. */
            if (!Win11TrayReader::Instance().IsRunning()) {
                if (Win11TrayReader::Instance().Start()) {
                    AppendCoreLog(L"tray Win11: lettore riavviato");
                }
            }

            ScheduleReconcile(kReconcileUiaTray, delayMs);
            m_uiaRetryDelayMs = (std::min)(15000ul, m_uiaRetryDelayMs * 2);
        } else {
            m_uiaRetryDelayMs = 1000;
            m_uiaFastRetries = 0;
        }
        return;
    }
    /* Lettura valida: il backoff riparte da un secondo e i tentativi
     * rapidi dell'avvio sono finiti. */
    m_uiaRetryDelayMs = 1000;
    m_uiaFastRetries = 0;

    /* UIA non espone normalmente HWND/GUID dell'app. Se il callback
     * TrayNotify ha già fornito la stessa voce, il tooltip è solo un
     * collegamento conservativo: l'identita' resta quella COM GUID/HWND+UID
     * e l'uid UIA diventa un alias di clic, non una seconda icona. */
    std::set<uint32_t> present;
    std::set<TrayIconKey> presentKeys;
    std::set<SystemIconKind> presentKinds;
    auto modelKeyForUia = [this](const Win11TrayItem& item) {
        const TrayIconKey direct{ 0, item.uid };
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto directIt = m_icons.find(direct);
        if (directIt != m_icons.end()) {
            directIt->second.uiaUid = item.uid;
            return direct;
        }
        for (auto& pair : m_icons) {
            TrayIconEntry& entry = pair.second;
            if (entry.uiaUid == item.uid ||
                (entry.fromTrayNotify && entry.uiaUid == 0 &&
                 !entry.tooltip.empty() && entry.tooltip == item.name)) {
                entry.uiaUid = item.uid;
                return pair.first;
            }
        }
        return direct;
    };
    for (const Win11TrayItem& item : items) {
        present.insert(item.uid);
        presentKeys.insert(modelKeyForUia(item));
        if (item.kind != SystemIconKind::None) {
            presentKinds.insert(item.kind);
        }
    }

    int added = 0;
    int updated = 0;
    int removed = 0;
    bool anyBitmapChange = false;

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);

        for (const Win11TrayItem& item : items) {
            const TrayIconKey key = modelKeyForUia(item);
            auto it = m_icons.find(key);
            if (it == m_icons.end()) {
                TrayIconEntry entry;
                entry.key          = key;
                entry.fromWin11Uia = true;
                entry.fromExplorer = false;
                entry.uiaUid       = item.uid;
                entry.lastUpdateSource = L"UIA tray";
                entry.tooltip      = item.name;
                entry.ownerPath    = item.exePath;
                entry.bitmap       = item.bitmap;
                entry.pixelHash    = ArgbHash(entry.bitmap);
                entry.iconRevision = entry.bitmap.empty() ? 0 : 1;
                /* v1.7.6: entries born from the UIA reading honor the
                 * saved choice too (they used to be created straight with
                 * the shell layout, which the next pass could overwrite
                 * only while no preference existed). */
                ApplySavedBehavior(key.ownerHwnd, key.uid, !item.hidden, entry);
                entry.hiddenDesired = item.hidden;
                /* v2.62 - NASCOSTO DALLA SHELL NON VUOL DIRE NASCOSTO PER NOI.
                 *
                 * NIS_HIDDEN significa "l'applicazione ha chiesto di non
                 * mostrare questa icona": e' quello che il modello usa per
                 * togliere l'icona da ENTRAMBE le viste. Le icone che
                 * Windows 11 tiene nel suo pannello delle icone nascoste non
                 * sono nascoste dall'applicazione: sono esattamente le icone
                 * che la NOSTRA freccetta deve mostrare. Marcandole
                 * NIS_HIDDEN sparivano dal modello (ne' barra ne' pannello) e
                 * l'overflow restava vuoto: la freccetta si nascondeva da
                 * sola e il pannello - quando si apriva - non conteneva
                 * niente.
                 *
                 * Quindi: stato 0 e isPinned falso. L'icona vive nella barra
                 * solo se la shell la mostra, altrimenti nel nostro pannello.
                 */
                entry.state        = 0;
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
                CoreState::Instance().QueueEvent(W7T_EVT_TRAY_ADD,
                                                 key.ownerHwnd, key.uid);
            } else {
                TrayIconEntry& entry = it->second;
                entry.missCount = 0;
                entry.lastReadFailed = false;

                entry.uiaUid = item.uid;
                entry.lastUpdateSource = L"UIA tray";
                bool changed = false;
                if (entry.tooltip.empty() && !item.name.empty()) {
                    entry.tooltip = item.name;
                    changed = true;
                }
                /* v2.62 - LA POSIZIONE LA DECIDE L'UTENTE, NON LA SHELL.
                 *
                 * Prima la disposizione di Windows 11 (dentro o fuori dal
                 * pannello delle icone nascoste) veniva riscritta nel modello
                 * a ogni lettura: spostare un'icona con pin/unpin non aveva
                 * effetto, perche' la lettura successiva la rimetteva dov'era.
                 * Ora la disposizione della shell vale solo finche' l'utente
                 * non ha espresso la sua (preferenza salvata). */
                if (!HasSavedPreference(key)) {
                    if (entry.isPinned != !item.hidden) {
                        entry.isPinned = !item.hidden;
                        entry.hiddenDesired = item.hidden;
                        changed = true;
                    }
                }
                /* v2.62: lo stato "nascosto" non si eredita dalla shell (vedi
                 * sopra): se una voce creata da una build precedente se lo
                 * portava dietro, si azzera qui alla prima lettura utile. */
                if (!entry.fromTrayNotify && entry.state != 0) {
                    entry.state = 0;
                    changed = true;
                }
                /* v2.62 - ANCHE IL DISEGNO PUO' CAMBIARE.
                 *
                 * Un'applicazione cambia icona quando cambia stato (una
                 * sincronizzazione in corso, un profilo diverso, un
                 * aggiornamento): prima il bitmap veniva preso solo se il
                 * modello non ne aveva ancora nessuno, quindi l'icona
                 * restava quella del primo avvio. Ora si aggiorna quando il
                 * disegno e' davvero diverso (il confronto e' un hash, non
                 * un'uguaglianza pixel per pixel). */
                if (!item.bitmap.empty() &&
                    (!entry.fromTrayNotify || entry.bitmap.empty())) {
                    const uint64_t hash = ArgbHash(item.bitmap);
                    if (hash != entry.pixelHash) {
                        entry.bitmap = item.bitmap;
                        entry.pixelHash = hash;
                        ++entry.iconRevision;
                        anyBitmapChange = true;
                        changed = true;
                    }
                }
                if (changed) {
                    ++updated;
                    CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY,
                                                     key.ownerHwnd, key.uid);
                }
            }
        }

        /* ------------------------------------------------------------------ */
        /* v2.61: le tre icone che la shell non espone: la funzione qui
         * sopra non dipende da questa lettura. */
        EnsureSyntheticSystemIcons(&presentKinds, &present, &added, &updated,
                                   &anyBitmapChange);


        /* Rimozione delle voci della tray di Windows 11 sparite. Due
         * assenze consecutive, come per le icone di Explorer: una lettura
         * transitoria non fa sparire nulla. */
        std::vector<TrayIconKey> toRemove;
        for (auto& pair : m_icons) {
            TrayIconEntry& entry = pair.second;
            /* Una voce gia' confermata da TrayNotify ha un canale live
             * WM_COPYDATA e non può essere cancellata perché UIA ha omesso
             * un elemento: la rimozione certa è NIM_DELETE o owner morto. */
            if (entry.fromTrayNotify ||
                (!entry.fromWin11Uia && entry.uiaUid == 0)) {
                continue;
            }
            if ((entry.uiaUid != 0 && presentKeys.count(pair.first) != 0) ||
                (entry.uiaUid == 0 && present.count(pair.first.uid) != 0)) {
                continue;
            }
            /* L'isola dell'overflow e' spesso creata solo quando il flyout
             * viene aperto. Una lettura valida della barra principale non e'
             * una prova che una voce gia' vista nel cassetto sia stata
             * rimossa: la conserviamo finche' UIA non attraversa davvero
             * l'overflow. */
            if ((!mainRead && !entry.hiddenDesired) ||
                (!overflowRead && entry.hiddenDesired)) {
                continue;
            }
            /* L'isola dell'overflow e' spesso creata solo quando il flyout
             * viene aperto. Una lettura valida della barra principale non e'
             * una prova che una voce gia' vista nel cassetto sia stata
             * rimossa: la conserviamo finche' UIA non attraversa davvero
             * l'overflow. */
            if ((!mainRead && !entry.hiddenDesired) ||
                (!overflowRead && entry.hiddenDesired)) {
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
        _wcsicmp(cls, L"Windows.UI.Input.InputSite.WindowClass") == 0 ||
        _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
        _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
    if (!isTrayHost) {
        return;
    }

    /* Il debounce e' gia' quello delle riconciliazioni: piu' eventi vicini
     * diventano una sola lettura. */
    /* v3.15: racchiuso in SEH + try anche questo callback WinEvent (stesso
     * motivo di OwnerDestroyedProc: mai bloccare la catena degli eventi
     * accessibilita' mentre la shell ridisegna le sue finestre). */
    W7T_SEH_TRY {
    try {
        self.ScheduleReconcile(kReconcileUiaTray, 250);
    } catch (...) {
    }
    } W7T_SEH_CATCH {
    } W7T_SEH_END
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
        /* v1.7.6: the managed layer is a VIEW of the model: it reads the
         * RESOLVED visibility too (saved choice, Always show all, the
         * system switches). For fully hidden entries isHidden means "do
         * not paint it anywhere", so the managed bar and its overflow
         * collapse into the same answer without inventing own rules. */
        bool barVisibleCopy = false, presentCopy = false;
        ResolveVisibilityLocked(entry, barVisibleCopy, presentCopy);
        info.isPinned        = (barVisibleCopy && presentCopy) ? 1 : 0;
        info.isHidden        = (!presentCopy
                                || (entry.state & NIS_HIDDEN) != 0) ? 1 : 0;
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

/* v2.63 - batteria: verifica differita dell'apertura del riquadro Win32. */
/* v3.5 - Un'icona VERA di stobject.dll nel modello: la batteria reale
 * importata dalla toolbar di Explorer (non le nostre ricreate, non le voci
 * UIA di Windows 11). I candidati si raccolgono col lucchetto, ma la
 * verifica del modulo (OpenProcess + psapi) resta FUORI dal lucchetto. */
bool TrayService::FindRealStobjectIcon(uint64_t* owner, uint32_t* uid,
                                       uint32_t* callback, uint32_t* version) {
    std::vector<std::pair<TrayIconKey, std::pair<uint32_t, uint32_t>>> candidates;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        candidates.reserve(16);
        for (const auto& pair : m_icons) {
            const TrayIconEntry& entry = pair.second;
            if (!entry.fromExplorer || entry.fromWin11Uia
                || !entry.ownerIsExplorer || pair.first.ownerHwnd == 0) {
                continue;
            }
            candidates.emplace_back(
                pair.first,
                std::make_pair(entry.callbackMessage, entry.version));
            if (candidates.size() >= 32) {
                break;
            }
        }
    }
    for (const auto& candidate : candidates) {
        HWND hwnd = reinterpret_cast<HWND>(
            static_cast<uintptr_t>(candidate.first.ownerHwnd));
        if (hwnd == nullptr || !IsWindow(hwnd)) {
            continue;
        }
        if (!OwnerModuleIs(hwnd, L"stobject.dll")) {
            continue;
        }
        if (owner != nullptr) {
            *owner = candidate.first.ownerHwnd;
        }
        if (uid != nullptr) {
            *uid = candidate.first.uid;
        }
        if (callback != nullptr) {
            *callback = candidate.second.first;
        }
        if (version != nullptr) {
            *version = candidate.second.second;
        }
        return true;
    }
    return false;
}

void TrayService::StartBatteryOpenWatch(const RECT& anchor) {
    if (m_trayWnd == nullptr) {
        return;
    }
    m_pendingBatteryAnchor = anchor;
    m_pendingBatteryPopups = CountVisibleForeignPopups();
    /* v3.5: prima/dopo per INSIEME di finestre, non solo per numero di
     * popup: il riquadro Win32 vero a volte non ha lo stile popup. */
    CollectForeignVisibleWindows(m_pendingBatteryWindows);
    /* 1200 ms: su macchine lente la shell puo' metterci piu' di 900 a
     * mostrare il riquadro Win32; il ricreato in caso di fallimento parte
     * solo dopo, quindi una finestra piu' larga non rallenta il caso
     * felice. */
    SetTimer(m_trayWnd, kTimerBatteryFallback, 1200, nullptr);
}

void TrayService::FinishBatteryOpenWatch() {
    if (m_trayWnd != nullptr) {
        KillTimer(m_trayWnd, kTimerBatteryFallback);
    }
    RestoreWin32BatteryFlyoutValue();
    std::set<uint64_t> now;
    CollectForeignVisibleWindows(now);
    bool shellOpenedSomething = false;
    for (uint64_t hwndValue : now) {
        if (m_pendingBatteryWindows.count(hwndValue) == 0) {
            shellOpenedSomething = true;
            break;
        }
    }
    const int popupNow = CountVisibleForeignPopups();
    if (shellOpenedSomething || popupNow > m_pendingBatteryPopups) {
        LogTagged(L"GATE", L"batteria: riquadro di Windows aperto dalla shell");
        return;
    }
    LogTagged(L"GATE", L"batteria: la shell non ha aperto il riquadro, uso il ricreato");
    BatteryFlyout::Instance().ShowAt(m_pendingBatteryAnchor);
}

/* v3.6 - Tentativi UIA del clic sul pulsante batteria vero di Windows 11.
 *
 * Questo e' il reindirizzamento all'ExplorerPatcher: con la chiave
 * UseWin32BatteryFlyout=1, il clic sul pulsante VERO della shell apre il
 * riquadro Win32 di Windows 7 (lo conferma la macchina dell'utente). Ma
 * lo snapshot della lettura UIA oscilla (0 <-> 3 icone) e al momento del
 * clic spesso e' vuoto: prima del tentativo 1.2.0-alpha si rinunciava
 * subito e partiva il ricreato. Ora si ordina una rilettura e si riprova
 * col timer: 5 tentativi ogni 250 ms, poi (solo allora) il ricreato. */
void TrayService::StartBatteryUiARetry(const RECT& anchor) {
    if (m_trayWnd == nullptr) {
        return;
    }
    m_batteryUiARetryAnchor = anchor;
    m_batteryUiARetryTicks = 0;
    Win11TrayReader::Instance().RequestRead();
    SetTimer(m_trayWnd, kTimerBatteryUiARetry, 250, nullptr);
}

void TrayService::StopBatteryUiARetry() {
    if (m_trayWnd != nullptr) {
        KillTimer(m_trayWnd, kTimerBatteryUiARetry);
    }
    m_batteryUiARetryTicks = 0;
    RestoreWin32BatteryFlyoutValue();
}

void TrayService::SetWin7NetworkFlyout(bool ready) {
    /* v2.62: il frontend avvisa che il riquadro di rete di Windows 7 e'
     * pronto (modulo inizializzato e modo "Windows 7 (ricreato)" scelto).
     * Senza questo avviso il core non puo' sapere se invocare quel modulo e'
     * sicuro: chiamarlo prima dell'inizializzazione significherebbe usare un
     * contesto vuoto. */
    m_win7NetworkFlyoutReady = ready;
}

void TrayService::SetWin8NetworkFlyout(bool ready) {
    /* v3.8: il frontend avvisa che la variante Windows 8 del riquadro di
     * rete e' pronta (modulo della logica inizializzato + tendina su
     * "Windows 8 (ricreato)"). Vale lo stesso patto della versione Win7:
     * senza l'avviso il core non chiama quel modulo. */
    m_win8NetworkFlyoutReady = ready;
}

/* v1.7.6: the single gate where a saved icon choice takes effect.
 * barVisible/presentSomewhere are BORN here and READ everywhere (the
 * toolbar model, the overflow panel, the managed view, the balloons):
 * no site decides visibility on its own. */
void TrayService::ResolveVisibilityLocked(const TrayIconEntry& entry,
                                          bool& barVisible,
                                          bool& presentSomewhere) const {
    TrayPrefsStore& store = TrayPrefsStore::Instance();

    /* default: the model's own state (import, hysteresis, NIS_* state) */
    barVisible        = entry.isPinned;
    presentSomewhere  = true;

    /* A system icon switched OFF at its own switch disappears from every
     * view, "Always show all icons" included: on the original page the
     * checkbox never resurrects a manually disabled system icon. */
    if (entry.systemKind != SystemIconKind::None
        && !store.SystemIconOn(static_cast<int32_t>(entry.systemKind))) {
        barVisible = false;
        presentSomewhere = false;
        return;
    }

    if (entry.userBehavior == kBehaviorHide) {
        /* "Hide icon and notifications": no bar, no overflow, no
         * balloons. The "Always show all" checkbox lights it back up
         * (that box literally says "show ALL icons"): the saved choice
         * survives and returns when the checkbox is switched off again. */
        if (!store.AlwaysShow()) {
            barVisible = false;
            presentSomewhere = false;
            return;
        }
        barVisible = true;
        return;
    }
    if (store.AlwaysShow()) {
        /* "Always show all icons and notifications on the taskbar":
         * everything on the bar; the choices underneath stay saved, they
         * are never rewritten by the checkbox. */
        barVisible = true;
        return;
    }
    if (entry.userBehavior == kBehaviorShow) {
        barVisible = true;
        return;
    }
    if (entry.userBehavior == kBehaviorNotifyOnly) {
        barVisible = false;   /* lives in the overflow only, balloons on */
        return;
    }
    /* No explicit choice: the WINDOWS rule the managed layer used to
     * apply on its own (EnableAutoTray=0 -> show everything). v1.7.6:
     * the rule lives in the core, so a view can never crush a page
     * choice by re-applying it. */
    if (!AutoTrayEnabledCached()) {
        barVisible = true;
        return;
    }
}

int32_t TrayService::SetPinned(uint64_t ownerHwnd, uint32_t uid, int32_t pinned) {
    /* v1.7.6: dragging no longer writes a bool of its own: it becomes the
     * page's equivalent choice (onto the bar = show; into the overflow =
     * only notifications). A "hidden" icon cannot be dragged by definition
     * (it is visible nowhere), so this can never overwrite a state 2. */
    return SetBehavior(ownerHwnd, uid, pinned != 0 ? kBehaviorShow
                                                    : kBehaviorNotifyOnly);
}

int32_t TrayService::SetBehavior(uint64_t ownerHwnd, uint32_t uid,
                                 int32_t behavior) {
    if (behavior < kBehaviorShow || behavior > kBehaviorHide) {
        return W7T_ERR_INVALID_ARG;
    }
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = m_icons.find(TrayIconKey{ ownerHwnd, uid });
        if (it == m_icons.end()) {
            return W7T_ERR_NOT_FOUND;
        }
        TrayIconEntry& entry = it->second;
        if (entry.userBehavior == behavior) {
            /* The same state again: nothing to save, nothing to repaint
             * (a no-op choice must not storm the event queue). */
            return W7T_OK;
        }
        /* Remember the choice: the application will never send NIM_ADD
         * again, so without this memory the icon would fall back to its
         * default at the next start. This is our PromotedIconStreams
         * equivalent, in OUR file (never in the registry). */
        PersistBehavior(ownerHwnd, uid, behavior, entry);
        entry.hiddenPending = 0;

        CoreState::Instance().QueueEvent(W7T_EVT_TRAY_MODIFY, ownerHwnd, uid);
    }
    /* The real button follows the model: TBSTATE_HIDDEN in our
     * ToolbarWindow32, exactly like Explorer's toolbar does. */
    SyncToolbarModel();
    TrayOverflowWindow::NotifyTrayChanged();   /* v3.1: refresh the panel */
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
bool IsFlyoutProcess(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return false;
    }
    raii::GenericHandle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                          FALSE, pid));
    if (!proc) {
        return false;
    }
    wchar_t path[MAX_PATH] = {};
    DWORD len = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(proc.get(), 0, path, &len) != FALSE;
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
    HWND trayWnd = nullptr;
    DWORD ownerThread = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        trayWnd = m_trayWnd;
        ownerThread = m_threadId.load();
    }
    if (trayWnd == nullptr) {
        return;
    }

    /* ReportShellRects arriva normalmente dal dispatcher WPF. Le finestre
     * Shell_TrayWnd/TrayNotifyWnd e soprattutto ToolbarWindow32, invece,
     * sono create dal thread del servizio. Muovere il toolbar da due thread
     * mentre comctl32 sta elaborando TB_* è una race nativa: su Windows 11
     * può terminare dentro COMCTL32/msvcrt con 0xC0000005. Si accoda sempre
     * l'ultimo rettangolo al thread proprietario. */
    if (ownerThread != 0 && GetCurrentThreadId() != ownerThread) {
        bool post = false;
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_pendingBarRect = bar;
            m_pendingNotifyRect = notify;
            if (!m_shellRectsPosted) {
                m_shellRectsPosted = true;
                post = true;
            }
        }
        if (post && !PostMessageW(trayWnd, kMsgShellRects, 0, 0)) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_shellRectsPosted = false;
        }
        return;
    }

    ApplyShellRectsOnThread(bar, notify);
}

void TrayService::ApplyShellRectsOnThread(const RECT& bar,
                                          const RECT& notify) {
    /* Questo metodo è chiamato solo dal thread che possiede le finestre.
     * Nessuna SendMessage di comctl32 attraversa il confine del dispatcher
     * gestito. */
    if (m_trayWnd == nullptr || !IsWindow(m_trayWnd)) {
        return;
    }

    SetWindowPos(m_trayWnd, nullptr,
                 static_cast<int>(bar.left), static_cast<int>(bar.top),
                 static_cast<int>(bar.right - bar.left),
                 static_cast<int>(bar.bottom - bar.top),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_notifyWnd != nullptr && IsWindow(m_notifyWnd)) {
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

/* ------------------------------------------------------------------ */
/*  v3.5 - Inoltro standard e chiave del riquadro batteria            */
/* ------------------------------------------------------------------ */

/* Garantisce che la chiave che chiede a ExplorerPatcher il riquadro
 * batteria Win32 sia a 1: HKCU\...\ImmersiveShell\UseWin32BatteryFlyout.
 * Il frontend la scrive quando le preferenze cambiano; qui si ripete al
 * momento del clic perche' la shell puo' leggerla proprio mentre apre il
 * riquadro (un'installazione recente o un reset delle impostazioni non
 * devono portare l'utente al riquadro moderno).
 *
 * v3.10.1 TODO (blast-radius): si e' valutata la chiave per-utente
 *   HKCU\Control Panel\Quick Actions\Control Center\QuickActionsStateCapture
 * scoperta da valinet come meccanismo piu' circoscritto per ottenere
 * lo stesso effetto; non viene adottata in questa PR perche' cambiare
 * chiave cambia il comportamento funzionale del routing del clic e va
 * verificata sulla build 26100 dell'utente prima di rilasciarla. Le
 * difese qui (restore in Stop/WM_ENDSESSION/UnhandledExceptionFilter e
 * file di backup con recovery al boot) riducono drasticamente il blast
 * radius della chiave attuale. */
/* v1.4 - LA CHIAVE LEGACY E' TRANSITORIA. Il valore UseWin32BatteryFlyout
 * viene scritto SOLO attorno al tentativo di apertura (prima del clic, poi
 * ripristinato al valore precedente quando il tentativo finisce, in un
 * senso o nell'altro): il registro dell'utente non resta toccato. E' la
 * piu' fedele attuazione possibile della richiesta "modifica in memoria
 * senza toccarlo realmente" da parte di un processo che NON vive dentro
 * explorer.exe: la lettura che conta e' quella di explorer, quindi il
 * valore deve essere vero nel registro per l'istante del clic.
 * OPZIONE B: questo core nativo e' l'UNICO scrittore della chiave. Il
 * livello gestito non la scrive mai: ne' all'avvio, ne' all'Applica/OK,
 * ne' sul percorso dell'icona batteria vera (TaskbarWindow si limita a
 * chiedere al nativo il ripristino esplicito in chiusura pulita). */
/* v3.10.1 - Crash-time restore della chiave batteria.
 *
 * Due best-effort path addizionali al restore on-stop / on-next-start:
 *
 *  1. SetUnhandledExceptionFilter: chiamato per AV, stack overflow,
 *     C++ terminate, ecc. Windows lascia ~1 secondo prima di terminare
 *     il processo: facciamo un restore sincrono minimale (solo Win32
 *     API, nessuna allocazione C++ perche' l'heap potrebbe essere
 *     corrotto) e poi passiamo al filtro precedente.
 *  2. WM_ENDSESSION / WM_QUERYENDSESSION: il wndproc chiama
 *     OnBatteryKeySessionEnding() non appena Windows avvisa che
 *     logoff/shutdown e' imminente (budget ~5 s), prima che il pump
 *     possa uscire senza eseguire Stop().
 *
 * Entrambi sono migliori di "aspetta il prossimo avvio" ma sono
 * best-effort: il file di backup e il restore-on-boot restano la rete
 * di sicurezza finale. */

static LPTOP_LEVEL_EXCEPTION_FILTER g_prevBatteryCrashFilter = nullptr;
static volatile LONG g_batteryCrashFilterInstalled = 0;

/* v3.10.1: i tre stati della chiave batteria sono definiti QUI, prima di
 * ogni helper che li usa (BestEffortRestoreBatteryKeyNoAlloc, crash
 * filter, OnBatteryKeySessionEnding), cosi' sono visibili nel punto
 * d'uso. */
static bool g_batteryKeyTouched = false;
static DWORD g_batteryKeyPrevValue = 0;
static bool g_batteryKeyPrevExists = false;
static std::wstring BatteryBackupFilePath();

static BOOL BestEffortRestoreBatteryKeyNoAlloc() {
    /* Restore minimale, no allocazioni C++, no std::string, no lock.
     * Ritorna TRUE se il registro e' coerente col valore precedente
     * (o se non c'era niente da ripristinare); il chiamante usa
     * questo per decidere se e' sicuro cancellare il file di backup. */
    if (!g_batteryKeyTouched) {
        return TRUE;
    }
    HKEY rawKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",
                      0, KEY_SET_VALUE, &rawKey) != ERROR_SUCCESS ||
        rawKey == nullptr) {
        return FALSE;
    }
    raii::RegKeyHandle key(rawKey);
    LSTATUS s = ERROR_SUCCESS;
    if (g_batteryKeyPrevExists) {
        s = RegSetValueExW(key.get(), L"UseWin32BatteryFlyout", 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&g_batteryKeyPrevValue),
                           sizeof(g_batteryKeyPrevValue));
    } else {
        s = RegDeleteValueW(key.get(), L"UseWin32BatteryFlyout");
        if (s == ERROR_FILE_NOT_FOUND) s = ERROR_SUCCESS;
    }
    return (s == ERROR_SUCCESS) ? TRUE : FALSE;
}

static LONG WINAPI BatteryKeyCrashFilter(EXCEPTION_POINTERS* ex) {
    /* Crash path: best effort only. Non cancelliamo il file di backup
     * da qui (lo spazio di indirizzi e' potenzialmente corrotto); il
     * boot successivo RecoverBatteryFlyoutKeyFromBackup lo pulira'. */
    (void)BestEffortRestoreBatteryKeyNoAlloc();
    if (g_prevBatteryCrashFilter != nullptr
        && g_prevBatteryCrashFilter != &BatteryKeyCrashFilter) {
        return g_prevBatteryCrashFilter(ex);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void InstallBatteryKeyCrashGuard() {
    if (InterlockedCompareExchange(&g_batteryCrashFilterInstalled, 1, 0) != 0) {
        return;   /* gia' installato */
    }
    g_prevBatteryCrashFilter = SetUnhandledExceptionFilter(&BatteryKeyCrashFilter);
}

static void OnBatteryKeySessionEnding() {
    /* Notifica di logoff/shutdown da Windows: ripristiniamo subito.
     * Cancelliamo il file di backup SOLAMENTE se il restore del
     * registro e' andato a buon fine: altrimenti teniamo il file per
     * il recovery al prossimo avvio. */
    if (BestEffortRestoreBatteryKeyNoAlloc()) {
        DeleteFileW(BatteryBackupFilePath().c_str());
    }
}

/* File di backup: prevExists(1 byte) + prevValue(4 byte LE). */

static std::wstring BatteryBackupFilePath() {
    wchar_t localAppData[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        /* Fallback minimale. */
        return L"C:\\Win7Taskbar-battery-backup.dat";
    }
    return std::wstring(localAppData) + L"\\Win7Taskbar\\battery-key-backup.dat";
}

static void WriteBatteryFlyoutBackup(bool prevExists, DWORD prevValue) {
    const std::wstring path = BatteryBackupFilePath();
    /* Assicura che la cartella esista. */
    {
        const size_t slash = path.find_last_of(L'\\');
        if (slash != std::wstring::npos) {
            CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
        }
    }
    raii::GenericHandle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                       nullptr));
    if (!h) {
        return;
    }
    uint8_t buf[1 + 4];
    buf[0] = prevExists ? 1u : 0u;
    buf[1] = static_cast<uint8_t>(prevValue & 0xFFu);
    buf[2] = static_cast<uint8_t>((prevValue >> 8) & 0xFFu);
    buf[3] = static_cast<uint8_t>((prevValue >> 16) & 0xFFu);
    buf[4] = static_cast<uint8_t>((prevValue >> 24) & 0xFFu);
    DWORD written = 0;
    BOOL okWrite = WriteFile(h.get(), buf, sizeof(buf), &written, nullptr);
    if (!okWrite || written != sizeof(buf)) {
        /* Scrittura fallita o incompleta: meglio NESSUN backup che un
         * file corrotto che mentirebbe sul valore precedente. */
        DeleteFileW(path.c_str());
    }
}

static void DeleteBatteryFlyoutBackup() {
    DeleteFileW(BatteryBackupFilePath().c_str());
}

/* Chiamata all'avvio (ThreadMain): se il file di backup esiste, il run
 * precedente e' terminato in modo anomalo mentre un clic-batteria era
 * pendente. Ripristiniamo la chiave al valore precedente e poi buttiamo
 * il file. */
static void RecoverBatteryFlyoutKeyFromBackup() {
    const std::wstring path = BatteryBackupFilePath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    raii::GenericHandle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h) {
        return;
    }
    uint8_t buf[1 + 4] = {};
    DWORD readN = 0;
    const bool ok = ReadFile(h.get(), buf, sizeof(buf), &readN, nullptr) == TRUE
                 && readN == sizeof(buf);
    if (!ok) {
        DeleteFileW(path.c_str());
        return;
    }
    const bool prevExists = (buf[0] != 0);
    const DWORD prevValue = static_cast<DWORD>(buf[1])
                         | (static_cast<DWORD>(buf[2]) << 8)
                         | (static_cast<DWORD>(buf[3]) << 16)
                         | (static_cast<DWORD>(buf[4]) << 24);
    HKEY rawKey = nullptr;
    raii::RegKeyHandle key;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",
            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
            nullptr, &rawKey, nullptr) == ERROR_SUCCESS && rawKey != nullptr) {
        key.reset(rawKey);
        const LSTATUS restoreResult = prevExists
            ? RegSetValueExW(key.get(), L"UseWin32BatteryFlyout", 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&prevValue),
                             sizeof(prevValue))
            : RegDeleteValueW(key.get(), L"UseWin32BatteryFlyout");
        if (restoreResult == ERROR_SUCCESS ||
            (!prevExists && restoreResult == ERROR_FILE_NOT_FOUND)) {
            /* Cancelliamo il backup solo DOPO un Reg* di successo. */
            DeleteFileW(path.c_str());
            LogTagged(L"GATE",
                      L"batteria: recuperata chiave legacy da backup (terminazione anomala precedente)");
        }
    }
    /* Se il restore e' fallito (key non apribile, ecc.), lasciamo il
     * file sul disco: il prossimo avvio riprovera'. */
}

static void EnsureWin32BatteryFlyoutValue() {
    if (g_batteryKeyTouched) {
        return;   /* un tentativo e' gia' in corso: non toccare due volte */
    }
    HKEY rawKey = nullptr;
    raii::RegKeyHandle key;
    const LSTATUS opened = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",
        0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_QUERY_VALUE,
        nullptr, &rawKey, nullptr);
    if (opened != ERROR_SUCCESS || rawKey == nullptr) {
        return;
    }
    key.reset(rawKey);
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LSTATUS read = RegQueryValueExW(key.get(), L"UseWin32BatteryFlyout",
                                          nullptr, &type,
                                          reinterpret_cast<LPBYTE>(&value),
                                          &size);
    g_batteryKeyPrevExists = (read == ERROR_SUCCESS);
    g_batteryKeyPrevValue = (read == ERROR_SUCCESS) ? value : 0;
    if (read != ERROR_SUCCESS || value != 1) {
        /* v3.10.1 hardening: PRIMA scriviamo il file di backup, POI
         * impostiamo la chiave. In caso di crash/kill fra RegSetValue e
         * Restore, il prossimo avvio RecoverBatteryFlyoutKeyFromBackup
         * ripristinera' il valore precedente. */
        WriteBatteryFlyoutBackup(g_batteryKeyPrevExists, g_batteryKeyPrevValue);
        const DWORD one = 1;
        /* Regola README: la scrittura passa da RegistryPolicy con
         * backup rilevato HKCU\Software\Win7Taskbar\RegistryBackup (il
         * file di backup qui sopra resta il recovery no-alloc del
         * filtro SEH: unica eccezione documentata ai Reg* diretti). */
        w7t::PolicyValue batteryFlyout;
        batteryFlyout.subkey =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell";
        batteryFlyout.valueName = L"UseWin32BatteryFlyout";
        batteryFlyout.type = REG_DWORD;
        batteryFlyout.data.assign(reinterpret_cast<const BYTE*>(&one),
                                  reinterpret_cast<const BYTE*>(&one) + sizeof(one));
        w7t::RegistryPolicy::WriteWithBackup(batteryFlyout);
        g_batteryKeyTouched = true;
    }
}

/* Ripristina il valore precedente (o cancella la voce che non c'era). */
static void RestoreWin32BatteryFlyoutValue() {
    if (!g_batteryKeyTouched) {
        return;
    }
    g_batteryKeyTouched = false;
    /* Deleghiamo al BestEffort (stessa logica). Solo se restituisce
     * successo cancelliamo il file di backup, come da review: uno
     * scrittura/Reg* fallito non deve privarci dell'unica informazione
     * di recovery. */
    const BOOL ok = BestEffortRestoreBatteryKeyNoAlloc();
    if (ok) {
        DeleteBatteryFlyoutBackup();
        LogTagged(L"GATE", L"batteria: chiave legacy ripristinata al valore precedente");
    } else {
        LogTagged(L"GATE",
                  L"batteria: restore chiave fallito, tengo il backup per il prossimo avvio");
    }
}

/* OPZIONE B: punto d'ingresso esplicito per il ripristino chiesto dal
 * livello gestito in chiusura pulita (W7T_BatteryFlyoutRestoreLegacyKey).
 * Lo stato (touched/prev/backup) e' tutto statico di questo file, quindi
 * il metodo e' statico anche lui. */
void TrayService::RestoreBatteryFlyoutKey() {
    RestoreWin32BatteryFlyoutValue();
}

/* Il clic standard su una voce VERA della tray, identico al forwarding in
 * fondo a SendClick (semantica ManagedShell):
 *   pressione  = WM_LBUTTONDOWN (doppio clic con GetDoubleClickTime)
 *   rilascio   = WM_LBUTTONUP, piu' NIN_SELECT dalla versione 3.
 * Serve al ramo batteria: quando il clic arriva su un'icona vera di
 * stobject.dll (o su una sua compagna nel modello) NON va toccato dal
 * routing dei riquadri: e' Windows ad aprire il suo riquadro Win32. */
static void ForwardStandardTrayClick(HWND owner, uint32_t callbackMessage,
                                     uint32_t uid, uint32_t version,
                                     int32_t clickType, int32_t x, int32_t y) {
    if (owner == nullptr || !IsWindow(owner) || callbackMessage == 0) {
        return;
    }
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
        case W7T_TRAY_CLICK_LEFT_DOWN:
            send(WM_LBUTTONDOWN);
            break;
        case W7T_TRAY_CLICK_LEFT:
            send(WM_LBUTTONUP);
            if (version >= 3) {
                send(NIN_SELECT);
            }
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/*  v3.5 - Menu contestuali delle icone di sistema ricreate           */
/* ------------------------------------------------------------------ */

/* Lancia un pannello di sistema (mixer, mmsys.cpl, Centri...). Il
 * resto della barra non apre finestre di errore: se il pannello non
 * esiste su quella build, il clic resta senza effetto.
 * v3.6: ShellExecuteExW con SEE_MASK_NOASYNC | SEE_MASK_FLAG_DDEWAIT -
 * i pannelli del Pannello di controllo parlano DDE e il lancio avviene
 * dal thread della barra: senza quei flag il lancio poteva fallire (o
 * peggio, restare impantanato nel DDE della shell). */
static void RunSystemPanel(const wchar_t* file, const wchar_t* params) {
    SHELLEXECUTEINFOW exec{};
    exec.cbSize = sizeof(exec);
    exec.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_DDEWAIT;
    exec.lpVerb = L"open";
    exec.lpFile = file;
    exec.lpParameters = params;
    exec.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&exec)) {
        LogTagged(L"GATE", L"menu tray: lancio non riuscito per %s", file);
    }
}

/* Mostra il menu di Windows 7 per l'icona di sistema ricreata indicata,
 * ancorato al cursore (x, y schermo). Ritorna il codice W7T_OK/W7T_ERR. */
static int32_t ShowSyntheticContextMenu(SystemIconKind kind, int32_t x,
                                        int32_t y) {
    /* Le voci arrivano dalle tabelle di traduzione del core (Strings.cpp),
     * quindi il menu parla la lingua scelta dall'utente. Le stesse voci
     * che Windows 7 mostrava con il tasto destro su quelle tre icone. */
    std::wstring text;
    if (kind == SystemIconKind::Volume) {
        text  = std::wstring(S(StrId::CtxVolMixer)) + L"\n-\n"
              + S(StrId::CtxPlayback) + L"\n"
              + S(StrId::CtxRecording) + L"\n"
              + S(StrId::CtxSounds);
    } else if (kind == SystemIconKind::Network) {
        text  = std::wstring(S(StrId::CtxTroubleshoot)) + L"\n-\n"
              + S(StrId::CtxNetCenter);
    } else if (kind == SystemIconKind::Battery) {
        text  = std::wstring(S(StrId::CtxMobility)) + L"\n-\n"
              + S(StrId::CtxPower);
    } else {
        return W7T_ERR_INVALID_ARG;
    }

    const int32_t chosen = ShellMenu::ShowContextMenuEx(
        x, y, true, text.c_str(), true);
    if (chosen <= 0) {
        return W7T_OK;   /* menu chiuso senza scelta */
    }

    /* I separatori non contano nella numerazione di ShowContextMenuEx. */
    if (kind == SystemIconKind::Volume) {
        switch (chosen) {
            case 1: RunSystemPanel(L"SndVol.exe", nullptr); break;
            case 2: RunSystemPanel(L"rundll32.exe",
                                   L"shell32.dll,Control_RunDLL mmsys.cpl,,0"); break;
            case 3: RunSystemPanel(L"rundll32.exe",
                                   L"shell32.dll,Control_RunDLL mmsys.cpl,,1"); break;
            case 4: RunSystemPanel(L"rundll32.exe",
                                   L"shell32.dll,Control_RunDLL mmsys.cpl,,2"); break;
            default: break;
        }
    } else if (kind == SystemIconKind::Network) {
        switch (chosen) {
            case 1: RunSystemPanel(L"msdt.exe",
                                   L"-id NetworkDiagnosticsWeb"); break;
            case 2: RunSystemPanel(L"control.exe",
                                   L"/name Microsoft.NetworkAndSharingCenter"); break;
            default: break;
        }
    } else {
        switch (chosen) {
            case 1: RunSystemPanel(L"mblctr.exe", nullptr); break;
            case 2: RunSystemPanel(L"control.exe",
                                   L"/name Microsoft.PowerOptions"); break;
            default: break;
        }
    }
    return W7T_OK;
}

int32_t TrayService::SendClick(uint64_t ownerHwnd, uint32_t uid, int32_t clickType,
                               int32_t x, int32_t y) {
    uint32_t callbackMessage = 0;
    uint32_t version = 0;
    uint32_t uiaUid = 0;
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
        uiaUid = it->second.uiaUid;
        uiaEntry = it->second.fromWin11Uia || uiaUid != 0;
        syntheticKind = it->second.syntheticKind;
    }

    /* v2.61 - Icone di sistema ricreate da noi (la tray di Windows 11 non
     * le espone: vedi ApplyWin11TraySnapshot). Non c'e' nessun elemento UI
     * Automation da invocare: il clic apre il riquadro del tipo.
     *
     * v2.62 - E IL RIQUADRO E' QUELLO DI WINDOWS 7.
     *
     * Prima questi tre clic finivano nei flyout immersivi della shell: su
     * Windows 11 l'utente si vedeva aprire i riquadri di Windows 10 (o il
     * ripiego) al posto di quelli di Windows 7 che questa barra ricrea. Le
     * icone sono ricreate da noi, quindi il riquadro e' il nostro:
     * volume -> riquadro classico del volume, rete -> riquadro di rete di
     * Windows 7, batteria -> riquadro batteria ricreato.
     *
     * Il tasto destro fa la stessa cosa del sinistro (come in Windows 7 il
     * clic sul riquadro di volume lo apre e basta): NON si inoltra piu' alla
     * shell, che avrebbe aperto un menu contestuale di Windows 11 estraneo a
     * questa barra. Il tasto centrale resta senza azione, come per le altre
     * voci della tray. */
    if (syntheticKind != SystemIconKind::None) {
        if (clickType == W7T_TRAY_CLICK_MIDDLE) {
            return W7T_ERR_INVALID_ARG;
        }

        /* Il frontend manda DUE eventi per un clic (pressione e rilascio),
         * esattamente come la shell. Qui si agisce solo sul rilascio: aprire
         * il riquadro anche sulla pressione significava aprirlo e richiuderlo
         * subito (i riquadri di rete e batteria si aprono/chiudono a
         * alternanza), cioe' il clic sembrava non fare niente. */
        if (clickType == W7T_TRAY_CLICK_LEFT_DOWN) {
            return W7T_OK;
        }

        /* v3.5 - IL TASTO DESTRO APRE IL MENU DI WINDOWS 7.
         *
         * Prima il tasto destro faceva la stessa cosa del sinistro: il
         * riquadro, mai un menu, perche' il menu era quello di Windows 11
         * e sarebbe risultato estraneo alla barra. Ora il destro mostra il
         * menu contestuale di Windows 7 di quell'icona (le stesse voci
         * tradotte del resto della barra: "Apri Mixer volume",
         * "Dispositivi di riproduzione", ... per il volume;
         * "Risoluzione dei problemi", "Centro connessioni" per la rete;
         * "Centro mobility", "Opzioni risparmio energia" per la
         * batteria). */
        if (clickType == W7T_TRAY_CLICK_RIGHT) {
            return ShowSyntheticContextMenu(syntheticKind, x, y);
        }

        /* v2.61 - IL FLYOUT VA DOVE STA L'ICONA ADESSO.
         *
         * Il rettangolo arriva dal frontend (W7T_SetIconRect) e viene
         * rimandato a ogni movimento reale dell'icona: barra spostata, DPI
         * cambiato, altro monitor, icone riordinate, overflow aperto o
         * chiuso, Explorer riavviato. Qui si usa quello, non la posizione
         * dell'importazione; se manca o non e' plausibile (fuori da ogni
         * monitor, rettangolo vuoto) si ripiega sul rettangolo della barra,
         * che il core conosce perche' il frontend glielo riporta a ogni
         * layout (W7T_SetShellRects). */
        RECT anchor{};
        if (!CurrentIconRect(TrayIconKey{ ownerHwnd, uid }, anchor)) {
            if (m_trayWnd == nullptr || !GetWindowRect(m_trayWnd, &anchor)) {
                return W7T_ERR_NOT_FOUND;
            }
        }

        /* v2.63 - CHI APRE IL RIQUADRO NON LO DECIDE PIU' QUESTO SWITCH.
         *
         * La scelta "Windows 7" / "Windows 10/11" delle Proprieta' arriva
         * qui dal frontend (W7T_SetFlyoutPreferences) e vive in un posto
         * solo (FlyoutLauncher.cpp). Prima ogni ramo aveva la propria copia
         * della regola: il volume apriva SEMPRE il mixer classico anche con
         * "Windows 10/11" selezionato, la batteria apriva SEMPRE il riquadro
         * ricreato anche con "Windows 10/11" selezionato, la rete il
         * ricreato o il ripiego moderno a seconda di un flag. Da qui
         * l'impressione - giusta - che le due voci fossero scambiate. */
        const w7t::FlyoutKind routeKind =
            (syntheticKind == SystemIconKind::Volume)  ? w7t::FlyoutKind::Sound
          : (syntheticKind == SystemIconKind::Network) ? w7t::FlyoutKind::Network
                                                       : w7t::FlyoutKind::Battery;
        const w7t::FlyoutRoute route = w7t::ChooseFlyoutRoute(routeKind);

        switch (syntheticKind) {
            case SystemIconKind::Volume: {
                /* "Windows 7": il mixer classico (SndVol -f), ancorato alla
                 * icona: e' quello che Windows 7 mostrava al clic
                 * sull'icona del volume.
                 * v3.9: il rettangolo passa INTERO (non piu' centro+top):
                 * con la barra IN ALTO il riquadro scende VERSO IL BASSO
                 * sotto l'icona invece di uscire fuori schermo. Se il
                 * lancio non riesce (SndVol assente o rifiutato dal
                 * sistema) si usa il riquadro del volume della shell. */
                if (route == w7t::FlyoutRoute::Classic) {
                    if (w7t::LaunchClassicVolumeNear(anchor) != 0) {
                        return W7T_OK;
                    }
                    LogTagged(L"GATE", L"volume: SndVol non disponibile, uso il riquadro della shell");
                }
                const int32_t immersive = FlyoutLauncher::ShowVolumeFlyoutAt(anchor);
                /* v3.9: anche il riquadro moderno va agganciato dove sta
                 * l'icona ADESSO: con la barra in alto la shell lo apre
                 * in basso a destra (dove starebbe la sua taskbar); il
                 * watcher del flyout lo riposiziona sotto/sopra l'icona
                 * come per i clic inoltrati alla tray. */
                if (immersive == W7T_OK) {
                    StartFlyoutWatcher(TrayIconKey{ ownerHwnd, uid });
                }
                return immersive;
            }

            case SystemIconKind::Network:
                /* "Windows 7": il riquadro di rete ricreato, ma solo quando il
                 * frontend lo ha preparato (NetFlyoutInit); la preparazione
                 * (g_ctx, hook) la fa il frontend una volta sola.
                 * v3.8: stessa cosa per la variante Windows 8 (scelta
                 * "Windows 8 (ricreato)"): il rilancio verso il riquadro
                 * della shell se non pronta e' lo stesso percorso gia'
                 * usato quando il modulo Windows 7 non e' pronto. */
                if (route == w7t::FlyoutRoute::Classic) {
                    if (w7t::PreferredStyle(FlyoutKind::Network) == w7t::FlyoutStyle::Win8) {
                        if (m_win8NetworkFlyoutReady) {
                            /* La scelta e' una sola: se il riquadro Windows
                             * 7 era visibile per strada secondaria (hotkey
                             * della mod) si chiude prima di aprire questo. */
                            w7tnet::W8NetLogic_HideWin7FlyoutIfOpen();
                            w7t::Win8NetworkFlyout::Instance().SetAnchorRect(anchor);
                            w7t::Win8NetworkFlyout::Instance().Toggle();
                            return W7T_OK;
                        }
                        LogTagged(L"GATE",
                            L"rete: variante Windows 8 non pronta, ripiego sul riquadro della shell");
                    } else if (m_win7NetworkFlyoutReady) {
                        /* Vale anche all'inverso: il riquadro Windows 8, se
                         * aperto, cede il posto a quello Windows 7. */
                        w7t::Win8NetworkFlyout::Instance().Hide();
                        w7tnet::W7TNetFlyout_SetAnchorRect(&anchor);
                        w7tnet::W7TNetFlyout_Toggle();
                        return W7T_OK;
                    }
                }
                return FlyoutLauncher::InvokeFlyoutAt(FlyoutKind::Network,
                                                      FlyoutAction::Show, anchor);

            case SystemIconKind::Battery: {
                /* v2.63/v3.5 - IL RIQUADRO BATTERIA VERO DI WINDOWS 7, A
                 * TUTTI I COSTI. L'ordine dei tentativi, tutti dentro la
                 * stessa risposta al clic:
                 *   1. se QUESTA icona e' quella vera di stobject.dll, il
                 *      clic si comporta come un clic normale su una voce
                 *      della tray (inoltro standard): e' WINDOWS ad aprire
                 *      il suo riquadro Win32, che con la chiave
                 *      UseWin32BatteryFlyout e' quello di Windows 7;
                 *   2. altrimenti si cerca un'icona vera di stobject.dll
                 *      nel modello e si inoltra a lei il clic;
                 *   3. altrimenti il pulsante batteria della tray di
                 *      Windows 11, via UI Automation;
                 *   4. solo se proprio non c'e' niente di vero, il riquadro
                 *      ricreato, ancorato all'icona nostra.
                 * Prima di qualunque inoltro la chiave UseWin32BatteryFlyout
                 * viene riaffermata a 1 (e' quella che ExplorerPatcher usa
                 * per questa scelta). La verifica differita (watch) dopo i
                 * tentativi 2 e 3 confronta le finestre visibili per
                 * INSIEME, non piu' solo lo stile popup: era il difetto che
                 * mostrava il ricreato SOPRA il riquadro vero. */
                /* v1.7 - POLITICA RICHIESTA: la voce "Windows 7" della
                 * tendina apre SEMPRE il riquadro ricreato, senza alcun
                 * tentativo verso la shell (il lettore UIA sulla build
                 * 26100 restituisce 0 icone quasi sempre, quindi la catena
                 * reale finiva nel ricreato dopo 5 inutili riprove). La
                 * voce "Windows 10/11" continua a puntare al riquadro
                 * VERO: prima le icone reali di stobject.dll, poi il
                 * pulsante batteria della tray di Windows 11 via UI
                 * Automation, e solo se non c'e' niente di vero il
                 * ricreato. La chiave legacy resta TRANSITORIA e viene
                 * usata solo da questa seconda voce. */
                if (route == w7t::FlyoutRoute::Classic) {
                    BatteryFlyout::Instance().ShowAt(anchor);
                    return W7T_OK;
                }

                /* v1.0.0-alpha: tutta la catena reale (chiave registry,
                 * enumerazione stobject.dll, UIA click, flyout watcher,
                 * BatteryFlyout::ShowAt) vive dentro un try/catch: un
                 * problema in uno qualunque di questi stadi (UIA non
                 * registrato, lettura di processo fallita, COM che
                 * lancia) non deve abbattere il thread del frontend WPF
                 * ne', peggio, il pump della tray. */
                int32_t batteryRouteResult = W7T_OK;
                bool batteryRouted = false;
                try {
                    EnsureWin32BatteryFlyoutValue();

                    /* 1. l'icona cliccata E' la batteria vera. */
                    {
                        HWND self = reinterpret_cast<HWND>(
                            static_cast<uintptr_t>(ownerHwnd));
                        if (self != nullptr && IsWindow(self)
                            && !uiaEntry
                            && OwnerModuleIs(self, L"stobject.dll")) {
                            ForwardStandardTrayClick(self, callbackMessage,
                                                     uid, version,
                                                     W7T_TRAY_CLICK_LEFT_DOWN,
                                                     x, y);
                            ForwardStandardTrayClick(self, callbackMessage,
                                                     uid, version,
                                                     W7T_TRAY_CLICK_LEFT,
                                                     x, y);
                            StartFlyoutWatcher(TrayIconKey{ ownerHwnd, uid });
                            LogTagged(L"GATE",
                                      L"batteria: clic standard sull'icona vera (stobject.dll)");
                            batteryRouteResult = W7T_OK;
                            batteryRouted = true;
                        }
                    }

                    /* 2. un'altra batteria vera nel modello. */
                    if (!batteryRouted) {
                        uint64_t fwdOwner = 0;
                        uint32_t fwdUid = 0, fwdCb = 0, fwdVer = 0;
                        if (FindRealStobjectIcon(&fwdOwner, &fwdUid,
                                                 &fwdCb, &fwdVer)) {
                            HWND real = reinterpret_cast<HWND>(
                                static_cast<uintptr_t>(fwdOwner));
                            ForwardStandardTrayClick(real, fwdCb, fwdUid,
                                                     fwdVer,
                                                     W7T_TRAY_CLICK_LEFT_DOWN,
                                                     x, y);
                            ForwardStandardTrayClick(real, fwdCb, fwdUid,
                                                     fwdVer,
                                                     W7T_TRAY_CLICK_LEFT,
                                                     x, y);
                            StartFlyoutWatcher(TrayIconKey{ ownerHwnd, uid });
                            LogTagged(L"GATE",
                                      L"batteria: clic inoltrato all'icona vera (stobject.dll)");
                            batteryRouteResult = W7T_OK;
                            batteryRouted = true;
                        }
                    }

                    /* 3. pulsante batteria Win11 via UIA. */
                    if (!batteryRouted) {
                        const uint32_t shellBatteryUid =
                            Win11TrayReader::Instance().UidOfKind(SystemIconKind::Battery);
                        if (shellBatteryUid != 0) {
                            if (Win11TrayReader::Instance().RequestClick(shellBatteryUid, false)) {
                                StartBatteryOpenWatch(anchor);
                                StartFlyoutWatcher(TrayIconKey{ ownerHwnd, uid });
                                LogTagged(L"GATE",
                                          L"batteria: clic sul pulsante vero della shell (UIA)");
                                batteryRouteResult = W7T_OK;
                                batteryRouted = true;
                            } else {
                                LogTagged(L"GATE",
                                          L"batteria: pulsante shell non cliccabile, riprovo");
                                StartBatteryUiARetry(anchor);
                                batteryRouteResult = W7T_OK;
                                batteryRouted = true;
                            }
                        } else {
                            LogTagged(L"GATE",
                                      L"batteria: pulsante shell assente dallo snapshot, riprovo");
                            StartBatteryUiARetry(anchor);
                            batteryRouteResult = W7T_OK;
                            batteryRouted = true;
                        }
                    }

                    /* 4. fallback: flyout ricreato. */
                    if (!batteryRouted) {
                        BatteryFlyout::Instance().ShowAt(anchor);
                        batteryRouteResult = W7T_OK;
                    }
                } catch (const std::exception&) {
                    LogTagged(L"GATE",
                              L"batteria: eccezione C++ nell'instradamento reale, uso il ricreato");
                    try { RestoreWin32BatteryFlyoutValue(); } catch (...) {}
                    try {
                        BatteryFlyout::Instance().ShowAt(anchor);
                        batteryRouteResult = W7T_OK;
                    } catch (...) {
                        batteryRouteResult = W7T_ERR_NOT_FOUND;
                    }
                } catch (...) {
                    LogTagged(L"GATE",
                              L"batteria: eccezione sconosciuta nell'instradamento reale, uso il ricreato");
                    try { RestoreWin32BatteryFlyoutValue(); } catch (...) {}
                    try {
                        BatteryFlyout::Instance().ShowAt(anchor);
                        batteryRouteResult = W7T_OK;
                    } catch (...) {
                        batteryRouteResult = W7T_ERR_NOT_FOUND;
                    }
                }
                return batteryRouteResult;
            }

            default:
                break;
        }
        return W7T_ERR_NOT_FOUND;
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
        return Win11TrayReader::Instance().RequestClick(
                   uiaUid != 0 ? uiaUid : uid, right)
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

    /* v3.6 - IL DESTRO SULLA BATTERIA VERA APRE IL NOSTRO MENU.
     * L'icona vera di stobject.dll (quella che l'utente vedeva comparire
     * in ritardo accanto alla ricreata) inoltrava il menu a Windows, che
     * lo disegna nella lingua DI WINDOWS: con l'app in francese il menu
     * usciva in italiano. Il destro ora apre il menu di Windows 7
     * tradotto, come per le icone ricreate. */
    if (clickType == W7T_TRAY_CLICK_RIGHT
        && OwnerModuleIs(owner, L"stobject.dll")) {
        return ShowSyntheticContextMenu(SystemIconKind::Battery, x, y);
    }

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
