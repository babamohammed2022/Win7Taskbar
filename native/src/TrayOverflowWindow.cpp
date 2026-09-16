// Win7Taskbar - pannello overflow nativo con vetro Aero
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later

#include "TrayOverflowWindow.h"
#include "Strings.h"
#include "TrayService.h"
#include "FlyoutLauncher.h"   /* ApplyAeroFlyoutStyle: bordi Aero */
#include <dwmapi.h>
#include <shellapi.h>
#include <wingdi.h>
#include <cstring>
#include "SehGuard.h"
namespace w7t {

namespace {
/* Disegna un'icona proteggendosi con SEH: un HICON corrotto o una HDC
 * morta non devono buttare giu' il pannello (riferimento: le mod Windhawk
 * proteggono allo stesso modo i callback di disegno). */
void SafeDrawIconEx(HDC hdc, int x, int y, HICON icon, int w, int h) {
    W7T_SEH_TRY
        DrawIconEx(hdc, x, y, icon, w, h, 0, nullptr, DI_NORMAL);
    W7T_SEH_CATCH
        /* icona non disegnabile: si salta, niente crash */
    W7T_SEH_END
}
}

TrayOverflowWindow::~TrayOverflowWindow() {
    Destroy();
}

bool TrayOverflowWindow::Create(HINSTANCE hInstance, HWND ownerTaskbar) {
    if (m_hWnd) return true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kOverflowClassName;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // niente pennello: sotto c'e' il blur DWM
    RegisterClassExW(&wc);        // ignora errore "gia' registrata"

    // Come la vera NotifyIconOverflowWindow: popup senza bordo standard,
    // topmost, fuori da taskbar/alt-tab.
    m_hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kOverflowClassName, L"User Promoted Notification Area",
        WS_POPUP, 0, 0, 220, 100,
        ownerTaskbar, nullptr, hInstance, nullptr);

    if (!m_hWnd) return false;
    SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // v3.0: pannello BIANCO coprente (le icone devono vedersi) con i bordi
    // Aero identici al flyout orologio (WS_THICKFRAME dal compositor); la
    // ridimensionabilita' viene tolta qui sotto in WM_NCHITTEST (finestra
    // nostra: subclass diretto, nessuna iniezione).
    ApplyAeroFlyoutStyle(m_hWnd);
    return true;
}

void TrayOverflowWindow::Destroy() {
    if (m_hWnd) {
        SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    for (auto& e : m_icons) {
        if (e.icon) DestroyIcon(e.icon);
    }
    m_icons.clear();
}

HICON TrayOverflowWindow::IconFromArgb(const ArgbBitmap& bmp) const {
    if (bmp.empty()) return nullptr;

    BITMAPINFOHEADER bih{};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = bmp.width;
    bih.biHeight      = -bmp.height;   // top-down
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bih),
                                     DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) return nullptr;
    std::memcpy(bits, bmp.pixels.data(), bmp.pixels.size());

    HBITMAP mask = CreateBitmap(bmp.width, bmp.height, 1, 1, nullptr);
    if (!mask) {
        DeleteObject(color);
        return nullptr;
    }

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = color;
    ii.hbmMask  = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

void TrayOverflowWindow::UpdateMetrics() {
    // v3.2: gestione DPI seria: tutte le misure fisse passano di qui.
    UINT dpi = 0;
    if (m_hWnd != nullptr) dpi = GetDpiForWindow(m_hWnd);
    if (dpi == 0) {
        HDC dc = GetDC(nullptr);
        if (dc) { dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY)); ReleaseDC(nullptr, dc); }
    }
    if (dpi == 0 || dpi > 480) dpi = 96;
    m_dpi = dpi;
    m_cell    = MulDiv(kCellSize, static_cast<int>(m_dpi), 96);
    m_pad     = MulDiv(kPadding,  static_cast<int>(m_dpi), 96);
    m_footerH = MulDiv(kFooterH,  static_cast<int>(m_dpi), 96);
    m_icon    = MulDiv(16,        static_cast<int>(m_dpi), 96);
}

void TrayOverflowWindow::RefreshIcons() {
    try {
    for (auto& e : m_icons) {
        if (e.icon) DestroyIcon(e.icon);
    }
    m_icons.clear();

    for (auto& snap : TrayService::Instance().GetUnpinnedSnapshot()) {
        OverflowIconEntry e;
        e.ownerHwnd = snap.key.ownerHwnd;
        e.uid       = snap.key.uid;
        e.icon      = IconFromArgb(snap.bitmap);
        e.tooltip   = snap.tooltip;
        m_icons.push_back(std::move(e));
    }

    // v3.3: il layout viene ricalcolato SEMPRE (anche da nascosto):
    // quando il pannello riapre dopo un pin/unpin l'altezza e' gia'
    // quella giusta ("deve tornare a tale stato").
    Layout();
    if (IsVisible()) {
        InvalidateRect(m_hWnd, nullptr, TRUE);
    }
    } catch (...) { /* elenco non aggiornato: il pannello resta usabile */ }
}

void TrayOverflowWindow::Layout() {
    UpdateMetrics();   // v3.2: ricalcola ad ogni layout (DPI/mostre varie)

    int rows = static_cast<int>((m_icons.size() + kCols - 1) / kCols);
    if (rows < 1) rows = 1;

    const int gridW  = kCols * m_cell;
    const int gridH  = rows * m_cell;
    const int totalW = gridW + m_pad * 2;
    const int totalH = gridH + m_pad * 2 + m_footerH + 1;

    m_footerRect = { m_pad, m_pad + gridH + 1,
                     totalW - m_pad, totalH - m_pad };

    // v3.2: dimensione DETERMINISTICA: l'area client richiesta viene
    // convertita in dimensione finestra con AdjustWindowRectEx (stessi
    // style/exstyle ogni volta), invece di misurare la cornice attuale:
    // cosi' il pannello non puo' finire in "dimensioni errate".
    RECT r{ 0, 0, totalW, totalH };
    AdjustWindowRectEx(&r,
        static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_STYLE)),
        FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE)));

    SetWindowPos(m_hWnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    /* v2.27: se il pannello e' visibile e cambia dimensione (es. entra
     * una quarta icona via drag&drop), ri-ancora il bordo inferiore alla
     * stessa distanza dal pulsante: senza questo passo la finestra
     * cresceva verso il basso restando con l'Y vecchio. */
    if (m_hasAnchor && IsVisible()) {
        RepositionAtAnchor();
    }
}

void TrayOverflowWindow::RepositionAtAnchor() {
    if (!m_hWnd || !m_hasAnchor) return;
    RECT wr{};
    GetWindowRect(m_hWnd, &wr);
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;
    const int cx = (m_anchor.left + m_anchor.right) / 2;
    const int x  = cx - w / 2;
    const int y  = m_anchor.top - h - (6 + (h * 3) / 100) - (h * 45) / 1000;
    SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

void TrayOverflowWindow::ShowNear(RECT btnScreen) {
    if (!m_hWnd) return;
    RefreshIcons();
    Layout();

    m_anchor = btnScreen;   /* v2.27: ricorda l'ancora per i resize */
    m_hasAnchor = true;

    RECT wr{};
    GetWindowRect(m_hWnd, &wr);
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;

    // v3.1: CENTRATO rispetto al pulsante (freccetta) che lo apre e
    // fluttuante qualche pixel piu' in alto, come il vero overflow Win7.
    const int cx = (btnScreen.left + btnScreen.right) / 2;
    const int x  = cx - w / 2;
    // v3.2: fluttua 6 px + un altro 3% della propria altezza piu' in alto.
    /* v2.26: richiesto 4,5% dell'altezza in piu' verso l'alto rispetto
     * al posizionamento precedente (solo posizione, metriche invariate). */
    const int y  = btnScreen.top - h - (6 + (h * 3) / 100) - (h * 45) / 1000;

    SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(m_hWnd, nullptr, TRUE);
}

void TrayOverflowWindow::NotifyTrayChanged() {
    // Conservativo: cerca il pannello solo se esiste (classe registrata da
    // questo modulo) e gli chiede di ricaricarsi sul proprio thread.
    HWND h = FindWindowW(kOverflowClassName, nullptr);
    if (h != nullptr) {
        PostMessageW(h, kMsgOverflowRefresh, 0, 0);
    }
}

void TrayOverflowWindow::Hide() {
    m_hasAnchor = false;   /* v2.27: alla chiusura l'ancora si azzera */
    if (m_hWnd && IsWindowVisible(m_hWnd)) {
        ShowWindow(m_hWnd, SW_HIDE);
        // v3.2: avvisa il lato C# cosi' aggiorna la "texture" della
        // freccetta (stato checked/pressed) e i rettangoli icone.
        CoreState::Instance().QueueEvent(W7T_EVT_OVERFLOW_HIDDEN, 0, 0);
    } else if (m_hWnd) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

int TrayOverflowWindow::HitTestIcon(POINT p) const {
    for (size_t i = 0; i < m_icons.size(); ++i) {
        const int col = static_cast<int>(i) % kCols;
        const int row = static_cast<int>(i) / kCols;
        RECT cell{ m_pad + col * m_cell, m_pad + row * m_cell,
                   m_pad + (col + 1) * m_cell, m_pad + (row + 1) * m_cell };
        if (PtInRect(&cell, p)) return static_cast<int>(i);
    }
    return -1;
}

void TrayOverflowWindow::ForwardClick(int index, bool right) {
    if (index < 0 || index >= static_cast<int>(m_icons.size())) return;
    const auto& e = m_icons[index];

    RECT rc{};
    GetWindowRect(m_hWnd, &rc);
    /* v2.42: il clic inoltrato usa le coordinate DELL'ICONA nel pannello
     * (cella centrata), cosi' il menu contestuale dell'app si apre sopra
     * l'icona stessa e non in un punto generico in alto. */
    const int col = index % kCols;
    const int row = index / kCols;
    const int32_t x = rc.left + m_pad + col * m_cell + m_cell / 2;
    const int32_t y = rc.top + m_pad + row * m_cell + m_cell / 2;

    if (right) {
        TrayService::Instance().SendClick(e.ownerHwnd, e.uid, W7T_TRAY_CLICK_RIGHT, x, y);
    } else {
        TrayService::Instance().SendClick(e.ownerHwnd, e.uid, W7T_TRAY_CLICK_LEFT_DOWN, x, y);
        TrayService::Instance().SendClick(e.ownerHwnd, e.uid, W7T_TRAY_CLICK_LEFT, x, y);
    }
    Hide();   // come in Win7: il clic su un'icona chiude il pannello
}

void TrayOverflowWindow::OnPaint(HDC hdcWindow) {
    RECT rc{};
    GetClientRect(m_hWnd, &rc);

    // Doppio buffer sopra il blur DWM.
    HDC hdc = CreateCompatibleDC(hdcWindow);
    HBITMAP bmp = CreateCompatibleBitmap(hdcWindow, rc.right, rc.bottom);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(hdc, bmp));

    // v3.0: sfondo BIANCO coprente (gradiente del pannello WPF congelato):
    // sopra il blur le icone non si vedevano.
    {
        TRIVERTEX vtx[2] = {};
        vtx[0].x = rc.left;  vtx[0].y = rc.top;
        vtx[0].Red = 0xFF00; vtx[0].Green = 0xFF00; vtx[0].Blue = 0xFF00;
        vtx[1].x = rc.right; vtx[1].y = rc.bottom;
        vtx[1].Red = 0xF400; vtx[1].Green = 0xF400; vtx[1].Blue = 0xF400;
        GRADIENT_RECT gf{ 0, 1 };
        GradientFill(hdc, vtx, 2, &gf, 1, GRADIENT_FILL_RECT_V);
    }

    // v3.1: NIENTE cornice interna disegnata a mano: il bordo Aero arriva
    // dal compositor (ApplyAeroFlyoutStyle), come per il flyout orologio.
    // Le mod Windhawk per i flyout Win7 fanno lo stesso: il client disegna
    // solo il contenuto, il frame lo mette DWM.

    // Griglia icone 3 colonne.
    for (size_t i = 0; i < m_icons.size(); ++i) {
        const int col = static_cast<int>(i) % kCols;
        const int row = static_cast<int>(i) / kCols;
        const int x = m_pad + col * m_cell;
        const int y = m_pad + row * m_cell;

        // Selezione blu al passaggio del mouse, come il pannello vero di
        // Windows 7 (tinta #6EA5D2 al 15% sul chiaro nel pannello WPF).
        if (static_cast<int>(i) == m_hotIcon) {
            RECT cell{ x + 2, y + 2, x + m_cell - 2, y + m_cell - 2 };
            HBRUSH fill = CreateSolidBrush(RGB(0xDC, 0xE9, 0xF5));
            HPEN edge = CreatePen(PS_SOLID, 1, RGB(0x6E, 0xA5, 0xD2));
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, fill));
            HPEN oldPn = static_cast<HPEN>(SelectObject(hdc, edge));
            RoundRect(hdc, cell.left, cell.top, cell.right, cell.bottom, 3, 3);
            SelectObject(hdc, oldBr);
            SelectObject(hdc, oldPn);
            DeleteObject(fill);
            DeleteObject(edge);
        }

        if (m_icons[i].icon) {
            SafeDrawIconEx(hdc, x + (m_cell - m_icon) / 2, y + (m_cell - m_icon) / 2,
                           m_icons[i].icon, m_icon, m_icon);
        }
    }

    // ------------------------------------------------------------------
    // v2.56: footer band, gradient taken from the Windows 7 / 8.1 Action
    // Center recreation mod (win7-action-center-recreation, by babamohammed).
    //
    // The mod paints its flyout with a three-step vertical palette: body
    // white, header RGB(233,240,248), footer RGB(240,245,252), with a 1 px
    // rule in RGB(204,217,234) separating the footer from the content above
    // it. Here the two blues of that palette are used as the two stops of a
    // vertical gradient over the footer band (header tone on top, footer
    // tone at the bottom), and the rule uses the mod's line colour instead
    // of the neutral grey that was there before. Same palette, same family
    // of colours as the reference flyout.
    // ------------------------------------------------------------------
    {
        const int footerTop = m_footerRect.top - 3;
        if (footerTop < rc.top) { /* pannello piccolo: si salta il gradiente */ }
        else
        {
            TRIVERTEX vtx[2] = {};
            vtx[0].x = rc.left;      vtx[0].y = footerTop;
            vtx[0].Red = 0xE9E9; vtx[0].Green = 0xF0F0; vtx[0].Blue = 0xF8F8;  /* RGB(233,240,248) */
            vtx[1].x = rc.right;     vtx[1].y = rc.bottom;
            vtx[1].Red = 0xF0F0; vtx[1].Green = 0xF5F5; vtx[1].Blue = 0xFCFC;  /* RGB(240,245,252) */
            GRADIENT_RECT gfFooter{ 0, 1 };
            GradientFill(hdc, vtx, 2, &gfFooter, 1, GRADIENT_FILL_RECT_V);
        }
    }

    // Separatore + link "Personalizza elementi di notifica...".
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(0xCC, 0xD9, 0xEA));   /* mod: COLOR_BORDER_LINE1 */
    HPEN oldPen2 = static_cast<HPEN>(SelectObject(hdc, sep));
    MoveToEx(hdc, 0, m_footerRect.top - 3, nullptr);
    LineTo(hdc, rc.right, m_footerRect.top - 3);
    SelectObject(hdc, oldPen2);
    DeleteObject(sep);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, m_footerHot ? RGB(0x00, 0x4E, 0x9E) : RGB(0x00, 0x66, 0xCC));
    HFONT font = CreateFontW(-MulDiv(12, static_cast<int>(m_dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, m_footerHot ? TRUE : FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
    RECT textRc = m_footerRect;
    // v3.2: centrato orizzontalmente, come il link vero di Win7.
    // v2.62: il testo arriva dalla tabella delle stringhe native (undici
    // lingue, ripiego inglese): prima era italiano fisso nel codice, quindi
    // su un sistema inglese o tedesco il pannello restava italiano.
    DrawTextW(hdc, w7t::S(w7t::StrId::OverflowCustomize), -1, &textRc,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SelectObject(hdc, oldFont);
    DeleteObject(font);

    BitBlt(hdcWindow, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    SelectObject(hdc, old);
    DeleteObject(bmp);
    DeleteDC(hdc);
}

LRESULT CALLBACK TrayOverflowWindow::WndProc(HWND hWnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<TrayOverflowWindow*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

    // v3.1: il pannello non deve mai crashare la barra: qualunque eccezione
    // C++ nel dispatch viene assorbita (le SEH sono gestite nei helper).
    try {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        self->OnPaint(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;   // niente pennello: il blur DWM fa da sfondo
    case kMsgOverflowRefresh:
        // v3.1: la tray e' cambiata (pin/unpin/aggiunta/rimozione): ricarica
        // conservativamente l'elenco sul thread della finestra.
        self->RefreshIcons();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        int idx = self->HitTestIcon(p);
        if (idx >= 0) {
            self->m_dragIdx = idx;
            self->m_dragStart = p;
            self->m_dragging = false;
            SetCapture(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        if (self->m_dragging) {
            ReleaseCapture();
            self->EndDragImage();
            const int idx = self->m_dragIdx;
            self->m_dragging = false;
            self->m_dragIdx = -1;

            POINT scr = p;
            ClientToScreen(hWnd, &scr);
            RECT wr{};
            GetWindowRect(hWnd, &wr);
            const bool outside = !PtInRect(&wr, scr);
            if (outside && idx >= 0 && idx < static_cast<int>(self->m_icons.size())) {
                // Drop fuori dal pannello (sulla barra): l'icona torna
                // visibile nella tray, come il drag vero di Windows 7.
                const auto& e = self->m_icons[idx];
                TrayService::Instance().SetPinned(e.ownerHwnd, e.uid, 1);
            }
            return 0;
        }
        if (self->m_dragIdx >= 0) {
            ReleaseCapture();
            self->m_dragIdx = -1;
        }
        if (PtInRect(&self->m_footerRect, p)) {
            // v3.0: nulla di piu' nulla di meno del bersaglio shell chiesto
            // dall'utente: la pagina "Icone di notifica" via CLSID.
            ShellExecuteW(hWnd, L"open",
                          L"shell:::{05d7b0f4-2121-4eff-bf6b-ed3f69b894d9}",
                          nullptr, nullptr, SW_SHOWNORMAL);
            self->Hide();
            return 0;
        }
        self->ForwardClick(self->HitTestIcon(p), false);
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        self->ForwardClick(self->HitTestIcon(p), true);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        const bool hot = PtInRect(&self->m_footerRect, p) != FALSE;
        if (hot != self->m_footerHot) {
            self->m_footerHot = hot;
            InvalidateRect(hWnd, &self->m_footerRect, FALSE);
        }

        if (self->m_dragIdx >= 0 && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            if (!self->m_dragging) {
                const int dx = p.x - self->m_dragStart.x;
                const int dy = p.y - self->m_dragStart.y;
                if (abs(dx) > GetSystemMetrics(SM_CXDRAG) || abs(dy) > GetSystemMetrics(SM_CYDRAG)) {
                    self->m_dragging = true;
                }
            }
            if (self->m_dragging) {
                POINT scr = p;
                ClientToScreen(hWnd, &scr);
                self->ShowDragImage(scr);
                return 0;
            }
        }

        // Hover sull'icona: rettangolo di selezione come in Win7.
        const int iconIdx = hot ? -1 : self->HitTestIcon(p);
        if (iconIdx != self->m_hotIcon) {
            self->m_hotIcon = iconIdx;
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self->m_hotIcon != -1) {
            self->m_hotIcon = -1;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        self->m_footerHot = false;
        return 0;
    case WM_NCHITTEST: {
        // v3.0: bordi Aero (WS_THICKFRAME) ma finestra NON ridimensionabile:
        // stessa tecnica della mod del flyout di connessione, ma qui la
        // finestra e' nostra: subclass diretto, nessuna iniezione.
        LRESULT r = DefWindowProcW(hWnd, msg, wParam, lParam);
        switch (r) {
            case HTTOP: case HTTOPLEFT: case HTTOPRIGHT:
            case HTBOTTOM: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
            case HTLEFT: case HTRIGHT:
                return HTBORDER;
            default: return r;
        }
    }
    case WM_SETCURSOR:
        // v3.4: sul link "Personalizza..." il cursore diventa una mano,
        // come su un collegamento vero.
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pc{};
            GetCursorPos(&pc);
            ScreenToClient(hWnd, &pc);
            if (PtInRect(&self->m_footerRect, pc)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;   // la barra non deve rubare il focus
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
    } catch (...) {
        return 0;
    }
}


/* v3.0 drag&drop: icona fantasma semitrasparente che segue il cursore.
 * Finestra layered disegnata a mano (niente dipendenze extra). */
void TrayOverflowWindow::ShowDragImage(POINT pt) {
    try {
    if (m_dragIdx < 0 || m_dragIdx >= static_cast<int>(m_icons.size())) return;
    HICON icon = m_icons[m_dragIdx].icon;
    if (!icon) return;

    if (m_hDragImage == nullptr) {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Win7Taskbar_DragImage";
        RegisterClassW(&wc);
        m_hDragImage = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"Win7Taskbar_DragImage", L"", WS_POPUP,
            pt.x + 8, pt.y + 8, 24, 24, nullptr, nullptr, wc.hInstance, nullptr);
        if (!m_hDragImage) return;
    }

    // icona su sfondo trasparente -> bitmap a 32 bit -> UpdateLayeredWindow
    HDC hdc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(hdc);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = 24; bi.bmiHeader.biHeight = -24;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldDib = (HBITMAP)SelectObject(mem, dib);
    memset(bits, 0, 24 * 24 * 4);
    SafeDrawIconEx(mem, 4, 4, icon, 16, 16);
    POINT srcPt{0, 0};
    SIZE sz{24, 24};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 210, AC_SRC_ALPHA};
    POINT dst{pt.x + 8, pt.y + 8};
    UpdateLayeredWindow(m_hDragImage, hdc, &dst, &sz, mem, &srcPt, 0, &bf, ULW_ALPHA);
    SelectObject(mem, oldDib);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);
    ShowWindow(m_hDragImage, SW_SHOWNOACTIVATE);
    } catch (...) { /* il drag fantasma e' cosmetico: mai crashare */ }
}

void TrayOverflowWindow::EndDragImage() {
    if (m_hDragImage) {
        DestroyWindow(m_hDragImage);
        m_hDragImage = nullptr;
    }
}

} /* namespace w7t */
