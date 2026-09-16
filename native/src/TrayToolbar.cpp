/*
 * Win7Taskbar - Core nativo - Toolbar reale dell'area di notifica
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Modello a ToolbarWindow32 della tray: vedi TrayToolbar.h perche'.
 * Nessun messaggio inventato: TB_*, TBBUTTON, ImageList e SysPager sono
 * documentati in <commctrl.h> (Windows SDK / MinGW-w64).
 */

#include "TrayToolbar.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>


namespace w7t {
namespace {

/* La classe SysPager e' registrata da comctl32 (il pager vero). Caricare
 * comctl32 e inizializzare le classi e' il modo pubblicamente documentato
 * per ottenere sia SysPager sia ToolbarWindow32. */
void EnsureCommonControls() {
    static bool done = false;
    if (!done) {
        INITCOMMONCONTROLSEX icc = {};
        icc.dwSize = sizeof(icc);
        icc.dwICC  = ICC_BAR_CLASSES | ICC_UPDOWN_CLASS;
        InitCommonControlsEx(&icc);
        done = true;
    }
}

} /* namespace */

TrayToolbar& TrayToolbar::Instance() {
    static TrayToolbar instance;
    return instance;
}

TrayToolbar::~TrayToolbar() {
    Destroy();
}

bool TrayToolbar::Create(HWND notifyParent, HINSTANCE instance) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar != nullptr) {
        return true;
    }
    if (notifyParent == nullptr) {
        return false;
    }
    EnsureCommonControls();

    m_iconSize = GetSystemMetrics(SM_CXSMICON);
    if (m_iconSize < 16) {
        m_iconSize = 16;
    }

    /* Gerarchia della shell: TrayNotifyWnd -> SysPager -> ToolbarWindow32.
     * "SysPager" e' la classe del pager registrata da comctl32 (il nome
     * WC_PAGECONTROLW non esiste nell'header MinGW: si usa la stringa
     * documentata). */
    m_pager = CreateWindowExW(
        0, L"SysPager", nullptr,
        WS_CHILD | WS_VISIBLE | CCS_NORESIZE | CCS_NOPARENTALIGN,
        0, 0, 0, 0,
        notifyParent, nullptr, instance, nullptr);
    if (m_pager == nullptr) {
        /* Il pager non e' essenziale: si ripiega sul padre diretto, come
         * fanno alcune build dove la toolbar e' figlia di TrayNotifyWnd. */
        m_pager = notifyParent;
    }

    m_toolbar = CreateWindowExW(
        0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
            CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
        0, 0, 0, m_iconSize + 4,
        m_pager, nullptr, instance, nullptr);
    if (m_toolbar == nullptr) {
        m_pager   = nullptr;
        m_toolbar = nullptr;
        return false;
    }

    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    /* L'ImageList determina la dimensione dei pulsanti: si fissa subito
     * con un segnaposto trasparente, poi ogni icona la sostituisce al
     * proprio indice (ImageList_Replace: nessun reindicizzazione). */
    m_images = ImageList_Create(m_iconSize, m_iconSize, ILC_COLOR32, 4, 16);
    if (m_images != nullptr) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth       = m_iconSize;
        bi.bmiHeader.biHeight      = -m_iconSize;   /* top-down */
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP placeholder = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS,
                                                &bits, nullptr, 0);
        if (placeholder != nullptr) {
            if (bits != nullptr) {
                memset(bits, 0, static_cast<size_t>(m_iconSize) * m_iconSize * 4);
            }
            ImageList_Add(m_images, placeholder, nullptr);
            DeleteObject(placeholder);
        }
        SendMessageW(m_toolbar, TB_SETIMAGELIST, 0,
                     reinterpret_cast<LPARAM>(m_images));
    }

    /* --- finestra di overflow: la gerarchia vera di Vista+ --------- */
    /* NotifyIconOverflowWindow e' una finestra TOP-LEVEL invisibile che
     * vive finche' la shell c'e'; il suo ToolbarWindow32 elenca le icone
     * nascoste. Ricostruiamo identica struttura: chi cerca la classe la
     * trova, e il riquadro e' un controllo vero pilotato dai TB_*, non un
     * disegno. Condivide la nostra ImageList: nessuna duplicazione. */
    WNDCLASSEXW oc = {};
    oc.cbSize        = sizeof(oc);
    oc.lpfnWndProc   = DefWindowProcW;
    oc.hInstance     = instance;
    oc.lpszClassName = L"NotifyIconOverflowWindow";
    if (RegisterClassExW(&oc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        m_overflowWnd = CreateWindowExW(
            0, L"NotifyIconOverflowWindow", L"User Promoted Notification Area",
            WS_POPUP, 0, 0, 0, 0,
            nullptr, nullptr, instance, nullptr);
        if (m_overflowWnd != nullptr) {
            m_overflowBar = CreateWindowExW(
                0, TOOLBARCLASSNAMEW, nullptr,
                WS_CHILD | TBSTYLE_FLAT | CCS_NODIVIDER | CCS_NORESIZE,
                0, 0, 0, 0,
                m_overflowWnd, nullptr, instance, nullptr);
            if (m_overflowBar != nullptr) {
                SendMessageW(m_overflowBar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
                if (m_images != nullptr) {
                    SendMessageW(m_overflowBar, TB_SETIMAGELIST, 0,
                                 reinterpret_cast<LPARAM>(m_images));
                }
            }
        }
    }

    return true;
}

void TrayToolbar::Destroy() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar != nullptr) {
        if (m_images != nullptr) {
            SendMessageW(m_toolbar, TB_SETIMAGELIST, 0, 0);
        }
        DestroyWindow(m_toolbar);
        m_toolbar = nullptr;
    }
    /* m_pager puo' essere il padre stesso (fallback): lo si distrugge solo
     * se l'abbiamo creato noi. */
    if (m_pager != nullptr && m_pager != m_toolbar) {
        /* Il SysPager e' figlio della TrayNotifyWnd: la distruzione del
         * padre lo rimuove. Non lo distruggiamo qui per non toccare finestre
         * che forse non abbiamo creato. */
    }
    if (m_overflowBar != nullptr) {
        DestroyWindow(m_overflowBar);
        m_overflowBar = nullptr;
    }
    if (m_overflowWnd != nullptr) {
        DestroyWindow(m_overflowWnd);
        m_overflowWnd = nullptr;
    }
    m_ovfToIndex.clear();

    m_pager = nullptr;
    m_areaValid = false;
    if (m_images != nullptr) {
        ImageList_Destroy(m_images);
        m_images = nullptr;
    }
    m_idToIndex.clear();
    m_idToImage.clear();
    m_idToString.clear();
}

void TrayToolbar::SetArea(const RECT& clientRect) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pager == nullptr || m_toolbar == nullptr) {
        return;
    }
    const int w = clientRect.right - clientRect.left;
    const int h = clientRect.bottom - clientRect.top;
    MoveWindow(m_pager, clientRect.left, clientRect.top,
               (std::max)(w, 0), (std::max)(h, 0), TRUE);
    MoveWindow(m_toolbar, 0, 0, (std::max)(w, 0), (std::max)(h, 0), TRUE);
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
    /* Geometria utilizzabile solo su area reale: prima del primo
     * SetShellRects la finestra fantasma e' 0x0 e un ripiego li' ancorerebbe
     * i flyout in alto a sinistra (il difetto storico della 1.9.15). */
    m_areaValid = (w > 8 && h > 8);
}

int TrayToolbar::EnsureSlot(uint32_t id) {
    /* Chiamato con m_mutex gia' acquisito dal chiamante: restituisce
     * l'indice ImageList del pulsante, creandolo trasparente se serve. */
    auto it = m_idToImage.find(id);
    if (it != m_idToImage.end()) {
        return it->second;
    }
    if (m_images == nullptr) {
        return -1;
    }
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = m_iconSize;
    bi.bmiHeader.biHeight      = -m_iconSize;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp == nullptr) {
        return -1;
    }
    if (bits != nullptr) {
        memset(bits, 0, static_cast<size_t>(m_iconSize) * m_iconSize * 4);
    }
    const int index = ImageList_Add(m_images, bmp, nullptr);
    DeleteObject(bmp);
    if (index >= 0) {
        m_idToImage[id] = index;
    }
    return index;
}

bool TrayToolbar::ApplyOrder(const std::vector<uint32_t>& order) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr) {
        return false;
    }
    return ApplyToToolbar(m_toolbar, m_idToIndex, order, *this);
}

bool TrayToolbar::ApplyOverflowOrder(const std::vector<uint32_t>& order) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_overflowBar == nullptr) {
        return false;
    }
    return ApplyToToolbar(m_overflowBar, m_ovfToIndex, order, *this);
}

bool TrayToolbar::ApplyToToolbar(HWND wnd, std::map<uint32_t, int>& idMap,
                                 const std::vector<uint32_t>& order,
                                 TrayToolbar& self) {
    bool geometryChanged = false;

    /* 1) Si leggono gli id correnti (TB_GETBUTTON su pulsanti reali). */
    const int current = static_cast<int>(
        SendMessageW(wnd, TB_BUTTONCOUNT, 0, 0));
    std::vector<uint32_t> ids(static_cast<size_t>((std::max)(current, 0)), 0);
    for (int i = 0; i < current; ++i) {
        TBBUTTON b = {};
        if (SendMessageW(wnd, TB_GETBUTTON, i,
                         reinterpret_cast<LPARAM>(&b)) != FALSE) {
            ids[static_cast<size_t>(i)] = static_cast<uint32_t>(b.idCommand);
        }
    }

    /* 2) Si tolgono i pulsanti che non esistono piu' (TB_DELETEBUTTON).
     *    Si procede dal fondo cosi' gli indici restano validi. */
    for (int i = current - 1; i >= 0; --i) {
        const uint32_t id = ids[static_cast<size_t>(i)];
        if (std::find(order.begin(), order.end(), id) == order.end()) {
            SendMessageW(wnd, TB_DELETEBUTTON, i, 0);
            idMap.erase(id);
            geometryChanged = true;
        }
    }

    /* 3) Si inseriscono i nuovi (TB_INSERTBUTTONW) e si riordinano i gia'
     *    presenti (TB_MOVEBUTTON), come fa la shell a ogni add/remove/
     *    drag&drop del pulsante. */
    for (size_t want = 0; want < order.size(); ++want) {
        const uint32_t id = order[want];
        int have = -1;
        const int count = static_cast<int>(
            SendMessageW(wnd, TB_BUTTONCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
            TBBUTTON b = {};
            if (SendMessageW(wnd, TB_GETBUTTON, i,
                             reinterpret_cast<LPARAM>(&b)) != FALSE
                && static_cast<uint32_t>(b.idCommand) == id) {
                have = i;
                break;
            }
        }
        if (have < 0) {
            TBBUTTON b = {};
            b.iBitmap   = self.EnsureSlot(id);
            b.idCommand = static_cast<INT_PTR>(id);
            b.fsState   = TBSTATE_ENABLED;
            b.fsStyle   = BTNS_BUTTON;
            b.iString   = 0;
            if (SendMessageW(wnd, TB_INSERTBUTTONW,
                             static_cast<WPARAM>(want),
                             reinterpret_cast<LPARAM>(&b)) != FALSE) {
                idMap[id] = static_cast<int>(want);
                geometryChanged = true;
            }
        } else if (have != static_cast<int>(want)) {
            SendMessageW(wnd, TB_MOVEBUTTON, have,
                         static_cast<LPARAM>(want));
            geometryChanged = true;
        }
    }

    /* 4) Si riallineano gli indici noti: TB_MOVEBUTTON puo' aver spostato
     *    tutto; si rilegge la lista e si aggiorna la mappa. */
    const int finalCount = static_cast<int>(
        SendMessageW(wnd, TB_BUTTONCOUNT, 0, 0));
    idMap.clear();
    for (int i = 0; i < finalCount; ++i) {
        TBBUTTON b = {};
        if (SendMessageW(wnd, TB_GETBUTTON, i,
                         reinterpret_cast<LPARAM>(&b)) != FALSE) {
            idMap[static_cast<uint32_t>(b.idCommand)] = i;
        }
    }

    if (geometryChanged) {
        SendMessageW(wnd, TB_AUTOSIZE, 0, 0);
    }
    return geometryChanged;
}

void TrayToolbar::SetButtonImage(uint32_t id, const ArgbBitmap& pixels) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr || m_images == nullptr) {
        return;
    }
    const int index = EnsureSlot(id);
    if (index < 0) {
        return;
    }
    const int w = m_iconSize;
    const int h = m_iconSize;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp == nullptr) {
        return;
    }
    if (bits != nullptr) {
        uint8_t* dst = static_cast<uint8_t*>(bits);
        memset(dst, 0, static_cast<size_t>(w) * h * 4);
        /* L'ARGB del core e' gia' BGRA top-down alla dimensione nativa
         * dell'icona: si centra nel quadro senza ricampionare quando le
         * dimensioni coincidono, altrimenti si copia il minimo. */
        const int copyW = (std::min)(w, pixels.width);
        const int copyH = (std::min)(h, pixels.height);
        const int offX = (w - copyW) / 2;
        const int offY = (h - copyH) / 2;
        for (int y = 0; y < copyH; ++y) {
            const uint8_t* srcRow = pixels.pixels.data()
                + static_cast<size_t>(y + (std::max)(0, (pixels.height - copyH) / 2))
                * static_cast<size_t>(pixels.width) * 4
                + static_cast<size_t>((std::max)(0, (pixels.width - copyW) / 2)) * 4;
            uint8_t* dstRow = dst
                + static_cast<size_t>(offY + y) * static_cast<size_t>(w) * 4
                + static_cast<size_t>(offX) * 4;
            memcpy(dstRow, srcRow, static_cast<size_t>(copyW) * 4);
        }
    }
    /* Sostituzione in-place: gli indici degli altri pulsanti non cambiano,
     * quindi nessuna geometria da ricalcolare e nessun lampeggio. */
    ImageList_Replace(m_images, index, bmp, nullptr);
    DeleteObject(bmp);
}

void TrayToolbar::SetButtonHidden(uint32_t id, bool hidden) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr) {
        return;
    }
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) {
        return;
    }
    const int state = static_cast<int>(
        SendMessageW(m_toolbar, TB_GETSTATE, static_cast<WPARAM>(it->second), 0));
    const BYTE desired = TBSTATE_ENABLED
                       | (hidden ? static_cast<BYTE>(TBSTATE_HIDDEN) : 0);
    if (static_cast<BYTE>(state) != desired) {
        /* TBSTATE_HIDDEN: il meccanismo con cui la shell (9x/2003 col pager,
         * Vista+ nella toolbar "User Promoted") toglie il pulsante dalla
         * vista senza rimuoverlo dal modello. */
        SendMessageW(m_toolbar, TB_SETSTATE, static_cast<WPARAM>(it->second),
                     MAKELPARAM(desired, static_cast<WORD>(-1)));
        SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
    }
}

bool TrayToolbar::IsButtonHidden(uint32_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr) {
        return false;
    }
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) {
        return false;
    }
    const int state = static_cast<int>(
        SendMessageW(m_toolbar, TB_GETSTATE, static_cast<WPARAM>(it->second), 0));
    return (state & TBSTATE_HIDDEN) != 0;
}

void TrayToolbar::SetButtonText(uint32_t id, const std::wstring&) {
    /* Il testo vive nel modello del servizio (m_icons.tooltip) perche' la
     * toolbar e' creata senza TBSTYLE_LIST: il pulsante referenzia il solo
     * stato. Tenere qui una copia sarebbe una secondo verita'. */
    (void)id;
}

void TrayToolbar::RemoveButton(uint32_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr) {
        return;
    }
    auto it = m_idToIndex.find(id);
    if (it != m_idToIndex.end()) {
        SendMessageW(m_toolbar, TB_DELETEBUTTON,
                     static_cast<WPARAM>(it->second), 0);
        m_idToIndex.erase(it);
        SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
    }
}

int TrayToolbar::ButtonCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr) {
        return 0;
    }
    return static_cast<int>(SendMessageW(m_toolbar, TB_BUTTONCOUNT, 0, 0));
}

int TrayToolbar::IndexOf(uint32_t id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_idToIndex.find(id);
    return it == m_idToIndex.end() ? -1 : it->second;
}

bool TrayToolbar::GetItemScreenRect(uint32_t id, RECT& out) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr || !m_areaValid) {
        return false;
    }
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end()) {
        return false;
    }
    RECT r = {};
    if (SendMessageW(m_toolbar, TB_GETITEMRECT, static_cast<WPARAM>(it->second),
                     reinterpret_cast<LPARAM>(&r)) == FALSE) {
        return false;
    }
    POINT tl = { r.left, r.top };
    POINT br = { r.right, r.bottom };
    if (!ClientToScreen(m_toolbar, &tl) || !ClientToScreen(m_toolbar, &br)) {
        return false;
    }
    out.left   = tl.x;
    out.top    = tl.y;
    out.right  = br.x;
    out.bottom = br.y;
    return true;
}

uint32_t TrayToolbar::HitTest(const POINT& screenPoint) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr || !m_areaValid) {
        return 0;
    }
    POINT p = screenPoint;
    if (!ScreenToClient(m_toolbar, &p)) {
        return 0;
    }
    const LRESULT idx = SendMessageW(m_toolbar, TB_HITTEST, 0,
                                     reinterpret_cast<LPARAM>(&p));
    if (idx < 0) {
        return 0;
    }
    TBBUTTON b = {};
    if (SendMessageW(m_toolbar, TB_GETBUTTON, static_cast<WPARAM>(idx),
                     reinterpret_cast<LPARAM>(&b)) == FALSE) {
        return 0;
    }
    return static_cast<uint32_t>(b.idCommand);
}

} /* namespace w7t */
