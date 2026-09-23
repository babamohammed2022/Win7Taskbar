// Win7Taskbar - Core nativo - policy registry con backup
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "RegistryPolicy.h"

#include <algorithm>
#include <cstring>

namespace w7t {

namespace {

constexpr const wchar_t* kBackupRoot =
    L"Software\\Win7Taskbar\\RegistryBackup";

/* Backup record value names under RegistryBackup\<policy>. */
constexpr const wchar_t* kRecExisted = L"Existed";   /* REG_DWORD 0/1 */
constexpr const wchar_t* kRecType    = L"Type";      /* REG_DWORD */
constexpr const wchar_t* kRecData    = L"Data";      /* REG_BINARY */
constexpr const wchar_t* kRecSubkey  = L"Subkey";    /* REG_SZ */

const PolicyDescriptor kDescriptors[] = {
    /* README compliance section: the four policies are descriptors. */
    { L"Start_JumpListItems",
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartMenu",
      REG_DWORD },
    { L"UseWin32TrayClockExperience",
      L"Software\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",
      REG_DWORD },
    { L"EnableMtcUvc",
      L"Software\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",
      REG_DWORD },
    { L"UseWin32BatteryFlyout",
      L"Software\\Microsoft\\Windows\\CurrentVersion\\ImmersiveShell",
      REG_DWORD },
};

bool ReadRaw(HKEY root, const std::wstring& subkey, const std::wstring& name,
             DWORD* type, std::vector<BYTE>* data) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD size = 0;
    LSTATUS st = RegQueryValueExW(key, name.c_str(), nullptr, type, nullptr, &size);
    if (st != ERROR_SUCCESS) {
        RegCloseKey(key);
        return false;
    }
    data->resize(size);
    st = RegQueryValueExW(key, name.c_str(), nullptr, type, data->data(), &size);
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

bool WriteRaw(HKEY root, const std::wstring& subkey, const std::wstring& name,
              DWORD type, const std::vector<BYTE>& data) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS st = RegSetValueExW(key, name.c_str(), 0, type,
                                      data.data(),
                                      static_cast<DWORD>(data.size()));
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}

} // namespace

const PolicyDescriptor* RegistryPolicy::FindDescriptor(const wchar_t* name) noexcept {
    if (name == nullptr) return nullptr;
    for (const PolicyDescriptor& d : kDescriptors) {
        if (_wcsicmp(d.name, name) == 0) return &d;
    }
    return nullptr;
}

PolicyValue RegistryPolicy::MakeDword(const PolicyDescriptor& desc, DWORD value) {
    PolicyValue v;
    v.subkey = desc.subkey;
    v.valueName = desc.name;
    v.type = desc.type;
    const BYTE* bytes = reinterpret_cast<const BYTE*>(&value);
    v.data.assign(bytes, bytes + sizeof(value));
    return v;
}

bool RegistryPolicy::ReadValue(const PolicyValue& probe, PolicyValue* out) {
    DWORD type = 0;
    std::vector<BYTE> data;
    if (!ReadRaw(HKEY_CURRENT_USER, probe.subkey, probe.valueName, &type, &data)) {
        return false;
    }
    if (out != nullptr) {
        out->subkey = probe.subkey;
        out->valueName = probe.valueName;
        out->type = type;
        out->data = std::move(data);
    }
    return true;
}

bool RegistryPolicy::WriteWithBackup(const PolicyValue& value) {
    /* 1) Read the CURRENT value (may not exist). */
    DWORD prevType = 0;
    std::vector<BYTE> prevData;
    const bool prevExists =
        ReadRaw(HKEY_CURRENT_USER, value.subkey, value.valueName, &prevType, &prevData);

    /* 2) Store the backup FIRST (mandatory, README). */
    const std::wstring recSubkey =
        std::wstring(kBackupRoot) + L"\\" + value.valueName;
    const DWORD existed = prevExists ? 1u : 0u;
    const DWORD type = prevExists ? prevType : value.type;
    if (!WriteRaw(HKEY_CURRENT_USER, recSubkey, kRecExisted, REG_DWORD,
                  std::vector<BYTE>(reinterpret_cast<const BYTE*>(&existed),
                                    reinterpret_cast<const BYTE*>(&existed) + sizeof(existed))) ||
        !WriteRaw(HKEY_CURRENT_USER, recSubkey, kRecType, REG_DWORD,
                  std::vector<BYTE>(reinterpret_cast<const BYTE*>(&type),
                                    reinterpret_cast<const BYTE*>(&type) + sizeof(type))) ||
        !WriteRaw(HKEY_CURRENT_USER, recSubkey, kRecSubkey, REG_SZ,
                  std::vector<BYTE>(
                      reinterpret_cast<const BYTE*>(value.subkey.c_str()),
                      reinterpret_cast<const BYTE*>(value.subkey.c_str()) +
                          (value.subkey.size() + 1) * sizeof(wchar_t)))) {
        return false;   /* no backup -> no write */
    }
    if (prevExists) {
        if (!WriteRaw(HKEY_CURRENT_USER, recSubkey, kRecData, REG_BINARY, prevData)) {
            return false;
        }
    }

    /* 3) Only now write the new value. */
    return WriteRaw(HKEY_CURRENT_USER, value.subkey, value.valueName,
                    value.type, value.data);
}

bool RegistryPolicy::HasBackup(const wchar_t* policyName) {
    if (policyName == nullptr) return false;
    const std::wstring recSubkey = std::wstring(kBackupRoot) + L"\\" + policyName;
    HKEY key = nullptr;
    const bool ok = RegOpenKeyExW(HKEY_CURRENT_USER, recSubkey.c_str(), 0,
                                  KEY_QUERY_VALUE, &key) == ERROR_SUCCESS;
    if (ok) RegCloseKey(key);
    return ok;
}

bool RegistryPolicy::RestoreBackup(const wchar_t* policyName) {
    if (policyName == nullptr) return false;
    const std::wstring recSubkey = std::wstring(kBackupRoot) + L"\\" + policyName;

    DWORD existed = 0, type = 0;
    std::vector<BYTE> existedBytes(sizeof(existed)), typeBytes(sizeof(type));
    DWORD size = static_cast<DWORD>(existedBytes.size());
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, recSubkey.c_str(), 0, KEY_QUERY_VALUE,
                      &key) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS st = RegQueryValueExW(key, kRecExisted, nullptr, nullptr,
                                  existedBytes.data(), &size);
    if (st != ERROR_SUCCESS) { RegCloseKey(key); return false; }
    size = static_cast<DWORD>(typeBytes.size());
    st = RegQueryValueExW(key, kRecType, nullptr, nullptr, typeBytes.data(), &size);
    if (st != ERROR_SUCCESS) { RegCloseKey(key); return false; }

    wchar_t subkeyBuf[512] = {};
    DWORD subkeyBytes = sizeof(subkeyBuf) - sizeof(wchar_t);
    st = RegQueryValueExW(key, kRecSubkey, nullptr, nullptr,
                          reinterpret_cast<LPBYTE>(subkeyBuf), &subkeyBytes);
    std::vector<BYTE> data;
    if (existedBytes[0] != 0) {
        DWORD dataBytes = 0;
        RegQueryValueExW(key, kRecData, nullptr, nullptr, nullptr, &dataBytes);
        data.resize(dataBytes);
        st = RegQueryValueExW(key, kRecData, nullptr, nullptr,
                              data.data(), &dataBytes);
        if (st != ERROR_SUCCESS) { RegCloseKey(key); return false; }
    }
    RegCloseKey(key);
    if (st != ERROR_SUCCESS && existedBytes[0] != 0) return false;

    memcpy(&existed, existedBytes.data(), sizeof(existed));
    memcpy(&type, typeBytes.data(), sizeof(type));

    /* Restore: write the old value back, or delete when it did not
     * exist (README: restore the PREVIOUS state, not "clear"). */
    const std::wstring targetSubkey(subkeyBuf);
    bool ok;
    if (existed != 0) {
        ok = WriteRaw(HKEY_CURRENT_USER, targetSubkey, policyName, type, data);
    } else {
        HKEY target = nullptr;
        ok = RegOpenKeyExW(HKEY_CURRENT_USER, targetSubkey.c_str(), 0,
                           KEY_SET_VALUE, &target) == ERROR_SUCCESS;
        if (ok) {
            ok = RegDeleteValueW(target, policyName) == ERROR_SUCCESS ||
                 GetLastError() == ERROR_FILE_NOT_FOUND;
            RegCloseKey(target);
        }
    }
    if (ok) {
        DeleteBackup(policyName);
    }
    return ok;
}

bool RegistryPolicy::DeleteBackup(const wchar_t* policyName) {
    if (policyName == nullptr) return false;
    const std::wstring recSubkey = std::wstring(kBackupRoot) + L"\\" + policyName;
    /* Delete the record values, then the key itself. */
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, recSubkey.c_str(), 0,
                      KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRecExisted);
        RegDeleteValueW(key, kRecType);
        RegDeleteValueW(key, kRecData);
        RegDeleteValueW(key, kRecSubkey);
        RegCloseKey(key);
    }
    return RegDeleteKeyW(HKEY_CURRENT_USER, recSubkey.c_str()) == ERROR_SUCCESS;
}

} // namespace w7t
