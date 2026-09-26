# Diagnostic Logging

Win7Taskbar supports diagnostic logging for troubleshooting startup, Windows-version compatibility gates, tray/battery integration, and other runtime decisions.

## Log format

Entries should use this format:

```text
[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [Function] message
```

The timestamp includes milliseconds so closely spaced Explorer/taskbar events can be correlated.

## Recommended diagnostic events

At startup, logging should record:

- Windows major/minor/build information detected by the application.
- The result of Windows-version/build compatibility gates.
- The function responsible for each compatibility decision.
- Initialization and shutdown of major components.
- Win32 failures together with the relevant `GetLastError()` value where available.
- Tray and battery refresh/import decisions that affect the visible taskbar state.

## Safety

Diagnostic logging must not change normal taskbar behavior. Logging failures should never terminate the application, and sensitive data such as credentials or unrelated registry contents should not be written to the log.

When investigating a problem, prefer narrowly scoped diagnostic entries over logging every API call. This keeps logs useful and avoids excessive overhead or noise.
