// Win7Taskbar - Core nativo - eventi shell
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "ShellHookReceiver.h"

#include <stdexcept>

namespace w7t {

namespace {

/* RegisterShellHookWindow is exported by name from shell32 since
 * Windows 2000 (public, documented in the shell hook samples); link it
 * dynamically to stay loader-safe. DeregisterShellHookWindow same. */
using RegisterShellHookWindowFn = BOOL (WINAPI*)(HWND);
using DeregisterShellHookWindowFn = BOOL (WINAPI*)(HWND);

RegisterShellHookWindowFn LoadRegister() noexcept {
    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    return reinterpret_cast<RegisterShellHookWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(shell32, "RegisterShellHookWindow")));
}

DeregisterShellHookWindowFn LoadDeregister() noexcept {
    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    return reinterpret_cast<DeregisterShellHookWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(shell32, "DeregisterShellHookWindow")));
}

} // namespace

ShellHookReceiver::ShellHookReceiver(HWND hwnd, EventFn onEvent)
    : m_hwnd(hwnd), m_onEvent(std::move(onEvent)) {
    if (hwnd == nullptr) {
        throw std::invalid_argument("ShellHookReceiver: null hwnd");
    }
    /* Public documented pattern: the notification message is the
     * registered message "SHELLHOOK" (NOT a private WM_APP number). */
    m_msgShellHook = RegisterWindowMessageW(L"SHELLHOOK");
    if (m_msgShellHook == 0) {
        throw std::runtime_error("ShellHookReceiver: RegisterWindowMessageW failed");
    }
    auto reg = LoadRegister();
    if (reg == nullptr) {
        throw std::runtime_error("ShellHookReceiver: RegisterShellHookWindow unavailable");
    }
    m_registered = reg(hwnd) != FALSE;
    if (!m_registered) {
        throw std::runtime_error("ShellHookReceiver: RegisterShellHookWindow failed");
    }
}

ShellHookReceiver::~ShellHookReceiver() {
    if (m_registered) {
        auto unreg = LoadDeregister();
        if (unreg != nullptr) unreg(m_hwnd);
        m_registered = false;
    }
}

bool ShellHookReceiver::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg != m_msgShellHook || msg == 0) {
        return false;
    }
    ShellHookEvent ev;
    ev.code = static_cast<int>(wp);
    ev.hwnd = reinterpret_cast<HWND>(lp);
    if (m_onEvent) m_onEvent(ev);
    return true;
}

} // namespace w7t
