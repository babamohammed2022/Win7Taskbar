/*
 * Win7Taskbar - Start Menu program scanner
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

#include "StartMenuScanner.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <objbase.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "propsys.lib")

namespace w7t {
namespace startmenu {
namespace {

struct KnownFolderFree {
    void operator()(wchar_t* p) const {
        if (p) CoTaskMemFree(p);
    }
};

using UniqueKnownFolder = std::unique_ptr<wchar_t, KnownFolderFree>;

bool GetKnownFolder(REFKNOWNFOLDERID id, int csidl, std::wstring& out) {
    PWSTR path = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path);
    UniqueKnownFolder guard(path);
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

std::wstring FoldPath(const std::wstring& s) {
    return FoldAscii(s);
}

std::wstring ResolveShortcut(const std::wstring& lnk) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr) || !link) {
        return {};
    }
    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
    if (FAILED(hr) || !persist) {
        link->Release();
        return {};
    }
    hr = persist->Load(lnk.c_str(), STGM_READ);
    persist->Release();
    if (FAILED(hr)) {
        link->Release();
        return {};
    }
    wchar_t target[MAX_PATH] = {};
    WIN32_FIND_DATAW fd{};
    hr = link->GetPath(target, MAX_PATH, &fd, SLGP_RAWPATH);
    link->Release();
    if (FAILED(hr) || target[0] == L'\0') {
        return {};
    }
    return std::wstring(target);
}

void WalkDirectory(const std::wstring& root, const std::wstring& relative,
                   int source, std::vector<IndexedApp>& out) {
    const std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == L'\0' ||
             (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0'))) {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
            continue;
        }
        const std::wstring name(fd.cFileName);
        if (_wcsicmp(name.c_str(), L"desktop.ini") == 0) {
            continue;
        }
        const std::wstring full = root + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            const std::wstring nextRel =
                relative.empty() ? name : (relative + L"\\" + name);
            WalkDirectory(full, nextRel, source, out);
            continue;
        }
        const std::size_t dot = name.rfind(L'.');
        if (dot == std::wstring::npos) {
            continue;
        }
        std::wstring ext = name.substr(dot);
        for (auto& c : ext) {
            if (c >= L'A' && c <= L'Z') {
                c = static_cast<wchar_t>(c - L'A' + L'a');
            }
        }
        if (ext != L".lnk" && ext != L".url") {
            continue;
        }
        IndexedApp app;
        app.name = name.substr(0, dot);
        app.path = full;
        app.folder = relative;
        app.source = source;
        if (ext == L".lnk") {
            app.target = ResolveShortcut(full);
        }
        out.push_back(std::move(app));
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

void EnumerateUwp(std::vector<IndexedApp>& out) {
    IShellItem* folder = nullptr;
    HRESULT hr = SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DONT_VERIFY,
                                      nullptr, IID_IShellItem,
                                      reinterpret_cast<void**>(&folder));
    if (FAILED(hr) || !folder) {
        return;
    }
    IEnumShellItems* enumerator = nullptr;
    hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_IEnumShellItems,
                               reinterpret_cast<void**>(&enumerator));
    folder->Release();
    if (FAILED(hr) || !enumerator) {
        return;
    }
    IShellItem* item = nullptr;
    while (enumerator->Next(1, &item, nullptr) == S_OK && item) {
        PWSTR display = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display)) &&
            display) {
            IndexedApp app;
            app.name.assign(display);
            CoTaskMemFree(display);
            PWSTR parsing = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,
                                               &parsing)) &&
                parsing) {
                app.path.assign(parsing);
                app.target.assign(parsing);
                CoTaskMemFree(parsing);
            }
            app.source = 2;
            if (!app.name.empty() && !app.path.empty()) {
                out.push_back(std::move(app));
            }
        }
        item->Release();
        item = nullptr;
    }
    enumerator->Release();
}

void DedupeUserWins(std::vector<IndexedApp>& apps) {
    std::stable_sort(apps.begin(), apps.end(),
                     [](const IndexedApp& a, const IndexedApp& b) {
                         const std::wstring ka =
                             FoldPath(a.target.empty() ? a.path : a.target);
                         const std::wstring kb =
                             FoldPath(b.target.empty() ? b.path : b.target);
                         if (ka != kb) {
                             return ka < kb;
                         }
                         if (a.source != b.source) {
                             return a.source < b.source; /* user (0) first */
                         }
                         return a.name < b.name;
                     });
    std::vector<IndexedApp> unique;
    unique.reserve(apps.size());
    std::wstring lastKey;
    for (auto& app : apps) {
        const std::wstring key =
            FoldPath(app.target.empty() ? app.path : app.target);
        if (!unique.empty() && key == lastKey) {
            continue; /* first (user) wins */
        }
        lastKey = key;
        unique.push_back(std::move(app));
    }
    apps.swap(unique);
}

} /* namespace */

bool ScanPrograms(std::vector<IndexedApp>& out, std::wstring& error) {
    out.clear();
    error.clear();

    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = (init == S_OK);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        error = L"CoInitializeEx failed";
        return false;
    }

    std::wstring userPrograms;
    std::wstring commonPrograms;
    if (!GetKnownFolder(FOLDERID_Programs, CSIDL_PROGRAMS, userPrograms)) {
        error = L"SHGetKnownFolderPath(FOLDERID_Programs) failed";
    }
    GetKnownFolder(FOLDERID_CommonPrograms, CSIDL_COMMON_PROGRAMS, commonPrograms);

    std::vector<IndexedApp> apps;
    apps.reserve(512);
    if (!userPrograms.empty()) {
        WalkDirectory(userPrograms, L"", 0, apps);
    }
    if (!commonPrograms.empty()) {
        WalkDirectory(commonPrograms, L"", 1, apps);
    }
    EnumerateUwp(apps);
    DedupeUserWins(apps);
    out.swap(apps);

    if (needUninit) {
        CoUninitialize();
    }
    return true;
}

bool ProgramCache::Refresh(std::wstring& error) {
    std::vector<IndexedApp> next;
    if (!ScanPrograms(next, error)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    apps_.swap(next);
    return true;
}

std::vector<IndexedApp> ProgramCache::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apps_;
}

std::size_t ProgramCache::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apps_.size();
}

} /* namespace startmenu */
} /* namespace w7t */
