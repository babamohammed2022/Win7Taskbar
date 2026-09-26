// Win7Taskbar - W7TTrayOverlayKill.dll
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Injected into explorer.exe with the documented SetWindowsHookEx
// (WH_CALLWNDPROC) path already used by W7TInject.dll. Every Win32 call
// in the callback is wrapped in SEH (SehGuard.h): a fault here would
// take down Explorer, not our bar.
//
// Duties, all in-process and reversible:
//   1. Hide the Win11 XAML tray overlay HWNDs
//      (DesktopWindowContentBridge / InputSite.WindowClass) so they do
//      not sit on top of our out-of-process icons. Icons themselves are
//      never enumerated or drawn here.
//   2. Apply SPI_SETWORKAREA + snap maximized frames from inside
//      Explorer (Windhawk taskbar-on-top geometry, without TrayUI
//      symbol hooks).
//
// Heartbeat: if the mutex created by our process is gone (crash/exit
// without Unhook), the next callback restores the overlays.

#include <windows.h>
#include <cstdint>
#include "SehGuard.h"

namespace {

constexpr wchar_t kMutexName[] = L"Local\\Win7Taskbar.XamlOverlayKill.Alive";
constexpr wchar_t kPropKilled[] = L"W7T_XamlOverlayKilled";
constexpr wchar_t kBridgeClass[] =
    L"Windows.UI.Composition.DesktopWindowContentBridge";
constexpr wchar_t kInputClass[] =
    L"Windows.UI.Input.InputSite.WindowClass";
constexpr wchar_t kWorkMapName[] = L"Local\\Win7Taskbar.OverlayWorkPacket";

struct OverlayWorkPacket {
    RECT work;
    RECT bar;
    int32_t edge;
};

struct SnapData {
    RECT bar;
    RECT work;
    HMONITOR monitor;
};

UINT KillMsg() {
    return RegisterWindowMessageW(L"W7T_KillXamlOverlay");
}

UINT RestoreMsg() {
    return RegisterWindowMessageW(L"W7T_RestoreXamlOverlay");
}

UINT WorkMsg() {
    return RegisterWindowMessageW(L"W7T_ExplorerSetWorkArea");
}

bool HostStillAlive() {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, kMutexName);
    if (m == nullptr) {
        return false;
    }
    CloseHandle(m);
    return true;
}

bool IsOverlayClass(HWND hwnd) {
    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) <= 0) {
        return false;
    }
    return _wcsicmp(cls, kBridgeClass) == 0 ||
           _wcsicmp(cls, kInputClass) == 0;
}

BOOL CALLBACK DisableChildProc(HWND hwnd, LPARAM) {
    W7T_SEH_TRY
        if (!IsOverlayClass(hwnd)) {
            EnumChildWindows(hwnd, DisableChildProc, 0);
            return TRUE;
        }
        const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if ((style & WS_DISABLED) == 0) {
            SetWindowLongW(hwnd, GWL_STYLE, style | WS_DISABLED);
        }
        HRGN empty = CreateRectRgn(0, 0, 0, 0);
        if (empty != nullptr) {
            SetWindowRgn(hwnd, empty, TRUE);
        }
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE |
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        ShowWindow(hwnd, SW_HIDE);
        SetPropW(hwnd, kPropKilled, reinterpret_cast<HANDLE>(1));
        EnumChildWindows(hwnd, DisableChildProc, 0);
    W7T_SEH_CATCH
    W7T_SEH_END
    return TRUE;
}

BOOL CALLBACK RestoreChildProc(HWND hwnd, LPARAM) {
    W7T_SEH_TRY
        if (GetPropW(hwnd, kPropKilled) != nullptr) {
            SetWindowRgn(hwnd, nullptr, TRUE);
            const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            if ((style & WS_DISABLED) != 0) {
                SetWindowLongW(hwnd, GWL_STYLE, style & ~WS_DISABLED);
            }
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            RemovePropW(hwnd, kPropKilled);
        }
        EnumChildWindows(hwnd, RestoreChildProc, 0);
    W7T_SEH_CATCH
    W7T_SEH_END
    return TRUE;
}

struct FindTrayCtx {
    DWORD pid;
    HWND hwnd;
};

BOOL CALLBACK FindTrayProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindTrayCtx*>(lp);
    W7T_SEH_TRY
        wchar_t cls[64] = {};
        if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0 &&
            _wcsicmp(cls, L"Shell_TrayWnd") == 0) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == ctx->pid) {
                ctx->hwnd = hwnd;
                return FALSE;
            }
        }
    W7T_SEH_CATCH
    W7T_SEH_END
    return TRUE;
}

HWND ExplorerTray() {
    FindTrayCtx ctx = {};
    ctx.pid = GetCurrentProcessId();
    EnumWindows(FindTrayProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

void DisableOverlays() {
    W7T_SEH_TRY
        HWND tray = ExplorerTray();
        if (tray == nullptr || !IsWindow(tray)) {
            return;
        }
        EnumChildWindows(tray, DisableChildProc, 0);
        HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
        if (notify != nullptr) {
            EnumChildWindows(notify, DisableChildProc, 0);
        }
    W7T_SEH_CATCH
    W7T_SEH_END
}

void RestoreOverlays() {
    W7T_SEH_TRY
        HWND tray = ExplorerTray();
        if (tray == nullptr || !IsWindow(tray)) {
            return;
        }
        EnumChildWindows(tray, RestoreChildProc, 0);
        HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
        if (notify != nullptr) {
            EnumChildWindows(notify, RestoreChildProc, 0);
        }
    W7T_SEH_CATCH
    W7T_SEH_END
}

BOOL CALLBACK SnapEnumProc(HWND hwnd, LPARAM lp) {
    auto* data = reinterpret_cast<SnapData*>(lp);
    W7T_SEH_TRY
        if (hwnd == nullptr || !IsWindowVisible(hwnd) || !IsZoomed(hwnd)) {
            return TRUE;
        }
        if (GetWindow(hwnd, GW_OWNER) != nullptr) {
            return TRUE;
        }
        wchar_t cls[64] = {};
        if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) > 0) {
            if (_wcsicmp(cls, L"Progman") == 0 ||
                _wcsicmp(cls, L"WorkerW") == 0 ||
                _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
                _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
                return TRUE;
            }
        }
        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != data->monitor) {
            return TRUE;
        }
        RECT vis = {};
        if (!GetWindowRect(hwnd, &vis)) {
            return TRUE;
        }
        RECT hit = {};
        if (IntersectRect(&hit, &vis, &data->bar) == FALSE) {
            return TRUE;
        }
        const int width = data->work.right - data->work.left;
        const int height = data->work.bottom - data->work.top;
        if (width <= 0 || height <= 0) {
            return TRUE;
        }
        SetWindowPos(hwnd, nullptr,
                     data->work.left, data->work.top, width, height,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    W7T_SEH_CATCH
    W7T_SEH_END
    return TRUE;
}

bool ReadWorkPacket(OverlayWorkPacket* out) {
    if (out == nullptr) {
        return false;
    }
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, kWorkMapName);
    if (map == nullptr) {
        return false;
    }
    void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(OverlayWorkPacket));
    bool ok = false;
    if (view != nullptr) {
        *out = *static_cast<const OverlayWorkPacket*>(view);
        UnmapViewOfFile(view);
        ok = true;
    }
    CloseHandle(map);
    return ok;
}

void ApplyWorkArea(const OverlayWorkPacket& pkt) {
    W7T_SEH_TRY
        RECT work = pkt.work;
        if (work.right <= work.left || work.bottom <= work.top) {
            return;
        }
        SystemParametersInfoW(SPI_SETWORKAREA, 0, &work, SPIF_SENDCHANGE);
        HWND tray = ExplorerTray();
        HMONITOR mon = MonitorFromWindow(
            tray != nullptr ? tray : GetDesktopWindow(),
            MONITOR_DEFAULTTONEAREST);
        SnapData data = {};
        data.bar = pkt.bar;
        data.work = work;
        data.monitor = mon;
        EnumWindows(SnapEnumProc, reinterpret_cast<LPARAM>(&data));
    W7T_SEH_CATCH
    W7T_SEH_END
}

bool g_killArmed = false;
OverlayWorkPacket g_lastWork = {};
bool g_haveWork = false;

void OnSessionEnd() {
    g_killArmed = false;
    RestoreOverlays();
}

} /* namespace */

extern "C" __declspec(dllexport)
LRESULT CALLBACK W7TOverlayKill_CallWndProc(int nCode, WPARAM wParam,
                                            LPARAM lParam) {
    W7T_SEH_TRY
        if (nCode >= 0 && lParam != 0) {
            const CWPSTRUCT* p = reinterpret_cast<const CWPSTRUCT*>(lParam);
            if (p != nullptr && p->hwnd != nullptr) {
                const UINT kill = KillMsg();
                const UINT restore = RestoreMsg();
                const UINT work = WorkMsg();

                if (p->message == WM_ENDSESSION && p->wParam != 0) {
                    OnSessionEnd();
                } else if (p->message == WM_QUERYENDSESSION) {
                    /* session is ending: restore on the following ENDSESSION */
                } else if (restore != 0 && p->message == restore) {
                    g_killArmed = false;
                    RestoreOverlays();
                } else if (!HostStillAlive()) {
                    if (g_killArmed) {
                        g_killArmed = false;
                        RestoreOverlays();
                    }
                } else if (kill != 0 && p->message == kill) {
                    g_killArmed = true;
                    DisableOverlays();
                } else if (work != 0 && p->message == work) {
                    OverlayWorkPacket pkt = {};
                    if (ReadWorkPacket(&pkt)) {
                        g_lastWork = pkt;
                        g_haveWork = true;
                        ApplyWorkArea(g_lastWork);
                    }
                } else if (g_killArmed) {
                    if (p->message == WM_WINDOWPOSCHANGING ||
                        p->message == WM_WINDOWPOSCHANGED ||
                        p->message == WM_SHOWWINDOW ||
                        p->message == WM_PAINT) {
                        if (IsOverlayClass(p->hwnd)) {
                            DisableOverlays();
                        }
                    }
                    if (p->message == WM_SETTINGCHANGE && g_haveWork) {
                        ApplyWorkArea(g_lastWork);
                    }
                }
            }
        }
    W7T_SEH_CATCH
        /* never crash Explorer */
    W7T_SEH_END
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        W7T_SEH_TRY
            RestoreOverlays();
        W7T_SEH_CATCH
        W7T_SEH_END
    }
    return TRUE;
}
