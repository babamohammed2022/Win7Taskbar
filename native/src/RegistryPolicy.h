// Win7Taskbar - Core nativo - policy registry con backup (13)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 13_registry_policy (snippet integration). Project rule (README):
// EVERY registry write goes through a backup-first path — the previous
// value is copied under HKCU\Software\Win7Taskbar\RegistryBackup\<policy>
// before the write lands, and restore_backup/has_backup make the backup
// discoverable. Windows Registry Virtualization is NEVER used or relied
// upon (see RegistryShadow.h for the detection guard).
//
// Policy descriptors (README compliance section): the values
// Start_JumpListItems, UseWin32TrayClockExperience, EnableMtcUvc and
// UseWin32BatteryFlyout are handled as POLICY descriptors through this
// module — never by direct RegSetValueExW call sites. The recovery
// paths that must stay allocation-free (the SEH crash filter in
// TrayService.cpp) keep their raw Reg* writes BY DESIGN and are
// documented as the single exception.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace w7t {

/* One registry value as data: subkey is relative to HKCU. */
struct PolicyValue {
    std::wstring subkey;
    std::wstring valueName;
    DWORD type = REG_DWORD;
    std::vector<BYTE> data;
};

/* Named policy descriptor (the README four + room for more). */
struct PolicyDescriptor {
    const wchar_t* name;        /* registry value name */
    const wchar_t* subkey;      /* relative to HKCU */
    DWORD type;
};

class RegistryPolicy {
public:
    /* Descriptor lookup by value name; nullptr when unknown. */
    static const PolicyDescriptor* FindDescriptor(const wchar_t* name) noexcept;

    /* Builds a REG_DWORD PolicyValue from a descriptor. */
    static PolicyValue MakeDword(const PolicyDescriptor& desc, DWORD value);

    /* Backs up the CURRENT value under
     * HKCU\Software\Win7Taskbar\RegistryBackup\<value.valueName>, then
     * writes `value`. Returns false and writes nothing when the backup
     * cannot be stored (backup-first is mandatory). */
    static bool WriteWithBackup(const PolicyValue& value);

    /* True when a backup exists for that policy name. */
    static bool HasBackup(const wchar_t* policyName);

    /* Restores the backed-up value (or deletes the value when it did
     * not exist before) and removes the backup afterwards. */
    static bool RestoreBackup(const wchar_t* policyName);

    /* Drops the backup only (after a successful external restore). */
    static bool DeleteBackup(const wchar_t* policyName);

    /* Reads a value (any policy path); false when absent. */
    static bool ReadValue(const PolicyValue& probe, PolicyValue* out);
};

} // namespace w7t
