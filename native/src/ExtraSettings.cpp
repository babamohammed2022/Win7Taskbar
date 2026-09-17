/* Win7Taskbar - native core - extra settings (implementation)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * See ExtraSettings.h for the design. What lives here is only the state of
 * the secondary settings published by the managed layer and the read of the
 * system accent colour: no write to the registry, no call that could change
 * the look of Windows or of any other application.
 */

#include "ExtraSettings.h"
#include "Win7NetworkFlyout.h"
#include "SehGuard.h"

#include <windows.h>
#include <dwmapi.h>
#include <atomic>

namespace w7t {
namespace extras {

namespace {

/* A single writer (the core thread that receives W7T_SetExtraSettings, i.e.
 * the managed UI thread) and several readers (the flyout and the Properties
 * window): atomics, so a value is never read half-written. */
std::atomic<int32_t>  g_flyoutColorMode{0};
std::atomic<uint32_t> g_flyoutCustomColor{0x000078D7u};   /* Windows 8 blue */
std::atomic<int32_t>  g_connectionPrivacyMode{0};

/* Fallback colour when the system does not provide an accent: the same blue
 * used as the default for the custom colour. */
constexpr uint32_t kFallbackAccent = 0x000078D7u;

} /* namespace */

void SetFlyoutColorMode(int32_t mode) {
    g_flyoutColorMode.store(mode == 1 ? 1 : 0, std::memory_order_relaxed);
}

int32_t FlyoutColorMode() {
    return g_flyoutColorMode.load(std::memory_order_relaxed);
}

void SetFlyoutCustomColor(uint32_t rgb) {
    g_flyoutCustomColor.store(rgb & 0x00FFFFFFu, std::memory_order_relaxed);
}

uint32_t FlyoutCustomColor() {
    return g_flyoutCustomColor.load(std::memory_order_relaxed);
}

void SetConnectionPrivacyMode(int32_t mode) {
    const int32_t value = mode == 1 ? 1 : 0;
    g_connectionPrivacyMode.store(value, std::memory_order_relaxed);

    /* A single point of application: the recreated network flyout reads the
     * privacy flag the same way it used to read the mod setting. */
    W7T_SEH_TRY
        w7tnet::W7TNetFlyout_SetPrivacyMode(value);
    W7T_SEH_CATCH
    W7T_SEH_END
}

int32_t ConnectionPrivacyMode() {
    return g_connectionPrivacyMode.load(std::memory_order_relaxed);
}

bool QuerySystemAccentColor(uint32_t* outRgb) {
    if (outRgb == nullptr) {
        return false;
    }

    /* 1) The documented way: DwmGetColorizationColor returns the accent as
     *    ARGB (the high byte is the colorization opacity, which is not needed
     *    here). */
    W7T_SEH_TRY {
        DWORD color = 0;
        BOOL opaque = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
            *outRgb = static_cast<uint32_t>(color) & 0x00FFFFFFu;
            return true;
        }
    } W7T_SEH_CATCH {} W7T_SEH_END

    /* 2) Fallback: the same value in 0xAARRGGBB form that Windows keeps in
     *    HKCU\Software\Microsoft\Windows\DWM\ColorizationColor. Read only:
     *    nothing is written to the personalization here. */
    W7T_SEH_TRY {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\DWM",
                          0, KEY_READ, &key) == ERROR_SUCCESS && key != nullptr) {
            DWORD value = 0;
            DWORD size = sizeof(value);
            DWORD type = 0;
            LSTATUS st = RegQueryValueExW(key, L"ColorizationColor", nullptr,
                                          &type,
                                          reinterpret_cast<LPBYTE>(&value),
                                          &size);
            RegCloseKey(key);
            if (st == ERROR_SUCCESS && type == REG_DWORD &&
                size == sizeof(value)) {
                *outRgb = value & 0x00FFFFFFu;
                return true;
            }
        }
    } W7T_SEH_CATCH {} W7T_SEH_END

    return false;
}

uint32_t ResolveFlyoutColor() {
    if (FlyoutColorMode() == 1) {
        return FlyoutCustomColor();
    }

    uint32_t accent = kFallbackAccent;
    if (QuerySystemAccentColor(&accent)) {
        return accent;
    }
    return kFallbackAccent;
}

} /* namespace extras */
} /* namespace w7t */
