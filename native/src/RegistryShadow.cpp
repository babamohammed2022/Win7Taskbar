// Win7Taskbar - Core nativo - shadow delle policy
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "RegistryShadow.h"

#include <cstring>

#include "LocalAppDataStore.h"
#include "RegistryPolicy.h"

namespace w7t {

namespace {

std::wstring ShadowDir() {
    return LocalAppDataStore::File(L"RegistryShadow");
}

std::wstring ShadowFile(const std::wstring& policyName, const wchar_t* suffix) {
    return ShadowDir() + L"\\" + policyName + suffix;
}

/* NtQueryKey(KeyVirtualizationInformation) — see header for why. */
constexpr int kKeyVirtualizationInformation = 6;

struct KeyVirtualizationInformation {
    ULONG isVirtualized;
    ULONG isDefaulted;
};

using NtQueryKeyFn = LONG (WINAPI*)(HANDLE, int, PVOID, ULONG, PULONG);

bool QueryVirtualization(HKEY key) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return false;
    auto ntQueryKey = reinterpret_cast<NtQueryKeyFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQueryKey")));
    if (ntQueryKey == nullptr) return false;
    KeyVirtualizationInformation info = {};
    ULONG written = 0;
    const LONG status = ntQueryKey(key, kKeyVirtualizationInformation,
                                   &info, sizeof(info), &written);
    if (status != 0 /* STATUS_SUCCESS */) {
        return false;   // TODO: verificare: su chiavi non virtualizzabili
                        // lo status e' STATUS_INVALID_PARAMETER
    }
    return info.isVirtualized != 0;
}

} // namespace

bool RegistryShadow::Propose(const std::wstring& policyName, DWORD value) {
    CreateDirectoryW(ShadowDir().c_str(), nullptr);
    const std::wstring file = ShadowFile(policyName, L".proposed");
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, &value, sizeof(value), &written, nullptr);
    CloseHandle(h);
    return ok != FALSE && written == sizeof(value);
}

DWORD RegistryShadow::ReadEffective(const std::wstring& policyName,
                                    DWORD defaultValue) {
    const PolicyDescriptor* desc = RegistryPolicy::FindDescriptor(policyName.c_str());
    if (desc == nullptr) return defaultValue;
    const PolicyValue probe = RegistryPolicy::MakeDword(*desc, 0);
    PolicyValue out;
    if (!RegistryPolicy::ReadValue(probe, &out) || out.data.size() < sizeof(DWORD)) {
        return defaultValue;
    }
    DWORD v = 0;
    memcpy(&v, out.data.data(), sizeof(v));
    return v;
}

bool RegistryShadow::Commit(const std::wstring& policyName) {
    const PolicyDescriptor* desc = RegistryPolicy::FindDescriptor(policyName.c_str());
    if (desc == nullptr) return false;
    const std::wstring file = ShadowFile(policyName, L".proposed");
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD value = 0, read = 0;
    const BOOL ok = ReadFile(h, &value, sizeof(value), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != sizeof(value)) return false;

    /* Apply through the backup-first policy path (README). */
    if (!RegistryPolicy::WriteWithBackup(RegistryPolicy::MakeDword(*desc, value))) {
        return false;
    }
    /* Shadow moves Proposed -> Applied. */
    DeleteFileW(file.c_str());
    const std::wstring applied = ShadowFile(policyName, L".applied");
    HANDLE h2 = CreateFileW(applied.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h2 != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h2, &value, sizeof(value), &written, nullptr);
        CloseHandle(h2);
    }
    return true;
}

bool RegistryShadow::Discard(const std::wstring& policyName) {
    return DeleteFileW(ShadowFile(policyName, L".proposed").c_str()) != FALSE;
}

ShadowState RegistryShadow::State(const std::wstring& policyName) {
    const std::wstring proposed = ShadowFile(policyName, L".proposed");
    if (GetFileAttributesW(proposed.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return ShadowState::Proposed;
    }
    const std::wstring applied = ShadowFile(policyName, L".applied");
    if (GetFileAttributesW(applied.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return ShadowState::Applied;
    }
    return ShadowState::Clean;
}

bool RegistryShadow::IsVirtualized() {
    /* Probe the policy backup root: if THAT is virtualized, every
     * policy write is suspect. Any failure = "not virtualized"
     * (graceful). // TODO: verificare su 8.1/10/11 reali. */
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Win7Taskbar", 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }
    const bool virtualized = QueryVirtualization(key);
    RegCloseKey(key);
    return virtualized;
}

} // namespace w7t
