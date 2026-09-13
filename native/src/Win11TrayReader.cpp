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
 * See Win11TrayReader.h for why this exists. In short: Windows 11 has no
 * Win32 notification toolbar, so the tray is read through UI Automation and
 * clicked through the same accessibility patterns, without synthetic input
 * and without touching the registry.
 */

#include "Win11TrayReader.h"

#include "SehGuard.h"
#include "Strings.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <map>
#include <new>
#include <set>

#include <objbase.h>
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

constexpr UINT kMsgRead        = WM_APP + 1;
constexpr UINT kMsgClick       = WM_APP + 2;
constexpr UINT kMsgOverflow    = WM_APP + 3;
constexpr UINT kMsgPlaceFlyout = WM_APP + 4;
constexpr UINT kTimerPlacement = 0x51;   /* thread timer, HWND == nullptr */

/* Overflow flyout placement: the hook callback runs on the worker thread
 * and only records what it saw; the loop does the window work. */
std::atomic<bool> g_watchFlyout{ false };
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
    PostThreadMessageW(GetCurrentThreadId(), kMsgPlaceFlyout, 0, 0);
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

/* Explorer's Shell_TrayWnd that is NOT owned by us: the tray service
 * registers a window with the same class name. */
HWND FindShellTaskbar() {
    const DWORD ourPid = GetCurrentProcessId();
    HWND candidate = nullptr;
    while ((candidate = FindWindowExW(nullptr, candidate, L"Shell_TrayWnd",
                                      nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(candidate, &pid);
        if (pid != 0 && pid != ourPid) {
            return candidate;
        }
    }
    return nullptr;
}

/* The Windows 11 overflow flyout lives in its own top-level island. */
HWND FindOverflowIsland() {
    return FindWindowExW(nullptr, nullptr,
                         L"TopLevelWindowForOverflowXamlIsland", nullptr);
}

/* The XAML bridge is the evidence that the taskbar content is XAML: it is
 * the child Explorer creates to host the island. */
bool HasXamlBridge(HWND taskbar) {
    if (taskbar == nullptr) {
        return false;
    }
    HWND child = nullptr;
    while ((child = FindWindowExW(taskbar, child, nullptr, nullptr)) != nullptr) {
        wchar_t cls[128] = {};
        if (GetClassNameW(child, cls, 128) > 0 &&
            wcsstr(cls, L"DesktopWindowContentBridge") != nullptr) {
            return true;
        }
    }
    return false;
}

/* The classic Win32 notification toolbar: present on 7/8/10, absent on 11. */
bool HasClassicTrayToolbar() {
    HWND tray = FindShellTaskbar();
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

std::wstring ExePathOf(uint32_t pid) {
    if (pid == 0) {
        return std::wstring();
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) {
        return std::wstring();
    }
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    std::wstring result;
    if (QueryFullProcessImageNameW(proc, 0, path, &length) != FALSE && length > 0) {
        result.assign(path, length);
    }
    CloseHandle(proc);
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
        if (Contains(std::wstring(cls), L"Tray")) {
            search->trayWindow = icon;
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
     * compila lo stesso e il bug si vede solo in CI. */
    HICON iconSmall = nullptr;
    if (!exePath.empty()) {
        W7T_SEH_TRY {
            if (ExtractIconExW(exePath.c_str(), 0, nullptr, &iconSmall, 1) > 0 &&
                iconSmall != nullptr) {
                ArgbBitmap bmp;
                if (IconToArgb(iconSmall, bmp) && BitmapSane(bmp)) {
                    out = std::move(bmp);
                }
            }
        } W7T_SEH_CATCH {
            out.clear();
        } W7T_SEH_END
    }
    if (iconSmall != nullptr) {
        DestroyIcon(iconSmall);
    }
    if (!out.empty()) {
        return true;
    }

    /* Last resort: the generic application icon. Never leave a tray slot
     * empty: an icon with a tooltip is always better than a hole. */
    HICON generic = LoadIconW(nullptr, IDI_APPLICATION);
    if (generic == nullptr) {
        return false;
    }
    bool ok = false;
    W7T_SEH_TRY {
        ArgbBitmap bmp;
        if (IconToArgb(generic, bmp) && BitmapSane(bmp)) {
            out = std::move(bmp);
            ok = true;
        }
    } W7T_SEH_CATCH {
        ok = false;
    } W7T_SEH_END
    return ok;
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

/* ------------------------------------------------------------------ */
/*  Requests between the caller and the worker                         */
/* ------------------------------------------------------------------ */

struct Win11TrayReader::Request {
    uint32_t uid         = 0;
    bool     rightButton = false;
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
    /* Esito positivo: definitivo, la forma della barra non cambia piu'.
     * Esito negativo: si rivaluta, ma non piu' di una volta ogni 30 s
     * (all'avvio l'isola XAML puo' non essere ancora creata, e un "no"
     * congelato lascerebbe la tray vuota per sempre). */
    static std::atomic<int> cached{ 0 };   /* 0 = ignoto, 1 = si', 2 = no */
    static std::atomic<ULONGLONG> lastCheck{ 0 };

    if (cached.load() == 1) {
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (cached.load() == 2 && now - lastCheck.load() < 30000) {
        return false;
    }
    lastCheck.store(now);

    HWND taskbar = FindShellTaskbar();
    const bool win11 = taskbar != nullptr && !HasClassicTrayToolbar() &&
                       HasXamlBridge(taskbar);
    cached.store(win11 ? 1 : 2);
    return win11;
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
    m_running.store(true);
    m_thread = std::thread(&Win11TrayReader::WorkerMain, this);
    m_started.store(true);

    /* The thread id is needed to post requests: wait for the worker to
     * publish it (a few milliseconds, the loop starts right away). */
    for (int i = 0; i < 200 && m_threadId == 0; ++i) {
        Sleep(5);
    }
    if (m_threadId != 0) {
        /* v2.62: prima lettura SUBITO. La tray deve riempirsi all'avvio, non
         * al primo evento della shell: aspettare l'hook o il risveglio di
         * sicurezza significava icona dopo icona con secondi di ritardo. */
        PostThreadMessageW(m_threadId, kMsgRead, 0, 0);
    }
    return m_threadId != 0;
}

void Win11TrayReader::Stop() {
    if (!m_started.load()) {
        return;
    }
    m_running.store(false);
    if (m_threadId != 0) {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
    }
    if (m_thread.joinable()) {
        /* Chiusura con limite: una chiamata UI Automation bloccata (shell
         * appesa) non deve impedire l'uscita del processo. Oltre il budget
         * il thread si stacca e lo porta via ExitProcess. */
        constexpr ULONGLONG kJoinBudgetMs = 2000;
        const ULONGLONG t0 = GetTickCount64();
        while (GetTickCount64() - t0 < kJoinBudgetMs && !m_threadDone.load()) {
            Sleep(20);
        }
        if (m_threadDone.load()) {
            m_thread.join();
        } else {
            m_thread.detach();
        }
    }
    m_threadId = 0;
    m_started.store(false);
}

void Win11TrayReader::RequestRead() {
    if (m_threadId != 0) {
        PostThreadMessageW(m_threadId, kMsgRead, 0, 0);
    }
}

bool Win11TrayReader::RequestClick(uint32_t uid, bool rightButton) {
    if (m_threadId == 0 || uid == 0) {
        return false;
    }
    Request* request = new (std::nothrow) Request();
    if (request == nullptr) {
        return false;
    }
    request->uid = uid;
    request->rightButton = rightButton;
    if (!PostThreadMessageW(m_threadId, kMsgClick, 0,
                            reinterpret_cast<LPARAM>(request))) {
        delete request;
        return false;
    }
    return true;
}

bool Win11TrayReader::RequestOverflowFlyout(const RECT& anchor) {
    if (m_threadId == 0) {
        return false;
    }
    Request* request = new (std::nothrow) Request();
    if (request == nullptr) {
        return false;
    }
    request->anchor = anchor;
    if (!PostThreadMessageW(m_threadId, kMsgOverflow, 0,
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
    m_threadId = GetCurrentThreadId();
    m_threadDone.store(false);

    /* UIA is used from a worker: MTA is the recommended apartment for a
     * client that does not own a window. */
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IUIAutomation* uia = nullptr;
    if (SUCCEEDED(CoCreateInstance(kClsidCUIAutomation, nullptr,
                                   CLSCTX_INPROC_SERVER, kIidIUIAutomation,
                                   reinterpret_cast<void**>(&uia))) &&
        uia != nullptr) {
        AppendCoreLog(L"tray Win11: lettura via UI Automation attiva");
    } else {
        AppendCoreLog(L"tray Win11: UI Automation non disponibile");
        if (uia != nullptr) {
            uia->Release();
            uia = nullptr;
        }
    }

    std::map<uint32_t, IUIAutomationElement*> elements;
    IUIAutomationElement* chevron = nullptr;
    HWINEVENTHOOK flyoutHook = nullptr;

    /* v2.62: ascolto dei cambi di proprieta' dell'isola della tray. */
    TrayPropertyChangeHandler* propertyHandler = nullptr;
    IUIAutomationElement* watchedRoot = nullptr;
    HWND watchedIsland = nullptr;

    auto watchIslandProperties = [&](HWND island) {
        if (uia == nullptr || island == nullptr || island == watchedIsland) {
            return;
        }
        if (watchedRoot != nullptr && propertyHandler != nullptr) {
            uia->RemovePropertyChangedEventHandler(watchedRoot, propertyHandler);
            watchedRoot->Release();
            watchedRoot = nullptr;
            watchedIsland = nullptr;
        }
        if (propertyHandler == nullptr) {
            propertyHandler = new (std::nothrow) TrayPropertyChangeHandler();
            if (propertyHandler == nullptr) {
                return;
            }
            propertyHandler->AttachTo(m_threadId);
        }
        IUIAutomationElement* root = nullptr;
        if (FAILED(uia->ElementFromHandle(island, &root)) || root == nullptr) {
            return;
        }
        /* Nome e stato attivo: sono le due proprieta' che cambiano quando
         * un'icona cambia stato (percentuale, rete, notifiche). */
        PROPERTYID properties[] = { UIA_NamePropertyId, UIA_IsEnabledPropertyId };
        if (SUCCEEDED(uia->AddPropertyChangedEventHandlerNativeArray(
                root, TreeScope_Subtree, nullptr, propertyHandler,
                properties, 2))) {
            watchedRoot = root;     /* tenuto per la disiscrizione */
            watchedIsland = island;
        } else {
            root->Release();
        }
    };

    auto releaseElements = [&]() {
        for (auto& pair : elements) {
            if (pair.second != nullptr) {
                pair.second->Release();
            }
        }
        elements.clear();
        if (chevron != nullptr) {
            chevron->Release();
            chevron = nullptr;
        }
    };

    auto postReady = [&]() {
        HWND target = nullptr;
        UINT message = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            target = m_notifyWnd;
            message = m_notifyMsg;
        }
        if (target != nullptr && IsWindow(target)) {
            PostMessageW(target, message, 0, 0);
        }
    };

    /* --- collection of the tray elements --------------------------- */

    auto isTrayIconClass = [](const std::wstring& cls) {
        return cls.find(L"SystemTray") != std::wstring::npos;
    };
    auto isChevron = [](const std::wstring& cls, const std::wstring& id) {
        return cls.find(L"Chevron") != std::wstring::npos ||
               id.find(L"Chevron") != std::wstring::npos ||
               id.find(L"Overflow") != std::wstring::npos;
    };

    auto collectIsland = [&](HWND island, bool hidden, int& order,
                             std::set<uint32_t>& usedUids,
                             std::vector<Win11TrayItem>& out) {
        if (uia == nullptr || island == nullptr) {
            return;
        }
        IUIAutomationElement* root = nullptr;
        if (FAILED(uia->ElementFromHandle(island, &root)) || root == nullptr) {
            return;
        }
        IUIAutomationCondition* all = nullptr;
        if (FAILED(uia->CreateTrueCondition(&all)) || all == nullptr) {
            root->Release();
            return;
        }
        IUIAutomationElementArray* found = nullptr;
        const HRESULT hr = root->FindAll(TreeScope_Descendants, all, &found);
        all->Release();
        root->Release();
        if (FAILED(hr) || found == nullptr) {
            return;
        }

        int length = 0;
        if (SUCCEEDED(found->get_Length(&length))) {
            for (int i = 0; i < length; ++i) {
                IUIAutomationElement* element = nullptr;
                if (FAILED(found->GetElement(i, &element)) || element == nullptr) {
                    continue;
                }

                Bstr name, cls, automationId;
                CONTROLTYPEID controlType = 0;
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

                if (isChevron(className, id)) {
                    if (chevron == nullptr) {
                        chevron = element;      /* keep the reference */
                    } else {
                        element->Release();
                    }
                    continue;
                }

                const bool isButton = controlType == UIA_ButtonControlTypeId ||
                                      controlType == UIA_ListItemControlTypeId;
                if (!isButton || text.empty() || offscreen != FALSE ||
                    !isTrayIconClass(className)) {
                    element->Release();
                    continue;
                }

                Win11TrayItem item;
                item.hidden = hidden;
                item.order = order++;
                item.pid = static_cast<uint32_t>(pid);
                item.name = text;
                item.exePath = ExePathOf(item.pid);
                item.systemOwned = IsShellProcess(ExeNameOf(item.exePath));

                if (item.systemOwned) {
                    item.kind = ClassifySystemIcon(text);
                    if (item.kind == SystemIconKind::None) {
                        /* Bell, location, Copilot...: not part of the
                         * Windows 7 tray, so not part of ours. */
                        element->Release();
                        continue;
                    }
                    if (!TrayFallbackIcons::Render(item.kind, item.bitmap)) {
                        element->Release();
                        continue;
                    }
                } else if (!LoadProcessWindowIcon(item.pid, item.bitmap)) {
                    /* Nessuna finestra del processo espone un'icona: si
                     * ripiega su quella dell'eseguibile (per la maggior
                     * parte delle applicazioni sono la stessa immagine) e
                     * solo se anche quella manca sull'icona generica. */
                    LoadImageIcon(item.exePath, item.bitmap);
                }

                /* Identita' STABILE: pid + nome accessibile. L'ordine di
                 * enumerazione NON entra nel token, altrimenti spostare
                 * un'icona fra barra e overflow (che cambia l'ordine)
                 * cambierebbe la chiave, e per il modello sarebbe
                 * un'icona sparita piu' una nuova: doppioni e salti.
                 * Due icone identiche nello stesso processo (caso raro)
                 * si distinguono con un suffisso assegnato in ordine di
                 * lettura. */
                /* Il processo cambia a ogni avvio: nell'identita' entra il
                 * NOME dell'eseguibile, cosi' la chiave (e quindi la
                 * preferenza dell'utente) sopravvive al riavvio. */
                std::wstring owner = ExeNameOf(item.exePath);
                if (owner.empty()) {
                    owner = std::to_wstring(item.pid);
                }
                /* v2.62 - IDENTITA' STABILE, SENZA IL TOOLTIP.
                 *
                 * Il nome accessibile di un'icona e' un bersaglio mobile:
                 * "Batteria 87%" diventa 86% dopo un minuto, un client di
                 * posta ci mette il numero di messaggi, un download manager
                 * la velocita'. Con il nome dentro la chiave ogni lettura
                 * creava un'icona NUOVA e ne faceva sparire un'altra:
                 * l'utente vedeva l'icona lampeggiare, la posizione e la
                 * preferenza pin/nascondi andavano perse e il modello si
                 * riempiva di doppioni.
                 *
                 * La chiave e' quindi il processo (piu' il tipo, per le
                 * icone di sistema che la shell espone): resta la stessa
                 * finche' l'icona esiste. Il nome continua ad arrivare al
                 * modello e ad aggiornare il tooltip (entry.tooltip), che e'
                 * esattamente dove deve stare. */
                std::wstring token;
                if (item.systemOwned) {
                    token = L"uia:system|";
                    token += (item.kind == SystemIconKind::Volume) ? L"volume"
                           : (item.kind == SystemIconKind::Network) ? L"network"
                                                                    : L"battery";
                } else {
                    token = L"uia:" + owner;
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
                elements[out.back().uid] = element;
            }
        }
        found->Release();
    };

    auto readNow = [&]() {
        releaseElements();
        std::vector<Win11TrayItem> items;
        std::set<uint32_t> usedUids;
        int order = 0;
        HWND taskbar = FindShellTaskbar();
        HWND overflow = FindOverflowIsland();

        if (uia == nullptr || (taskbar == nullptr && overflow == nullptr)) {
            /* No island at all: the shell is restarting or not ready. Keep
             * the last good snapshot, a failed read never clears icons.
             * v2.61: la lettura si dichiara NON valida, cosi' il chiamante
             * ritenta in backoff invece di considerare la tray vuota. */
            m_lastReadValid.store(false);
            return;
        }

        collectIsland(taskbar, false, order, usedUids, items);
        collectIsland(overflow, true, order, usedUids, items);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snapshot = items;
        }
        /* L'isola c'era ed e' stata attraversata: la lettura e' valida anche
         * se non ha trovato nulla (tutte le icone nascoste dall'utente). */
        m_lastReadValid.store(true);

        /* Da adesso i cambi di stato della shell arrivano da soli. */
        watchIslandProperties(taskbar != nullptr ? taskbar : overflow);
        wchar_t line[160] = {};
        swprintf(line, 160, L"tray Win11: %u icone (UI Automation)",
                 static_cast<unsigned>(items.size()));
        AppendCoreLog(line);
        postReady();
    };

    /* --- clicks ---------------------------------------------------- */

    auto callPattern = [&](IUIAutomationElement* element, bool rightButton) {
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
            IUIAutomationInvokePattern* invoke = nullptr;
            if (SUCCEEDED(element->GetCurrentPatternAs(
                    UIA_InvokePatternId, kIidInvokePattern,
                    reinterpret_cast<void**>(&invoke))) &&
                invoke != nullptr) {
                done = SUCCEEDED(invoke->Invoke());
                invoke->Release();
            }
            if (!done) {
                IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
                if (SUCCEEDED(element->GetCurrentPatternAs(
                        UIA_LegacyIAccessiblePatternId, kIidLegacyPattern,
                        reinterpret_cast<void**>(&legacy))) &&
                    legacy != nullptr) {
                    done = SUCCEEDED(legacy->DoDefaultAction());
                    legacy->Release();
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
        if (flyoutHook != nullptr) {
            UnhookWinEvent(flyoutHook);
            flyoutHook = nullptr;
        }
        KillTimer(nullptr, kTimerPlacement);
        g_flyoutHwnd = nullptr;
    };

    /* --- worker loop ----------------------------------------------- */

    MSG msg{};
    while (m_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kMsgRead) {
            readNow();
            continue;
        }
        if (msg.message == kMsgClick) {
            Request* request = reinterpret_cast<Request*>(msg.lParam);
            if (request != nullptr) {
                bool delivered = false;
                auto it = elements.find(request->uid);
                if (it != elements.end()) {
                    delivered = callPattern(it->second, request->rightButton);
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
                    readNow();
                    auto again = elements.find(request->uid);
                    if (again != elements.end()) {
                        delivered = callPattern(again->second,
                                                request->rightButton);
                    }
                }
                if (!delivered) {
                    AppendCoreLog(L"tray Win11: clic non consegnato");
                }
                delete request;
            }
            continue;
        }
        if (msg.message == kMsgOverflow) {
            Request* request = reinterpret_cast<Request*>(msg.lParam);
            if (request != nullptr) {
                if (chevron == nullptr) {
                    readNow();
                }
                if (chevron != nullptr) {
                    g_flyoutAnchor = request->anchor;
                    g_flyoutHwnd = nullptr;
                    g_watchFlyout.store(true);
                    if (flyoutHook == nullptr) {
                        flyoutHook = SetWinEventHook(
                            EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                            OverflowShownProc, 0, 0, WINEVENT_OUTOFCONTEXT);
                    }
                    SetTimer(nullptr, kTimerPlacement, 1500, nullptr);
                    callPattern(chevron, false);
                } else {
                    AppendCoreLog(L"tray Win11: freccetta overflow non trovata");
                }
                delete request;
            }
            continue;
        }
        if (msg.message == kMsgPlaceFlyout) {
            HWND flyout = g_flyoutHwnd;
            if (flyout != nullptr && IsWindow(flyout) && g_watchFlyout.load()) {
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
        if (msg.message == WM_TIMER && msg.wParam == kTimerPlacement) {
            stopFlyoutWatch();
            continue;
        }
        if (msg.message == WM_QUIT) {
            break;
        }
    }

    stopFlyoutWatch();
    releaseElements();
    if (watchedRoot != nullptr && propertyHandler != nullptr && uia != nullptr) {
        uia->RemovePropertyChangedEventHandler(watchedRoot, propertyHandler);
        watchedRoot->Release();
        watchedRoot = nullptr;
    }
    if (propertyHandler != nullptr) {
        propertyHandler->Release();
        propertyHandler = nullptr;
    }
    if (uia != nullptr) {
        uia->Release();
    }
    if (SUCCEEDED(comHr)) {
        CoUninitialize();
    }
    m_threadId = 0;
    m_threadDone.store(true);
}

} /* namespace w7t */
