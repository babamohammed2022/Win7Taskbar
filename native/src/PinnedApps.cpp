// Win7Taskbar - Pinned Application Model
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

#include "PinnedApps.h"
#include "Common.h"
#include "SehGuard.h"
#include "../include/Win7TaskbarCore.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <propsys.h>
#include <propkey.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

namespace w7t {

namespace {

constexpr int kMaxPathW = 520;

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle = nullptr) : m_handle(handle) {}
    ~ScopedHandle() {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HANDLE get() const { return m_handle; }
    HANDLE release() {
        HANDLE handle = m_handle;
        m_handle = nullptr;
        return handle;
    }

private:
    HANDLE m_handle;
};

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
    UniqueFind() noexcept = default;
    explicit UniqueFind(HANDLE h) noexcept : h_(h) {}
    ~UniqueFind() {
        if (h_ != nullptr && h_ != INVALID_HANDLE_VALUE) {
            FindClose(h_);
        }
    }
    UniqueFind(const UniqueFind&) = delete;
    UniqueFind& operator=(const UniqueFind&) = delete;
    HANDLE get() const noexcept { return h_; }
    explicit operator bool() const noexcept {
        return h_ != nullptr && h_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

struct UniquePidl {
    PIDLIST_ABSOLUTE p = nullptr;
    UniquePidl() = default;
    ~UniquePidl() {
        if (p) {
            CoTaskMemFree(p);
        }
    }
    UniquePidl(const UniquePidl&) = delete;
    UniquePidl& operator=(const UniquePidl&) = delete;
};

std::wstring UserPinnedRoot() {
    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base))) {
        return {};
    }
    return std::wstring(base) +
        L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned";
}

std::wstring PinnedFolder() {
    const std::wstring root = UserPinnedRoot();
    if (root.empty()) {
        return {};
    }
    return root + L"\\TaskBar";
}

std::wstring ImplicitPinnedFolder() {
    const std::wstring root = UserPinnedRoot();
    if (root.empty()) {
        return {};
    }
    return root + L"\\ImplicitAppShortcuts";
}

/* PKEY_AppUserModel_ID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 12 */
PROPERTYKEY PKEY_Aumid() {
    PROPERTYKEY k{};
    k.fmtid.Data1 = 0x9F4C2855; k.fmtid.Data2 = 0x9F79; k.fmtid.Data3 = 0x4B39;
    const BYTE d[8] = { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 };
    memcpy(k.fmtid.Data4, d, 8);
    k.pid = 12;
    return k;
}

std::wstring ReadAumid(const std::wstring& lnk) {
    std::wstring out;
    try {
        W7T_SEH_TRY
            UniqueCom<IPropertyStore> ps;
            if (SUCCEEDED(SHGetPropertyStoreFromParsingName(lnk.c_str(), nullptr,
                    GPS_DEFAULT, IID_PPV_ARGS(ps.put())))) {
                PROPERTYKEY key = PKEY_Aumid();
                PROPVARIANT pv{};
                PropVariantInit(&pv);
                if (SUCCEEDED(ps->GetValue(key, &pv)) &&
                    pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
                    out = pv.pwszVal;
                }
                PropVariantClear(&pv);
            }
        W7T_SEH_CATCH
        W7T_SEH_END
    } catch (...) {
        out.clear();
    }
    return out;
}

/* Normalizza il target: env var, virgolette, relativi (contro la working
 * directory del lnk), fullPath, minuscolo. Documentato: i confronti path
 * su Windows sono case-insensitive. */
std::wstring NormalizeTarget(IShellLinkW* link, const std::wstring& raw) {
    if (raw.empty()) return {};
    wchar_t exp[kMaxPathW]{};
    ExpandEnvironmentStringsW(raw.c_str(), exp, kMaxPathW);
    PathUnquoteSpacesW(exp);

    std::wstring full = exp;
    if (!full.empty() && !PathIsRootW(full.c_str())) {
        wchar_t wd[kMaxPathW]{};
        if (link != nullptr &&
            SUCCEEDED(link->GetWorkingDirectory(wd, kMaxPathW)) && wd[0]) {
            wchar_t combined[kMaxPathW]{};
            if (PathCombineW(combined, wd, full.c_str())) {
                full = combined;
            }
        }
        wchar_t abs[kMaxPathW]{};
        if (GetFullPathNameW(full.c_str(), kMaxPathW, abs, nullptr)) {
            full = abs;
        }
    }
    std::transform(full.begin(), full.end(), full.begin(), ::towlower);
    return full;
}

bool IsSpecialTarget(const std::wstring& t) {
    return t.rfind(L"::", 0) == 0 || t.rfind(L"shell:", 0) == 0 ||
           t.rfind(L"com:", 0) == 0 || t.rfind(L"ms-", 0) == 0;
}

bool IsPackagedAumid(const std::wstring& id) {
    return id.find(L'!') != std::wstring::npos;
}

bool TargetAlive(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    if (IsSpecialTarget(path)) {
        return true;
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    /* WindowsApps binaries often return ACCESS_DENIED to GetFileAttributes
     * even though the packaged app is installed. That is not a missing pin. */
    return GetLastError() == ERROR_ACCESS_DENIED;
}

void Log(const wchar_t* fmt, ...) {
    wchar_t line[700];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);
    AppendCoreLog(line);
}

void CollectFolder(const std::wstring& dir,
                   std::vector<PinnedApp>& apps,
                   std::vector<std::wstring>& seenIdentities) {
    if (dir.empty()) {
        return;
    }

    WIN32_FIND_DATAW fd{};
    UniqueFind find(FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd));
    if (!find) {
        return;
    }
    do {
        try {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            const std::wstring name = fd.cFileName;
            const std::wstring lnk = dir + L"\\" + name;

            std::wstring targetRaw, target, aumid, identity, status;
            bool valid = false;
            bool hasIdList = false;

            W7T_SEH_TRY
            UniqueCom<IShellLinkW> link;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.put())))) {
                UniqueCom<IPersistFile> pf;
                if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(pf.put())))) {
                    if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
                        wchar_t t[kMaxPathW]{};
                        WIN32_FIND_DATAW dummy{};
                        link->GetPath(t, kMaxPathW, &dummy, 0);
                        targetRaw = t;

                        if (targetRaw.empty()) {
                            UniquePidl pidl;
                            if (SUCCEEDED(link->GetIDList(&pidl.p)) &&
                                pidl.p != nullptr) {
                                hasIdList = true;
                                wchar_t p2[kMaxPathW]{};
                                if (SHGetPathFromIDListW(pidl.p, p2)) {
                                    targetRaw = p2;
                                }
                            }
                        }
                        if (IsSpecialTarget(targetRaw)) {
                            target = targetRaw;
                        } else {
                            target = NormalizeTarget(link.get(), targetRaw);
                        }
                    }
                }
            }
            W7T_SEH_CATCH
            W7T_SEH_END

            aumid = ReadAumid(lnk);

            if (!aumid.empty()) {
                identity = aumid;
            } else if (!target.empty()) {
                identity = target;
            } else {
                identity = name;
                std::transform(identity.begin(), identity.end(),
                               identity.begin(), ::towlower);
            }

            /* Packaged (UWP) pins often have an AUMID and either no
             * filesystem path (AppsFolder IDList) or a stub/WindowsApps
             * path that GetFileAttributes cannot see. Those are live
             * pins, not missing targets. */
            if (TargetAlive(target) || TargetAlive(targetRaw)) {
                valid = true;
                status = L"VALID";
            } else if (IsPackagedAumid(aumid)) {
                valid = true;
                status = L"VALID (packaged)";
                if (!TargetAlive(target)) {
                    target = L"shell:AppsFolder\\";
                    target += aumid;
                }
            } else if (target.empty() &&
                       (IsSpecialTarget(targetRaw) || hasIdList)) {
                valid = true;
                status = hasIdList ? L"VALID (shell item)"
                                   : L"VALID (special)";
            } else {
                valid = false;
                status = L"INVALID TARGET";
            }

            bool dup = false;
            if (valid) {
                for (const auto& s : seenIdentities) {
                    if (_wcsicmp(s.c_str(), identity.c_str()) == 0) {
                        dup = true;
                        break;
                    }
                }
            }

            Log(L"[Pinned] %s", name.c_str());
            Log(L"  Target: %s",
                targetRaw.empty() ? L"<nessuno>" : targetRaw.c_str());
            Log(L"  Identity: %s", identity.c_str());
            if (!valid) {
                Log(L"  Status: %s", status.c_str());
                Log(L"  Action: IGNORED");
            } else if (dup) {
                Log(L"  Status: DUPLICATE");
                Log(L"  Action: IGNORED");
            } else {
                Log(L"  Status: %s", status.c_str());
                Log(L"  Action: ADDED");
                seenIdentities.push_back(identity);
                std::wstring disp = name;
                if (disp.size() > 4) disp.erase(disp.size() - 4);
                apps.push_back(PinnedApp{ identity, lnk, target, disp });
            }
        } catch (...) {
            Log(L"[Pinned] skipped unreadable shortcut");
        }
    } while (FindNextFileW(find.get(), &fd));
}

} /* namespace */

PinnedApps& PinnedApps::Instance() {
    static PinnedApps s;
    return s;
}

void PinnedApps::Start() {
    if (m_running.exchange(true)) return;

    try {
        Refresh();

        m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stopEvent) {
            m_running = false;
            Log(L"[Pinned] Failed to create stop event");
            return;
        }

        m_watchThread = std::thread([this] { WatcherLoop(); });
    } catch (...) {
        m_running = false;
        if (m_stopEvent) {
            CloseHandle(m_stopEvent);
            m_stopEvent = nullptr;
        }
        Log(L"[Pinned] Exception while starting pinned-app watcher");
    }
}

void PinnedApps::Stop() {
    if (!m_running.exchange(false)) return;

    // Keep the stop event alive and published until the watcher exits.
    // ReadDirectoryChangesW is synchronous, so signalling the event alone
    // cannot wake it. Cancel the thread's pending synchronous I/O first.
    HANDLE stopEvent = m_stopEvent;
    if (stopEvent) {
        SetEvent(stopEvent);
    }

    if (m_watchThread.joinable()) {
        CancelSynchronousIo(m_watchThread.native_handle());
        m_watchThread.join();
    }

    // The watcher no longer uses these handles after join().
    if (m_watchDir) {
        CloseHandle(m_watchDir);
        m_watchDir = nullptr;
    }

    if (stopEvent) {
        CloseHandle(stopEvent);
    }
    m_stopEvent = nullptr;
}

void PinnedApps::WatcherLoop() {
    try {
        std::wstring dir = UserPinnedRoot();
        if (dir.empty()) {
            dir = PinnedFolder();
        }
        if (dir.empty()) return;

        HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        m_watchDir = h;

        char buf[4096];
        while (m_running) {
            DWORD bytes = 0;
            BOOL ok = ReadDirectoryChangesW(h, buf, sizeof(buf), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                &bytes, nullptr, nullptr);
            if (!ok || bytes == 0) {
                if (WaitForSingleObject(m_stopEvent, 500) == WAIT_OBJECT_0) break;
                continue;
            }
            // debounce: pin/unpin arrivano a raffica durante i drag
            if (WaitForSingleObject(m_stopEvent, 500) == WAIT_OBJECT_0) break;
            Refresh();
        }
        CloseHandle(h);
        m_watchDir = nullptr;
    } catch (...) {
        if (m_watchDir) {
            CloseHandle(m_watchDir);
            m_watchDir = nullptr;
        }
        Log(L"[Pinned] watcher stopped after exception");
    }
}

void PinnedApps::Refresh() {
    try {
        std::vector<PinnedApp> apps;
        std::vector<std::wstring> seenIdentities;
        CollectFolder(PinnedFolder(), apps, seenIdentities);
        CollectFolder(ImplicitPinnedFolder(), apps, seenIdentities);

        /* Ordine: enumerazione NTFS della cartella pin (ordine di creazione
         * dei .lnk): approssimazione documentata dell'ordine TaskBand, che
         * vive in un blob binario del registry non parseato qui. MAI ordine
         * alfabetico/per-PID/per-avvio. ImplicitAppShortcuts follow TaskBar. */
        bool changed;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            changed = apps.size() != m_apps.size();
            if (!changed) {
                for (size_t i = 0; i < apps.size(); ++i) {
                    if (_wcsicmp(apps[i].identity.c_str(),
                                 m_apps[i].identity.c_str()) != 0 ||
                        _wcsicmp(apps[i].lnkPath.c_str(),
                                 m_apps[i].lnkPath.c_str()) != 0) {
                        changed = true;
                        break;
                    }
                }
            }
            m_apps = std::move(apps);
        }

        if (changed) {
            CoreState::Instance().QueueEvent(W7T_EVT_PINNED_CHANGED, 0, 0);
        }
    } catch (...) {
        Log(L"[Pinned] Refresh failed");
    }
}

int32_t PinnedApps::GetCount() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return static_cast<int32_t>(m_apps.size());
}

int32_t PinnedApps::CopyTo(W7T_PinnedInfo* buffer, int32_t capacity) {
    if (buffer == nullptr) return W7T_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lk(m_mutex);
    const int32_t n = std::min<int32_t>(
        capacity, static_cast<int32_t>(m_apps.size()));
    for (int32_t i = 0; i < n; ++i) {
        const PinnedApp& a = m_apps[i];
        W7T_PinnedInfo& o = buffer[i];
        wcsncpy_s(o.identity, a.identity.c_str(), _TRUNCATE);
        wcsncpy_s(o.lnkPath, a.lnkPath.c_str(), _TRUNCATE);
        wcsncpy_s(o.target, a.target.c_str(), _TRUNCATE);
        wcsncpy_s(o.displayName, a.displayName.c_str(), _TRUNCATE);
        o.order = i;
        o.reserved = 0;
    }
    return n;
}

} /* namespace w7t */
