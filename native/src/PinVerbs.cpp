/* Win7Taskbar - native core - canonical pin verbs (implementation)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * The .lnk write of the real pin folder lives here and ONLY here: the
 * Jump List pin row calls the same functions, so every part of the bar
 * agrees about what "pinned" means on disk. The model refresh is the
 * PinnedApps folder watcher's job (it queues W7T_EVT_PINNED_CHANGED only
 * when the model actually changed).
 */

#include "PinVerbs.h"
#include "Common.h"   /* w7t::LogTagged */
#include "SehGuard.h" /* W7T_SEH_TRY / W7T_SEH_CATCH / W7T_SEH_END */

#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <string.h>

namespace w7t {
namespace {

/* COM guard: initialises the apartment only when the current thread does
 * not have COM up yet (the callers may already be on a COM-initialized
 * thread); CoUninitialize runs only for the S_OK case. */
struct ComGuard {
    HRESULT hr = E_NOTINITIALIZED;
    ComGuard() { hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComGuard() { if (hr == S_OK) ::CoUninitialize(); }
};

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : m_p(p) {}
    ~ComPtr() { if (m_p) m_p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return m_p; }
    T* operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
    void reset() { if (m_p) { m_p->Release(); m_p = nullptr; } }
private:
    T* m_p = nullptr;
};

/* The real shell pin folder (the same one PinnedApps reads; the same
 * path rule the Jump List was already using). Empty on failure. */
std::wstring PinFolderLocal() {
    wchar_t base[MAX_PATH]{};
    if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base))) {
        return std::wstring();
    }
    return std::wstring(base) +
        L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
}

/* The .lnk name is the (sanitised) display name, as the Jump List did:
 * the shell shows that name on the button. */
std::wstring SanitizeBaseName(const std::wstring& name) {
    std::wstring out = name;
    for (wchar_t& c : out) {
        if (c == L'/' || c == L'\\' || c == L':' || c == L'*' ||
            c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|') {
            c = L'_';
        }
    }
    return out.empty() ? std::wstring(L"App") : out;
}

bool FileExistsW(const std::wstring& path) {
    const DWORD a = ::GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

} /* namespace */

int ParseCanonicalVerb(const wchar_t* verb) {
    if (verb == nullptr) {
        return VerbUnknown;
    }
    /* Ordinal, case-insensitive: no locale surprises, no allocations. */
    auto eq = [verb](const wchar_t* v) {
        return ::CompareStringOrdinal(verb, -1, v, -1, TRUE) == CSTR_EQUAL;
    };
    if (eq(L"taskbarpin")) return VerbTaskbarPin;
    if (eq(L"taskbarunpin")) return VerbTaskbarUnpin;
    if (eq(L"startpin")) return VerbStartPin;
    if (eq(L"startunpin")) return VerbStartUnpin;
    if (eq(L"togglepin")) return VerbTogglePin;
    if (eq(L"customopen")) return VerbCustomOpen;
    if (eq(L"delete")) return VerbDelete;
    if (eq(L"open")) return VerbOpen;
    if (eq(L"runas")) return VerbRunAs;
    return VerbUnknown;
}

bool IsTaskbarPinVerb(int verb) {
    return verb == VerbTaskbarPin || verb == VerbTaskbarUnpin ||
        verb == VerbTogglePin;
}

bool IsStartPinVerb(int verb) {
    return verb == VerbStartPin || verb == VerbStartUnpin;
}

bool DeletePinnedShortcut(const std::wstring& lnkPath) {
    if (lnkPath.empty()) {
        return false;
    }
    if (!FileExistsW(lnkPath)) {
        return false;   /* nothing on disk: no change */
    }
    BOOL ok = FALSE;
    W7T_SEH_TRY {
        ok = ::DeleteFileW(lnkPath.c_str()) ? TRUE : FALSE;
    } W7T_SEH_CATCH {
        ok = FALSE;
    } W7T_SEH_END
    if (!ok) {
        LogTagged(L"PIN", L"delete failed (in use?): path=\"%s\"",
                  lnkPath.c_str());
        return false;
    }
    LogTagged(L"PIN", L"unpinned: path=\"%s\"", lnkPath.c_str());
    return true;
}

bool TogglePinnedApp(const std::wstring& target, const std::wstring& baseName,
                     bool pin, std::wstring* outLnk) {
    if (outLnk != nullptr) {
        *outLnk = std::wstring();
    }
    if (!pin) {
        const std::wstring dir = PinFolderLocal();
        if (dir.empty()) {
            return false;
        }
        const std::wstring lnk = dir + L"\\" + SanitizeBaseName(baseName) + L".lnk";
        if (outLnk != nullptr) {
            *outLnk = lnk;
        }
        return DeletePinnedShortcut(lnk);
    }

    if (target.empty()) {
        return false;
    }
    const std::wstring dir = PinFolderLocal();
    if (dir.empty()) {
        return false;
    }
    const std::wstring lnk = dir + L"\\" + SanitizeBaseName(baseName) + L".lnk";

    if (FileExistsW(lnk)) {
        return false;   /* already pinned: no change */
    }

    bool saved = false;
    W7T_SEH_TRY {
        ::CreateDirectoryW(dir.c_str(), nullptr); /* ignore the result */
        ComGuard com;
        ComPtr<IShellLinkW> link;
        /* The ComPtr's only member is the raw pointer, so its address can
         * be reinterpreted as the void** an interface factory wants (the
         * same technique IID_PPV_ARGS uses). */
        if (SUCCEEDED(::CoCreateInstance(CLSID_ShellLink, nullptr,
                CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                reinterpret_cast<void**>(&link))) &&
            link.get() != nullptr) {
            link->SetPath(target.c_str());
            link->SetIconLocation(target.c_str(), 0);
            ComPtr<IPersistFile> pf;
            if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                    reinterpret_cast<void**>(&pf))) &&
                pf.get() != nullptr) {
                saved = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
            }
        }
    } W7T_SEH_CATCH {
        saved = false;
    } W7T_SEH_END
    if (!saved) {
        LogTagged(L"PIN", L"pin failed to write: path=\"%s\" target=\"%s\"",
                  lnk.c_str(), target.c_str());
        return false;
    }
    if (outLnk != nullptr) {
        *outLnk = lnk;
    }
    LogTagged(L"PIN", L"pinned: path=\"%s\" target=\"%s\"", lnk.c_str(),
              target.c_str());
    return true;
}

} /* namespace w7t */
