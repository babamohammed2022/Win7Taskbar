// Win7Taskbar - Core nativo - client ITaskbarList3
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "TaskbarListClient.h"
#include "SehGuard.h"

namespace w7t {

/* v3.15: i forwarding verso ITaskbarList3 girano dentro COM e toccano le
 * finestre di ALTRE applicazioni (AddTab/DeleteTab di terzi nel flusso
 * della nostra barra): una rigida doppia rete SEH + try su OGNI metodo.
 * Un HRESULT cattivo e' un dettaglio; un fault che attraversa questo
 * confine e' un hang di chi ha inviato. */
#define W7T_TB3_SAFE_FORWARD(call)                                        \
    HRESULT w7tHr = E_UNEXPECTED;                                         \
    W7T_SEH_TRY {                                                         \
        try {                                                             \
            w7tHr = m_list ? m_list->call : E_UNEXPECTED;                 \
        } catch (...) {                                                   \
            w7tHr = E_FAIL;                                               \
        }                                                                 \
    } W7T_SEH_CATCH {                                                     \
        w7tHr = E_FAIL;                                                   \
    } W7T_SEH_END                                                         \
    return w7tHr;

TaskbarListClient::TaskbarListClient() {
    /* Apartment check (phase rule: verify CoInit/thread model first):
     * the UI thread is STA. If nobody initialized COM yet, do it here
     * and remember to uninitialize — never double-uninitialize. */
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (co == S_OK || co == S_FALSE) {
        m_coInitHere = (co == S_OK);
        /* RPC_E_CHANGED_MODE: COM already up with another model: keep
         * going (the class factory still works in-process). */
    }

    ITaskbarList3* raw = nullptr;
    const HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&raw));
    if (SUCCEEDED(hr)) {
        /* raii::ComPtr takes ownership (no AddRef round-trip). */
        *m_list.Put() = raw;
    } else if (m_coInitHere) {
        CoUninitialize();
        m_coInitHere = false;
    }
}

TaskbarListClient::~TaskbarListClient() {
    m_list.Reset();
    if (m_coInitHere) {
        CoUninitialize();
        m_coInitHere = false;
    }
}

TaskbarListClient::TaskbarListClient(TaskbarListClient&& other) noexcept
    : m_list(std::move(other.m_list)),
      m_coInitHere(std::exchange(other.m_coInitHere, false)) {}

TaskbarListClient& TaskbarListClient::operator=(TaskbarListClient&& other) noexcept {
    if (this != &other) {
        m_list.Reset();
        if (m_coInitHere) CoUninitialize();
        m_list = std::move(other.m_list);
        m_coInitHere = std::exchange(other.m_coInitHere, false);
    }
    return *this;
}

#define W7T_TB3_FORWARD(call) W7T_TB3_SAFE_FORWARD(call)

HRESULT TaskbarListClient::HrInit() {
    W7T_TB3_FORWARD(HrInit())
}
HRESULT TaskbarListClient::ActivateTab(HWND hwnd) {
    W7T_TB3_FORWARD(ActivateTab(hwnd))
}
HRESULT TaskbarListClient::DeleteTab(HWND hwnd) {
    W7T_TB3_FORWARD(DeleteTab(hwnd))
}
HRESULT TaskbarListClient::MarkFullscreenWindow(HWND hwnd, BOOL fullscreen) {
    W7T_TB3_FORWARD(MarkFullscreenWindow(hwnd, fullscreen))
}
HRESULT TaskbarListClient::RegisterTab(HWND hwndTab, HWND hwndMDI) {
    W7T_TB3_FORWARD(RegisterTab(hwndTab, hwndMDI))
}
HRESULT TaskbarListClient::UnregisterTab(HWND hwndTab) {
    W7T_TB3_FORWARD(UnregisterTab(hwndTab))
}
HRESULT TaskbarListClient::SetTabOrder(HWND hwndTab, HWND hwndInsertBefore) {
    W7T_TB3_FORWARD(SetTabOrder(hwndTab, hwndInsertBefore))
}
HRESULT TaskbarListClient::SetTabActive(HWND hwndTab, HWND hwndMDI, DWORD flags) {
    W7T_TB3_FORWARD(SetTabActive(hwndTab, hwndMDI, flags))
}
HRESULT TaskbarListClient::SetOverlayIcon(HWND hwnd, HICON overlay, LPCWSTR description) {
    W7T_TB3_FORWARD(SetOverlayIcon(hwnd, overlay, description))
}
HRESULT TaskbarListClient::SetProgressState(HWND hwnd, TBPFLAG state) {
    W7T_TB3_FORWARD(SetProgressState(hwnd, state))
}
HRESULT TaskbarListClient::SetProgressValue(HWND hwnd, ULONGLONG current, ULONGLONG total) {
    W7T_TB3_FORWARD(SetProgressValue(hwnd, current, total))
}
HRESULT TaskbarListClient::SetThumbnailClip(HWND hwnd, RECT* clip) {
    W7T_TB3_FORWARD(SetThumbnailClip(hwnd, clip))
}
HRESULT TaskbarListClient::SetThumbnailTooltip(HWND hwnd, LPCWSTR tooltip) {
    W7T_TB3_FORWARD(SetThumbnailTooltip(hwnd, tooltip))
}

#undef W7T_TB3_FORWARD
#undef W7T_TB3_SAFE_FORWARD

} // namespace w7t
