// Win7Taskbar - Windows 7 style Jump List for taskbar buttons
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Only public, documented Shell APIs: IApplicationDocumentLists for the
// application's real Recent/Frequent destinations, the window/shortcut
// property stores for the AppUserModelID, IShellLink for the taskbar pin,
// and the shared DWM flyout border. Every COM/Shell call sits behind an
// explicit failure path that logs and degrades to "no jump list"; hard
// faults (dead network paths, malformed destination items) stay inside the
// portable SEH barrier the core uses elsewhere. Resources with ownership
// semantics (HBITMAP, HICON, COM interfaces, PROPVARIANTs) are owned by
// RAII guards, so no early return or fault can leak them.
//
// Coordinate contract with the managed side: every RECT and POINT crossing
// Open/SetHover/ActivateRow is a SCREEN PHYSICAL PIXEL value (the space of
// SetWindowPos/GetCursorPos and of WPF PointToScreen on a per-monitor-DPI
// process). Geometry constants are 96-DPI reference values scaled by the
// DPI of the monitor that owns the taskbar button - measured against the
// Windows 7 jump list screenshots: 300 px popup width, 16 px document
// icons, 32 px application icon, 19 px section header band; the row heights
// (28/48/34) and the menu palette carry over the v2.40 tuning of this
// popup. All offsets go through Sc(): no unscaled magic numbers.

#include "JumpListWindow.h"
#include "FlyoutLauncher.h"
#include "SehGuard.h"
#include "ScopeGuards.h"
#include "Common.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <dwmapi.h>
#include <cstring>
#include <algorithm>

namespace w7t {

namespace {

constexpr wchar_t kClassName[] = L"W7T_JumpList";

/* --- 96-DPI reference geometry (scaled by JumpListWindow::Sc) --------- */
constexpr int kWidth96      = 300;
constexpr int kRowApp96     = 48;
constexpr int kRowPin96     = 34;
constexpr int kRowDoc96     = 28;
constexpr int kHeader96     = 19;
constexpr int kPad96        = 8;   /* top/bottom inner padding           */
constexpr int kSep96        = 6;   /* separator band between sections     */
constexpr int kGap96        = 4;   /* popup-to-button gap (Windows 7)     */
constexpr int kEdgeMargin96 = 2;   /* never closer to the work area edge  */
constexpr int kDocIcon96    = 16;
constexpr int kAppIcon96    = 32;
constexpr int kMaxDocsPerSection = 10; /* the taskbar list caps at ten    */

/* RAII for COM interfaces: Release() on every path - early returns, C++
 * exceptions and SEH-fault unwinds alike (same principle as IconHandle and
 * BitmapHandle in RaiiWrappers.h; kept local because the project has no
 * shared COM wrapper today). */
template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    explicit ComPtr(T* ptr) : p(ptr) {}
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; }
        return *this;
    }
    T* operator->() const { return p; }
    T** operator&() { return &p; }
    T* get() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

/* Popup strings. Wording follows the Windows 7 shell ("Recent items",
 * "Frequent items" and the taskbar pin commands). Language indices: the
 * single project language list (Strings.cpp): 0 it, 1 en, 2 es, 3 fr,
 * 4 de, 5 pt, 6 pl, 7 ru, 8 ja, 9 zh, 10 ar. */
struct JumpStr {
    const wchar_t* recent;
    const wchar_t* frequent;
    const wchar_t* pin;
    const wchar_t* unpin;
};
const JumpStr& Str(int lang) {
    static const JumpStr kIt = {
        L"Voci usate di recente", L"Voci usate di frequente",
        L"Fissa questo programma alla barra delle applicazioni",
        L"Rimuovi questo programma dalla barra delle applicazioni" };
    static const JumpStr kEn = {
        L"Recent items", L"Frequent items",
        L"Pin this program to the taskbar",
        L"Unpin this program from the taskbar" };
    static const JumpStr kEs = {
        L"Elementos recientes", L"Elementos frecuentes",
        L"Anclar este programa a la barra de tareas",
        L"Desanclar este programa de la barra de tareas" };
    static const JumpStr kFr = {
        L"\u00c9l\u00e9ments r\u00e9cents", L"\u00c9l\u00e9ments fr\u00e9quents",
        L"\u00c9pingler ce programme \u00e0 la barre des t\u00e2ches",
        L"D\u00e9tacher ce programme de la barre des t\u00e2ches" };
    static const JumpStr kDe = {
        L"Zuletzt verwendete Elemente", L"H\u00e4ufig verwendete Elemente",
        L"Dieses Programm an die Taskleiste anheften",
        L"Dieses Programm von der Taskleiste l\u00f6sen" };
    static const JumpStr kPt = {
        L"Itens recentes", L"Itens frequentes",
        L"Fixar este programa na barra de tarefas",
        L"Desafixar este programa da barra de tarefas" };
    static const JumpStr kPl = {
        L"Ostatnie elementy", L"Cz\u0119ste elementy",
        L"Przypnij ten program do paska zada\u0144",
        L"Odepnij ten program od paska zada\u0144" };
    static const JumpStr kRu = {
        L"\u041d\u0435\u0434\u0430\u0432\u043d\u0438\u0435 \u044d\u043b\u0435"
        L"\u043c\u0435\u043d\u0442\u044b",
        L"\u0427\u0430\u0441\u0442\u044b\u0435 \u044d\u043b\u0435\u043c"
        L"\u0435\u043d\u0442\u044b",
        L"\u0417\u0430\u043a\u0440\u0435\u043f\u0438\u0442\u044c \u044d"
        L"\u0442\u0443 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c"
        L"\u0443 \u043d\u0430 \u043f\u0430\u043d\u0435\u043b\u0438 \u0437"
        L"\u0430\u0434\u0430\u0447",
        L"\u041e\u0442\u043a\u0440\u0435\u043f\u0438\u0442\u044c \u044d"
        L"\u0442\u0443 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c"
        L"\u0443 \u043e\u0442 \u043f\u0430\u043d\u0435\u043b\u0438 \u0437"
        L"\u0430\u0434\u0430\u0447" };
    static const JumpStr kJa = {
        L"\u6700\u8fd1\u4f7f\u3063\u305f\u9805\u76ee",
        L"\u3088\u304f\u4f7f\u3046\u9805\u76ee",
        L"\u3053\u306e\u30d7\u30ed\u30b0\u30e9\u30e0\u3092\u30bf\u30b9"
        L"\u30af\u30d0\u30fc\u306b\u8868\u793a\u3059\u308b",
        L"\u3053\u306e\u30d7\u30ed\u30b0\u30e9\u30e0\u3092\u30bf\u30b9"
        L"\u30af\u30d0\u30fc\u306b\u8868\u793a\u3057\u306a\u3044" };
    static const JumpStr kZh = {
        L"\u6700\u8fd1\u4f7f\u7528\u3057\u305f\u9879\u76ee",
        L"\u7ecf\u5e38\u4f7f\u7528\u3059\u308b\u9879\u76ee",
        L"\u5c06\u6b64\u7a0b\u5e8f\u56fa\u5b9a\u5230\u4efb\u52a1\u680f",
        L"\u5c06\u6b64\u7a0b\u5e8f\u4ece\u4efb\u52a1\u680f\u89e3\u9664" };
    static const JumpStr kAr = {
        L"\u0627\u0644\u0639\u0646\u0627\u0635\u0631 \u0627\u0644\u0623"
        L"\u062e\u064a\u0631\u0629",
        L"\u0627\u0644\u0639\u0646\u0627\u0635\u0631 \u0627\u0644\u0645"
        L"\u062a\u0643\u0631\u0631\u0629",
        L"\u062a\u062b\u0628\u064a\u062a \u0647\u0630\u0627 \u0627\u0644"
        L"\u0628\u0631\u0646\u0627\u0645\u062c \u0625\u0644\u0649 \u0634"
        L"\u0631\u064a\u0637 \u0627\u0644\u0645\u0647\u0627\u0645",
        L"\u0625\u0644\u063a\u0627\u0621 \u062a\u062b\u0628\u064a\u062a"
        L" \u0647\u0630\u0627 \u0627\u0644\u0628\u0631\u0646\u0627\u0645"
        L"\u062c \u0645\u0646 \u0634\u0631\u064a\u0637 \u0627\u0644\u0645"
        L"\u0647\u0627\u0645" };
    switch (lang) {
        case 1: return kEn; case 2: return kEs; case 3: return kFr;
        case 4: return kDe; case 5: return kPt; case 6: return kPl;
        case 7: return kRu; case 8: return kJa; case 9: return kZh;
        case 10: return kAr;
        default: return kIt;
    }
}

/* The REAL taskbar pin folder (the same one PinnedApps reads). */
std::wstring PinnedFolder() {
    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base)))
        return std::wstring();
    return std::wstring(base) +
        L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
}

/* Read one string property out of a property store. True when the value
 * existed and was a non-empty string. The PROPVARIANT is always released
 * through PropVariantClear (its string is CoTaskMem memory), including the
 * miss paths - that is the shell contract, RAII for PROPVARIANT is exactly
 * what PropVariantClear is for. */
bool ReadStringProp(IPropertyStore* store, const PROPERTYKEY& key,
                    std::wstring& out) {
    if (store == nullptr) return false;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(store->GetValue(key, &pv)) &&
        pv.vt == VT_LPWSTR && pv.pwszVal != nullptr && pv.pwszVal[0]) {
        out = pv.pwszVal;
        found = true;
    }
    PropVariantClear(&pv);
    return found;
}

/* Extract the document path from one destination item: automatic
 * destinations usually surface as an IShellLink to the document, with an
 * IShellItem fallback. True when a non-empty path came out. */
bool ExtractDocPath(IUnknown* unk, std::wstring& outPath) {
    if (!unk) return false;
    ComPtr<IShellLinkW> lnk;
    if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&lnk))) && lnk) {
        wchar_t buf[MAX_PATH]{};
        if (SUCCEEDED(lnk->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH))
            && buf[0]) {
            outPath = buf;
            return true;
        }
    }
    ComPtr<IShellItem> si;
    if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&si))) && si) {
        wchar_t* disp = nullptr;
        if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &disp)) && disp) {
            outPath = disp;
            CoTaskMemFree(disp);
            return !outPath.empty();
        }
    }
    return false;
}

} // namespace

/* ------------------------------------------------------------------ */
/*  Application identity - public property-store APIs only.             */
/* ------------------------------------------------------------------ */
std::wstring JumpListWindow::ResolveAppUserModelId(HWND hwnd,
        const std::wstring& lnkPath, const std::wstring& exePath,
        int32_t& outSource) {
    (void)exePath;  /* kept for the caller: it is the implicit fallback id */
    outSource = kSourceNone;

    if (hwnd != nullptr && IsWindow(hwnd)) {
        /* 1. The window-level AppUserModelID - the key the shell groups
         *    taskbar buttons by (documented: a window-level id overrides
         *    the process-level one). SHGetPropertyStoreForWindow is the
         *    public door to it; no private shell classes are touched. */
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd,
                IID_PPV_ARGS(&store))) && store) {
            std::wstring id;
            if (ReadStringProp(store.get(), PKEY_AppUserModel_ID, id)) {
                outSource = kSourceWindow;
                return id;
            }
        }
    }
    if (!lnkPath.empty()) {
        /* 2. Classic Win32 apps: the id carried by the shell metadata of
         *    the pinned shortcut (the .lnk the group was launched from) -
         *    the same source PinnedApps uses for grouping, so the jump
         *    list lands on the application the button represents. */
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName(lnkPath.c_str(),
                nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))) && store) {
            std::wstring id;
            if (ReadStringProp(store.get(), PKEY_AppUserModel_ID, id)) {
                outSource = kSourceShortcut;
                return id;
            }
        }
    }
    return std::wstring();
}

/* ------------------------------------------------------------------ */
/*  Real jump list read: IApplicationDocumentLists.                      */
/*  This is the documented READ side of the taskbar jump list data.      */
/*  ICustomDestinationList is the app's own write API (its one read      */
/*  method only returns user-removed destinations), so it is             */
/*  deliberately NOT used, and nothing is ever invented: when the Shell  */
/*  exposes no list the sections simply stay hidden.                     */
/* ------------------------------------------------------------------ */
int32_t JumpListWindow::ReadDocumentLists(const std::wstring& appUserModelId,
        const std::wstring& exePath, std::vector<JumpListDoc>& outDocs) {
    outDocs.clear();

    /* Which id do we ask the Shell with? An explicit/default AUMID first.
     * For classic apps that never set one, Windows keys the application's
     * documents under the DEFAULT AppUserModelID derived from the
     * executable path ("Application User Model IDs", MSDN), so asking the
     * same storage with that path is the documented fallback, not a
     * fabrication. */
    std::wstring askId = appUserModelId;
    const bool implicitId = askId.empty() && !exePath.empty();
    if (implicitId) askId = exePath;
    if (askId.empty()) {
        LogTagged(L"JUMPLIST",
                  L"no application identity for the button - document"
                  L" sections stay empty (nothing fabricated)");
        return 0;
    }

    W7T_SEH_TRY {
        raii::ComInitializer com;
        if (FAILED(com.result()) && com.result() != RPC_E_CHANGED_MODE) {
            LogTagged(L"JUMPLIST", L"CoInitializeEx failed hr=0x%08X",
                      (unsigned)com.result());
            return -1;
        }
        try {
            ComPtr<IApplicationDocumentLists> adl;
            const HRESULT hrCreate = CoCreateInstance(
                CLSID_ApplicationDocumentLists, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&adl));
            if (FAILED(hrCreate) || !adl) {
                LogTagged(L"JUMPLIST",
                          L"CoCreateInstance(ApplicationDocumentLists)"
                          L" hr=0x%08X", (unsigned)hrCreate);
                return -1;
            }
            const HRESULT hrSet = adl->SetAppID(askId.c_str());
            if (FAILED(hrSet)) {
                LogTagged(L"JUMPLIST", L"SetAppID(\"%s\") failed hr=0x%08X",
                          askId.c_str(), (unsigned)hrSet);
                return -1;
            }
            UINT sectionCounts[2] = { 0, 0 };
            const APPDOCLISTTYPE kinds[2] = { ADLT_RECENT,
                                                        ADLT_FREQUENT };
            for (int pass = 0; pass < 2; ++pass) {
                ComPtr<IObjectArray> items;
                const HRESULT hrList = adl->GetList(
                    kinds[pass], (UINT)kMaxDocsPerSection,
                    IID_PPV_ARGS(&items));
                if (FAILED(hrList) || !items) {
                    /* A normal answer for apps that register no automatic
                     * destinations: empty section, not a failure. */
                    LogTagged(L"JUMPLIST",
                              L"GetList(section %d) hr=0x%08X - the app"
                              L" exposes no such list",
                              pass, (unsigned)hrList);
                    continue;
                }
                UINT count = 0;
                items->GetCount(&count);
                for (UINT i = 0; i < count &&
                                sectionCounts[pass] < (UINT)kMaxDocsPerSection;
                     ++i) {
                    /* One malformed element must not stop the others. */
                    ComPtr<IUnknown> unk;
                    if (FAILED(items->GetAt(i, IID_PPV_ARGS(&unk))) || !unk)
                        continue;
                    JumpListDoc entry;
                    entry.section = pass;
                    if (!ExtractDocPath(unk.get(), entry.path)) continue;
                    const size_t slash = entry.path.find_last_of(L"\\/");
                    entry.displayName = (slash == std::wstring::npos)
                        ? entry.path : entry.path.substr(slash + 1);
                    /* Windows 7 shows the document name without the
                     * extension; same rule, taken from the file name. */
                    const size_t dot = entry.displayName.find_last_of(L'.');
                    if (dot != std::wstring::npos && dot > 0)
                        entry.displayName = entry.displayName.substr(0, dot);
                    ++sectionCounts[pass];
                    outDocs.push_back(std::move(entry));
                }
            }
            LogTagged(L"JUMPLIST",
                      L"entries loaded: recent=%d frequent=%d (id source=%s)",
                      (int)sectionCounts[0], (int)sectionCounts[1],
                      implicitId ? L"implicit, from executable path"
                                 : L"AppUserModelID");
            return 0;
        } catch (...) {
            /* A C++ exception here can only be std::bad_alloc: controlled
             * failure, logged, never propagated to the taskbar thread. */
            outDocs.clear();
            LogTagged(L"JUMPLIST", L"C++ exception while reading the list");
            return -1;
        }
    } W7T_SEH_CATCH {
        outDocs.clear();
        LogTagged(L"JUMPLIST", L"hardware fault while reading the list");
        return -1;
    } W7T_SEH_END
    return -1;
}

/* ------------------------------------------------------------------ */

JumpListWindow& JumpListWindow::Instance() {
    static JumpListWindow instance;
    return instance;
}

void JumpListWindow::RegisterClassOnce() {
    if (m_classRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    m_classRegistered = true;
}

int JumpListWindow::Sc(int v96) const {
    return ::MulDiv(v96, (int)m_dpi, 96);
}

void JumpListWindow::ClearContent() {
    m_rows.clear();   /* each Row owns its HICON through raii::IconHandle */
    m_docs.clear();
}

void JumpListWindow::BuildRows() {
    m_rows.clear();

    auto addDocs = [&](int32_t section, Row::Kind kind) {
        for (const JumpListDoc& doc : m_docs) {
            if (doc.section != section) continue;
            Row r;
            r.kind = kind;
            r.label = doc.displayName;
            r.path = doc.path;
            /* The REAL file icon (never a generic placeholder): fetched
             * once per open; the row owns it from here on. SHGetFileInfo
             * has faulted on dead network paths in the past, so the call
             * keeps its own SEH barrier - one bad icon must not take the
             * popup down, the entry simply paints without its icon. */
            SHFILEINFOW sfi{};
            W7T_SEH_TRY {
                if (SHGetFileInfoW(r.path.c_str(), 0, &sfi, sizeof(sfi),
                                   SHGFI_ICON | SHGFI_SMALLICON)
                    && sfi.hIcon != nullptr) {
                    r.icon.reset(sfi.hIcon);  /* ownership moves into the row */
                }
            } W7T_SEH_CATCH {
                /* row survives, iconless; space stays reserved so the
                 * text keeps its alignment either way */
            } W7T_SEH_END
            m_rows.push_back(std::move(r));
        }
    };

    addDocs(0, Row::DocRecent);

    /* The Frequent section only earns its place when it is not a copy of
     * the Recent one (many apps return the same documents in both lists;
     * the Windows 7 jump list then shows a single section). */
    bool frequentDiffers = false;
    for (const JumpListDoc& d : m_docs) {
        if (d.section != 1) continue;
        bool found = false;
        for (const JumpListDoc& e : m_docs) {
            if (e.section == 0 &&
                _wcsicmp(e.path.c_str(), d.path.c_str()) == 0) {
                found = true;
                break;
            }
        }
        if (!found) { frequentDiffers = true; break; }
    }
    if (frequentDiffers) addDocs(1, Row::DocFrequent);

    Row app;
    app.kind = Row::App;
    app.label = m_title;
    m_rows.push_back(std::move(app));

    Row pin;
    pin.kind = Row::Pin;
    pin.label = m_pinned ? Str(m_lang).unpin : Str(m_lang).pin;
    m_rows.push_back(std::move(pin));
}

void JumpListWindow::Layout() {
    m_width = Sc(kWidth96);
    int y = Sc(kPad96);

    int lastKind = -1;   /* -1: no previous row (outside the Kind range) */
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        const bool isDoc =
            r.kind == Row::DocRecent || r.kind == Row::DocFrequent;

        /* Section header band before the first row of every section. */
        if (isDoc && (int)r.kind != lastKind) {
            y += Sc(kHeader96);
        }
        /* Separator band before the application row and before the pin
         * row (the Windows 7 list separates documents, the app link and
         * the tasks). */
        if (!isDoc && (lastKind == (int)Row::DocRecent ||
                       lastKind == (int)Row::DocFrequent ||
                       lastKind == (int)Row::App)) {
            y += Sc(kSep96);
        }

        const int rowH = (r.kind == Row::App) ? Sc(kRowApp96)
                         : (r.kind == Row::Pin) ? Sc(kRowPin96)
                         : Sc(kRowDoc96);
        r.rect = RECT{ 0, y, m_width, y + rowH };
        y += rowH;
        lastKind = (int)r.kind;
    }
    m_totalH = y + Sc(kPad96);
}

RECT JumpListWindow::RowRect(size_t index) const {
    if (index < m_rows.size()) return m_rows[index].rect;
    return RECT{ 0, 0, 0, 0 };
}

/* Index of the row under client coordinates, -1 for none. The empty strip
 * below the last row is NOT a target (Windows 7: releasing on free popup
 * space closes the list without activating anything). */
int JumpListWindow::HitRowClient(POINT pt) const {
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (PtInRect(&m_rows[i].rect, pt)) return (int)i;
    }
    return -1;
}

bool JumpListWindow::InInteractionArea(POINT screenPt) const {
    return PtInRect(&m_area, screenPt) != FALSE;
}

/* Placement from the REAL taskbar button rectangle, per edge, clamped to
 * the work area of the monitor that hosts the button - the same clamp the
 * clock flyout applies (FlyoutLauncher::FixFlyoutPosition). With the bar
 * at the bottom the popup opens ABOVE the button, left-aligned with it,
 * like Windows 7. Gap and margin are DPI-scaled; no unscaled offsets. */
void JumpListWindow::Place(HWND hwnd, const RECT& button, int32_t edge) {
    const int gap = Sc(kGap96);
    const int margin = Sc(kEdgeMargin96);

    HMONITOR mon = MonitorFromRect(&button, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT wa{};
    if (!GetMonitorInfoW(mon, &mi)) {
        /* No monitor info (rare): fall back to the button neighborhood so
         * the popup is at least visible near its anchor. */
        wa = RECT{ button.left - Sc(400), button.top - Sc(600),
                   button.right + Sc(400), button.bottom + Sc(80) };
    } else {
        wa = mi.rcWork;
    }

    const int w = m_width, h = m_totalH;
    int x = button.left, y = button.top - h - gap;
    switch (edge) {
        case kEdgeTop:    /* bar at the top: the list opens BELOW */
            y = button.bottom + gap;
            break;
        case kEdgeLeft:   /* vertical bar at the left: open to its right */
            x = button.right + gap;
            y = button.top;
            break;
        case kEdgeRight:  /* vertical bar at the right: open to its left */
            x = button.left - w - gap;
            y = button.top;
            break;
        case kEdgeBottom:
        default:
            break;
    }
    if (x + w > wa.right - margin)  x = wa.right - margin - w;
    if (x < wa.left + margin)       x = wa.left + margin;
    if (y + h > wa.bottom - margin) y = wa.bottom - margin - h;
    if (y < wa.top + margin)        y = wa.top + margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_popupRect = RECT{ x, y, x + w, y + h };
}

/* Interaction area of the gesture: popup, taskbar button and everything
 * between them. Leaving it is what the managed side treats as cancel. */
void JumpListWindow::UpdateInteractionArea() {
    RECT u = m_popupRect;
    UnionRect(&u, &u, &m_buttonRect);
    const int pad = Sc(4);
    InflateRect(&u, pad, pad);
    m_area = u;
}

int32_t JumpListWindow::Open(const RECT& buttonRectScreen, int32_t edge,
        const std::wstring& title, const std::wstring& launchPath,
        const std::wstring& pinnedLnkPath, bool isPinned,
        HWND representativeHwnd, const std::wstring& exePath,
        const uint32_t* iconArgb, int iconW, int iconH, int lang,
        wchar_t* outAppId, int outAppIdCap) {
    if (buttonRectScreen.right <= buttonRectScreen.left ||
        buttonRectScreen.bottom <= buttonRectScreen.top) {
        LogTagged(L"JUMPLIST", L"bad button rectangle - refusing to open");
        return -3;
    }

    /* One gesture at a time: drop any stale popup before touching the
     * shared state, so a failure below can never leave yesterday's rows
     * on screen. */
    Hide();
    m_hover = -1;
    ClearContent();
    m_appIcon.reset();

    W7T_SEH_TRY {
        RegisterClassOnce();

        m_dpi = GetDpiForScreenRect(buttonRectScreen);
        m_lang = (lang >= 0 && lang <= 10) ? lang : 1;
        m_title = title;
        m_launchPath = launchPath;
        m_pinnedLnk = pinnedLnkPath;
        m_pinned = isPinned;
        m_buttonRect = buttonRectScreen;
        m_edge = edge;

        /* --- application identity (public Shell APIs only) --- */
        int32_t source = kSourceNone;
        std::wstring aumid = ResolveAppUserModelId(representativeHwnd,
                                                   pinnedLnkPath,
                                                   exePath, source);
        if (source == kSourceNone && !exePath.empty()) {
            LogTagged(L"JUMPLIST",
                      L"application identity resolved (source=implicit"
                      L" default): AppUserModelID=<from executable path>");
        } else {
            LogTagged(L"JUMPLIST",
                      L"application identity resolved (source=%d):"
                      L" AppUserModelID=%s", source,
                      aumid.empty() ? L"(none)" : aumid.c_str());
        }
        if (outAppId != nullptr && outAppIdCap > 0) {
            CopyToFixed(outAppId, (size_t)outAppIdCap, aumid);
        }

        /* --- the application's REAL jump list entries --- */
        const int32_t read = ReadDocumentLists(aumid, exePath, m_docs);
        if (read < 0) {
            ClearContent();
            return -1;   /* Shell/COM failure: the managed side cancels */
        }
        const int32_t docCount = (int32_t)m_docs.size();

        /* --- app icon for the application row: pixels handed over by the
         *     managed side (the group's live icon, packaged apps included) */
        if (iconArgb != nullptr && iconW > 0 && iconH > 0) {
            const std::vector<uint32_t> px(
                iconArgb, iconArgb + (size_t)iconW * (size_t)iconH);
            if (HBITMAP hb = MakeHBitmapFromArgb(px, iconW, iconH)) {
                m_appIcon.reset(hb);   /* BitmapHandle owns and deletes it */
            }
        }

        BuildRows();
        Layout();

        if (m_hwnd == nullptr) {
            m_hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                kClassName, L"", WS_POPUP,
                0, 0, m_width, m_totalH,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m_hwnd == nullptr) {
                LogTagged(L"JUMPLIST", L"CreateWindowExW failed err=%d",
                          (int)GetLastError());
                return -2;
            }
            ApplyAeroFlyoutStyle(m_hwnd);   /* shared Aero flyout border */
        }
        Place(m_hwnd, m_buttonRect, m_edge);
        UpdateInteractionArea();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        LogTagged(L"JUMPLIST",
                  L"popup opened: rect=(%d,%d)-(%d,%d) screen px, dpi=%d,"
                  L" entries=%d, edge=%d",
                  (int)m_popupRect.left, (int)m_popupRect.top,
                  (int)m_popupRect.right, (int)m_popupRect.bottom,
                  (int)m_dpi, (int)docCount, (int)edge);
        return docCount;
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while opening the popup");
        Hide();
        ClearContent();
        return -1;
    } W7T_SEH_END
}

void JumpListWindow::Hide() {
    W7T_SEH_TRY {
        if (m_hwnd != nullptr) ShowWindow(m_hwnd, SW_HIDE);
        m_hover = -1;
    } W7T_SEH_CATCH {
    } W7T_SEH_END
}

bool JumpListWindow::IsVisible() const {
    return m_hwnd != nullptr && IsWindowVisible(m_hwnd);
}

int32_t JumpListWindow::SetHover(int32_t screenX, int32_t screenY) {
    if (!IsVisible()) return 0;
    W7T_SEH_TRY {
        POINT pt{ screenX, screenY };
        if (!InInteractionArea(pt)) {
            if (m_hover != -1) {
                m_hover = -1;
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            return 0;
        }
        POINT cl = pt;
        ScreenToClient(m_hwnd, &cl);
        const int hit = HitRowClient(cl);
        if (hit != m_hover) {
            const int previous = m_hover;
            m_hover = hit;
            /* Repaint only the involved row bands (client coordinates);
             * the full-width band keeps the separators and headers intact
             * because the paint pass redraws them from the same geometry. */
            if (m_hover >= 0) {
                RECT r = RowRect((size_t)m_hover);
                r.left = 0;
                r.right = m_width;
                InvalidateRect(m_hwnd, &r, FALSE);
            }
            if (previous >= 0) {
                RECT r = RowRect((size_t)previous);
                r.left = 0;
                r.right = m_width;
                InvalidateRect(m_hwnd, &r, FALSE);
            }
        }
        return 1;
    } W7T_SEH_CATCH {
    } W7T_SEH_END
    return 1;
}

/* Execute the action behind the row under the screen point. Returns 0 when
 * the popup was not open (nothing to do); 1 when the gesture ended, with
 * *outBits telling the managed side what happened. */
int32_t JumpListWindow::ActivateRow(int32_t screenX, int32_t screenY,
                                    int32_t* outBits) {
    int32_t bits = 0;
    if (outBits != nullptr) *outBits = 0;
    if (!IsVisible()) return 0;
    W7T_SEH_TRY {
        POINT cl{ screenX, screenY };
        ScreenToClient(m_hwnd, &cl);
        const int hit = HitRowClient(cl);
        if (hit >= 0) {
            const Row& row = m_rows[(size_t)hit];
            switch (row.kind) {
                case Row::DocRecent:
                case Row::DocFrequent: {
                    /* Open the document with its registered application.
                     * A dead path fails quietly: ShellExecuteW returns an
                     * SE_ERR_* code - that is logged, never reported as a
                     * success. */
                    const HINSTANCE hi = ShellExecuteW(nullptr, L"open",
                        row.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    const INT_PTR rc = (INT_PTR)hi;
                    if (rc <= 32) {
                        LogTagged(L"JUMPLIST",
                                  L"ShellExecuteW(open document) failed"
                                  L" code=%d path=\"%s\"",
                                  (int)rc, row.path.c_str());
                    } else {
                        bits |= BitsOpenedDoc;
                        LogTagged(L"JUMPLIST",
                                  L"item activated: document \"%s\"",
                                  row.path.c_str());
                    }
                    break;
                }
                case Row::App:
                    LaunchApp();
                    bits |= BitsLaunchedApp;
                    LogTagged(L"JUMPLIST", L"item activated: application row");
                    break;
                case Row::Pin:
                    PerformPinOrUnpin();
                    bits |= BitsPinToggled;
                    LogTagged(L"JUMPLIST", L"item activated: taskbar pin"
                                           L" toggled");
                    break;
            }
        } else {
            LogTagged(L"JUMPLIST",
                      L"released over empty popup space - closing");
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while activating a row");
    } W7T_SEH_END
    Hide();
    if (outBits != nullptr) *outBits = bits;
    return 1;
}

void JumpListWindow::LaunchApp() {
    W7T_SEH_TRY {
        if (!m_launchPath.empty()) {
            ShellExecuteW(nullptr, L"open", m_launchPath.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while launching the app");
    } W7T_SEH_END
}

void JumpListWindow::PerformPinOrUnpin() {
    W7T_SEH_TRY {
        if (m_pinned) {
            /* Unpin: delete the real .lnk; the PinnedApps watcher refreshes
             * the model on its own, and the managed side also calls
             * InvalidatePins when it sees BitsPinToggled. */
            if (!m_pinnedLnk.empty()) {
                DeleteFileW(m_pinnedLnk.c_str());
            }
        } else {
            /* Pin: create the .lnk in the real shell pin folder. */
            const std::wstring dir = PinnedFolder();
            if (!dir.empty() && !m_launchPath.empty()) {
                CreateDirectoryW(dir.c_str(), nullptr); /* ignore result */
                std::wstring name = m_title;
                for (wchar_t& c : name) {
                    if (c == L'/' || c == L'\\' || c == L':' || c == L'*' ||
                        c == L'?' || c == L'"' || c == L'<' || c == L'>' ||
                        c == L'|') c = L'_';
                }
                if (name.empty()) name = L"App";
                const std::wstring lnk = dir + L"\\" + name + L".lnk";

                raii::ComInitializer com;
                ComPtr<IShellLinkW> link;
                if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) && link) {
                    link->SetPath(m_launchPath.c_str());
                    link->SetIconLocation(m_launchPath.c_str(), 0);
                    ComPtr<IPersistFile> pf;
                    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))
                        && pf) {
                        pf->Save(lnk.c_str(), TRUE);
                    }
                }
            }
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while toggling the pin");
    } W7T_SEH_END
}

/* ------------------------------------------------------------------ */
/*  Painting. Colors measured from Windows 7 jump list reference         */
/*  screenshots and shared with the project's recreated flyouts: the     */
/*  popup body is the pale blue-white, section headers are bold          */
/*  steel-blue on the same background, separators are the #C9D6E6 of     */
/*  the Aero menus, the hover is the light azure gradient inside its     */
/*  #94C6EF border - the exact hover the Windows 7 menus paint. Every    */
/*  length below goes through Sc(), so the proportions are DPI proof.    */
/* ------------------------------------------------------------------ */
void JumpListWindow::GradientRect(HDC hdc, const RECT& r, COLORREF top,
                                   COLORREF bottom, COLORREF edge) {
    const int h = r.bottom - r.top;
    if (h <= 0 || r.right <= r.left + 4) return;
    /* One horizontal line per scanline with an interpolated color: pure
     * GDI, no msimg32 dependency inside this window (the project's other
     * popups paint the same way), and trivially correct at any DPI because
     * the row rects are already device pixels. */
    for (int y = r.top + 2; y < r.bottom - 2; ++y) {
        const int t = (y - r.top) * 256 / h;
        const COLORREF c = RGB(
            GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * t / 256,
            GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * t / 256,
            GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * t / 256);
        const UniqueGdiObject pen(CreatePen(PS_SOLID, 1, c));
        if (!pen.valid()) continue;
        const SelectGuard sg(hdc, (HGDIOBJ)pen.get());
        MoveToEx(hdc, r.left + 3, y, nullptr);
        LineTo(hdc, r.right - 3, y);
    }
    const UniqueGdiObject epen(CreatePen(PS_SOLID, 1, edge));
    if (epen.valid()) {
        const SelectGuard esg(hdc, (HGDIOBJ)epen.get());
        const int l = r.left + 2, t2 = r.top + 1;
        const int rr = r.right - 3, bb = r.bottom - 2;
        MoveToEx(hdc, l, t2, nullptr);   LineTo(hdc, rr + 1, t2);
        MoveToEx(hdc, l, bb, nullptr);   LineTo(hdc, rr + 1, bb);
        MoveToEx(hdc, l, t2, nullptr);   LineTo(hdc, l, bb);
        MoveToEx(hdc, rr, t2, nullptr);  LineTo(hdc, rr, bb);
    }
}

void JumpListWindow::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr) return;
    RECT client{};
    GetClientRect(hwnd, &client);

    const UniqueGdiObject bg(CreateSolidBrush(RGB(0xF2, 0xF6, 0xFB)));
    if (bg.valid()) FillRect(hdc, &client, (HBRUSH)bg.get());

    SetBkMode(hdc, TRANSPARENT);
    const int fontH = -::MulDiv(13, (int)m_dpi, 96);
    const UniqueGdiObject font(CreateFontW(fontH, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    const UniqueGdiObject fontBold(CreateFontW(fontH, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    if (font.valid()) SelectObject(hdc, (HGDIOBJ)font.get());

    auto hline = [&](int lineY) {
        const UniqueGdiObject pen(
            CreatePen(PS_SOLID, 1, RGB(0xC9, 0xD6, 0xE6)));
        if (!pen.valid()) return;
        const SelectGuard pg(hdc, (HGDIOBJ)pen.get());
        MoveToEx(hdc, Sc(10), lineY, nullptr);
        LineTo(hdc, client.right - Sc(10), lineY);
    };

    const JumpStr& S = Str(m_lang);
    const int margin = Sc(12);
    int lastKind = -1;

    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const bool isDoc =
            r.kind == Row::DocRecent || r.kind == Row::DocFrequent;

        if (isDoc && (int)r.kind != lastKind) {
            /* Section header ("Recent items" / "Frequent items"): bold
             * steel-blue with the separator under it - the Windows 7
             * jump list band. Its y comes from the row band Layout left
             * empty right above this row. */
            const int hy = r.rect.top - Sc(kHeader96);
            if (fontBold.valid()) SelectObject(hdc, (HGDIOBJ)fontBold.get());
            SetTextColor(hdc, RGB(0x40, 0x58, 0x78));
            RECT hr{ margin, hy, client.right - margin, hy + Sc(kHeader96) };
            DrawTextW(hdc,
                      r.kind == Row::DocFrequent ? S.frequent : S.recent,
                      -1, &hr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            if (font.valid()) SelectObject(hdc, (HGDIOBJ)font.get());
            hline(hy + Sc(kHeader96));
        }
        if (!isDoc && (lastKind == (int)Row::DocRecent ||
                       lastKind == (int)Row::DocFrequent ||
                       lastKind == (int)Row::App)) {
            hline(r.rect.top - Sc(kSep96) / 2);
        }
        lastKind = (int)r.kind;

        if ((int)i == m_hover) {
            GradientRect(hdc, r.rect,
                         RGB(0xED, 0xF6, 0xFD), RGB(0xC5, 0xE1, 0xF7),
                         RGB(0x94, 0xC6, 0xEE));
        }

        const int iconLeft = Sc(14);
        const int textLeft = isDoc ? iconLeft + Sc(kDocIcon96) + Sc(6)
                                   : iconLeft;
        SetTextColor(hdc, (r.kind == Row::Pin) ? RGB(0x1E, 0x6F, 0xC9)
                                                : RGB(0x1E, 0x1E, 0x1E));
        RECT tr{ textLeft, r.rect.top, client.right - margin, r.rect.bottom };
        DrawTextW(hdc, r.label.c_str(), -1, &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

        if (isDoc && r.icon.get() != nullptr) {
            const int box = Sc(kDocIcon96);
            DrawIconEx(hdc, iconLeft, r.rect.top + (Sc(kRowDoc96) - box) / 2,
                       r.icon.get(), box, box, 0, nullptr, DI_NORMAL);
        }
        if (r.kind == Row::App && m_appIcon) {
            const int box = Sc(kAppIcon96);
            DrawBitmapScaled(hdc, m_appIcon.get(), box, box,
                             iconLeft,
                             r.rect.top + (Sc(kRowApp96) - box) / 2);
        }
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK JumpListWindow::WndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    W7T_SEH_TRY {
        switch (msg) {
            case WM_PAINT:
                Instance().OnPaint(hwnd);
                return 0;
            case WM_ERASEBKGND:
                return 1;   /* the paint pass fills every band itself */
            case WM_MOUSEMOVE: {
                /* Fallback path, only reached when NO capture owns the
                 * input (capture-loss robustness): keep the hover in sync
                 * with the real cursor. During a gesture this window
                 * receives no mouse messages at all - the managed side
                 * forwards positions through SetHover instead. */
                JumpListWindow& j = Instance();
                if (j.IsVisible()) {
                    POINT cl{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    const int hit = j.HitRowClient(cl);
                    if (hit != j.m_hover) {
                        j.m_hover = hit;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                /* Only reachable without a capture (same fallback): the
                 * release activates the row under the cursor, exactly like
                 * the gesture release does. */
                JumpListWindow& j = Instance();
                if (j.IsVisible()) {
                    POINT sc{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ClientToScreen(hwnd, &sc);
                    int32_t bits = 0;
                    j.ActivateRow(sc.x, sc.y, &bits);
                }
                return 0;
            }
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) Instance().Hide();
                return 0;
            case WM_NCDESTROY:
                /* Window gone: drop everything bound to it so a later open
                 * starts from a clean state (no stuck hover, no dangling
                 * HWND, no orphan handles). */
                Instance().m_hwnd = nullptr;
                Instance().m_hover = -1;
                Instance().m_popupRect = RECT{};
                Instance().m_area = RECT{};
                Instance().ClearContent();
                Instance().m_appIcon.reset();
                return 0;
        }
    } W7T_SEH_CATCH {
    } W7T_SEH_END
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace w7t
