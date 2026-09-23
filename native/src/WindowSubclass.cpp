// Win7Taskbar - Core nativo - subclassing RAII
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "WindowSubclass.h"

#include <stdexcept>

namespace w7t {

WindowSubclass::WindowSubclass(HWND hwnd, Proc proc, UINT_PTR id, DWORD_PTR refData)
    : m_hwnd(hwnd), m_proc(proc), m_id(id), m_refData(refData) {
    if (hwnd == nullptr || proc == nullptr) {
        throw std::invalid_argument("WindowSubclass: null arguments");
    }
    /* SetWindowSubclass fails across threads: subclass only windows the
     * caller owns (that is a design rule, not a runtime condition). */
    m_installed = SetWindowSubclass(hwnd, proc, id, refData) != FALSE;
    if (!m_installed) {
        throw std::runtime_error("WindowSubclass: SetWindowSubclass failed");
    }
}

WindowSubclass::~WindowSubclass() {
    Uninstall();
}

WindowSubclass::WindowSubclass(WindowSubclass&& other) noexcept
    : m_hwnd(other.m_hwnd), m_proc(other.m_proc), m_id(other.m_id),
      m_refData(other.m_refData), m_installed(std::exchange(other.m_installed, false)) {}

WindowSubclass& WindowSubclass::operator=(WindowSubclass&& other) noexcept {
    if (this != &other) {
        Uninstall();
        m_hwnd = other.m_hwnd;
        m_proc = other.m_proc;
        m_id = other.m_id;
        m_refData = other.m_refData;
        m_installed = std::exchange(other.m_installed, false);
    }
    return *this;
}

void WindowSubclass::Uninstall() noexcept {
    if (m_installed && m_hwnd != nullptr) {
        RemoveWindowSubclass(m_hwnd, m_proc, m_id);
        m_installed = false;
    }
}

} // namespace w7t
