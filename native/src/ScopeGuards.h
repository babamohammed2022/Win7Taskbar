// Win7Taskbar - RAII scope guards for Win32/GDI resources
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// v1.7.5: shared RAII toolbox. Every guard here is noexcept, move-free
// (scope-bound) and self-contained so both toolchains (MSVC and
// MinGW-w64) compile it identically. The point of these guards is that
// NO early return, exception or maintenance edit can leak a GDI object,
// a device context or a selected bitmap anymore.

#pragma once
#include <windows.h>

namespace w7t {

// Owns one GDI object (brush, pen, font, bitmap) created ad hoc and
// deletes it at scope end. Not for shared stock objects.
class UniqueGdiObject {
public:
    explicit UniqueGdiObject(HGDIOBJ obj) noexcept : m_obj(obj) {}
    ~UniqueGdiObject() noexcept {
        if (m_obj != nullptr) {
            DeleteObject(m_obj);
        }
    }
    UniqueGdiObject(const UniqueGdiObject&) = delete;
    UniqueGdiObject& operator=(const UniqueGdiObject&) = delete;

    bool valid() const noexcept { return m_obj != nullptr; }
    HGDIOBJ get() const noexcept { return m_obj; }
    operator HGDIOBJ() const noexcept { return m_obj; }

private:
    HGDIOBJ m_obj;
};

// Selects an object into a DC and restores the previous one at scope
// end. The DC must outlive this guard.
class SelectGuard {
public:
    SelectGuard(HDC hdc, HGDIOBJ obj) noexcept
        : m_hdc(hdc),
          m_old(obj != nullptr ? SelectObject(hdc, obj) : nullptr) {}
    ~SelectGuard() noexcept {
        if (m_old != nullptr) {
            SelectObject(m_hdc, m_old);
        }
    }
    SelectGuard(const SelectGuard&) = delete;
    SelectGuard& operator=(const SelectGuard&) = delete;

    HGDIOBJ old() const noexcept { return m_old; }

private:
    HDC     m_hdc;
    HGDIOBJ m_old;
};

// Owns a memory DC created with CreateCompatibleDC (DeleteDC at scope
// end).
class MemDcGuard {
public:
    explicit MemDcGuard(HDC compatibleWith) noexcept
        : m_hdc(CreateCompatibleDC(compatibleWith)) {}
    ~MemDcGuard() noexcept {
        if (m_hdc != nullptr) {
            DeleteDC(m_hdc);
        }
    }
    MemDcGuard(const MemDcGuard&) = delete;
    MemDcGuard& operator=(const MemDcGuard&) = delete;

    bool valid() const noexcept { return m_hdc != nullptr; }
    HDC  get() const noexcept { return m_hdc; }
    operator HDC() const noexcept { return m_hdc; }

private:
    HDC m_hdc;
};

// Sets a bool for the current scope and restores it (re-entrancy flags
// that must never stay stuck after an early exit).
class ScopeFlag {
public:
    explicit ScopeFlag(bool& flag) noexcept : m_flag(flag) { m_flag = true; }
    ~ScopeFlag() noexcept { m_flag = false; }
    ScopeFlag(const ScopeFlag&) = delete;
    ScopeFlag& operator=(const ScopeFlag&) = delete;

private:
    bool& m_flag;
};

} /* namespace w7t */
