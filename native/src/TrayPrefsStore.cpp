/* Win7Taskbar - native core - notification-area icon preferences store
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "TrayPrefsStore.h"
#include "Common.h"

#include <windows.h>
#include <shellapi.h>
#include <cwctype>
#include <cstdlib>
#include <vector>

namespace w7t {

namespace {

/* The key the OLD builds used (HKCU, per-icon DWORD). The migration imports
 * it once and then deletes it; nothing here ever writes registry values.
 * Note: this is OUR key, never Explorer's TrayNotify key - that one is not
 * read for stored choices either (the whole point of this page is that it
 * controls only the Win7Taskbar tray). */
constexpr const wchar_t* kLegacyKeyPath = L"SOFTWARE\\Win7Taskbar\\TrayIconPrefs2";

/* Section names of trayicons.ini. */
constexpr const wchar_t* kSectionSettings = L"[settings]";
constexpr const wchar_t* kSectionIcons = L"[icons]";

int32_t LegacyValueToBehavior(DWORD legacy) {
    /* Old storage: 1 = on the bar, 0 = in the overflow. */
    return legacy != 0 ? kBehaviorShow : kBehaviorNotifyOnly;
}

std::wstring ToLowerAscii(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return s;
}

} /* namespace */

TrayPrefsStore& TrayPrefsStore::Instance() {
    static TrayPrefsStore s_instance;
    return s_instance;
}

std::wstring TrayPrefsStore::FilePath() {
    wchar_t dir[MAX_PATH * 2] = {0};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", dir,
                                        static_cast<DWORD>(sizeof(dir) / sizeof(dir[0])));
    if (len == 0 || len >= sizeof(dir) / sizeof(dir[0])) {
        /* Extremely defensive: no LOCALAPPDATA (some service contexts).
         * USERPROFILE is the next portable folder; the file lives there in
         * that rare case and is still removed with the user profile. */
        wchar_t home[MAX_PATH] = {0};
        DWORD hlen = GetEnvironmentVariableW(L"USERPROFILE", home,
                                              static_cast<DWORD>(sizeof(home) / sizeof(home[0])));
        if (hlen == 0 || hlen >= sizeof(home) / sizeof(home[0])) {
            return std::wstring(L"trayicons.ini"); /* current dir, last resort */
        }
        return std::wstring(home) + L"\\Win7Taskbar\\trayicons.ini";
    }
    return std::wstring(dir) + L"\\Win7Taskbar\\trayicons.ini";
}

void TrayPrefsStore::LoadIfNeeded() {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
}

void TrayPrefsStore::EnsureLoadedLocked() {
    if (m_loaded) {
        return;
    }
    m_loaded = true;   /* once per process: external edits are not watched */

    /* --- 1) the file, if it exists ------------------------------------- */
    const std::wstring path = FilePath();
    std::vector<wchar_t> text;
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(hFile, &size) && size.QuadPart > 0 &&
                size.QuadPart < 4 * 1024 * 1024) {
                text.resize(static_cast<size_t>(size.QuadPart / 2) + 1);
                DWORD read = 0;
                if (ReadFile(hFile, text.data(),
                             static_cast<DWORD>(size.QuadPart), &read, nullptr)) {
                    text[read / 2] = 0;
                    text.resize(read / 2);
                } else {
                    text.clear();
                    LogTagged(L"TRAYPREFS", L"read failed (code %u), starting empty",
                              GetLastError());
                }
            }
            CloseHandle(hFile);
        }
    }

    bool fileHasIcons = false;
    {
        bool inIcons = false;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t eol = text.size();
            for (size_t i = pos; i < text.size(); ++i) {
                if (text[i] == L'\n') { eol = i; break; }
            }
            std::wstring line(text.data() + pos, eol - pos);
            pos = eol + 1;
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            size_t first = line.find_first_not_of(L" \t");
            if (first == std::wstring::npos) {
                continue;
            }
            line.erase(0, first);
            if (line.front() == L';') {
                continue;                        /* comment            */
            }
            if (line.front() == L'[') {
                inIcons = ToLowerAscii(line) == kSectionIcons;
                continue;
            }
            const size_t eq = line.find(L'=');
            if (eq == std::wstring::npos || eq == 0) {
                continue;                        /* total parser: skip junk */
            }
            const std::wstring key = line.substr(0, eq);
            const std::wstring value = line.substr(eq + 1);
            if (inIcons) {
                int32_t behavior = kBehaviorNone;
                wchar_t* end = nullptr;
                const long parsed = wcstol(value.c_str(), &end, 10);
                if (end != value.c_str() && parsed >= 0 && parsed <= 2) {
                    behavior = static_cast<int32_t>(parsed);
                }
                if (behavior != kBehaviorNone) {
                    m_behaviors[key] = behavior;
                    fileHasIcons = true;
                }
            } else {
                const std::wstring low = ToLowerAscii(key);
                if (low == L"alwaysshow") {
                    m_alwaysShow = value.front() != L'0';
                } else if (low.size() > 7 && low.compare(0, 7, L"system.") == 0) {
                    const int32_t kind = _wtoi(low.c_str() + 7);
                    if (kind >= 1 && kind <= 3) {
                        m_systemOff[kind] = value.front() == L'0';
                    }
                }
            }
        }
    }

    /* --- 2) one-time migration from the legacy registry key ---------- */
    /* The key is opened on EVERY load (cheap) and deleted unconditionally:
     * even when the file already has rows, a leftover key must not survive
     * this version - that is the zero-footprint promise. Importing is only
     * meaningful when the file has no icon rows (a build that wrote the
     * file before migrating would otherwise strand the choices); the
     * import + delete is idempotent, which is exactly what a crash between
     * the two steps needs: next start sees no key, nothing to do. */
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyKeyPath, 0,
                          KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            int imported = 0;
            wchar_t valueName[512];
            for (DWORD index = 0; !fileHasIcons; ++index) {
                DWORD nameChars = static_cast<DWORD>(sizeof(valueName) / sizeof(valueName[0]));
                DWORD type = 0, dataSize = 0;
                valueName[0] = 0;
                /* First query for the size, then read: the two-call pattern
                 * RegEnumValueW is documented for. */
                if (RegEnumValueW(hKey, index, valueName, &nameChars, nullptr,
                                  &type, nullptr, &dataSize) != ERROR_SUCCESS) {
                    break;
                }
                if (type != REG_DWORD || dataSize != sizeof(DWORD)) {
                    continue;
                }
                DWORD legacy = 0;
                dataSize = sizeof(legacy);
                if (RegEnumValueW(hKey, index, valueName, &nameChars, nullptr,
                                  &type, reinterpret_cast<BYTE*>(&legacy),
                                  &dataSize) != ERROR_SUCCESS) {
                    continue;
                }
                const std::wstring name(valueName);
                if (!name.empty() && m_behaviors.find(name) == m_behaviors.end()) {
                    m_behaviors[name] = LegacyValueToBehavior(legacy);
                    ++imported;
                }
            }
            RegCloseKey(hKey);

            if (imported > 0) {
                SaveLocked();   /* persist BEFORE dropping the key */
                LogTagged(L"TRAYPREFS",
                          L"migrated %d stored behaviors from the legacy "
                          L"registry key into trayicons.ini", imported);
            }

            /* Zero-footprint self-cleaning: our old key must not outlive
             * the file that replaces it. Deletion failing (permissions,
             * race) is harmless - the next start retries. */
            if (RegDeleteTreeW(HKEY_CURRENT_USER, kLegacyKeyPath) == ERROR_SUCCESS) {
                LogTagged(L"TRAYPREFS",
                          L"legacy registry key removed (zero-footprint)");
            } else {
                LogTagged(L"TRAYPREFS",
                          L"legacy registry key still present (code %u), will retry",
                          GetLastError());
            }
        }
    }
}

void TrayPrefsStore::SaveLocked() {
    /* Full rewrite through a temp file. The file is small (tens of lines);
     * rewriting keeps the format trivial and removes any stale row. */
    const std::wstring path = FilePath();

    /* Ensure the Win7Taskbar folder exists. */
    const size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
    }
    const std::wstring tmp = path + L".tmp";

    std::wstring out;
    out.reserve(256 + m_behaviors.size() * 48);
    out += L"; Win7Taskbar notification-area icon preferences\r\n";
    out += L"; Managed by the program - deleting this file restores every\r\n";
    out += L"; default. Nothing about it is stored in the registry.\r\n";
    out += kSectionSettings;
    out += L"\r\nalwaysshow=";
    out += (m_alwaysShow ? L'1' : L'0');
    out += L"\r\n";
    for (const auto& pair : m_systemOff) {
        if (pair.second) {   /* only the OFF kinds are written */
            out += L"system.";
            out += std::to_wstring(pair.first);
            out += L"=0\r\n";
        }
    }
    out += kSectionIcons;
    out += L"\r\n";
    for (const auto& pair : m_behaviors) {
        out += pair.first;
        out += L'=';
        out += std::to_wstring(pair.second);
        out += L"\r\n";
    }

    HANDLE hFile = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogTagged(L"TRAYPREFS", L"cannot create temp file (code %u)", GetLastError());
        return;
    }
    bool ok = false;
    {
        const DWORD bytes = static_cast<DWORD>(out.size() * sizeof(wchar_t));
        DWORD written = 0;
        ok = WriteFile(hFile, out.c_str(), bytes, &written, nullptr) != FALSE &&
             written == bytes;
    }
    CloseHandle(hFile);
    if (ok) {
        ok = MoveFileExW(tmp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) {
        LogTagged(L"TRAYPREFS", L"save failed (code %u); in-memory values still apply",
                  GetLastError());
        DeleteFileW(tmp.c_str());
        return;
    }
    LogTagged(L"TRAYPREFS", L"saved %u icon behaviors to trayicons.ini",
              static_cast<unsigned>(m_behaviors.size()));
}

int32_t TrayPrefsStore::Behavior(const std::wstring& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    auto it = m_behaviors.find(name);
    return it != m_behaviors.end() ? it->second : kBehaviorNone;
}

bool TrayPrefsStore::Has(const std::wstring& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return m_behaviors.find(name) != m_behaviors.end();
}

void TrayPrefsStore::SetBehavior(const std::wstring& name, int32_t behavior) {
    if (name.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    if (behavior < 0 || behavior > 2) {
        m_behaviors.erase(name);
    } else {
        m_behaviors[name] = behavior;
    }
    SaveLocked();
}

bool TrayPrefsStore::AlwaysShow() {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return m_alwaysShow;
}

void TrayPrefsStore::SetAlwaysShow(bool on) {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    if (m_alwaysShow == on) {
        return;
    }
    m_alwaysShow = on;
    SaveLocked();
}

bool TrayPrefsStore::SystemIconOn(int32_t kind) {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    auto it = m_systemOff.find(kind);
    return it == m_systemOff.end() || !it->second;
}

void TrayPrefsStore::SetSystemIconOn(int32_t kind, bool on) {
    if (kind < 1 || kind > 3) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    /* Same rule SystemIconOn implements, read directly: the public method
     * would take the (non-recursive) mutex again and deadlock. find()
     * only - operator[] would insert a phantom entry here. */
    const auto offIt = m_systemOff.find(kind);
    const bool wasOn = offIt == m_systemOff.end() || !offIt->second;
    if (wasOn == on) {
        return;
    }
    m_systemOff[kind] = !on;
    SaveLocked();
}

void TrayPrefsStore::ClearIconBehaviors() {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    if (m_behaviors.empty()) {
        return;
    }
    const size_t cleared = m_behaviors.size();
    m_behaviors.clear();
    SaveLocked();
    LogTagged(L"TRAYPREFS", L"restored defaults: %u stored behaviors cleared",
              static_cast<unsigned>(cleared));
}

size_t TrayPrefsStore::IconCount() {
    std::lock_guard<std::mutex> lock(m_mutex);
    EnsureLoadedLocked();
    return m_behaviors.size();
}

} /* namespace w7t */
