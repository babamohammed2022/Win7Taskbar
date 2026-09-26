/* Win7Taskbar - W7TTrayOverlayKill installer (our process)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 */

#include "TrayOverlayKill.h"
#include "Common.h"
#include "SehGuard.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace w7t {
namespace {

constexpr wchar_t kDllName[] = L"W7TTrayOverlayKill.dll";
constexpr char kProcName[] = "W7TOverlayKill_CallWndProc";
constexpr wchar_t kMutexName[] = L"Local\\Win7Taskbar.XamlOverlayKill.Alive";
constexpr wchar_t kWorkMapName[] = L"Local\\Win7Taskbar.OverlayWorkPacket";

struct OverlayWorkPacket {
    RECT work;
    RECT bar;
    int32_t edge;
};

std::atomic<bool> g_enabled{true};
HMODULE g_dll = nullptr;
HHOOK g_hookTray = nullptr;
HHOOK g_hookNotify = nullptr;
HWND g_explorerTray = nullptr;
HANDLE g_aliveMutex = nullptr;
HANDLE g_workMap = nullptr;
OverlayWorkPacket* g_workView = nullptr;

struct FindTrayCtx {
    DWORD skipPid;
    HWND hwnd;
};

BOOL CALLBACK FindExplorerTrayProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindTrayCtx*>(lp);
    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 &&
        _wcsicmp(cls, L"Shell_TrayWnd") == 0) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != 0 && pid != ctx->skipPid) {
            ctx->hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

HWND FindExplorerTray() {
    FindTrayCtx ctx = {};
    ctx.skipPid = GetCurrentProcessId();
    EnumWindows(FindExplorerTrayProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

struct OverlayHit {
    bool found;
};

BOOL CALLBACK OverlayChildProc(HWND hwnd, LPARAM lp) {
    auto* hit = reinterpret_cast<OverlayHit*>(lp);
    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0) {
        if (_wcsicmp(cls,
                     L"Windows.UI.Composition.DesktopWindowContentBridge") == 0 ||
            _wcsicmp(cls, L"Windows.UI.Input.InputSite.WindowClass") == 0) {
            hit->found = true;
            return FALSE;
        }
    }
    return TRUE;
}

bool OverlayChildFound(HWND root) {
    if (root == nullptr) {
        return false;
    }
    OverlayHit hit = {};
    EnumChildWindows(root, OverlayChildProc, reinterpret_cast<LPARAM>(&hit));
    return hit.found;
}

void EnsureMutex() {
    if (g_aliveMutex != nullptr) {
        return;
    }
    g_aliveMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (g_aliveMutex == nullptr) {
        AppendCoreLog(L"overlay-kill: CreateMutex fallita");
    }
}

void DropMutex() {
    if (g_aliveMutex != nullptr) {
        CloseHandle(g_aliveMutex);
        g_aliveMutex = nullptr;
    }
}

void SendRegistered(HWND hwnd, const wchar_t* name) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }
    const UINT msg = RegisterWindowMessageW(name);
    if (msg == 0) {
        return;
    }
    DWORD_PTR ack = 0;
    SendMessageTimeoutW(hwnd, msg, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL,
                        400, &ack);
}

} /* namespace */

void TrayOverlayKill_SetEnabled(bool enabled) {
    const bool was = g_enabled.exchange(enabled);
    if (enabled && !was) {
        TrayOverlayKill_Install();
    } else if (!enabled && was) {
        TrayOverlayKill_Uninstall();
    }
}

bool TrayOverlayKill_Enabled() {
    return g_enabled.load();
}

void TrayOverlayKill_Install() {
    W7T_SEH_TRY
        if (!g_enabled.load()) {
            return;
        }
        if (!IsWindows11OrBetter()) {
            return;
        }

        HWND tray = FindExplorerTray();
        if (tray == nullptr) {
            AppendCoreLog(L"overlay-kill: Shell_TrayWnd di Explorer non trovata, inject saltata");
            return;
        }

        HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
        if (notify == nullptr) {
            AppendCoreLog(L"overlay-kill: TrayNotifyWnd di Explorer non trovata");
        }
        if (!OverlayChildFound(tray) && !OverlayChildFound(notify)) {
            AppendCoreLog(L"overlay-kill: overlay XAML (DesktopWindowContentBridge/"
                          L"InputSite) non trovato; inject comunque per work-area");
        }

        if (g_dll == nullptr) {
            g_dll = LoadLibraryW(kDllName);
        }
        if (g_dll == nullptr) {
            wchar_t line[160] = {};
            swprintf(line, ARRAYSIZE(line),
                     L"overlay-kill: LoadLibrary W7TTrayOverlayKill.dll fallita err=%lu",
                     static_cast<unsigned long>(GetLastError()));
            AppendCoreLog(line);
            return;
        }

        auto proc = reinterpret_cast<HOOKPROC>(GetProcAddress(g_dll, kProcName));
        if (proc == nullptr) {
            AppendCoreLog(L"overlay-kill: export W7TOverlayKill_CallWndProc assente");
            return;
        }

        EnsureMutex();

        if (g_hookTray != nullptr) {
            UnhookWindowsHookEx(g_hookTray);
            g_hookTray = nullptr;
        }
        if (g_hookNotify != nullptr) {
            UnhookWindowsHookEx(g_hookNotify);
            g_hookNotify = nullptr;
        }

        DWORD tidTray = GetWindowThreadProcessId(tray, nullptr);
        if (tidTray == 0) {
            AppendCoreLog(L"overlay-kill: thread di Explorer TrayWnd sconosciuto");
            return;
        }
        g_hookTray = SetWindowsHookExW(WH_CALLWNDPROC, proc, g_dll, tidTray);
        if (g_hookTray == nullptr) {
            wchar_t line[160] = {};
            swprintf(line, ARRAYSIZE(line),
                     L"overlay-kill: SetWindowsHookEx fallita err=%lu",
                     static_cast<unsigned long>(GetLastError()));
            AppendCoreLog(line);
            DropMutex();
            return;
        }

        if (notify != nullptr) {
            DWORD tidNotify = GetWindowThreadProcessId(notify, nullptr);
            if (tidNotify != 0 && tidNotify != tidTray) {
                g_hookNotify = SetWindowsHookExW(WH_CALLWNDPROC, proc, g_dll,
                                                 tidNotify);
            }
        }

        g_explorerTray = tray;
        SendRegistered(tray, L"W7T_KillXamlOverlay");
        AppendCoreLog(L"overlay-kill: hook WH_CALLWNDPROC installato in explorer.exe");
    W7T_SEH_CATCH
        AppendCoreLog(L"overlay-kill: eccezione in Install");
    W7T_SEH_END
}

void DropWorkMap() {
    if (g_workView != nullptr) {
        UnmapViewOfFile(g_workView);
        g_workView = nullptr;
    }
    if (g_workMap != nullptr) {
        CloseHandle(g_workMap);
        g_workMap = nullptr;
    }
}

bool EnsureWorkMap() {
    if (g_workView != nullptr) {
        return true;
    }
    g_workMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   0, sizeof(OverlayWorkPacket), kWorkMapName);
    if (g_workMap == nullptr) {
        AppendCoreLog(L"overlay-kill: CreateFileMapping work-area fallita");
        return false;
    }
    g_workView = static_cast<OverlayWorkPacket*>(
        MapViewOfFile(g_workMap, FILE_MAP_WRITE, 0, 0, sizeof(OverlayWorkPacket)));
    if (g_workView == nullptr) {
        AppendCoreLog(L"overlay-kill: MapViewOfFile work-area fallita");
        CloseHandle(g_workMap);
        g_workMap = nullptr;
        return false;
    }
    return true;
}

void TrayOverlayKill_Uninstall() {
    W7T_SEH_TRY
        if (g_explorerTray != nullptr && IsWindow(g_explorerTray)) {
            SendRegistered(g_explorerTray, L"W7T_RestoreXamlOverlay");
        }
        if (g_hookNotify != nullptr) {
            UnhookWindowsHookEx(g_hookNotify);
            g_hookNotify = nullptr;
        }
        if (g_hookTray != nullptr) {
            UnhookWindowsHookEx(g_hookTray);
            g_hookTray = nullptr;
        }
        g_explorerTray = nullptr;
        DropMutex();
        DropWorkMap();
        AppendCoreLog(L"overlay-kill: hook rimosso, overlay XAML ripristinato");
    W7T_SEH_CATCH
        AppendCoreLog(L"overlay-kill: eccezione in Uninstall");
    W7T_SEH_END
}

void TrayOverlayKill_OnTaskbarCreated() {
    W7T_SEH_TRY
        TrayOverlayKill_Uninstall();
        TrayOverlayKill_Install();
    W7T_SEH_CATCH
        AppendCoreLog(L"overlay-kill: eccezione su TaskbarCreated");
    W7T_SEH_END
}

void TrayOverlayKill_OnSessionEnding() {
    TrayOverlayKill_Uninstall();
}

void TrayOverlayKill_PushWorkArea(int32_t edge, const RECT& barRect,
                                  const RECT& workRect) {
    W7T_SEH_TRY
        if (!g_enabled.load() || g_hookTray == nullptr) {
            return;
        }
        HWND tray = g_explorerTray;
        if (tray == nullptr || !IsWindow(tray)) {
            tray = FindExplorerTray();
            g_explorerTray = tray;
        }
        if (tray == nullptr) {
            return;
        }
        if (!EnsureWorkMap() || g_workView == nullptr) {
            return;
        }
        g_workView->work = workRect;
        g_workView->bar = barRect;
        g_workView->edge = edge;
        SendRegistered(tray, L"W7T_ExplorerSetWorkArea");
    W7T_SEH_CATCH
        AppendCoreLog(L"overlay-kill: eccezione in PushWorkArea");
    W7T_SEH_END
}

} /* namespace w7t */
