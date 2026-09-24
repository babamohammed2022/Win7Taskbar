/*
 * Win7Taskbar - Win7StartHelper.exe
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Tiny native process whose ONLY jobs are:
 *   1. WH_KEYBOARD_LL
 *   2. the Windows-key state machine (lone Win vs Win+combo)
 *   3. signalling the Start Menu (named event)
 *   4. watchdog / heartbeat
 *
 * The hook callback is deliberately tiny: no COM, no allocation, no
 * logging, no filesystem, no WPF, no SendInput. SendInput of the dummy
 * key (needed so Windows does not open Start after we eat Win UP) runs
 * on this process's message-loop thread, never inside the hook.
 *
 * W7T_SEH_TRY is NOT used here: the macros are not considered sufficient
 * protection for a system-wide keyboard hook. The callback is C-like
 * (atomics + SetEvent + return).
 *
 * Windows-key state machine
 * -------------------------
 * States: Idle, WinHeld, WinCombo.
 *
 *   Idle
 *     Win DOWN (not injected) -> WinHeld, PASS THROUGH (never eat DOWN)
 *
 *   WinHeld
 *     any other KEYDOWN       -> WinCombo, PASS THROUGH
 *     Win UP                  -> Idle, EAT the UP, signal helper thread
 *                                (lone press)
 *
 *   WinCombo
 *     Win UP                  -> Idle, PASS THROUGH (Win+E / Win+R / ...)
 *     other keys              -> PASS THROUGH
 *
 * Injected events (LLKHF_INJECTED) always pass through and never change
 * state, so the dummy key we inject after a lone press cannot re-enter
 * the machine.
 *
 * After a lone Win UP is eaten, the helper thread (not the hook):
 *   AllowSetForegroundWindow(taskbarPid)
 *   SendInput(dummy DOWN, dummy UP, Win UP)  -- restores key state,
 *                                               Windows treats it as a combo
 *                                               and does not open Start
 *   SetEvent(Local\Win7Taskbar_WindowsKey)
 *
 * If the heartbeat event is not pulsed within kHeartbeatTimeoutMs, or
 * the taskbar process exits, consume-mode is switched off and every
 * subsequent key is passed through. Native Start / Open-Shell then works.
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
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <atomic>
#include <cstdint>

namespace {

constexpr wchar_t kWinKeyEventName[] = L"Local\\Win7Taskbar_WindowsKey";
constexpr wchar_t kHeartbeatEventName[] = L"Local\\Win7Taskbar_StartMenuHeartbeat";
constexpr wchar_t kRegKey[] = L"Software\\Win7Taskbar";
constexpr wchar_t kRegValue[] = L"WindowsKeyOpensOurMenu";
constexpr UINT kHeartbeatTimeoutMs = 2000;
constexpr UINT_PTR kWatchdogTimerId = 1;
constexpr UINT_PTR kSettingsTimerId = 2;
constexpr UINT kLoneWinMsg = WM_APP + 32;
constexpr WORD kDummyVk = 0x0E; /* unassigned: not a real combo */

enum class WinState : int {
    Idle = 0,
    WinHeld = 1,
    WinCombo = 2,
};

std::atomic<WinState> g_state{WinState::Idle};
std::atomic<DWORD> g_winVk{VK_LWIN};
std::atomic<int> g_consume{1};      /* 1 = our menu, 0 = pass through */
std::atomic<int> g_passThrough{0};  /* watchdog forced pass-through */
std::atomic<HWND> g_helperWnd{nullptr};

HANDLE g_winKeyEvent = nullptr;
HANDLE g_heartbeatEvent = nullptr;
HANDLE g_taskbarProcess = nullptr;
HHOOK g_hook = nullptr;
DWORD g_taskbarPid = 0;
DWORD g_lastHeartbeatTick = 0;

struct HandleCloser {
    HANDLE h = nullptr;
    explicit HandleCloser(HANDLE v = nullptr) : h(v) {}
    ~HandleCloser() {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    HandleCloser(const HandleCloser&) = delete;
    HandleCloser& operator=(const HandleCloser&) = delete;
    HANDLE get() const { return h; }
    void reset(HANDLE v) {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = v;
    }
    HANDLE release() {
        HANDLE t = h;
        h = nullptr;
        return t;
    }
};

LONG WINAPI HelperCrashFilter(EXCEPTION_POINTERS*) {
    /* Helper crash unloads WH_KEYBOARD_LL: Win-key pass-through resumes. */
    return EXCEPTION_EXECUTE_HANDLER;
}

struct HookCloser {
    HHOOK h = nullptr;
    explicit HookCloser(HHOOK v = nullptr) : h(v) {}
    ~HookCloser() {
        if (h) UnhookWindowsHookEx(h);
    }
    HookCloser(const HookCloser&) = delete;
    HookCloser& operator=(const HookCloser&) = delete;
};

bool ReadConsumeSetting() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return true; /* default: our menu */
    }
    DWORD type = 0;
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LONG st = RegQueryValueExW(key, kRegValue, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    if (st != ERROR_SUCCESS || type != REG_DWORD) {
        return true;
    }
    return value != 0;
}

bool ShouldConsume() {
    return g_consume.load(std::memory_order_acquire) != 0 &&
           g_passThrough.load(std::memory_order_acquire) == 0;
}

void InjectDummyAndWinUp(DWORD winVk) {
    INPUT in[3] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = kDummyVk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = kDummyVk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[2].type = INPUT_KEYBOARD;
    in[2].ki.wVk = static_cast<WORD>(winVk);
    in[2].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(3, in, sizeof(INPUT));
}

void OnLoneWin() {
    if (g_taskbarPid != 0) {
        AllowSetForegroundWindow(g_taskbarPid);
    }
    InjectDummyAndWinUp(g_winVk.load(std::memory_order_acquire));
    if (g_winKeyEvent) {
        SetEvent(g_winKeyEvent);
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0 || lParam == 0) {
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    const KBDLLHOOKSTRUCT* info =
        reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (info->flags & LLKHF_INJECTED) {
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }
    if (!ShouldConsume()) {
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    const DWORD vk = info->vkCode;
    const bool isWin = (vk == VK_LWIN || vk == VK_RWIN);
    const bool isUp = (info->flags & LLKHF_UP) != 0;

    if (isWin && !isUp) {
        WinState expected = WinState::Idle;
        if (g_state.compare_exchange_strong(expected, WinState::WinHeld,
                                            std::memory_order_acq_rel)) {
            g_winVk.store(vk, std::memory_order_release);
        }
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    if (isWin && isUp) {
        const WinState was = g_state.exchange(WinState::Idle,
                                              std::memory_order_acq_rel);
        if (was == WinState::WinHeld) {
            HWND wnd = g_helperWnd.load(std::memory_order_acquire);
            if (wnd) {
                PostMessageW(wnd, kLoneWinMsg, vk, 0);
            }
            return 1; /* eat Win UP of a lone press */
        }
        return CallNextHookEx(g_hook, code, wParam, lParam);
    }

    if (!isUp) {
        WinState held = WinState::WinHeld;
        g_state.compare_exchange_strong(held, WinState::WinCombo,
                                        std::memory_order_acq_rel);
    }
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

LRESULT CALLBACK HelperWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case kLoneWinMsg:
        OnLoneWin();
        return 0;
    case WM_TIMER:
        if (wParam == kWatchdogTimerId) {
            if (g_taskbarProcess &&
                WaitForSingleObject(g_taskbarProcess, 0) == WAIT_OBJECT_0) {
                g_passThrough.store(1, std::memory_order_release);
                PostQuitMessage(0);
                return 0;
            }
            if (g_heartbeatEvent) {
                const DWORD wait = WaitForSingleObject(g_heartbeatEvent, 0);
                const DWORD now = GetTickCount();
                if (wait == WAIT_OBJECT_0) {
                    g_lastHeartbeatTick = now;
                    g_passThrough.store(0, std::memory_order_release);
                } else if ((now - g_lastHeartbeatTick) > kHeartbeatTimeoutMs) {
                    g_passThrough.store(1, std::memory_order_release);
                }
            }
            return 0;
        }
        if (wParam == kSettingsTimerId) {
            g_consume.store(ReadConsumeSetting() ? 1 : 0,
                            std::memory_order_release);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

DWORD ParsePid(LPWSTR cmd) {
    if (cmd == nullptr) {
        return 0;
    }
    const wchar_t* p = wcsstr(cmd, L"--pid");
    if (p == nullptr) {
        return 0;
    }
    p += 5;
    while (*p == L' ' || *p == L'=') {
        ++p;
    }
    DWORD n = 0;
    while (*p >= L'0' && *p <= L'9') {
        n = n * 10u + static_cast<DWORD>(*p - L'0');
        ++p;
    }
    return n;
}

} /* namespace */

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmd, int) {
    SetUnhandledExceptionFilter(HelperCrashFilter);
    g_taskbarPid = ParsePid(cmd);
    g_consume.store(ReadConsumeSetting() ? 1 : 0, std::memory_order_release);
    g_lastHeartbeatTick = GetTickCount();

    HandleCloser winKey(CreateEventW(nullptr, FALSE, FALSE, kWinKeyEventName));
    HandleCloser heartbeat(CreateEventW(nullptr, FALSE, FALSE, kHeartbeatEventName));
    g_winKeyEvent = winKey.get();
    g_heartbeatEvent = heartbeat.get();
    if (!g_winKeyEvent || !g_heartbeatEvent) {
        return 1;
    }

    HandleCloser proc;
    if (g_taskbarPid != 0) {
        proc.reset(OpenProcess(SYNCHRONIZE, FALSE, g_taskbarPid));
        g_taskbarProcess = proc.get();
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HelperWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"Win7Taskbar.StartHelper";
    if (RegisterClassExW(&wc) == 0) {
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"",
                                WS_OVERLAPPED, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        return 1;
    }
    g_helperWnd.store(hwnd, std::memory_order_release);

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                               GetModuleHandleW(nullptr), 0);
    HookCloser hookGuard(g_hook);
    if (!g_hook) {
        return 1;
    }

    SetTimer(hwnd, kWatchdogTimerId, 500, nullptr);
    SetTimer(hwnd, kSettingsTimerId, 1000, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_passThrough.store(1, std::memory_order_release);
    g_helperWnd.store(nullptr, std::memory_order_release);
    return 0;
}
