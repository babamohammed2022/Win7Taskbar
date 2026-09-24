/*
 * Win7Taskbar - Start Menu search: Shell Search Folder backend +
 *               high-quality shell icon export
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Written from scratch on the Microsoft documentation (Shell Search API,
 * ISearchFolderItemFactory, SHGetKnownFolderIDList, IShellItemImageFactory)
 * and on the behavioural reconstruction of the
 * SearchFolder.dll search pipeline provided with the task. No Microsoft
 * source code is copied and no SearchFolder.dll is loaded or referenced:
 * the backend uses only public shell32 COM interfaces.
 *
 * RAII ovunque (COM, PIDL, DC, handle servizi) e try/catch + guardie SEH
 * in ogni punto di confine: la scansione non deve mai buttare giu' il
 * menu per un risultato "impossibile".
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define W7T_BUILDING_DLL 1

#include "Win7TaskbarCore.h"
#include "StartMenuSearchFolder.h"
#include "StartMenuIndex.h"
#include "SehGuard.h"

#include <windows.h>
#include <winsvc.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <knownfolders.h>
#include <objbase.h>
/* niente propsys/propkey/structuredquerycondition: la rotta finale (v3.10)
 * non usa piu' le condizioni strutturate della ricerca - il wordwheel e'
 * il matcher condiviso del progetto sui risultati enumerati dallo scope.
 * L'header legacy della Structured Query si e' rivelato non dichiarativo
 * su alcune versioni del SDK usate dalla CI. */

#include <atomic>
#include <new>
#include <cwchar>
/* niente <cwctype>: non serve (nessuna funzione wctype usata) e su uno dei
 * toolchain CI ha fatto saltare la TU inteira con errori sul namespace
 * globale (wctrans_t/towctrans) prima ancora di entrare nel corpo. */
#include <set>
#include <string>
#include <vector>

namespace {

template <typename T>
class UniqueCom {
public:
    UniqueCom() noexcept = default;
    explicit UniqueCom(T* p) noexcept : p_(p) {}
    ~UniqueCom() { reset(); }
    UniqueCom(const UniqueCom&) = delete;
    UniqueCom& operator=(const UniqueCom&) = delete;
    void reset(T* p = nullptr) noexcept {
        if (p_) {
            p_->Release();
        }
        p_ = p;
    }
    T** put() noexcept { reset(); return &p_; }
    T* get() const noexcept { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

struct UniqueComInit {
    bool owned = false;
    UniqueComInit() noexcept {
        owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    }
    ~UniqueComInit() {
        if (owned) {
            CoUninitialize();
        }
    }
    UniqueComInit(const UniqueComInit&) = delete;
    UniqueComInit& operator=(const UniqueComInit&) = delete;
};

struct UniquePidl {
    PIDLIST_ABSOLUTE p = nullptr;
    UniquePidl() noexcept = default;
    ~UniquePidl() {
        if (p != nullptr) {
            CoTaskMemFree(p);
        }
    }
    UniquePidl(const UniquePidl&) = delete;
    UniquePidl& operator=(const UniquePidl&) = delete;
};

struct UniqueScHandle {
    SC_HANDLE h = nullptr;
    UniqueScHandle() noexcept = default;
    ~UniqueScHandle() {
        if (h != nullptr) {
            CloseServiceHandle(h);
        }
    }
    UniqueScHandle(const UniqueScHandle&) = delete;
    UniqueScHandle& operator=(const UniqueScHandle&) = delete;
};

struct UniqueDc {
    HDC dc = nullptr;
    UniqueDc() noexcept = default;
    ~UniqueDc() {
        if (dc != nullptr) {
            DeleteDC(dc);
        }
    }
    UniqueDc(const UniqueDc&) = delete;
    UniqueDc& operator=(const UniqueDc&) = delete;
};

struct UniqueGdiObj {
    HGDIOBJ obj = nullptr;
    UniqueGdiObj() noexcept = default;
    ~UniqueGdiObj() {
        if (obj != nullptr) {
            DeleteObject(obj);
        }
    }
    UniqueGdiObj(const UniqueGdiObj&) = delete;
    UniqueGdiObj& operator=(const UniqueGdiObj&) = delete;
};

/* RAII per le stringhe COM restituite da GetDisplayName: mai una
 * CoTaskMemFree saltata, nemmeno se la conversione in wstring lancia. */
struct UniqueCoStr {
    PWSTR p = nullptr;
    explicit UniqueCoStr(PWSTR raw) noexcept : p(raw) {}
    ~UniqueCoStr() {
        if (p != nullptr) {
            CoTaskMemFree(p);
        }
    }
    UniqueCoStr(const UniqueCoStr&) = delete;
    UniqueCoStr& operator=(const UniqueCoStr&) = delete;
};

struct RowCaseLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

bool QueryWindowsSearchStatus() {
    bool running = false;
    try {
        UniqueScHandle scm;
        scm.h = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm.h == nullptr) {
            return false;
        }
        UniqueScHandle svc;
        svc.h = OpenServiceW(scm.h, L"WSearch", SERVICE_QUERY_STATUS);
        if (svc.h == nullptr) {
            return false;
        }
        SERVICE_STATUS_PROCESS ssp{};
        DWORD bytes = 0;
        if (QueryServiceStatusEx(svc.h, SC_STATUS_PROCESS_INFO,
                                 reinterpret_cast<LPBYTE>(&ssp),
                                 sizeof(ssp), &bytes)) {
            running = ssp.dwCurrentState == SERVICE_RUNNING;
        }
    } catch (...) {
        running = false;
    }
    return running;
}

struct SearchRowBudget {
    const ULONGLONG started = GetTickCount64();
    std::size_t visited = 0;
    bool timedOut = false;
    static const std::size_t kMaxRows = 48;
    static const ULONGLONG kMaxMs = 6000;

    bool Step(uint32_t generation,
              const std::atomic<uint32_t>& generationSource) {
        if ((++visited & 0x7u) == 0) {
            if (GetTickCount64() - started > kMaxMs) {
                timedOut = true;
                return false;
            }
            if (generationSource.load(std::memory_order_acquire) != generation) {
                timedOut = true;
                return false;
            }
        }
        return true;
    }
};

/* Semantica wordwheel: ISearchFolderItemFactory SENZA condizione
 * (explicitly documented empty condition = enumerates the whole scope);
 * ogni candidato passa poi per il matcher del progetto
 * (w7t::startmenu::NameMatchesQuery / RankMatchAll, lo stesso del walker
 * e dei suoi test unitari): prefisso di parola o sottostringa per OGNI
 * token della query, cioe' esattamente il comportamento "mentre scrivi"
 * della ricerca di Windows 7. I filtri strutturati (IConditionFactory)
 * richiedono un header legacy del SDK che in CI si rivela non
 * dichiarativo: questa rotta li elimina del tutto senza perdere nulla. */

/* Scope = le stesse radici della ricerca classica del menu Start /
 * Open-Shell / walking di riserva: Documenti, Immagini, Musica, Video,
 * Download, Desktop (cartelle note pubbliche, SHGetKnownFolderIDList).
 * Niente scansione indiscriminata del disco: fuori scope -> niente righe. */
HRESULT CreateSearchScope(IShellItemArray** scopeOut) {
    if (scopeOut == nullptr) {
        return E_INVALIDARG;
    }
    *scopeOut = nullptr;
    static const struct { const GUID* id; } kRoots[] = {
        { &FOLDERID_Documents },
        { &FOLDERID_Pictures },
        { &FOLDERID_Music },
        { &FOLDERID_Videos },
        { &FOLDERID_Downloads },
        { &FOLDERID_Desktop },
    };
    std::vector<LPCITEMIDLIST> pidls;
    pidls.reserve(_countof(kRoots));
    std::vector<UniquePidl*> owned;
    owned.reserve(_countof(kRoots));
    try {
        for (const auto& def : kRoots) {
            auto holder = new (std::nothrow) UniquePidl();
            if (holder == nullptr) {
                for (UniquePidl* u : owned) { delete u; }
                return E_OUTOFMEMORY;
            }
            owned.push_back(holder);
            if (SUCCEEDED(SHGetKnownFolderIDList(*def.id, KF_FLAG_DONT_VERIFY,
                                                 nullptr, &holder->p)) &&
                holder->p != nullptr) {
                pidls.push_back(holder->p);
            }
        }
    } catch (...) {
        for (UniquePidl* u : owned) { delete u; }
        return E_FAIL;
    }
    HRESULT hr = E_FAIL;
    if (!pidls.empty()) {
        hr = SHCreateShellItemArrayFromIDLists(
            static_cast<UINT>(pidls.size()), pidls.data(),
            IID_PPV_ARGS(scopeOut));
    }
    for (UniquePidl* u : owned) {
        delete u; /* la ShellItemArray tiene i suoi riferimenti */
    }
    return hr;
}

std::wstring ItemFileSysPath(IShellItem* item) {
    if (item == nullptr) {
        return std::wstring();
    }
    PWSTR raw = nullptr;
    const HRESULT hr = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
    UniqueCoStr guard(raw);
    if (SUCCEEDED(hr) && raw != nullptr && raw[0] != L'\0') {
        return std::wstring(raw);
    }
    return std::wstring();
}

} /* namespace */

namespace w7t {
namespace startmenu {

bool WindowsSearchRunning() {
    static std::atomic<ULONGLONG> g_checkedAt{0};
    static std::atomic<int> g_running{-1};
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG at = g_checkedAt.load(std::memory_order_acquire);
    const int cached = g_running.load(std::memory_order_acquire);
    if (cached >= 0 && now - at < 60000ULL) {
        return cached == 1;
    }
    const bool running = QueryWindowsSearchStatus();
    g_running.store(running ? 1 : 0, std::memory_order_release);
    g_checkedAt.store(now, std::memory_order_release);
    return running;
}

int CollectShellSearchRows(const std::wstring& query,
                           uint32_t generation,
                           const std::atomic<uint32_t>& generationSource,
                           std::vector<std::wstring>& rows) {
    rows.clear();
    if (query.empty()) {
        return 1;
    }
    if (!WindowsSearchRunning()) {
        return 1; /* servizio fermo: il walker classico resta il fallback */
    }
    int result = 1;
    W7T_SEH_TRY {
    try {
        UniqueComInit comInit;
        UniqueCom<ISearchFolderItemFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_SearchFolderItemFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.put()));
        if (FAILED(hr) || !factory) {
            /* interfaccia assente (Windows mai previsto, ma il menu non si
             * fida): il chiamante ripiega sul walker */
            return 1;
        }
        UniqueCom<IShellItemArray> scope;
        hr = CreateSearchScope(scope.put());
        if (FAILED(hr) || !scope) {
            return 1;
        }
        hr = factory->SetScope(scope.get());
        if (FAILED(hr)) {
            return 1;
        }
        /* Nessuna SetCondition: condizione vuota = enumerazione di tutto
         * lo scope (documentato); il wordwheel e' il matcher condiviso
         * del progetto applicato riga per riga piu' sotto. */
        UniqueCom<IShellItem> searchItem;
        hr = factory->GetShellItem(IID_PPV_ARGS(searchItem.put()));
        if (FAILED(hr) || !searchItem) {
            return 1;
        }
        UniqueCom<IEnumShellItems> enumItems;
        hr = searchItem->BindToHandler(nullptr, BHID_EnumItems,
                                       IID_PPV_ARGS(enumItems.put()));
        if (FAILED(hr) || !enumItems) {
            return 1;
        }

        SearchRowBudget budget;
        std::set<std::wstring, RowCaseLess> seen;
        result = 0;
        while (rows.size() < SearchRowBudget::kMaxRows) {
            if (!budget.Step(generation, generationSource)) {
                if (budget.timedOut && !rows.empty()) {
                    result = 2; /* troncato dal budget */
                }
                break;
            }
            IShellItem* rawItem = nullptr;
            ULONG fetched = 0;
            hr = enumItems->Next(1, &rawItem, &fetched);
            if (hr != S_OK || fetched == 0 || rawItem == nullptr) {
                break;
            }
            UniqueCom<IShellItem> item(rawItem);
            try {
                /* Come la ricerca verde: mai oggetti nascosti/sistema. */
                SFGAOF attrs = 0;
                if (SUCCEEDED(item->GetAttributes(SFGAO_HIDDEN, &attrs)) &&
                    (attrs & SFGAO_HIDDEN) != 0) {
                    continue;
                }
                const std::wstring path = ItemFileSysPath(item.get());
                if (path.empty()) {
                    continue; /* scope filesystem: solo righe apribili */
                }
                if (!seen.insert(path).second) {
                    continue;
                }
                SFGAOF folderAttr = 0;
                const bool isFolder =
                    SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &folderAttr)) &&
                    (folderAttr & SFGAO_FOLDER) != 0;
                const wchar_t* name = wcsrchr(path.c_str(), L'\\');
                name = (name != nullptr) ? name + 1 : path.c_str();
                /* Wordwheel: prefisso-di-parola o sottostringa per OGNI
                 * token della query - lo stesso filtro del walker, quindi
                 * i due backend non possono mai divergere. */
                if (!w7t::startmenu::NameMatchesQuery(name, query)) {
                    continue;
                }
                std::wstring line;
                line.reserve(path.size() + 2);
                line.push_back(isFolder
                    ? L'R'
                    : w7t::startmenu::FileKindFromExtension(name));
                line.push_back(L'|');
                line += path;
                rows.push_back(line);
            } catch (...) {
                /* una riga "impossibile" non ferma la scansione */
            }
        }
        if (rows.size() >= SearchRowBudget::kMaxRows) {
            result = 2;
        }
    } catch (...) {
        rows.clear();
        result = 1;
    }
    } W7T_SEH_CATCH {
        rows.clear();
        result = 1;
    } W7T_SEH_END
    /* Cancellata mentre enumerava? Il chiamante scarta tutto comunque per
     * via della generazione; qui non pubblichiamo nulla. */
    if (generationSource.load(std::memory_order_acquire) != generation) {
        rows.clear();
    }
    return result;
}

} /* namespace startmenu */
} /* namespace w7t */

/* ------------------------------------------------------------------ */
/*  Icona Shell ad alta qualita' (v3.10)                               */
/*                                                                     */
/*  IShellItemImageFactory::GetImage e' la pipeline nativa della shell:  */
/*  ogni tipo di oggetto (file, collegamento, voce del Pannello di       */
/*  controllo, app UWP in AppsFolder, estensioni registrate) rende la    */
/*  SUA icona a 32 bpp nell'esatta dimensione richiesta - qualita'       */
/*  GDI+ senza ridimensionamenti a catena. Se la shell non ha nulla, il  */
/*  frontend ripiega sulla sua pipeline HICON/classica.                  */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_ShellItemIconBitmap(
        const wchar_t* parsingName, int32_t size, uint8_t* pixels,
        int32_t pixelsBytes) {
    if (parsingName == nullptr || parsingName[0] == L'\0' ||
        size < 4 || size > 256 || pixelsBytes < 0) {
        return W7T_ERR_INVALID_ARG;
    }
    const int64_t needed = static_cast<int64_t>(size) * size * 4;
    if (pixels == nullptr) {
        return pixelsBytes == 0
            ? static_cast<int32_t>(needed)
            : W7T_ERR_INVALID_ARG;
    }
    if (static_cast<int64_t>(pixelsBytes) < needed) {
        return W7T_ERR_BUFFER_TOO_SMALL;
    }
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
    try {
        UniqueComInit comInit;
        UniqueCom<IShellItem> item;
        HRESULT hr = SHCreateItemFromParsingName(
            parsingName, nullptr, IID_PPV_ARGS(item.put()));
        if (FAILED(hr) || !item) {
            return W7T_ERR_NOT_FOUND;
        }
        UniqueCom<IShellItemImageFactory> imageFactory;
        hr = item->BindToHandler(nullptr, BHID_ImageFactory,
                                 IID_PPV_ARGS(imageFactory.put()));
        if (FAILED(hr) || !imageFactory) {
            return W7T_ERR_NOT_FOUND;
        }
        SIZE wanted;
        wanted.cx = size;
        wanted.cy = size;
        HBITMAP rawBitmap = nullptr;
        /* ICONONLY: mai anteprime dei documenti al posto delle icone;
         * BIGGERSIZEOK: il raster piu' grande che la shell possiede. */
        hr = imageFactory->GetImage(wanted,
                                    SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK,
                                    &rawBitmap);
        if (FAILED(hr) || rawBitmap == nullptr) {
            return W7T_ERR_NOT_FOUND;
        }
        UniqueGdiObj bitmap;
        bitmap.obj = rawBitmap;

        /* BIGGERSIZEOK puo' restituire una bitmap piu' grande del formato
         * richiesto: in quell'unico caso si ridimensiona con HALFTONE
         * (qualita' GDI+ liscia), altrimenti i byte non tornerebbero. */
        BITMAP rawInfo{};
        if (GetObjectW(rawBitmap, sizeof(BITMAP), &rawInfo) == sizeof(BITMAP) &&
            (rawInfo.bmWidth != size || rawInfo.bmHeight != size)) {
            UniqueDc srcDc;
            srcDc.dc = CreateCompatibleDC(nullptr);
            UniqueDc dstDc;
            dstDc.dc = CreateCompatibleDC(nullptr);
            if (srcDc.dc == nullptr || dstDc.dc == nullptr) {
                return W7T_ERR_NOT_FOUND;
            }
            HBITMAP resizedRaw = CreateCompatibleBitmap(srcDc.dc, size, size);
            if (resizedRaw == nullptr) {
                return W7T_ERR_NOT_FOUND;
            }
            HGDIOBJ oldSrc = SelectObject(srcDc.dc, rawBitmap);
            HGDIOBJ oldDst = SelectObject(dstDc.dc, resizedRaw);
            SetStretchBltMode(dstDc.dc, HALFTONE);
            SetBrushOrgEx(dstDc.dc, 0, 0, nullptr);
            if (oldDst != nullptr && StretchBlt(dstDc.dc, 0, 0, size, size,
                                                srcDc.dc, 0, 0,
                                                rawInfo.bmWidth,
                                                rawInfo.bmHeight,
                                                SRCCOPY)) {
                UniqueGdiObj resized;
                resized.obj = resizedRaw;
                if (oldDst != nullptr) {
                    SelectObject(dstDc.dc, oldDst);
                }
                if (oldSrc != nullptr) {
                    SelectObject(srcDc.dc, oldSrc);
                }
                if (bitmap.obj != nullptr) {
                    DeleteObject(bitmap.obj);
                }
                bitmap.obj = resized.obj;
                resized.obj = nullptr;
                rawBitmap = bitmap.obj;
            } else {
                if (oldDst != nullptr) {
                    SelectObject(dstDc.dc, oldDst);
                }
                if (oldSrc != nullptr) {
                    SelectObject(srcDc.dc, oldSrc);
                }
                DeleteObject(resizedRaw);
            }
        }

        UniqueDc dc;
        dc.dc = CreateCompatibleDC(nullptr);
        if (dc.dc == nullptr) {
            return W7T_ERR_NOT_FOUND;
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = size;
        bmi.bmiHeader.biHeight = -size; /* top-down, come il resto della ABI */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        if (GetDIBits(dc.dc, rawBitmap, 0, static_cast<UINT>(size), pixels,
                      &bmi, DIB_RGB_COLORS) != size) {
            return W7T_ERR_NOT_FOUND;
        }
        /* Pbgra32 del frontend: premoltiplica qui una volta sola. */
        uint8_t* row = pixels;
        const size_t total = static_cast<size_t>(size) *
                             static_cast<size_t>(size);
        for (size_t i = 0; i < total; ++i) {
            uint8_t* px = row + i * 4;
            const uint32_t a = px[3];
            px[0] = static_cast<uint8_t>((px[0] * a + 127u) / 255u);
            px[1] = static_cast<uint8_t>((px[1] * a + 127u) / 255u);
            px[2] = static_cast<uint8_t>((px[2] * a + 127u) / 255u);
        }
        result = W7T_OK;
    } catch (...) {
        result = W7T_ERR_NOT_FOUND;
    }
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}
