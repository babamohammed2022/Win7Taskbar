// Win7Taskbar - pannello overflow nativo con vetro Aero
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v2.7: finestra Win32 propria (come la vera NotifyIconOverflowWindow)
// con blur DWM vero (AeroGlass), griglia 3 colonne e link "Personalizza
// elementi di notifica...". Sostituisce il Popup WPF quando la creazione
// riesce; il Popup resta solo come fallback raro (vedi TaskbarWindow).

#pragma once
#include <windows.h>
#include <uxtheme.h>
#include <vector>
#include <string>
#include "Common.h"
namespace w7t {

/* Nome classe del pannello: usato anche da TrayService per notificare
 * cambiamenti della tray senza accoppiamenti forti. */
inline constexpr wchar_t kOverflowClassName[] = L"Win7Taskbar_TrayOverflow";

/* Messaggio registrato-like (WM_APP privato): "ricarica le icone". */
constexpr UINT kMsgOverflowRefresh = WM_APP + 40;

struct OverflowIconEntry {
    uint64_t ownerHwnd;
    uint32_t uid;
    HICON    icon;
    std::wstring tooltip;
};

class TrayOverflowWindow {
public:
    ~TrayOverflowWindow();

    bool Create(HINSTANCE hInstance, HWND ownerTaskbar);
    void Destroy();
    HWND NativeHandle() const { return m_hWnd; }

    /* Ricarica le icone non fissate dal modello TrayService e le disegna. */
    void RefreshIcons();

    /* v3.1: notifica conservativa: se il pannello e' aperto gli manda un
     * messaggio e lui si ricarica sul proprio thread (nessun cross-thread). */
    static void NotifyTrayChanged();
    void ShowNear(RECT overflowButtonScreenRect);
    void Hide();
    bool IsVisible() const { return m_hWnd != nullptr && IsWindowVisible(m_hWnd); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void OnPaint(HDC hdc);
    void Layout();
    int  HitTestIcon(POINT clientPt) const;
    HICON IconFromArgb(const ArgbBitmap& bmp) const;
    void ForwardClick(int index, bool right);

    HWND   m_hWnd = nullptr;
    int    m_hotIcon = -1;      // icona sotto il cursore (hover Win7)

    /* v3.8 (1.21.49): texture di hover = la STESSA tessera di vetro che la
     * system tray accende sotto le icone (OverflowTileAsset.inc, estratta
     * dal bundle C#). Decodificata UNA volta al primo uso; se non e'
     * disponibile OnPaint ripiega sul rettangolo azzurro classico: la
     * texture e' un miglioramento, mai un punto di crash. */
    HBITMAP m_hoverTile = nullptr;
    bool    m_hoverTileTried = false;
    void    EnsureHoverTile();

    // v3.2: misure scalate col DPI (GetDpiForWindow): i numeri fissi dei
    // commenti restano i valori a 96 DPI, qui arrivano moltiplicati.
    UINT   m_dpi = 96;
    int    m_cell = kCellSize;
    int    m_pad  = kPadding;
    int    m_footerH = kFooterH;
    int    m_icon = 16;
    void   UpdateMetrics();

    // v3.0: drag nativo dal pannello verso la tray (icona che segue il cursore)
    int    m_dragIdx = -1;
    POINT  m_dragStart{};
    bool   m_dragging = false;
    HWND   m_hDragImage = nullptr;
    void   ShowDragImage(POINT screenPt);
    void   EndDragImage();
    std::vector<OverflowIconEntry> m_icons;
    bool m_footerHot = false;
    RECT m_footerRect{};

    /* v2.27: ancoraggio al pulsante che ha aperto il pannello: se il
     * contenuto cambia dimensione da visibile (drag&drop di una nuova
     * icona) il riquadro deve restare ancorato SOPRA il pulsante, non
     * crescere verso il basso. */
    RECT m_anchor{};
    bool m_hasAnchor = false;
    void RepositionAtAnchor();

    // Misure fedeli allo screenshot Win7, scala +5% (v3.1): cella 36x36,
    // 3 colonne, icona 16px centrata.
    static constexpr int kCols     = 3;
    static constexpr int kCellSize = 36;
    static constexpr int kPadding  = 6;
    static constexpr int kFooterH  = 26;
};

} /* namespace w7t */
