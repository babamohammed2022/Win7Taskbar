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

std::wstring PercentEncodeUtf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        utf8.data(), n, nullptr, nullptr);
    std::wstring o;
    o.reserve(static_cast<std::size_t>(n) * 3);
    static const wchar_t kHex[] = L"0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            o.push_back(static_cast<wchar_t>(c));
        } else if (c == ' ') {
            o.push_back(L'+');
        } else {
            o.push_back(L'%');
            o.push_back(kHex[c >> 4]);
            o.push_back(kHex[c & 0x0F]);
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
     * (SHCreateItemFromParsingName). Only keep hits with a live
     * filesystem path. Cancellation is the generation counter. */
    W7T_SEH_TRY {
    try {
        const std::wstring uri =
            L"search-ms:query=" + PercentEncodeUtf8(query) + L"&crumb=kind:file";
        UniqueCom<IShellItem> folder;
        HRESULT hr = SHCreateItemFromParsingName(uri.c_str(), nullptr,
                                                 IID_IShellItem,
                                                 reinterpret_cast<void**>(folder.put()));
        if (SUCCEEDED(hr) && folder) {
            UniqueCom<IEnumShellItems> enumerator;
            hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_IEnumShellItems,
                                       reinterpret_cast<void**>(enumerator.put()));
            folder.reset();
            if (SUCCEEDED(hr) && enumerator) {
                IShellItem* raw = nullptr;
                while (hits.size() < 32 &&
                       enumerator->Next(1, &raw, nullptr) == S_OK && raw) {
                    UniqueCom<IShellItem> item(raw);
                    raw = nullptr;
                    if (g_fileSearchGeneration.load(std::memory_order_acquire) !=
                        generation) {
                        break;
                    }
                    UniqueCoStr path;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path.p)) &&
                        path.p) {
                        if (path.p[0] != L'\0' &&
                            GetFileAttributesW(path.p) != INVALID_FILE_ATTRIBUTES) {
                            hits.emplace_back(path.p);
                        }
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

bool LaunchPath(const wchar_t* path) {
    if (path == nullptr || path[0] == L'\0') {
        return false;
    }
    try {
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
