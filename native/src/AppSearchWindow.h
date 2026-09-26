#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include "Strings.h"

namespace w7t {
struct AppEntry {
    std::wstring name;
    std::wstring targetPath;
    std::wstring lnkPath;
    std::wstring nameLower;
    std::wstring targetNameLower;
    HICON iconSmall = nullptr;
    HICON iconLarge = nullptr;
    HICON iconPreview = nullptr;
};

class AppSearchWindow {
public:
    ~AppSearchWindow();
    bool Create(HINSTANCE hInstance, HWND owner, const uint32_t* argbPixels = nullptr, int32_t iconW = 0, int32_t iconH = 0);
    void Destroy();
    void Show(int anchorX, int anchorY);
    void Hide();
    bool IsVisible() const { return m_hWnd != nullptr && IsWindowVisible(m_hWnd); }
    // Search appearance only; taskbar skin resources are not modified.
    // 0: translucent Windows 7; 1: opaque Windows 8.1; 2: opaque Win7
    // Aero Basic; 3: the same Windows 8.1 search with a 7.45%-transparent
    // background for Windows 8 Beta 8148. Unknown IDs fall back to Win7.
    void SetTheme(int32_t theme) {
        m_theme = (theme == 1 || theme == 2 || theme == 3) ? theme : 0;
        if (m_hWnd != nullptr) InvalidateRect(m_hWnd, nullptr, TRUE);
    }
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK OutsideMouseProc(int, WPARAM, LPARAM);
    void InstallOutsideMouseHook();
    void RemoveOutsideMouseHook();
    void MaybeRescanIfStale();
    void ScanInstalledApps();
    void ApplyFilter(const std::wstring& query);
    void SelectRow(int index);
    void OnPaint(HDC hdcWindow);
    void RenderScene(HDC hdc, uint32_t* sceneBits, int W, int H, bool mask);
    int HitTestRow(POINT p) const;
    int HitTestOption(POINT p) const;
    bool HitTestClear(POINT p) const;
    bool HitTestMagnifier(POINT p) const;
    void RunOption(int optionIndex);
    void ShowPropertiesOfSelected();
    void DecodeAssetsOnce();
    struct ScrollGeom {
        bool visible = false;
        RECT track{}; RECT upBtn{}; RECT dnBtn{}; RECT thumb{};
        int maxScroll = 0; int firstRow = 0; int totalRows = 0; int visRows = 0;
    };
    ScrollGeom ComputeScrollGeom() const;
    void ClampScroll();
    void ScrollTo(int pos);
    void AddRecentFile(int appIndex);

    /* v2.60: DPI. Il layout della finestra resta scritto in pixel logici
     * (96 dpi) esattamente come prima: la scala entra in gioco solo dove
     * qualcosa tocca lo schermo, cioe' dimensione della finestra,
     * coordinate del mouse e chiamate GDI. I testi usano font creati
     * direttamente alla taglia reale, quindi restano nitidi. */
    int  Px(int logical) const { return MulDiv(logical, static_cast<int>(m_dpi), 96); }
    int  Dip(int device) const { return MulDiv(device, 96, static_cast<int>(m_dpi)); }
    RECT PxRect(int l, int t, int r, int b) const {
        return RECT{ Px(l), Px(t), Px(r), Px(b) };
    }
    RECT PxRect(const RECT& r) const { return PxRect(r.left, r.top, r.right, r.bottom); }
    POINT ToLogical(POINT device) const {
        return POINT{ Dip(device.x), Dip(device.y) };
    }
    UINT m_dpi = 96;

    int32_t m_theme = 0;
    HWND m_hWnd = nullptr;
    /* Taskbar window that opened the panel. A click that lands on the
     * taskbar (or on one of its own child windows) belongs to the taskbar:
     * the managed layer closes the panel there, and closing it from the hook
     * as well would race with the search button's own toggle. */
    HWND m_owner = nullptr;
    /* Screen rectangle of the panel while it is visible; used to tell an
     * outside click from a click on the panel itself. */
    RECT m_windowRect = {};
    HHOOK m_outsideMouseHook = nullptr;
    HICON m_searchIcon = nullptr;
    std::thread m_scanThread;
    std::mutex m_scanMutex;
    std::atomic<bool> m_stopScan{ false };
    std::atomic<bool> m_scanDone{ false };
    std::chrono::steady_clock::time_point m_lastScanTime{};
    static constexpr std::chrono::minutes kRescanInterval{ 2 };
    std::vector<AppEntry> m_allApps;
    std::vector<int> m_filtered;
    std::wstring m_query;
    int m_selectedRow = -1;
    int m_scroll = 0;
    int m_hoverOption = -1;
    bool m_hoverClear = false;
    bool m_hoverMag = false;
    bool m_caretOn = true;
    bool m_hasSelection = false;
    bool m_scrollDragging = false;
    int m_scrollDragAnchorY = 0;
    int m_scrollDragAnchorPos = 0;
    std::vector<int> m_recentFiles;
    std::vector<uint32_t> m_magPixels; int m_magW = 0, m_magH = 0;
    std::vector<uint32_t> m_shieldSmall; int m_shieldSmallW = 0, m_shieldSmallH = 0;
    std::vector<uint32_t> m_openPixels; int m_openW = 0, m_openH = 0;
    std::vector<uint32_t> m_folderPixels; int m_folderW = 0, m_folderH = 0;
    std::vector<uint32_t> m_shieldLarge; int m_shieldLargeW = 0, m_shieldLargeH = 0;
    /* The mouse hook is a global callback with no window handle of its own,
     * so the panel it belongs to is kept here. There is exactly one app
     * search window per process (g_appSearch in Exports.cpp); Create and
     * Destroy keep the pointer in step with the real instance. */
    static AppSearchWindow* s_hookOwner;
    static constexpr int kLeftWidth = 260;
    static constexpr int kRightWidth = 250;
    static constexpr int kTotalWidth = kLeftWidth + kRightWidth;
    static constexpr int kListTop = 10;
    static constexpr int kListH = 372;
    static constexpr int kEditBarH = 46;
    static constexpr int kTotalHeight = kListH + kEditBarH + 10;
    static constexpr int kRowHeight = 38;
    static constexpr int kHeaderH = 104;
    static constexpr int kOptionHeight = 26;
};
} /* namespace w7t */
