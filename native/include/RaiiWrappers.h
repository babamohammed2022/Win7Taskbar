/*
 * Win7Taskbar - RAII wrappers for native resources
 * English: Useful RAII helpers to prevent leaks in native code
 * Italiano: Helper RAII utili per evitare leak nel codice nativo
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 */

#pragma once

#include <windows.h>
#include <objbase.h>
#include <combaseapi.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace w7t {
namespace raii {

// ------------------------------------------------------------
// Generic unique_handle pattern
// ------------------------------------------------------------
template<typename HandleType, typename Deleter>
class unique_handle {
public:
    explicit unique_handle(HandleType h = nullptr) noexcept : handle_(h) {}
    ~unique_handle() { reset(); }

    unique_handle(const unique_handle&) = delete;
    unique_handle& operator=(const unique_handle&) = delete;

    unique_handle(unique_handle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    unique_handle& operator=(unique_handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HandleType get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HandleType release() noexcept {
        HandleType tmp = handle_;
        handle_ = nullptr;
        return tmp;
    }
    void reset(HandleType h = nullptr) noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            Deleter{}(handle_);
        }
        handle_ = h;
    }
private:
    HandleType handle_;
};

// ------------------------------------------------------------
// DC handle wrapper
// ------------------------------------------------------------
struct DcDeleter {
    HWND hwnd = nullptr;
    void operator()(HDC hdc) const noexcept {
        if (hdc) {
            if (hwnd) ReleaseDC(hwnd, hdc);
            else DeleteDC(hdc);
        }
    }
};

class DcHandle {
public:
    explicit DcHandle(HDC hdc = nullptr, HWND hwnd = nullptr) noexcept : hdc_(hdc), hwnd_(hwnd) {}
    ~DcHandle() { reset(); }
    DcHandle(const DcHandle&) = delete;
    DcHandle& operator=(const DcHandle&) = delete;
    DcHandle(DcHandle&& other) noexcept : hdc_(other.hdc_), hwnd_(other.hwnd_) { other.hdc_ = nullptr; }
    DcHandle& operator=(DcHandle&& other) noexcept {
        if (this != &other) { reset(); hdc_ = other.hdc_; hwnd_ = other.hwnd_; other.hdc_ = nullptr; }
        return *this;
    }
    HDC get() const noexcept { return hdc_; }
    operator HDC() const noexcept { return hdc_; }
    explicit operator bool() const noexcept { return hdc_ != nullptr; }
    void reset(HDC hdc = nullptr, HWND hwnd = nullptr) noexcept {
        if (hdc_) {
            if (hwnd_) ReleaseDC(hwnd_, hdc_);
            else DeleteDC(hdc_);
        }
        hdc_ = hdc;
        hwnd_ = hwnd;
    }
    HDC release() noexcept { HDC tmp = hdc_; hdc_ = nullptr; hwnd_ = nullptr; return tmp; }
private:
    HDC hdc_;
    HWND hwnd_;
};

class CompatibleDcHandle {
public:
    explicit CompatibleDcHandle(HDC hdc = nullptr) noexcept : hdc_(hdc) {}
    ~CompatibleDcHandle() { if (hdc_) DeleteDC(hdc_); }
    CompatibleDcHandle(const CompatibleDcHandle&) = delete;
    CompatibleDcHandle& operator=(const CompatibleDcHandle&) = delete;
    CompatibleDcHandle(CompatibleDcHandle&& other) noexcept : hdc_(other.hdc_) { other.hdc_ = nullptr; }
    CompatibleDcHandle& operator=(CompatibleDcHandle&& other) noexcept {
        if (this != &other) { if (hdc_) DeleteDC(hdc_); hdc_ = other.hdc_; other.hdc_ = nullptr; }
        return *this;
    }
    HDC get() const noexcept { return hdc_; }
    operator HDC() const noexcept { return hdc_; }
    explicit operator bool() const noexcept { return hdc_ != nullptr; }
private:
    HDC hdc_;
};

// ------------------------------------------------------------
// Bitmap handle
// ------------------------------------------------------------
struct BitmapDeleter { void operator()(HBITMAP hbmp) const noexcept { if (hbmp) DeleteObject(hbmp); } };
using BitmapHandle = unique_handle<HBITMAP, BitmapDeleter>;

// ------------------------------------------------------------
// Icon handle
// ------------------------------------------------------------
struct IconDeleter { void operator()(HICON hicon) const noexcept { if (hicon) DestroyIcon(hicon); } };
using IconHandle = unique_handle<HICON, IconDeleter>;

// ------------------------------------------------------------
// Menu handle (HMENU).
// v2.6.1: used by ShellMenu.cpp so that popup menus can never
// leak, even on early returns or C++ exceptions.
// NOTE: DestroyMenu() destroys a popup menu AND every submenu
// appended to it, so ownership must be *released* (see
// unique_handle::release) when a child menu is attached to a
// parent with AppendMenuW(MF_POPUPUP, ...) — from that moment
// the parent's wrapper owns the whole tree.
// ------------------------------------------------------------
struct MenuDeleter { void operator()(HMENU hmenu) const noexcept { if (hmenu) DestroyMenu(hmenu); } };
using MenuHandle = unique_handle<HMENU, MenuDeleter>;

// ------------------------------------------------------------
// Registry key handle
// ------------------------------------------------------------
struct RegKeyDeleter { void operator()(HKEY hkey) const noexcept { if (hkey) RegCloseKey(hkey); } };
using RegKeyHandle = unique_handle<HKEY, RegKeyDeleter>;

// ------------------------------------------------------------
// COM initializer RAII
// ------------------------------------------------------------
class ComInitializer {
public:
    ComInitializer() noexcept : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    explicit ComInitializer(DWORD coInit) noexcept : hr_(CoInitializeEx(nullptr, coInit)) {}
    ~ComInitializer() { if (SUCCEEDED(hr_)) CoUninitialize(); }
    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;
    HRESULT result() const noexcept { return hr_; }
    bool succeeded() const noexcept { return SUCCEEDED(hr_); }
private:
    HRESULT hr_;
};

// ------------------------------------------------------------
// Power notification handle (RegisterPowerSettingNotification)
// ------------------------------------------------------------
class PowerNotifyHandle {
public:
    PowerNotifyHandle() noexcept : handle_(nullptr) {}
    explicit PowerNotifyHandle(HPOWERNOTIFY h) noexcept : handle_(h) {}
    ~PowerNotifyHandle() { reset(); }
    PowerNotifyHandle(const PowerNotifyHandle&) = delete;
    PowerNotifyHandle& operator=(const PowerNotifyHandle&) = delete;
    PowerNotifyHandle(PowerNotifyHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    PowerNotifyHandle& operator=(PowerNotifyHandle&& other) noexcept {
        if (this != &other) { reset(); handle_ = other.handle_; other.handle_ = nullptr; }
        return *this;
    }
    HPOWERNOTIFY get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    void reset(HPOWERNOTIFY h = nullptr) noexcept {
        if (handle_) UnregisterPowerSettingNotification(handle_);
        handle_ = h;
    }
    HPOWERNOTIFY release() noexcept { HPOWERNOTIFY tmp = handle_; handle_ = nullptr; return tmp; }
private:
    HPOWERNOTIFY handle_;
};

// ------------------------------------------------------------
// Generic handle (HANDLE)
// ------------------------------------------------------------
struct HandleDeleter { void operator()(HANDLE h) const noexcept { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
using GenericHandle = unique_handle<HANDLE, HandleDeleter>;

// ------------------------------------------------------------
// GDI object selector (auto restore)
// ------------------------------------------------------------
class GdiSelector {
public:
    GdiSelector(HDC hdc, HGDIOBJ obj) noexcept : hdc_(hdc), old_(nullptr) {
        if (hdc_ && obj) old_ = SelectObject(hdc_, obj);
    }
    ~GdiSelector() { if (hdc_ && old_) SelectObject(hdc_, old_); }
    GdiSelector(const GdiSelector&) = delete;
    GdiSelector& operator=(const GdiSelector&) = delete;
private:
    HDC hdc_;
    HGDIOBJ old_;
};

// ------------------------------------------------------------
// Critical section lock guard (for legacy CRITICAL_SECTION)
// ------------------------------------------------------------
class CriticalSectionLock {
public:
    explicit CriticalSectionLock(CRITICAL_SECTION& cs) noexcept : cs_(cs) { EnterCriticalSection(&cs_); }
    ~CriticalSectionLock() { LeaveCriticalSection(&cs_); }
    CriticalSectionLock(const CriticalSectionLock&) = delete;
    CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;
private:
    CRITICAL_SECTION& cs_;
};

// ------------------------------------------------------------
// scope_guard: runs a callable at scope exit (also on exception).
// Used at Win32 boundaries where a leak in a resize/paint loop is
// immediately visible. dismiss() cancels the action.
// ------------------------------------------------------------
template <typename F>
class scope_guard {
public:
    explicit scope_guard(F&& f) noexcept : f_(std::move(f)), active_(true) {}
    scope_guard(scope_guard&& other) noexcept
        : f_(std::move(other.f_)), active_(std::exchange(other.active_, false)) {}
    scope_guard(const scope_guard&) = delete;
    scope_guard& operator=(const scope_guard&) = delete;

    void dismiss() noexcept { active_ = false; }

    ~scope_guard() {
        if (active_) {
            try { f_(); } catch (...) { /* never propagate out of a guard */ }
        }
    }
private:
    F f_;
    bool active_;
};

template <typename F>
scope_guard<std::decay_t<F>> on_scope_exit(F&& f) {
    return scope_guard<std::decay_t<F>>(std::forward<F>(f));
}

// ------------------------------------------------------------
// Typed deleters for the handles that recur in the taskbar code,
// plus unique_handle aliases and adopt helpers (01_raii_win32).
// IconDeleter is the one defined above (line with IconHandle).
// ------------------------------------------------------------
struct HookDeleter   { void operator()(HHOOK h)   const noexcept { if (h)   UnhookWindowsHookEx(h); } };
struct FontDeleter   { void operator()(HFONT h)   const noexcept { if (h)   DeleteObject(h); } };
struct LocalDeleter  { void operator()(HLOCAL h)  const noexcept { if (h)   LocalFree(h); } };
struct GlobalDeleter { void operator()(HGLOBAL h) const noexcept { if (h)   GlobalFree(h); } };
struct CoTaskDeleter { void operator()(void* p)   const noexcept { if (p)   CoTaskMemFree(p); } };

using unique_hicon   = unique_handle<HICON,   IconDeleter>;
using unique_hfont   = unique_handle<HFONT,   FontDeleter>;
using unique_hhook   = unique_handle<HHOOK,   HookDeleter>;
using unique_hlocal  = unique_handle<HLOCAL,  LocalDeleter>;
using unique_hglobal = unique_handle<HGLOBAL, GlobalDeleter>;
using unique_cotask  = unique_handle<void*,   CoTaskDeleter>;

inline unique_hicon  adopt_icon(HICON h)  noexcept { return unique_hicon(h); }
inline unique_hfont  adopt_font(HFONT h)  noexcept { return unique_hfont(h); }
inline unique_hhook  adopt_hook(HHOOK h)  noexcept { return unique_hhook(h); }

// ------------------------------------------------------------
// MemoryDc: compatible DC with a selected bitmap restored safely.
// GdiSelector/SelectGuard already restore a single selection; this
// bundles the DC itself (DeleteDC) with its first selection record.
// ------------------------------------------------------------
class MemoryDc {
public:
    MemoryDc() : hdc_(CreateCompatibleDC(nullptr)) {}
    ~MemoryDc() {
        if (hdc_) {
            if (original_) SelectObject(hdc_, original_);
            DeleteDC(hdc_);
        }
    }
    MemoryDc(const MemoryDc&) = delete;
    MemoryDc& operator=(const MemoryDc&) = delete;

    HBITMAP select_bitmap(HBITMAP bmp) noexcept {
        const HGDIOBJ previous = SelectObject(hdc_, bmp);
        if (!original_) original_ = previous;
        return static_cast<HBITMAP>(previous);
    }
    HDC get() const noexcept { return hdc_; }
private:
    HDC hdc_ = nullptr;
    HGDIOBJ original_ = nullptr;
};

// ------------------------------------------------------------
// ComPtr: minimal COM smart pointer (same shape as the one in
// ImmersiveFlyouts.h). Kept here as the shared toolbox version;
// a uniform merge of the two is a documented follow-up.
// ------------------------------------------------------------
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) noexcept : m_ptr(p) {}
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    T** Put() { Reset(); return &m_ptr; }
    T* Get() const noexcept { return m_ptr; }
    T* operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }
    T* Detach() noexcept { T* p = m_ptr; m_ptr = nullptr; return p; }
    void Reset() {
        if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; }
    }
private:
    T* m_ptr = nullptr;
};

// Throws only on paths that never cross a Win32 window-proc boundary
// (see ExceptionGuards.h for the boundary wrappers).
[[noreturn]] inline void throw_if_failed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + " failed");
    }
}

} // namespace raii
} // namespace w7t
