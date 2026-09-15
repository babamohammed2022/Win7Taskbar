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
// ScopeGuard: generic cleanup-on-scope-exit
// v4.9: inspired by GSL ScopeGuard. Executes the cleanup function
// when the guard goes out of scope, swallowing any exceptions to
// prevent stack unwinding conflicts. Use for ad-hoc cleanup that
// doesn't fit a typed RAII wrapper.
// ------------------------------------------------------------
template <class Fn>
class ScopeGuard {
    Fn fn_;
    bool active_ = true;
public:
    explicit ScopeGuard(Fn fn) : fn_(std::move(fn)) {}
    ~ScopeGuard() { if (active_) { try { fn_(); } catch (...) {} } }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&& other) noexcept : fn_(std::move(other.fn_)), active_(other.active_) {
        other.active_ = false;
    }
    void dismiss() noexcept { active_ = false; }
};

template <class Fn>
ScopeGuard<Fn> MakeScopeGuard(Fn fn) { return ScopeGuard<Fn>(std::move(fn)); }

} // namespace raii
} // namespace w7t
