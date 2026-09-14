/* Win7Taskbar - native core - "Notification Area Icons" page
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ============================================================================
 * WHAT THIS IS
 *
 * A Win32 recreation of the Windows 7 Control Panel page "Select which
 * icons and notifications appear on the taskbar" (Notification Area
 * Icons), scoped DOWN by design: it lists and configures ONLY the icons
 * the Win7Taskbar tray itself manages (the native TrayService model), and
 * it NEVER touches Explorer's real tray or the TrayNotify registry keys.
 *
 * Architecture decisions, in the order the prompt asked them to be
 * weighed:
 *
 *  - NO CONTROL PANEL HOSTING, NO REGISTRATION, NO REGISTRY AT ALL.
 *    The hosting question (CLSID + canonical name registered on-demand
 *    behind a scope guard that deregisters in every exit path) only
 *    exists if the page has to live inside control.exe. This page is a
 *    fully proprietary window of OUR process, so the whole failure class
 *    (orphaned keys after a crash, failed host spawn, cleanup races)
 *    cannot occur. That is the alternative the prompt names as
 *    preferred, taken literally: the dialog is modeless like the
 *    Properties window, created on the thread of whoever asked for it
 *    (managed UI thread via W7T_TrayCplShow, Properties button, overflow
 *    footer link), it simply appears in no "All Control Panel Items"
 *    list, and deleting the program leaves nothing behind.
 *    Trade-off accepted deliberately: the page is not openable from the
 *    system Control Panel - by request, it must not appear there.
 *
 *  - Storage: trayicons.ini in %LOCALAPPDATA%\Win7Taskbar (TrayPrefsStore).
 *    Changes from this page apply to TrayService immediately (the app
 *    never needs a restart); Cancel rewinds everything to the snapshot
 *    taken when the page opened.
 *
 *  - Layout fidelity: the dialog template lives in resources/app.rc
 *    (written from zero here); the numbers reproduce the 96-DPI metrics of
 *    the original page and the procedure re-verifies them scaled by the
 *    DPI of the monitor showing the dialog. The Win7 original is NOT a
 *    SysListView32 - it is a DUI ScrollViewer with one in-place themed
 *    combo per row (see docs) - so the combo is a single real ComboBox
 *    parked over the row being edited, exactly what the DUI did, while
 *    the row itself paints a themed combo face for every icon.
 *
 *  - "Turn system icons on or off" is page 2 of the SAME window (the real
 *    one is a second navigation page): the three kinds TrayService can own
 *    (network, volume, battery) get an on/off switch each; "off" removes
 *    the icon from bar, overflow AND notifications.
 *
 *  - "Restore default icon behaviors" clears the stored choices and lets
 *    the shell rule (EnableAutoTray) apply again, like the original link.
 *
 * Threading: every TrayService call below is lock-safe; the dialog is
 * modeless so the tray keeps animating and updating live while it is open
 * (that is also why modeless, not DialogBoxParam: a modal loop on the
 * managed UI thread would freeze the taskbar for the whole edit).
 * ============================================================================
 */

#ifndef W7T_TRAY_CPL_DIALOG_H
#define W7T_TRAY_CPL_DIALOG_H

#include "Common.h"
#include "../include/RaiiWrappers.h"
#include "TrayService.h"
#include <vector>

namespace w7t {

class TrayCplDialog {
public:
    static TrayCplDialog& Instance();

    /* Create (or raise, if already open) the page. Runs on the CALLING
     * thread - the caller's message pump owns the dialog from then on,
     * exactly like the Properties window. owner may be null (the overflow
     * thread opens it without an owner). Returns 1 = opened, 0 = already
     * open and raised, negative = W7T_* failure (never throws). */
    int32_t Show(HWND owner);

    bool IsOpen() const { return m_hWnd != nullptr; }

private:
    TrayCplDialog() = default;
    TrayCplDialog(const TrayCplDialog&) = delete;
    TrayCplDialog& operator=(const TrayCplDialog&) = delete;

    /* --- dialog plumbing ------------------------------------------------ */
    static INT_PTR CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK ListSubclassProc(HWND, UINT, WPARAM, LPARAM,
                                             UINT_PTR, DWORD_PTR);

    INT_PTR OnCommand(WPARAM wp, LPARAM lp);
    INT_PTR OnNotify(WPARAM wp, LPARAM lp);
    INT_PTR OnCustomDraw(NMHDR* nmhdr);
    void    Layout();                       /* DPI-scaled placement       */
    void    ApplyLanguage();                /* strings for both pages     */
    void    ShowPage(int page);             /* 1 icons, 2 system icons    */

    /* --- data ----------------------------------------------------------- */
    /* force=true rebuilds even on an identical set (DPI change re-renders
     * the icons at the new size). Otherwise a diff keeps the page still. */
    void    RebuildRows(bool force);        /* snapshot + icons + rows     */
    void    FreeRowIcons();
    int     EffectiveSelected(int rowIndex) const; /* combo index 0..2     */
    void    BeginRowEdit(int rowIndex);     /* park the combo on the row   */
    void    EndRowEdit(bool apply);         /* optionally commit selection */
    void    ToggleAlwaysShow();
    void    ToggleSystemIcon(int32_t kind);
    void    RestoreDefaults();
    void    TakeSnapshot();
    void    RollbackToSnapshot();           /* Cancel semantics            */

    struct RowIcon { raii::IconHandle handle; };

    HWND m_hWnd        = nullptr;
    HWND m_list        = nullptr;
    HWND m_combo       = nullptr;   /* the one in-place editor            */
    HWND m_check       = nullptr;
    HWND m_linkSystem  = nullptr;
    HWND m_linkRestore = nullptr;
    HWND m_back        = nullptr;
    HWND m_chkNet      = nullptr;
    HWND m_chkVol      = nullptr;
    HWND m_chkBat      = nullptr;
    HFONT m_font       = nullptr;   /* owned, RAII via ~ + reopen         */
    HFONT m_fontBold   = nullptr;
    /* HIMAGELIST, kept opaque here so the header does not have to pull
     * <commctrl.h> into every translation unit that includes it. */
    void* m_rowSizer = nullptr;   /* transparent row-height image      */
    UINT m_dpi         = 96;
    int  m_page        = 1;
    int  m_editRow     = -1;        /* row being edited (-1: none)        */
    int  m_hotRow      = -1;        /* hover highlight (subclass-tracked) */

    std::vector<TrayCplRow>   m_rows;
    std::vector<RowIcon>      m_icons;   /* parallel to m_rows            */

    /* Cancel snapshot: what the page found when it opened. */
    struct SnapshotEntry { uint64_t ownerHwnd; uint32_t uid; int32_t behavior; };
    std::vector<SnapshotEntry> m_snapshot;
    bool m_snapAlwaysShow = false;
    bool m_snapSystem[4]  = { true, true, true, true };
    bool m_snapValid      = false;
};

} /* namespace w7t */

#endif /* W7T_TRAY_CPL_DIALOG_H */
