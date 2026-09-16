// Win7Taskbar - Windows 7/8.1 style language switcher: a port of the
// Windhawk mod "Windows 7/8.1 Language Switcher Restorer" v1.1.0
// (babamohammed2022, GPL-3.0 or later).
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// See the .h header for the list of what was ported and what was dropped
// (the parts of the mod that only exist because it is injected into
// explorer.exe).

#include "LanguageSwitcher.h"

#include "Common.h"
#include "Strings.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <strsafe.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace w7t {
namespace langswitcher {

namespace {

/* ------------------------------------------------------------------ */
/*  Constants and state                                                 */
/* ------------------------------------------------------------------ */

constexpr wchar_t kFlyoutClassName[] = L"W7T_LangSwitcherFlyout";
constexpr UINT kLangWatchTimerId = 0xB7;   /* 200 ms: active lang  */
constexpr UINT kLangWatchTimerMs = 200;

struct KeyboardLayoutItem {
    HKL hkl = nullptr;
    LANGID langId = 0;
    std::wstring langName;
    std::wstring langAbbrev;
    std::wstring subAbbrev;
    std::wstring layoutName;
    std::wstring klid;
    bool isCurrent = false;
    size_t sameLangCount = 1;
};

enum class SwitcherStyle { Win8, Win7 };

/* The popup window and its thread (the one that created it: the thread
 * the taskbar UI) and the EXTERNAL window that is the layout-switch target
 * (clicking the taskbar takes the foreground away from the user). */
std::atomic<HWND> g_hFlyoutWnd{ nullptr };
std::atomic<HWND> g_targetWindow{ nullptr };
std::atomic<HWND> g_hClickedTaskbar{ nullptr };

CRITICAL_SECTION g_cs;
bool g_csInit = false;
SwitcherStyle g_style = SwitcherStyle::Win8;
int g_styleMode = 1;                 /* Properties mode (1/2/3)           */
std::vector<KeyboardLayoutItem> g_layouts;
size_t g_selectedIndex = 0;
int g_hoveredIndex = -1;
bool g_hoveredFooter = false;
int g_hoveredWin7Index = -1;
WORD g_lastNotifiedLangId = 0;
LangChangedCallback g_callback = nullptr;

HMODULE g_hGdiPlus = nullptr;
ULONG_PTR g_gdiplusToken = 0;

/* ------------------------------------------------------------------ */
/*  GDI+ loaded at runtime (as in the mod: no link-time dependency,    */
/*  the checkmark drawing degrades to plain GDI).                      */
/* ------------------------------------------------------------------ */

typedef int (WINAPI *GdiplusStartupFunc)(ULONG_PTR*, const void*, void*);
typedef void (WINAPI *GdiplusShutdownFunc)(ULONG_PTR);
typedef int (WINAPI *GdipCreateFromHDCFunc)(HDC, void**);
typedef int (WINAPI *GdipDeleteGraphicsFunc)(void*);
typedef int (WINAPI *GdipSetSmoothingModeFunc)(void*, int);
typedef int (WINAPI *GdipSetPixelOffsetModeFunc)(void*, int);
typedef int (WINAPI *GdipCreatePathFunc)(int, void**);
typedef int (WINAPI *GdipDeletePathFunc)(void*);
typedef int (WINAPI *GdipAddPathPolygonFunc)(void*, const void*, int);
typedef int (WINAPI *GdipCreateSolidFillFunc)(DWORD, void**);
typedef int (WINAPI *GdipDeleteBrushFunc)(void*);
typedef int (WINAPI *GdipFillPathFunc)(void*, void*, void*);
typedef int (WINAPI *GdipCreatePen1Func)(DWORD, float, int, void**);
typedef int (WINAPI *GdipDeletePenFunc)(void*);
typedef int (WINAPI *GdipSetPenLineJoinFunc)(void*, int);
typedef int (WINAPI *GdipDrawPathFunc)(void*, void*, void*);

static GdipCreateFromHDCFunc pGdipCreateFromHDC = nullptr;
static GdipDeleteGraphicsFunc pGdipDeleteGraphics = nullptr;
static GdipSetSmoothingModeFunc pGdipSetSmoothingMode = nullptr;
static GdipSetPixelOffsetModeFunc pGdipSetPixelOffsetMode = nullptr;
static GdipCreatePathFunc pGdipCreatePath = nullptr;
static GdipDeletePathFunc pGdipDeletePath = nullptr;
static GdipAddPathPolygonFunc pGdipAddPathPolygon = nullptr;
static GdipCreateSolidFillFunc pGdipCreateSolidFill = nullptr;
static GdipDeleteBrushFunc pGdipDeleteBrush = nullptr;
static GdipFillPathFunc pGdipFillPath = nullptr;
static GdipCreatePen1Func pGdipCreatePen1 = nullptr;
static GdipDeletePenFunc pGdipDeletePen = nullptr;
static GdipSetPenLineJoinFunc pGdipSetPenLineJoin = nullptr;
static GdipDrawPathFunc pGdipDrawPath = nullptr;

static BOOL InitGdiPlusRendering() {
    if (g_hGdiPlus) return TRUE;
    g_hGdiPlus = LoadLibraryW(L"gdiplus.dll");
    if (!g_hGdiPlus) return FALSE;

    GdiplusStartupFunc pStartup = (GdiplusStartupFunc)GetProcAddress(
        g_hGdiPlus, "GdiplusStartup");
    if (!pStartup) { FreeLibrary(g_hGdiPlus); g_hGdiPlus = nullptr; return FALSE; }

    struct { DWORD Version; void* Callback; BOOL Suppress; } si = {1, nullptr, FALSE};
    if (pStartup(&g_gdiplusToken, &si, nullptr) != 0) {
        FreeLibrary(g_hGdiPlus); g_hGdiPlus = nullptr; return FALSE;
    }

    #define W7T_GDIP_PROC(name, type) p##name = (type)GetProcAddress(g_hGdiPlus, #name); \
        if (!p##name) { ShutdownGdiPlusRendering(); return FALSE; }
    W7T_GDIP_PROC(GdipCreateFromHDC, GdipCreateFromHDCFunc)
    W7T_GDIP_PROC(GdipDeleteGraphics, GdipDeleteGraphicsFunc)
    W7T_GDIP_PROC(GdipSetSmoothingMode, GdipSetSmoothingModeFunc)
    W7T_GDIP_PROC(GdipSetPixelOffsetMode, GdipSetPixelOffsetModeFunc)
    W7T_GDIP_PROC(GdipCreatePath, GdipCreatePathFunc)
    W7T_GDIP_PROC(GdipDeletePath, GdipDeletePathFunc)
    W7T_GDIP_PROC(GdipAddPathPolygon, GdipAddPathPolygonFunc)
    W7T_GDIP_PROC(GdipCreateSolidFill, GdipCreateSolidFillFunc)
    W7T_GDIP_PROC(GdipDeleteBrush, GdipDeleteBrushFunc)
    W7T_GDIP_PROC(GdipFillPath, GdipFillPathFunc)
    W7T_GDIP_PROC(GdipCreatePen1, GdipCreatePen1Func)
    W7T_GDIP_PROC(GdipDeletePen, GdipDeletePenFunc)
    W7T_GDIP_PROC(GdipSetPenLineJoin, GdipSetPenLineJoinFunc)
    W7T_GDIP_PROC(GdipDrawPath, GdipDrawPathFunc)
    #undef W7T_GDIP_PROC
    return TRUE;
}

static void ShutdownGdiPlusRendering() {
    if (g_hGdiPlus) {
        GdiplusShutdownFunc pShutdown = (GdiplusShutdownFunc)GetProcAddress(
            g_hGdiPlus, "GdiplusShutdown");
        if (pShutdown) pShutdown(g_gdiplusToken);
        FreeLibrary(g_hGdiPlus);
        g_hGdiPlus = nullptr;
    }
    g_gdiplusToken = 0;
}

/* ------------------------------------------------------------------ */
/*  Mod tables: abbreviations and localization                         */
/* ------------------------------------------------------------------ */

struct LangAbbrevEntry {
    WORD langId;
    const wchar_t* abbrev;
};

static const LangAbbrevEntry g_LangAbbrevs[] = {
    { LANG_AFRIKAANS,   L"AFR" },
    { LANG_ALBANIAN,    L"ALB" },
    { LANG_ARABIC,      L"ARA" },
    { LANG_ARMENIAN,    L"ARM" },
    { LANG_ASSAMESE,    L"ASM" },
    { LANG_AZERI,       L"AZE" },
    { LANG_BASQUE,      L"BSQ" },
    { LANG_BELARUSIAN,  L"BEL" },
    { LANG_BENGALI,     L"BEN" },
    { LANG_BULGARIAN,   L"BGR" },
    { LANG_CATALAN,     L"CAT" },
    { LANG_CHINESE,     L"CHS" },
    { LANG_CROATIAN,    L"HRV" },
    { LANG_CZECH,       L"CSY" },
    { LANG_DANISH,      L"DAN" },
    { LANG_DUTCH,       L"NLD" },
    { LANG_ENGLISH,     L"ENG" },
    { LANG_ESTONIAN,    L"ETI" },
    { LANG_FAEROESE,    L"FRO" },
    { LANG_FARSI,       L"FAR" },
    { LANG_FINNISH,     L"FIN" },
    { LANG_FRENCH,      L"FRA" },
    { LANG_GEORGIAN,    L"GEO" },
    { LANG_GERMAN,      L"DEU" },
    { LANG_GREEK,       L"ELL" },
    { LANG_GUJARATI,    L"GUJ" },
    { LANG_HEBREW,      L"HEB" },
    { LANG_HINDI,       L"HIN" },
    { LANG_HUNGARIAN,   L"HUN" },
    { LANG_ICELANDIC,   L"ISL" },
    { LANG_INDONESIAN,  L"IND" },
    { LANG_ITALIAN,     L"ITA" },
    { LANG_JAPANESE,    L"JPN" },
    { LANG_KANNADA,     L"KAN" },
    { LANG_KASHMIRI,    L"KSH" },
    { LANG_KAZAK,       L"KAZ" },
    { LANG_KONKANI,     L"KOK" },
    { LANG_KOREAN,      L"KOR" },
    { LANG_LATVIAN,     L"LVI" },
    { LANG_LITHUANIAN,  L"LTH" },
    { LANG_MACEDONIAN,  L"MKI" },
    { LANG_MALAY,       L"MSL" },
    { LANG_MALAYALAM,   L"MAL" },
    { LANG_MANIPURI,    L"MPI" },
    { LANG_MARATHI,     L"MAR" },
    { LANG_NEPALI,      L"NEP" },
    { LANG_NORWEGIAN,   L"NOR" },
    { LANG_ORIYA,       L"ORI" },
    { LANG_POLISH,      L"PLK" },
    { LANG_PORTUGUESE,  L"PTG" },
    { LANG_PUNJABI,     L"PAN" },
    { LANG_ROMANIAN,    L"ROM" },
    { LANG_RUSSIAN,     L"RUS" },
    { LANG_SANSKRIT,    L"SAN" },
    { LANG_SERBIAN,     L"SRB" },
    { LANG_SLOVAK,      L"SLK" },
    { LANG_SLOVENIAN,   L"SLV" },
    { LANG_SPANISH,     L"ESP" },
    { LANG_SWAHILI,     L"SWK" },
    { LANG_SWEDISH,     L"SVE" },
    { LANG_TAMIL,       L"TAM" },
    { LANG_TATAR,       L"TTT" },
    { LANG_TELUGU,      L"TEL" },
    { LANG_THAI,        L"THA" },
    { LANG_TURKISH,     L"TRK" },
    { LANG_UKRAINIAN,   L"UKR" },
    { LANG_URDU,        L"URD" },
    { LANG_UZBEK,       L"UZB" },
    { LANG_VIETNAMESE,  L"VIT" },
};

struct LocalizedUiText {
    const wchar_t* langTag;
    const wchar_t* preferences;
    const wchar_t* shortcutHint;
    const wchar_t* showLanguageBar;
};

static const LocalizedUiText kLocalizedStrings[] = {
    { L"it", L"Preferenze lingua", L"Per passare da una lingua all'altra, premi tasto Windows + Spazio", L"Mostra barra della lingua" },
    { L"en", L"Language preferences", L"To switch, press Windows key + Space", L"Show the Language bar" },
    { L"tr", L"Dil tercihleri", L"Ge\u00E7i\u015F yapmak i\u00E7in Windows tu\u015Fu + Bo\u015Fluk tu\u015Funa bas\u0131n", L"Dil \u00E7ubu\u011Funu g\u00F6ster" },
    { L"fr", L"Pr\u00E9f\u00E9rences linguistiques", L"Pour basculer, appuyez sur la touche Windows + Espace", L"Afficher la barre des langues" },
    { L"es", L"Preferencias de idioma", L"Para cambiar, presione la tecla Windows + Barra espaciadora", L"Mostrar la barra de idioma" },
    { L"pt", L"Prefer\u00EAncias de idioma", L"Para alternar, pressione a tecla Windows + Espa\u00E7o", L"Mostrar a barra de idiomas" },
    { L"zh", L"\u8BED\u8A00\u9996\u9009\u9879", L"\u82E5\u8981\u5207\u6362\uFF0C\u8BF7\u6309 Windows \u5FBD\u6807\u952E + \u7A7A\u683C\u952E", L"\u663E\u793A\u8BED\u8A00\u680F" },
    { L"pl", L"Preferencje j\u0119zykowe", L"Aby prze\u0142\u0105czy\u0107, naci\u015Bnij klawisz Windows + Spacja", L"Poka\u017C pasek j\u0119zyka" },
    { L"nl", L"Taalvoorkeuren", L"Druk op Windows-toets + Spatiebalk om te wisselen", L"Taalbalk weergeven" },
    { L"de", L"Spracheinstellungen", L"Dr\u00FCcken Sie Windows-Taste + Leertaste, um zu wechseln", L"Sprachenleiste anzeigen" },
    { L"ru", L"\u041D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0438 \u044F\u0437\u044B\u043A\u0430", L"\u0414\u043B\u044F \u043F\u0435\u0440\u0435\u043A\u043B\u044E\u0447\u0435\u043D\u0438\u044F \u043D\u0430\u0436\u043C\u0438\u0442\u0435 \u043A\u043B\u0430\u0432\u0438\u0448\u0443 Windows + \u041F\u0440\u043E\u0431\u0435\u043B", L"\u041E\u0442\u043E\u0431\u0440\u0430\u0437\u0438\u0442\u044C \u044F\u0437\u044B\u043A\u043E\u0432\u0443\u044E \u043F\u0430\u043D\u0435\u043B\u044C" },
    { L"ja", L"\u8A00\u8A9E\u306E\u8A2D\u5B9A", L"\u5207\u308A\u66FF\u3048\u308B\u306B\u306F\u3001Windows \u30ED\u30B4 \u30AD\u30FC + Space \u30AD\u30FC\u3092\u62BC\u3057\u307E\u3059", L"\u8A00\u8A9E\u30D0\u30FC\u3092\u8868\u793A" },
    { L"ko", L"\uC5B8\uC5B4 \uAE30\uBCF8 \uC124\uC815", L"\uC804\uD658\uD558\uB824\uBA74 Windows \uD0A4 + \uC2A4\uD398\uC774\uC2A4\uBC14\uB97C \uB204\uB974\uC138\uC694", L"\uC5B8\uC5B4 \uD45C\uC2DC\uC904 \uD45C\uC2DC" },
    { L"ar", L"\u062A\u0641\u0636\u064A\u0644\u0627\u062A \u0627\u0644\u0644\u063A\u0629", L"\u0644\u0644\u062A\u0628\u062F\u064A\u0644\u060C \u0627\u0636\u063A\u0637 \u0639\u0644\u0649 \u0645\u0641\u062A\u0627\u062D Windows + \u0627\u0644\u0645\u0633\u0627\u0641\u0629", L"\u0625\u0638\u0647\u0627\u0631 \u0634\u0631\u064A\u0637 \u0627\u0644\u0644\u063A\u0629" },
    { L"sv", L"Spr\u00E5kinst\u00E4llningar", L"Tryck p\u00E5 Windows-tangenten + Blanksteg f\u00F6r att v\u00E4xla", L"Visa spr\u00E5kf\u00E4ltet" },
    { L"cs", L"Jazykov\u00E9 p\u0159edvolby", L"Chcete-li p\u0159epnout, stiskn\u011Bte kl\u00E1vesu Windows + Mezern\u00EDk", L"Zobrazit panel jazyk\u016F" },
    { L"da", L"Sprogindstillinger", L"Tryk p\u00E5 Windows-tasten + Mellemrum for at skifte", L"Vis proceslinjen Sprog" },
    { L"fi", L"Kieliasetukset", L"Vaihda painamalla Windows-n\u00E4pp\u00E4int\u00E4 + v\u00E4lily\u00F6nti\u00E4", L"N\u00E4yt\u00E4 kielipalkki" },
    { L"el", L"\u03A0\u03C1\u03BF\u03C4\u03B9\u03BC\u03AE\u03C3\u03B5\u03B9\u03C2 \u03B3\u03BB\u03CE\u03C3\u03C3\u03B1\u03C2", L"\u0393\u03B9\u03B1 \u03B5\u03BD\u03B1\u03BB\u03BB\u03B1\u03B3\u03AE, \u03C0\u03B1\u03C4\u03AE\u03C3\u03C4\u03B5 \u03C4\u03BF \u03C0\u03BB\u03AE\u03BA\u03C4\u03C1\u03BF Windows + \u0394\u03B9\u03AC\u03C3\u03C4\u03B7\u03BC\u03B1", L"\u0395\u03BC\u03C6\u03AC\u03BD\u03B9\u03C3\u03B7 \u03C4\u03B7\u03C2 \u03B3\u03C1\u03B1\u03BC\u03BC\u03AE\u03C2 \u03B3\u03BB\u03CE\u03C3\u03C3\u03B1\u03C2" },
    { L"he", L"\u05D4\u05E2\u05D3\u05E4\u05D5\u05EA \u05E9\u05E4\u05D4", L"\u05DB\u05D3\u05D9 \u05DC\u05E2\u05D1\u05D5\u05E8, \u05DC\u05D7\u05E5 \u05E2\u05DC \u05DE\u05E7\u05E9 Windows + \u05E8\u05D5\u05D5\u05D7", L"\u05D4\u05E6\u05D2 \u05D0\u05EA \u05E1\u05E8\u05D2\u05DC \u05D4\u05E9\u05E4\u05D4" },
    { L"hu", L"Nyelvi be\u00E1ll\u00EDt\u00E1sok", L"A v\u00E1lt\u00E1shoz nyomja le a Windows billenty\u0171 + Sz\u00F3k\u00F6z billenty\u0171t", L"Nyelvi s\u00E1v megjelen\u00EDt\u00E9se" },
    { L"nb", L"Spr\u00E5kinnstillinger", L"Trykk p\u00E5 Windows-tasten + Mellomrom for \u00E5 bytte", L"Vis spr\u00E5klinjen" },
    { L"ro", L"Preferin\u021Be de limb\u0103", L"Pentru a comuta, ap\u0103sa\u021Bi tasta Windows + Spa\u021Biu", L"Afi\u0219are bar\u0103 de limb\u0103" },
    { L"sk", L"Jazykov\u00E9 predvo\u013Eby", L"Ak chcete prepn\u00FA\u0165, stla\u010Dte kl\u00E1ves s logom Windows + Medzern\u00EDk", L"Zobrazi\u0165 panel jazykov" },
    { L"uk", L"\u041C\u043E\u0432\u043D\u0456 \u043F\u0430\u0440\u0430\u043C\u0435\u0442\u0440\u0438", L"\u0429\u043E\u0431 \u043F\u0435\u0440\u0435\u043A\u043B\u044E\u0447\u0438\u0442\u0438, \u043D\u0430\u0442\u0438\u0441\u043D\u0456\u0442\u044C \u043A\u043B\u0430\u0432\u0456\u0448\u0443 Windows + \u041F\u0440\u043E\u0431\u0456\u043B", L"\u0412\u0456\u0434\u043E\u0431\u0440\u0430\u0437\u0438\u0442\u0438 \u043C\u043E\u0432\u043D\u0443 \u043F\u0430\u043D\u0435\u043B\u044C" },
    { L"af", L"Taalvoorkeure", L"Vir maklike wisseling, druk Windows-sleutel + Spasie", L"Wys die taalbalk" }
};

/* The popup footer language is the APP's (the single list of the
 * taskbar), not Windows': it is the only intentional difference of the
 * port. */
static void GetLocalizedFooterStrings(std::wstring& outPreferences,
                                      std::wstring& outHint,
                                      std::wstring* outShowBar) {
    wchar_t tag[8] = {};
    switch (CurrentLanguage()) {
        case Lang::It: wcscpy_s(tag, L"it"); break;
        case Lang::Es: wcscpy_s(tag, L"es"); break;
        case Lang::Fr: wcscpy_s(tag, L"fr"); break;
        case Lang::De: wcscpy_s(tag, L"de"); break;
        case Lang::Pt: wcscpy_s(tag, L"pt"); break;
        case Lang::Pl: wcscpy_s(tag, L"pl"); break;
        case Lang::Ru: wcscpy_s(tag, L"ru"); break;
        case Lang::Ja: wcscpy_s(tag, L"ja"); break;
        case Lang::Zh: wcscpy_s(tag, L"zh"); break;
        case Lang::Ar: wcscpy_s(tag, L"ar"); break;
        default:       wcscpy_s(tag, L"en"); break;
    }

    for (const LocalizedUiText& item : kLocalizedStrings) {
        if (item.langTag && wcscmp(tag, item.langTag) == 0) {
            outPreferences = item.preferences;
            outHint = item.shortcutHint;
            if (outShowBar) *outShowBar = item.showLanguageBar;
            return;
        }
    }
    outPreferences = L"Language preferences";
    outHint = L"To switch, press Windows key + Space";
    if (outShowBar) *outShowBar = L"Show the Language bar";
}

/* ------------------------------------------------------------------ */
/*  Helpers (dpi, themes) - from the mod                               */
/* ------------------------------------------------------------------ */

static int ScaleForDpi(int value, UINT dpi) {
    if (dpi == 0) dpi = 96;
    return MulDiv(value, static_cast<int>(dpi), 96);
}

static UINT GetMonitorDpi(HMONITOR hMon) {
    if (hMon) {
        HMODULE hShcore = LoadLibraryW(L"shcore.dll");
        if (hShcore) {
            using GetDpiForMonitor_t = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
            auto pGetDpiForMonitor = reinterpret_cast<GetDpiForMonitor_t>(
                GetProcAddress(hShcore, "GetDpiForMonitor"));
            if (pGetDpiForMonitor) {
                UINT dpiX = 0, dpiY = 0;
                if (SUCCEEDED(pGetDpiForMonitor(hMon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) &&
                    dpiY > 0) {
                    FreeLibrary(hShcore);
                    return dpiY;
                }
            }
            FreeLibrary(hShcore);
        }
    }
    HDC dc = GetDC(nullptr);
    if (dc) {
        int dpi = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(nullptr, dc);
        if (dpi > 0) return static_cast<UINT>(dpi);
    }
    return 96;
}

static UINT GetWindowDpi(HWND hwnd) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        auto pGetDpi = reinterpret_cast<UINT (WINAPI*)(HWND)>(
            reinterpret_cast<void*>(GetProcAddress(hUser, "GetDpiForWindow")));
        if (pGetDpi && hwnd && IsWindow(hwnd)) {
            UINT dpi = pGetDpi(hwnd);
            if (dpi > 0) return dpi;
        }
    }
    HMONITOR hMon = (hwnd && IsWindow(hwnd))
                        ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                        : nullptr;
    return GetMonitorDpi(hMon);
}

static COLORREF GetSystemAccentColor() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    }
    return RGB(91, 44, 130);
}

static bool IsDarkModeActive() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD appsUseLightTheme = 1;
        DWORD size = sizeof(appsUseLightTheme);
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&appsUseLightTheme), &size);
        RegCloseKey(hKey);
        return appsUseLightTheme == 0;
    }
    return false;
}

static HWND AtomicLoadHwnd(const std::atomic<HWND>& a) {
    return a.load(std::memory_order_acquire);
}

/* The window is OUR OWN taskbar (or one of its children): the layout
 * deve finire su di noi. Nel mod il controllo era Shell_TrayWnd. */
static bool IsOurTaskbarWindow(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return false;
    const HWND root = GetAncestor(hWnd, GA_ROOT);
    const HWND owner = AtomicLoadHwnd(g_hClickedTaskbar);
    return owner != nullptr && root == owner;
}

/* ------------------------------------------------------------------ */
/*  Layout enumeration (port of the mod)                               */
/* ------------------------------------------------------------------ */

static std::wstring GetLangAbbrev(LANGID langId) {
    WORD priLang = PRIMARYLANGID(langId);
    for (const LangAbbrevEntry& item : g_LangAbbrevs) {
        if (item.langId == priLang) {
            return item.abbrev;
        }
    }
    wchar_t abbrBuf[16] = {};
    if (GetLocaleInfoW(langId, LOCALE_SABBREVLANGNAME, abbrBuf,
                       ARRAYSIZE(abbrBuf)) > 0) {
        for (wchar_t* p = abbrBuf; *p; ++p) *p = towupper(*p);
        return abbrBuf;
    }
    return L"ENG";
}

static std::wstring GetLayoutDisplayName(const std::wstring& klid) {
    if (klid.empty()) return L"";
    std::wstring regPath =
        L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\" + klid;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ,
                      &hKey) == ERROR_SUCCESS) {
        wchar_t displayBuf[512] = {};
        DWORD cbData = sizeof(displayBuf);
        if (RegQueryValueExW(hKey, L"Layout Display Name", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(displayBuf),
                             &cbData) == ERROR_SUCCESS && displayBuf[0]) {
            wchar_t resolvedBuf[512] = {};
            if (SUCCEEDED(SHLoadIndirectString(displayBuf, resolvedBuf,
                                               ARRAYSIZE(resolvedBuf),
                                               nullptr)) && resolvedBuf[0]) {
                RegCloseKey(hKey);
                return resolvedBuf;
            }
        }
        cbData = sizeof(displayBuf);
        if (RegQueryValueExW(hKey, L"Layout Text", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(displayBuf),
                             &cbData) == ERROR_SUCCESS && displayBuf[0]) {
            RegCloseKey(hKey);
            return displayBuf;
        }
        RegCloseKey(hKey);
    }
    return L"";
}

static std::wstring GetSubstituteKlid(const std::wstring& klid) {
    if (klid.empty()) return klid;
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Keyboard Layout\\Substitutes", 0,
                      KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t subBuf[64] = {};
        DWORD cb = sizeof(subBuf);
        if (RegQueryValueExW(hKey, klid.c_str(), nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(subBuf),
                             &cb) == ERROR_SUCCESS && subBuf[0]) {
            RegCloseKey(hKey);
            return subBuf;
        }
        RegCloseKey(hKey);
    }
    return klid;
}

/* Layouts with a hardware "Layout Id" in the high word of the HKL (0xFxxx):
 * the real KLID is found by walking HKLM\...\Keyboard Layouts. */
static std::wstring FindKlidByLayoutId(WORD layoutId) {
    std::wstring result;
    if (layoutId == 0) return result;
    HKEY hRoot = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
                      0, KEY_READ, &hRoot) != ERROR_SUCCESS) {
        return result;
    }
    for (DWORD i = 0; ; ++i) {
        wchar_t subKey[256] = {};
        DWORD subLen = ARRAYSIZE(subKey);
        FILETIME ft = {};
        if (RegEnumKeyExW(hRoot, i, subKey, &subLen, nullptr, nullptr,
                          nullptr, &ft) != ERROR_SUCCESS) {
            break;
        }
        HKEY hSub = nullptr;
        if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
            wchar_t idBuf[16] = {};
            DWORD cb = sizeof(idBuf);
            if (RegQueryValueExW(hSub, L"Layout Id", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(idBuf),
                                 &cb) == ERROR_SUCCESS && idBuf[0]) {
                wchar_t* end = nullptr;
                unsigned long parsed = wcstoul(idBuf, &end, 16);
                if (end != idBuf && (parsed & 0xFFFF) == layoutId) {
                    result = subKey;
                    RegCloseKey(hSub);
                    break;
                }
            }
            RegCloseKey(hSub);
        }
    }
    RegCloseKey(hRoot);
    return result;
}

static bool IsCaseInsensitiveSubstr(const std::wstring& str,
                                    const std::wstring& sub) {
    if (sub.empty()) return true;
    if (sub.length() > str.length()) return false;
    auto it = std::search(str.begin(), str.end(), sub.begin(), sub.end(),
                          [](wchar_t ch1, wchar_t ch2) {
                              return towlower(ch1) == towlower(ch2);
                          });
    return it != str.end();
}

static std::wstring FormatWin7LayoutItemText(const KeyboardLayoutItem& item,
                                             size_t sameLangCount) {
    std::wstring text = item.langAbbrev + L"  " + item.langName;
    if (sameLangCount > 1 && !item.layoutName.empty()) {
        if (!IsCaseInsensitiveSubstr(item.langName, item.layoutName) &&
            !IsCaseInsensitiveSubstr(item.layoutName, item.langName)) {
            text += L" (" + item.layoutName + L")";
        }
    }
    return text;
}

static void RefreshKeyboardLayouts() {
    UINT count = GetKeyboardLayoutList(0, nullptr);
    if (count == 0 || count > 64) return;

    std::vector<HKL> hkls(count);
    UINT fetched = GetKeyboardLayoutList(count, hkls.data());
    if (fetched == 0) return;
    hkls.resize(fetched);

    HWND hFlyout = AtomicLoadHwnd(g_hFlyoutWnd);
    HWND hFore = AtomicLoadHwnd(g_targetWindow);
    if (!hFore || !IsWindow(hFore) || hFore == hFlyout) {
        hFore = GetForegroundWindow();
        if (hFore == hFlyout) hFore = nullptr;
    }

    DWORD dwTid = (hFore ? GetWindowThreadProcessId(hFore, nullptr) : 0);
    HKL activeHkl = (dwTid != 0 ? GetKeyboardLayout(dwTid) : GetKeyboardLayout(0));

    std::vector<KeyboardLayoutItem> newLayouts;
    newLayouts.reserve(hkls.size());
    size_t foundActiveIndex = 0;

    for (size_t i = 0; i < hkls.size(); ++i) {
        HKL hkl = hkls[i];
        if (!hkl) continue;

        KeyboardLayoutItem item;
        item.hkl = hkl;
        item.langId = LOWORD(reinterpret_cast<uintptr_t>(hkl));
        item.isCurrent = (hkl == activeHkl);
        if (item.isCurrent) foundActiveIndex = i;

        /* KLID from the HKL: low word = LANGID, high word = device id.
         * 0xFxxx = "Layout Id" hardware da risolvere nel registro. */
        WORD dev = HIWORD(reinterpret_cast<uintptr_t>(hkl));
        WORD lang = LOWORD(reinterpret_cast<uintptr_t>(hkl));
        if ((dev & 0xF000) == 0xF000) {
            std::wstring klid = FindKlidByLayoutId(dev & 0x0FFF);
            if (klid.empty()) {
                wchar_t buf[16] = {};
                wsprintfW(buf, L"%08X", static_cast<UINT>(lang));
                klid = buf;
            }
            item.klid = klid;
        } else if (dev == 0 || dev == lang) {
            wchar_t klidBuf[16] = {};
            wsprintfW(klidBuf, L"%08X", static_cast<UINT>(lang));
            item.klid = klidBuf;
        } else {
            wchar_t klidBuf[16] = {};
            wsprintfW(klidBuf, L"%04X%04X",
                      static_cast<UINT>(dev), static_cast<UINT>(lang));
            item.klid = klidBuf;
        }

        std::wstring effectiveKlid = GetSubstituteKlid(item.klid);

        wchar_t langNameBuf[256] = {};
        if (GetLocaleInfoW(item.langId, LOCALE_SLOCALIZEDDISPLAYNAME,
                           langNameBuf, ARRAYSIZE(langNameBuf)) > 0) {
            item.langName = langNameBuf;
        } else if (GetLocaleInfoW(item.langId, LOCALE_SENGLISHDISPLAYNAME,
                                  langNameBuf, ARRAYSIZE(langNameBuf)) > 0) {
            item.langName = langNameBuf;
        } else {
            item.langName = L"Language";
        }
        if (!item.langName.empty() && iswlower(item.langName[0])) {
            item.langName[0] = towupper(item.langName[0]);
        }

        item.langAbbrev = GetLangAbbrev(item.langId);

        std::wstring layoutDesc = GetLayoutDisplayName(effectiveKlid);
        if (layoutDesc.empty()) layoutDesc = GetLayoutDisplayName(item.klid);
        if (layoutDesc.empty()) layoutDesc = item.langName;
        item.layoutName = layoutDesc;

        newLayouts.push_back(std::move(item));
    }

    for (size_t i = 0; i < newLayouts.size(); ++i) {
        size_t sameLangCount = 0;
        for (size_t j = 0; j < newLayouts.size(); ++j) {
            if (newLayouts[i].langAbbrev == newLayouts[j].langAbbrev ||
                PRIMARYLANGID(newLayouts[i].langId) ==
                    PRIMARYLANGID(newLayouts[j].langId)) {
                sameLangCount++;
            }
        }
        newLayouts[i].sameLangCount = sameLangCount;

        if (sameLangCount > 1) {
            /* Different layouts for the same language: an extra
             * (US/UK/INTL/DV, otherwise the initials). */
            std::wstring upperLayout = newLayouts[i].layoutName;
            for (auto& c : upperLayout) c = towupper(c);

            if (upperLayout.find(L"INTERNATIONAL") != std::wstring::npos ||
                upperLayout.find(L"INTL") != std::wstring::npos) {
                newLayouts[i].subAbbrev = L"INTL";
            } else if (upperLayout.find(L"UNITED STATES") != std::wstring::npos ||
                       upperLayout.find(L"US") != std::wstring::npos) {
                newLayouts[i].subAbbrev = L"US";
            } else if (upperLayout.find(L"DVORAK") != std::wstring::npos) {
                newLayouts[i].subAbbrev = L"DV";
            } else if (upperLayout.find(L"UNITED KINGDOM") != std::wstring::npos ||
                       upperLayout.find(L"UK") != std::wstring::npos) {
                newLayouts[i].subAbbrev = L"UK";
            } else {
                std::wstring tag;
                bool newWord = true;
                for (wchar_t ch : newLayouts[i].layoutName) {
                    if (iswalpha(ch)) {
                        if (newWord && tag.size() < 4) {
                            tag.push_back(towupper(ch));
                            newWord = false;
                        }
                    } else {
                        newWord = true;
                    }
                }
                newLayouts[i].subAbbrev = tag.empty() ? L"1" : tag;
            }
        }
    }

    EnterCriticalSection(&g_cs);
    g_layouts = std::move(newLayouts);
    g_selectedIndex = (foundActiveIndex < g_layouts.size()) ? foundActiveIndex : 0;
    LeaveCriticalSection(&g_cs);
}

/* The layout-switch target: the window that had keyboard focus
 * before the taskbar click (in the port the caller may pass it too). */
static HWND FindSwitchTarget() {
    HWND hFlyout = AtomicLoadHwnd(g_hFlyoutWnd);
    HWND hTarget = AtomicLoadHwnd(g_targetWindow);

    if (!hTarget || !IsWindow(hTarget) || hTarget == hFlyout ||
        IsOurTaskbarWindow(hTarget)) {
        hTarget = GetForegroundWindow();
        if (!hTarget || IsOurTaskbarWindow(hTarget) || hTarget == hFlyout) {
            hTarget = GetActiveWindow();
            if (!hTarget || IsOurTaskbarWindow(hTarget) || hTarget == hFlyout) {
                GUITHREADINFO gti = { sizeof(GUITHREADINFO) };
                if (GetGUIThreadInfo(0, &gti)) {
                    if (gti.hwndActive && !IsOurTaskbarWindow(gti.hwndActive) &&
                        gti.hwndActive != hFlyout) {
                        hTarget = gti.hwndActive;
                    } else if (gti.hwndFocus && !IsOurTaskbarWindow(gti.hwndFocus) &&
                               gti.hwndFocus != hFlyout) {
                        hTarget = gti.hwndFocus;
                    }
                }
            }
        }
        if (hTarget && !IsOurTaskbarWindow(hTarget) && hTarget != hFlyout) {
            g_targetWindow.store(hTarget, std::memory_order_release);
        } else {
            hTarget = nullptr;
        }
    }
    return hTarget;
}

static void SwitchToLayout(size_t index) {
    HKL targetHkl = nullptr;
    HWND hTarget = nullptr;

    EnterCriticalSection(&g_cs);
    if (index >= g_layouts.size()) {
        LeaveCriticalSection(&g_cs);
        return;
    }
    targetHkl = g_layouts[index].hkl;
    g_selectedIndex = index;
    for (size_t i = 0; i < g_layouts.size(); ++i) {
        g_layouts[i].isCurrent = (i == index);
    }
    LeaveCriticalSection(&g_cs);

    if (!targetHkl) return;

    hTarget = FindSwitchTarget();
    if (!hTarget) {
        LogTagged(L"LANGSW", L"no target window for the layout switch");
        return;
    }

    const DWORD dwTargetThreadId = GetWindowThreadProcessId(hTarget, nullptr);
    if (hTarget && IsWindow(hTarget) && dwTargetThreadId != 0) {
        HKL currentLayout = GetKeyboardLayout(dwTargetThreadId);
        if (currentLayout != targetHkl) {
            ActivateKeyboardLayout(targetHkl, KLF_SETFORPROCESS);
            if (GetKeyboardLayout(dwTargetThreadId) != targetHkl) {
                /* Per-message fallback, then the reset flag: same
                 * chain of the mod. */
                if (!PostMessageW(hTarget, WM_INPUTLANGCHANGEREQUEST, 0,
                                  reinterpret_cast<LPARAM>(targetHkl))) {
                    SendMessageW(hTarget, WM_INPUTLANGCHANGEREQUEST, 0,
                                 reinterpret_cast<LPARAM>(targetHkl));
                }
                if (GetKeyboardLayout(dwTargetThreadId) != targetHkl) {
                    ActivateKeyboardLayout(targetHkl,
                                           KLF_SETFORPROCESS | KLF_RESET);
                }
            }
            LogTagged(L"LANGSW",
                      L"layout switched (external window updated)");
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Active language (for the tray text)                                */
/* ------------------------------------------------------------------ */

static WORD ActiveLangIdWord() {
    GUITHREADINFO info = {};
    info.cbSize = sizeof(info);
    DWORD tid = 0;
    if (GetGUIThreadInfo(0, &info) && info.hwndActive != nullptr) {
        tid = GetWindowThreadProcessId(info.hwndActive, nullptr);
    }
    HKL hkl = GetKeyboardLayout(tid);
    return LOWORD(reinterpret_cast<uintptr_t>(hkl));
}

static void LettersForLangId(WORD langId, WCHAR* two, size_t twoCap) {
    two[0] = L'\0';
    if (GetLocaleInfoW(MAKELCID(langId, SORT_DEFAULT),
                       LOCALE_SISO639LANGNAME, two, (int)twoCap) == 0) {
        StringCchCopyW(two, twoCap, L"--");
    }
    CharUpperBuffW(two, (DWORD)wcslen(two));
}

/* ------------------------------------------------------------------ */
/*  Rendering (port of the two mod styles)                             */
/* ------------------------------------------------------------------ */

static void DrawWin7GdiFallbackCheck(HDC hdc, const RECT& gutter,
                                     COLORREF color, UINT dpi) {
    if (!hdc) return;
    int thickness = ScaleForDpi(2, dpi);
    if (thickness < 2) thickness = 2;
    LOGBRUSH lb = {};
    lb.lbStyle = BS_SOLID;
    lb.lbColor = color;
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND |
                            PS_JOIN_MITER, thickness, &lb, 0, nullptr);
    if (!pen) return;
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    const int cx = (gutter.left + gutter.right) / 2;
    const int cy = (gutter.top + gutter.bottom) / 2 + ScaleForDpi(1, dpi);
    MoveToEx(hdc, cx - ScaleForDpi(5, dpi), cy, nullptr);
    LineTo(hdc, cx - ScaleForDpi(1, dpi), cy + ScaleForDpi(4, dpi));
    LineTo(hdc, cx + ScaleForDpi(6, dpi), cy - ScaleForDpi(5, dpi));
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawWin7MenuCheckmark(HDC hdc, const RECT& gutter,
                                  COLORREF color, UINT dpi) {
    if (!hdc) return;

    if (!g_hGdiPlus || !pGdipCreateFromHDC || !pGdipFillPath) {
        DrawWin7GdiFallbackCheck(hdc, gutter, color, dpi);
        return;
    }

    const BYTE r = GetRValue(color);
    const BYTE g = GetGValue(color);
    const BYTE b = GetBValue(color);

    void* graphics = nullptr;
    if (pGdipCreateFromHDC(hdc, &graphics) != 0 || !graphics) {
        DrawWin7GdiFallbackCheck(hdc, gutter, color, dpi);
        return;
    }

    pGdipSetSmoothingMode(graphics, 2);
    pGdipSetPixelOffsetMode(graphics, 2);

    const float boxL = static_cast<float>(gutter.left);
    const float boxT = static_cast<float>(gutter.top);
    const float boxW = static_cast<float>(gutter.right - gutter.left);
    const float boxH = static_cast<float>(gutter.bottom - gutter.top);

    float size = boxW * 0.70f;
    if (size > boxH * 0.50f) size = boxH * 0.50f;
    if (size < 8.0f) size = (boxH < boxW ? boxH : boxW) * 0.48f;

    const float originX = boxL + (boxW - size) * 0.42f;
    const float originY = boxT + (boxH - size) * 0.54f;

    struct PointF { float X; float Y; };
    const PointF pts[] = {
        { originX + size * 0.06f, originY + size * 0.50f },
        { originX + size * 0.18f, originY + size * 0.38f },
        { originX + size * 0.38f, originY + size * 0.62f },
        { originX + size * 0.82f, originY + size * 0.08f },
        { originX + size * 0.96f, originY + size * 0.20f },
        { originX + size * 0.38f, originY + size * 0.90f },
    };

    void* path = nullptr;
    if (pGdipCreatePath(0, &path) == 0 && path) {
        pGdipAddPathPolygon(path, pts, 6);
        DWORD argb = (255 << 24) | (r << 16) | (g << 8) | b;
        void* brush = nullptr;
        if (pGdipCreateSolidFill(argb, &brush) == 0 && brush) {
            pGdipFillPath(graphics, brush, path);
            pGdipDeleteBrush(brush);
        }
        void* pen = nullptr;
        float outlineW = (dpi >= 144) ? 0.90f * (static_cast<float>(dpi) / 96.0f)
                                      : 0.70f;
        if (pGdipCreatePen1(argb, outlineW, 2, &pen) == 0 && pen) {
            pGdipSetPenLineJoin(pen, 2);
            pGdipDrawPath(graphics, pen, path);
            pGdipDeletePen(pen);
        }
        pGdipDeletePath(path);
    }

    pGdipDeleteGraphics(graphics);
}

static HFONT CreateMenuFont(int sizePx, int weight, UINT dpi, bool underline) {
    return CreateFontW(ScaleForDpi(sizePx, dpi), 0, 0, 0, weight, FALSE,
                       underline ? TRUE : FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Segoe UI");
}

/* The classic Windows 7 menu. */
static void PaintWin7Menu(HWND hwnd, HDC hdc) {
    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) return;

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) return;

    UINT dpi = GetWindowDpi(hwnd);
    const int itemHeight = ScaleForDpi(26, dpi);
    const int paddingLeft = ScaleForDpi(28, dpi);
    const int paddingRight = ScaleForDpi(16, dpi);

    HDC memDC = CreateCompatibleDC(hdc);
    if (!memDC) return;
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    if (!memBmp) { DeleteDC(memDC); return; }
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
    SetBkMode(memDC, TRANSPARENT);

    const bool isDark = IsDarkModeActive();
    const COLORREF colBg = isDark ? RGB(36, 36, 36) : RGB(242, 242, 242);
    const COLORREF colTextNormal = isDark ? RGB(245, 245, 245) : RGB(0, 0, 0);
    const COLORREF colHoverBg = isDark ? RGB(56, 56, 56) : RGB(185, 215, 251);
    const COLORREF colHoverBorder = isDark ? RGB(80, 80, 80) : RGB(125, 162, 206);
    const COLORREF colBorder = isDark ? RGB(70, 70, 70) : RGB(160, 160, 160);
    const COLORREF colSeparator = isDark ? RGB(55, 55, 55) : RGB(210, 210, 210);

    HBRUSH bgBrush = CreateSolidBrush(colBg);
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    HFONT fontMenu = CreateMenuFont(13, FW_NORMAL, dpi, false);

    std::vector<KeyboardLayoutItem> layoutsCopy;
    size_t activeIdx = 0;
    int hovIdx = -1;
    EnterCriticalSection(&g_cs);
    layoutsCopy = g_layouts;
    activeIdx = g_selectedIndex;
    hovIdx = g_hoveredWin7Index;
    LeaveCriticalSection(&g_cs);

    int currentY = ScaleForDpi(3, dpi);

    for (size_t i = 0; i < layoutsCopy.size(); ++i) {
        RECT itemRect{ScaleForDpi(2, dpi), currentY,
                      width - ScaleForDpi(2, dpi), currentY + itemHeight};
        const bool isHovered = (static_cast<int>(i) == hovIdx);
        const bool isActive = (i == activeIdx);

        if (isHovered) {
            HBRUSH hovBrush = CreateSolidBrush(colHoverBg);
            FillRect(memDC, &itemRect, hovBrush);
            DeleteObject(hovBrush);

            HPEN hovPen = CreatePen(PS_SOLID, 1, colHoverBorder);
            HGDIOBJ oldPen = SelectObject(memDC, hovPen);
            HGDIOBJ oldNull = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, itemRect.left, itemRect.top,
                      itemRect.right, itemRect.bottom);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldNull);
            DeleteObject(hovPen);
        }

        if (isActive && g_style == SwitcherStyle::Win7) {
            RECT checkRect{ScaleForDpi(4, dpi), currentY, paddingLeft,
                           currentY + itemHeight};
            DrawWin7MenuCheckmark(memDC, checkRect, colTextNormal, dpi);
        }

        std::wstring itemText =
            FormatWin7LayoutItemText(layoutsCopy[i], layoutsCopy[i].sameLangCount);

        HGDIOBJ oldF = SelectObject(memDC, fontMenu);
        SetTextColor(memDC, colTextNormal);
        RECT textRect{paddingLeft, currentY, width - paddingRight,
                      currentY + itemHeight};
        DrawTextW(memDC, itemText.c_str(), -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
        SelectObject(memDC, oldF);

        currentY += itemHeight;
    }

    currentY += ScaleForDpi(2, dpi);
    RECT sepRect{ScaleForDpi(6, dpi), currentY + ScaleForDpi(3, dpi),
                 width - ScaleForDpi(6, dpi), currentY + ScaleForDpi(4, dpi)};
    HBRUSH sepBrush = CreateSolidBrush(colSeparator);
    FillRect(memDC, &sepRect, sepBrush);
    DeleteObject(sepBrush);
    currentY += ScaleForDpi(7, dpi);

    std::wstring prefStr, hintStr, showBarStr;
    GetLocalizedFooterStrings(prefStr, hintStr, &showBarStr);

    const int footer1Index = static_cast<int>(layoutsCopy.size());
    const int footer2Index = footer1Index + 1;

    /* Footer 1: "Show the language bar". */
    {
        RECT itemRect{ScaleForDpi(2, dpi), currentY,
                      width - ScaleForDpi(2, dpi), currentY + itemHeight};
        if (hovIdx == footer1Index) {
            HBRUSH hovBrush = CreateSolidBrush(colHoverBg);
            FillRect(memDC, &itemRect, hovBrush);
            DeleteObject(hovBrush);
            HPEN hovPen = CreatePen(PS_SOLID, 1, colHoverBorder);
            HGDIOBJ oldPen = SelectObject(memDC, hovPen);
            HGDIOBJ oldNull = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, itemRect.left, itemRect.top,
                      itemRect.right, itemRect.bottom);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldNull);
            DeleteObject(hovPen);
        }
        HGDIOBJ oldF = SelectObject(memDC, fontMenu);
        SetTextColor(memDC, colTextNormal);
        RECT textRect{paddingLeft, currentY, width - paddingRight,
                      currentY + itemHeight};
        DrawTextW(memDC, showBarStr.c_str(), -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
        currentY += itemHeight;
    }

    /* Footer 2: "Language preferences...". */
    {
        RECT itemRect{ScaleForDpi(2, dpi), currentY,
                      width - ScaleForDpi(2, dpi), currentY + itemHeight};
        if (hovIdx == footer2Index) {
            HBRUSH hovBrush = CreateSolidBrush(colHoverBg);
            FillRect(memDC, &itemRect, hovBrush);
            DeleteObject(hovBrush);
            HPEN hovPen = CreatePen(PS_SOLID, 1, colHoverBorder);
            HGDIOBJ oldPen = SelectObject(memDC, hovPen);
            HGDIOBJ oldNull = SelectObject(memDC, GetStockObject(NULL_BRUSH));
            Rectangle(memDC, itemRect.left, itemRect.top,
                      itemRect.right, itemRect.bottom);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldNull);
            DeleteObject(hovPen);
        }
        HGDIOBJ oldF = SelectObject(memDC, fontMenu);
        SetTextColor(memDC, colTextNormal);
        RECT textRect{paddingLeft, currentY, width - paddingRight,
                      currentY + itemHeight};
        std::wstring prefWithDots = prefStr + L"...";
        DrawTextW(memDC, prefWithDots.c_str(), -1, &textRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, colBorder);
    HGDIOBJ oldPen = SelectObject(memDC, borderPen);
    HGDIOBJ oldNullBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, width, height);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldNullBrush);
    DeleteObject(borderPen);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    DeleteObject(fontMenu);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

/* The modern Windows 8.1 card (same style as the mod). */
static void PaintWin8Flyout(HWND hwnd, HDC hdc) {
    RECT clientRect = {};
    if (!GetClientRect(hwnd, &clientRect)) return;

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) return;

    UINT dpi = GetWindowDpi(hwnd);
    const int itemHeight = ScaleForDpi(58, dpi);
    const int badgeWidth = ScaleForDpi(62, dpi);
    const int paddingX = ScaleForDpi(16, dpi);
    const int separatorHeight = ScaleForDpi(1, dpi);

    HDC memDC = CreateCompatibleDC(hdc);
    if (!memDC) return;
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, width, height);
    if (!memBmp) { DeleteDC(memDC); return; }
    HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
    SetBkMode(memDC, TRANSPARENT);

    const bool isDark = IsDarkModeActive();
    const COLORREF colBg = isDark ? RGB(32, 32, 32) : RGB(242, 242, 242);
    const COLORREF colTextNormal = isDark ? RGB(240, 240, 240) : RGB(0, 0, 0);
    const COLORREF colTextSub = isDark ? RGB(160, 160, 160) : RGB(96, 96, 96);
    const COLORREF colHoverBg = isDark ? RGB(50, 50, 50) : RGB(220, 220, 220);
    const COLORREF colBorder = isDark ? RGB(60, 60, 60) : RGB(190, 190, 190);
    const COLORREF colSeparator = isDark ? RGB(55, 55, 55) : RGB(200, 200, 200);
    const COLORREF colSelectedBg = GetSystemAccentColor();
    const COLORREF colSelectedText = RGB(255, 255, 255);
    const COLORREF colSelectedSubText = RGB(235, 235, 235);
    const COLORREF colLink = isDark ? RGB(100, 160, 255) : RGB(0, 102, 204);
    const COLORREF colTipText = isDark ? RGB(160, 160, 160) : RGB(96, 96, 96);

    HBRUSH bgBrush = CreateSolidBrush(colBg);
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    HFONT fontAbbr = CreateMenuFont(18, FW_BOLD, dpi, false);
    HFONT fontSubAbbr = CreateMenuFont(11, FW_BOLD, dpi, false);
    HFONT fontTitle = CreateMenuFont(14, FW_NORMAL, dpi, false);
    HFONT fontSub = CreateMenuFont(12, FW_NORMAL, dpi, false);
    HFONT fontLink = CreateMenuFont(13, FW_NORMAL, dpi, g_hoveredFooter);
    HFONT fontHint = CreateMenuFont(11, FW_NORMAL, dpi, false);

    std::vector<KeyboardLayoutItem> layoutsCopy;
    size_t selIndex = 0;
    int hovIndex = -1;
    EnterCriticalSection(&g_cs);
    layoutsCopy = g_layouts;
    selIndex = g_selectedIndex;
    hovIndex = g_hoveredIndex;
    LeaveCriticalSection(&g_cs);

    int currentY = 0;
    for (size_t i = 0; i < layoutsCopy.size(); ++i) {
        RECT itemRect{0, currentY, width, currentY + itemHeight};
        const bool isSelected = (i == selIndex);
        const bool isHovered = (static_cast<int>(i) == hovIndex);

        if (isSelected) {
            HBRUSH selBrush = CreateSolidBrush(colSelectedBg);
            FillRect(memDC, &itemRect, selBrush);
            DeleteObject(selBrush);
        } else if (isHovered) {
            HBRUSH hovBrush = CreateSolidBrush(colHoverBg);
            FillRect(memDC, &itemRect, hovBrush);
            DeleteObject(hovBrush);
        }

        const COLORREF itemTextColor = isSelected ? colSelectedText : colTextNormal;
        const COLORREF itemSubTextColor = isSelected ? colSelectedSubText : colTextSub;

        if (layoutsCopy[i].subAbbrev.empty()) {
            HGDIOBJ oldF = SelectObject(memDC, fontAbbr);
            SetTextColor(memDC, itemTextColor);
            RECT abbrRect{paddingX, currentY, badgeWidth + paddingX,
                          currentY + itemHeight};
            DrawTextW(memDC, layoutsCopy[i].langAbbrev.c_str(), -1, &abbrRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, oldF);
        } else {
            HGDIOBJ oldF = SelectObject(memDC, fontAbbr);
            SetTextColor(memDC, itemTextColor);
            RECT abbrRect1{paddingX, currentY + ScaleForDpi(6, dpi),
                           badgeWidth + paddingX, currentY + ScaleForDpi(30, dpi)};
            DrawTextW(memDC, layoutsCopy[i].langAbbrev.c_str(), -1, &abbrRect1,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, fontSubAbbr);
            RECT abbrRect2{paddingX, currentY + ScaleForDpi(28, dpi),
                           badgeWidth + paddingX, currentY + ScaleForDpi(48, dpi)};
            DrawTextW(memDC, layoutsCopy[i].subAbbrev.c_str(), -1, &abbrRect2,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, oldF);
        }

        const int rightX = badgeWidth + paddingX + ScaleForDpi(6, dpi);
        const int textRight = width - paddingX;

        {
            HGDIOBJ oldF = SelectObject(memDC, fontTitle);
            SetTextColor(memDC, itemTextColor);
            RECT textRect1{rightX, currentY + ScaleForDpi(8, dpi), textRight,
                           currentY + ScaleForDpi(30, dpi)};
            DrawTextW(memDC, layoutsCopy[i].langName.c_str(), -1, &textRect1,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
            SelectObject(memDC, oldF);
        }
        {
            HGDIOBJ oldF = SelectObject(memDC, fontSub);
            SetTextColor(memDC, itemSubTextColor);
            RECT textRect2{rightX, currentY + ScaleForDpi(30, dpi), textRight,
                           currentY + ScaleForDpi(50, dpi)};
            DrawTextW(memDC, layoutsCopy[i].layoutName.c_str(), -1, &textRect2,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
            SelectObject(memDC, oldF);
        }

        currentY += itemHeight;
    }

    RECT sepRect{paddingX, currentY + ScaleForDpi(4, dpi),
                 width - paddingX,
                 currentY + ScaleForDpi(4, dpi) + separatorHeight};
    HBRUSH sepBrush = CreateSolidBrush(colSeparator);
    FillRect(memDC, &sepRect, sepBrush);
    DeleteObject(sepBrush);
    currentY += ScaleForDpi(8, dpi);

    std::wstring prefStr, hintStr;
    GetLocalizedFooterStrings(prefStr, hintStr, nullptr);

    {
        HGDIOBJ oldF = SelectObject(memDC, fontLink);
        SetTextColor(memDC, colLink);
        RECT linkRect{paddingX, currentY, width - paddingX,
                      currentY + ScaleForDpi(22, dpi)};
        DrawTextW(memDC, prefStr.c_str(), -1, &linkRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
    }
    {
        HGDIOBJ oldF = SelectObject(memDC, fontHint);
        SetTextColor(memDC, colTipText);
        RECT tipRect{paddingX, currentY + ScaleForDpi(22, dpi),
                     width - paddingX, currentY + ScaleForDpi(44, dpi)};
        DrawTextW(memDC, hintStr.c_str(), -1, &tipRect,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldF);
    }

    HPEN borderPen = CreatePen(PS_SOLID, 1, colBorder);
    HGDIOBJ oldPen = SelectObject(memDC, borderPen);
    HGDIOBJ oldNullBr = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, width, height);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldNullBr);
    DeleteObject(borderPen);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    DeleteObject(fontAbbr);
    DeleteObject(fontSubAbbr);
    DeleteObject(fontTitle);
    DeleteObject(fontSub);
    DeleteObject(fontLink);
    DeleteObject(fontHint);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

static void PaintSwitcher(HWND hwnd, HDC hdc) {
    static thread_local bool s_inPaint = false;
    if (s_inPaint || !hwnd || !hdc) return;
    s_inPaint = true;
    if (g_style == SwitcherStyle::Win7) {
        PaintWin7Menu(hwnd, hdc);
    } else {
        PaintWin8Flyout(hwnd, hdc);
    }
    s_inPaint = false;
}

/* ------------------------------------------------------------------ */
/*  Positioning near the taskbar (multi-screen/multi-edge)             */
/* ------------------------------------------------------------------ */

static void PositionWindowNearTray(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    /* The anchor is OUR OWN taskbar (passed to Show), not Shell_TrayWnd. */
    HWND hAnchor = AtomicLoadHwnd(g_hClickedTaskbar);

    HMONITOR hMon = MonitorFromWindow(hAnchor ? hAnchor : hwnd,
                                      MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    RECT rcWork = {};
    if (hMon && GetMonitorInfoW(hMon, &mi)) {
        rcWork = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    }

    UINT dpi = GetMonitorDpi(hMon);

    int flyoutWidth = 0;
    int totalHeight = 0;

    size_t count = 0;
    std::vector<KeyboardLayoutItem> layoutsCopy;
    EnterCriticalSection(&g_cs);
    count = g_layouts.size();
    layoutsCopy = g_layouts;
    LeaveCriticalSection(&g_cs);

    if (count == 0) return;

    if (g_style == SwitcherStyle::Win7) {
        const int itemHeight = ScaleForDpi(26, dpi);
        const int sepHeight = ScaleForDpi(9, dpi);
        const int footerHeight = itemHeight * 2;
        totalHeight = ScaleForDpi(6, dpi) +
                      static_cast<int>(count) * itemHeight + sepHeight +
                      footerHeight;

        int calculatedWidth = ScaleForDpi(260, dpi);
        HDC dc = GetDC(nullptr);
        if (dc) {
            HFONT fontMenu = CreateMenuFont(13, FW_NORMAL, dpi, false);
            HGDIOBJ oldF = SelectObject(dc, fontMenu);
            for (const KeyboardLayoutItem& item : layoutsCopy) {
                std::wstring itemText =
                    FormatWin7LayoutItemText(item, item.sameLangCount);
                RECT rcCalc = {};
                DrawTextW(dc, itemText.c_str(), -1, &rcCalc,
                          DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
                const int itemW = rcCalc.right - rcCalc.left +
                                  ScaleForDpi(48, dpi);
                if (itemW > calculatedWidth) calculatedWidth = itemW;
            }
            std::wstring prefStr, hintStr, showBarStr;
            GetLocalizedFooterStrings(prefStr, hintStr, &showBarStr);
            RECT rcShowBar = {};
            DrawTextW(dc, showBarStr.c_str(), -1, &rcShowBar,
                      DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
            const int sbW = rcShowBar.right - rcShowBar.left +
                            ScaleForDpi(48, dpi);
            if (sbW > calculatedWidth) calculatedWidth = sbW;

            std::wstring prefWithDots = prefStr + L"...";
            RECT rcPref = {};
            DrawTextW(dc, prefWithDots.c_str(), -1, &rcPref,
                      DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
            const int prW = rcPref.right - rcPref.left + ScaleForDpi(48, dpi);
            if (prW > calculatedWidth) calculatedWidth = prW;

            SelectObject(dc, oldF);
            DeleteObject(fontMenu);
            ReleaseDC(nullptr, dc);
        }
        flyoutWidth = (calculatedWidth > ScaleForDpi(420, dpi))
                          ? ScaleForDpi(420, dpi) : calculatedWidth;
    } else {
        const int itemHeight = ScaleForDpi(58, dpi);
        const int footerHeight = ScaleForDpi(62, dpi);
        totalHeight = static_cast<int>(count) * itemHeight + footerHeight;
        flyoutWidth = ScaleForDpi(330, dpi);
    }

    APPBARDATA abd = { sizeof(APPBARDATA) };
    SHAppBarMessage(ABM_GETTASKBARPOS, &abd);

    RECT rcTaskbar = {};
    UINT edge = abd.uEdge;
    if (hAnchor && GetWindowRect(hAnchor, &rcTaskbar)) {
        const int monW = mi.rcMonitor.right - mi.rcMonitor.left;
        const int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
        const int tbW = rcTaskbar.right - rcTaskbar.left;
        const int tbH = rcTaskbar.bottom - rcTaskbar.top;
        if (tbW >= monW * 0.8 && tbH < monH / 2) {
            edge = (rcTaskbar.top <= mi.rcMonitor.top + 8) ? ABE_TOP : ABE_BOTTOM;
        } else if (tbH >= monH * 0.8 && tbW < monW / 2) {
            edge = (rcTaskbar.left <= mi.rcMonitor.left + 8) ? ABE_LEFT : ABE_RIGHT;
        }
    }

    int x = rcWork.right - flyoutWidth - 8;
    int y = rcWork.bottom - totalHeight - 8;
    if (edge == ABE_TOP) {
        y = (hAnchor ? rcTaskbar.bottom : abd.rc.bottom) + 8;
    } else if (edge == ABE_LEFT) {
        x = (hAnchor ? rcTaskbar.right : abd.rc.right) + 8;
    } else if (edge == ABE_RIGHT) {
        x = (hAnchor ? rcTaskbar.left : abd.rc.left) - flyoutWidth - 8;
    } else if (hAnchor) {
        y = rcTaskbar.top - totalHeight - 8;
    }

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, flyoutWidth, totalHeight,
                 SWP_SHOWWINDOW);
}

/* ------------------------------------------------------------------ */
/*  The language timer: refreshes the tray and fires the callback       */
/* ------------------------------------------------------------------ */

static void OnLangWatchTick(HWND hwnd) {
    const WORD langId = ActiveLangIdWord();
    if (langId != 0 && langId != g_lastNotifiedLangId) {
        g_lastNotifiedLangId = langId;
        LangChangedCallback cb = g_callback;
        if (cb != nullptr) {
            /* The managed-layer callback expects to run on the
             * UI thread: the timer lives on that thread. */
            cb(langId);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/*  Popup window procedure (port)                                      */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK FlyoutWndProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                      LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            InitGdiPlusRendering();
            SetTimer(hwnd, kLangWatchTimerId, kLangWatchTimerMs, nullptr);
            g_lastNotifiedLangId = ActiveLangIdWord();
            return 0;
        }

        case WM_TIMER:
            if (wParam == kLangWatchTimerId) {
                OnLangWatchTick(hwnd);
                return 0;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc) {
                PaintSwitcher(hwnd, hdc);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            UINT dpi = GetWindowDpi(hwnd);

            size_t count = 0;
            EnterCriticalSection(&g_cs);
            count = g_layouts.size();
            LeaveCriticalSection(&g_cs);

            bool needsRepaint = false;

            if (g_style == SwitcherStyle::Win7) {
                const int itemHeight = ScaleForDpi(26, dpi);
                const int sepHeight = ScaleForDpi(9, dpi);
                int newHov = -1;

                if (y >= ScaleForDpi(3, dpi) &&
                    y < ScaleForDpi(3, dpi) + static_cast<int>(count) * itemHeight) {
                    newHov = (y - ScaleForDpi(3, dpi)) / itemHeight;
                } else if (y >= ScaleForDpi(3, dpi) +
                                  static_cast<int>(count) * itemHeight + sepHeight) {
                    const int footerY = y - (ScaleForDpi(3, dpi) +
                        static_cast<int>(count) * itemHeight + sepHeight);
                    const int fIndex = footerY / itemHeight;
                    if (fIndex == 0) newHov = static_cast<int>(count);
                    else if (fIndex == 1) newHov = static_cast<int>(count) + 1;
                }

                EnterCriticalSection(&g_cs);
                if (newHov != g_hoveredWin7Index) {
                    g_hoveredWin7Index = newHov;
                    needsRepaint = true;
                }
                LeaveCriticalSection(&g_cs);
            } else {
                const int itemHeight = ScaleForDpi(58, dpi);
                const int paddingX = ScaleForDpi(16, dpi);
                int newHoveredIndex = -1;
                bool newHoveredFooter = false;

                if (itemHeight > 0 && y >= 0 &&
                    y < static_cast<int>(count) * itemHeight) {
                    newHoveredIndex = y / itemHeight;
                    if (newHoveredIndex >= static_cast<int>(count)) {
                        newHoveredIndex = -1;
                    }
                } else if (y >= static_cast<int>(count) * itemHeight) {
                    const int linkY = static_cast<int>(count) * itemHeight +
                                      ScaleForDpi(8, dpi);
                    if (y >= linkY && y <= linkY + ScaleForDpi(24, dpi) &&
                        x >= paddingX && x <= ScaleForDpi(250, dpi)) {
                        newHoveredFooter = true;
                    }
                }

                EnterCriticalSection(&g_cs);
                if (newHoveredIndex != g_hoveredIndex ||
                    newHoveredFooter != g_hoveredFooter) {
                    g_hoveredIndex = newHoveredIndex;
                    g_hoveredFooter = newHoveredFooter;
                    needsRepaint = true;
                }
                LeaveCriticalSection(&g_cs);
            }

            if (needsRepaint && IsWindow(hwnd)) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE:
            EnterCriticalSection(&g_cs);
            g_hoveredIndex = -1;
            g_hoveredFooter = false;
            g_hoveredWin7Index = -1;
            LeaveCriticalSection(&g_cs);
            if (IsWindow(hwnd)) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_SETCURSOR: {
            bool isFooter = false;
            EnterCriticalSection(&g_cs);
            isFooter = g_hoveredFooter;
            LeaveCriticalSection(&g_cs);

            if (isFooter && g_style != SwitcherStyle::Win7) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }

        case WM_LBUTTONUP: {
            const int y = GET_Y_LPARAM(lParam);
            const int x = GET_X_LPARAM(lParam);
            UINT dpi = GetWindowDpi(hwnd);

            size_t count = 0;
            EnterCriticalSection(&g_cs);
            count = g_layouts.size();
            LeaveCriticalSection(&g_cs);

            if (g_style == SwitcherStyle::Win7) {
                const int itemHeight = ScaleForDpi(26, dpi);
                const int sepHeight = ScaleForDpi(9, dpi);

                if (y >= ScaleForDpi(3, dpi) &&
                    y < ScaleForDpi(3, dpi) + static_cast<int>(count) * itemHeight) {
                    const size_t clickedIndex = static_cast<size_t>(
                        (y - ScaleForDpi(3, dpi)) / itemHeight);
                    if (clickedIndex < count) {
                        SwitchToLayout(clickedIndex);
                    }
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                } else if (y >= ScaleForDpi(3, dpi) +
                                  static_cast<int>(count) * itemHeight + sepHeight) {
                    const int footerY = y - (ScaleForDpi(3, dpi) +
                        static_cast<int>(count) * itemHeight + sepHeight);
                    const int fIndex = footerY / itemHeight;
                    ShowWindow(hwnd, SW_HIDE);
                    /* As in the mod: the first footer opens the system
                     * Control Panel, the second the modern one; "Show the
                     * language bar" lives on as an informational entry of
                     * the recreation. */
                    if (fIndex == 0) {
                        ShellExecuteW(nullptr, L"open", L"control.exe",
                                      L"/name Microsoft.Language", nullptr,
                                      SW_SHOWNORMAL);
                    } else if (fIndex == 1) {
                        ShellExecuteW(nullptr, L"open",
                                      L"ms-settings:regionlanguage", nullptr,
                                      nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
            } else {
                const int itemHeight = ScaleForDpi(58, dpi);
                const int paddingX = ScaleForDpi(16, dpi);

                if (itemHeight > 0 && y >= 0 &&
                    y < static_cast<int>(count) * itemHeight) {
                    const size_t clickedIndex = static_cast<size_t>(y / itemHeight);
                    if (clickedIndex < count) {
                        SwitchToLayout(clickedIndex);
                    }
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                } else if (y >= static_cast<int>(count) * itemHeight) {
                    const int linkY = static_cast<int>(count) * itemHeight +
                                      ScaleForDpi(8, dpi);
                    if (y >= linkY && y <= linkY + ScaleForDpi(26, dpi) &&
                        x >= paddingX && x <= ScaleForDpi(250, dpi)) {
                        ShowWindow(hwnd, SW_HIDE);
                        ShellExecuteW(nullptr, L"open",
                                      L"ms-settings:regionlanguage", nullptr,
                                      nullptr, SW_SHOWNORMAL);
                        return 0;
                    }
                }
            }
            break;
        }

        case WM_KEYDOWN: {
            size_t count = 0;
            EnterCriticalSection(&g_cs);
            count = g_layouts.size();
            LeaveCriticalSection(&g_cs);

            if (count == 0) break;

            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            } else if (wParam == VK_DOWN || wParam == VK_TAB) {
                EnterCriticalSection(&g_cs);
                g_selectedIndex = (g_selectedIndex + 1) % count;
                LeaveCriticalSection(&g_cs);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if (wParam == VK_UP) {
                EnterCriticalSection(&g_cs);
                g_selectedIndex = (g_selectedIndex + count - 1) % count;
                LeaveCriticalSection(&g_cs);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            } else if (wParam == VK_RETURN || wParam == VK_SPACE) {
                size_t sel = 0;
                EnterCriticalSection(&g_cs);
                sel = g_selectedIndex;
                LeaveCriticalSection(&g_cs);
                SwitchToLayout(sel);
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                ShowWindow(hwnd, SW_HIDE);
            }
            break;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kLangWatchTimerId);
            g_hFlyoutWnd.store(nullptr, std::memory_order_release);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  Public entry points                                                */
/* ------------------------------------------------------------------ */

HINSTANCE ThisModule() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisModule), &self);
    return reinterpret_cast<HINSTANCE>(self);
}

} // namespace

void Show(uint64_t ownerHwnd, uint64_t foregroundHwnd, int styleMode) {
    HWND owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd));
    if (owner == nullptr || !IsWindow(owner)) {
        return;
    }

    g_styleMode = styleMode;
    g_style = (styleMode == 1) ? SwitcherStyle::Win7 : SwitcherStyle::Win8;

    /* The window that had keyboard focus BEFORE the click: it is the one
     * that receives the layout switch (the taskbar click took it away).
     * If the caller did not tell us, we discover it ourselves. */
    HWND fg = reinterpret_cast<HWND>(static_cast<uintptr_t>(foregroundHwnd));
    if (fg != nullptr && IsWindow(fg) && !IsOurTaskbarWindow(fg) &&
        fg != AtomicLoadHwnd(g_hFlyoutWnd)) {
        g_targetWindow.store(fg, std::memory_order_release);
    }
    g_hClickedTaskbar.store(owner, std::memory_order_release);

    RefreshKeyboardLayouts();

    HWND hFlyout = AtomicLoadHwnd(g_hFlyoutWnd);
    if (!hFlyout || !IsWindow(hFlyout)) {
        HINSTANCE hInst = ThisModule();
        WNDCLASSW wc = {};
        wc.lpfnWndProc = FlyoutWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = kFlyoutClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassW(&wc)) {
            LogTagged(L"LANGSW", L"RegisterClassW failed");
            return;
        }

        const DWORD dwExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        const DWORD dwStyle = WS_POPUP | WS_CLIPCHILDREN | WS_BORDER;
        hFlyout = CreateWindowExW(dwExStyle, kFlyoutClassName,
                                  L"Windows Language Switcher", dwStyle,
                                  0, 0, 100, 100,
                                  nullptr, nullptr, hInst, nullptr);
        if (!hFlyout) {
            return;
        }
        g_hFlyoutWnd.store(hFlyout, std::memory_order_release);

        const bool isDark = IsDarkModeActive();
        const BOOL useDarkMode = isDark ? TRUE : FALSE;
        DwmSetWindowAttribute(hFlyout, 20, &useDarkMode, sizeof(useDarkMode));
        DwmSetWindowAttribute(hFlyout, 19, &useDarkMode, sizeof(useDarkMode));
        DWORD cornerPref = 2;   /* DWMWCP_ROUND */
        DwmSetWindowAttribute(hFlyout, 33, &cornerPref, sizeof(cornerPref));
    }

    if (hFlyout && IsWindow(hFlyout)) {
        PositionWindowNearTray(hFlyout);
        ShowWindow(hFlyout, SW_SHOW);
        SetForegroundWindow(hFlyout);
        InvalidateRect(hFlyout, nullptr, TRUE);
    }
}

void Hide() {
    HWND hFlyout = AtomicLoadHwnd(g_hFlyoutWnd);
    if (hFlyout && IsWindow(hFlyout)) {
        ShowWindow(hFlyout, SW_HIDE);
    }
}

void GetActiveInfo(uint32_t* langId, wchar_t* three, int threeCap,
                   wchar_t* two, int twoCap) {
    const WORD langIdWord = ActiveLangIdWord();
    if (langId != nullptr) {
        *langId = langIdWord;
    }
    /* Bounded copies written by hand: wcsncpy_s/_TRUNCATE is not
     * available on every MinGW toolchain this project builds with. */
    if (three != nullptr && threeCap > 0) {
        const std::wstring abbrev = GetLangAbbrev(langIdWord);
        const size_t n = abbrev.size() < static_cast<size_t>(threeCap) - 1
                             ? abbrev.size()
                             : static_cast<size_t>(threeCap) - 1;
        if (n > 0) {
            wmemcpy(three, abbrev.c_str(), n);
        }
        three[n] = L'\0';
    }
    if (two != nullptr && twoCap > 0) {
        WCHAR buf[16] = {};
        LettersForLangId(langIdWord, buf, 16);
        const size_t len = wcslen(buf);
        const size_t n = len < static_cast<size_t>(twoCap) - 1
                             ? len
                             : static_cast<size_t>(twoCap) - 1;
        if (n > 0) {
            wmemcpy(two, buf, n);
        }
        two[n] = L'\0';
    }
}

void SetChangedCallback(LangChangedCallback callback) {
    g_callback = callback;
}

void Shutdown() {
    /* orderly shutdown (core shutdown): hide the popup if it is open. */
    Hide();
}

} // namespace langswitcher
} // namespace w7t
