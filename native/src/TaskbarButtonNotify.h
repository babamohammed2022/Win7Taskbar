/* Win7Taskbar - native core - TaskbarButtonCreated notification
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * When the project owns the taskbar, the bar itself is the taskbar: a new
 * application button appearing is something a shell-aware tool must be told
 * about, and the established contract for that is the registered
 * "TaskbarButtonCreated" message, broadcast to all top-level windows.
 *
 * Rules kept deliberately simple:
 *  - one message id per process (RegisterWindowMessage, first call);
 *  - each process is notified at most once (a relaunch of the same pid is
 *    not spammed); the dedup set is capped and reset when the native
 *    taskbar becomes visible again (nothing is "owned" then);
 *  - while the native taskbar is visible the notification is skipped
 *    entirely (the shell owns the buttons then);
 *  - the broadcast is fire-and-forget: a slow or dead receiver must never
 *    delay the window enumeration.
 */
#pragma once

#include <windows.h>

namespace w7t {

/* Broadcast the registered TaskbarButtonCreated message for this window's
 * process (deduplicated per pid). No-op while the native taskbar is
 * visible or when the message could not be registered. */
void NotifyTaskbarButton(HWND hwnd, DWORD pid);

/* Drop the per-pid dedup: used when the native taskbar becomes visible
 * again, so the next ownership period starts clean. */
void ResetTaskbarButtonNotify();

/* The registered message id (0 when registration failed). Used by the
 * diagnostic probe to recognise the message. */
UINT GetTaskbarButtonMessageId();

/* True when the W7T_TASKBAND_PROBE environment variable is set to a
 * non-"0" value. Resolved once, at first use. */
bool TaskbandProbeEnabled();

} /* namespace w7t */
