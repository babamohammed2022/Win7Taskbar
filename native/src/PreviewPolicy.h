/* Win7Taskbar - native core - preview policy (user configuration)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * Reads the user's own preview configuration (read-only; this module never
 * writes to the registry, and no call here can change the look of Windows
 * or of any other application). Windows consults these values before
 * drawing the live window thumbnails and the desktop peek; honouring them
 * is the only way the reconstructed bar does not contradict an explicit
 * user choice.
 *
 * Everything is cached: the registry is touched at most once per
 * configuration change (TrayService drops the cache on WM_SETTINGCHANGE),
 * never on a hover/paint path.
 *
 * Fail-safe: the system-side gate for the "live" preview uses a helper of
 * the shell that is not documented, so it is NOT replicated here - the
 * module reports it as allowed (fail-open) and logs that the gate was not
 * evaluated, instead of introducing a fragile dependency.
 */
#pragma once

#include <cstdint>

namespace w7t {

struct PreviewPolicy {
    /* User preference: live window thumbnails enabled (the disable switch
     * is read, inverted, and defaulted to enabled). */
    bool windowThumbsEnabled = true;
    /* User preference: the desktop peek (hover over the show-desktop
     * button) enabled. */
    bool desktopPeekEnabled = true;
    /* System-side "live preview allowed" gate: always true here (fail-open;
     * the shell helper that sets it is not a public API). */
    bool livePreviewAllowed = true;
    /* Hover delay before the first window preview, ms. Null when the user
     * value is absent: the caller keeps its own project default. */
    uint32_t thumbHoverMs = 0;
    bool thumbHoverMsSet = false;
    /* Hover delay before the desktop peek, ms. Null-safe flag as above. */
    uint32_t peekHoverMs = 0;
    bool peekHoverMsSet = false;
    /* Extended-UI hover time, ms. Read and exposed for completeness; the
     * project has no extended-UI equivalent, so no consumer uses it. */
    uint32_t extendedUiHoverMs = 0;
};

/* The current policy (cached). Always succeeds; the fields carry the
 * project defaults when the user configuration is absent. */
PreviewPolicy GetPreviewPolicy();

/* Drops the cache so the next GetPreviewPolicy resolves the current
 * configuration. Called by the tray window on WM_SETTINGCHANGE. */
void InvalidatePreviewPolicy();

} /* namespace w7t */
