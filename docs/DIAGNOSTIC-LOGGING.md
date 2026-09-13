# Runtime diagnostic logging

Win7Taskbar can use a runtime diagnostic log without recompilation. The runtime switch is the file `%APPDATA%\Win7Taskbar\logging.enabled` (the file contents are irrelevant). Delete the file to disable logging. `W7T_ENABLE_LOGGING=1` is also supported for temporary sessions.

The log is `%APPDATA%\Win7Taskbar\Win7Taskbar.log` and uses local timestamps with millisecond precision (`yyyy-MM-dd HH:mm:ss.fff`). Logging must never be allowed to terminate or block the taskbar UI.

## Required event contract

When this diagnostic facility is wired into a code path, every event records the caller/function and the current settings snapshot. The diagnostic categories are:

- `WINDOWS` — exact NT build and Windows 11 build gate result.
- `GATE` — every version-dependent gate, including accepted/rejected outcome and caller.
- `FLYOUT` — requested flyout, originating handler/function, gate outcome, and actually displayed flyout.
- `SEARCH` — window creation/opening, search/indexing result, and errors.
- `SETTINGS` / `SNAPSHOT` — all user-facing settings relevant to the action, not merely changed properties.
- `LANGUAGE` — Windows/system UI language and the language actually displayed by Win7Taskbar.
- `CONTEXT_MENU` — frontend-localized versus shell-native construction and resulting language.
- `TRAY_ICON` — real shell-derived versus application-recreated icon and `process+kind` identity.
- `OVERFLOW` — tray model contents at chevron click and whether overflow window creation occurred or the event was lost earlier.
- `DRAG` — ownership source (application-owned or UI Automation) and whether a technically valid drag operation exists.

## Important implementation rule

Do not infer an event after the fact. Log the state at the decision boundary and, for asynchronous operations, log both the request and the completion/failure. If a window/event disappears before its target object is instantiated, emit an explicit `result=event-lost-before-instantiation` record.

The logger is deliberately file-flag based so diagnostic logging can be enabled on an installed binary without rebuilding it.