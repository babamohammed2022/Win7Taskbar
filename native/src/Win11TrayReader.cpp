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
 * Vedi Win11TrayReader.h. In breve: Windows 11 puo' usare una tray XAML
 * senza toolbar Win32. Il lettore attraversa solo cio' che il provider UIA
 * espone, usa prima i pattern di accessibilita' per il clic e non modifica
 * il registro; il supporto completo della shell non viene dichiarato.
 */

#include "Win11TrayReader.h"
#include "../include/RaiiWrappers.h"

#include "SehGuard.h"
#include "ScopeGuards.h"
#include "Strings.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <map>
#include <memory>
#include <new>
#include <set>

#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <uiautomationclient.h>

namespace w7t {

namespace {

/* CLSID_CUIAutomation and the two pattern IIDs are written out here on
 * purpose: the import libraries that carry them (uiautomationcore.lib /
 * libuiautomationcore.a) are not part of every MinGW-w64 build, and we never
 * call a plain UIA function - only COM methods through the vtables. */
const GUID kClsidCUIAutomation =
    { 0xFF48DBA4, 0x60EF, 0x4201, { 0xAA, 0x87, 0x54, 0x10, 0x3E, 0xEF, 0x59, 0x4E } };
const GUID kIidIUIAutomation =
    { 0x30CBE57D, 0xD9D0, 0x452A, { 0xAB, 0x13, 0x7A, 0xC5, 0xAC, 0x48, 0x25, 0xEE } };
const GUID kIidInvokePattern =
    { 0xFB377FBE, 0x8EA6, 0x46D5, { 0x9C, 0x73, 0x64, 0x99, 0x64, 0x2D, 0x30, 0x59 } };
const GUID kIidLegacyPattern =
    { 0x828055AD, 0x355B, 0x4435, { 0x86, 0xD5, 0x3B, 0x51, 0xC1, 0x4A, 0x9B, 0x1B } };
/* IUIAutomationPropertyChangedEventHandler: serve per sapere SUBITO che lo
 * stato di un'icona e' cambiato (percentuale della batteria, rete che va e
 * viene, tooltip riscritto) invece di aspettare il risveglio di sicurezza. */
const GUID kIidPropertyChangedHandler =
    { 0x40CD37D4, 0xC756, 0x4B0C, { 0x8C, 0x6F, 0xBD, 0xDF, 0xEE, 0xB1, 0x3B, 0x50 } };
const GUID kIidStructureChangedHandler =
    { 0xE81D1B4E, 0x11C5, 0x42F8, { 0x97, 0x54, 0xE7, 0x03, 0x6C, 0x79, 0xF0, 0x54 } };

constexpr UINT kMsgRead        = WM_APP + 1;
constexpr UINT kMsgClick       = WM_APP + 2;
constexpr UINT kMsgOverflow    = WM_APP + 3;
constexpr UINT kMsgPlaceFlyout = WM_APP + 4;
constexpr UINT kTimerPlacement = 0x51;   /* thread timer, HWND == nullptr */
/* v1.5: fine della finestra di trasparenza al click (stesso tipo di timer). */
constexpr UINT kTimerClickThrough = 0x52; /* thread timer, HWND == nullptr */
/* Materializzazione ritardata del flyout per il raccolto silenzioso. */
constexpr UINT kTimerSilentHarvest = 0x53; /* thread timer, HWND == nullptr */

/* v1.5: stato del clic "passante" (patterns UIA muti -> clic vero attraverso
 * la nostra barra). Salva stili e cursore; il timer del thread ripristina. */
struct ClickThroughSaved {
    HWND hwnd;
    LONG_PTR exStyle;
};
static std::vector<ClickThroughSaved> g_clickThroughSaved;
static POINT g_clickThroughCursor = {};
static bool g_clickThroughActive = false;

struct ClickThroughEnumCtx {
    POINT pt;
    std::vector<ClickThroughSaved>* out;
};

static BOOL CALLBACK CollectOurWindowsAtPoint(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<ClickThroughEnumCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) {
        return TRUE;
    }
    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) {
        return TRUE;
    }
    POINT pt = ctx->pt;
    if (PtInRect(&wr, pt)) {
        try {
            ctx->out->push_back(
                { hwnd, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) });
        } catch (...) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Overflow flyout placement: the hook callback runs on the worker thread
 * and only records what it saw; the loop does the window work. */
std::atomic<bool> g_watchFlyout{ false };
std::atomic<bool> g_flyoutSilent{ false };
std::atomic<DWORD> g_flyoutThreadId{ 0 };
HWND              g_flyoutHwnd  = nullptr;
RECT              g_flyoutAnchor = {};

void CALLBACK OverflowShownProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject,
                                LONG idChild, DWORD, DWORD) {
    if (hwnd == nullptr || idObject != OBJID_WINDOW || idChild != 0) {
        return;
    }
    if (!g_watchFlyout.load()) {
        return;
    }
    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, 128) == 0) {
        return;
    }
    if (wcsstr(cls, L"TopLevelWindowForOverflowXamlIsland") == nullptr) {
        return;
    }
    g_flyoutHwnd = hwnd;
    const DWORD threadId = g_flyoutThreadId.load();
    if (threadId != 0) {
        PostThreadMessageW(threadId, kMsgPlaceFlyout, 0, 0);
    }
}

/* BSTR with a destructor: UIA hands out owned strings. */
struct Bstr {
    BSTR value = nullptr;
    ~Bstr() {
        if (value != nullptr) {
            SysFreeString(value);
        }
    }
    std::wstring str() const {
        return value != nullptr ? std::wstring(value, SysStringLen(value))
                                : std::wstring();
    }
};

std::wstring Lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

bool Contains(const std::wstring& haystack, const wchar_t* needle) {
    if (needle == nullptr || *needle == L'\0') {
        return false;
    }
    return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}

/* Le finestre Shell_TrayWnd di Explorer non sono una API della tray: sono
 * solo il punto di ingresso pubblico di User32 per raggiungere l'albero UIA.
 * Il servizio registra una finestra con lo stesso nome, percio' il processo
 * viene sempre verificato prima di usare un HWND. */
DWORD WindowProcessId(HWND hwnd) {
    DWORD pid = 0;
    if (hwnd != nullptr) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
    return pid;
}

bool IsExplorerProcess(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    raii::GenericHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                            FALSE, pid));
    if (!process) {
        return false;
    }
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    if (QueryFullProcessImageNameW(process.get(), 0, path, &length) == FALSE ||
        length == 0) {
        return false;
    }
    const wchar_t* slash = wcsrchr(path, L'\\');
    const wchar_t* name = slash == nullptr ? path : slash + 1;
    return _wcsicmp(name, L"explorer.exe") == 0;
}

struct WindowList {
    DWORD processId = 0;
    std::vector<HWND> windows;
};

BOOL CALLBACK CollectExplorerTaskbars(HWND hwnd, LPARAM parameter) {
    auto* list = reinterpret_cast<WindowList*>(parameter);
    if (list == nullptr) {
        return FALSE;
    }
    wchar_t className[128] = {};
    if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) == 0 ||
        (lstrcmpW(className, L"Shell_TrayWnd") != 0 &&
         lstrcmpW(className, L"Shell_SecondaryTrayWnd") != 0)) {
        return TRUE;
    }
    const DWORD pid = WindowProcessId(hwnd);
    if (pid == 0 || pid == GetCurrentProcessId() ||
        !IsExplorerProcess(pid)) {
        return TRUE;
    }
    try {
        list->windows.push_back(hwnd);
    } catch (...) {
        /* Un callback User32 non deve mai propagare un'eccezione C++. */
        return FALSE;
    }
    return TRUE;
}

std::vector<HWND> FindShellTaskbars() {
    WindowList list;
    EnumWindows(CollectExplorerTaskbars, reinterpret_cast<LPARAM>(&list));
    return list.windows;
}

struct ChildWindowList {
    DWORD processId = 0;
    std::vector<HWND> windows;
};

BOOL CALLBACK CollectXamlBridges(HWND hwnd, LPARAM parameter) {
    auto* list = reinterpret_cast<ChildWindowList*>(parameter);
    if (list == nullptr || WindowProcessId(hwnd) != list->processId) {
        return TRUE;
    }
    wchar_t className[128] = {};
    if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) == 0 ||
        (wcsstr(className, L"DesktopWindowContentBridge") == nullptr &&
         lstrcmpW(className, L"Windows.UI.Input.InputSite.WindowClass") != 0)) {
        return TRUE;
    }
    try {
        list->windows.push_back(hwnd);
    } catch (...) {
        return FALSE;
    }
    return TRUE;
}

std::vector<HWND> FindXamlBridgeWindows(HWND taskbar) {
    ChildWindowList list;
    list.processId = WindowProcessId(taskbar);
    if (list.processId == 0) {
        return list.windows;
    }
    EnumChildWindows(taskbar, CollectXamlBridges,
                     reinterpret_cast<LPARAM>(&list));
    return list.windows;
}

BOOL CALLBACK CollectOverflowIslands(HWND hwnd, LPARAM parameter) {
    auto* list = reinterpret_cast<WindowList*>(parameter);
    if (list == nullptr || WindowProcessId(hwnd) != list->processId) {
        return TRUE;
    }
    wchar_t className[128] = {};
    if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) == 0 ||
        lstrcmpW(className, L"TopLevelWindowForOverflowXamlIsland") != 0) {
        return TRUE;
    }
    try {
        list->windows.push_back(hwnd);
    } catch (...) {
        return FALSE;
    }
    return TRUE;
}

/* L'isola dell'overflow puo' essere assente quando il flyout e' chiuso.
 * Quando esiste, si filtrano tutte le istanze e non solo la prima finestra
 * in ordine Z: Explorer puo' ricrearla mentre quella precedente sta uscendo. */
std::vector<HWND> FindOverflowIslands() {
    std::vector<HWND> result;
    std::set<HWND> seen;
    const std::vector<HWND> taskbars = FindShellTaskbars();
    for (HWND taskbar : taskbars) {
        WindowList list;
        list.processId = WindowProcessId(taskbar);
        if (list.processId == 0) {
            continue;
        }
        EnumWindows(CollectOverflowIslands, reinterpret_cast<LPARAM>(&list));
        for (HWND overflow : list.windows) {
            if (seen.insert(overflow).second) {
                result.push_back(overflow);
            }
        }
    }
    return result;
}

/* Il bridge XAML (o il relativo InputSite nelle build che lo espongono) e'
 * l'evidenza osservabile che la taskbar usa la tray moderna. E' una verifica
 * di forma della finestra, non una promessa che ogni build esponga tutti gli
 * elementi tramite UI Automation. */
bool HasXamlBridge(HWND taskbar) {
    if (taskbar != nullptr && !FindXamlBridgeWindows(taskbar).empty()) {
        return true;
    }
    return !FindOverflowIslands().empty();
}

/* Struttura della toolbar legacy della tray sotto una taskbar specifica. */
bool HasClassicTrayToolbarUnder(HWND tray) {
    if (tray == nullptr) {
        return false;
    }
    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify == nullptr) {
        return false;
    }
    if (FindWindowExW(notify, nullptr, L"SysPager", nullptr) == nullptr) {
        return false;
    }
    return FindWindowExW(notify, nullptr, L"ToolbarWindow32", nullptr) != nullptr;
}

/* ------------------------------------------------------------------ */
/*  WORKAROUND — risoluzione REALE dello stato della tray              */
/*                                                                     */
/*  Su Windows 11 < 24H2 Explorer puo' lasciare la gerarchia legacy     */
/*  TrayNotifyWnd -> SysPager -> ToolbarWindow32 come guscio vuoto     */
/*  accanto alla tray XAML. La sola presenza della catena non basta:   */
/*  si interroga il numero reale dei pulsanti prima di scegliere il    */
/*  lettore classico o quello UI Automation.                           */
/* ------------------------------------------------------------------ */

enum class TrayShellState : int {
    Unknown = 0,
    Classic = 1,
    Xaml = 2,
};

/* Generazione del riavvio di Explorer, consumata dalla cache di Detect(). */
std::atomic<ULONGLONG> g_explorerRestartGeneration{ 0 };

/* TB_BUTTONCOUNT senza dipendere da commctrl.h in questo translation unit. */
constexpr UINT kTbButtonCount = WM_USER + 24;

bool HasRealClassicTrayToolbar(HWND taskbar) {
    if (!HasClassicTrayToolbarUnder(taskbar)) {
        return false;
    }
    HWND toolbar = FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    if (toolbar != nullptr) {
        toolbar = FindWindowExW(toolbar, nullptr, L"SysPager", nullptr);
    }
    if (toolbar != nullptr) {
        toolbar = FindWindowExW(toolbar, nullptr, L"ToolbarWindow32", nullptr);
    }
    if (toolbar == nullptr) {
        return false;
    }

    DWORD_PTR buttons = 0;
    if (SendMessageTimeoutW(toolbar, kTbButtonCount, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 200,
                            &buttons) == 0) {
        return false;
    }
    return buttons != 0;
}

TrayShellState ResolveTrayShellState() {
    const std::vector<HWND> taskbars = FindShellTaskbars();
    if (taskbars.empty()) {
        return TrayShellState::Unknown;
    }

    bool classic = false;
    bool xaml = false;
    for (HWND taskbar : taskbars) {
        classic = HasRealClassicTrayToolbar(taskbar) || classic;
        xaml = HasXamlBridge(taskbar) || xaml;
    }
    if (xaml && !classic) {
        return TrayShellState::Xaml;
    }
    if (classic) {
        return TrayShellState::Classic;
    }
    return TrayShellState::Unknown;
}

std::wstring ExePathOf(uint32_t pid) {
    if (pid == 0) {
        return std::wstring();
    }
    raii::GenericHandle proc(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                          FALSE, pid));
    if (!proc) {
        return std::wstring();
    }
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    std::wstring result;
    if (QueryFullProcessImageNameW(proc.get(), 0, path, &length) != FALSE &&
        length > 0) {
        result.assign(path, length);
    }
    return result;
}

std::wstring ExeNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

/* Icons owned by the shell itself: volume, network, battery and the other
 * Windows 11 extras (bell, location...). They are recreated with our own
 * Windows 7 artwork when we know them, and skipped when we do not: a
 * Windows 7 tray has no bell and no location icon. */
bool IsShellProcess(const std::wstring& exeName) {
    static const wchar_t* kShellProcesses[] = {
        L"explorer.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe",
        L"SystemSettings.exe", L"ShellHost.exe",
    };
    for (const wchar_t* name : kShellProcesses) {
        if (_wcsicmp(exeName.c_str(), name) == 0) {
            return true;
        }
    }
    return false;
}

SystemIconKind ClassifySystemIcon(const std::wstring& name) {
    /* Localized labels from our own string tables first (Properties window
     * wording: "Volume:", "Rete:", "Batteria:"), then English and the most
     * common shell wordings as fallback. */
    const PropStrings& prop = PropStringsFor(CurrentLanguage());
    auto match = [&name](const wchar_t* label) {
        if (label == nullptr) {
            return false;
        }
        std::wstring word(label);
        while (!word.empty() && (word.back() == L':' || word.back() == L' ')) {
            word.pop_back();
        }
        if (word.size() < 3) {
            return false;
        }
        return Contains(name, word.c_str());
    };
    if (match(prop.lblVolume)) return SystemIconKind::Volume;
    if (match(prop.lblNetwork)) return SystemIconKind::Network;
    if (match(prop.lblBattery)) return SystemIconKind::Battery;

    static const wchar_t* kVolume[] = {
        L"volume", L"speaker", L"audio", L"altoparlant", L"lautstärke",
        L"громкость", L"音量", L"الصوت",
    };
    static const wchar_t* kNetwork[] = {
        L"network", L"wi-fi", L"wifi", L"ethernet", L"rete", L"réseau",
        L"netzwerk", L"сеть", L"网络", L"الشبكة",
    };
    static const wchar_t* kBattery[] = {
        L"battery", L"batter", L"bateria", L"аккумулятор", L"电池", L"البطارية",
    };
    for (const wchar_t* word : kVolume) {
        if (Contains(name, word)) return SystemIconKind::Volume;
    }
    for (const wchar_t* word : kNetwork) {
        if (Contains(name, word)) return SystemIconKind::Network;
    }
    for (const wchar_t* word : kBattery) {
        if (Contains(name, word)) return SystemIconKind::Battery;
    }
    return SystemIconKind::None;
}

/* -------------------------------------------------------------------------
 *  v2.62 - ICONA VERA DELL'APPLICAZIONE
 *
 *  La tray di Windows 11 non consegna nessuna immagine: l'albero di
 *  accessibilita' ha il nome e il proprietario, non il disegno. L'icona va
 *  quindi chiesta al processo, e la fonte migliore e' il processo stesso:
 *  molte applicazioni di tray mettono la PROPRIA icona (quella che su
 *  Windows 10 finirebbe in NOTIFYICONDATA.hIcon) su una finestra, spesso
 *  invisibile. Chiedere quel HICON da' l'icona esatta, stato compreso: e'
 *  quello che si vede sulla tray vera.
 *
 *  Ordine di ricerca: finestra con la classe che contiene "Tray" (quasi
 *  sempre la finestra del messaggio di tray dell'applicazione), poi
 *  qualunque altra finestra del processo, poi - solo se il processo non
 *  espone nulla - l'icona dell'eseguibile, e infine l'icona generica.
 * ------------------------------------------------------------------------- */

struct ProcessIconSearch {
    DWORD pid = 0;
    HICON trayWindow = nullptr;   /* finestra che sembra quella della tray */
    HICON anyWindow  = nullptr;   /* qualunque finestra del processo        */
};

BOOL CALLBACK CollectProcessIcon(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<ProcessIconSearch*>(param);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid) {
        return TRUE;
    }

    HICON icon = nullptr;
    DWORD_PTR result = 0;
    /* ICON_SMALL2 e' l'icona piccola "vera" quando l'applicazione ne ha una
     * distinta; ICON_SMALL la copre per le applicazioni piu' vecchie. */
    if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &result) != 0 &&
        result != 0) {
        icon = reinterpret_cast<HICON>(result);
    }
    if (icon == nullptr &&
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 120, &result) != 0 &&
        result != 0) {
        icon = reinterpret_cast<HICON>(result);
    }
    if (icon == nullptr) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    }
    if (icon == nullptr) {
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    }
    if (icon == nullptr) {
        return TRUE;
    }

    if (search->anyWindow == nullptr) {
        search->anyWindow = icon;
    }
    if (search->trayWindow == nullptr) {
        wchar_t cls[128] = {};
        GetClassNameW(hwnd, cls, _countof(cls));
        try {
            if (Contains(std::wstring(cls), L"Tray")) {
                search->trayWindow = icon;
            }
        } catch (...) {
            return FALSE;
        }
    }
    return TRUE;
}

bool LoadProcessWindowIcon(DWORD pid, ArgbBitmap& out) {
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return false;
    }

    ProcessIconSearch search;
    search.pid = pid;
    W7T_SEH_TRY {
        EnumWindows(CollectProcessIcon, reinterpret_cast<LPARAM>(&search));
    } W7T_SEH_CATCH {
        search.trayWindow = nullptr;
        search.anyWindow  = nullptr;
    } W7T_SEH_END

    HICON icon = search.trayWindow != nullptr ? search.trayWindow
                                              : search.anyWindow;
    if (icon == nullptr) {
        return false;
    }

    ArgbBitmap bmp;
    if (!IconToArgb(icon, bmp) || !BitmapSane(bmp)) {
        return false;
    }
    out = std::move(bmp);
    return true;
}

/* Icon of an executable: the image the tray shows for applications that do
 * not hand the shell their own HICON. */
bool LoadImageIcon(const std::wstring& exePath, ArgbBitmap& out) {
    /* NB: la variabile NON si puo' chiamare "small": rpcndr.h definisce
     * small/far/near/hyper/pascal come macro, e con MSVC la dichiarazione
     * diventa "HICON char" (dieci errori di sintassi a catena). Con MinGW
     * compila lo stesso e il bug si vede solo in CI.
     *
     * Il blocco SEH protegge esclusivamente la chiamata Win32 e contiene solo
     * tipi banali. Un longjmp non deve mai attraversare IconHandle o
     * ArgbBitmap: la conversione, che possiede oggetti C++, avviene dopo il
     * blocco protetto e resta quindi sotto RAII normale. */
    if (!exePath.empty()) {
        volatile HICON rawIcon = nullptr;
        volatile UINT extracted = 0;
        W7T_SEH_TRY {
            extracted = ExtractIconExW(
                exePath.c_str(), 0, nullptr,
                const_cast<HICON*>(&rawIcon), 1);
        } W7T_SEH_CATCH {
            const HICON failedIcon = rawIcon;
            if (failedIcon != nullptr) {
                DestroyIcon(failedIcon);
            }
            rawIcon = nullptr;
            extracted = 0;
        } W7T_SEH_END

        const HICON extractedIcon = rawIcon;
        if (extracted > 0 && extractedIcon != nullptr) {
            rawIcon = nullptr;
            raii::IconHandle iconSmall(extractedIcon);
            ArgbBitmap bmp;
            if (IconToArgb(iconSmall.get(), bmp) && BitmapSane(bmp) &&
                BitmapHasContent(bmp)) {
                out = std::move(bmp);
            }
        }
    }
    if (!out.empty()) {
        return true;
    }

    /* Last resort: the generic application icon. Never leave a tray slot
     * empty: an icon with a tooltip is always better than a hole. */
    volatile HICON generic = nullptr;
    W7T_SEH_TRY {
        generic = LoadIconW(nullptr, IDI_APPLICATION);
    } W7T_SEH_CATCH {
        generic = nullptr;
    } W7T_SEH_END
    const HICON genericIcon = generic;
    if (genericIcon == nullptr) {
        return false;
    }

    ArgbBitmap bmp;
    if (!IconToArgb(genericIcon, bmp) || !BitmapSane(bmp)) {
        return false;
    }
    out = std::move(bmp);
    return true;
}

/* Stable identity of an accessibility element: the tray has no numeric id,
 * so the key is the owning process plus the tooltip plus the position. */
uint32_t UidFromToken(const std::wstring& token) {
    uint32_t hash = 2166136261u;                 /* FNV-1a */
    for (wchar_t c : token) {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }
    uint32_t uid = 0x77000000u | (hash & 0x00FFFFFFu);
    if (uid == 0x77000000u) {
        uid = 0x77000001u;
    }
    return uid;
}

/* UIA non offre un HWND o un uID dell'applicazione che ha registrato
 * l'icona: CurrentProcessId e' il processo del provider, normalmente
 * explorer.exe. RuntimeId e' l'identita' dell'elemento per tutta la vita
 * dell'isola e non cambia quando cambia il tooltip. Viene usato solo come
 * chiave di sessione; dopo un riavvio della shell la documentazione UIA non
 * promette che resti uguale. */
struct SafeArrayDestroyGuard {
    SAFEARRAY* value = nullptr;
    ~SafeArrayDestroyGuard() {
        if (value != nullptr) {
            SafeArrayDestroy(value);
        }
    }
};

struct SafeArrayAccessGuard {
    SAFEARRAY* value = nullptr;
    bool active = false;
    ~SafeArrayAccessGuard() {
        if (active && value != nullptr) {
            SafeArrayUnaccessData(value);
        }
    }
};

std::wstring RuntimeIdOf(IUIAutomationElement* element) {
    if (element == nullptr) {
        return std::wstring();
    }
    SAFEARRAY* raw = nullptr;
    if (FAILED(element->GetRuntimeId(&raw)) || raw == nullptr) {
        return std::wstring();
    }
    SafeArrayDestroyGuard arrayGuard{ raw };
    if (SafeArrayGetDim(raw) != 1) {
        return std::wstring();
    }
    LONG lower = 0;
    LONG upper = -1;
    if (FAILED(SafeArrayGetLBound(raw, 1, &lower)) ||
        FAILED(SafeArrayGetUBound(raw, 1, &upper)) || upper < lower) {
        return std::wstring();
    }

    LONG* values = nullptr;
    if (FAILED(SafeArrayAccessData(raw, reinterpret_cast<void**>(&values))) ||
        values == nullptr) {
        return std::wstring();
    }
    SafeArrayAccessGuard accessGuard{ raw, true };
    try {
        std::wstring result;
        for (LONG index = lower; index <= upper; ++index) {
            if (!result.empty()) {
                result.push_back(L':');
            }
            result += std::to_wstring(values[index - lower]);
        }
        return result;
    } catch (...) {
        return std::wstring();
    }
}

} /* namespace */

/* ------------------------------------------------------------------ */
/*  v2.62 - Eventi di proprieta' della tray                            */
/*                                                                     */
/*  La shell notifica il cambio di proprieta' degli elementi della     */
/*  tray: e' il modo per sapere SUBITO che la batteria e' scesa, che   */
/*  la rete e' cambiata o che un'applicazione ha riscritto il suo      */
/*  tooltip, senza interrogare l'albero a intervalli. L'handler non    */
/*  legge niente: segnala al thread di lavoro di rileggere, con una    */
/*  soglia minima fra due segnalazioni (gli aggiornamenti della shell  */
/*  arrivano a raffica).                                               */
/*                                                                     */
/*  La classe sta fuori dal namespace anonimo di proposito: un         */
/*  oggetto COM implementato in un namespace anonimo puo' essere       */
/*  ottimizzato via, e il difetto si vede solo a runtime.              */
/* ------------------------------------------------------------------ */

class TrayPropertyChangeHandler final : public IUIAutomationPropertyChangedEventHandler {
public:
    void AttachTo(DWORD threadId) {
        m_threadId = threadId;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refs));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG left = InterlockedDecrement(&m_refs);
        if (left == 0) {
            delete this;
        }
        return static_cast<ULONG>(left);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, kIidPropertyChangedHandler) ||
            IsEqualIID(riid, IID_IUnknown)) {
            *object = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(
            IUIAutomationElement* sender, PROPERTYID propertyId,
            VARIANT newValue) override {
        (void)sender;
        (void)propertyId;
        (void)newValue;

        const ULONGLONG now = GetTickCount64();
        if (now - m_lastPost.load() < 300) {
            return S_OK;
        }
        m_lastPost.store(now);

        const DWORD thread = m_threadId;
        if (thread != 0) {
            PostThreadMessageW(thread, kMsgRead, 0, 0);
        }
        return S_OK;
    }

private:
    LONG  volatile       m_refs = 1;
    DWORD                m_threadId = 0;
    std::atomic<ULONGLONG> m_lastPost{ 0 };
};

/* Un cambio nella struttura dell'albero copre aggiunte, rimozioni e
 * ricreazioni dell'isola che non generano una modifica di Name/IsEnabled.
 * UIA mantiene il riferimento all'handler fino alla rimozione esplicita;
 * il servizio lo libera sempre nel percorso di uscita del worker. */
class TrayStructureChangeHandler final
    : public IUIAutomationStructureChangedEventHandler {
public:
    void AttachTo(DWORD threadId) {
        m_threadId = threadId;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&m_refs));
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG left = InterlockedDecrement(&m_refs);
        if (left == 0) {
            delete this;
        }
        return static_cast<ULONG>(left);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, kIidStructureChangedHandler) ||
            IsEqualIID(riid, IID_IUnknown)) {
            *object = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(
            IUIAutomationElement* sender, StructureChangeType changeType,
            SAFEARRAY* runtimeId) override {
        (void)sender;
        (void)changeType;
        (void)runtimeId;

        const ULONGLONG now = GetTickCount64();
        if (now - m_lastPost.load() < 300) {
            return S_OK;
        }
        m_lastPost.store(now);

        const DWORD thread = m_threadId;
        if (thread != 0) {
            PostThreadMessageW(thread, kMsgRead, 0, 0);
        }
        return S_OK;
    }

private:
    LONG  volatile        m_refs = 1;
    DWORD                 m_threadId = 0;
    std::atomic<ULONGLONG> m_lastPost{ 0 };
};

/* ------------------------------------------------------------------ */
/*  Requests between the caller and the worker                         */
/* ------------------------------------------------------------------ */

struct Win11TrayReader::Request {
    uint32_t uid         = 0;
    bool     rightButton = false;
    bool     silent      = false;
    RECT     anchor      = {};
};

/* ------------------------------------------------------------------ */
/*  Singleton / detection                                              */
/* ------------------------------------------------------------------ */

Win11TrayReader& Win11TrayReader::Instance() {
    static Win11TrayReader instance;
    return instance;
}

bool Win11TrayReader::Detect() {
    /* WORKAROUND: la rilevazione risolve lo stato reale della tray. Su
     * Windows 11 < 24H2 la catena legacy puo' sopravvivere come guscio
     * vuoto: TB_BUTTONCOUNT vale zero anche se la tray XAML e' attiva.
     *
     * Caching:
     *  - il risultato POSITIVO (XAML) resta definitivo;
     *  - Classic resta in cache solo 2 secondi;
     *  - Unknown non viene mai memorizzato come risultato negativo. */
    static std::atomic<int> cached{ 0 };       /* 0 = unknown, 1 = Xaml, 2 = Classic */
    static std::atomic<ULONGLONG> lastCheck{ 0 };
    static std::atomic<ULONGLONG> lastRestartGenerationSeen{ 0 };

    const ULONGLONG now = GetTickCount64();
    if (cached.load() == 1) {
        return true;
    }
    if (cached.load() == 2 && now - lastCheck.load() < 2000) {
        return false;
    }
    lastCheck.store(now);

    /* Dopo un rebuild di Explorer la forma della tray puo' essere cambiata. */
    const ULONGLONG restartGen = g_explorerRestartGeneration.load();
    if (restartGen != lastRestartGenerationSeen.load()) {
        lastRestartGenerationSeen.store(restartGen);
        cached.store(0);
    }

    const TrayShellState state = ResolveTrayShellState();
    switch (state) {
        case TrayShellState::Xaml:
            cached.store(1);
            return true;
        case TrayShellState::Classic:
            cached.store(2);
            lastCheck.store(GetTickCount64());
            return false;
        case TrayShellState::Unknown:
        default:
            cached.store(0);
            return false;
    }
}

void Win11TrayReader::NoteExplorerRestart() {
    /* Explorer puo' aver cambiato forma della tray: la prossima Detect()
     * deve risolvere nuovamente il livello legacy/XAML. */
    g_explorerRestartGeneration.fetch_add(1);
}

void Win11TrayReader::SetNotify(HWND wnd, UINT message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notifyWnd = wnd;
    m_notifyMsg = message;
}

bool Win11TrayReader::Start() {
    if (m_started.load()) {
        return true;
    }
    /* Un thread staccato qui sarebbe un use-after-free sul singleton. La
     * chiusura precedente deve avere sempre consumato il joinable prima di
     * permettere un nuovo avvio. */
    if (m_thread.joinable()) {
        return false;
    }
    m_threadId = 0;
    m_threadDone.store(false);
    m_running.store(true);
    try {
        m_thread = std::thread(&Win11TrayReader::WorkerMain, this);
    } catch (...) {
        m_running.store(false);
        return false;
    }

    /* Il thread crea la coda messaggi prima di pubblicare l'id: cosi' sia la
     * prima richiesta sia WM_QUIT non possono cadere per una coda ancora
     * inesistente. */
    for (int i = 0; i < 200 && m_threadId.load() == 0; ++i) {
        Sleep(5);
    }
    const DWORD threadId = m_threadId.load();
    if (threadId == 0) {
        m_running.store(false);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        return false;
    }
    m_started.store(true);
    /* v2.62: prima lettura SUBITO, senza aspettare un evento della shell. */
    if (!PostThreadMessageW(threadId, kMsgRead, 0, 0)) {
        m_running.store(false);
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        if (m_thread.joinable()) {
            m_thread.join();
        }
        m_threadId = 0;
        m_started.store(false);
        return false;
    }
    return true;
}

void Win11TrayReader::Stop() {
    if (!m_started.load() && !m_thread.joinable()) {
        return;
    }
    m_running.store(false);
    const DWORD threadId = m_threadId.load();
    if (threadId != 0) {
        PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }
    if (m_thread.joinable()) {
        /* UIA non offre un cancel portabile per una chiamata in corso. Si
         * attende la fine reale invece di fare detach: il detach lasciava il
         * worker con un puntatore a questo singleton durante la distruzione. */
        m_thread.join();
    }
    m_threadId = 0;
    m_started.store(false);
}

void Win11TrayReader::RequestRead() {
    const DWORD threadId = m_threadId.load();
    if (threadId != 0) {
        PostThreadMessageW(threadId, kMsgRead, 0, 0);
    }
}

bool Win11TrayReader::RequestClick(uint32_t uid, bool rightButton) {
    const DWORD threadId = m_threadId.load();
    if (threadId == 0 || uid == 0) {
        return false;
    }
    Request* request = new (std::nothrow) Request();
    if (request == nullptr) {
        return false;
    }
    request->uid = uid;
    request->rightButton = rightButton;
    if (!PostThreadMessageW(threadId, kMsgClick, 0,
                            reinterpret_cast<LPARAM>(request))) {
        delete request;
        return false;
    }
    return true;
}

bool Win11TrayReader::RequestOverflowFlyout(const RECT& anchor, bool silent) {
    const DWORD threadId = m_threadId.load();
    if (threadId == 0) {
        return false;
    }
    Request* request = new (std::nothrow) Request();
    if (request == nullptr) {
        return false;
    }
    request->anchor = anchor;
    request->silent = silent;
    if (!PostThreadMessageW(threadId, kMsgOverflow, 0,
                            reinterpret_cast<LPARAM>(request))) {
        delete request;
        return false;
    }
    return true;
}

std::vector<Win11TrayItem> Win11TrayReader::TakeSnapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

uint32_t Win11TrayReader::UidOfKind(SystemIconKind kind) const {
    if (kind == SystemIconKind::None) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Win11TrayItem& item : m_snapshot) {
        if (item.kind == kind) {
            return item.uid;
        }
    }
    return 0;
}

SystemIconKind Win11TrayReader::KindOf(uint32_t uid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Win11TrayItem& item : m_snapshot) {
        if (item.uid == uid) {
            return item.kind;
        }
    }
    return SystemIconKind::None;
}

/* ------------------------------------------------------------------ */
/*  Worker thread                                                      */
/* ------------------------------------------------------------------ */

void Win11TrayReader::WorkerMain() {
    /* Nessuna eccezione deve attraversare std::thread: oltre a terminare il
     * processo, lascerebbe l'istanza con il worker marcato vivo. La guardia
     * ripristina anche lo stato globale usato dal clic passante/flyout quando
     * un'operazione UIA o una allocazione fallisce a meta'. */
    try {
        const auto workerCleanup = raii::on_scope_exit([]() noexcept {
            g_watchFlyout.store(false);
            g_flyoutSilent.store(false);
            g_flyoutThreadId.store(0);
            KillTimer(nullptr, kTimerPlacement);
            KillTimer(nullptr, kTimerSilentHarvest);
            KillTimer(nullptr, kTimerClickThrough);
            g_flyoutHwnd = nullptr;
            for (const auto& saved : g_clickThroughSaved) {
                if (IsWindow(saved.hwnd)) {
                    SetWindowLongPtrW(saved.hwnd, GWL_EXSTYLE, saved.exStyle);
                }
            }
            g_clickThroughSaved.clear();
            g_clickThroughActive = false;
        });

        /* Forza la creazione della message queue prima di pubblicare l'id:
         * PostThreadMessage/WM_QUIT richiedono una coda appartenente al thread. */
        MSG bootstrap{};
    PeekMessageW(&bootstrap, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    m_threadId = GetCurrentThreadId();
    g_flyoutThreadId.store(m_threadId.load());
    m_threadDone.store(false);

    /* UIA is used from a worker: MTA is the recommended apartment for a
     * client that does not own a window. */
    raii::ComInitializer com(COINIT_MULTITHREADED);

    raii::ComPtr<IUIAutomation> uia;
    if (com.succeeded() &&
        SUCCEEDED(CoCreateInstance(kClsidCUIAutomation, nullptr,
                                   CLSCTX_INPROC_SERVER, kIidIUIAutomation,
                                   reinterpret_cast<void**>(uia.Put()))) &&
        uia) {
        AppendCoreLog(L"tray Win11: lettura via UI Automation attiva");
    } else {
        AppendCoreLog(L"tray Win11: UI Automation non disponibile");
    }

    std::map<uint32_t, raii::ComPtr<IUIAutomationElement>> elements;
    raii::ComPtr<IUIAutomationElement> chevron;
    UniqueWinEventHook flyoutHook;

    /* v2.62: ascolto dei cambi di proprieta' e della struttura delle isole.
     * Si tengono piu' radici: la barra principale e l'overflow XAML sono
     * finestre top-level diverse, soprattutto mentre il flyout e' aperto. */
    raii::ComPtr<TrayPropertyChangeHandler> propertyHandler;
    raii::ComPtr<TrayStructureChangeHandler> structureHandler;
    struct WatchedRoot {
        HWND hwnd = nullptr;
        raii::ComPtr<IUIAutomationElement> element;
        bool propertyAdded = false;
        bool structureAdded = false;
    };
    std::vector<WatchedRoot> watchedRoots;

    auto clearWatchedRoots = [&]() {
        for (WatchedRoot& watched : watchedRoots) {
            if (uia && watched.element) {
                if (watched.propertyAdded && propertyHandler) {
                    uia->RemovePropertyChangedEventHandler(
                        watched.element.Get(), propertyHandler.Get());
                }
                if (watched.structureAdded && structureHandler) {
                    uia->RemoveStructureChangedEventHandler(
                        watched.element.Get(), structureHandler.Get());
                }
            }
        }
        watchedRoots.clear();
    };
    const auto watchedRootsCleanup = raii::on_scope_exit(
        [&]() noexcept { clearWatchedRoots(); });

    auto watchIslandProperties = [&](HWND island) {
        if (!uia || island == nullptr) {
            return;
        }
        for (const WatchedRoot& watched : watchedRoots) {
            if (watched.hwnd == island) {
                return;
            }
        }
        if (!propertyHandler) {
            propertyHandler = raii::ComPtr<TrayPropertyChangeHandler>(
                new (std::nothrow) TrayPropertyChangeHandler());
            if (!propertyHandler) {
                return;
            }
            propertyHandler->AttachTo(m_threadId.load());
        }
        if (!structureHandler) {
            structureHandler = raii::ComPtr<TrayStructureChangeHandler>(
                new (std::nothrow) TrayStructureChangeHandler());
            if (!structureHandler) {
                return;
            }
            structureHandler->AttachTo(m_threadId.load());
        }

        raii::ComPtr<IUIAutomationElement> root;
        if (FAILED(uia->ElementFromHandle(island, root.Put())) || !root) {
            return;
        }
        /* Nome e stato attivo coprono i tooltip e i cambi di stato; la
         * struttura copre NIM_ADD/NIM_DELETE riflessi dalla shell XAML. */
        PROPERTYID properties[] = {
            UIA_NamePropertyId,
            UIA_IsEnabledPropertyId,
            UIA_IsOffscreenPropertyId,
        };
        const bool propertyAdded = SUCCEEDED(
            uia->AddPropertyChangedEventHandlerNativeArray(
                root.Get(), TreeScope_Subtree, nullptr, propertyHandler.Get(),
                properties, ARRAYSIZE(properties)));
        const bool structureAdded = SUCCEEDED(
            uia->AddStructureChangedEventHandler(
                root.Get(), TreeScope_Subtree, nullptr, structureHandler.Get()));
        if (propertyAdded || structureAdded) {
            try {
                WatchedRoot watched;
                watched.hwnd = island;
                watched.propertyAdded = propertyAdded;
                watched.structureAdded = structureAdded;
                watched.element = std::move(root);
                watchedRoots.push_back(std::move(watched));
            } catch (...) {
                if (propertyAdded) {
                    uia->RemovePropertyChangedEventHandler(root.Get(),
                                                           propertyHandler.Get());
                }
                if (structureAdded) {
                    uia->RemoveStructureChangedEventHandler(root.Get(),
                                                            structureHandler.Get());
                }
            }
        }
    };

    auto releaseElements = [&]() {
        elements.clear();
        chevron.Reset();
    };

    auto postReady = [&]() {
        HWND target = nullptr;
        UINT message = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            target = m_notifyWnd;
            message = m_notifyMsg;
        }
        if (target != nullptr && message != 0 && IsWindow(target)) {
            PostMessageW(target, message, 0, 0);
        }
    };

    /* --- collection of the tray elements --------------------------- */

    /* I nomi delle classi sono quelli osservabili dal provider UIA. Non si
     * accetta "SystemTray" in generale: TextIconContent e ImageIconContent
     * sono figli visuali, non icone, e produrrebbero doppioni. */
    auto isChevron = [](const std::wstring& cls,
                         const std::wstring& id,
                         const std::wstring& text) {
        /* I nomi UIA non sono un contratto: alcune build usano
         * SystemTray.ChevronIconView, altre espongono solo il nome
         * localizzato "Notification Chevron" o l'AutomationId. Il confronto
         * è case-insensitive e resta limitato a questi indizi, mai a tutti i
         * controlli chiamati SystemTray. */
        return Contains(cls, L"chevron") || Contains(cls, L"overflow") ||
               Contains(id, L"chevron") || Contains(id, L"overflow") ||
               Contains(text, L"notification chevron") ||
               Contains(text, L"show hidden icons");
    };
    auto isNotifyIconView = [](const std::wstring& cls) {
        return cls.find(L"SystemTray.NotifyIconView") != std::wstring::npos;
    };
    auto isSystemIconView = [](const std::wstring& cls,
                               const std::wstring& id,
                               const std::wstring& text) {
        if (cls.find(L"SystemTray.IconView") == std::wstring::npos) {
            return false;
        }
        /* Nelle build osservate l'AutomationId e' SystemTrayIcon; il nome
         * puo' essere vuoto per un provider XAML, quindi si accetta anche il
         * testo solo come seconda forma, mai il solo prefisso SystemTray. */
        return id == L"SystemTrayIcon" || text == L"SystemTrayIcon" ||
               (!id.empty() && id.find(L"SystemTrayIcon") != std::wstring::npos);
    };

    auto collectIsland = [&](HWND island, bool hidden, int& order,
                             std::set<uint32_t>& usedUids,
                             std::set<std::wstring>& seenElements,
                             std::vector<Win11TrayItem>& out) -> bool {
        if (!uia || island == nullptr) {
            return false;
        }
        raii::ComPtr<IUIAutomationElement> root;
        if (FAILED(uia->ElementFromHandle(island, root.Put())) || !root) {
            return false;
        }
        raii::ComPtr<IUIAutomationCondition> all;
        if (FAILED(uia->CreateTrueCondition(all.Put())) || !all) {
            return false;
        }
        raii::ComPtr<IUIAutomationElementArray> found;
        const HRESULT hr = root->FindAll(TreeScope_Descendants, all.Get(),
                                         found.Put());
        if (FAILED(hr) || !found) {
            return false;
        }

        int length = 0;
        if (FAILED(found->get_Length(&length))) {
            return false;
        }

        for (int i = 0; i < length; ++i) {
            raii::ComPtr<IUIAutomationElement> element;
            if (FAILED(found->GetElement(i, element.Put())) || !element) {
                continue;
            }

            Bstr name, cls, automationId;
            int controlType = UIA_CustomControlTypeId;
            BOOL offscreen = FALSE;
            int pid = 0;
            element->get_CurrentName(&name.value);
            element->get_CurrentClassName(&cls.value);
            element->get_CurrentAutomationId(&automationId.value);
            element->get_CurrentControlType(&controlType);
            element->get_CurrentIsOffscreen(&offscreen);
            element->get_CurrentProcessId(&pid);

            const std::wstring className = cls.str();
            const std::wstring id = automationId.str();
            const std::wstring text = name.str();

            if (isChevron(className, id, text)) {
                if (!chevron) {
                    chevron = std::move(element);
                }
                continue;
            }

            const bool notifyIconView = isNotifyIconView(className);
            const bool systemIconView = isSystemIconView(className, id, text);
            const bool isButton =
                controlType == UIA_ButtonControlTypeId ||
                controlType == UIA_ListItemControlTypeId ||
                controlType == UIA_CustomControlTypeId;

            /* WORKAROUND (overflow vuoto): regole di ritenzione per ruolo.
             * La barra principale richiede un elemento on-screen, nominato e
             * appartenente alla tray; l'overflow conserva anche elementi
             * off-screen e senza nome, perche' il flyout chiuso li espone
             * proprio in quello stato. Il tipo Custom resta ammesso: diverse
             * build XAML pubblicano NotifyIconView senza Button/ListItem. */
            const bool roleOverflow = hidden;
            const bool trayClass = notifyIconView || systemIconView;
            const bool trayishClass =
                className.find(L"Tray") != std::wstring::npos ||
                className.find(L"Icon") != std::wstring::npos;

            if (!isButton || !(trayClass || (roleOverflow && trayishClass))) {
                continue;
            }
            if (!roleOverflow && (offscreen != FALSE || text.empty())) {
                continue;
            }

            Win11TrayItem item;
            item.hidden = hidden;
            item.order = order++;
            item.pid = static_cast<uint32_t>(pid);

            /* CurrentProcessId identifica il provider UIA (quasi sempre
             * explorer.exe), NON il processo che ha registrato l'icona.
             * Usarlo come proprietario faceva classificare ogni app come
             * icona di sistema e la scartava. Per un'icona XAML generica
             * l'owner resta volutamente ignoto: il clic passa da UIA e il
             * modello usa un bitmap di ripiego, senza inventare un HWND. */
            const std::wstring providerPath = ExePathOf(static_cast<uint32_t>(pid));
            const bool providerIsShell =
                IsShellProcess(ExeNameOf(providerPath));
            item.exePath = providerIsShell ? std::wstring() : providerPath;
            item.systemOwned = systemIconView && providerIsShell;
            item.name = text;
            if (item.name.empty()) {
                /* Identita' di ripiego per i provider UIA senza CurrentName. */
                std::wstring owner = ExeNameOf(item.exePath);
                if (owner.empty()) {
                    wchar_t buf[32] = {};
                    swprintf(buf, 32, L"App %u",
                             static_cast<unsigned>(item.pid));
                    owner = buf;
                }
                item.name = owner;
            }

            if (item.systemOwned) {
                item.kind = ClassifySystemIcon(item.name);
                if (item.kind == SystemIconKind::None) {
                    /* Campanella, posizione, Copilot e altri elementi della
                     * shell moderna non hanno un equivalente Win7. */
                    continue;
                }
                if (!TrayFallbackIcons::Render(item.kind, item.bitmap)) {
                    continue;
                }
            } else {
                if (!providerIsShell &&
                    !LoadProcessWindowIcon(static_cast<DWORD>(pid), item.bitmap)) {
                    LoadImageIcon(item.exePath, item.bitmap);
                }
                if (item.bitmap.empty()) {
                    /* La tray XAML non espone l'HICON. Un elemento con nome
                     * valido resta comunque cliccabile e visibile nel
                     * modello: il generico evita un buco vuoto. */
                    LoadImageIcon(std::wstring(), item.bitmap);
                }
            }

            /* RuntimeId non include il tooltip e distingue due elementi
             * dello stesso provider; e' piu' onesto del falso "owner exe".
             * Se il provider non lo offre, l'AutomationId e il nome sono il
             * ripiego locale e l'isteresi del servizio evita rimozioni su un
             * singolo giro transitorio. */
            const std::wstring runtimeId = RuntimeIdOf(element.Get());
            std::wstring token;
            if (item.systemOwned) {
                token = L"uia:system|";
                token += (item.kind == SystemIconKind::Volume) ? L"volume"
                       : (item.kind == SystemIconKind::Network) ? L"network"
                                                                : L"battery";
            } else if (!runtimeId.empty()) {
                token = L"uia:runtime|" + runtimeId;
            } else {
                token = L"uia:element|" + className + L"|" + id;
                if (token.back() == L'|') {
                    token += text;
                }
            }
            if (!runtimeId.empty() || item.systemOwned) {
                const std::wstring dedupeKey = item.systemOwned
                    ? token : (L"runtime|" + runtimeId);
                if (!seenElements.insert(dedupeKey).second) {
                    continue;
                }
            }
            uint32_t uid = UidFromToken(token);
            for (int suffix = 1; usedUids.count(uid) != 0 ||
                                uid == 0x77000000u; ++suffix) {
                uid = UidFromToken(token + L"#" + std::to_wstring(suffix));
            }
            item.token = token;
            item.uid = uid;
            usedUids.insert(uid);
            out.push_back(std::move(item));
            const uint32_t uidKey = out.back().uid;
            elements.emplace(uidKey, std::move(element));
        }
        return true;
    };

    /* Fallback conservativo per le build che non mettono il chevron sotto
     * ElementFromHandle(Shell_TrayWnd) ma lo pubblicano comunque nel root
     * UIA desktop. Usiamo condizioni sui nomi/id, non una scansione cieca
     * dell'intero albero e non tocchiamo la shell. */
    auto findGlobalChevron = [&]() {
        if (chevron || !uia) {
            return;
        }
        raii::ComPtr<IUIAutomationElement> desktop;
        if (FAILED(uia->GetRootElement(desktop.Put())) || !desktop) {
            return;
        }

        const wchar_t* const names[] = {
            L"Notification Chevron",
            L"Show hidden icons",
            L"Show hidden icons menu",
        };
        const wchar_t* const ids[] = {
            L"NotificationChevron",
            L"Overflow",
        };
        raii::ComPtr<IUIAutomationCondition> conditions[5];
        int conditionCount = 0;
        auto addCondition = [&](PROPERTYID property, const wchar_t* value) {
            VARIANT variant{};
            VariantInit(&variant);
            variant.vt = VT_BSTR;
            variant.bstrVal = SysAllocString(value);
            if (variant.bstrVal != nullptr) {
                uia->CreatePropertyCondition(property, variant,
                                             conditions[conditionCount].Put());
                if (conditions[conditionCount]) {
                    ++conditionCount;
                }
            }
            VariantClear(&variant);
        };
        for (const wchar_t* name : names) {
            addCondition(UIA_NamePropertyId, name);
        }
        for (const wchar_t* id : ids) {
            addCondition(UIA_AutomationIdPropertyId, id);
        }
        if (conditionCount == 0) {
            return;
        }

        /* IUIAutomation offre solo l'overload binario: si costruisce
         * l'OR in modo incrementale, trasferendo ogni condizione con RAII. */
        raii::ComPtr<IUIAutomationCondition> any;
        for (int i = 0; i < conditionCount; ++i) {
            if (!any) {
                any = std::move(conditions[i]);
                continue;
            }
            raii::ComPtr<IUIAutomationCondition> combined;
            if (FAILED(uia->CreateOrCondition(any.Get(), conditions[i].Get(),
                                              combined.Put())) || !combined) {
                return;
            }
            any = std::move(combined);
        }
        if (!any) {
            return;
        }
        raii::ComPtr<IUIAutomationElement> candidate;
        if (FAILED(desktop->FindFirst(TreeScope_Descendants, any.Get(),
                                      candidate.Put())) || !candidate) {
            return;
        }

        Bstr name;
        Bstr cls;
        Bstr id;
        int pid = 0;
        candidate->get_CurrentName(&name.value);
        candidate->get_CurrentClassName(&cls.value);
        candidate->get_CurrentAutomationId(&id.value);
        candidate->get_CurrentProcessId(&pid);
        if (pid > 0 && IsExplorerProcess(static_cast<DWORD>(pid)) &&
            isChevron(cls.str(), id.str(), name.str())) {
            chevron = std::move(candidate);
            AppendCoreLog(L"tray Win11: chevron trovato dal root UIA");
        }
    };

    auto readNow = [&]() {
        releaseElements();
        std::vector<Win11TrayItem> items;
        std::set<uint32_t> usedUids;
        std::set<std::wstring> seenElements;
        int order = 0;
        const std::vector<HWND> taskbars = FindShellTaskbars();
        const std::vector<HWND> overflows = FindOverflowIslands();

        if (!uia || (taskbars.empty() && overflows.empty())) {
            /* Explorer sta ricreando le finestre o UIA non e' disponibile:
             * una lettura fallita non deve cancellare l'ultima fotografia. */
            m_lastReadValid.store(false);
            m_lastReadMainValid.store(false);
            m_lastReadOverflowValid.store(false);
            postReady();
            return;
        }

        bool traversedMain = false;
        for (HWND taskbar : taskbars) {
            const size_t beforeTaskbar = items.size();
            traversedMain = collectIsland(taskbar, false, order, usedUids,
                                          seenElements, items) || traversedMain;

            /* ElementFromHandle(Shell_TrayWnd) non include sempre il bridge
             * XAML nel provider UIA. Si prova il bridge di QUESTA taskbar
             * quando la radice non ha prodotto alcun elemento; non si usa il
             * risultato della taskbar primaria per saltare una secondaria. */
            if (items.size() == beforeTaskbar) {
                const std::vector<HWND> bridges = FindXamlBridgeWindows(taskbar);
                for (HWND bridge : bridges) {
                    traversedMain = collectIsland(bridge, false, order,
                                                  usedUids, seenElements,
                                                  items) || traversedMain;
                }
            }
        }

        bool traversedOverflow = false;
        for (HWND overflow : overflows) {
            traversedOverflow = collectIsland(overflow, true, order, usedUids,
                                               seenElements, items) ||
                                traversedOverflow;
        }

        /* Il chevron è necessario per la raccolta silenziosa dell'overflow,
         * ma la sua assenza non rende invalida una fotografia della barra. */
        findGlobalChevron();

        /* La presenza dell'host non basta: ElementFromHandle/FindAll possono
         * fallire mentre Explorer e' in ricostruzione. Solo una traversata
         * riuscita autorizza il servizio a considerare vuoto lo snapshot. */
        const bool valid = traversedMain || traversedOverflow;
        m_lastReadMainValid.store(traversedMain);
        m_lastReadOverflowValid.store(traversedOverflow);
        if (!valid) {
            m_lastReadValid.store(false);
            postReady();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snapshot = items;
        }
        m_lastReadValid.store(true);

        /* Da adesso i cambi di stato della shell arrivano da soli. */
        clearWatchedRoots();
        for (HWND taskbar : taskbars) {
            watchIslandProperties(taskbar);
            for (HWND bridge : FindXamlBridgeWindows(taskbar)) {
                watchIslandProperties(bridge);
            }
        }
        for (HWND overflow : overflows) {
            watchIslandProperties(overflow);
        }
        /* v1.7: si registra SOLO il cambiamento del conteggio; l'evento di
         * struttura/property copre nel frattempo aggiunte e modifiche. */
        static std::atomic<int> s_lastLoggedCount{ -1 };
        if (s_lastLoggedCount.exchange(
                static_cast<int>(items.size())) !=
            static_cast<int>(items.size())) {
            wchar_t line[192] = {};
            swprintf(line, 192,
                     L"tray Win11: %u icone (UI Automation), overflow=%s",
                     static_cast<unsigned>(items.size()),
                     traversedOverflow ? L"letto" : L"non disponibile");
            AppendCoreLog(line);
        }
        postReady();
    };

    auto safeReadNow = [&]() noexcept {
        try {
            readNow();
        } catch (...) {
            releaseElements();
            clearWatchedRoots();
            m_lastReadValid.store(false);
            m_lastReadMainValid.store(false);
            m_lastReadOverflowValid.store(false);
            OutputDebugStringW(L"Win11TrayReader: eccezione nella lettura UIA\n");
            postReady();
        }
    };

    /* --- clicks ---------------------------------------------------- */

    auto callPattern = [&](IUIAutomationElement* element, bool rightButton,
                           bool silent = false) {
        if (element == nullptr) {
            return false;
        }
        bool done = false;
        if (rightButton) {
            /* There is no "show context menu" pattern on
             * IUIAutomationLegacyIAccessiblePattern (only on the text range
             * interface), so the context menu is opened the way the
             * accessibility keyboard does it: focus the element and press
             * the Menu key. The focused element belongs to Explorer's island,
             * so our topmost window is not in the way. */
            element->SetFocus();
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_APPS;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = VK_APPS;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            done = SendInput(2, inputs, sizeof(INPUT)) == 2;
            if (!done) {
                AppendCoreLog(L"tray Win11: tasto Menu non consegnato");
            }
        } else {
            raii::ComPtr<IUIAutomationInvokePattern> invoke;
            if (SUCCEEDED(element->GetCurrentPatternAs(
                    UIA_InvokePatternId, kIidInvokePattern,
                    reinterpret_cast<void**>(invoke.Put()))) &&
                invoke) {
                done = SUCCEEDED(invoke->Invoke());
            }
            if (!done) {
                raii::ComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;
                if (SUCCEEDED(element->GetCurrentPatternAs(
                        UIA_LegacyIAccessiblePatternId, kIidLegacyPattern,
                        reinterpret_cast<void**>(legacy.Put()))) &&
                    legacy) {
                    done = SUCCEEDED(legacy->DoDefaultAction());
                }
            }
            if (!done) {
                if (silent) {
                    /* Durante un raccolto silenzioso non si usa mai
                     * SendInput, non si cambia il focus e non si attraversa
                     * la barra WPF: se il provider non espone Invoke, il
                     * timer/hook chiuderà e pulirà il tentativo. */
                    AppendCoreLog(L"tray Win11: overflow silenzioso senza pattern UIA");
                    return false;
                }
                /* v1.5 - ULTIMO RIPIEGO: il clic REALE per coordinate. Su
                 * alcune build di Windows 11 i pattern di accessibilita'
                 * dei pulsanti di sistema non producono effetto; un clic
                 * sintetico al centro del rettangolo dell'elemento e'
                 * esattamente cio' che farebbe l'utente. La nostra barra
                 * copre quella nativa, quindi il punto cade quasi sempre
                 * su una finestra NOSTRA: per l'istante del clic le nostre
                 * finestre sul punto diventano trasparenti al mouse
                 * (WS_EX_TRANSPARENT + layered opaco), il clic passa al
                 * pulsante VERO di explorer e il timer kTimerClickThrough
                 * ripristina stili e cursore. Prima (v1.4) il caso "coperto
                 * da noi" veniva saltato: cosi' il clic non arrivava MAI e
                 * il riquadro ricreato partiva sempre. */
                RECT bounds = {};
                if (SUCCEEDED(element->get_CurrentBoundingRectangle(
                        &bounds)) && bounds.right > bounds.left &&
                    bounds.bottom > bounds.top) {
                    const POINT centre = {
                        (bounds.left + bounds.right) / 2,
                        (bounds.top + bounds.bottom) / 2
                    };

                    ClickThroughEnumCtx ctx = { centre,
                                                &g_clickThroughSaved };
                    g_clickThroughSaved.clear();
                    EnumWindows(CollectOurWindowsAtPoint,
                                reinterpret_cast<LPARAM>(&ctx));

                    auto makeClick = [&centre](INPUT* click) {
                        click[0].type = INPUT_MOUSE;
                        click[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE |
                                              MOUSEEVENTF_MOVE |
                                              MOUSEEVENTF_VIRTUALDESK;
                        click[0].mi.dx = static_cast<LONG>(
                            (static_cast<LONG>(centre.x) * 65535LL) /
                            (GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1));
                        click[0].mi.dy = static_cast<LONG>(
                            (static_cast<LONG>(centre.y) * 65535LL) /
                            (GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1));
                        click[1] = click[0];
                        click[1].mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
                        click[2] = click[0];
                        click[2].mi.dwFlags |= MOUSEEVENTF_LEFTUP;
                    };

                    if (!g_clickThroughSaved.empty()) {
                        for (const auto& pw : g_clickThroughSaved) {
                            SetWindowLongPtrW(
                                pw.hwnd, GWL_EXSTYLE,
                                pw.exStyle | WS_EX_TRANSPARENT |
                                    WS_EX_LAYERED);
                            /* Layered senza attributi non verrebbe dipinta:
                             * alpha piena la tiene identica a prima. */
                            SetLayeredWindowAttributes(pw.hwnd, 0, 255,
                                                       LWA_ALPHA);
                        }
                        HWND hit = WindowFromPoint(centre);
                        DWORD hitPid = 0;
                        if (hit != nullptr) {
                            GetWindowThreadProcessId(hit, &hitPid);
                        }
                        if (hitPid != 0 &&
                            hitPid != GetCurrentProcessId()) {
                            INPUT click[3] = {};
                            makeClick(click);
                            GetCursorPos(&g_clickThroughCursor);
                            g_clickThroughActive = true;
                            if (SendInput(3, click, sizeof(INPUT)) == 3) {
                                done = true;
                                SetTimer(nullptr, kTimerClickThrough, 140,
                                         nullptr);
                                AppendCoreLog(L"tray Win11: clic consegnato "
                                              L"attraverso la nostra barra "
                                              L"(pattern muto)");
                            } else {
                                AppendCoreLog(L"tray Win11: SendInput del "
                                              L"clic passante fallito");
                            }
                        } else {
                            AppendCoreLog(L"tray Win11: anche trasparenti il "
                                          L"punto resta nostro, clic salto");
                        }
                        if (!done) {
                            /* Niente timer: ripristino immediato. */
                            for (const auto& pw : g_clickThroughSaved) {
                                SetWindowLongPtrW(pw.hwnd, GWL_EXSTYLE,
                                                  pw.exStyle);
                            }
                            g_clickThroughSaved.clear();
                            g_clickThroughActive = false;
                        }
                    } else {
                        INPUT click[3] = {};
                        makeClick(click);
                        done = SendInput(3, click, sizeof(INPUT)) == 3;
                        if (done) {
                            AppendCoreLog(L"tray Win11: clic consegnato per "
                                          L"coordinate (pattern muto)");
                        }
                    }
                }
            }
        }
        if (!done) {
            AppendCoreLog(L"tray Win11: clic non consegnato dall'elemento");
        }
        return done;
    };

    auto stopFlyoutWatch = [&]() {
        g_watchFlyout.store(false);
        g_flyoutSilent.store(false);
        flyoutHook.reset();
        KillTimer(nullptr, kTimerPlacement);
        KillTimer(nullptr, kTimerSilentHarvest);
        g_flyoutHwnd = nullptr;
    };

    /* --- worker loop ----------------------------------------------- */

    MSG msg{};
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kMsgRead) {
            safeReadNow();
            continue;
        }
        if (msg.message == kMsgClick) {
            std::unique_ptr<Request> request(
                reinterpret_cast<Request*>(msg.lParam));
            if (request != nullptr) {
                bool delivered = false;
                auto it = elements.find(request->uid);
                if (it != elements.end() && it->second) {
                    delivered = callPattern(it->second.Get(), request->rightButton);
                }
                /* v2.62 - IL PUNTATORE PUO' ESSERE MORTO.
                 *
                 * Dopo un riavvio di Explorer, o quando la shell ricrea
                 * l'isola della tray, gli elementi raccolti prima non
                 * esistono piu': la chiave dell'icona e' ancora valida ma il
                 * clic non arriva a nessuno. Si rilegge e si ritenta UNA
                 * volta (la rilettura rilascia i puntatori vecchi, quindi
                 * dopo non si usa piu' quello di prima). */
                if (!delivered) {
                    safeReadNow();
                    auto again = elements.find(request->uid);
                    if (again != elements.end() && again->second) {
                        delivered = callPattern(again->second.Get(),
                                                request->rightButton);
                    }
                }
                if (!delivered) {
                    AppendCoreLog(L"tray Win11: clic non consegnato");
                }
            }
            continue;
        }
        if (msg.message == kMsgOverflow) {
            std::unique_ptr<Request> request(
                reinterpret_cast<Request*>(msg.lParam));
            if (request != nullptr) {
                if (!chevron) {
                    safeReadNow();
                }
                if (chevron) {
                    g_flyoutAnchor = request->anchor;
                    g_flyoutHwnd = nullptr;
                    g_flyoutSilent.store(request->silent);
                    g_watchFlyout.store(true);
                    if (!flyoutHook.valid()) {
                        flyoutHook.reset(SetWinEventHook(
                            EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                            OverflowShownProc, 0, 0, WINEVENT_OUTOFCONTEXT));
                    }
                    SetTimer(nullptr, kTimerPlacement, 1500, nullptr);
                    if (request->silent) {
                        /* L'isola XAML può essere materializzata alcuni
                         * messaggi dopo EVENT_OBJECT_SHOW. Questo timer è un
                         * semplice fallback sullo stesso thread: non invoca
                         * una seconda volta il chevron. */
                        SetTimer(nullptr, kTimerSilentHarvest, 220, nullptr);
                    }
                    callPattern(chevron.Get(), false, request->silent);
                } else {
                    g_watchFlyout.store(false);
                    g_flyoutSilent.store(false);
                    flyoutHook.reset();
                    KillTimer(nullptr, kTimerPlacement);
                    KillTimer(nullptr, kTimerSilentHarvest);
                    AppendCoreLog(L"tray Win11: freccetta overflow non trovata");
                }
            }
            continue;
        }
        if (msg.message == kMsgPlaceFlyout) {
            HWND flyout = g_flyoutHwnd;
            if (g_flyoutSilent.load() && g_watchFlyout.load()) {
                /* Il callback SHOW è solo un suggerimento: la lista Win32
                 * viene interrogata di nuovo qui, dopo la materializzazione
                 * XAML. In questo modo readNow() vede l'isola vera invece di
                 * consumare uno snapshot della barra principale. */
                std::vector<HWND> islands = FindOverflowIslands();
                if (flyout != nullptr && IsWindow(flyout) &&
                    std::find(islands.begin(), islands.end(), flyout) ==
                        islands.end()) {
                    islands.push_back(flyout);
                }
                if (!islands.empty()) {
                bool visibleIsland = false;
                for (HWND island : islands) {
                    if (IsWindow(island) && IsWindowVisible(island)) {
                        visibleIsland = true;
                        /* Il controllo overflow è stato aperto dal pattern
                         * UIA reale. Lo si sposta solo dopo l'evento SHOW,
                         * senza SWP_SHOWWINDOW: non si forza la visibilità
                         * interna di Explorer e non si crea un falso pannello. */
                        SetWindowPos(island, nullptr, -32000, -32000, 0, 0,
                                     SWP_NOSIZE | SWP_NOZORDER |
                                         SWP_NOACTIVATE);
                    }
                }
                if (!visibleIsland) {
                    continue;
                }
                safeReadNow();
                    /* Il flyout è una finestra della shell: non la
                     * distruggiamo. ESC è il percorso pubblico equivalente
                     * alla chiusura dell'utente; il messaggio viene inviato
                     * senza SetForegroundWindow. */
                    for (HWND island : islands) {
                        if (IsWindow(island)) {
                            PostMessageW(island, WM_KEYDOWN, VK_ESCAPE, 0);
                            PostMessageW(island, WM_KEYUP, VK_ESCAPE,
                                         0xC0000001u);
                        }
                    }
                }
            } else if (flyout != nullptr && IsWindow(flyout) &&
                       g_watchFlyout.load()) {
                RECT wr{};
                if (GetWindowRect(flyout, &wr)) {
                    const int w = wr.right - wr.left;
                    const int h = wr.bottom - wr.top;
                    const int cx = (g_flyoutAnchor.left + g_flyoutAnchor.right) / 2;
                    int x = cx - w / 2;
                    int y = g_flyoutAnchor.top - h - 6;
                    /* Keep the flyout on the monitor that hosts the bar. */
                    HMONITOR mon = MonitorFromRect(&g_flyoutAnchor,
                                                   MONITOR_DEFAULTTONEAREST);
                    MONITORINFO mi{};
                    mi.cbSize = sizeof(mi);
                    if (GetMonitorInfoW(mon, &mi)) {
                        if (x < mi.rcWork.left) x = mi.rcWork.left;
                        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
                        if (y < mi.rcWork.top) y = mi.rcWork.top;
                    }
                    SetWindowPos(flyout, HWND_TOPMOST, x, y, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            stopFlyoutWatch();
            continue;
        }
        if (msg.message == WM_TIMER && msg.wParam == kTimerSilentHarvest) {
            if (!g_watchFlyout.load() || !g_flyoutSilent.load()) {
                KillTimer(nullptr, kTimerSilentHarvest);
                continue;
            }
            /* EVENT_OBJECT_SHOW non è garantito per tutte le build della
             * shell. Il fallback esegue lo stesso percorso silenzioso non
             * appena FindOverflowIslands() può osservare l'isola. */
            const std::vector<HWND> islands = FindOverflowIslands();
            if (islands.empty()) {
                continue;
            }
            bool visibleIsland = false;
            for (HWND island : islands) {
                if (IsWindow(island) && IsWindowVisible(island)) {
                    visibleIsland = true;
                    SetWindowPos(island, nullptr, -32000, -32000, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            if (!visibleIsland) {
                continue;
            }
            safeReadNow();
            for (HWND island : islands) {
                if (IsWindow(island)) {
                    PostMessageW(island, WM_KEYDOWN, VK_ESCAPE, 0);
                    PostMessageW(island, WM_KEYUP, VK_ESCAPE, 0xC0000001u);
                }
            }
            stopFlyoutWatch();
            continue;
        }
        if (msg.message == WM_TIMER && msg.wParam == kTimerPlacement) {
            stopFlyoutWatch();
            continue;
        }
        if (msg.message == WM_TIMER && msg.wParam == kTimerClickThrough) {
            /* v1.5: la finestra di trasparenza e' finita: stili e cursore
             * tornano esattamente com'erano. */
            KillTimer(nullptr, kTimerClickThrough);
            if (g_clickThroughActive) {
                INPUT back[1] = {};
                back[0].type = INPUT_MOUSE;
                back[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE |
                                     MOUSEEVENTF_MOVE |
                                     MOUSEEVENTF_VIRTUALDESK;
                back[0].mi.dx = static_cast<LONG>(
                    (static_cast<LONG>(g_clickThroughCursor.x) * 65535LL) /
                    (GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1));
                back[0].mi.dy = static_cast<LONG>(
                    (static_cast<LONG>(g_clickThroughCursor.y) * 65535LL) /
                    (GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1));
                SendInput(1, back, sizeof(INPUT));
                for (const auto& pw : g_clickThroughSaved) {
                    if (IsWindow(pw.hwnd)) {
                        SetWindowLongPtrW(pw.hwnd, GWL_EXSTYLE, pw.exStyle);
                    }
                }
                g_clickThroughSaved.clear();
                g_clickThroughActive = false;
            }
            continue;
        }
        if (msg.message == WM_QUIT) {
            break;
        }
    }

    /* Se Stop() mette m_running a false mentre ci sono richieste accodate,
     * la condizione del while puo' uscire prima di consumarle. Le due code
     * contengono puntatori di proprieta' nostra: li si svuota esplicitamente
     * prima di distruggere il worker. */
    MSG pending{};
    while (PeekMessageW(&pending, nullptr, kMsgClick, kMsgClick,
                        PM_REMOVE) != FALSE) {
        delete reinterpret_cast<Request*>(pending.lParam);
    }
    while (PeekMessageW(&pending, nullptr, kMsgOverflow, kMsgOverflow,
                        PM_REMOVE) != FALSE) {
        delete reinterpret_cast<Request*>(pending.lParam);
    }

    stopFlyoutWatch();
    releaseElements();
    clearWatchedRoots();
    /* I ComPtr rilasciano UIA, gli handler e gli elementi anche se una
     * chiamata precedente ha lasciato una risorsa a meta'. */
        m_running.store(false);
        m_threadId = 0;
        m_started.store(false);
        m_threadDone.store(true);
    } catch (...) {
        MSG pending{};
        while (PeekMessageW(&pending, nullptr, kMsgClick, kMsgClick,
                            PM_REMOVE) != FALSE) {
            delete reinterpret_cast<Request*>(pending.lParam);
        }
        while (PeekMessageW(&pending, nullptr, kMsgOverflow, kMsgOverflow,
                            PM_REMOVE) != FALSE) {
            delete reinterpret_cast<Request*>(pending.lParam);
        }
        m_running.store(false);
        m_threadId = 0;
        m_started.store(false);
        m_threadDone.store(true);
        OutputDebugStringW(L"Win11TrayReader: eccezione non gestita nel worker UIA\n");
    }
}

} /* namespace w7t */
