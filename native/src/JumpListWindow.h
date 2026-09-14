// Win7Taskbar - Windows 7 style Jump List for taskbar buttons
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// The popup is opened by the left-button press + upward drag gesture that
// the managed side runs as a state machine (TaskbarWindow.JumpList.cs):
// the opening release leaves the list visible. While the drag owns input,
// positions arrive here as SCREEN PHYSICAL PIXELS through SetHover and the
// popup remains WS_EX_NOACTIVATE. On release MakeInteractive transfers
// focus/input to this window; a separate click selects a row, Escape or a
// click anywhere outside dismisses it.
//
// Content rules (project instructions for this subsystem):
//   - entries come ONLY from the public Shell Jump List APIs
//     (IApplicationDocumentLists - the documented read side); nothing is
//     ever invented for an application that exposes no list;
//   - application identity is the AppUserModelID of the group's window,
//     else the shell metadata of the pinned shortcut, else the default id
//     Windows derives from the executable path (see appids.md, MSDN);
//   - the two standard Windows 7 rows (application link + "Pin/Unpin this
//     program to the taskbar") act on the group's own window/shortcut data.
//
// All geometry constants are 96-DPI reference values scaled by the DPI of
// the monitor hosting the taskbar button (GetDpiForScreenRect), so the
// popup keeps Windows 7 proportions at 100/125/150/200% and on mixed-DPI
// multi-monitor setups.
//
// Historical note: before this subsystem existed, v2.38 opened the same
// popup from the right-click; v2.40 moved opening to the drag gesture but
// kept the list disabled. The right-click menu of a taskbar button has
// stayed the Windows 7 context menu ever since and is not touched here.

#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "RaiiWrappers.h"

namespace w7t {

/* One entry of the application's REAL jump list, as returned by
 * IApplicationDocumentLists (ADLT_RECENT / ADLT_FREQUENT). */
struct JumpListDoc {
    std::wstring displayName;   /* file name without extension */
    std::wstring path;          /* full path                   */
    int32_t section;            /* 0 = recent, 1 = frequent    */
};

/* Taskbar edges, same numeric values as the managed TaskbarEdge enum and
 * the Win32 ABE_* constants. */
enum JumpListEdge {
    kEdgeLeft = 0,
    kEdgeTop = 1,
    kEdgeRight = 2,
    kEdgeBottom = 3
};

/* Identity resolution sources reported by ResolveAppUserModelId. */
enum JumpListIdentitySource {
    kSourceNone = 0,
    kSourceWindow = 1,     /* window-level AppUserModelID            */
    kSourceShortcut = 2    /* PKEY on the pinned .lnk (shell metadata) */
};

class JumpListWindow {
public:
    /* Bits reported by ActivateRow: what the release did. */
    enum ActivationBits {
        BitsOpenedDoc    = 1,
        BitsLaunchedApp  = 2,
        BitsPinToggled   = 4
    };

    static JumpListWindow& Instance();

    /* Builds the popup from the group's data, anchored to the button rect
     * (SCREEN PHYSICAL PIXELS), and shows it. Returns the number of shell
     * document entries (>= 0; zero means "only the standard rows") or a
     * negative failure code (-1 COM/Shell failure, -2 window creation
     * failure, -3 bad argument) - the managed side then cancels the
     * gesture. outAppId receives the resolved AppUserModelID for logging
     * (may stay empty when the app exposes none). */
    int32_t Open(const RECT& buttonRectScreen, int32_t edge,
                 const std::wstring& title,
                 const std::wstring& launchPath,
                 const std::wstring& pinnedLnkPath,
                 bool isPinned,
                 HWND representativeHwnd,
                 const std::wstring& exePath,
                 const uint32_t* iconArgb, int iconW, int iconH,
                 int lang,
                 wchar_t* outAppId, int outAppIdCap);

    /* Gesture move: updates the hover row under the screen point and
     * reports whether the point is still inside the interaction area
     * (popup + button + corridor): 1 inside, 0 outside. */
    int32_t SetHover(int32_t screenX, int32_t screenY);

    /* Hands input from the completed drag gesture to the popup itself.
     * The release which opened the list never selects an item: after this
     * call ordinary mouse input is handled by WndProc and deactivation
     * (an outside click) dismisses the list. */
    void MakeInteractive();

    /* Activates one row from an ordinary click in the persistent popup. */
    int32_t ActivateRow(int32_t screenX, int32_t screenY, int32_t* outBits);

    void Hide();
    bool IsVisible() const;

    /* Application identity via public property-store APIs. */
    static std::wstring ResolveAppUserModelId(HWND hwnd,
        const std::wstring& lnkPath, const std::wstring& exePath,
        int32_t& outSource);

    /* Real jump list read for one identity (recent + frequent, max 10
     * entries each). Returns 0 on success (entries may legitimately be
     * empty), negative on hard Shell/COM failure. Never fabricates. */
    static int32_t ReadDocumentLists(const std::wstring& appUserModelId,
        const std::wstring& exePath, std::vector<JumpListDoc>& outDocs);

private:
    JumpListWindow() = default;

    /* One hit-testable row of the popup. The icon is owned by the row and
     * released through the raii handle (move-only row storage). */
    struct Row {
        enum Kind {
            DocRecent = 0, DocFrequent = 1, App = 2, Close = 3, Pin = 4
        };
        Kind kind = App;
        RECT rect = {};
        std::wstring label;
        std::wstring path;
        raii::IconHandle icon;   /* real file icon or null */

        Row() = default;
        Row(Row&&) noexcept = default;
        Row& operator=(Row&&) noexcept = default;
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK OutsideMouseProc(int, WPARAM, LPARAM);
    void RegisterClassOnce();
    void OnPaint(HWND hwnd);
    void BuildRows();
    void Layout();
    void Place(HWND hwnd, const RECT& button, int32_t edge);
    void UpdateInteractionArea();
    int HitRowClient(POINT clientPt) const;
    RECT RowRect(size_t index) const;
    RECT CloseRect() const;
    bool HitCloseClient(POINT clientPt) const;
    bool InInteractionArea(POINT screenPt) const;
    void LaunchApp();
    void CloseRunningApplication();
    void PerformPinOrUnpin();
    void ClearContent();
    int Sc(int v96) const;           /* 96-DPI value -> device px at m_dpi */
    static void GradientRect(HDC hdc, const RECT& r, COLORREF top,
                             COLORREF bottom, COLORREF edge);

    HWND m_hwnd = nullptr;
    bool m_classRegistered = false;

    /* State for one gesture only; rebuilt on every Open. */
    int m_lang = 1;
    UINT m_dpi = 96;
    int32_t m_edge = kEdgeBottom;
    std::wstring m_title;
    std::wstring m_launchPath;
    std::wstring m_pinnedLnk;
    bool m_pinned = false;

    std::vector<JumpListDoc> m_docs;
    std::vector<Row> m_rows;
    raii::BitmapHandle m_appIcon;
    raii::BitmapHandle m_closeNormal;
    raii::BitmapHandle m_closeHover;
    raii::BitmapHandle m_closePressed;
    HWND m_representativeHwnd = nullptr;
    bool m_interactive = false;
    bool m_closeHot = false;
    bool m_closeDown = false;
    HHOOK m_outsideMouseHook = nullptr;

    int m_width = 300;      /* device px, already scaled */
    int m_totalH = 0;       /* device px */
    int m_hover = -1;       /* row index under the cursor, -1 none */
    RECT m_popupRect = {};      /* screen px */
    RECT m_buttonRect = {};     /* screen px */
    RECT m_area = {};           /* interaction area, screen px */
};

} // namespace w7t
