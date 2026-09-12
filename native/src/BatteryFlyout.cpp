// Win7Taskbar - flyout batteria nativo Win32, stile Windows 7
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Costruito da zero con API pubbliche documentate (GetSystemPowerStatus,
// GDI/AlphaBlend, DWM). Bordo Aero con lo STESSO helper condiviso degli
// altri flyout (ApplyAeroFlyoutStyle in FlyoutLauncher.cpp), non una copia
// della logica. Le icone sono i glifi REALI ritagliati dalla striscia
// fornita (BatteryAssets.inc), decodificati UNA volta all'avvio.

#include "BatteryFlyout.h"
#include "BatteryAssets.inc"
#include "FlyoutLauncher.h"
#include "SehGuard.h"
#include "Common.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <powrprof.h>
#include <cstring>
#include <vector>

namespace w7t {

namespace {
constexpr wchar_t kClassName[] = L"W7T_BatteryFlyout";
/* v2.40: misure replicate dallo screenshot reale del flyout batteria di
 * Windows 7 (utente): ~270 x 110 px a 96 DPI. Zona superiore (icona+testo)
 * fino a kLinkTop, poi separatore e barra col link centrata. */
constexpr int kWidth  = 270;
constexpr int kHeight = 110;
constexpr int kLinkTop = 70;

/* v2.41: icone disegnate in GDI+ ad alta qualita', con lo STESSO
 * approccio della mod Windhawk MIT "Windows 7/8.1 Action Center
 * Recreation" v2.2.0: gdiplus.dll caricata dinamicamente, puntatori
 * risolti una volta, disegno con interpolazione/smoothing high
 * quality. Se gdiplus non e' disponibile si torna al ripiego GDI
 * (DrawBitmapScaled) senza cambiare layout ne' misure. */
typedef int  (WINAPI *GdipCreateFromHDC_t)(HDC, void**);
typedef int  (WINAPI *GdipCreateBitmapFromScan0_t)(int, int, int, int,
                                                   const void*, void**);
typedef int  (WINAPI *GdipSetInterpolationMode_t)(void*, int);
typedef int  (WINAPI *GdipSetSmoothingMode_t)(void*, int);
typedef int  (WINAPI *GdipSetCompositingQuality_t)(void*, int);
typedef int  (WINAPI *GdipSetPixelOffsetMode_t)(void*, int);
typedef int  (WINAPI *GdipDrawImageRectI_t)(void*, void*, int, int, int, int);
typedef int  (WINAPI *GdipDeleteGraphics_t)(void*);
typedef int  (WINAPI *GdipDisposeImage_t)(void*);
typedef int  (WINAPI *GdiplusStartup_t)(ULONG_PTR*, const void*, void*);
typedef void (WINAPI *GdiplusShutdown_t)(ULONG_PTR);

static HMODULE s_gdipMod = nullptr;
static ULONG_PTR s_gdipToken = 0;
static GdipCreateFromHDC_t fGdipCreateFromHDC = nullptr;
static GdipCreateBitmapFromScan0_t fGdipCreateBitmapFromScan0 = nullptr;
static GdipSetInterpolationMode_t fGdipSetInterpolationMode = nullptr;
static GdipSetSmoothingMode_t fGdipSetSmoothingMode = nullptr;
static GdipSetCompositingQuality_t fGdipSetCompositingQuality = nullptr;
static GdipSetPixelOffsetMode_t fGdipSetPixelOffsetMode = nullptr;
static GdipDrawImageRectI_t fGdipDrawImageRectI = nullptr;
static GdipDeleteGraphics_t fGdipDeleteGraphics = nullptr;
static GdipDisposeImage_t fGdipDisposeImage = nullptr;
static GdiplusShutdown_t fGdiplusShutdown = nullptr;

static bool GdipEnsure() {
    if (s_gdipMod) return true;
    s_gdipMod = LoadLibraryW(L"gdiplus.dll");
    if (!s_gdipMod) return false;
    fGdipCreateFromHDC = reinterpret_cast<GdipCreateFromHDC_t>(
        GetProcAddress(s_gdipMod, "GdipCreateFromHDC"));
    fGdipCreateBitmapFromScan0 = reinterpret_cast<GdipCreateBitmapFromScan0_t>(
        GetProcAddress(s_gdipMod, "GdipCreateBitmapFromScan0"));
    fGdipSetInterpolationMode = reinterpret_cast<GdipSetInterpolationMode_t>(
        GetProcAddress(s_gdipMod, "GdipSetInterpolationMode"));
    fGdipSetSmoothingMode = reinterpret_cast<GdipSetSmoothingMode_t>(
        GetProcAddress(s_gdipMod, "GdipSetSmoothingMode"));
    fGdipSetCompositingQuality = reinterpret_cast<GdipSetCompositingQuality_t>(
        GetProcAddress(s_gdipMod, "GdipSetCompositingQuality"));
    fGdipSetPixelOffsetMode = reinterpret_cast<GdipSetPixelOffsetMode_t>(
        GetProcAddress(s_gdipMod, "GdipSetPixelOffsetMode"));
    fGdipDrawImageRectI = reinterpret_cast<GdipDrawImageRectI_t>(
        GetProcAddress(s_gdipMod, "GdipDrawImageRectI"));
    fGdipDeleteGraphics = reinterpret_cast<GdipDeleteGraphics_t>(
        GetProcAddress(s_gdipMod, "GdipDeleteGraphics"));
    fGdipDisposeImage = reinterpret_cast<GdipDisposeImage_t>(
        GetProcAddress(s_gdipMod, "GdipDisposeImage"));
    auto pStartup = reinterpret_cast<GdiplusStartup_t>(
        GetProcAddress(s_gdipMod, "GdiplusStartup"));
    fGdiplusShutdown = reinterpret_cast<GdiplusShutdown_t>(
        GetProcAddress(s_gdipMod, "GdiplusShutdown"));
    if (!fGdipCreateFromHDC || !fGdipCreateBitmapFromScan0 ||
        !fGdipDrawImageRectI || !fGdipDeleteGraphics || !pStartup) {
        FreeLibrary(s_gdipMod); s_gdipMod = nullptr;
        return false;
    }
    struct { DWORD Version; void* Callback; BOOL Suppress; } si = { 1, nullptr, FALSE };
    if (pStartup(&s_gdipToken, &si, nullptr) != 0) {
        FreeLibrary(s_gdipMod); s_gdipMod = nullptr; s_gdipToken = 0;
        return false;
    }
    return true;
}

/* Crea una bitmap GDI+ 32bppARGB (alpha straight, byte BGRA) dai pixel
 * decodificati: stesso formato uint32 0xAARRGGBB di DecodeEmbeddedPng. */
static void* GdipBitmapFromArgb(const std::vector<uint32_t>& px, int w, int h) {
    if (!GdipEnsure() || w <= 0 || h <= 0 ||
        px.size() < static_cast<size_t>(w) * h) return nullptr;
    void* bmp = nullptr;
    constexpr int kFmt32bppARGB = 0x0026200A;
    if (fGdipCreateBitmapFromScan0(w, h, w * 4, kFmt32bppARGB,
                                   const_cast<uint32_t*>(px.data()),
                                   &bmp) != 0 || !bmp)
        return nullptr;
    return bmp;
}

/* Disegno ad alta qualita' (interpolazione bicubica + smoothing), come
 * DrawGdipBitmapHighQuality della mod di riferimento. */
static bool GdipDrawHQ(HDC hdc, void* bmp, int x, int y, int w, int h) {
    if (!hdc || !bmp || w <= 0 || h <= 0) return false;
    void* g = nullptr;
    if (fGdipCreateFromHDC(hdc, &g) != 0 || !g) return false;
    if (fGdipSetInterpolationMode)   fGdipSetInterpolationMode(g, 7);
    if (fGdipSetSmoothingMode)       fGdipSetSmoothingMode(g, 2);
    if (fGdipSetCompositingQuality)  fGdipSetCompositingQuality(g, 2);
    if (fGdipSetPixelOffsetMode)     fGdipSetPixelOffsetMode(g, 2);
    const int st = fGdipDrawImageRectI(g, bmp, x, y, w, h);
    fGdipDeleteGraphics(g);
    return st == 0;
}

/* Traduzioni minime (stesse lingue dell'app): solo le stringhe del flyout. */
struct BattStr {
    const wchar_t* remaining;   /* "%d%% di carica rimanente" */
    const wchar_t* timeLeft;    /* "Tempo restante: %d h %02d min (%d%%)" */
    const wchar_t* charging;    /* "In carica (%d%%)" */
    const wchar_t* full;        /* "Carica completa" */
    const wchar_t* noBattery;   /* "Batteria non rilevata" */
    const wchar_t* link;        /* "Altre opzioni di risparmio energia" */
};
const BattStr& Str(int lang) {
    static const BattStr kIt = {
        L"%d%% di carica rimanente",
        L"Tempo restante: %d h %02d min (%d%%)",
        L"In carica (%d%%)",
        L"Carica completa",
        L"Batteria non rilevata",
        L"Altre opzioni di risparmio energia" };
    static const BattStr kEn = {
        L"%d%% battery remaining",
        L"Time remaining: %d h %02d min (%d%%)",
        L"Charging (%d%%)",
        L"Fully charged",
        L"No battery detected",
        L"More power options" };
    static const BattStr kEs = {
        L"%d%% de batería restante",
        L"Tiempo restante: %d h %02d min (%d%%)",
        L"Cargando (%d%%)",
        L"Batería cargada",
        L"Batería no detectada",
        L"Más opciones de energía" };
    static const BattStr kFr = {
        L"%d%% de batterie restante",
        L"Temps restant : %d h %02d min (%d%%)",
        L"En charge (%d%%)",
        L"Batterie chargée",
        L"Aucune batterie détectée",
        L"Plus d'options d'alimentation" };
    static const BattStr kDe = {
        L"%d%% Akkuladung verbleibend",
        L"Verbleibend: %d h %02d min (%d%%)",
        L"Wird aufgeladen (%d%%)",
        L"Voll aufgeladen",
        L"Kein Akku erkannt",
        L"Weitere Energieoptionen" };
    static const BattStr kPt = {
        L"%d%% de bateria restante",
        L"Tempo restante: %d h %02d min (%d%%)",
        L"Carregando (%d%%)",
        L"Bateria carregada",
        L"Nenhuma bateria detectada",
        L"Mais opções de energia" };
    static const BattStr kPl = {
        L"Pozostało %d%% baterii",
        L"Pozostały czas: %d h %02d min (%d%%)",
        L"Ładowanie (%d%%)",
        L"W pełni naładowana",
        L"Nie wykryto baterii",
        L"Więcej opcji zasilania" };
    static const BattStr kRu = {
        L"Осталось %d%% заряда",
        L"Осталось: %d ч %02d мин (%d%%)",
        L"Зарядка (%d%%)",
        L"Батарея заряжена",
        L"Батарея не обнаружена",
        L"Дополнительные параметры питания" };
    static const BattStr kJa = {
        L"残り %d%%",
        L"残り時間: %d 時間 %02d 分 (%d%%)",
        L"充電中 (%d%%)",
        L"満充電",
        L"バッテリーが見つかりません",
        L"その他の電源オプション" };
    static const BattStr kZh = {
        L"剩余 %d%%",
        L"剩余时间: %d 小时 %02d 分钟 (%d%%)",
        L"正在充电 (%d%%)",
        L"电量已满",
        L"未检测到电池",
        L"更多电源选项" };
    switch (lang) {
        case 1: return kEn; case 2: return kEs; case 3: return kFr;
        case 4: return kDe; case 5: return kPt; case 6: return kPl;
        case 7: return kRu; case 8: return kJa; case 9: return kZh;
        default: return kIt;
    }
}

/* HBITMAP/AlphaBlend condivisi: vedi Common.cpp (MakeHBitmapFromArgb,
 * DrawBitmapScaled). */
} // namespace

BatteryFlyout& BatteryFlyout::Instance() {
    static BatteryFlyout instance;
    return instance;
}

void BatteryFlyout::SetLanguage(int appLang) {
    m_lang = (appLang >= 0 && appLang <= 9) ? appLang : 0;
    if (m_hwnd && IsWindow(m_hwnd)) InvalidateRect(m_hwnd, nullptr, TRUE);
}

void BatteryFlyout::RegisterClassOnce() {
    if (m_classRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    m_classRegistered = true;
}

void BatteryFlyout::DecodeIconsOnce() {
    if (m_iconsReady) return;
    for (int i = 0; i < battassets::IdxCount; ++i) {
        std::vector<uint32_t> px;
        int w = 0, h = 0;
        /* decodifica UNA volta; un glifo corrotto non blocca gli altri */
        if (DecodeEmbeddedPng(battassets::kAll[i].b64, px, w, h, false, 0)) {
            m_icons[i] = MakeHBitmapFromArgb(px, w, h);
            m_iconW[i] = w;
            m_iconH[i] = h;
            /* v2.41: stessa sorgente anche come bitmap GDI+ (HQ). */
            m_gdip[i] = GdipBitmapFromArgb(px, w, h);
        }
    }
    m_iconsReady = true;
}

void BatteryFlyout::Shutdown() {
    for (int i = 0; i < battassets::IdxCount; ++i) {
        if (m_gdip[i] && fGdipDisposeImage) fGdipDisposeImage(m_gdip[i]);
        m_gdip[i] = nullptr;
    }
    if (s_gdipMod) {
        if (fGdiplusShutdown && s_gdipToken) fGdiplusShutdown(s_gdipToken);
        s_gdipToken = 0;
        FreeLibrary(s_gdipMod);
        s_gdipMod = nullptr;
    }
}

RECT BatteryFlyout::LinkRect() const {
    /* barra inferiore intera: cliccabile ovunque, testo centrato. */
    RECT r{ 0, kLinkTop, kWidth, kHeight };
    return r;
}

void BatteryFlyout::ShowAt(const RECT& iconRect) {
    W7T_SEH_TRY
        RegisterClassOnce();
        DecodeIconsOnce();
        if (m_hwnd == nullptr) {
            m_hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                kClassName, L"", WS_POPUP,
                0, 0, kWidth, kHeight,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m_hwnd == nullptr) return;
            ApplyAeroFlyoutStyle(m_hwnd);   /* bordo Aero condiviso */
        }
        int x = iconRect.left + (iconRect.right - iconRect.left) / 2 - kWidth / 2;
        int y = iconRect.top - kHeight - 4;
        /* resta dentro lo schermo orizzontalmente */
        int sw = GetSystemMetrics(SM_CXSCREEN);
        if (x < 4) x = 4;
        if (x + kWidth > sw - 4) x = sw - kWidth - 4;
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, kWidth, kHeight,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
        InvalidateRect(m_hwnd, nullptr, TRUE);
    W7T_SEH_CATCH
    W7T_SEH_END
}

void BatteryFlyout::Hide() {
    W7T_SEH_TRY
        if (m_hwnd != nullptr) ShowWindow(m_hwnd, SW_HIDE);
    W7T_SEH_CATCH
    W7T_SEH_END
}

bool BatteryFlyout::IsVisible() const {
    return m_hwnd != nullptr && IsWindowVisible(m_hwnd);
}

void BatteryFlyout::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(0xF2, 0xF6, 0xFB));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    SYSTEM_POWER_STATUS sps{};
    GetSystemPowerStatus(&sps);
    const bool charging = (sps.ACLineStatus == 1);
    const bool noBatt   = (sps.BatteryFlag & 128) != 0;
    const int  percent  = (sps.BatteryLifePercent <= 100)
                          ? static_cast<int>(sps.BatteryLifePercent) : -1;

    /* scegli il glifo: colore per soglia, livello per decile */
    int idx = battassets::IdxEmpty;
    if (noBatt) idx = battassets::IdxNoBatt;
    else if (percent >= 0) {
        int base = battassets::IdxGreenBase, maxLvl = 10;
        if (percent <= 15)      { base = battassets::IdxRedBase;    maxLvl = 6; }
        else if (percent <= 35) { base = battassets::IdxYellowBase; maxLvl = 10; }
        int lvl = (percent + 9) / 10;
        if (lvl < 1) lvl = 1;
        if (lvl > maxLvl) lvl = maxLvl;
        idx = base + lvl - 1;
    }
    /* Barra inferiore col link + separatore, come nello screenshot reale. */
    RECT linkBar{ 0, kLinkTop, kWidth, kHeight };
    HBRUSH lb = CreateSolidBrush(RGB(0xE9, 0xF1, 0xFB));
    FillRect(hdc, &linkBar, lb);
    DeleteObject(lb);
    HPEN sep = CreatePen(PS_SOLID, 1, RGB(0xC3, 0xCE, 0xDE));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, sep));
    MoveToEx(hdc, 0, kLinkTop, nullptr);
    LineTo(hdc, kWidth, kLinkTop);
    SelectObject(hdc, oldPen);
    DeleteObject(sep);

    if (m_icons[idx] || m_gdip[idx]) {
        const int dw = 34, dh = 45;
        int iy = (kLinkTop - dh) / 2;
        /* v2.41: prima GDI+ alta qualita', ripiego GDI identico. */
        if (!(m_gdip[idx] && GdipDrawHQ(hdc, m_gdip[idx], 20, iy, dw, dh)))
            DrawBitmapScaled(hdc, m_icons[idx], dw, dh, 20, iy);
        if (charging && (m_icons[battassets::IdxPlug] ||
                         m_gdip[battassets::IdxPlug])) {
            if (!(m_gdip[battassets::IdxPlug] &&
                  GdipDrawHQ(hdc, m_gdip[battassets::IdxPlug], 8, iy + 10, 12, 26)))
                DrawBitmapScaled(hdc, m_icons[battassets::IdxPlug], 12, 26, 8, iy + 10);
        }
    }

    SetBkMode(hdc, TRANSPARENT);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);

    const BattStr& S = Str(m_lang);
    wchar_t line[160] = {};
    if (noBatt) {
        wcscpy_s(line, S.noBattery);
    } else if (charging) {
        if (percent >= 100) wcscpy_s(line, S.full);
        else swprintf_s(line, S.charging, percent);
    } else if (percent < 0) {
        wcscpy_s(line, S.noBattery);
    } else if (sps.BatteryLifeTime != 0xFFFFFFFF && sps.BatteryLifeTime > 0) {
        swprintf_s(line, S.timeLeft,
                   static_cast<int>(sps.BatteryLifeTime / 3600),
                   static_cast<int>((sps.BatteryLifeTime % 3600) / 60),
                   percent);
    } else {
        swprintf_s(line, S.remaining, percent);
    }
    RECT textRect{ 56, 8, kWidth - 12, kLinkTop - 8 };
    SetTextColor(hdc, RGB(0x20, 0x20, 0x20));
    DrawTextW(hdc, line, -1, &textRect, DT_WORDBREAK | DT_VCENTER | DT_LEFT);

    RECT linkRect = LinkRect();
    SetTextColor(hdc, RGB(0x1E, 0x6F, 0xC9));
    DrawTextW(hdc, S.link, -1, &linkRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK BatteryFlyout::WndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    W7T_SEH_TRY
        switch (msg) {
            case WM_PAINT:
                Instance().OnPaint(hwnd);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_LBUTTONUP: {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT lr = Instance().LinkRect();
                if (PtInRect(&lr, pt)) {
                    ShellExecuteW(nullptr, L"open", L"control.exe",
                                  L"powercfg.cpl", nullptr, SW_SHOWNORMAL);
                    Instance().Hide();
                }
                return 0;
            }
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) Instance().Hide();
                return 0;
            case WM_NCDESTROY:
                Instance().m_hwnd = nullptr;
                return 0;
        }
    W7T_SEH_CATCH
    W7T_SEH_END
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace w7t
