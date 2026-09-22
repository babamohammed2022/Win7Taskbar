// Win7Taskbar - Windows 7 style Jump List for taskbar buttons
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// The popup is opened by the managed state machine
// (TaskbarWindow.JumpList.cs) through one trigger, the right-click of a
// task button NEVER opening it:
//
//   LEFT press + drag away from the bar (up for a bottom bar - the
//   Windows 7 Superbar gesture). The popup opens at the canonical
//   Windows 7 position - directly above the button, LEFT-ALIGNED with
//   its left edge, small gap - and stays there for the whole gesture:
//   the shell places the jump view next to the button it belongs to,
//   not where the cursor wanders. While the drag owns the pointer,
//   moves arrive as SCREEN PHYSICAL PIXELS through SetHover and the
//   popup remains WS_EX_NOACTIVATE. The release on a row activates it;
//   the release over the list or the button persists the list and
//   MakeInteractive transfers focus/input to this window; the release
//   outside the interaction area dismisses it.
//
// Once persistent, a separate click selects a row, Escape or a click
// anywhere outside dismisses the list.
//
// Content rules (project instructions for this subsystem):
//   - entries come ONLY from the public Shell Jump List APIs -
//     IApplicationDocumentLists (Recent/Frequent). The pinned (custom)
//     section is NOT shown: Windows 7 and later expose no public API
//     that reads or removes it (MSDN: the pinned items "cannot be
//     removed programmatically; only the user can remove them"; the
//     Shell reads its own store directly - see
//     docs/JUMPLIST-RE-VERIFICATION.md); nothing is ever invented for
//     an application that exposes no list;
//   - application identity is the AppUserModelID of the group's window,
//     else the shell metadata of the pinned shortcut, else the default id
//     Windows derives from the executable path (see appids.md, MSDN);
//   - the Windows 7 Tasks section (Minimize/Maximize/Restore/Move/Size)
//     appears only while the group has a live window and reuses the
//     existing native window-command path (WindowManager::ExecuteCommand);
//   - Start_JumpListItems = 0 (HKCU\...\Explorer\StartMenu) disables the
//     jump lists, as in Windows 7 (open fails with code -4);
//   - the two standard rows (application link + "Pin/Unpin this program
//     to the taskbar") act on the group's own window/shortcut data.
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
     * failure, -3 bad argument, -4 disabled by the Start_JumpListItems
     * policy) - the managed side then cancels the gesture. outAppId
     * receives the resolved AppUserModelID for logging (may stay empty
     * when the app exposes none). */
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
     * (popup + button + corridor): 1 inside, 0 outside. The popup
     * position itself never moves: it stays anchored to the button,
     * exactly where the Windows 7 shell opens its jump view. */
    int32_t SetHover(int32_t screenX, int32_t screenY);

    /* Row under the screen point, -1 when none; no side effects. The
     * release decision (activate / keep open / cancel) is the managed
     * state machine's, this is only its hit-test. */
    int32_t HitRowAt(int32_t screenX, int32_t screenY) const;

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
            DocRecent = 0, DocFrequent = 1, App = 2, Close = 3, Pin = 4,
            Task = 5          /* window task; cmd = W7T_CMD_* value    */
        };
        Kind kind = App;
        RECT rect = {};
        std::wstring label;
        std::wstring path;
        raii::IconHandle icon;   /* real file icon or null */
        int32_t cmd = 0;         /* Task rows only (W7T_CMD_*) */

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
    /* Work area of the monitor that hosts the button (Place clamps into
     * it; the fallback keeps the popup near its anchor when monitor info
     * is unavailable). */
    RECT WorkAreaForButton() const;
    /* Hover-row update from a screen point (the invalidation pass is
     * shared by SetHover). */
    void UpdateHoverFromScreen(POINT screenPt);
    int HitRowClient(POINT clientPt) const;
    RECT RowRect(size_t index) const;
    RECT CloseRect() const;
    bool HitCloseClient(POINT clientPt) const;
    bool InInteractionArea(POINT screenPt) const;
    void LaunchApp();
    void CloseRunningApplication();
    void PerformPinOrUnpin();
    void ExecuteTask(int32_t cmd);
    std::wstring TooltipFor(const Row& row) const;
    void ShowRowTooltip(int row, POINT clientPt);
    void ClearRowTooltip();
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
    raii::BitmapHandle m_pinIcon;
    raii::BitmapHandle m_closeNormal;
    raii::BitmapHandle m_closeHover;
    raii::BitmapHandle m_closePressed;
    HWND m_representativeHwnd = nullptr;
    bool m_interactive = false;
    bool m_closeHot = false;
    bool m_closeDown = false;
    HHOOK m_outsideMouseHook = nullptr;

    /* The Windows 7 tooltip tracked under the hovered row (name-only when
     * the item has no resolvable path - the NoJumpListPathTooltip case). */
    HWND m_tooltip = nullptr;
    int m_tipRow = -1;
    DWORD m_tipStart = 0;
    bool m_tipShown = false;
    std::wstring m_tipText;    /* tracked tool text (stable while shown) */

    int m_width = 300;      /* device px, already scaled */
    int m_totalH = 0;       /* device px */
    int m_hover = -1;       /* row index under the cursor, -1 none */
    RECT m_popupRect = {};      /* screen px */
    RECT m_buttonRect = {};     /* screen px */
    RECT m_area = {};           /* interaction area, screen px */
};

} // namespace w7t
