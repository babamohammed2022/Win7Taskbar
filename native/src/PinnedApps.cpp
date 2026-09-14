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

std::wstring PinnedFolder() {
    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base))) {
        return {};
    }
    return std::wstring(base) +
        L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
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
    W7T_SEH_TRY
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName(lnk.c_str(), nullptr,
                GPS_DEFAULT, IID_PPV_ARGS(&ps)))) {
            PROPERTYKEY key = PKEY_Aumid();
            PROPVARIANT pv{};
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(key, &pv)) &&
                pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
                out = pv.pwszVal;
            }
            PropVariantClear(&pv);
            ps->Release();
        }
    W7T_SEH_CATCH
    W7T_SEH_END
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

void Log(const wchar_t* fmt, ...) {
    wchar_t line[700];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);
    AppendCoreLog(line);
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

    ScopedHandle stopEvent(m_stopEvent);
    if (stopEvent.get()) SetEvent(stopEvent.get());
    m_stopEvent = nullptr;

    if (m_watchDir) { CloseHandle(m_watchDir); m_watchDir = nullptr; }
    if (m_watchThread.joinable()) m_watchThread.join();
}

void PinnedApps::WatcherLoop() {
    const std::wstring dir = PinnedFolder();
    if (dir.empty()) return;

    HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    m_watchDir = h;

    char buf[4096];
    while (m_running) {
        DWORD bytes = 0;
        BOOL ok = ReadDirectoryChangesW(h, buf, sizeof(buf), FALSE,
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
}

void PinnedApps::Refresh() {
    const std::wstring dir = PinnedFolder();
    std::vector<PinnedApp> apps;
    std::vector<std::wstring> seenIdentities;

    if (!dir.empty()) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring name = fd.cFileName;
                const std::wstring lnk = dir + L"\\" + name;

                std::wstring targetRaw, target, aumid, identity, status;
                bool valid = false;
                bool hasIdList = false;

                IShellLinkW* link = nullptr;
                W7T_SEH_TRY
                if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
                    IPersistFile* pf = nullptr;
                    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
                        if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
                            wchar_t t[kMaxPathW]{};
                            WIN32_FIND_DATAW dummy{};
                            link->GetPath(t, kMaxPathW, &dummy, 0);
                            targetRaw = t;

                            /* v2.26: alcuni collegamenti della shell
                             * (Esplora file pinnato da Windows, voci del
                             * menu Start) puntano a una IDList senza
                             * percorso diretto: GetPath lascia vuoto ma
                             * il lnk e' validissimo. Risolviamo la IDList
                             * e, se non ha forma di percorso, teniamo il
                             * pin come elemento shell valido. */
                            if (targetRaw.empty()) {
                                PIDLIST_ABSOLUTE pidl = nullptr;
                                if (SUCCEEDED(link->GetIDList(&pidl)) &&
                                    pidl != nullptr) {
                                    hasIdList = true;
                                    wchar_t p2[kMaxPathW]{};
                                    if (SHGetPathFromIDListW(pidl, p2)) {
                                        targetRaw = p2;
                                    }
                                    CoTaskMemFree(pidl);
                                }
                            }
                            target = NormalizeTarget(link, targetRaw);
                        }
                        pf->Release();
                    }
                    link->Release();
                    link = nullptr;
                }
                W7T_SEH_CATCH
                W7T_SEH_END

                aumid = ReadAumid(lnk);

                /* Identita' (ordine documentato): 1) AppUserModelID;
                 * 2) percorso eseguibile normalizzato; 3) nome del lnk.
                 * L'AUMID partecipa all'identita', non e' una regola cieca
                 * di dedup: due lnk con lo stesso AUMID MA target diversi
                 * e esistenti restano distinti solo se l'AUMID manca. */
                if (!aumid.empty()) {
                    identity = aumid;
                } else if (!target.empty()) {
                    identity = target;
                } else {
                    identity = name;
                    std::transform(identity.begin(), identity.end(),
                                   identity.begin(), ::towlower);
                }

                /* Validazione: target inesistente = pin NON utilizzabile
                 * (ignorato, MAI cancellato dal disco). */
                if (target.empty()) {
                    valid = IsSpecialTarget(targetRaw) || hasIdList;
                    status = !valid ? L"INVALID TARGET"
                        : (hasIdList ? L"VALID (shell item)"
                                     : L"VALID (special)");
                } else if (GetFileAttributesW(target.c_str()) ==
                           INVALID_FILE_ATTRIBUTES) {
                    valid = false;
                    status = L"INVALID TARGET";
                } else {
                    valid = true;
                    status = L"VALID";
                }

                /* Dedup: stessa identita' = stesso bottone. */
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
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }

    /* Ordine: enumerazione NTFS della cartella pin (ordine di creazione
     * dei .lnk): approssimazione documentata dell'ordine TaskBand, che
     * vive in un blob binario del registry non parseato qui. MAI ordine
     * alfabetico/per-PID/per-avvio. */
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
