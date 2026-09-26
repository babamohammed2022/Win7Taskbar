/* Win7Taskbar - native core - canonical pin verbs (G6)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * The shell talks to taskbars through a small set of canonical command
 * verbs (the ones a shell client sends when the user asks to pin or unpin
 * an application). This module:
 *
 *  - recognises those verb names (case-insensitive ordinal comparison);
 *  - exposes the ONE write point for a taskbar pin: the .lnk of the real
 *    pin folder is created or deleted here, and nowhere else - the Jump
 *    List pin row (JumpListWindow) calls the same function, so the bar
 *    can never disagree with itself about what "pinned" means on disk.
 *
 * The Start-menu pin verbs (startpin/startunpin) are recognised but
 * deliberately NOT handled: the Start menu belongs to the shell and is
 * out of scope (see the provenance rules of this project).
 *
 * The model update is not queued here: the PinnedApps folder watcher sees
 * the file change and queues W7T_EVT_PINNED_CHANGED only when the model
 * actually changed, which is exactly the required semantics.
 */
#pragma once

#include <string>

namespace w7t {

/* Canonical verb ids (0 = not a known verb). */
enum CanonicalVerb {
    VerbUnknown = 0,
    VerbTaskbarPin,
    VerbTaskbarUnpin,
    VerbStartPin,
    VerbStartUnpin,
    VerbTogglePin,
    VerbCustomOpen,
    VerbDelete,
    VerbOpen,
    VerbRunAs
};

/* Case-insensitive ordinal match of a shell command verb. */
int ParseCanonicalVerb(const wchar_t* verb);

/* True for the verbs this project executes itself (the taskbar pin
 * family). VerbCustomOpen/VerbDelete/VerbOpen/VerbRunAs travel on the
 * shell's own ShellExecute path and are not handled here. */
bool IsTaskbarPinVerb(int verb);

/* True when the verb is recognised but belongs to the Start menu (out of
 * scope: recognised, logged, not executed). */
bool IsStartPinVerb(int verb);

/* The single write point of a taskbar pin.
 * pin=true: creates <pin folder>\<sanitised baseName>.lnk targeting
 * `target` (the folder is created if missing). pin=false: deletes the
 * .lnk (a no-op when it is not there).
 * Returns true when the on-disk state actually changed. `outLnk` (may be
 * null) receives the .lnk path that was written/removed. */
bool TogglePinnedApp(const std::wstring& target, const std::wstring& baseName,
                     bool pin, std::wstring* outLnk);

/* Deletes exactly this .lnk from the pin folder. Returns true only when
 * the file existed and was removed (so the caller knows the on-disk
 * state changed). */
bool DeletePinnedShortcut(const std::wstring& lnkPath);

} /* namespace w7t */
