/*
 * Win7Taskbar - Start Menu program scanner
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Dead-shortcut skip is inspired by Open-Shell's public behavior
 * (hide .lnk whose target is gone; do not list leftover installer
 * links). Written from scratch — Open-Shell source is not copied.
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
#include <shellapi.h>
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

class UniqueFind {
public:
    explicit UniqueFind(HANDLE h) noexcept : h_(h) {}
    ~UniqueFind() {
        if (h_ != INVALID_HANDLE_VALUE && h_ != nullptr) {
            FindClose(h_);
        }
    }
    UniqueFind(const UniqueFind&) = delete;
    UniqueFind& operator=(const UniqueFind&) = delete;
    bool valid() const noexcept {
        return h_ != INVALID_HANDLE_VALUE && h_ != nullptr;
    }
    HANDLE get() const noexcept { return h_; }

private:
    HANDLE h_;
};

class UniquePidl {
public:
    UniquePidl() noexcept = default;
    ~UniquePidl() {
        if (p_) {
            CoTaskMemFree(p_);
        }
    }
    UniquePidl(const UniquePidl&) = delete;
    UniquePidl& operator=(const UniquePidl&) = delete;
    PIDLIST_ABSOLUTE* put() noexcept { return &p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

private:
    PIDLIST_ABSOLUTE p_ = nullptr;
};

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

std::wstring ShellDisplayName(const std::wstring& path, const std::wstring& fallback) {
    try {
        SHFILEINFOW info{};
        const DWORD_PTR ok = SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                                            SHGFI_DISPLAYNAME);
        if (ok != 0 && info.szDisplayName[0] != L'\0') {
            return std::wstring(info.szDisplayName);
        }
    } catch (...) {
    }
    return fallback;
}

bool ContainsI(const std::wstring& hay, const wchar_t* needle) {
    if (needle == nullptr || needle[0] == L'\0') {
        return false;
    }
    const size_t n = wcslen(needle);
    if (hay.size() < n) {
        return false;
    }
    for (size_t i = 0; i + n <= hay.size(); ++i) {
        if (_wcsnicmp(hay.c_str() + i, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

/* Retired Windows 7 Start Menu leftovers that still drop .lnk files on
 * later Windows. Names authored here; not copied from Open-Shell. */
bool NameBlacklisted(const std::wstring& name) {
    static const wchar_t* kNames[] = {
        L"Windows Anytime Upgrade",
        L"Windows Easy Transfer",
        L"Windows DVD Maker",
        L"Windows Media Center",
        L"Windows Journal",
        L"Getting Started",
        L"Windows Experience Index",
        L"Windows CardSpace",
        L"Windows Meeting Space",
        L"Windows Ultimate Extras",
        L"Windows Sidebar",
        L"Desktop Gadgets",
        L"Windows Marketplace",
        L"Microsoft Silverlight",
        L"Windows Live Messenger",
        L"Windows Live Mail",
        L"Windows Live Photo Gallery",
        L"Windows Live Writer",
        L"Windows Live Mesh",
        L"Windows Live Movie Maker",
        L"Aggiornamento in qualsiasi momento di Windows",
        L"Trasferimento facile Windows",
        L"DVD Maker di Windows",
        L"Indice prestazioni Windows",
        L"Barra laterale di Windows",
        L"Gadget del desktop",
    };
    for (const wchar_t* s : kNames) {
        if (_wcsicmp(name.c_str(), s) == 0) {
            return true;
        }
    }
    return false;
}

bool JunkTarget(const std::wstring& target) {
    if (target.empty()) {
        return false;
    }
    if (ContainsI(target, L"InstallShield Installation Information")) {
        return true;
    }
    if (ContainsI(target, L"\\Windows\\Installer\\{")) {
        return true;
    }
    return false;
}

bool ShortcutTargetDead(const std::wstring& target) {
    if (target.empty()) {
        return false; /* shell-namespace shortcut */
    }
    if (_wcsnicmp(target.c_str(), L"shell:", 6) == 0) {
        return false;
    }
    if (wcsncmp(target.c_str(), L"::{", 3) == 0) {
        return false;
    }
    if (wcsstr(target.c_str(), L"AppsFolder") != nullptr) {
        return false;
    }
    wchar_t expanded[32768] = {};
    const DWORD n = ExpandEnvironmentStringsW(target.c_str(), expanded,
                                              static_cast<DWORD>(32768));
    const wchar_t* check = (n > 0 && n <= 32768) ? expanded : target.c_str();
    return GetFileAttributesW(check) == INVALID_FILE_ATTRIBUTES;
}

/* Silent Resolve + GetPath. Empty path with a PIDL is a shell item and
 * is kept. No path and no PIDL (or Resolve failed with no path) is a
 * broken advertised shortcut and is dropped. */
bool ReadShortcut(const std::wstring& lnk, std::wstring& target) {
    target.clear();
    try {
        UniqueCom<IShellLinkW> link;
        HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IShellLinkW, reinterpret_cast<void**>(link.put()));
        if (FAILED(hr) || !link) {
            return false;
        }
        UniqueCom<IPersistFile> persist;
        hr = link->QueryInterface(IID_IPersistFile,
                                  reinterpret_cast<void**>(persist.put()));
        if (FAILED(hr) || !persist) {
            return false;
        }
        hr = persist->Load(lnk.c_str(), STGM_READ);
        if (FAILED(hr)) {
            return false;
        }
        const HRESULT resolved = link->Resolve(
            nullptr, SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK);
        wchar_t buf[MAX_PATH] = {};
        WIN32_FIND_DATAW fd{};
        if (SUCCEEDED(link->GetPath(buf, MAX_PATH, &fd, SLGP_RAWPATH)) &&
            buf[0] != L'\0') {
            target.assign(buf);
            return true;
        }
        /* Resolve failed and there is no filesystem path: advertised /
         * leftover installer link. Drop it even if a PIDL remains. */
        if (FAILED(resolved)) {
            return false;
        }
        UniquePidl pidl;
        if (SUCCEEDED(link->GetIDList(pidl.put())) && pidl) {
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

void WalkDirectory(const std::wstring& root, const std::wstring& relative,
                   int source, std::vector<IndexedApp>& out) {
    try {
        const std::wstring pattern = root + L"\\*";
        WIN32_FIND_DATAW fd{};
        UniqueFind find(FindFirstFileW(pattern.c_str(), &fd));
        if (!find.valid()) {
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
            if (NameBlacklisted(app.name) ||
                NameBlacklisted(ShellDisplayName(full, app.name))) {
                continue;
            }
            if (ext == L".lnk") {
                if (!ReadShortcut(full, app.target)) {
                    continue;
                }
                if (ShortcutTargetDead(app.target) || JunkTarget(app.target)) {
                    continue;
                }
            }
            out.push_back(std::move(app));
        } while (FindNextFileW(find.get(), &fd));
    } catch (...) {
    }
}

void EnumerateUwp(std::vector<IndexedApp>& out) {
    try {
        UniqueCom<IShellItem> folder;
        HRESULT hr = SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DONT_VERIFY,
                                          nullptr, IID_IShellItem,
                                          reinterpret_cast<void**>(folder.put()));
        if (FAILED(hr) || !folder) {
            return;
        }
        UniqueCom<IEnumShellItems> enumerator;
        hr = folder->BindToHandler(nullptr, BHID_EnumItems, IID_IEnumShellItems,
                                   reinterpret_cast<void**>(enumerator.put()));
        if (FAILED(hr) || !enumerator) {
            return;
        }
        folder.reset();
        IShellItem* raw = nullptr;
        while (enumerator->Next(1, &raw, nullptr) == S_OK && raw) {
            UniqueCom<IShellItem> item(raw);
            raw = nullptr;
            PWSTR display = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display)) &&
                display) {
                UniqueKnownFolder displayGuard(display);
                IndexedApp app;
                app.name.assign(display);
                PWSTR parsing = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,
                                                   &parsing)) &&
                    parsing) {
                    UniqueKnownFolder parsingGuard(parsing);
                    app.path.assign(parsing);
                    app.target.assign(parsing);
                }
                app.source = 2;
                if (!app.name.empty() && !app.path.empty() &&
                    !NameBlacklisted(app.name)) {
                    out.push_back(std::move(app));
                }
            }
        }
    } catch (...) {
    }
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
    try {
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
    } catch (...) {
        error = L"ScanPrograms exception";
        return false;
    }
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
