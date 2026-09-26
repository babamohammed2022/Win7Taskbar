// Win7Taskbar - Core nativo - storage %LOCALAPPDATA% (14)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 14_localappdata_store (snippet integration). Project rule (README):
// the only writable files live under %LOCALAPPDATA%\Win7Taskbar\ — this
// module formalises the pattern already used by TrayPrefsStore
// (trayicons.ini) and by the battery-key backup file: a directory
// helper, a file helper, a backup-file helper and a tiny INI store.
// Ownership note: trayicons.ini stays with TrayPrefsStore; IniStore is
// the GENERIC engine for the other small state files (no duplication:
// one path helper for everyone).

#pragma once

#include <windows.h>

#include <string>

namespace w7t {

class LocalAppDataStore {
public:
    /* %LOCALAPPDATA%\Win7Taskbar (created on first use). Empty on
     * failure (no LOCALAPPDATA: do not improvise a location). */
    static std::wstring Dir();

    /* Dir() + "\\" + name. */
    static std::wstring File(const std::wstring& name);

    /* Dir() + "\\" + name + ".bak" (the backup-file convention used
     * by battery-key-backup.dat). */
    static std::wstring BackupFile(const std::wstring& name);
};

/* Generic INI read/write over an explicit file path (profile APIs).
 * trayicons.ini keeps its owner (TrayPrefsStore); use IniStore for new
 * small state files so they all land in the approved directory. */
class IniStore {
public:
    explicit IniStore(std::wstring path) : m_path(std::move(path)) {}

    const std::wstring& path() const noexcept { return m_path; }

    std::wstring ReadString(const std::wstring& section, const std::wstring& key,
                            const std::wstring& defaultValue) const;
    bool WriteString(const std::wstring& section, const std::wstring& key,
                     const std::wstring& value) const;
    int ReadInt(const std::wstring& section, const std::wstring& key,
                int defaultValue) const;
    bool WriteInt(const std::wstring& section, const std::wstring& key,
                  int value) const;

private:
    std::wstring m_path;
};

} // namespace w7t
