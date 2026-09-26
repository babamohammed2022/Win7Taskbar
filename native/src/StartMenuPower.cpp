/*
 * Win7Taskbar - Start Menu power / session actions
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Uses documented APIs only:
 *   LockWorkStation, WtsDisconnectSession, ExitWindowsEx,
 *   InitiateSystemShutdownExW, SetSuspendState.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif

#include "StartMenuPower.h"

#include <windows.h>
#include <powrprof.h>
#include <wtsapi32.h>
#include <reason.h>

#include <string>

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace w7t {
namespace startmenu {
namespace {

bool EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const BOOL looked = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME,
                                              &tp.Privileges[0].Luid);
    BOOL ok = FALSE;
    if (looked) {
        ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr,
                                   nullptr);
        if (ok && GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
            ok = FALSE;
        }
    }
    CloseHandle(token);
    return ok == TRUE;
}

} /* namespace */

bool RunPowerAction(PowerAction action, std::wstring& error) {
    error.clear();
    switch (action) {
    case PowerAction::Lock:
        if (!LockWorkStation()) {
            error = L"LockWorkStation failed";
            return false;
        }
        return true;
    case PowerAction::SwitchUser:
        if (!WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
                                  FALSE)) {
            error = L"WTSDisconnectSession failed";
            return false;
        }
        return true;
    case PowerAction::LogOff:
        if (!ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OTHER |
                                          SHTDN_REASON_MINOR_OTHER |
                                          SHTDN_REASON_FLAG_PLANNED)) {
            error = L"ExitWindowsEx(EWX_LOGOFF) failed";
            return false;
        }
        return true;
    case PowerAction::Sleep:
        if (!SetSuspendState(FALSE, FALSE, FALSE)) {
            error = L"SetSuspendState(sleep) failed";
            return false;
        }
        return true;
    case PowerAction::Hibernate:
        if (!SetSuspendState(TRUE, FALSE, FALSE)) {
            error = L"SetSuspendState(hibernate) failed";
            return false;
        }
        return true;
    case PowerAction::Restart:
        if (!EnableShutdownPrivilege()) {
            error = L"SE_SHUTDOWN_NAME not granted";
            return false;
        }
        if (!InitiateSystemShutdownExW(nullptr, nullptr, 0, FALSE, TRUE,
                                       SHTDN_REASON_MAJOR_OTHER |
                                           SHTDN_REASON_MINOR_OTHER |
                                           SHTDN_REASON_FLAG_PLANNED)) {
            error = L"InitiateSystemShutdownExW(restart) failed";
            return false;
        }
        return true;
    case PowerAction::Shutdown:
        if (!EnableShutdownPrivilege()) {
            error = L"SE_SHUTDOWN_NAME not granted";
            return false;
        }
        if (!InitiateSystemShutdownExW(nullptr, nullptr, 0, FALSE, FALSE,
                                       SHTDN_REASON_MAJOR_OTHER |
                                           SHTDN_REASON_MINOR_OTHER |
                                           SHTDN_REASON_FLAG_PLANNED)) {
            error = L"InitiateSystemShutdownExW(shutdown) failed";
            return false;
        }
        return true;
    default:
        error = L"unknown power action";
        return false;
    }
}

} /* namespace startmenu */
} /* namespace w7t */
