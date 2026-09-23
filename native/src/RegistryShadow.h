// Win7Taskbar - Core nativo - shadow delle policy (16)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 16_registry_shadow (snippet integration): proposed/applied shadow
// for policy values — the UI proposes a value (file cache under
// %LOCALAPPDATA%\Win7Taskbar\RegistryShadow\), read_effective shows what
// the live registry really holds, commit() applies the proposal THROUGH
// RegistryPolicy::WriteWithBackup (README: every write has a backup),
// discard() drops the proposal. Nothing is silently applied: the
// commit step is always explicit.
//
// Virtualization guard: a value that lives under Registry Virtualization
// is not really applied (the shell reads the virtual store). The
// snippet probed it through RegQueryKeyExW/KEY_IS_VIRTUALIZED — those
// do not exist in any SDK (documented APIs are RegQueryValueEx/RegGet
// Value/RegEnumKeyEx); the real query is NtQueryKey with
// KeyVirtualizationInformation (ntdll, undocumented-ish but the only
// actual mechanism). Loaded dynamically; on any doubt the guard
// reports "not virtualized" and the caller proceeds.
// // TODO: verificare comportamento su 8.1 (virtualizzazione disattiva
// per processi per-monitor aware) e su 11 (rimossa).

#pragma once

#include <windows.h>

#include <string>

namespace w7t {

enum class ShadowState : int {
    Clean = 0,     /* no proposal, live value is the truth */
    Proposed,      /* proposal cached, live value unchanged */
    Applied        /* proposal committed (backup taken at commit) */
};

class RegistryShadow {
public:
    /* Caches a proposed value (file only; registry untouched).
     * `value` is REG_DWORD (the four policies are all DWORD). */
    static bool Propose(const std::wstring& policyName, DWORD value);

    /* What the live registry holds right now (HKCU, descriptor subkey
     * via RegistryPolicy). `defaultValue` when the value is absent. */
    static DWORD ReadEffective(const std::wstring& policyName, DWORD defaultValue);

    /* Applies the proposal through RegistryPolicy::WriteWithBackup and
     * moves the shadow to Applied. False when there is no proposal or
     * the backup-first write fails. */
    static bool Commit(const std::wstring& policyName);

    /* Drops the proposal (registry untouched). */
    static bool Discard(const std::wstring& policyName);

    static ShadowState State(const std::wstring& policyName);

    /* True when the policy key is subject to Registry Virtualization:
     * the write would land in the virtual store and the shell would
     * keep reading the old value. Dynamic ntdll query (see header). */
    static bool IsVirtualized();
};

} // namespace w7t
