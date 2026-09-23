// Win7Taskbar - Core nativo - storage %LOCALAPPDATA%
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "LocalAppDataStore.h"

#include <cstdlib>

namespace w7t {

std::wstring LocalAppDataStore::Dir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::wstring();   /* no improvisation (README) */
    }
    std::wstring dir(buf);
    dir += L"\\Win7Taskbar";
    CreateDirectoryW(dir.c_str(), nullptr);   /* ERROR_EXISTS: fine */
    return dir;
}

std::wstring LocalAppDataStore::File(const std::wstring& name) {
    const std::wstring dir = Dir();
    if (dir.empty()) return dir;
    return dir + L"\\" + name;
}

std::wstring LocalAppDataStore::BackupFile(const std::wstring& name) {
    return File(name + L".bak");
}

std::wstring IniStore::ReadString(const std::wstring& section,
                                  const std::wstring& key,
                                  const std::wstring& defaultValue) const {
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section.c_str(), key.c_str(), defaultValue.c_str(),
                             buf, 1024, m_path.c_str());
    return std::wstring(buf);
}

bool IniStore::WriteString(const std::wstring& section, const std::wstring& key,
                           const std::wstring& value) const {
    return WritePrivateProfileStringW(section.c_str(), key.c_str(),
                                      value.c_str(), m_path.c_str()) != FALSE;
}

int IniStore::ReadInt(const std::wstring& section, const std::wstring& key,
                      int defaultValue) const {
    return static_cast<int>(GetPrivateProfileIntW(section.c_str(), key.c_str(),
                                                  defaultValue, m_path.c_str()));
}

bool IniStore::WriteInt(const std::wstring& section, const std::wstring& key,
                        int value) const {
    return WriteString(section, key, std::to_wstring(value));
}

} // namespace w7t
