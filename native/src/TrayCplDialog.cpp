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
 */

#include "TrayCplDialog.h"
#include "TrayCplRes.h"
#include "Strings.h"
#include "TrayPrefsStore.h"
#include "ScopeGuards.h"
#include "SehGuard.h"

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <string>
#include <vector>

/* msimg32 (GradientFill) and comctl32 (subclassing, ListView/ImageList)
 * are already in the project's link list (native/CMakeLists.txt), so no
 * pragma-lib is needed here - and zig's driver would only warn about it. */
namespace w7t {

namespace {

/* ------------------------------------------------------------------ */
/*  Metrics of the Windows 7 page, in 96-DPI reference pixels.        */
/*  The .rc template carries the same numbers in dialog units; this   */
/*  file is the authority at runtime: everything the user sees goes    */
/*  through Sc(), so one DPI reading (the monitor showing the dialog)   */
/*  drives fonts, rows, columns and the command band alike.           */
/*                                                                     */
/*  Sources (measured on the original page, see docs/FEATURE-STATUS.md */
/*  jump-list notes for the palette): header + description block, the  */
/*  "always show" checkbox, the list with its 23px header band and     */
/*  44px rows (icon 16, name line, tooltip caption, 150px in-row      */
/*  combo), then the command band with the two links and OK/Cancel.   */
/* ------------------------------------------------------------------ */
constexpr int kDlgWidth      = 460;   /* client width                    */
constexpr int kClientH       = 397;   /* client height                   */
constexpr int kSidePad        = 12;   /* left/right page padding         */
constexpr int kTitleY          = 12;  constexpr int kTitleH = 15;
constexpr int kDescY           = 31;  constexpr int kDescH   = 28;
constexpr int kCheckY          = 64;  constexpr int kCheckH  = 16;
constexpr int kLinkY           = 86;  constexpr int kLinkH   = 14;
constexpr int kListY          = 106;
constexpr int kRowH            = 44;   /* list row: header band 23 + the
                                        * two text lines + the 21-row combo
                                        * fit inside it (96-DPI reference) */
constexpr int kIconSlot        = 16;   /* small tray icon                */
constexpr int kComboW          = 150;  /* the DUI's "width 150rp"        */
constexpr int kComboH          = 21;
constexpr int kComboGapRight   = 10;   /* gap between combo and row edge  */
constexpr int kListBottomGap   = 12;
constexpr int kFooterH         = 47;   /* command band under the list     */
constexpr int kButtonW         = 76;
constexpr int kButtonH         = 23;
constexpr int kButtonGap        = 7;
constexpr int kListH           = kClientH - kListY - kFooterH - kListBottomGap;

constexpr UINT_PTR kRefreshTimer = 0xC91;  /* live tray changes (1 s)     */

/* The Win7 page is a light control-panel surface: white body, near-white
 * command band split by a hairline, Aero hover exactly like the project's
 * tray menus. No theme part is borrowed from Windows binaries - these are
 * colors re-created by measurement (see the palette notes in
 * docs/FEATURE-STATUS.md). */
constexpr COLORREF kBodyBg      = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kBandBg      = RGB(0xF2, 0xF6, 0xFB);
constexpr COLORREF kBandLine    = RGB(0xC9, 0xD6, 0xE6);
constexpr COLORREF kHoverTop    = RGB(0xED, 0xF6, 0xFD);
constexpr COLORREF kHoverBottom = RGB(0xC5, 0xE1, 0xF7);
constexpr COLORREF kHoverBorder = RGB(0x94, 0xC6, 0xEF);
constexpr COLORREF kSelFill     = RGB(0xC5, 0xE1, 0xF7);
constexpr COLORREF kComboBorder = RGB(0x7A, 0x96, 0xB2);
constexpr COLORREF kComboButton = RGB(0xE9, 0xF3, 0xFB);
constexpr COLORREF kCaptionFg   = RGB(0x6C, 0x6C, 0x6C);
constexpr COLORREF kTextFg      = RGB(0x1E, 0x39, 0x57);

int Sc(UINT dpi, int refPx) { return MulDiv(refPx, static_cast<int>(dpi), 96); }

/* A GetDC/ReleaseDC pair that no early return can escape (the shared
 * guards in ScopeGuards.h do not cover this pair; one tiny local guard
 * beats repeating ReleaseDC on every path). */
class ReleaseDcGuard {
public:
    ReleaseDcGuard(HWND hwnd, HDC hdc) : m_hwnd(hwnd), m_hdc(hdc) {}
    ~ReleaseDcGuard() { if (m_hdc != nullptr) { ReleaseDC(m_hwnd, m_hdc); } }
    ReleaseDcGuard(const ReleaseDcGuard&) = delete;
    ReleaseDcGuard& operator=(const ReleaseDcGuard&) = delete;
    HDC  get()   const { return m_hdc; }
    bool valid() const { return m_hdc != nullptr; }
private:
    HWND m_hwnd;
    HDC  m_hdc;
};

/* GradientFill lives in msimg32 (already linked by the project); it is
 * loaded defensively so an unusable system DLL can never crash a row. */
typedef BOOL(WINAPI* GradientFillFn)(HDC, PTRIVERTEX, int, LPVOID, int, DWORD);

GradientFillFn QueryGradientFill() {
    static GradientFillFn fn = [] {
        HMODULE mod = LoadLibraryExW(L"msimg32.dll", nullptr,
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
        return mod != nullptr
            ? reinterpret_cast<GradientFillFn>(GetProcAddress(mod, "GradientFill"))
            : nullptr;
    }();
    return fn;
}

/* Scale a tray snapshot (BGRA32, premultiplied) to an HICON of `sizePx`.
 * The AND mask stays all-zero: the 32-bit alpha shapes the icon. Every GDI
 * piece is guard-owned (MemDcGuard CREATES the memory DC from its seed DC -
 * use it as a producer, never as an owner of a hand-made DC), so no early
 * return leaks a DC, a bitmap or a selection. */
HICON IconFromArgb(const ArgbBitmap& bmp, int sizePx) {
    if (bmp.empty() || sizePx <= 0) {
        return nullptr;
    }

    ReleaseDcGuard screenGuard(nullptr, GetDC(nullptr));
    if (!screenGuard.valid()) {
        return nullptr;
    }
    HDC screen = screenGuard.get();

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = bmp.width;
    bi.bmiHeader.biHeight      = -bmp.height;   /* top-down rows */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* srcBits = nullptr;
    HBITMAP hbmSrc = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &srcBits,
                                      nullptr, 0);
    if (hbmSrc == nullptr || srcBits == nullptr) {
        if (hbmSrc != nullptr) { DeleteObject(hbmSrc); }
        return nullptr;
    }
    UniqueGdiObject srcGuard(hbmSrc);
    const size_t copyBytes = static_cast<size_t>(bmp.width) *
                             static_cast<size_t>(bmp.height) * 4;
    CopyMemory(srcBits, bmp.pixels.data(),
               (std::min)(copyBytes, bmp.pixels.size()));

    HBITMAP hbmDst = CreateCompatibleBitmap(screen, sizePx, sizePx);
    if (hbmDst == nullptr) {
        return nullptr;
    }
    UniqueGdiObject dstGuard(hbmDst);

    MemDcGuard srcDcGuard(screen);
    MemDcGuard dstDcGuard(screen);
    if (!srcDcGuard.valid() || !dstDcGuard.valid()) {
        return nullptr;
    }
    HDC srcDc = srcDcGuard.get();
    HDC dstDc = dstDcGuard.get();
    SelectGuard srcSel(srcDc, hbmSrc);
    SelectGuard dstSel(dstDc, hbmDst);

    SetStretchBltMode(dstDc, HALFTONE);
    SetBrushOrgEx(dstDc, 0, 0, nullptr);
    StretchBlt(dstDc, 0, 0, sizePx, sizePx,
               srcDc, 0, 0, bmp.width, bmp.height, SRCCOPY);

    /* 1bpp AND mask, DWORD-aligned rows, all zero: the 32-bit alpha of the
     * color bitmap does the shaping. CreateIconIndirect (not CreateIcon):
     * the mingw and MSVC CreateIcon PROTOTYPES differ in arity (the SDK's
     * documented form has the cbXORBits parameter, mingw's has not) - the
     * ICONINFO form is identical everywhere and keeps this file buildable
     * with both toolchains, which CI checks. */
    const int maskStride = ((sizePx + 31) / 32) * 4;
    std::vector<BYTE> maskBytes(static_cast<size_t>(maskStride) * sizePx, 0);
    HBITMAP hbmMask = CreateBitmap(sizePx, sizePx, 1, 1, maskBytes.data());
    if (hbmMask == nullptr) {
        return nullptr;
    }
    UniqueGdiObject maskGuard(hbmMask);

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmColor = hbmDst;
    ii.hbmMask  = hbmMask;
    return CreateIconIndirect(&ii);
}

/* Implemented further down (same unnamed namespace, file-local): the
 * DPI-scaled placement done at WM_INITDIALOG. */
void CenterWindowOnMonitor(HWND hwnd, UINT dpi, HWND owner);

/* One fully transparent image whose HEIGHT sizes the report rows: the
 * list view derives row height from the small image list, and every
 * visible pixel of a row is painted by the custom draw instead. */
HIMAGELIST CreateRowSizer(UINT dpi) {
    const int w = Sc(dpi, kIconSlot);
    const int h = Sc(dpi, kRowH) - 2;   /* 2px of row padding are the LV's own */
    HIMAGELIST il = ImageList_Create(w, h, ILC_COLOR32 | ILC_MASK, 1, 4);
    if (il == nullptr) {
        return nullptr;
    }
    ReleaseDcGuard screenGuard(nullptr, GetDC(nullptr));
    if (!screenGuard.valid()) {
        ImageList_Destroy(il);
        return nullptr;
    }
    HBITMAP hColor = CreateCompatibleBitmap(screenGuard.get(), w, h);
    std::vector<BYTE> maskBits(static_cast<size_t>(w / 8 + 1) * h, 0xFF);
    HBITMAP hMask = CreateBitmap(w, h, 1, 1, maskBits.data());
    if (hColor != nullptr && hMask != nullptr) {
        UniqueGdiObject colorGuard(hColor);
        UniqueGdiObject maskGuard(hMask);
        /* Mask fully set = image fully transparent. */
        if (ImageList_Add(il, hColor, hMask) >= 0) {
            return il;
        }
    }
    if (hColor != nullptr) { DeleteObject(hColor); }
    if (hMask != nullptr) { DeleteObject(hMask); }
    ImageList_Destroy(il);
    return nullptr;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/*  Singleton + Show                                                  */
/* ------------------------------------------------------------------ */

TrayCplDialog& TrayCplDialog::Instance() {
    static TrayCplDialog s_instance;
    return s_instance;
}

int32_t TrayCplDialog::Show(HWND owner) {
    /* One page at a time: a second request raises the open one. The page
     * is modeless and lives on the CALLING thread's pump (WPF dispatcher
     * for the managed entry, the service pump for the overflow link), so
     * the tray keeps updating live while the user edits it. */
    if (m_hWnd != nullptr && IsWindow(m_hWnd)) {
        ShowWindow(m_hWnd, SW_SHOW);
        SetForegroundWindow(m_hWnd);
        return 0;
    }
    m_hWnd = nullptr;

    HMODULE hMod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&TrayCplDialog::Instance),
                            &hMod)
        || hMod == nullptr) {
        LogTagged(L"TRAYCPL", L"module handle unavailable (code %u)", GetLastError());
        return W7T_ERR_NOT_INIT;
    }

    HRSRC hRsrc = FindResourceW(hMod, MAKEINTRESOURCEW(IDD_TRAYCPL), RT_DIALOG);
    HGLOBAL hMem = hRsrc != nullptr ? LoadResource(hMod, hRsrc) : nullptr;
    LPDLGTEMPLATEW tmpl = hMem != nullptr
        ? static_cast<LPDLGTEMPLATEW>(LockResource(hMem)) : nullptr;
    if (tmpl == nullptr) {
        LogTagged(L"TRAYCPL", L"dialog template IDD_TRAYCPL missing from resources");
        return W7T_ERR_CREATE_WINDOW;
    }

    HWND hwnd = nullptr;
    /* SEH containment at the entry point (same rule as every other native
     * window of the project): a fault while the dialog manager builds the
     * controls must be a logged failure, not a dead process. */
    W7T_SEH_TRY {
        hwnd = CreateDialogIndirectParamW(hMod, tmpl, owner, DlgProc,
                                          reinterpret_cast<LPARAM>(this));
    } W7T_SEH_CATCH {
        LogTagged(L"TRAYCPL", L"dialog creation faulted");
        return W7T_ERR_CREATE_WINDOW;
    } W7T_SEH_END

    if (hwnd == nullptr) {
        LogTagged(L"TRAYCPL", L"dialog creation failed (code %u)", GetLastError());
        return W7T_ERR_CREATE_WINDOW;
    }
    LogTagged(L"TRAYCPL", L"page opened (dpi-scaled, modeless)");
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Dialog procedure                                                  */
/* ------------------------------------------------------------------ */

INT_PTR CALLBACK TrayCplDialog::DlgProc(HWND hwnd, UINT msg,
                                        WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<TrayCplDialog*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        self = reinterpret_cast<TrayCplDialog*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hwnd;

        /* DPI FIRST: monitor of the window as placed by the dialog
         * manager, refined after the explicit sizing below. */
        {
            RECT rc{};
            GetWindowRect(hwnd, &rc);
            self->m_dpi = GetDpiForScreenRect(rc);
        }
        CenterWindowOnMonitor(hwnd, self->m_dpi, GetWindow(hwnd, GW_OWNER));

        const TrayCplStrings& S = TrayCplStringsFor(CurrentLanguage());

        /* The project icon (resources/app.rc, IDI_APPICON = 101) from this
         * module - the dialog is created by the native core, so that is
         * where the resource lives (same access the Properties window
         * uses, for the same reason). */
        {
            HMODULE hModIco = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&TrayCplDialog::DlgProc),
                               &hModIco);
            if (hModIco != nullptr) {
                HICON hBig = static_cast<HICON>(LoadImageW(hModIco, MAKEINTRESOURCEW(101),
                                                           IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
                HICON hSmall = static_cast<HICON>(LoadImageW(hModIco, MAKEINTRESOURCEW(101),
                                                             IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
                if (hBig != nullptr) {
                    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
                }
                if (hSmall != nullptr) {
                    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
                }
            }
        }

        SetWindowTextW(hwnd, S.title);

        const int fontHeight = -MulDiv(9, static_cast<int>(self->m_dpi), 72);
        self->m_font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        self->m_fontBold = CreateFontW(fontHeight, 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        if (self->m_font != nullptr) {
            SendMessageW(hwnd, WM_SETFONT, (WPARAM)self->m_font, TRUE);
            EnumChildWindows(hwnd, [](HWND h, LPARAM fp) -> BOOL {
                SendMessageW(h, WM_SETFONT, fp, TRUE);
                return TRUE;
            }, (LPARAM)self->m_font);
        }

        self->m_list       = GetDlgItem(hwnd, IDC_TRAYCPL_LIST);
        self->m_check      = GetDlgItem(hwnd, IDC_TRAYCPL_CHECK);
        self->m_linkSystem = GetDlgItem(hwnd, IDC_TRAYCPL_LINKSYS);
        self->m_linkRestore= GetDlgItem(hwnd, IDC_TRAYCPL_LINKREST);
        self->m_back       = GetDlgItem(hwnd, IDC_TRAYCPL_BACK);
        self->m_chkNet     = GetDlgItem(hwnd, IDC_TRAYCPL_CHK_NET);
        self->m_chkVol     = GetDlgItem(hwnd, IDC_TRAYCPL_CHK_VOL);
        self->m_chkBat     = GetDlgItem(hwnd, IDC_TRAYCPL_CHK_BAT);

        /* The one in-place editor (created hidden, parked on a row while
         * that row is edited, hidden otherwise). Child of the DIALOG, not
         * of the list: the row it edits lives inside the list, but the
         * editor must never be clipped by the list's client area. */
        self->m_combo = CreateWindowExW(
            0, L"COMBOBOX", L"",
            WS_CHILD | WS_BORDER | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<uintptr_t>(IDC_TRAYCPL_COMBO)),
            nullptr, nullptr);
        if (self->m_combo != nullptr && self->m_font != nullptr) {
            SendMessageW(self->m_combo, WM_SETFONT, (WPARAM)self->m_font, TRUE);
        }

        if (self->m_list != nullptr) {
            SendMessageW(self->m_list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                         LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

            self->m_rowSizer = CreateRowSizer(self->m_dpi);
            if (self->m_rowSizer != nullptr) {
                ListView_SetImageList(self->m_list,
                    static_cast<HIMAGELIST>(self->m_rowSizer), LVSIL_SMALL);
            }

            LVCOLUMNW col = {};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<LPWSTR>(S.colIcons);
            col.iSubItem = 0;
            col.cx = Sc(self->m_dpi, kDlgWidth - 2 * kSidePad) -
                     Sc(self->m_dpi, kComboW + 40);
            ListView_InsertColumn(self->m_list, 0, &col);
            col.pszText = const_cast<LPWSTR>(S.colBehaviors);
            col.iSubItem = 1;
            col.cx = Sc(self->m_dpi, kComboW + 40);
            ListView_InsertColumn(self->m_list, 1, &col);

            SetWindowSubclass(self->m_list, ListSubclassProc, 0,
                              reinterpret_cast<DWORD_PTR>(self));
        }

        self->ApplyLanguage();
        self->RebuildRows(true);
        self->TakeSnapshot();
        self->ShowPage(1);
        self->Layout();

        ShowWindow(hwnd, SW_SHOWNA);
        SetForegroundWindow(hwnd);
        SetTimer(hwnd, kRefreshTimer, 1000, nullptr);
        return TRUE;
    }

    case WM_COMMAND:
        if (self != nullptr) {
            return self->OnCommand(wp, lp);
        }
        return FALSE;

    case WM_NOTIFY:
        if (self != nullptr) {
            return self->OnNotify(wp, lp);
        }
        return FALSE;

    case WM_ERASEBKGND: {
        if (self == nullptr) {
            return FALSE;
        }
        /* Body + command band are painted here (not via brushes): the same
         * two-tone split the Win7 page has, with the hairline separator. */
        HDC hdc = reinterpret_cast<HDC>(wp);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int footerTop = rc.bottom - Sc(self->m_dpi, kFooterH);
        RECT body = { rc.left, rc.top, rc.right, footerTop };
        RECT band = { rc.left, footerTop, rc.right, rc.bottom };
        HBRUSH bodyBrush = CreateSolidBrush(kBodyBg);
        HBRUSH bandBrush = CreateSolidBrush(kBandBg);
        if (bodyBrush != nullptr) {
            FillRect(hdc, &body, bodyBrush);
            DeleteObject(bodyBrush);
        }
        if (bandBrush != nullptr) {
            FillRect(hdc, &band, bandBrush);
            DeleteObject(bandBrush);
        }
        HPEN pen = CreatePen(PS_SOLID, 1, kBandLine);
        if (pen != nullptr) {
            HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));
            MoveToEx(hdc, rc.left, footerTop, nullptr);
            LineTo(hdc, rc.right, footerTop);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        /* Statics/links on the white body take the window-color brush; the
         * "restore defaults" link lives in the band and must not punch a
         * white rectangle through it. */
        if (self == nullptr) {
            return FALSE;
        }
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND child = reinterpret_cast<HWND>(lp);
        SetBkMode(hdc, TRANSPARENT);
        if (child == self->m_linkRestore) {
            static HBRUSH bandBrush = CreateSolidBrush(kBandBg);
            if (bandBrush != nullptr) {
                return reinterpret_cast<INT_PTR>(bandBrush);
            }
        }
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
    }

    case WM_SIZE:
        if (self != nullptr) {
            self->Layout();
        }
        return FALSE;

    case WM_TIMER:
        if (self != nullptr && wp == kRefreshTimer) {
            /* The tray lives while the page is open: icons come and go, so
             * the row set is re-snapshotted. RebuildRows() diffs and paints
             * nothing when nothing changed - no flicker, no log spam. */
            self->RebuildRows(false);
            return TRUE;
        }
        return FALSE;

    case WM_CLOSE:
        if (self != nullptr) {
            self->RollbackToSnapshot();   /* X = Cancel (documented) */
            KillTimer(hwnd, kRefreshTimer);
            DestroyWindow(hwnd);
            return TRUE;
        }
        return FALSE;

    case WM_DESTROY:
        if (self != nullptr) {
            KillTimer(hwnd, kRefreshTimer);
            if (self->m_list != nullptr && IsWindow(self->m_list)) {
                RemoveWindowSubclass(self->m_list, ListSubclassProc, 0);
            }
            if (self->m_rowSizer != nullptr) {
                ImageList_Destroy(static_cast<HIMAGELIST>(self->m_rowSizer));
                self->m_rowSizer = nullptr;
            }
            self->FreeRowIcons();
            if (self->m_font != nullptr) {
                DeleteObject(self->m_font);
                self->m_font = nullptr;
            }
            if (self->m_fontBold != nullptr) {
                DeleteObject(self->m_fontBold);
                self->m_fontBold = nullptr;
            }
            self->m_hWnd = nullptr;
            self->m_list = self->m_combo = self->m_check = nullptr;
            self->m_linkSystem = self->m_linkRestore = self->m_back = nullptr;
            self->m_chkNet = self->m_chkVol = self->m_chkBat = nullptr;
            self->m_editRow = -1;
            self->m_hotRow = -1;
            LogTagged(L"TRAYCPL", L"page closed");
        }
        return TRUE;

    case WM_DPICHANGED: {
        if (self == nullptr) {
            return FALSE;
        }
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
        self->m_dpi = GetDpiForScreenRect(*suggested);

        const int fontHeight = -MulDiv(9, static_cast<int>(self->m_dpi), 72);
        if (self->m_font != nullptr) {
            DeleteObject(self->m_font);
        }
        if (self->m_fontBold != nullptr) {
            DeleteObject(self->m_fontBold);
        }
        self->m_font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        self->m_fontBold = CreateFontW(fontHeight, 0, 0, 0, FW_SEMIBOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
        if (self->m_font != nullptr) {
            SendMessageW(hwnd, WM_SETFONT, (WPARAM)self->m_font, TRUE);
            EnumChildWindows(hwnd, [](HWND h, LPARAM fp) -> BOOL {
                SendMessageW(h, WM_SETFONT, fp, TRUE);
                return TRUE;
            }, (LPARAM)self->m_font);
        }
        if (self->m_list != nullptr && self->m_rowSizer != nullptr) {
            ImageList_Destroy(static_cast<HIMAGELIST>(self->m_rowSizer));
            self->m_rowSizer = CreateRowSizer(self->m_dpi);
            ListView_SetImageList(self->m_list,
                static_cast<HIMAGELIST>(self->m_rowSizer), LVSIL_SMALL);
        }
        self->RebuildRows(true);   /* icons re-rendered at the new size */
        self->Layout();
        return TRUE;
    }
    }
    return FALSE;
}

namespace {

void CenterWindowOnMonitor(HWND hwnd, UINT dpi, HWND owner) {
    /* Center on the work area of the monitor that shows the owner (or the
     * primary one). Coordinates: screen px of the monitor work area; the
     * window size comes from AdjustWindowRectEx over the DPI-scaled client
     * so the frame math is the OS's, not an estimate. */
    RECT work{};
    HMONITOR mon = nullptr;
    if (owner != nullptr && IsWindow(owner)) {
        RECT rc{};
        if (GetWindowRect(owner, &rc)) {
            mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
        }
    }
    if (mon == nullptr) {
        mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    } else {
        work.left = work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const int clientW = Sc(dpi, kDlgWidth);
    const int clientH = Sc(dpi, kClientH);
    RECT want = { 0, 0, clientW, clientH };
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    AdjustWindowRectEx(&want, style, FALSE, exStyle);
    const int winW = want.right - want.left;
    const int winH = want.bottom - want.top;
    const int x = work.left + ((work.right - work.left) - winW) / 2;
    const int y = work.top + ((work.bottom - work.top) - winH) / 2;
    SetWindowPos(hwnd, nullptr, x, y, winW, winH,
                 SWP_NOZORDER | SWP_FRAMECHANGED);
    (void)owner;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/*  List subclass: Enter opens the row editor, scroll closes it,      */
/*  hover drives the highlight (the rows are owner-drawn).            */
/* ------------------------------------------------------------------ */

LRESULT CALLBACK TrayCplDialog::ListSubclassProc(HWND hwnd, UINT msg,
                                                  WPARAM wp, LPARAM lp,
                                                  UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<TrayCplDialog*>(ref);
    if (self == nullptr) {
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_SPACE || wp == VK_F2) {
            const int sel =
                static_cast<int>(ListView_GetNextItem(hwnd, -1, LVNI_SELECTED));
            self->BeginRowEdit(sel >= 0 ? sel : 0);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (self->m_editRow >= 0) {
                self->EndRowEdit(false);
                return 0;
            }
            PostMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        LVHITTESTINFO hit = {};
        hit.pt = p;
        const int idx = ListView_SubItemHitTest(hwnd, &hit);
        if (idx >= 0 && hit.iSubItem == 1) {
            /* Click inside the Behaviors column = click on that row's
             * combo, exactly where Windows 7 put it. Consume the press so
             * the list does not start a drag-select from the editor area. */
            ListView_SetItemState(hwnd, idx, LVIS_SELECTED, LVIS_SELECTED);
            self->BeginRowEdit(idx);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        LVHITTESTINFO hit = {};
        hit.pt = p;
        const int idx = ListView_SubItemHitTest(hwnd, &hit);
        const int row = idx >= 0 ? idx : -1;
        if (row != self->m_hotRow) {
            const int old = self->m_hotRow;
            self->m_hotRow = row;
            if (old >= 0) {
                ListView_RedrawItems(hwnd, old, old);
            }
            if (row >= 0) {
                ListView_RedrawItems(hwnd, row, row);
            }
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        break;
    }

    case WM_MOUSELEAVE:
        if (self->m_hotRow >= 0) {
            const int old = self->m_hotRow;
            self->m_hotRow = -1;
            ListView_RedrawItems(hwnd, old, old);
        }
        break;

    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
        /* A scrolled list would leave the editor floating over the wrong
         * row: commit (if anything was selected) and hide first. */
        if (self->m_editRow >= 0) {
            self->EndRowEdit(true);
        }
        break;

    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  Commands (buttons, links, checkbox, combo editor)                 */
/* ------------------------------------------------------------------ */

INT_PTR TrayCplDialog::OnCommand(WPARAM wp, LPARAM lp) {
    const WORD id = LOWORD(wp);
    const WORD code = HIWORD(wp);
    (void)lp;

    if (id == IDOK) {
        /* Keep everything already applied live and close. */
        KillTimer(m_hWnd, kRefreshTimer);
        DestroyWindow(m_hWnd);
        return TRUE;
    }
    if (id == IDCANCEL) {
        RollbackToSnapshot();
        KillTimer(m_hWnd, kRefreshTimer);
        DestroyWindow(m_hWnd);
        return TRUE;
    }
    if (id == IDC_TRAYCPL_CHECK && code == BN_CLICKED) {
        ToggleAlwaysShow();
        return TRUE;
    }
    if ((id == IDC_TRAYCPL_CHK_NET || id == IDC_TRAYCPL_CHK_VOL ||
         id == IDC_TRAYCPL_CHK_BAT) && code == BN_CLICKED) {
        const int32_t kind = id == IDC_TRAYCPL_CHK_NET ? 1
                           : id == IDC_TRAYCPL_CHK_VOL ? 2 : 3;
        ToggleSystemIcon(kind);
        return TRUE;
    }
    if (id == IDC_TRAYCPL_COMBO && m_editRow >= 0) {
        /* CLOSEUP fires when the dropdown closes (choice made or not);
         * KILLFOCUS covers Tab/Shift-Tab out of the editor. */
        if (code == CBN_CLOSEUP || code == CBN_KILLFOCUS) {
            EndRowEdit(true);
            return TRUE;
        }
    }
    return FALSE;
}

INT_PTR TrayCplDialog::OnNotify(WPARAM wp, LPARAM lp) {
    NMHDR* hdr = reinterpret_cast<NMHDR*>(lp);
    if (hdr == nullptr) {
        return FALSE;
    }
    (void)wp;

    /* SysLink reports clicks through WM_NOTIFY (NM_CLICK), not WM_COMMAND. */
    if (hdr->code == NM_CLICK || hdr->code == NM_RETURN) {
        if (hdr->idFrom == IDC_TRAYCPL_LINKSYS) {
            ShowPage(2);
            return TRUE;
        }
        if (hdr->idFrom == IDC_TRAYCPL_BACK) {
            ShowPage(1);
            return TRUE;
        }
        if (hdr->idFrom == IDC_TRAYCPL_LINKREST) {
            RestoreDefaults();
            return TRUE;
        }
    }

    if (hdr->idFrom == IDC_TRAYCPL_LIST) {
        if (hdr->code == NM_CUSTOMDRAW) {
            return OnCustomDraw(hdr);
        }
        if (hdr->code == LVN_ITEMACTIVATE) {
            const LPNMLISTVIEW lv = reinterpret_cast<LPNMLISTVIEW>(lp);
            BeginRowEdit(lv->iItem);
            return TRUE;
        }
        if (hdr->code == LVN_BEGINDRAG || hdr->code == LVN_ENDSCROLL) {
            EndRowEdit(false);
            return TRUE;
        }
    }
    return FALSE;
}

INT_PTR TrayCplDialog::OnCustomDraw(NMHDR* hdr) {
    LPNMLVCUSTOMDRAW cd = reinterpret_cast<LPNMLVCUSTOMDRAW>(hdr);

    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT: {
        /* The whole row is ours: the default item drawing would fight the
         * two-line layout and the in-row combo, so the row is fully painted
         * here and the default is skipped. All coordinates are the list's
         * client pixels (already DPI-scaled by layout, never mixed with
         * DIPs - this window has none). */
        HDC hdc = cd->nmcd.hdc;
        const int row = static_cast<int>(cd->nmcd.dwItemSpec);
        RECT rc{};
        if (ListView_GetItemRect(m_list, row, &rc, LVIR_BOUNDS) == FALSE) {
            return CDRF_DODEFAULT;
        }

        const bool selected =
            (ListView_GetItemState(m_list, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
        const bool hot = m_hotRow == row;

        if (selected) {
            HBRUSH hb = CreateSolidBrush(kSelFill);
            if (hb != nullptr) {
                FillRect(hdc, &rc, hb);
                DeleteObject(hb);
            }
        } else if (hot) {
            GradientFillFn gradient = QueryGradientFill();
            TRIVERTEX vert[2] = {};
            vert[0].x = rc.left;   vert[0].y = rc.top;
            vert[0].Red   = static_cast<COLOR16>(GetRValue(kHoverTop) << 8);
            vert[0].Green = static_cast<COLOR16>(GetGValue(kHoverTop) << 8);
            vert[0].Blue  = static_cast<COLOR16>(GetBValue(kHoverTop) << 8);
            vert[1].x = rc.right;  vert[1].y = rc.bottom;
            vert[1].Red   = static_cast<COLOR16>(GetRValue(kHoverBottom) << 8);
            vert[1].Green = static_cast<COLOR16>(GetGValue(kHoverBottom) << 8);
            vert[1].Blue  = static_cast<COLOR16>(GetBValue(kHoverBottom) << 8);
            GRADIENT_RECT gr = { 0, 1 };
            if (gradient != nullptr) {
                gradient(hdc, vert, 2, &gr, 1, GRADIENT_FILL_RECT_V);
            } else {
                HBRUSH hb = CreateSolidBrush(kHoverTop);
                if (hb != nullptr) {
                    FillRect(hdc, &rc, hb);
                    DeleteObject(hb);
                }
            }
            HPEN pen = CreatePen(PS_SOLID, 1, kHoverBorder);
            if (pen != nullptr) {
                HPEN old = static_cast<HPEN>(SelectObject(hdc, pen));
                MoveToEx(hdc, rc.left, rc.top, nullptr);
                LineTo(hdc, rc.right, rc.top);
                MoveToEx(hdc, rc.left, rc.bottom - 1, nullptr);
                LineTo(hdc, rc.right, rc.bottom - 1);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }
        } else {
            HBRUSH hb = CreateSolidBrush(kBodyBg);
            if (hb != nullptr) {
                FillRect(hdc, &rc, hb);
                DeleteObject(hb);
            }
        }

        if (row >= 0 && row < static_cast<int>(m_rows.size())) {
            const TrayCplRow& data = m_rows[row];

            if (row < static_cast<int>(m_icons.size())
                && m_icons[row].handle) {
                const int isz = Sc(m_dpi, kIconSlot);
                DrawIconEx(hdc, rc.left + Sc(m_dpi, 6),
                           rc.top + (rc.bottom - rc.top - isz) / 2,
                           m_icons[row].handle.get(), isz, isz, 0, nullptr,
                           DI_NORMAL);
            }

            const int textX = rc.left + Sc(m_dpi, kIconSlot + 10);
            const int labelRight = rc.right - Sc(m_dpi, kComboW + kComboGapRight + 16);

            SetBkMode(hdc, TRANSPARENT);
            HGDIOBJ oldFont = SelectObject(hdc, m_fontBold != nullptr ? m_fontBold : m_font);
            SetTextColor(hdc, kTextFg);
            RECT nameRc = { textX, rc.top + Sc(m_dpi, 4), labelRight,
                            rc.top + Sc(m_dpi, 4) + Sc(m_dpi, 16) };
            DrawTextW(hdc, data.name.c_str(), -1, &nameRc,
                      DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            if (!data.tooltip.empty() || data.appHidden) {
                HGDIOBJ oldF2 = SelectObject(hdc, m_font);
                SetTextColor(hdc, kCaptionFg);
                RECT capRc = { nameRc.left, nameRc.bottom, labelRight,
                               nameRc.bottom + Sc(m_dpi, 16) };
                std::wstring caption = data.tooltip;
                if (data.appHidden && caption.empty()) {
                    caption = L"(\u2014)";
                }
                DrawTextW(hdc, caption.c_str(), -1, &capRc,
                          DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(hdc, oldF2);
            }

            /* Painted combo face for the row; suppressed on the row being
             * edited, where the real ComboBox sits over it. */
            if (m_editRow != row) {
                const int comboW = Sc(m_dpi, kComboW);
                const int comboH = Sc(m_dpi, kComboH);
                RECT rcCombo = {
                    rc.right - Sc(m_dpi, kComboGapRight) - comboW,
                    rc.top + (rc.bottom - rc.top - comboH) / 2,
                    rc.right - Sc(m_dpi, kComboGapRight),
                    rc.top + (rc.bottom - rc.top - comboH) / 2 + comboH
                };
                HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
                if (white != nullptr) {
                    FillRect(hdc, &rcCombo, white);
                    DeleteObject(white);
                }
                HPEN pen = CreatePen(PS_SOLID, 1, kComboBorder);
                HBRUSH oldBrush = nullptr;
                HPEN oldPen = nullptr;
                if (pen != nullptr) {
                    oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
                    oldBrush = static_cast<HBRUSH>(
                        SelectObject(hdc, GetStockObject(NULL_BRUSH)));
                    Rectangle(hdc, rcCombo.left, rcCombo.top,
                              rcCombo.right, rcCombo.bottom);
                    SelectObject(hdc, oldBrush);
                    SelectObject(hdc, oldPen);
                    DeleteObject(pen);
                }
                const int btnW = Sc(m_dpi, 16);
                RECT btnRc = { rcCombo.right - btnW - 1, rcCombo.top + 1,
                               rcCombo.right - 1, rcCombo.bottom - 1 };
                HBRUSH btnBrush = CreateSolidBrush(kComboButton);
                if (btnBrush != nullptr) {
                    FillRect(hdc, &btnRc, btnBrush);
                    DeleteObject(btnBrush);
                }
                {
                    const int cx = btnRc.left + btnW / 2;
                    const int cy = (btnRc.top + btnRc.bottom) / 2;
                    POINT tri[3] = { { cx - Sc(m_dpi, 3), cy - Sc(m_dpi, 1) },
                                     { cx + Sc(m_dpi, 3), cy - Sc(m_dpi, 1) },
                                     { cx, cy + Sc(m_dpi, 2) } };
                    HBRUSH fg = CreateSolidBrush(RGB(0x33, 0x33, 0x33));
                    HPEN triPen = CreatePen(PS_SOLID, 1, RGB(0x33, 0x33, 0x33));
                    if (fg != nullptr && triPen != nullptr) {
                        HBRUSH of = static_cast<HBRUSH>(SelectObject(hdc, fg));
                        HPEN op = static_cast<HPEN>(SelectObject(hdc, triPen));
                        Polygon(hdc, tri, 3);
                        SelectObject(hdc, of);
                        SelectObject(hdc, op);
                    }
                    if (fg != nullptr) { DeleteObject(fg); }
                    if (triPen != nullptr) { DeleteObject(triPen); }
                }

                const TrayCplStrings& S = TrayCplStringsFor(CurrentLanguage());
                const int sel = EffectiveSelected(row);
                const wchar_t* label =
                    sel == kBehaviorShow ? S.show
                  : sel == kBehaviorNotifyOnly ? S.notify
                  : S.hide;
                HGDIOBJ oldF3 = SelectObject(hdc, m_font);
                SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
                RECT labelRc = { rcCombo.left + Sc(m_dpi, 4), rcCombo.top,
                                 btnRc.left - Sc(m_dpi, 3), rcCombo.bottom };
                DrawTextW(hdc, label, -1, &labelRc,
                          DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
                SelectObject(hdc, oldF3);
            }
        }
        return CDRF_SKIPDEFAULT;
    }

    default:
        break;
    }
    return CDRF_DODEFAULT;
}

/* ------------------------------------------------------------------ */
/*  Rows: build + icons + effective selection                         */
/* ------------------------------------------------------------------ */

void TrayCplDialog::RebuildRows(bool force) {
    /* One snapshot call to the service; the dialog never reaches into the
     * model's internals (same contract the overflow panel follows). */
    std::vector<TrayCplRow> fresh;
    const int32_t n = TrayService::Instance().BuildCplRows(fresh);
    if (n < 0) {
        LogTagged(L"TRAYCPL", L"snapshot failed (code %d)", static_cast<int>(n));
        return;
    }

    /* Diff against what is on screen: a steady tray keeps the page still
     * (no icon re-render, no flicker, no log line). */
    bool same = !force && m_rows.size() == fresh.size();
    if (same) {
        for (size_t i = 0; i < fresh.size() && same; ++i) {
            same = fresh[i].key.ownerHwnd == m_rows[i].key.ownerHwnd &&
                   fresh[i].key.uid == m_rows[i].key.uid &&
                   fresh[i].tooltip == m_rows[i].tooltip &&
                   fresh[i].behavior == m_rows[i].behavior &&
                   fresh[i].barVisible == m_rows[i].barVisible;
        }
    }
    if (same) {
        return;
    }

    m_rows = std::move(fresh);

    FreeRowIcons();
    m_icons.clear();
    m_icons.reserve(m_rows.size());
    for (const TrayCplRow& row : m_rows) {
        RowIcon icon;
        HICON h = IconFromArgb(row.bitmap, Sc(m_dpi, kIconSlot));
        if (h != nullptr) {
            icon.handle = raii::IconHandle(h);
        }
        m_icons.push_back(std::move(icon));
    }

    if (m_editRow >= 0) {
        EndRowEdit(false);
    }
    m_hotRow = -1;

    if (m_list != nullptr) {
        ListView_DeleteAllItems(m_list);
        for (size_t i = 0; i < m_rows.size(); ++i) {
            LVITEMW it = {};
            it.mask = LVIF_TEXT | LVIF_PARAM;
            it.iItem = static_cast<int>(i);
            it.iSubItem = 0;
            it.pszText = const_cast<LPWSTR>(m_rows[i].name.c_str());
            it.lParam = static_cast<LPARAM>(i);
            ListView_InsertItem(m_list, &it);
        }
    }
    LogTagged(L"TRAYCPL", L"list rebuilt: %d icon(s)",
              static_cast<int>(m_rows.size()));
}

void TrayCplDialog::FreeRowIcons() {
    m_icons.clear();   /* raii::IconHandle destroys every HICON */
}

int TrayCplDialog::EffectiveSelected(int rowIndex) const {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
        return kBehaviorShow;
    }
    const TrayCplRow& row = m_rows[rowIndex];
    if (row.behavior >= kBehaviorShow && row.behavior <= kBehaviorHide) {
        return row.behavior;
    }
    /* No explicit choice yet: show the EFFECTIVE state (a promoted icon
     * reads "show", an overflowed one reads "only notifications"), like
     * the original page which always displays a concrete position. */
    return row.barVisible ? kBehaviorShow : kBehaviorNotifyOnly;
}

/* ------------------------------------------------------------------ */
/*  The in-place combo editor                                         */
/* ------------------------------------------------------------------ */

void TrayCplDialog::BeginRowEdit(int rowIndex) {
    if (m_combo == nullptr || m_list == nullptr || m_hWnd == nullptr) {
        return;
    }
    if (m_editRow >= 0) {
        EndRowEdit(false);
    }
    if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
        return;
    }
    m_editRow = rowIndex;

    SendMessageW(m_combo, CB_RESETCONTENT, 0, 0);
    const TrayCplStrings& S = TrayCplStringsFor(CurrentLanguage());
    SendMessageW(m_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(S.show));
    SendMessageW(m_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(S.notify));
    SendMessageW(m_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(S.hide));
    SendMessageW(m_combo, CB_SETCURSEL, EffectiveSelected(rowIndex), 0);

    RECT rc{};
    if (ListView_GetItemRect(m_list, rowIndex, &rc, LVIR_BOUNDS) == FALSE) {
        m_editRow = -1;
        return;
    }
    const int comboW = Sc(m_dpi, kComboW);
    const int comboH = Sc(m_dpi, kComboH);
    RECT cell = { rc.right - Sc(m_dpi, kComboGapRight) - comboW,
                  rc.top + (rc.bottom - rc.top - comboH) / 2,
                  rc.right - Sc(m_dpi, kComboGapRight),
                  rc.top + (rc.bottom - rc.top - comboH) / 2 + comboH };
    MapWindowPoints(m_list, m_hWnd, reinterpret_cast<POINT*>(&cell), 2);

    SetWindowPos(m_combo, nullptr, cell.left - 1, cell.top - 1,
                 (cell.right - cell.left) + 2, (cell.bottom - cell.top) + 2,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(m_combo, SW_SHOW);
    SetFocus(m_combo);
    SendMessageW(m_combo, CB_SHOWDROPDOWN, TRUE, 0);
    ListView_RedrawItems(m_list, rowIndex, rowIndex);
}

void TrayCplDialog::EndRowEdit(bool apply) {
    if (m_editRow < 0 || m_combo == nullptr) {
        return;
    }
    const int row = m_editRow;
    m_editRow = -1;

    const int sel = static_cast<int>(SendMessageW(m_combo, CB_GETCURSEL, 0, 0));
    ShowWindow(m_combo, SW_HIDE);

    if (apply && sel >= kBehaviorShow && sel <= kBehaviorHide
        && row >= 0 && row < static_cast<int>(m_rows.size())
        && m_rows[row].behavior != sel) {
        /* Apply NOW - immediate visible effect, no restart, no closing
         * anything. The service persists the choice in trayicons.ini and
         * re-syncs the toolbar model, the overflow panel and the managed
         * view through its normal event path. */
        const int32_t rc = TrayService::Instance().SetBehavior(
            m_rows[row].key.ownerHwnd, m_rows[row].key.uid, sel);
        if (rc == W7T_OK) {
            m_rows[row].behavior = sel;
            /* kindOff cannot apply here: system kinds live on page 2, so
             * the effective bar state is "always-show or the saved show". */
            m_rows[row].barVisible = sel == kBehaviorShow
                || TrayPrefsStore::Instance().AlwaysShow();
            LogTagged(L"TRAYCPL", L"behavior row %d -> state %d", row, sel);
        } else {
            LogTagged(L"TRAYCPL",
                      L"behavior row %d -> state %d FAILED (code %d)",
                      row, sel, static_cast<int>(rc));
        }
    }
    if (m_list != nullptr) {
        ListView_RedrawItems(m_list, row, row);
        SetFocus(m_list);
    }
}

/* ------------------------------------------------------------------ */
/*  Global switches + restore + snapshot                              */
/* ------------------------------------------------------------------ */

void TrayCplDialog::ToggleAlwaysShow() {
    const bool checked =
        SendMessageW(m_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
    TrayPrefsStore::Instance().SetAlwaysShow(checked);
    TrayService::Instance().ApplyVisibilityPolicyChanged();
    LogTagged(L"TRAYCPL", L"always-show-all = %d", checked ? 1 : 0);
    /* The rows keep showing the SAVED choice (the checkbox deliberately
     * does not rewrite it - unchecking brings everything back, as in
     * Windows 7), but the "no explicit choice" rows read their effective
     * state, so repaint the visible range only. */
    if (m_list != nullptr) {
        const int count = ListView_GetItemCount(m_list);
        if (count > 0) {
            ListView_RedrawItems(m_list, 0, count - 1);
        }
    }
}

void TrayCplDialog::ToggleSystemIcon(int32_t kind) {
    const HWND chk = kind == 1 ? m_chkNet : kind == 2 ? m_chkVol : m_chkBat;
    if (chk == nullptr) {
        return;
    }
    const bool on = SendMessageW(chk, BM_GETCHECK, 0, 0) == BST_CHECKED;
    TrayPrefsStore::Instance().SetSystemIconOn(kind, on);
    TrayService::Instance().ApplyVisibilityPolicyChanged();
    LogTagged(L"TRAYCPL", L"system icon %d -> %d",
              static_cast<int>(kind), on ? 1 : 0);
}

void TrayCplDialog::RestoreDefaults() {
    /* The exact job of the Win7 link: every per-icon behavior returns to
     * "whatever the shell rule says". The two global switches stay, they
     * are separate settings on the real page too. */
    const int32_t cleared = TrayService::Instance().ResetUserBehaviors();
    LogTagged(L"TRAYCPL", L"restore defaults: %d behavior(s) cleared",
              static_cast<int>(cleared));
    const bool checked = TrayPrefsStore::Instance().AlwaysShow();
    SendMessageW(m_check, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    RebuildRows(true);
}

void TrayCplDialog::TakeSnapshot() {
    m_snapshot.clear();
    for (const TrayCplRow& row : m_rows) {
        m_snapshot.push_back({ row.key.ownerHwnd, row.key.uid, row.behavior });
    }
    m_snapAlwaysShow = TrayPrefsStore::Instance().AlwaysShow();
    m_snapSystem[1] = TrayPrefsStore::Instance().SystemIconOn(1);
    m_snapSystem[2] = TrayPrefsStore::Instance().SystemIconOn(2);
    m_snapSystem[3] = TrayPrefsStore::Instance().SystemIconOn(3);
    m_snapValid = true;
}

void TrayCplDialog::RollbackToSnapshot() {
    if (!m_snapValid) {
        return;
    }
    m_snapValid = false;   /* once: never fight a second close path */

    int reverted = 0;
    for (const SnapshotEntry& e : m_snapshot) {
        int32_t now = kBehaviorNone;
        if (TrayService::Instance().GetBehavior(e.ownerHwnd, e.uid, &now) == W7T_OK
            && now != e.behavior) {
            TrayService::Instance().SetBehavior(e.ownerHwnd, e.uid, e.behavior);
            ++reverted;
        }
    }
    TrayPrefsStore::Instance().SetAlwaysShow(m_snapAlwaysShow);
    TrayPrefsStore::Instance().SetSystemIconOn(1, m_snapSystem[1]);
    TrayPrefsStore::Instance().SetSystemIconOn(2, m_snapSystem[2]);
    TrayPrefsStore::Instance().SetSystemIconOn(3, m_snapSystem[3]);
    TrayService::Instance().ApplyVisibilityPolicyChanged();

    if (reverted > 0) {
        LogTagged(L"TRAYCPL", L"cancel: %d behavior(s) reverted", reverted);
    }
}

/* ------------------------------------------------------------------ */
/*  Pages + localized strings + geometry                               */
/* ------------------------------------------------------------------ */

void TrayCplDialog::ShowPage(int page) {
    m_page = page;
    if (m_hWnd == nullptr) {
        return;
    }
    const int p1 = page == 1 ? SW_SHOW : SW_HIDE;
    const int p2 = page == 2 ? SW_SHOW : SW_HIDE;
    if (m_editRow >= 0) {
        EndRowEdit(false);
    }
    ShowWindow(GetDlgItem(m_hWnd, IDC_TRAYCPL_TITLE), p1);
    ShowWindow(GetDlgItem(m_hWnd, IDC_TRAYCPL_DESC), p1);
    ShowWindow(m_check, p1);
    ShowWindow(m_linkSystem, p1);
    ShowWindow(m_linkRestore, p1);
    ShowWindow(m_list, p1);
    ShowWindow(m_back, p2);
    ShowWindow(GetDlgItem(m_hWnd, IDC_TRAYCPL_DESC2), p2);
    ShowWindow(m_chkNet, p2);
    ShowWindow(m_chkVol, p2);
    ShowWindow(m_chkBat, p2);
    Layout();
}

void TrayCplDialog::ApplyLanguage() {
    if (m_hWnd == nullptr) {
        return;
    }
    const TrayCplStrings& S = TrayCplStringsFor(CurrentLanguage());
    SetWindowTextW(m_hWnd, S.title);
    SetWindowTextW(GetDlgItem(m_hWnd, IDC_TRAYCPL_TITLE), S.header);
    SetWindowTextW(GetDlgItem(m_hWnd, IDC_TRAYCPL_DESC), S.description);
    SetWindowTextW(m_check, S.alwaysShow);
    /* SysLink renders the Win7-blue link look only inside its <a> markup -
     * the three links get it, everything else stays plain text. */
    SetWindowTextW(m_linkSystem,
                   (L"<a>" + std::wstring(S.linkSystem) + L"</a>").c_str());
    SetWindowTextW(m_linkRestore,
                   (L"<a>" + std::wstring(S.linkRestore) + L"</a>").c_str());
    SetWindowTextW(GetDlgItem(m_hWnd, IDOK), S.ok);
    SetWindowTextW(GetDlgItem(m_hWnd, IDCANCEL), S.cancel);
    SetWindowTextW(m_back,
                   (L"<a>\u2039 " + std::wstring(S.back) + L"</a>").c_str());
    SetWindowTextW(GetDlgItem(m_hWnd, IDC_TRAYCPL_DESC2), S.sysDescription);
    SetWindowTextW(m_chkNet, S.sysNetwork);
    SetWindowTextW(m_chkVol, S.sysVolume);
    SetWindowTextW(m_chkBat, S.sysBattery);

    SendMessageW(m_check, BM_SETCHECK,
                 TrayPrefsStore::Instance().AlwaysShow() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkNet, BM_SETCHECK,
                 TrayPrefsStore::Instance().SystemIconOn(1) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkVol, BM_SETCHECK,
                 TrayPrefsStore::Instance().SystemIconOn(2) ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_chkBat, BM_SETCHECK,
                 TrayPrefsStore::Instance().SystemIconOn(3) ? BST_CHECKED : BST_UNCHECKED, 0);
}

void TrayCplDialog::Layout() {
    if (m_hWnd == nullptr) {
        return;
    }

    const int x = Sc(m_dpi, kSidePad);
    const int innerW = Sc(m_dpi, kDlgWidth) - 2 * x;

    const auto place = [&](int id, int rx, int ry, int rw, int rh) {
        HWND h = GetDlgItem(m_hWnd, id);
        if (h != nullptr) {
            SetWindowPos(h, nullptr, x + rx, ry, rw, rh,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    };

    place(IDC_TRAYCPL_TITLE,   0, Sc(m_dpi, kTitleY), innerW, Sc(m_dpi, kTitleH));
    place(IDC_TRAYCPL_DESC,    0, Sc(m_dpi, kDescY),  innerW, Sc(m_dpi, kDescH));
    place(IDC_TRAYCPL_CHECK,   0, Sc(m_dpi, kCheckY), innerW, Sc(m_dpi, kCheckH));
    place(IDC_TRAYCPL_LINKSYS, innerW - Sc(m_dpi, 200), Sc(m_dpi, kLinkY),
          Sc(m_dpi, 200), Sc(m_dpi, kLinkH));
    place(IDC_TRAYCPL_LIST,    0, Sc(m_dpi, kListY), innerW, Sc(m_dpi, kListH));

    /* page 2 */
    place(IDC_TRAYCPL_BACK,    0, Sc(m_dpi, kTitleY), innerW, Sc(m_dpi, kTitleH));
    place(IDC_TRAYCPL_DESC2,   0, Sc(m_dpi, kDescY),  innerW, Sc(m_dpi, kDescH));
    place(IDC_TRAYCPL_CHK_NET, 0, Sc(m_dpi, kCheckY),        innerW, Sc(m_dpi, kCheckH));
    place(IDC_TRAYCPL_CHK_VOL, 0, Sc(m_dpi, kCheckY + 22),   innerW, Sc(m_dpi, kCheckH));
    place(IDC_TRAYCPL_CHK_BAT, 0, Sc(m_dpi, kCheckY + 44),   innerW, Sc(m_dpi, kCheckH));

    /* Command band: restore link left, Cancel/OK right, band at the very
     * bottom of the client. */
    const int footerTop = Sc(m_dpi, kListY + kListH + kListBottomGap);
    const int btnW = Sc(m_dpi, kButtonW);
    const int btnH = Sc(m_dpi, kButtonH);
    const int btnY = footerTop + (Sc(m_dpi, kFooterH) - btnH) / 2;
    HWND cancel = GetDlgItem(m_hWnd, IDCANCEL);
    if (cancel != nullptr) {
        SetWindowPos(cancel, nullptr, x + innerW - btnW, btnY, btnW, btnH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    HWND ok = GetDlgItem(m_hWnd, IDOK);
    if (ok != nullptr) {
        SetWindowPos(ok, nullptr,
                     x + innerW - btnW * 2 - Sc(m_dpi, kButtonGap),
                     btnY, btnW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_linkRestore != nullptr) {
        SetWindowPos(m_linkRestore, nullptr,
                     x + Sc(m_dpi, 4),
                     footerTop + (Sc(m_dpi, kFooterH) - Sc(m_dpi, 14)) / 2,
                     Sc(m_dpi, 240), Sc(m_dpi, 14),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (m_list != nullptr) {
        const int comboColW = Sc(m_dpi, kComboW + 40);
        ListView_SetColumnWidth(m_list, 1, comboColW);
        ListView_SetColumnWidth(m_list, 0, innerW - comboColW - 2);
    }
}

} /* namespace w7t */
