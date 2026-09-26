// Win7Taskbar - Core nativo - subclassing RAII (comctl32 v6)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 10_subclass_guard (snippet integration): SetWindowSubclass is the
// documented, multi-subclass-safe and thread-safe way to add behaviour
// to a window (the pre-Vista pattern of swapping GWLP_USERDATA thunks is
// what older shells did by hand; it breaks with two subclasses).
// Usage: own a WindowSubclass for the lifetime of the hook; inside the
// proc call call_default() to reach DefSubclassProc.

#pragma once

#include <windows.h>
#include <commctrl.h>   /* SetWindowSubclass / DefSubclassProc (comctl32 v6) */
#include <cstdint>
#include <utility>

namespace w7t {

class WindowSubclass {
public:
    /* comctl32 subclass proc signature. */
    using Proc = LRESULT (CALLBACK*)(HWND, UINT, WPARAM, LPARAM,
                                    UINT_PTR /*subclass id*/, DWORD_PTR /*ref data*/);

    WindowSubclass(HWND hwnd, Proc proc, UINT_PTR id, DWORD_PTR refData = 0);
    ~WindowSubclass();

    WindowSubclass(WindowSubclass&& other) noexcept;
    WindowSubclass& operator=(WindowSubclass&& other) noexcept;
    WindowSubclass(const WindowSubclass&) = delete;
    WindowSubclass& operator=(const WindowSubclass&) = delete;

    HWND hwnd() const noexcept { return m_hwnd; }
    UINT_PTR id() const noexcept { return m_id; }
    bool installed() const noexcept { return m_installed; }

    /* Forwards to the next proc in the subclass chain. */
    static LRESULT CallDefault(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

private:
    void Uninstall() noexcept;

    HWND m_hwnd = nullptr;
    Proc m_proc = nullptr;
    UINT_PTR m_id = 0;
    DWORD_PTR m_refData = 0;
    bool m_installed = false;
};

} // namespace w7t
