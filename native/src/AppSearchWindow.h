// Win7Taskbar - ricerca applicazioni opzionale
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.6: finestra NON layered con VERO vetro Aero (blur DWM + tinta blu
// satura, come lo screenshot della ricerca Win7): il compositor sfoca
// il desktop dietro, noi disegniamo testo bianco nitido sopra (ClearType
// funziona perche' la finestra e' regolare). Bordo Aero dal frame DWM,
// non ridimensionabile. Misure avvicinate alle proporzioni della foto.

#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

namespace w7t {

struct AppEntry {
    std::wstring name;
    std::wstring targetPath;
    std::wstring lnkPath;
    /* v2.37: precalcolate UNA volta in scansione (bug #1 + velocita'):
     * nome minuscolo e nome del file target SENZA estensione minuscolo
     * (cosi' "notepad" trova "Blocco note.lnk" -> notepad.exe). */
    std::wstring nameLower;
    std::wstring targetNameLower;
    HICON iconSmall = nullptr;
    HICON iconLarge = nullptr;
    HICON iconPreview = nullptr;
};

class AppSearchWindow {
public:
    ~AppSearchWindow();

    bool Create(HINSTANCE hInstance, HWND owner,
                const uint32_t* argbPixels = nullptr,
                int32_t iconW = 0, int32_t iconH = 0);
    void Destroy();
    void Show(int anchorX, int anchorY);
    void Hide();
    bool IsVisible() const { return m_hWnd != nullptr && IsWindowVisible(m_hWnd); }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void MaybeRescanIfStale();
    void ScanInstalledApps();
    void ApplyFilter(const std::wstring& query);
    void SelectRow(int index);
    void OnPaint(HDC hdcWindow);
    void RenderScene(HDC hdc, uint32_t* sceneBits, int W, int H, bool mask);
    int  HitTestRow(POINT p) const;
    int  HitTestOption(POINT p) const;
    bool HitTestClear(POINT p) const;
    bool HitTestMagnifier(POINT p) const;           /* v2.37 punto 11 */
    void RunOption(int optionIndex);
    void ShowPropertiesOfSelected();

    /* v2.37: risorse incorporate (punti 10/13) decodificate una volta. */
    void DecodeAssetsOnce();

    /* v2.37 punto 12: scrollbar DISEGNATA (una finestra layered non puo'
     * ospitare le scrollbar native di Windows). */
    struct ScrollGeom {
        bool visible = false;
        RECT track{};      /* binario completo (frecce comprese) */
        RECT upBtn{};
        RECT dnBtn{};
        RECT thumb{};
        int  maxScroll = 0;
        int  firstRow  = 0;
        int  totalRows = 0;
        int  visRows   = 0;
    };
    ScrollGeom ComputeScrollGeom() const;
    void ClampScroll();
    void ScrollTo(int pos);
    void AddRecentFile(int appIndex);

    HWND  m_hWnd = nullptr;
    HICON m_searchIcon = nullptr;

    /* v3.9: scansione app su thread separato: la taskbar non si blocca
     * piu' mentre enumeriamo collegamenti/icone. */
    std::thread m_scanThread;
    std::mutex  m_scanMutex;
    std::atomic<bool> m_stopScan{ false };
    std::atomic<bool> m_scanDone{ false };

    /* v2.43: rescan periodico dell'indice (vedi MaybeRescanIfStale in
     * AppSearchWindow.cpp): il menu Start puo' cambiare mentre il pannello
     * resta chiuso per ore, quindi alla riapertura si riscansiona se
     * l'indice ha piu' di kRescanInterval. */
    std::chrono::steady_clock::time_point m_lastScanTime{};
    static constexpr std::chrono::minutes kRescanInterval{ 2 };

    std::vector<AppEntry> m_allApps;
    std::vector<int> m_filtered;
    std::wstring m_query;
    int  m_selectedRow = -1;
    int  m_scroll = 0;
    int  m_hoverOption = -1;
    bool m_hoverClear = false;
    bool m_hoverMag = false;      /* v2.37 punto 11: terza zona hover */
    bool m_caretOn = true;

    /* v2.37 punto 7: selezione + appunti della casella (non e' un vero
     * controllo EDIT: la selezione copre l'intero testo). */
    bool m_hasSelection = false;

    /* v2.37 punto 12: trascinamento della scrollbar disegnata. */
    bool m_scrollDragging = false;
    int  m_scrollDragAnchorY = 0;
    int  m_scrollDragAnchorPos = 0;

    /* v2.37 punto 9: file recenti (indici in m_allApps, piu' recente
     * per primo). Vuoto -> intestazione e separatore non disegnati. */
    std::vector<int> m_recentFiles;

    /* v2.37 punti 10/13: pixel ARGB (alpha DRITTO, non premoltiplicato)
     * decodificati UNA volta all'avvio della finestra. */
    std::vector<uint32_t> m_magPixels;   int m_magW = 0, m_magH = 0;
    std::vector<uint32_t> m_shieldSmall; int m_shieldSmallW = 0, m_shieldSmallH = 0;
    /* v2.41: icone incorporate per le voci "Apri" (openicon.png) e
     * "Apri percorso file" (image-2.png), fornite dall'utente. */
    std::vector<uint32_t> m_openPixels;  int m_openW = 0, m_openH = 0;
    std::vector<uint32_t> m_folderPixels; int m_folderW = 0, m_folderH = 0;
    std::vector<uint32_t> m_shieldLarge; int m_shieldLargeW = 0, m_shieldLargeH = 0;

    // Proporzioni ricalcate sullo screenshot della ricerca Win7.
    static constexpr int kLeftWidth  = 260;
    static constexpr int kRightWidth = 250;
    static constexpr int kTotalWidth = kLeftWidth + kRightWidth;
    static constexpr int kListTop    = 10;
    static constexpr int kListH      = 372;
    static constexpr int kEditBarH   = 46;
    static constexpr int kTotalHeight= kListH + kEditBarH + 10;
    /* v2.35: righe come Windows 7 (32 px di icona + padding). */
    static constexpr int kRowHeight  = 38;
    static constexpr int kHeaderH    = 104;
    static constexpr int kOptionHeight = 26;
};

} /* namespace w7t */
