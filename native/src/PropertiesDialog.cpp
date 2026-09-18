// Win7Taskbar - finestra Proprieta' Win32 classica (stile Win7)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//

#include "PropertiesDialog.h"
#include "SehGuard.h"
#include "Strings.h"
#include "Common.h"
/* v1.21.7: single source of the system accent colour for the extra settings
 * tab (the same function the recreated Windows 8-style flyout will use when
 * it arrives: here it is needed by the colour swatch). */
#include "ExtraSettings.h"
/* v2.48: il pulsante "Personalizza..." dell'area di notifica usa LO STESSO
 * comando del menu di overflow della barra (W7T_OpenNotificationIconsSettings,
 * definito nel core nativo): nessuna pagina sostitutiva, nessun percorso
 * alternativo inventato qui. */
#include "Win7TaskbarCore.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <uxtheme.h>
/* v1.21.7: ChooseColorW for the colour picker of the extra settings tab.
 * The include must be explicit: WIN32_LEAN_AND_MEAN (CMake) keeps commdlg.h
 * out of windows.h, and without it the colour chooser does not exist at
 * compile time. Link: comdlg32 (see CMakeLists.txt). */
#include <commdlg.h>
#include <cstring>
#include <string>   /* v2.58: std::wstring per il testo unico di "Informazioni" */

#ifndef SIID_TASKBAR
#define SIID_TASKBAR 39
#endif
#ifndef ETDT_ENABLE
#define ETDT_ENABLE 0x00000002
#endif
#ifndef ETDT_USETABTEXTURE
#define ETDT_USETABTEXTURE 0x00000004
#endif
#ifndef ETDT_ENABLETAB
#define ETDT_ENABLETAB (ETDT_ENABLE | ETDT_USETABTEXTURE)
#endif

namespace w7t {

namespace {

/* ============================================================================
 * v2.47: RAII anche qui dentro.
 *
 * Il codice Win32 classico e' pieno di coppie "acquisisci/rilascia" e basta
 * una via d'uscita anticipata per perdere la seconda meta'. ScopeExit lega il
 * rilascio alla distruzione dell'oggetto: qualunque return, eccezione o
 * percorso alternativo passa dal distruttore. Si usa per il buffer del
 * template, per l'HDC del dialogo e per tutto cio' che va liberato.
 * ========================================================================== */
template <typename F>
struct ScopeExit {
    F fn;
    explicit ScopeExit(F f) : fn(f) {}
    ~ScopeExit() { fn(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
};
template <typename F>
ScopeExit<F> MakeScopeExit(F f) { return ScopeExit<F>(f); }

/* Dimensioni in unita' DLU basate sulla mod di riferimento.
 * La larghezza include 16 DLU aggiuntive per non troncare l'etichetta
 * italiana "Gestione attività"; l'altezza ospita la scheda Barre degli
 * strumenti e la seconda riga del gruppo lingua.
 *
 * v1.21.7: the width grows from 278 to 330 DLU for the fourth tab, extra
 * settings. The reason is technical: SysTabControl32 moves the labels that
 * do not fit onto a second row, and that row would be drawn ON TOP of the
 * first group of the page (page controls are not children of the tab: they
 * have fixed coordinates in the dialog and the first row starts at 30 DLU).
 * With four labels on the same row the problem does not exist: 18 DLU of
 * extra margin per side are enough, and the groups of the existing pages
 * grow horizontally without moving a single control by one unit. */
constexpr short MAIN_WIDTH  = 330;
constexpr short MAIN_HEIGHT = 326;

/* Usable width of the tab and of the page groups. */
constexpr short TAB_WIDTH      = 318;
constexpr short GROUP_WIDTH    = 300;
constexpr short PAGE_TEXT_WIDTH = 288;

/* v2.50: le tendine di volume e batteria non si chiamano piu' "mixer
 * classico"/"flyout batteria": dicono a quale VERSIONE del sistema
 * appartiene il flyout, con le stesse parole in ogni lingua (sono nomi di
 * prodotto, come "Windows 7" e "Windows 10/11"). */
constexpr const wchar_t* kFlyoutWin7  = L"Windows 7";
constexpr const wchar_t* kFlyoutWin10 = L"Windows 10/11";
/* v1.7: la voce "Windows 7" apre SEMPRE il riquadro ricreato; la voce
 * "Windows 10/11" punta al riquadro reale della shell (con la chiave
 * legacy applicata solo attorno al tentativo di apertura). */
constexpr const wchar_t* kFlyoutBatteryWin10 = L"Windows 10/11";

enum CtrlId {
    IDC_TAB_MAIN = 100,
    IDC_GRP_CLOCK, IDC_CHK_SECONDS, IDC_TXT_FLYOUT, IDC_CMB_FLYOUT,
    IDC_GRP_SEARCH, IDC_CHK_SEARCH,
    IDC_LBL_TASKMGR, IDC_CMB_TASKMGR,
    IDC_GRP_LANG, IDC_CMB_LANG,
    IDC_GRP_NETFLY, IDC_TXT_NETFLY, IDC_CMB_NETFLY,
    IDC_GRP_SYSFLY, IDC_CHK_CLASSIC_VOL, IDC_CHK_BATT_FLYOUT,
    IDC_GRP_EXIT, IDC_BTN_EXIT,
    IDC_TXT_ABOUT, IDC_TXT_CREDITS,
    /* v1.21.37: avvio automatico con Windows (scheda Informazioni). */
    IDC_CHK_AUTOSTART,
    /* v2.47 */
    IDC_GRP_TASKBAR,
    IDC_LBL_CLOCK, IDC_CMB_CLOCK,
    IDC_LBL_NET, IDC_LBL_LANG, IDC_LBL_VOLUME, IDC_CMB_VOLUME,
    IDC_LBL_BATT, IDC_CMB_BATTERY,
    /* v3.5: indicatore della lingua di input. */
    IDC_LBL_LANGBAR, IDC_CMB_LANGBAR,
    IDC_GRP_NOTIF, IDC_TXT_NOTIF, IDC_BTN_CUSTOMIZE,
    IDC_GRP_AERO, IDC_TXT_AERO, IDC_CHK_AERO,
    IDC_LINK_HELP,
    IDC_TXT_TB_INFO, IDC_CHK_TB_DESKTOP, IDC_CHK_TB_ADDRESS, IDC_CHK_TB_LINKS,
    IDC_LST_TOOLBARS,   /* v2.50: checked list as in the mod */
    /* v1.21.7: fourth tab, extra settings. */
    IDC_TXT_EXTRA_TITLE,
    IDC_GRP_EX_FLYOUT, IDC_LBL_EX_COLOR,
    IDC_RADIO_COLOR_SYS, IDC_RADIO_COLOR_CUSTOM,
    IDC_COLOR_SWATCH, IDC_BTN_PICK_COLOR, IDC_TXT_COLOR_HINT,
    IDC_LBL_EX_PRIVACY, IDC_CMB_EX_PRIVACY, IDC_TXT_PRIVACY_HINT,
    IDC_GRP_EX_TASKBAR, IDC_LBL_EX_THEME, IDC_CMB_EX_THEME,
    IDC_LBL_EX_ICON_ORDER, IDC_TXT_ORDER_HINT,
    IDC_BTN_APPLY = 3000,
};

/* v2.42 (v2.59: unificato) - LA TABELLA DELLE STRINGHE NON STA PIU' QUI.
 *
 * Tutte le lingue supportate e le traduzioni di questa finestra vivono in
 * Strings.h / Strings.cpp: una sola sorgente per il nativo, con l'elenco
 * delle lingue che il selettore qui sotto scorre senza elencare nulla a
 * mano. Qui resta solo il disegno del dialogo.
 */

/* v2.41: etichette delle opzioni orologio in TUTTE le lingue della mod.
 * L'opzione nativa si chiama ora "Windows 7"; quella ricreata "Tema
 * classico (ricreato)" (tradotte).
 * v2.59: l'indice e' quello dell'elenco unico (0=it ... 10=ar); fuori
 * elenco si risponde in inglese, come ovunque nel core. */
struct ClockFlyoutLabels { const wchar_t* recreated; const wchar_t* native; };
static ClockFlyoutLabels ClockLabels(int lang) {
    switch (lang) {
        case 1:  return { L"Classic theme (recreated)", L"Windows 7" };
        case 2:  return { L"Tema clásico (recreado)", L"Windows 7" };
        case 3:  return { L"Thème classique (recréé)", L"Windows 7" };
        case 4:  return { L"Klassisches Thema (nachgebildet)", L"Windows 7" };
        case 5:  return { L"Tema clássico (recriado)", L"Windows 7" };
        case 6:  return { L"Motyw klasyczny (odtworzony)", L"Windows 7" };
        case 7:  return { L"Классическая тема (воссоздано)", L"Windows 7" };
        case 8:  return { L"クラシックテーマ（再現）", L"Windows 7" };
        case 9:  return { L"经典主题（重制）", L"Windows 7" };
        case 10: return { L"السمة الكلاسيكية (أُعيد إنشاؤها)", L"Windows 7" };
        default: return { L"Classic theme (recreated)", L"Windows 7" };
    }
}

HICON GetSystemIcon(int siid) {
    SHSTOCKICONINFO sii{};
    sii.cbSize = sizeof(sii);
    W7T_SEH_TRY
    if (SUCCEEDED(SHGetStockIconInfo(static_cast<SHSTOCKICONID>(siid),
            SHGSI_ICON | SHGSI_SMALLICON, &sii))) {
        return sii.hIcon;
    }
    W7T_SEH_CATCH
    W7T_SEH_END
    return nullptr;
}

/* v2.50: elenco delle barre della pagina 3, costruito come quello della mod
 * (report senza intestazione, caselle di controllo, colonna lunga quanto il
 * controllo). L'ordine Item 0/1/2 e' quello letto in SendApply. */
void InitToolbarsList(HWND hwnd, const PropStrings& S,
                      bool address, bool desktop, bool links) {
    HWND hList = GetDlgItem(hwnd, IDC_LST_TOOLBARS);
    if (!hList) return;

    ListView_SetExtendedListViewStyle(hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    ListView_DeleteAllItems(hList);
    while (ListView_DeleteColumn(hList, 0)) {}

    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH | LVCF_FMT;
    col.fmt  = LVCFMT_LEFT;
    col.cx   = 242;
    ListView_InsertColumn(hList, 0, &col);

    const wchar_t* names[]  = { S.tbAddress, S.tbDesktop, S.tbLinks };
    const bool     states[] = { address, desktop, links };
    for (int i = 0; i < 3; ++i) {
        LVITEMW lvi{};
        lvi.mask   = LVIF_TEXT;
        lvi.iItem  = i;
        lvi.pszText = const_cast<wchar_t*>(names[i]);
        ListView_InsertItem(hList, &lvi);
        ListView_SetCheckState(hList, i, states[i] ? TRUE : FALSE);
    }
}

/* v2.47: visibilita' delle tre pagine. In un unico posto, cosi' l'apertura e
 * il cambio di scheda non possono divergere (era il difetto classico di
 * questo tipo di dialogo: si aggiunge un controllo e ci si dimentica di
 * nasconderlo in una delle due strade). */
void ShowTabPage(HWND hwnd, int page) {
    const bool p1 = (page == 0);
    const bool p2 = (page == 1);
    const bool p3 = (page == 2);
    const bool p4 = (page == 3);   /* v1.21.7: extra settings */

    auto vis = [&](int idc, bool v) {
        if (HWND h = GetDlgItem(hwnd, idc)) {
            ShowWindow(h, v ? SW_SHOW : SW_HIDE);
        }
    };

    /* Pagina 1: orologio, ricerca, flyout, lingua, area di notifica. */
    vis(IDC_GRP_CLOCK, p1); vis(IDC_CHK_SECONDS, p1);
    vis(IDC_LBL_CLOCK, p1); vis(IDC_CMB_CLOCK, p1);
    vis(IDC_GRP_SEARCH, p1); vis(IDC_CHK_SEARCH, p1);
    /* Windows 11 starts at build 22000 (21H2). Windows 10 has only the
     * ordinary taskmgr command, so this selector must not exist there. */
    const bool showTaskManagerChoice = p1 && IsWindows11OrBetter();
    vis(IDC_LBL_TASKMGR, showTaskManagerChoice);
    vis(IDC_CMB_TASKMGR, showTaskManagerChoice);
    vis(IDC_GRP_NETFLY, p1); vis(IDC_TXT_NETFLY, p1); vis(IDC_CMB_NETFLY, p1);
    vis(IDC_LBL_VOLUME, p1); vis(IDC_CMB_VOLUME, p1);
    vis(IDC_LBL_BATT, p1); vis(IDC_CMB_BATTERY, p1);
    vis(IDC_GRP_LANG, p1); vis(IDC_LBL_LANG, p1); vis(IDC_CMB_LANG, p1);
    /* v3.5: riga dell'indicatore della lingua di input. */
    vis(IDC_LBL_LANGBAR, p1); vis(IDC_CMB_LANGBAR, p1);
    vis(IDC_GRP_NOTIF, p1); vis(IDC_TXT_NOTIF, p1); vis(IDC_BTN_CUSTOMIZE, p1);

    /* Pagina 2: informazioni + uscita. */
    vis(IDC_TXT_ABOUT, p2);   /* v2.58: i crediti sono dentro questo testo */
    /* v1.21.37: la casella dell'avvio automatico sta fra il testo e il
     * gruppo di uscita. */
    vis(IDC_CHK_AUTOSTART, p2);
    vis(IDC_GRP_EXIT, p2); vis(IDC_BTN_EXIT, p2);

    /* Pagina 3: le nostre barre degli strumenti. */
    vis(IDC_TXT_TB_INFO, p3); vis(IDC_LST_TOOLBARS, p3);

    /* Page 4: secondary settings (flyout, skin, icon order). */
    vis(IDC_TXT_EXTRA_TITLE, p4);
    vis(IDC_GRP_EX_FLYOUT, p4); vis(IDC_LBL_EX_COLOR, p4);
    vis(IDC_RADIO_COLOR_SYS, p4); vis(IDC_RADIO_COLOR_CUSTOM, p4);
    vis(IDC_COLOR_SWATCH, p4); vis(IDC_BTN_PICK_COLOR, p4);
    vis(IDC_TXT_COLOR_HINT, p4);
    vis(IDC_LBL_EX_PRIVACY, p4); vis(IDC_CMB_EX_PRIVACY, p4);
    vis(IDC_TXT_PRIVACY_HINT, p4);
    vis(IDC_GRP_EX_TASKBAR, p4); vis(IDC_LBL_EX_THEME, p4);
    vis(IDC_CMB_EX_THEME, p4);
    vis(IDC_LBL_EX_ICON_ORDER, p4); vis(IDC_TXT_ORDER_HINT, p4);
}

/* v1.21.7 - Skins available in THIS version of the program.
 *
 * The index is the one stored in the configuration: 0 = Windows 7,
 * 1 = Windows 8.1. Both are implemented now: the Windows 8.1 theme file
 * (Themes/Windows8.1.xaml) ships with the program and its Start button uses
 * the two sprites embedded in GraphicalResourceBundle
 * (startwin81flag / startwin81flagscaled). Windows 7 stays the default and
 * the fallback for anything unknown.
 *
 * A single function for the judgement, so the dropdown and SendApply cannot
 * diverge. */
constexpr bool ThemeIsAvailable(int32_t themeId) { return themeId == 0 || themeId == 1; }

/* v1.21.7 - Colour picker of the extra settings tab.
 *
 * It uses the Windows colour chooser (ChooseColorW): it is the same classic
 * look as the rest of the dialog - no home-made control and no invented
 * palette - and every fix Microsoft makes to that dialog arrives here for
 * free.
 *
 * Colours come in and go out as 0x00RRGGBB (the form stored in the
 * configuration); COLORREF is 0x00BBGGRR instead, so the conversion happens
 * in one place only, here. */
bool PickCustomColor(HWND owner, uint32_t rgb, uint32_t* outRgb) {
    if (outRgb == nullptr) {
        return false;
    }

    static COLORREF customColors[16] = {};
    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.rgbResult = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (!ChooseColorW(&cc)) {
        return false;
    }

    *outRgb = (static_cast<uint32_t>(GetRValue(cc.rgbResult)) << 16)
            | (static_cast<uint32_t>(GetGValue(cc.rgbResult)) << 8)
            |  static_cast<uint32_t>(GetBValue(cc.rgbResult));
    return true;
}

/* Paints the colour swatch (owner-draw control). The rectangle is filled
 * with the current colour and framed like the other controls: in this
 * classic look it is the only way to show what "system colour" means. */
void DrawColorSwatch(const DRAWITEMSTRUCT& dis, uint32_t rgb) {
    HBRUSH fill = CreateSolidBrush(RGB((rgb >> 16) & 0xFF,
                                       (rgb >> 8) & 0xFF,
                                        rgb & 0xFF));
    if (fill != nullptr) {
        FillRect(dis.hDC, &dis.rcItem, fill);
        DeleteObject(fill);
    }

    /* Frame: two one-pixel rectangles, like the swatches of the classic
     * dialogs (shadow bottom right, highlight top left). */
    HBRUSH shadow = CreateSolidBrush(GetSysColor(COLOR_3DSHADOW));
    HBRUSH light  = CreateSolidBrush(GetSysColor(COLOR_3DHILIGHT));
    if (shadow != nullptr) {
        FrameRect(dis.hDC, &dis.rcItem, shadow);
    }
    if (light != nullptr) {
        RECT inner = dis.rcItem;
        InflateRect(&inner, -1, -1);
        if (inner.right > inner.left && inner.bottom > inner.top) {
            FrameRect(dis.hDC, &inner, light);
        }
    }
    if (shadow != nullptr) DeleteObject(shadow);
    if (light  != nullptr) DeleteObject(light);
}

BOOL CALLBACK ThemeChildProc(HWND h, LPARAM) {
    SetWindowTheme(h, L"explorer", nullptr);
    return TRUE;
}

} /* namespace */

PropertiesDialog::~PropertiesDialog() {
    /* RAII: il font del dialogo e' un oggetto GDI, si distrugge qui. */
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
}

/* v1.21.8: command-button row.
 *
 * The rectangle of a control in a dialog template is in DLU, and DLU are a
 * function of the dialog font: the same 330 x 326 is a different number of
 * pixels on every machine, font size and DPI. The rule for the row itself
 * does not change with any of that, and Microsoft states it in "Dialog
 * Boxes: Design Guidelines": the command buttons go in the lower-right
 * corner, horizontally, with OK as the left-most one. So the row is placed
 * here against measured pixels:
 *
 *   - the right edge is the right edge of the tab control, which is the
 *     visual reference of every page (GetWindowRect + MapWindowPoints);
 *   - the vertical position follows the tab and stays inside the client
 *     area, with a few pixels of air above and below;
 *   - the widths are the real ones of the three buttons, so a longer label
 *     ("Uebernehmen", "Aplicar") simply moves the row left instead of
 *     running past the border.
 *
 * Nothing here guesses a size: if a measurement fails the function returns
 * and the template coordinates stay, which are already right at 96 DPI. */
void PropertiesDialog::LayoutCommandButtons(HWND hwnd) {
    HWND hOk = GetDlgItem(hwnd, IDOK);
    HWND hCancel = GetDlgItem(hwnd, IDCANCEL);
    HWND hApply = GetDlgItem(hwnd, IDC_BTN_APPLY);
    HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
    RECT client{};
    if (hwnd == nullptr || hOk == nullptr || hCancel == nullptr ||
        hApply == nullptr || hTab == nullptr ||
        !GetClientRect(hwnd, &client)) {
        return;
    }

    /* 6 DLU between the buttons and 6 DLU of margin, the same numbers the
     * template uses: at the standard font this reproduces the template
     * position exactly (y = 326 - 6 - 14 = 306) and at any other font or DPI
     * it keeps the same proportion. MapDialogRect is the documented
     * DLU-to-pixel conversion for the dialog being created. */
    RECT gap{ 0, 0, 6, 6 };
    if (!MapDialogRect(hwnd, &gap)) {
        return;
    }
    const int gapX = gap.right;
    const int margin = gap.bottom;

    POINT tabEdges[2] = { { 0, 0 }, { 0, 0 } };
    RECT tabRect{};
    if (!GetWindowRect(hTab, &tabRect)) {
        return;
    }
    tabEdges[0] = { tabRect.left, tabRect.top };
    tabEdges[1] = { tabRect.right, tabRect.bottom };
    MapWindowPoints(nullptr, hwnd, tabEdges, 2);

    RECT r{};
    if (!GetWindowRect(hOk, &r)) return;
    const int okWidth = r.right - r.left;
    const int rowHeight = r.bottom - r.top;
    if (!GetWindowRect(hCancel, &r)) return;
    const int cancelWidth = r.right - r.left;
    if (!GetWindowRect(hApply, &r)) return;
    const int applyWidth = r.right - r.left;
    if (rowHeight <= 0) {
        return;
    }

    const int rowWidth = okWidth + cancelWidth + applyWidth + 2 * gapX;
    int x = tabEdges[1].x - rowWidth;
    if (x < margin) {
        /* Client narrower than the row: the row comes in from the left
         * border instead of leaving the window. */
        x = margin;
    }

    /* Lower-right corner: the row keeps the same margin from the bottom of
     * the client area as it does from its right edge, and can never climb
     * over the bottom border of the tab control. */
    int y = client.bottom - margin - rowHeight;
    if (y < tabEdges[1].y + 2) {
        y = tabEdges[1].y + 2;
    }
    if (y < 0) {
        y = 0;
    }

    SetWindowPos(hOk, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hCancel, nullptr, x + okWidth + gapX, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(hApply, nullptr, x + okWidth + gapX + cancelWidth + gapX, y,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* v1.21.8: the row above cannot be visible if the dialog itself is taller or
 * wider than the screen. The dialog is created with DS_CENTER and a size in
 * DLU, so on a small screen - or with a large system font, which scales every
 * DLU - the window can be centred partly outside the work area and take the
 * command buttons with it. Here the created window is moved (and, only if it
 * is larger than the work area, shortened) so that its lower-right corner,
 * where the buttons live, is always reachable. GetMonitorInfo returns the
 * work area - the monitor minus the taskbar - which is what the documentation
 * recommends over the full screen rectangle. */
void PropertiesDialog::FitDialogToWorkArea(HWND hwnd) {
    RECT win{};
    if (hwnd == nullptr || !GetWindowRect(hwnd, &win)) {
        return;
    }
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &mi)) {
        return;
    }
    const RECT& work = mi.rcWork;
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    if (workWidth <= 0 || workHeight <= 0) {
        return;
    }

    const int width = win.right - win.left;
    const int height = win.bottom - win.top;
    const int newWidth = (width > workWidth) ? workWidth : width;
    const int newHeight = (height > workHeight) ? workHeight : height;

    int x = win.left;
    int y = win.top;
    if (x + newWidth > work.right) x = work.right - newWidth;
    if (y + newHeight > work.bottom) y = work.bottom - newHeight;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;

    if (newWidth != width || newHeight != height ||
        x != win.left || y != win.top) {
        SetWindowPos(hwnd, nullptr, x, y, newWidth, newHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* v1.21.7: recomputes the colour SHOWN by the swatch of the extra settings
 * tab. In "system colour" mode the value is asked to the system
 * (DwmGetColorizationColor, with a registry fallback): no stored copy, so
 * the swatch follows the Windows personalization changes. In custom mode it
 * shows the colour chosen by the user. */
void PropertiesDialog::RefreshExtraSwatchColor() {
    if (m_flyoutColorMode == 1) {
        m_extraSwatchRgb = static_cast<uint32_t>(m_flyoutColorRgb) & 0x00FFFFFFu;
        return;
    }

    uint32_t accent = 0;
    if (w7t::extras::QuerySystemAccentColor(&accent)) {
        m_extraSwatchRgb = accent;
    } else {
        /* The system does not provide it: the last good value is kept. */
        m_extraSwatchRgb = w7t::extras::ResolveFlyoutColor();
    }
}

void PropertiesDialog::Show(HWND owner, int32_t lang, int32_t seconds,
                            int32_t nativeFlyout, int32_t enableSearch,
                            int32_t netFlyout, int32_t classicVolume,
                            int32_t batteryFlyout, int32_t aeroPeek,
                            int32_t toolbarDesktop, int32_t toolbarAddress,
                            int32_t toolbarLinks, int32_t inputLanguageMode,
                            int32_t taskManagerMode,
                            int32_t flyoutColorMode, int32_t flyoutColorRgb,
                            int32_t connectionPrivacyMode, int32_t themeSelection,
                            int32_t autoStart) {
    try {
        if (m_hWnd && IsWindow(m_hWnd)) {
            return;
        }
        m_owner = owner;
        /* Indice fuori elenco: inglese, mai italiano per omissione. */
        m_lang = (lang >= 0 && lang < kLangCount) ? lang : LangIndex(Lang::En);
        m_seconds = seconds;
        m_nativeFlyout = nativeFlyout;
        m_enableSearch = enableSearch;
        /* v3.8: 0 = Windows 7 (ricreato), 1 = Windows 10/11 (sistema),
         * 2 = Windows 8 (ricreato). Valori fuori elenco -> Windows 7. */
        m_netFlyout = (netFlyout >= 0 && netFlyout <= 2) ? netFlyout : 0;
        m_classicVolume = classicVolume ? 1 : 0;
        m_batteryFlyout = batteryFlyout ? 1 : 0;
        m_aeroPeek = aeroPeek ? 1 : 0;
        m_tbDesktop = toolbarDesktop ? 1 : 0;
        m_tbAddress = toolbarAddress ? 1 : 0;
        /* v3.5: 0 nascosta, 1 Win7, 2 Win8.1, 3 Win10/11; fuori elenco ->
         * stile Windows 7 (il default). */
        m_inputLanguageMode =
            (inputLanguageMode >= 0 && inputLanguageMode <= 3)
                ? inputLanguageMode : 1;
        m_taskManagerMode = IsWindows11OrBetter() &&
                            taskManagerMode >= 0 && taskManagerMode <= 2
            ? taskManagerMode : 0;
        m_tbLinks = toolbarLinks ? 1 : 0;

        /* v1.21.7 - extra settings. Values outside the list fall back to the
         * safe defaults (system colour, no privacy, Windows 7 skin), never to
         * an invented state. */
        m_flyoutColorMode = (flyoutColorMode == 1) ? 1 : 0;
        m_flyoutColorRgb = static_cast<int32_t>(
            static_cast<uint32_t>(flyoutColorRgb) & 0x00FFFFFFu);
        m_connectionPrivacyMode = (connectionPrivacyMode == 1) ? 1 : 0;
        /* Windows 8.1 is not available in this version: the only usable skin
         * stays Windows 7, and the stored value is brought back to 0 when the
         * user opens and confirms the Properties. */
        m_themeSelection = (themeSelection == 1 && ThemeIsAvailable(1)) ? 1 : 0;
        /* v1.21.37: stato dell'avvio automatico letto dal registro dal gestito
         * prima di aprire il dialogo (come LoadAutoStart di RetroBar). */
        m_autoStart = autoStart ? 1 : 0;
        RefreshExtraSwatchColor();

        /* v2.47: oltre alle schede e ai controlli standard serve la classe
         * del controllo collegamento (SysLink) usato in fondo alla prima
         * pagina: senza ICC_LINK_CLASS CreateDialogIndirectParamW non riesce
         * a creare il controllo. */
        INITCOMMONCONTROLSEX icc{ sizeof(icc),
            ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS |
            ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);

        /* ---- template in memoria, identico alla mod ---- */
        BYTE* buf = new BYTE[8192];
        /* RAII: qualunque uscita da qui libera il template. */
        auto bufGuard = MakeScopeExit([buf] { delete[] buf; });
        (void)bufGuard;
        BYTE* p = buf;
        int controlCount = 0;
        auto align4 = [](BYTE*& ptr) { ptr = (BYTE*)(((UINT_PTR)ptr + 3) & ~3); };

        LPDLGTEMPLATEW pDlg = (LPDLGTEMPLATEW)p;
        pDlg->style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP |
                      WS_CAPTION | WS_SYSMENU;
        pDlg->dwExtendedStyle = 0;
        pDlg->cdit = 0;
        pDlg->x = 0; pDlg->y = 0;
        pDlg->cx = MAIN_WIDTH;
        pDlg->cy = MAIN_HEIGHT;
        p += sizeof(DLGTEMPLATE);
        *(WORD*)p = 0; p += 2;
        *(WORD*)p = 0; p += 2;
        *(WCHAR*)p = 0; p += 2;
        *(WORD*)p = 9; p += 2;
        const wchar_t* face = L"Segoe UI";
        memcpy(p, face, (wcslen(face) + 1) * 2);
        p += (wcslen(face) + 1) * 2;

        auto addCtrl = [&](DWORD style, DWORD exStyle, short x, short y,
                           short cx, short cy, WORD id, LPCWSTR cls, LPCWSTR cap) {
            align4(p);
            LPDLGITEMTEMPLATE pi = (LPDLGITEMTEMPLATE)p;
            pi->style = WS_CHILD | WS_VISIBLE | style;
            pi->dwExtendedStyle = exStyle;
            pi->x = x; pi->y = y; pi->cx = cx; pi->cy = cy; pi->id = id;
            p += sizeof(DLGITEMTEMPLATE);
            memcpy(p, cls, (wcslen(cls) + 1) * 2);
            p += (wcslen(cls) + 1) * 2;
            memcpy(p, cap, (wcslen(cap) + 1) * 2);
            p += (wcslen(cap) + 1) * 2;
            *(WORD*)p = 0; p += 2;
            controlCount++;
        };

        addCtrl(TCS_TABS | WS_TABSTOP, 0, 6, 6, TAB_WIDTH, 292, IDC_TAB_MAIN,
                L"SysTabControl32", L"");
        /* ============================================================
         * PAGINA 1 - "Barra delle applicazioni"
         *
         * Impostazioni NOSTRE nella grafica classica del dialogo: orologio
         * (secondi + riquadro), ricerca applicazioni, flyout
         * (rete/volume/batteria), lingua e l'AREA DI NOTIFICA presa dalla foto
         * di riferimento, con il pulsante "Personalizza..." che apre
         * esattamente quello che apre il menu di overflow della barra.
         * Niente Aero Peek, niente collegamenti esterni.
         * ============================================================ */
        /* GRUPPO 1 - FLYOUT. E' il primo gruppo della pagina, in cima a
         * tutto, come nella foto di riferimento: quattro righe etichetta +
         * tendina (orologio, rete, volume, batteria). Passo fra le righe 16
         * DLU (tendina alta 14 + 2 di aria), etichette a 18, tendine a 72
         * larghe 172: la stessa griglia del resto del dialogo. */
        addCtrl(BS_GROUPBOX, 0, 12, 30, GROUP_WIDTH, 74, IDC_GRP_NETFLY, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 40, 50, 10, IDC_LBL_CLOCK, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 38, 218, 80, IDC_CMB_CLOCK, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 56, 50, 10, IDC_TXT_NETFLY, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 54, 218, 80, IDC_CMB_NETFLY, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 72, 50, 10, IDC_LBL_VOLUME, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 70, 218, 80, IDC_CMB_VOLUME, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 88, 50, 10, IDC_LBL_BATT, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 86, 218, 80, IDC_CMB_BATTERY, L"ComboBox", L"");

        /* GRUPPO 2 - OROLOGIO: una sola casella, gruppo alto 30. */
        addCtrl(BS_GROUPBOX, 0, 12, 108, GROUP_WIDTH, 30, IDC_GRP_CLOCK, L"Button", L"");
        addCtrl(BS_AUTOCHECKBOX | WS_TABSTOP, 0, 18, 118, PAGE_TEXT_WIDTH, 10, IDC_CHK_SECONDS, L"Button", L"");

        /* GRUPPO 3 - RICERCA APPLICAZIONI. La casella e' su DUE righe
         * (BS_MULTILINE, alto 20): la sua etichetta e' lunga e in tedesco,
         * polacco e russo non entrerebbe in una riga sola - prima si leggeva
         * "Attiva ricerca a..." e sembrava che la stringa mancasse. Il
         * pulsante sta sotto, dentro il gruppo (142..188). */
        /* Il pulsante "Apri ricerca" resta assente. Su Windows 11 21H2+
         * la seconda riga sceglie quale Task Manager viene aperto dalla
         * voce del menu contestuale; ShowTabPage la nasconde su Windows 10. */
        addCtrl(BS_GROUPBOX, 0, 12, 142, GROUP_WIDTH, 50, IDC_GRP_SEARCH, L"Button", L"");
        addCtrl(BS_AUTOCHECKBOX | WS_TABSTOP | BS_MULTILINE, 0, 18, 150, PAGE_TEXT_WIDTH, 18, IDC_CHK_SEARCH, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 175, 68, 10, IDC_LBL_TASKMGR, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 88, 172, 202, 80, IDC_CMB_TASKMGR, L"ComboBox", L"");

        /* GRUPPO 4 - LINGUA (al posto della sezione Aero Peek della foto).
         * v3.5: due righe - la lingua del programma (come prima) e lo
         * stile dell'indicatore della lingua di input (0 nascosta,
         * 1 Windows 7, 2 Windows 8.1, 3 Windows 10/11). */
        addCtrl(BS_GROUPBOX, 0, 12, 196, GROUP_WIDTH, 44, IDC_GRP_LANG, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 206, 50, 10, IDC_LBL_LANG, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 204, 218, 80, IDC_CMB_LANG, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 222, 50, 10, IDC_LBL_LANGBAR, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 220, 218, 80, IDC_CMB_LANGBAR, L"ComboBox", L"");

        /* GRUPPO 5 - AREA DI NOTIFICA: testo su due righe (20) + pulsante. */
        addCtrl(BS_GROUPBOX, 0, 12, 244, GROUP_WIDTH, 52, IDC_GRP_NOTIF, L"Button", L"");
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 18, 254, PAGE_TEXT_WIDTH, 20, IDC_TXT_NOTIF, L"Static", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 18, 276, 76, 14, IDC_BTN_CUSTOMIZE, L"Button", L"");

        /* ============================================================
         * PAGINA 2 - "Informazioni"
         * ============================================================ */
        /* v2.58 - the About page is back to a FIXED template layout, and the
         * whole text (what the project is + the credits) lives in ONE box:
         *
         *   - IDC_TXT_ABOUT holds everything: the code fills it with "about"
         *     followed by the credits, so the two cannot overlap and the page
         *     cannot be left with a hole in the middle (both bugs reported on
         *     the previous layout, where the two blocks were placed from the
         *     measured height of the text);
         *   - the exit group is anchored to the BOTTOM of the page, so the
         *     close button sits at the bottom left of the information area,
         *     just above the OK / Cancel / Apply row, and its position does
         *     not depend on how long the text is;
         *   - the separate credits control is gone: the text box swallowed it.
         *
         * Coordinates are plain dialog units from the template - nothing on
         * this page is measured or computed at run time any more. The page
         * still ends at ~282 units, like the other two. */
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 14, 22, 296, 196, IDC_TXT_ABOUT, L"Static", L"");
        /* v1.21.37 - "Avvio automatico con Windows", COPIATO DA RETROBAR.
         * RetroBar ha la stessa casella nella finestra Proprieta' (voce
         * "autostart", Advanced tab): la spunta scrive il percorso
         * dell'eseguibile nel valore "RetroBar" di
         * HKCU\Software\Microsoft\Windows\CurrentVersion\Run, toglierla
         * elimina il valore - reversibile. Qui la casella sta nella scheda
         * Informazioni, fra il testo e il gruppo di uscita, nello spazio che
         * il template lasciava libero (222..232 DLU); l'effetto sul registro
         * lo applica il gestito all'Applica/OK (Utilities/AutoStart.cs,
         * stessa logica di RetroBar, stessa reversibilita'). */
        addCtrl(BS_AUTOCHECKBOX | WS_TABSTOP, 0, 14, 222, 296, 10,
                IDC_CHK_AUTOSTART, L"Button", L"");
        /* v3.5: il gruppo di uscita segue il fondo pagina (+14 DLU). */
        addCtrl(BS_GROUPBOX, 0, 14, 238, 296, 40, IDC_GRP_EXIT, L"Button", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 20, 254, 110, 14, IDC_BTN_EXIT, L"Button", L"");

        /* ============================================================
         * PAGINA 3 - "Toolbar"
         * Le TRE barre della mod, nell'ordine richiesto: Indirizzi,
         * Desktop, Collegamenti. Le caselle chiamano gli stessi comandi del
         * menu "Barre degli strumenti" della barra (SetBandVisible lato
         * gestito): accendono le bande della MOD, non le barre di Windows.
         * ============================================================ */
        /* v2.50: PAGINA 3 copiata DALLA MOD, spaziature comprese: il testo
         * informativo in alto (14,22) e sotto un unico elenco con le caselle
         * (SysListView32 in stile report, senza intestazione), largo 246 e
         * alto 160. Le tre caselle separate di prima erano troppo distanti
         * fra loro: qui le righe hanno il passo compatto della mod. */
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 14, 22, 296, 26, IDC_TXT_TB_INFO, L"Static", L"");
        addCtrl(LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | WS_BORDER | WS_TABSTOP,
                0, 16, 52, 292, 160, IDC_LST_TOOLBARS, L"SysListView32", L"");

        /* ============================================================
         * PAGE 4 - extra settings
         *
         * Secondary settings, in two groups as in the requested structure:
         *
         *   Flyout  - colour of the recreated flyout (system or custom, with
         *             swatch + colour chooser) and privacy mode of the
         *             connection flyout;
         *   Taskbar - skin choice (Windows 7, with Windows 8.1 listed as not
         *             available) and a reminder about the icon order, which
         *             is changed by dragging the icons on the bar (not from
         *             here: here it is only explained).
         *
         * The colour swatch is an owner-draw control (WM_DRAWITEM, see
         * DlgProc): filling it with the real colour is the only way, in this
         * classic look, to show the user what "system colour" means.
         * ============================================================ */
        addCtrl(SS_LEFT, 0, 14, 20, PAGE_TEXT_WIDTH, 10, IDC_TXT_EXTRA_TITLE, L"Static", L"");

        addCtrl(BS_GROUPBOX, 0, 12, 34, GROUP_WIDTH, 134, IDC_GRP_EX_FLYOUT, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 46, 200, 10, IDC_LBL_EX_COLOR, L"Static", L"");
        addCtrl(BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 0, 18, 58, 200, 10,
                IDC_RADIO_COLOR_SYS, L"Button", L"");
        addCtrl(BS_AUTORADIOBUTTON | WS_TABSTOP, 0, 18, 72, 130, 10,
                IDC_RADIO_COLOR_CUSTOM, L"Button", L"");
        addCtrl(SS_OWNERDRAW, 0, 172, 70, 26, 13, IDC_COLOR_SWATCH, L"Static", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 202, 69, 96, 14,
                IDC_BTN_PICK_COLOR, L"Button", L"");
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 18, 90, PAGE_TEXT_WIDTH, 22,
                IDC_TXT_COLOR_HINT, L"Static", L"");
        addCtrl(SS_LEFT, 0, 18, 118, 90, 10, IDC_LBL_EX_PRIVACY, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 112, 116, 178, 80,
                IDC_CMB_EX_PRIVACY, L"ComboBox", L"");
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 18, 134, PAGE_TEXT_WIDTH, 24,
                IDC_TXT_PRIVACY_HINT, L"Static", L"");

        addCtrl(BS_GROUPBOX, 0, 12, 176, GROUP_WIDTH, 94, IDC_GRP_EX_TASKBAR, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 188, 60, 10, IDC_LBL_EX_THEME, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 84, 186, 218, 80,
                IDC_CMB_EX_THEME, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 206, 200, 10, IDC_LBL_EX_ICON_ORDER, L"Static", L"");
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 18, 220, PAGE_TEXT_WIDTH, 42,
                IDC_TXT_ORDER_HINT, L"Static", L"");

        /* v1.21.28 - OPZIONE "POSIZIONE DELLA BARRA" DISATTIVATA.
         * La rotazione della taskbar e' stata ritirata (l'edge scelto non
         * coincideva con l'enum del taskbar: "A destra" finiva in basso e le
         * anteprime DWM restavano ancorate al lato sbagliato), quindi questa
         * tendina non viene creata e il pacchetto WM_COPYDATA resta a 76 byte.
         * Il codice e' tenuto qui commentato, pronto da riattivare insieme
         * alla conversione esplicita posizione -> TaskbarEdge che manca nel
         * core gestito (vedi TaskbarWindow.xaml.cs):
         *
         *   // IDC_LBL_EX_POSITION / IDC_CMB_EX_POSITION nell'enum CtrlId;
         *   // gruppo IDC_GRP_EX_TASKBAR alto 102 per ospitare la riga:
         *   // addCtrl(BS_GROUPBOX, 0, 12, 176, GROUP_WIDTH, 102,
         *   //         IDC_GRP_EX_TASKBAR, L"Button", L"");
         *   // addCtrl(SS_LEFT, 0, 18, 204, 90, 10,
         *   //         IDC_LBL_EX_POSITION, L"Static", L"");
         *   // addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
         *   //         0, 112, 202, 190, 80,
         *   //         IDC_CMB_EX_POSITION, L"ComboBox", L"");
         *   //   + righe "Ordine icone"/hint spostate a y 220/236 (hint h26)
         *   // WM_INITDIALOG: SetDlgItemTextW(IDC_LBL_EX_POSITION, X.lblPosition)
         *   //   e 4 ComboBox_AddString (Basso/Alto/Sinistra/Destra) + SetCurSel
         *   // SendApply: msg.taskbarPosition da CB_GETCURSEL (CB_ERR -> 0)
         *   // Strings.h/Strings.cpp: lblPosition + posBottom/posTop/posLeft/
         *   //   posRight APPESI IN FONDO a ExtraStrings (11 lingue, ordine
         *   //   posizionale: campo e valore vanno aggiunti insieme).
         */

        // ---- pulsanti standard 50x14, come la mod ----
        /* v3.5: the row moves 14 DLU down with the window.
         * v1.21.8: the three command buttons sit in the LOWER-RIGHT corner,
         * aligned with the right edge of the tab control, in the order
         * OK, Cancel, Apply. This is what Microsoft's "Dialog Boxes: Design
         * Guidelines" prescribes - "Position these command buttons
         * horizontally in the lower-right corner. If you use the OK button,
         * make it the left-most button" - and it is what every Windows
         * property sheet does. The previous centred row (84/140/196) was the
         * v1.21.7 mistake: 6 + 318 = 324 DLU is the tab's right edge, so the
         * row ends where every page underneath it ends. LayoutCommandButtons
         * repeats the placement in measured pixels once the dialog exists,
         * so a larger system font or an unexpected client size cannot push
         * the buttons out of the window. */
        addCtrl(BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 162, 306, 50, 14, IDOK, L"Button", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 218, 306, 50, 14, IDCANCEL, L"Button", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 274, 306, 50, 14, IDC_BTN_APPLY, L"Button", L"");

        pDlg->cdit = controlCount;
        m_hWnd = CreateDialogIndirectParamW(GetModuleHandleW(nullptr),
            (LPDLGTEMPLATE)buf, owner, DlgProc,
            reinterpret_cast<LPARAM>(this));

        if (m_hWnd) ShowWindow(m_hWnd, SW_SHOW);
    } catch (...) {
        /* mai propagare */
    }
}

void PropertiesDialog::SendApply(bool openSearch, bool closeApp) {
    if (m_owner == nullptr || !IsWindow(m_owner) || m_hWnd == nullptr) return;

    PropsApplyMsg msg{};
    msg.seconds =
        (SendDlgItemMessageW(m_hWnd, IDC_CHK_SECONDS, BM_GETCHECK, 0, 0)
            & BST_CHECKED) ? 1 : 0;
    msg.enableSearch =
        (SendDlgItemMessageW(m_hWnd, IDC_CHK_SEARCH, BM_GETCHECK, 0, 0)
            & BST_CHECKED) ? 1 : 0;
    if (IsWindows11OrBetter()) {
        const LRESULT selected = SendDlgItemMessageW(
            m_hWnd, IDC_CMB_TASKMGR, CB_GETCURSEL, 0, 0);
        msg.taskManagerMode = selected >= 0 && selected <= 2
            ? static_cast<int32_t>(selected) : 0;
    } else {
        msg.taskManagerMode = 0;
    }
    msg.classicVolume =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_VOLUME, CB_GETCURSEL, 0, 0) == 1)
            ? 0 : 1;
    msg.batteryFlyout =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_BATTERY, CB_GETCURSEL, 0, 0) == 1)
            ? 0 : 1;
    /* Tendine: indice 0 = versione ricreata dalla mod, 1 = quella del sistema.
     * Ogni impostazione ha la sua polarita' (vedi le colonne nel messaggio). */
    msg.nativeFlyout =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_CLOCK, CB_GETCURSEL, 0, 0) == 1)
            ? 1 : 0;
    /* v3.8: la tendina ora ha tre voci e l'INDICE e' il modo (0/1/2):
     * il pacchetto lo porta cosi' com'e', limitato per difesa. */
    {
        const LRESULT netSel =
            SendDlgItemMessageW(m_hWnd, IDC_CMB_NETFLY, CB_GETCURSEL, 0, 0);
        msg.netFlyoutMode = (netSel >= 0 && netSel <= 2)
            ? static_cast<int32_t>(netSel) : 0;
    }
    /* La finestra non ha piu' il controllo Aero Peek: il campo resta nel
     * pacchetto (compatibilita' con i campi aggiunti in coda) e rimanda
     * indietro il valore ricevuto all'apertura, senza toccarlo. */
    msg.aeroPeek = m_aeroPeek ? 1 : 0;
    {
        HWND hTL = GetDlgItem(m_hWnd, IDC_LST_TOOLBARS);
        msg.toolbarAddress = (hTL && ListView_GetCheckState(hTL, 0)) ? 1 : 0;
        msg.toolbarDesktop = (hTL && ListView_GetCheckState(hTL, 1)) ? 1 : 0;
        msg.toolbarLinks   = (hTL && ListView_GetCheckState(hTL, 2)) ? 1 : 0;
    }
    {
        const int32_t langSel = static_cast<int32_t>(
            SendDlgItemMessageW(m_hWnd, IDC_CMB_LANG, CB_GETCURSEL, 0, 0));
        msg.lang = (langSel >= 0 && langSel <= 10) ? langSel : 0;
    }
    {
        /* v3.5: input language indicator style.
         * v1.7.6: the dropdown no longer offers value 3 ("Windows 10/11"),
         * so the read stops at 2 too; a CB_ERR (invalid selection) falls
         * back to the Windows 7 default, as before. */
        const int32_t langBarSel = static_cast<int32_t>(
            SendDlgItemMessageW(m_hWnd, IDC_CMB_LANGBAR, CB_GETCURSEL, 0, 0));
        msg.inputLanguageMode =
            (langBarSel >= 0 && langBarSel <= 2) ? langBarSel : 1;
    }
    /* v1.21.7 - extra settings tab.
     *
     * The custom colour is the one chosen with the button (m_flyoutColorRgb is
     * updated there); the swatch is not read, because in "system colour" mode
     * it holds the system accent and not a choice of the user. The skin is
     * read back from the dropdown and passed through ThemeIsAvailable(): the
     * only value that can come out of here today is 0. */
    msg.flyoutColorMode =
        (SendDlgItemMessageW(m_hWnd, IDC_RADIO_COLOR_CUSTOM, BM_GETCHECK, 0, 0)
            & BST_CHECKED) ? 1 : 0;
    msg.flyoutColorRgb = m_flyoutColorRgb & 0x00FFFFFF;
    {
        const int32_t privacySel = static_cast<int32_t>(
            SendDlgItemMessageW(m_hWnd, IDC_CMB_EX_PRIVACY, CB_GETCURSEL, 0, 0));
        msg.connectionPrivacyMode = (privacySel == 1) ? 1 : 0;
    }
    {
        const int32_t themeSel = static_cast<int32_t>(
            SendDlgItemMessageW(m_hWnd, IDC_CMB_EX_THEME, CB_GETCURSEL, 0, 0));
        msg.themeSelection = ThemeIsAvailable(themeSel) ? themeSel : 0;
    }
    /* v1.21.37: avvio automatico con Windows (casella della scheda
     * Informazioni, logica copiata da RetroBar). Il pacchetto porta solo la
     * scelta; a scrivere/togliere il valore Run nel registro e' il gestito. */
    msg.autoStart =
        (SendDlgItemMessageW(m_hWnd, IDC_CHK_AUTOSTART, BM_GETCHECK, 0, 0)
            & BST_CHECKED) ? 1 : 0;
    msg.openSearch = openSearch ? 1 : 0;
    msg.closeApp = closeApp ? 1 : 0;

    /* v3.6: le stringhe del NATIVO (menu della tray, jump list, ecc.)
     * seguono subito la scelta del dialogo, senza aspettare il giro
     * COPYDATA -> gestito -> W7T_SetLanguage: se quel giro non parte (o
     * arriva tardi), i menu restavano nella lingua precedente. */
    w7t::SetLanguageByIndex(msg.lang);

    COPYDATASTRUCT cds{};
    cds.dwData = kPropsCopyDataId;
    cds.cbData = sizeof(msg);
    cds.lpData = &msg;
    SendMessageW(m_owner, WM_COPYDATA,
                 reinterpret_cast<WPARAM>(m_hWnd),
                 reinterpret_cast<LPARAM>(&cds));
}

INT_PTR CALLBACK PropertiesDialog::DlgProc(HWND hwnd, UINT msg,
                                           WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PropertiesDialog*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        self = reinterpret_cast<PropertiesDialog*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hwnd;

        HICON hIcon = GetSystemIcon(SIID_TASKBAR);
        if (hIcon) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }

        /* v2.59: accesso unico alla tabella delle stringhe. */
        const PropStrings& S = PropStringsFor(LangFromIndex(self->m_lang));
        /* v1.21.7: string tables for the new tab too. */
        const ExtraStrings& X = ExtraStringsFor(LangFromIndex(self->m_lang));

        {
            /* RAII: l'HDC si rilascia uscendo dal blocco, anche se una delle
             * chiamate fallisse o tornasse prima. */
            HDC hdc = GetDC(hwnd);
            auto dcGuard = MakeScopeExit([hwnd, hdc] { ReleaseDC(hwnd, hdc); });
            (void)dcGuard;
            int ptPx = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);

            /* v2.47: il font e' di proprieta' dell'oggetto dialogo: si crea
             * qui e si distrugge nel distruttore (o alla riapertura). Prima
             * ogni apertura lasciava per strada un HFONT. */
            if (self->m_font) {
                DeleteObject(self->m_font);
                self->m_font = nullptr;
            }
            self->m_font = CreateFontW(ptPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (self->m_font) {
                SendMessageW(hwnd, WM_SETFONT, (WPARAM)self->m_font, TRUE);
                EnumChildWindows(hwnd, [](HWND h, LPARAM fp) -> BOOL {
                    SendMessageW(h, WM_SETFONT, fp, TRUE);
                    return TRUE;
                }, (LPARAM)self->m_font);
            }
        }

        SetWindowTextW(hwnd, S.title);

        /* v2.57: the dialog carries the project icon (embedded in this module
         * by resources/app.rc, IDI_APPICON = 101). Loaded from the module of
         * this file, not from the executable: the dialog is created by the
         * native core, so that is where the resource lives. */
        {
            HINSTANCE hMod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&PropertiesDialog::DlgProc), &hMod);
            if (hMod) {
                HICON hBig = static_cast<HICON>(LoadImageW(hMod, MAKEINTRESOURCEW(101),
                                                          IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
                HICON hSmall = static_cast<HICON>(LoadImageW(hMod, MAKEINTRESOURCEW(101),
                                                            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
                if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
                if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
            }
        }
        EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
        EnumChildWindows(hwnd, ThemeChildProc, 0);

        /* Four tabs: taskbar | information | toolbars (the third one is ours,
         * with our own bars) | extra settings (added in v1.21.7). */
        HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
        TCITEMW ti{ TCIF_TEXT, 0, 0, nullptr, 0 };
        ti.pszText = const_cast<wchar_t*>(S.tab1);
        TabCtrl_InsertItem(hTab, 0, &ti);
        ti.pszText = const_cast<wchar_t*>(S.tab2);
        TabCtrl_InsertItem(hTab, 1, &ti);
        ti.pszText = const_cast<wchar_t*>(S.tab3);
        TabCtrl_InsertItem(hTab, 2, &ti);
        /* v1.21.7: fourth tab, extra settings. */
        ti.pszText = const_cast<wchar_t*>(X.tabExtra);
        TabCtrl_InsertItem(hTab, 3, &ti);
        TabCtrl_SetCurSel(hTab, 0);

        SetDlgItemTextW(hwnd, IDC_GRP_CLOCK, S.grpClock);
        SetDlgItemTextW(hwnd, IDC_CHK_SECONDS, S.chkSeconds);
        SetDlgItemTextW(hwnd, IDC_LBL_CLOCK, S.lblClock);
        SetDlgItemTextW(hwnd, IDC_GRP_SEARCH, S.grpSearch);
        SetDlgItemTextW(hwnd, IDC_CHK_SEARCH, S.chkSearch);
        SetDlgItemTextW(hwnd, IDC_LBL_TASKMGR, S.lblTaskManager);
        SetDlgItemTextW(hwnd, IDC_GRP_NETFLY, S.grpFlyouts);   /* "Flyout" */
        SetDlgItemTextW(hwnd, IDC_TXT_NETFLY, S.lblNetwork);
        SetDlgItemTextW(hwnd, IDC_LBL_VOLUME, S.lblVolume);
        SetDlgItemTextW(hwnd, IDC_LBL_BATT, S.lblBattery);
        SetDlgItemTextW(hwnd, IDC_GRP_LANG, S.grpLang);
        SetDlgItemTextW(hwnd, IDC_LBL_LANG, S.lblLang);
        SetDlgItemTextW(hwnd, IDC_LBL_LANGBAR, S.lblLangBar);   /* v3.5 */
        SetDlgItemTextW(hwnd, IDC_GRP_NOTIF, S.grpNotif);
        SetDlgItemTextW(hwnd, IDC_TXT_NOTIF, S.txtNotif);
        SetDlgItemTextW(hwnd, IDC_BTN_CUSTOMIZE, S.btnCustomize);
        SetDlgItemTextW(hwnd, IDC_GRP_EXIT, S.grpExit);
        SetDlgItemTextW(hwnd, IDC_BTN_EXIT, S.btnExit);
        /* v2.58: one text, two paragraphs - what the project is, then the
         * credits - kept apart by a blank line. */
        {
            std::wstring aboutText = S.about;
            aboutText += L"\n\n";
            aboutText += S.credits;
            SetDlgItemTextW(hwnd, IDC_TXT_ABOUT, aboutText.c_str());
        }
        /* v1.21.37: la casella dell'avvio automatico (RetroBar): etichetta
         * dalla tabella della lingua e stato corrente letto dal registro. */
        SetDlgItemTextW(hwnd, IDC_CHK_AUTOSTART, S.chkAutoStart);
        SendDlgItemMessageW(hwnd, IDC_CHK_AUTOSTART, BM_SETCHECK,
                            self->m_autoStart ? BST_CHECKED : BST_UNCHECKED, 0);
        SetDlgItemTextW(hwnd, IDC_TXT_TB_INFO, S.txtToolbars);
        SetDlgItemTextW(hwnd, IDOK, S.ok);
        SetDlgItemTextW(hwnd, IDCANCEL, S.cancel);
        SetDlgItemTextW(hwnd, IDC_BTN_APPLY, S.apply);

        /* ---- PAGE 4: extra settings (v1.21.7) ---- */
        SetDlgItemTextW(hwnd, IDC_TXT_EXTRA_TITLE, X.tabExtra);
        SetDlgItemTextW(hwnd, IDC_GRP_EX_FLYOUT, X.grpFlyout);
        SetDlgItemTextW(hwnd, IDC_LBL_EX_COLOR, X.lblFlyoutColor);
        SetDlgItemTextW(hwnd, IDC_RADIO_COLOR_SYS, X.optColorSystem);
        SetDlgItemTextW(hwnd, IDC_RADIO_COLOR_CUSTOM, X.optColorCustom);
        SetDlgItemTextW(hwnd, IDC_BTN_PICK_COLOR, X.btnPickColor);
        SetDlgItemTextW(hwnd, IDC_TXT_COLOR_HINT, X.txtFlyoutColorHint);
        SetDlgItemTextW(hwnd, IDC_LBL_EX_PRIVACY, X.lblPrivacy);
        SetDlgItemTextW(hwnd, IDC_TXT_PRIVACY_HINT, X.txtPrivacyHint);
        SetDlgItemTextW(hwnd, IDC_LBL_EX_THEME, X.lblTheme);
        SetDlgItemTextW(hwnd, IDC_GRP_EX_TASKBAR, X.grpTaskbar);
        SetDlgItemTextW(hwnd, IDC_LBL_EX_ICON_ORDER, X.lblIconOrder);
        SetDlgItemTextW(hwnd, IDC_TXT_ORDER_HINT, X.txtIconOrderHint);

        /* The two colour entries: 0 = system (default), 1 = chosen. WS_GROUP
         * on the first one keeps the two radio buttons independent of the
         * other dialog boxes: they are a group of their own. */
        SendDlgItemMessageW(hwnd, IDC_RADIO_COLOR_SYS, BM_SETCHECK,
                            self->m_flyoutColorMode == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(hwnd, IDC_RADIO_COLOR_CUSTOM, BM_SETCHECK,
                            self->m_flyoutColorMode == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
        /* The picker button is enabled only with "custom colour": with the
         * system colour there is nothing to choose. */
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_PICK_COLOR),
                     self->m_flyoutColorMode == 1);

        /* Privacy mode of the recreated connection flyout. */
        {
            HWND hPv = GetDlgItem(hwnd, IDC_CMB_EX_PRIVACY);
            ComboBox_AddString(hPv, X.optPrivacyNormal);    /* 0 */
            ComboBox_AddString(hPv, X.optPrivacyPrivate);   /* 1 */
            ComboBox_SetCurSel(hPv, self->m_connectionPrivacyMode == 1 ? 1 : 0);
        }

        /* Skin: Windows 7 (available) and Windows 8.1 (not implemented yet).
         * The missing skin stays spelled out, so the user knows the value
         * exists but cannot be used: choosing it cannot lead to an invented
         * theme, the selection falls back to Windows 7 (see WM_COMMAND). */
        {
            HWND hTh = GetDlgItem(hwnd, IDC_CMB_EX_THEME);
            ComboBox_AddString(hTh, X.themeWin7);       /* 0 */
            ComboBox_AddString(hTh, X.themeWin81);      /* 1, non disponibile */
            ComboBox_SetCurSel(hTh, ThemeIsAvailable(self->m_themeSelection)
                                        ? self->m_themeSelection : 0);
        }

        /* v2.59: IL SELETTORE SCORRE L'ELENCO UNICO DELLE LINGUE
         * (w7t::Languages(), Strings.cpp). Nessuna voce scritta a mano qui:
         * aggiungere una lingua in un posto solo la fa comparire anche in
         * questa tendina, con lo stesso indice che il managed usa per
         * parlare al nativo. Le etichette restano i nomi NATIVI
         * ("Italiano", "العربية"): sono nomi propri, non testo tradotto. */
        HWND hCL = GetDlgItem(hwnd, IDC_CMB_LANG);
        for (int i = 0; i < kLangCount; ++i) {
            ComboBox_AddString(hCL, Languages()[i].nativeName);
        }
        ComboBox_SetCurSel(hCL, self->m_lang);

        /* v2.49: LE QUATTRO TENDINE DEI FLYOUT ERANO VUOTE. Una combo senza
         * voci non ha nulla da mostrare: aprirla faceva comparire un
         * rettangolo bianco vuoto con la barra di scorrimento, che copriva
         * mezza finestra (ed e' il motivo per cui le etichette sotto
         * sembravano "sparite"). Ora ogni tendina ha le sue voci, negli
         * stessi termini del resto del programma, e parte dallo stato
         * corrente: l'indice 0 e' sempre la versione ricreata dalla mod,
         * l'indice 1 quella del sistema (la polarita' di ogni campo e'
         * quella che il pacchetto WM_COPYDATA si aspetta). */
        HWND hCC = GetDlgItem(hwnd, IDC_CMB_CLOCK);
        ComboBox_AddString(hCC, S.flyRecreated);   /* 0 = ricreato */
        ComboBox_AddString(hCC, S.flyNative);      /* 1 = sistema */
        ComboBox_SetCurSel(hCC, self->m_nativeFlyout ? 1 : 0);

        HWND hCN = GetDlgItem(hwnd, IDC_CMB_NETFLY);
        ComboBox_AddString(hCN, S.netWin7);        /* 0 = ricreato (Windows 7) */
        ComboBox_AddString(hCN, S.netModern);      /* 1 = sistema */
        /* v3.8: terza voce: la variante Windows 8 ricreata (implementazione
         * Administratox), indicizzata 2 anche nel pacchetto WM_COPYDATA. */
        ComboBox_AddString(hCN, S.netWin8);        /* 2 = ricreato (Windows 8) */
        ComboBox_SetCurSel(hCN, self->m_netFlyout);

        HWND hCV = GetDlgItem(hwnd, IDC_CMB_VOLUME);
        ComboBox_AddString(hCV, kFlyoutWin7);      /* 0 = flyout stile Windows 7 */
        ComboBox_AddString(hCV, kFlyoutWin10);     /* 1 = flyout del sistema */
        ComboBox_SetCurSel(hCV, self->m_classicVolume ? 0 : 1);

        HWND hCB = GetDlgItem(hwnd, IDC_CMB_BATTERY);
        ComboBox_AddString(hCB, kFlyoutWin7);      /* 0 = flyout stile Windows 7 */
        ComboBox_AddString(hCB, kFlyoutBatteryWin10); /* 1 = riquadro reale di Windows 10 */
        ComboBox_SetCurSel(hCB, self->m_batteryFlyout ? 0 : 1);

        /* v3.5: input language indicator style.
         * 0 hidden, 1 Windows 7, 2 Windows 8.1.
         * v1.7.6: the "Windows 10/11" option (value 3) is HIDDEN as
         * requested: the item is gone from the dropdown. A 3 saved in the
         * past keeps working until Properties is opened and applied again:
         * the combo shows it as Windows 8.1 (the nearest available option)
         * and a fresh Apply normalizes it to 2. The packet field and the
         * managed-side validation still accept 0..3 for settings files
         * already written. */
        HWND hCLB = GetDlgItem(hwnd, IDC_CMB_LANGBAR);
        ComboBox_AddString(hCLB, S.langHidden);    /* 0 = nascosta */
        ComboBox_AddString(hCLB, S.langWin7);      /* 1 = Windows 7 */
        ComboBox_AddString(hCLB, S.langWin81);     /* 2 = Windows 8.1 */
        ComboBox_SetCurSel(hCLB, self->m_inputLanguageMode >= 3
                                     ? 2 : self->m_inputLanguageMode);

        if (IsWindows11OrBetter()) {
            HWND hTM = GetDlgItem(hwnd, IDC_CMB_TASKMGR);
            ComboBox_AddString(hTM, S.taskManagerAuto);
            ComboBox_AddString(hTM, S.taskManagerModern);
            ComboBox_AddString(hTM, S.taskManagerLegacy);
            ComboBox_SetCurSel(hTM, self->m_taskManagerMode);
        }

        SendDlgItemMessageW(hwnd, IDC_CHK_SECONDS, BM_SETCHECK,
                            self->m_seconds ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(hwnd, IDC_CHK_SEARCH, BM_SETCHECK,
                            self->m_enableSearch ? BST_CHECKED : BST_UNCHECKED, 0);
        InitToolbarsList(hwnd, S, self->m_tbAddress != 0,
                         self->m_tbDesktop != 0, self->m_tbLinks != 0);

        // Applica parte disattivato, come nella mod
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), FALSE);

        // pagina iniziale: la 1, con le altre due nascoste
        ShowTabPage(hwnd, 0);

        /* v1.21.8: text, fonts and tab pages are final here, so the command
         * buttons can be measured and put in the corner, and the window can
         * be pulled back inside the work area if it does not fit. */
        self->FitDialogToWorkArea(hwnd);
        self->LayoutCommandButtons(hwnd);

        return TRUE;
    }
    /* v1.21.7: the colour swatch is owner-draw (SS_OWNERDRAW). */
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (self != nullptr && dis != nullptr &&
            dis->CtlID == IDC_COLOR_SWATCH) {
            DrawColorSwatch(*dis, self->m_extraSwatchRgb);
            return TRUE;
        }
        return FALSE;
    }
    /* System accent colour changed by the Windows personalization or by the
     * theme sync: the swatch is read from the system again, so "system
     * colour" always stays the real one. */
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        if (self != nullptr && self->m_flyoutColorMode == 0) {
            self->RefreshExtraSwatchColor();
            InvalidateRect(GetDlgItem(hwnd, IDC_COLOR_SWATCH), nullptr, TRUE);
        }
        return TRUE;
    case WM_SETTINGCHANGE:
        if (self != nullptr && self->m_flyoutColorMode == 0) {
            self->RefreshExtraSwatchColor();
            InvalidateRect(GetDlgItem(hwnd, IDC_COLOR_SWATCH), nullptr, TRUE);
        }
        return FALSE;
    case WM_GETMINMAXINFO: {
        // non ridimensionabile: min=max=attuale, come la mod
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        RECT rc; GetWindowRect(hwnd, &rc);
        mmi->ptMinTrackSize.x = mmi->ptMaxTrackSize.x = rc.right - rc.left;
        mmi->ptMaxTrackSize.y = mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD act = HIWORD(wp);
        if (!self) return FALSE;
        if ((act == BN_CLICKED || act == CBN_SELCHANGE) &&
            id != IDOK && id != IDCANCEL && id != IDC_BTN_APPLY &&
            id != IDC_BTN_EXIT &&
            id != IDC_BTN_CUSTOMIZE &&
            /* v1.21.7: the colour button turns "Apply" on only when the user
             * really confirms a choice. v1.21.19: the skin dropdown is a real
             * choice too (both skins are implemented), so it uses the generic
             * rule and enables "Apply" like any other setting. */
            id != IDC_BTN_PICK_COLOR) {
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), TRUE);
        }
        /* ---- v1.21.7: extra settings tab ---- */
        if (id == IDC_RADIO_COLOR_SYS || id == IDC_RADIO_COLOR_CUSTOM) {
            self->m_flyoutColorMode = (id == IDC_RADIO_COLOR_CUSTOM) ? 1 : 0;
            /* "Choose color..." is only needed for the custom colour. */
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_PICK_COLOR),
                         self->m_flyoutColorMode == 1);
            self->RefreshExtraSwatchColor();
            InvalidateRect(GetDlgItem(hwnd, IDC_COLOR_SWATCH), nullptr, TRUE);
            return TRUE;
        }
        if (id == IDC_BTN_PICK_COLOR) {
            uint32_t chosen = 0;
            if (PickCustomColor(hwnd, static_cast<uint32_t>(self->m_flyoutColorRgb),
                                &chosen)) {
                self->m_flyoutColorRgb = static_cast<int32_t>(chosen);
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), TRUE);
                /* Choosing a colour means wanting to use it: the radio
                 * switches by itself to "custom colour", as one expects from
                 * a colour chooser. */
                self->m_flyoutColorMode = 1;
                SendDlgItemMessageW(hwnd, IDC_RADIO_COLOR_SYS, BM_SETCHECK,
                                    BST_UNCHECKED, 0);
                SendDlgItemMessageW(hwnd, IDC_RADIO_COLOR_CUSTOM, BM_SETCHECK,
                                    BST_CHECKED, 0);
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_PICK_COLOR), TRUE);
                self->RefreshExtraSwatchColor();
                InvalidateRect(GetDlgItem(hwnd, IDC_COLOR_SWATCH), nullptr, TRUE);
            }
            return TRUE;
        }
        if (id == IDC_CMB_EX_THEME && act == CBN_SELCHANGE) {
            const int sel = static_cast<int>(
                SendDlgItemMessageW(hwnd, IDC_CMB_EX_THEME, CB_GETCURSEL, 0, 0));
            /* v1.21.19: the skin is a real choice now. The selected index is
             * stored as it is (SendApply reads it back through
             * ThemeIsAvailable(), which rejects anything unknown), so the
             * dropdown no longer snaps back to Windows 7. */
            if (ThemeIsAvailable(sel)) {
                self->m_themeSelection = sel;
            } else {
                ComboBox_SetCurSel(GetDlgItem(hwnd, IDC_CMB_EX_THEME), 0);
                self->m_themeSelection = 0;
            }
            return TRUE;
        }
        if (id == IDOK) {
            self->SendApply(false, false);
            DestroyWindow(hwnd);
        } else if (id == IDCANCEL) {
            DestroyWindow(hwnd);
        } else if (id == IDC_BTN_APPLY) {
            self->SendApply(false, false);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), FALSE);
        } else if (id == IDC_BTN_EXIT) {
            self->SendApply(false, true);
            DestroyWindow(hwnd);
        } else if (id == IDC_BTN_CUSTOMIZE) {
            /* Open Windows' native Notification Area settings page
             * directly; the removed in-process imitation is not involved. */
            if (W7T_OpenNotificationIconsSettings() != W7T_OK) {
                ShellExecuteW(nullptr, L"open", L"ms-settings:taskbar",
                              nullptr, nullptr, SW_SHOW);
            }
        }
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lp;
        if (hdr->idFrom == IDC_LST_TOOLBARS && hdr->code == LVN_ITEMCHANGED) {
            /* v2.50: le caselle dell'elenco non passano da WM_COMMAND:
             * qualunque modifica riaccende "Applica" come le altre voci. */
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), TRUE);
        }
        if (hdr->idFrom == IDC_TAB_MAIN && hdr->code == TCN_SELCHANGE) {
            int sel = (int)SendDlgItemMessageW(hwnd, IDC_TAB_MAIN,
                                               TCM_GETCURSEL, 0, 0);
            /* v1.21.7: four tabs (0..3). */
            ShowTabPage(hwnd, (sel >= 0 && sel <= 3) ? sel : 0);
        }
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return TRUE;
    case WM_DESTROY:
        if (self) self->m_hWnd = nullptr;
        return TRUE;
    }
    return FALSE;
}

} /* namespace w7t */
