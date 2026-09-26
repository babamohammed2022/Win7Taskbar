// Win7Taskbar - Core nativo - client ITaskbarList3 (11)
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.
//
// 11_taskbar_list3 (snippet integration). The Win7 taskbar COM surface
// an app uses for progress/overlay/thumbbar/tab APIs is the PUBLIC
// ITaskbarList3 (shobjidl_core.h, shobjidl.idl — documented on MSDN).
// The snippet's private-interface neighbours (ITaskGroup, ITaskItem,
// IRunnableTaskScheduler2, TOID_ResolveWindow) are NOT used here:
// undocumented, unstable and ABI-hostile to MinGW COM (project rule).
// The snippet's vtable-offset map is deliberately not reproduced: the
// SDK interface IS the ABI (project rule §7 — nothing derived is
// needed to build or run).
//
// Threading: created on the UI (STA) thread that owns the taskbar
// window; CoInitializeEx(COINIT_APARTMENTTHREADED) is balanced here if
// this client performs the initialization (documented in
// docs/native-module-notes.md). Per-monitor windows live on their
// creating UI thread (same rule as TaskbarWindow).

#pragma once

#include <windows.h>
#include <objbase.h>
#include <shobjidl_core.h>

#include <utility>

#include "../include/RaiiWrappers.h"

namespace w7t {

class TaskbarListClient {
public:
    TaskbarListClient();
    ~TaskbarListClient();

    TaskbarListClient(TaskbarListClient&& other) noexcept;
    TaskbarListClient& operator=(TaskbarListClient&& other) noexcept;
    TaskbarListClient(const TaskbarListClient&) = delete;
    TaskbarListClient& operator=(const TaskbarListClient&) = delete;

    bool valid() const noexcept { return static_cast<bool>(m_list); }

    /* All methods: HRESULT from the documented ITaskbarList3 surface. */
    HRESULT HrInit();
    HRESULT ActivateTab(HWND hwnd);
    HRESULT DeleteTab(HWND hwnd);
    HRESULT MarkFullscreenWindow(HWND hwnd, BOOL fullscreen);
    HRESULT RegisterTab(HWND hwndTab, HWND hwndMDI);
    HRESULT UnregisterTab(HWND hwndTab);
    HRESULT SetTabOrder(HWND hwndTab, HWND hwndInsertBefore);
    HRESULT SetTabActive(HWND hwndTab, HWND hwndMDI, DWORD flags);
    HRESULT SetOverlayIcon(HWND hwnd, HICON overlay, LPCWSTR description);
    HRESULT SetProgressState(HWND hwnd, TBPFLAG state);
    HRESULT SetProgressValue(HWND hwnd, ULONGLONG current, ULONGLONG total);
    HRESULT SetThumbnailClip(HWND hwnd, RECT* clip);
    HRESULT SetThumbnailTooltip(HWND hwnd, LPCWSTR tooltip);

private:
    raii::ComPtr<ITaskbarList3> m_list;
    bool m_coInitHere = false;   /* we called CoInitializeEx: balance it */
};

} // namespace w7t
