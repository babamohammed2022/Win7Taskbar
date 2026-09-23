// Win7Taskbar - Core nativo - confini eccezioni C++ <-> Win32
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 02_exception_safe_loop (snippet integration): a WindowProc, a thread
// entry or a COM callback must never let a C++ exception unwind across
// OS frames (undefined behaviour, in practice a crash inside user32).
// Every entry point that runs project code is wrapped here.
//
// Compile with /EHsc (MSVC) or the MinGW default; never /EHa globally.
// If SEH translation is ever needed it belongs to one isolated module
// plus _set_se_translator, never to the whole core.

#pragma once

#include <windows.h>
#include <exception>
#include <string>
#include <utility>

namespace w7t {

namespace detail {

inline void ReportBoundaryException(const wchar_t* where, const char* what) noexcept {
    wchar_t buf[256];
    MultiByteToWideChar(CP_UTF8, 0, what ? what : "(null)", -1, buf, 256);
    OutputDebugStringW(L"[Win7Taskbar] exception in ");
    OutputDebugStringW(where);
    OutputDebugStringW(L": ");
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\r\n");
}

} // namespace detail

// Exception-safe message loop: logs and exits with a clean code.
inline int RunMessageLoop() noexcept {
    MSG msg = {};
    int exitCode = 0;
    try {
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exitCode = static_cast<int>(msg.wParam);
    } catch (const std::exception& e) {
        detail::ReportBoundaryException(L"message loop", e.what());
        exitCode = 1;
    } catch (...) {
        OutputDebugStringW(L"[Win7Taskbar] unknown exception in message loop\r\n");
        exitCode = 2;
    }
    return exitCode;
}

// WindowProc guard: wraps the dispatch so exceptions never cross Win32.
template <typename Handler>
LRESULT GuardedWndProc(Handler&& handler, LRESULT fallback = 0) noexcept {
    try {
        return static_cast<LRESULT>(handler());
    } catch (const std::exception& e) {
        detail::ReportBoundaryException(L"WndProc", e.what());
    } catch (...) {
        OutputDebugStringW(L"[Win7Taskbar] unknown exception in WndProc\r\n");
    }
    SetLastError(ERROR_UNHANDLED_EXCEPTION);
    return fallback;
}

// Generic guard for thread entries and recurring tasks (resize, layout).
template <typename Fn>
void GuardedRun(Fn&& fn) noexcept {
    try {
        fn();
    } catch (const std::exception& e) {
        detail::ReportBoundaryException(L"task", e.what());
    } catch (...) {
        OutputDebugStringW(L"[Win7Taskbar] unknown exception in task\r\n");
    }
}

} // namespace w7t
