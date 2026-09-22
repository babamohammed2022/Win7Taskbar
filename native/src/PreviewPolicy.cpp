/* Win7Taskbar - native core - preview policy (implementation)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * Read-only, cached. No write to the registry, ever; no call here can
 * change the look of Windows or of any other application.
 */

#include "PreviewPolicy.h"
#include "Common.h"   /* w7t::LogTagged */

#include <windows.h>
#include <mutex>

namespace w7t {
namespace {

constexpr wchar_t kSubKeyAdvanced[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

constexpr wchar_t kDisableWindow[]  = L"DisablePreviewWindow";
constexpr wchar_t kDisableDesktop[] = L"DisablePreviewDesktop";
constexpr wchar_t kThumbHover[]     = L"ThumbnailLivePreviewHoverTime";
constexpr wchar_t kPeekHover[]      = L"DesktopLivePreviewHoverTime";
constexpr wchar_t kExtendedHover[]  = L"ExtendedUIHoverTime";

/* Safety net: an absurd value must not freeze the interaction. */
constexpr uint32_t kHoverMax = 5000;

struct PreviewPolicyCache {
    std::mutex mutex;
    PreviewPolicy policy;
    bool valid = false;
};

PreviewPolicyCache& PolicyCache() {
    static PreviewPolicyCache cache;
    return cache;
}

/* One DWORD of the user's configuration (RegGetValueW does the type
 * conversion; no manual buffer). False when the value is absent - the
 * caller keeps its own default. */
bool ReadDword(const wchar_t* value, uint32_t* out) {
    DWORD data = 0;
    DWORD size = sizeof(data);
    const LSTATUS st = ::RegGetValueW(HKEY_CURRENT_USER, kSubKeyAdvanced,
                                      value, RRF_RT_REG_DWORD, nullptr,
                                      &data, &size);
    if (st != ERROR_SUCCESS || size != sizeof(data)) {
        return false;
    }
    *out = static_cast<uint32_t>(data);
    return true;
}

uint32_t ClampHover(uint32_t v) {
    return v > kHoverMax ? kHoverMax : v;
}

PreviewPolicy Resolve() {
    PreviewPolicy p;
    uint32_t v = 0;

    if (ReadDword(kDisableWindow, &v)) {
        p.windowThumbsEnabled = (v == 0);
    }
    if (ReadDword(kDisableDesktop, &v)) {
        p.desktopPeekEnabled = (v == 0);
    }
    if (ReadDword(kThumbHover, &v)) {
        p.thumbHoverMs = ClampHover(v);
        p.thumbHoverMsSet = true;
    }
    if (ReadDword(kPeekHover, &v)) {
        p.peekHoverMs = ClampHover(v);
        p.peekHoverMsSet = true;
    }
    if (ReadDword(kExtendedHover, &v)) {
        p.extendedUiHoverMs = ClampHover(v);
    }

    /* The system-side "live preview allowed" gate is consulted by Windows
     * through a helper that is not a public API: it is NOT replicated
     * here. The conservative choice is to leave the feature on
     * (fail-open) and say in the log that the gate was not evaluated. */
    p.livePreviewAllowed = true;
    return p;
}

} /* namespace */

PreviewPolicy GetPreviewPolicy() {
    PreviewPolicyCache& cache = PolicyCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (!cache.valid) {
        cache.policy = Resolve();
        cache.valid = true;
        LogTagged(L"PREVIEW",
                  L"policy: thumbs=%d peek=%d liveGate=not-evaluated"
                  L" hover=%u%s peekHover=%u%s extended=%u",
                  (int)cache.policy.windowThumbsEnabled,
                  (int)cache.policy.desktopPeekEnabled,
                  (unsigned)cache.policy.thumbHoverMs,
                  cache.policy.thumbHoverMsSet ? L" (user)" : L" (project default)",
                  (unsigned)cache.policy.peekHoverMs,
                  cache.policy.peekHoverMsSet ? L" (user)" : L" (project default)",
                  (unsigned)cache.policy.extendedUiHoverMs);
    }
    return cache.policy;
}

void InvalidatePreviewPolicy() {
    PreviewPolicyCache& cache = PolicyCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.valid = false;
}

} /* namespace w7t */
