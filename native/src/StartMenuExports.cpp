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
#include "SehGuard.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

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

bool HasJumpListFor(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return false;
    }
    IApplicationDocumentLists* lists = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_IApplicationDocumentLists,
                                  reinterpret_cast<void**>(&lists));
    if (FAILED(hr) || lists == nullptr) {
        return false;
    }
    hr = lists->SetAppID(path);
    IObjectArray* recent = nullptr;
    if (SUCCEEDED(hr)) {
        hr = lists->GetList(ADLT_RECENT, 1, IID_IObjectArray,
                            reinterpret_cast<void**>(&recent));
    }
    UINT count = 0;
    if (SUCCEEDED(hr) && recent) {
        recent->GetCount(&count);
        recent->Release();
    }
    lists->Release();
    return count > 0;
}

std::wstring PercentEncode(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size() * 3);
    for (wchar_t c : s) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
            (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' || c == L'.') {
            o.push_back(c);
        } else if (c == L' ') {
            o.push_back(L'+');
        } else {
            static const wchar_t kHex[] = L"0123456789ABCDEF";
            const unsigned v = static_cast<unsigned>(c) & 0xFFu;
            o.push_back(L'%');
            o.push_back(kHex[v >> 4]);
            o.push_back(kHex[v & 0x0F]);
        }
    }
    return o;
}

void FileSearchWorker(std::wstring query, uint32_t generation) {
    std::vector<std::wstring> hits;
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = (init == S_OK);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        std::lock_guard<std::mutex> lock(g_fileMutex);
        if (g_fileSearchGeneration.load() == generation) {
            g_fileHits.clear();
            g_fileReady.store(1, std::memory_order_release);
        }
        return;
    }

    /* Documented Windows Search via the search-ms: shell namespace
     * (SHCreateItemFromParsingName). Cancellation is the generation
     * counter. If the catalog is missing we return no file hits. */
    const std::wstring uri =
        L"search-ms:query=" + PercentEncode(query) + L"&crumb=kind:file";
    IShellItem* folder = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(uri.c_str(), nullptr,
                                             IID_IShellItem,
                                             reinterpret_cast<void**>(&folder));
    if (SUCCEEDED(hr) && folder) {
        IEnumShellItems* enumerator = nullptr;
        hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_IEnumShellItems,
                                   reinterpret_cast<void**>(&enumerator));
        folder->Release();
        if (SUCCEEDED(hr) && enumerator) {
            IShellItem* item = nullptr;
            while (hits.size() < 32 &&
                   enumerator->Next(1, &item, nullptr) == S_OK && item) {
                if (g_fileSearchGeneration.load(std::memory_order_acquire) !=
                    generation) {
                    item->Release();
                    break;
                }
                PWSTR name = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) &&
                    name) {
                    hits.emplace_back(name);
                    CoTaskMemFree(name);
                }
                item->Release();
                item = nullptr;
            }
            enumerator->Release();
        }
    }

    if (needUninit) {
        CoUninitialize();
    }
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

extern "C" W7T_API int32_t W7T_CALL W7T_StartMenuLaunch(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return W7T_ERR_INVALID_ARG;
    }
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        HINSTANCE r = ShellExecuteW(nullptr, L"open", path, nullptr, nullptr,
                                    SW_SHOWNORMAL);
        result = (reinterpret_cast<intptr_t>(r) > 32) ? W7T_OK : W7T_ERR_NOT_FOUND;
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
        return g_fileReady.load() ? 0 : -1;
    }
    std::lock_guard<std::mutex> lock(g_fileMutex);
    std::wstring joined;
    for (std::size_t i = 0; i < g_fileHits.size(); ++i) {
        if (i != 0) {
            joined.push_back(L'\n');
        }
        joined += g_fileHits[i];
    }
    CopyW(buffer, static_cast<std::size_t>(capacityChars), joined);
    return static_cast<int32_t>(g_fileHits.size());
}

extern "C" W7T_API void W7T_CALL W7T_StartMenuFileSearchCancel(void) {
    g_fileSearchGeneration.fetch_add(1);
    g_fileReady.store(0, std::memory_order_release);
}
