// Win7Taskbar - Core nativo - eventi shell
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "ShellHookReceiver.h"
#include "SehGuard.h"
#include "ScopeGuards.h"

#include <stdexcept>
#include <exception>

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
    /* v3.15: il messaggio SHELLHOOK viaggia dentro un broadcast della
     * shell e il callback utente tocca lo stato della barra (enumerazione
     * finestre, COM, ridisegno). Un'eccezione C++ o un fault hardware che
     * attraversa qui il confine Win32 andrebbe a consumare la coda
     * messaggi a meta' - lo stesso "hang" visto in verticale. Il messaggio
     * viene risposto sempre coerentemente; l'evento utente e' protetto. */
    ShellHookEvent ev;
    ev.code = static_cast<int>(wp);
    ev.hwnd = reinterpret_cast<HWND>(lp);
    if (m_onEvent) {
        W7T_SEH_TRY {
            try {
                m_onEvent(ev);
            } catch (...) {
                /* L'hook non deve MAI propagare eccezioni C++ attraverso i
                 * frame del sistema (UB -> in pratica crash dentro user32,
                 * oppure una coda messaggi shell che resta bloccata). */
            }
        } W7T_SEH_CATCH {
            /* hardware fault nel callback: la coda messaggi resta viva */
        } W7T_SEH_END
    }
    return true;
}

} // namespace w7t
