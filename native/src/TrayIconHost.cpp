// Win7Taskbar - Core nativo - host icone area di notifica
// Copyright (c) 2026 Win7Taskbar contributors
// Licensed under the GNU General Public License version 3 or later.

#include "TrayIconHost.h"

#include <algorithm>

#include "PrivateWindowMessages.h"

namespace w7t {

namespace {

using QueryUserNotificationStateFn = HRESULT (WINAPI*)(DWORD*);

/* QUNS_* (public shell header values). */
constexpr DWORD kQunsQuietTime        = 1;
constexpr DWORD kQunsApp              = 2;
constexpr DWORD kQunsPresentationMode = 3;

/* Portable bounded copy (wcsncpy_s is MSVC-only). */
void CopyTip(wchar_t* dst, size_t cap, const std::wstring& src) noexcept {
    if (cap == 0) return;
    const size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    wmemcpy(dst, src.c_str(), n);
    dst[n] = L'\0';
}

} // namespace

TrayIconHost::TrayIconHost(HWND owner, EventFn onEvent)
    : m_owner(owner), m_onEvent(std::move(onEvent)) {
    /* Public documented pattern: the shell broadcasts the registered
     * "TaskbarCreated" message when Explorer (re)builds the tray. */
    m_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}

TrayIconHost::~TrayIconHost() {
    for (const Entry& e : m_icons) {
        NOTIFYICONDATAW data = MakeData(e.id);
        Shell_NotifyIconW(NIM_DELETE, &data);
    }
}

NOTIFYICONDATAW TrayIconHost::MakeData(UINT iconId) const {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = m_owner;
    data.uID = iconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kMsgTrayIconCallback;
    return data;
}

bool TrayIconHost::AddIcon(UINT iconId, HICON icon, const std::wstring& tooltip) {
    NOTIFYICONDATAW data = MakeData(iconId);
    data.hIcon = icon;
    CopyTip(data.szTip, sizeof(data.szTip) / sizeof(wchar_t), tooltip);
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        return false;
    }
    /* NIM_SETVERSION: modern event semantics (NIN_*, wParam=icon id,
     * lParam=packed message) — public Shell_NotifyIcon contract. */
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);

    auto it = std::find_if(m_icons.begin(), m_icons.end(),
                           [iconId](const Entry& e) { return e.id == iconId; });
    if (it != m_icons.end()) {
        it->icon = icon;
        it->tooltip = tooltip;
    } else {
        m_icons.push_back(Entry{ iconId, icon, tooltip });
    }
    return true;
}

bool TrayIconHost::ModifyIcon(UINT iconId, HICON icon, const std::wstring& tooltip) {
    NOTIFYICONDATAW data = MakeData(iconId);
    data.hIcon = icon;
    CopyTip(data.szTip, sizeof(data.szTip) / sizeof(wchar_t), tooltip);
    if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
        return false;
    }
    for (Entry& e : m_icons) {
        if (e.id == iconId) {
            e.icon = icon;
            e.tooltip = tooltip;
        }
    }
    return true;
}

void TrayIconHost::RemoveIcon(UINT iconId) {
    NOTIFYICONDATAW data = MakeData(iconId);
    Shell_NotifyIconW(NIM_DELETE, &data);
    m_icons.erase(std::remove_if(m_icons.begin(), m_icons.end(),
                                 [iconId](const Entry& e) { return e.id == iconId; }),
                  m_icons.end());
}

bool TrayIconHost::ShowBalloon(UINT iconId, const std::wstring& title,
                               const std::wstring& text, DWORD infoFlags,
                               UINT timeoutMs) {
    if (IsQuietTime()) {
        return false;   /* quiet time: no balloons (public behaviour) */
    }
    NOTIFYICONDATAW data = MakeData(iconId);
    data.uFlags |= NIF_INFO;
    data.dwInfoFlags = infoFlags;
    data.uTimeout = timeoutMs;
    CopyTip(data.szInfoTitle, sizeof(data.szInfoTitle) / sizeof(wchar_t), title);
    CopyTip(data.szInfo, sizeof(data.szInfo) / sizeof(wchar_t), text);
    return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

bool TrayIconHost::IsQuietTime() {
    /* shcore!SHQueryUserNotificationState (Win7+): dynamic load, no
     * delay-load dependency for pre-Win7 runtimes. */
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (shcore == nullptr) shcore = LoadLibraryW(L"shcore.dll");
    if (shcore != nullptr) {
        auto query = reinterpret_cast<QueryUserNotificationStateFn>(
            reinterpret_cast<void*>(GetProcAddress(shcore, "SHQueryUserNotificationState")));
        if (query != nullptr) {
            DWORD state = 0;
            if (SUCCEEDED(query(&state))) {
                /* Suppress tips outside quiet time only when the user
                 * is presenting or the session is full-screen; quiet
                 * time itself also means "don't pop balloons". */
                return state == kQunsQuietTime || state == kQunsPresentationMode;
            }
        }
    }
    return false;   /* unknown: show the tip */
}

void TrayIconHost::ReAddAll() {
    /* Shell restarted: re-add every icon from scratch (the public
     * reliable pattern for "TaskbarCreated"). */
    std::vector<Entry> snapshot = m_icons;
    m_icons.clear();
    for (const Entry& e : snapshot) {
        AddIcon(e.id, e.icon, e.tooltip);
    }
}

bool TrayIconHost::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (m_msgTaskbarCreated != 0 && msg == m_msgTaskbarCreated) {
        ReAddAll();
        return true;
    }
    if (msg == kMsgTrayIconCallback) {
        TrayIconEvent ev;
        ev.iconId = static_cast<UINT>(wp);
        ev.message = LOWORD(lp);
        GetCursorPos(&ev.pt);
        if (m_onEvent) m_onEvent(ev);
        return true;
    }
    return false;
}

} // namespace w7t
