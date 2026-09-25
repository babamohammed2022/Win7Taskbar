/*
 * Win7Taskbar - Start Menu C ABI (must live in native/src/*.cpp so
 * check-exports.py sees the W7T_StartMenu* names).
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
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
#include "StartMenuScanner.h"
#include "StartMenuIndex.h"
#include "StartMenuPower.h"
#include "StartMenuSearchFolder.h"
#include "SehGuard.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

/* NOTA: le interfacce COM vanno dichiarate FUORI dai namespace anonimi
 * (stessa regola di ImmersiveFlyouts.h / FlyoutLauncher.cpp): con un
 * namespace anonimo GCC -O2 riduce le chiamate virtuali a
 * __cxa_pure_virtual e il lancio UWP morirebbe in silenzio. */

/* IApplicationActivationManager (documented): la via ufficiale per
 * avviare una app UWP a partire dal suo AppUserModelID. I GUID sono
 * valori di interoperabilita' pubblici (shobjidl.h) ridefiniti qui a
 * mano - come gia' fatto per ImmersiveFlyouts - per non dipendere da
 * quale SDK e' installato sulla macchina di compilazione. */
struct IW7AppActivationManager : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ActivateApplication(
        LPCWSTR appUserModelId, LPCWSTR arguments, DWORD options,
        DWORD* processId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ActivateForFile(
        LPCWSTR appUserModelId, IShellItemArray* itemArray, LPCWSTR verb,
        DWORD* processId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ActivateForProtocol(
        LPCWSTR appUserModelId, IShellItemArray* itemArray,
        DWORD* processId) = 0;
};

namespace {

/* CLSID_ApplicationActivationManager */
const CLSID kClsidAppActivationManager = {
    0x45BA127D, 0x10A8, 0x46EA,
    { 0x8A, 0xB7, 0x56, 0xEA, 0x90, 0x78, 0x94, 0x3C }
};

/* IID_IApplicationActivationManager */
const IID kIidAppActivationManager = {
    0x2E941141, 0x7F97, 0x4756,
    { 0xBA, 0x1D, 0x9D, 0xEC, 0xDE, 0x89, 0x4A, 0x3D }
};

w7t::startmenu::ProgramCache g_cache;
std::atomic<uint32_t> g_queryGeneration{1};
std::atomic<uint32_t> g_fileSearchGeneration{1};

std::mutex g_fileMutex;
std::vector<std::wstring> g_fileHits;
std::atomic<int> g_fileReady{0};

void CopyW(wchar_t* dest, std::size_t cap, const std::wstring& src) {
    if (dest == nullptr || cap == 0) {
        return;
    }
    const std::size_t n = src.size() < (cap - 1) ? src.size() : (cap - 1);
    for (std::size_t i = 0; i < n; ++i) {
        dest[i] = src[i];
    }
    dest[n] = L'\0';
}

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
    T** put() noexcept {
        reset();
        return &p_;
    }
    T* get() const noexcept { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

struct UniqueCoStr {
    PWSTR p = nullptr;
    UniqueCoStr() = default;
    ~UniqueCoStr() {
        if (p) {
            CoTaskMemFree(p);
        }
    }
    UniqueCoStr(const UniqueCoStr&) = delete;
    UniqueCoStr& operator=(const UniqueCoStr&) = delete;
};

struct UniqueHandle {
    HANDLE h = nullptr;
    UniqueHandle() = default;
    ~UniqueHandle() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
};

/* RAII per i PIDL assoluti restituiti da SHParseDisplayName (memoria COM,
 * rilascio con CoTaskMemFree come da documentazione della libreria shell). */
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

/* CoInit per il chiamante (il menu ci arriva da un thread P/Invoke su cui
 * nulla e' garantito). CoUninitialize solo se QUESTO punto l'ha
 * inizializzata. */
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

bool HasJumpListFor(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return false;
    }
    try {
        UniqueCom<IApplicationDocumentLists> lists;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_IApplicationDocumentLists,
                                      reinterpret_cast<void**>(lists.put()));
        if (FAILED(hr) || !lists) {
            return false;
        }
        hr = lists->SetAppID(path);
        UniqueCom<IObjectArray> recent;
        if (SUCCEEDED(hr)) {
            hr = lists->GetList(ADLT_RECENT, 1, IID_IObjectArray,
                                reinterpret_cast<void**>(recent.put()));
        }
        UINT count = 0;
        if (SUCCEEDED(hr) && recent) {
            recent->GetCount(&count);
        }
        return count > 0;
    } catch (...) {
        return false;
    }
}

/* ------------------------------------------------------------------ */
/*  Ricerca file del menu Start (riscritta)                            */
/*                                                                    */
/*  Prima si enumerava l'URI search-ms: come se fosse un IShellItem    */
/*  (SHCreateItemFromParsingName): la shell rifiuta quell'indirizzo    */
/*  come nome di parsing, quindi la sezione file restava SEMPRE        */
/*  vuota. Come fa la scansione classica di Open-Shell (comportamento  */
/*  osservato, codice riscritto da zero: nessuna riga copiata), la     */
/*  ricerca file ora cammina le cartelle note - Documenti, Immagini,   */
/*  Musica, Video, Download, Desktop - con FindFirstFileExW, matcha    */
/*  il nome con lo stesso matcher tokenizzato della ricerca            */
/*  programmi e classifica l'estensione in quattro famiglie            */
/*  (documenti/immagini/musica/video) piu' "altri". Ogni riga emessa   */
/*  e' "K|percorso" dove K e' D/P/M/V/F; una riga "T" segnala che il   */
/*  risultato e' stato troncato dal budget. La cancellazione resta il  */
/*  contatore di generazione.                                          */
/* ------------------------------------------------------------------ */

/* v3.10: FileKindFromExtension vive ora in StartMenuIndex.cpp (condivisa
 * dal backend Shell Search Folder): classificazione identica ovunque. */

bool GetKnownFolderPath(REFKNOWNFOLDERID id, int csidl, std::wstring& out) {
    PWSTR path = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path);
    UniqueCoStr guard;
    guard.p = path;
    if (SUCCEEDED(hr) && path && path[0] != L'\0') {
        out.assign(path);
        return true;
    }
    wchar_t buf[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf)) &&
        buf[0] != L'\0') {
        out.assign(buf);
        return true;
    }
    return false;
}

/* RAII per le chi di ricerca file: niente FindClose saltati sulle
 * centinaia di return/catch che una scansione reale puo' prendere. */
struct UniqueFindCloser {
    HANDLE h = nullptr;
    explicit UniqueFindCloser(HANDLE raw) noexcept : h(raw) {}
    ~UniqueFindCloser() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            FindClose(h);
        }
    }
    UniqueFindCloser(const UniqueFindCloser&) = delete;
    UniqueFindCloser& operator=(const UniqueFindCloser&) = delete;
};

struct FileSearchBudget {
    const ULONGLONG started;
    size_t visited = 0;
    size_t maxHits = 48;
    bool timedOut = false;
    static const size_t kMaxVisited = 131072;
    static const ULONGLONG kMaxMs = 8000;

    FileSearchBudget() : started(GetTickCount64()) {}

    /* true = la scansione continua; false = budget finito o cancellata. */
    bool Step(uint32_t generation) {
        if ((++visited & 0xFFu) == 0) {
            if (GetTickCount64() - started > kMaxMs) {
                timedOut = true;
                return false;
            }
            if (g_fileSearchGeneration.load(std::memory_order_acquire) !=
                generation) {
                return false;
            }
        }
        return visited < kMaxVisited;
    }
};

struct FileSearchCaseLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    }
};

void WalkFilesForQuery(const std::wstring& folder, const std::wstring& query,
                       int depth, uint32_t generation, FileSearchBudget& budget,
                       std::set<std::wstring, FileSearchCaseLess>& seen,
                       std::vector<std::wstring>& hits) {
    if (depth > 12 || hits.size() >= budget.maxHits || folder.empty()) {
        return;
    }
    if (!budget.Step(generation)) {
        return;
    }
    /* Prefisso \\?\ per i percorsi oltre MAX_PATH: il menu non deve
     * dipendere dalla lunghezza della cartella Documenti dell'utente. */
    std::wstring prefix;
    if (folder.size() > 200 && folder[1] == L':' && folder[0] != L'\\') {
        prefix = L"\\\\?\\" + folder;
    } else {
        prefix = folder;
    }
    const std::wstring pattern = prefix + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE raw = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                  FindExSearchNameMatch, nullptr,
                                  FIND_FIRST_EX_LARGE_FETCH);
    if (raw == INVALID_HANDLE_VALUE || raw == nullptr) {
        return;
    }
    UniqueFindCloser handle(raw);
    do {
        const wchar_t* name = fd.cFileName;
        if (name[0] == L'.' &&
            (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
            continue;
        }
        /* Nascoste/sistema mai nei risultati, come fa la ricerca verde. */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* v3.10: anche le cartelle che corrispondono alla query
             * diventano un risultato (riga 'R', sezione Cartelle del
             * frontend): la ricerca del menu di Windows 7 mostra cartelle
             * e documenti insieme; qui restano in una sezione pulita. */
            if (hits.size() < budget.maxHits) {
                bool folderMatch = false;
                try {
                    folderMatch = w7t::startmenu::NameMatchesQuery(name, query);
                } catch (...) {
                    folderMatch = false;
                }
                if (folderMatch) {
                    std::wstring full = folder + L"\\" + name;
                    if (seen.insert(full).second) {
                        hits.push_back(std::wstring(L"R|") + full);
                    }
                }
            }
            /* Le giunzioni (Documents\My Pictures ecc.) porta-via i due
             * doppioni e i cicli: le cartelle vere le copre il root suo. */
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                std::wstring sub = folder + L"\\" + name;
                WalkFilesForQuery(sub, query, depth + 1, generation, budget,
                                  seen, hits);
                if (hits.size() >= budget.maxHits) {
                    break;
                }
            }
            continue;
        }
        if (!budget.Step(generation)) {
            break;
        }
        /* Match sul nome completo (l'estensione fa parte dei token, come
         * nella ricerca verde: "report.pdf" funziona, "report" anche,
         * il punto e' uno dei separatori di parola) con il matcher
         * tokenizzato condiviso con la ricerca programmi. */
        bool match = false;
        try {
            match = w7t::startmenu::NameMatchesQuery(name, query);
        } catch (...) {
            match = false; /* un nome "impossibile" non ferma la ricerca */
        }
        if (!match) {
            continue;
        }
        std::wstring full = folder + L"\\" + name;
        if (!seen.insert(full).second) {
            continue;
        }
        const wchar_t kind = w7t::startmenu::FileKindFromExtension(name);
        std::wstring line;
        line.reserve(full.size() + 2);
        line.push_back(kind);
        line.push_back(L'|');
        line += full;
        hits.push_back(line);
        if (hits.size() >= budget.maxHits) {
            break;
        }
    } while (FindNextFileW(handle.h, &fd));
}

void FileSearchWorker(std::wstring query, uint32_t generation) {
    /* v3.10: prima il backend Windows Search / Shell Search Folder
     * (documentato; pipeline ricostruita dal comportamento osservato di
     * SearchFolder.dll, scritta da zero: ISearchFolderItemFactory con
     * scope = cartelle note e condizione wordwheel, risultati IShellItem).
     * Se WSearch non gira, l'interfaccia manca o la query e' troppo corta
     * si ripiega sul walker classico, limitato alle stesse radici (mai
     * una scansione indiscriminata di tutto il disco). */
    std::vector<std::wstring> hits;
    W7T_SEH_TRY {
    try {
        const int backend = w7t::startmenu::CollectShellSearchRows(
            query, generation, g_fileSearchGeneration, hits);
        const bool stale = g_fileSearchGeneration.load(
            std::memory_order_acquire) != generation;
        /* Backend fallito, oppure riuscito ma a mani vuote (radici non
         * indicizzate dal servizio, es. Downloads): il walker classico
         * resta il passo che non perde mai un risultato. */
        if (backend != 1 && !hits.empty() && !stale) {
            auto kindRank2 = [](const std::wstring& line) {
                static const wchar_t kOrder2[] = L"DPMVRF";
                const wchar_t* pp = wcschr(kOrder2, line.empty() ? L'F' : line[0]);
                return pp == nullptr ? 99 : static_cast<int>(pp - kOrder2);
            };
            std::stable_sort(hits.begin(), hits.end(),
                             [&](const std::wstring& a, const std::wstring& b) {
                                 return kindRank2(a) < kindRank2(b);
                             });
            if (backend == 2) {
                hits.emplace_back(L"T");
            }
        } else {
            hits.clear();
            {
        /* Documenti, Immagini, Musica, Video: le stesse librerie del menu
         * di Windows 7, piu' Download e Desktop dove poggiano i file di
         * uso corrente (le radici della scansione classica di Open-Shell,
         * comportamento osservato e riprodotto da zero). */
        static const struct {
            const GUID* id; int csidl;
        } kRoots[] = {
            { &FOLDERID_Documents, CSIDL_PERSONAL },
            { &FOLDERID_Pictures, CSIDL_MYPICTURES },
            { &FOLDERID_Music, CSIDL_MYMUSIC },
            { &FOLDERID_Videos, CSIDL_MYVIDEO },
            { &FOLDERID_Downloads, 0 },
            { &FOLDERID_Desktop, CSIDL_DESKTOPDIRECTORY },
        };
        std::vector<std::wstring> roots;
        roots.reserve(_countof(kRoots));
        std::set<std::wstring, FileSearchCaseLess> seenRoots;
        for (const auto& def : kRoots) {
            std::wstring root;
            if (GetKnownFolderPath(*def.id, def.csidl, root) &&
                seenRoots.insert(root).second) {
                roots.push_back(root);
            }
        }
        if (!roots.empty()) {
            FileSearchBudget budget;
            std::set<std::wstring, FileSearchCaseLess> seen;
            for (const std::wstring& root : roots) {
                if (hits.size() >= budget.maxHits || budget.timedOut) {
                    break;
                }
                WalkFilesForQuery(root, query, 0, generation, budget, seen, hits);
            }
            /* Ordinamento stabile per famiglia D -> P -> M -> V -> R -> F,
             * come le sezioni della ricerca di Windows 7/Open-Shell:
             * dentro la famiglia l'ordine di scoperta e' stabile. */
            auto kindRank = [](const std::wstring& line) {
                static const wchar_t kOrder[] = L"DPMVRF";
                const wchar_t* p = wcschr(kOrder, line.empty() ? L'F' : line[0]);
                return p == nullptr ? 99 : static_cast<int>(p - kOrder);
            };
            std::stable_sort(hits.begin(), hits.end(),
                             [&](const std::wstring& a, const std::wstring& b) {
                                 return kindRank(a) < kindRank(b);
                             });
            /* Il marker "T" informa il frontend che la ricerca e' stata
             * chiusa dal budget: lui mostrera' "Visualizza altri risultati". */
            if (hits.size() >= budget.maxHits || budget.timedOut) {
                hits.emplace_back(L"T");
            }
        }
            }
            }
    } catch (...) {
        hits.clear();
    }
    } W7T_SEH_CATCH {
        hits.clear();
    } W7T_SEH_END

    if (g_fileSearchGeneration.load(std::memory_order_acquire) != generation) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_fileMutex);
    g_fileHits.swap(hits);
    g_fileReady.store(1, std::memory_order_release);
}

} /* namespace */

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuScan(void) {
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        std::wstring error;
        result = g_cache.Refresh(error) ? W7T_OK : W7T_ERR_NOT_FOUND;
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuGetCount(void) {
    int32_t n = 0;
    W7T_SEH_TRY {
        n = static_cast<int32_t>(g_cache.Count());
    } W7T_SEH_CATCH {
        n = 0;
    } W7T_SEH_END
    return n;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuGetEntry(int32_t index,
                                                         W7T_StartMenuEntry* out) {
    if (out == nullptr || index < 0) {
        return W7T_ERR_INVALID_ARG;
    }
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        const auto apps = g_cache.Snapshot();
        if (static_cast<std::size_t>(index) >= apps.size()) {
            result = W7T_ERR_NOT_FOUND;
        } else {
            const auto& app = apps[static_cast<std::size_t>(index)];
            CopyW(out->name, W7T_MAX_TITLE, app.name);
            CopyW(out->path, W7T_MAX_PATH_, app.path);
            CopyW(out->target, W7T_MAX_PATH_, app.target);
            CopyW(out->folder, W7T_MAX_PATH_, app.folder);
            out->source = app.source;
            out->usageCount = app.usageCount;
            result = W7T_OK;
        }
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuQuery(
        const wchar_t* query, int32_t* indices, int32_t capacity) {
    if (indices == nullptr || capacity <= 0) {
        return W7T_ERR_INVALID_ARG;
    }
    int32_t written = 0;
    W7T_SEH_TRY {
        const uint32_t gen = g_queryGeneration.fetch_add(1) + 1;
        const auto apps = g_cache.Snapshot();
        std::vector<w7t::startmenu::RankedHit> hits;
        const bool ok = w7t::startmenu::QueryIndex(
            apps, query ? query : L"", hits, &gen, gen,
            static_cast<std::size_t>(capacity));
        if (ok) {
            for (const auto& hit : hits) {
                if (written >= capacity) {
                    break;
                }
                indices[written++] = hit.index;
            }
        }
    } W7T_SEH_CATCH {
        written = 0;
    } W7T_SEH_END
    return written;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuPower(int32_t action) {
    int32_t result = W7T_ERR_INVALID_ARG;
    W7T_SEH_TRY {
        std::wstring error;
        const auto kind = static_cast<w7t::startmenu::PowerAction>(action);
        result = w7t::startmenu::RunPowerAction(kind, error)
                     ? W7T_OK
                     : W7T_ERR_NOT_FOUND;
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}

namespace {

bool ShellExec(const wchar_t* file, const wchar_t* parameters, DWORD extraMask) {
    try {
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_NOASYNC | extraMask;
        info.lpVerb = nullptr;
        info.lpFile = file;
        info.lpParameters = parameters;
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) {
            UniqueHandle proc;
            proc.h = info.hProcess;
            info.hProcess = nullptr;
            return true;
        }
        info.lpVerb = L"open";
        if (ShellExecuteExW(&info)) {
            UniqueHandle proc;
            proc.h = info.hProcess;
            info.hProcess = nullptr;
            return true;
        }
    } catch (...) {
    }
    return false;
}

/* Lancio UWP documentato. Le voci AppsFolder del menu arrivano come nome
 * di parsing, per esempio
 *   ::{4234D49B-0245-4DF3-B780-3893943456E9}\Pacchetto_x64!AppId
 * o   shell:AppsFolder\Pacchetto_x64!AppId
 *
 * L'AppUserModelID e' il frammento dopo l'ultimo backslash ("Pkg!App").
 * IApplicationActivationManager::ActivateApplication e' l'API pubblica
 * pensata apposta per questo (la stessa usata da RetroBar/ManagedShell),
 * molto piu' affidabile di explorer.exe su un nome di parsing, che su
 * alcune build apriva la cartella AppsFolder invece della app. RAII sui
 * puntatori COM, SEH attorno alla COM, e ricaduta su explorer.exe
 * "shell:AppsFolder\..." se l'attivatore non c'e' (Windows < 8). */
/* Rileva le voci UWP: compare come "shell:AppsFolder\Pkg!App" oppure come
 * nome di parsing "::{4234D49B-0245-4DF3-B780-3893943456E9}\Pkg!App"
 * (e' il CLSID pubblico di AppsFolder; nello scanner del menu si prende
 * questa seconda forma). Cercare solo la stringa "AppsFolder" sbagliava
 * il secondo caso: il lancio non prendeva mai la via dell'attivatore. */
/* Apertura universale tramite la shell (documentata): SHParseDisplayName
 * digerisce percorsi normali, "shell:..." e i nomi canonici ":: {CLSID}"
 * - usati dalle voci del Pannello di controllo e da AppsFolder - e
 * ShellExecuteEx con SEE_MASK_IDLIST | INVOKEIDLIST chiede alla shell
 * stessa di eseguire il verbo predefinito. E' come Explorer apre i suoi
 * elementi, quindi funziona per .cpl, voci canoniche, collegamenti e
 * anche per app UWP sulle quali l'attivatore non puo' agire (fallback).
 * Prima questa rotta mancava: "Pannello di controllo" e app simili dal
 * catalogo impostazioni non si aprivano proprio. */
bool ShellOpenByPidl(const wchar_t* name) {
    if (name == nullptr || name[0] == L'\0') {
        return false;
    }
    bool opened = false;
    W7T_SEH_TRY {
    try {
        UniqueComInit comInit;
        PIDLIST_ABSOLUTE pidl = nullptr;
        SFGAOF attrs = 0;
        if (FAILED(SHParseDisplayName(name, nullptr, &pidl, 0, &attrs)) ||
            pidl == nullptr) {
            return false;
        }
        UniquePidl own;
        own.p = pidl;
        pidl = nullptr;
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_IDLIST | SEE_MASK_INVOKEIDLIST |
                     SEE_MASK_FLAG_DDEWAIT | SEE_MASK_NOASYNC;
        info.lpIDList = own.p;
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) {
            UniqueHandle proc;
            proc.h = info.hProcess;
            info.hProcess = nullptr;
            opened = true;
        }
    } catch (...) {
        opened = false;
    }
    } W7T_SEH_CATCH {
        opened = false;
    } W7T_SEH_END
    return opened;
}

bool IsAppsFolderParsingName(const wchar_t* path) {
    if (path == nullptr) {
        return false;
    }
    if (_wcsnicmp(path, L"shell:AppsFolder", 16) == 0) {
        return true;
    }
    if (_wcsnicmp(path, L"::{4234D49B-0245-4DF3-B780-3893943456E9}", 39) == 0) {
        return true;
    }
    return wcsstr(path, L"AppsFolder") != nullptr;
}

bool ExtractAppUserModelId(const wchar_t* path, std::wstring& out) {
    out.clear();
    if (!IsAppsFolderParsingName(path)) {
        return false;
    }
    const wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr || slash[1] == L'\0') {
        slash = wcsrchr(path, L'/');
    }
    const wchar_t* id = (slash == nullptr) ? path : slash + 1;
    /* Un AppUserModelID valido ha SEMPRE la forma Pacchetto_Famiglia!App. */
    if (wcschr(id, L'!') == nullptr) {
        return false;
    }
    out.assign(id);
    return !out.empty();
}

/* RAII per CoInitializeEx: IApplicationActivationManager e' un COM su
 * server locale, quindi la macchina deve essere inizializzata sul thread
 * chiamante (il menu ci arriva da un thread P/Invoke su cui nulla e'
 * garantito). CoUninitialize solo se QUESTO punto l'ha inizializzata. */
bool LaunchUwpApp(const std::wstring& appUserModelId) {
    if (appUserModelId.empty()) {
        return false;
    }
    bool launched = false;
    W7T_SEH_TRY {
    try {
        UniqueComInit comInit;
        UniqueCom<IW7AppActivationManager> activator;
        const HRESULT created = CoCreateInstance(
            kClsidAppActivationManager, nullptr,
            CLSCTX_LOCAL_SERVER | CLSCTX_INPROC_SERVER,
            kIidAppActivationManager,
            reinterpret_cast<void**>(activator.put()));
        if (SUCCEEDED(created) && activator) {
            DWORD pid = 0;
            if (SUCCEEDED(activator->ActivateApplication(
                    appUserModelId.c_str(), nullptr, 0 /* AO_NONE */, &pid))) {
                launched = true;
            }
        }
    } catch (...) {
        launched = false;
    }
    } W7T_SEH_CATCH {
        launched = false;
    } W7T_SEH_END
    return launched;
}

bool LaunchPath(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return false;
    }
    try {
        /* 0. AppsFolder -> attivatore UWP documentato. E' il percorso piu'
         * sicuro per le app UWP dalla ricerca del menu Start. */
        std::wstring aumid;
        if (ExtractAppUserModelId(path, aumid)) {
            if (LaunchUwpApp(aumid)) {
                return true;
            }
            std::wstring shellForm = L"shell:AppsFolder\\" + aumid;
            if (ShellExec(L"explorer.exe", shellForm.c_str(), 0)) {
                return true;
            }
            /* se explorer fallisce si prosegue coi percorsi generici */
        }
        /* v3.10: rotta PIDL documentata PRIMA dei tentativi letterali:
         * sblocca Pannello di controllo (nomi canonici "shell:::" e
         * "::{CLSID}\..."), i collegamenti indiretti e ogni oggetto shell. */
        if (ShellOpenByPidl(path)) {
            return true;
        }
        if (ShellExec(path, nullptr, SEE_MASK_DOENVSUBST | SEE_MASK_INVOKEIDLIST)) {
            return true;
        }
        if (ShellExec(path, nullptr, SEE_MASK_DOENVSUBST)) {
            return true;
        }
        /* Documented explorer.exe parsing names: shell:, ::{CLSID}, AppsFolder. */
        if (_wcsnicmp(path, L"shell:", 6) == 0 ||
            wcsncmp(path, L"::{", 3) == 0 ||
            wcsstr(path, L"AppsFolder") != nullptr) {
            if (ShellExec(L"explorer.exe", path, 0)) {
                return true;
            }
        }
        std::wstring quoted = L"\"";
        quoted += path;
        quoted += L"\"";
        if (ShellExec(L"explorer.exe", quoted.c_str(), 0)) {
            return true;
        }
        HINSTANCE r = ShellExecuteW(nullptr, L"open", path, nullptr, nullptr,
                                    SW_SHOWNORMAL);
        return reinterpret_cast<intptr_t>(r) > 32;
    } catch (...) {
        return false;
    }
}

} /* namespace */

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuLaunch(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return W7T_ERR_INVALID_ARG;
    }
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        result = LaunchPath(path) ? W7T_OK : W7T_ERR_NOT_FOUND;
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuHasJumpList(const wchar_t* path) {
    int32_t result = 0;
    W7T_SEH_TRY {
        result = HasJumpListFor(path) ? 1 : 0;
    } W7T_SEH_CATCH {
        result = 0;
    } W7T_SEH_END
    return result;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuFileSearchStart(const wchar_t* query) {
    if (query == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    const uint32_t gen = g_fileSearchGeneration.fetch_add(1) + 1;
    g_fileReady.store(0, std::memory_order_release);
    std::wstring q(query);
    try {
        std::thread([q, gen]() { FileSearchWorker(q, gen); }).detach();
    } catch (...) {
        return W7T_ERR_NOT_FOUND;
    }
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuFileSearchPoll(
        wchar_t* buffer, int32_t capacityChars) {
    if (g_fileReady.load(std::memory_order_acquire) == 0) {
        return -1; /* not ready */
    }
    if (buffer == nullptr || capacityChars <= 1) {
        /* Senza buffer niente dati: la chiamata e' solo conoscitiva.
         * Prima della correzione questo ramo era INVERTITO (ritornava -1
         * proprio quando i dati erano pronti). */
        return 0;
    }
    std::vector<std::wstring> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_fileMutex);
        snapshot = g_fileHits;
    }
    /* Budget di caratteri deciso qui: ogni riga che non ci sta intera
     * viene omessa (mai mezzi percorsi in coda al buffer del frontend). */
    const std::size_t charBudget =
        static_cast<std::size_t>(capacityChars) - 1;
    std::wstring joined;
    int32_t written = 0;
    for (const std::wstring& line : snapshot) {
        const std::size_t add = line.size() + (written == 0 ? 0 : 1);
        if (joined.size() + add > charBudget) {
            break;
        }
        if (written != 0) {
            joined.push_back(L'\n');
        }
        joined += line;
        ++written;
    }
    CopyW(buffer, static_cast<std::size_t>(capacityChars), joined);
    return written;
}

extern "C" W7T_API void W7T_CALL W7T_StartMenuFileSearchCancel(void) {
    g_fileSearchGeneration.fetch_add(1);
    g_fileReady.store(0, std::memory_order_release);
}
