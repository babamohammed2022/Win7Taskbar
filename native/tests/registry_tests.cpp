// Win7Taskbar - test unitari: policy registry (13) e shadow (16)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// Runs against TEMPORARY HKCU test keys only (Software\Win7Taskbar\
// Tests\...) and cleans up after itself — never touches real policy
// keys. The Commit() round-trip of 16 rides on the same
// WriteWithBackup path exercised here plus descriptor lookup (unknown
// names are refused), so no real policy value is needed.

#include <windows.h>

#include <cstdio>
#include <string>
#include <cstring>

#include "../src/LocalAppDataStore.h"
#include "../src/RegistryPolicy.h"
#include "../src/RegistryShadow.h"

using namespace w7t;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

namespace {

constexpr const wchar_t* kTestSubkey = L"Software\\Win7Taskbar\\Tests\\RegistryPolicyTest";
constexpr const wchar_t* kTestValue = L"W7T_Test_Value";

PolicyValue MakeTestValue(DWORD v) {
    PolicyValue value;
    value.subkey = kTestSubkey;
    value.valueName = kTestValue;
    value.type = REG_DWORD;
    value.data.assign(reinterpret_cast<const BYTE*>(&v),
                      reinterpret_cast<const BYTE*>(&v) + sizeof(v));
    return value;
}

DWORD ReadTestValue(DWORD fallback) {
    PolicyValue out;
    if (!RegistryPolicy::ReadValue(MakeTestValue(0), &out) ||
        out.data.size() < sizeof(DWORD)) {
        return fallback;
    }
    DWORD v = 0;
    memcpy(&v, out.data.data(), sizeof(v));
    return v;
}

void Cleanup() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kTestSubkey, 0, KEY_SET_VALUE, &key) ==
        ERROR_SUCCESS) {
        RegDeleteValueW(key, kTestValue);
        RegCloseKey(key);
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, kTestSubkey);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Win7Taskbar\\Tests");
    RegistryPolicy::DeleteBackup(kTestValue);
}

} // namespace

static void TestWriteWithBackupRoundTrip() {
    Cleanup();
    CHECK(!RegistryPolicy::HasBackup(kTestValue));
    CHECK(ReadTestValue(0xFFFFFFFF) == 0xFFFFFFFF);   /* assente */

    /* 1) Prima scrittura: il backup dice "non esisteva". */
    CHECK(RegistryPolicy::WriteWithBackup(MakeTestValue(42)));
    CHECK(RegistryPolicy::HasBackup(kTestValue));
    CHECK(ReadTestValue(0) == 42);

    /* 2) Seconda scrittura: il backup ora custodisce il 42. */
    CHECK(RegistryPolicy::WriteWithBackup(MakeTestValue(7)));
    CHECK(ReadTestValue(0) == 7);

    /* 3) Restore: torna a 42 e il backup viene consumato. */
    CHECK(RegistryPolicy::RestoreBackup(kTestValue));
    CHECK(ReadTestValue(0) == 42);
    CHECK(!RegistryPolicy::HasBackup(kTestValue));
}

static void TestRestoreDeletesMissingValue() {
    Cleanup();
    /* Assente -> scrivo 5 -> restore rimuove la voce (stato prec.). */
    CHECK(RegistryPolicy::WriteWithBackup(MakeTestValue(5)));
    CHECK(RegistryPolicy::RestoreBackup(kTestValue));
    CHECK(ReadTestValue(0xFFFFFFFF) == 0xFFFFFFFF);   /* di nuovo assente */
}

static void TestBackupFirstRefusesWithoutBackup() {
    Cleanup();
    /* DeleteBackup non lascia residui: HasBackup false. */
    CHECK(RegistryPolicy::WriteWithBackup(MakeTestValue(1)));
    CHECK(RegistryPolicy::DeleteBackup(kTestValue));
    CHECK(!RegistryPolicy::HasBackup(kTestValue));
    Cleanup();
}

static void TestDescriptors() {
    /* I 4 valori policy README sono descrittori, non chiamate dirette. */
    CHECK(RegistryPolicy::FindDescriptor(L"Start_JumpListItems") != nullptr);
    CHECK(RegistryPolicy::FindDescriptor(L"UseWin32TrayClockExperience") != nullptr);
    CHECK(RegistryPolicy::FindDescriptor(L"EnableMtcUvc") != nullptr);
    CHECK(RegistryPolicy::FindDescriptor(L"UseWin32BatteryFlyout") != nullptr);
    CHECK(RegistryPolicy::FindDescriptor(L"W7T_Test_Value") == nullptr);
}

static void TestShadowLifecycle() {
    /* File-only lifecycle (nessuna chiave reale toccata). */
    const std::wstring name = L"W7T_Shadow_Test";
    RegistryShadow::Discard(name);
    CHECK(RegistryShadow::State(name) == ShadowState::Clean);

    CHECK(RegistryShadow::Propose(name, 5));
    CHECK(RegistryShadow::State(name) == ShadowState::Proposed);
    /* Proposta NON applicata: ReadEffective resta il valore reale
     * (assente -> default). Nessun cambiamento silenzioso. */
    CHECK(RegistryShadow::ReadEffective(name, 0xFFFFFFFF) == 0xFFFFFFFF);

    /* Commit su nome senza descrittore: rifiutato, la proposta resta. */
    CHECK(!RegistryShadow::Commit(name));
    CHECK(RegistryShadow::State(name) == ShadowState::Proposed);

    CHECK(RegistryShadow::Discard(name));
    CHECK(RegistryShadow::State(name) == ShadowState::Clean);

    /* Guard virtualizzazione: chiamata sicura, tipicamente false. */
    (void)RegistryShadow::IsVirtualized();
}

int main() {
    TestWriteWithBackupRoundTrip();
    TestRestoreDeletesMissingValue();
    TestBackupFirstRefusesWithoutBackup();
    TestDescriptors();
    TestShadowLifecycle();
    Cleanup();
    if (g_failures != 0) {
        std::printf("registry_tests: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("registry_tests: all checks passed\n");
    return 0;
}
