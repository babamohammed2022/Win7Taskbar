// Win7Taskbar - indicatore della lingua di input: port COMPLETO delle tre
// mod Windhawk (taskbar-language-indicator-layout-control, more-space-in-
// language-indicator, fix-legacy-taskbar-tray-input-indicator).
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// ============================================================================
// PERCHE' UN PORT E NON DEGLI HOOK.
//
// Le tre mod originali lavorano DENTRO explorer.exe deviando ShowWindow,
// DeferWindowPos, ExtTextOutW e BitBlt dell'indicatore DI Windows. Questa
// barra ricrea l'area di notifica: l'indicatore e' una finestra NOSTRA, con
// la STESSA struttura di quella di Windows (finestra-cornice
// "TrayInputIndicatorWClass" che contiene la finestra-testo
// "InputIndicatorButton", con ricerca ricorsiva dei figli e cache, come nel
// porting di riferimento). Non serve deviare nessuna API: ogni comportamento
// delle mod diventa logica diretta, con gli stessi valori e le stesse
// regole:
//
//   - layout-control: 4 modi (keepLayoutOnly, hide, show, windowsDefault);
//     SPI_GETSYSTEMLANGUAGEBAR INVERTITO (FALSE => mostra); SPI_SETSYSTEMLAN-
//     GUAGEBAR con pvParam = (PVOID)(BOOL) e SPIF_SENDCHANGE|SPIF_UPDATEINIFI-
//     LE (la documentazione dice "puntatore a BOOL" ed e' SBAGLIATA: solo il
//     BOOL castato ha effetto). keepLayoutOnly devia il nascondimento TEMPORA-
//     NEO (finestra di Remote Desktop in primo piano) al SOLO figlio-testo:
//     la cornice resta e l'area di notifica non salta.
//   - more-space: l'altezza della cornice non va mai sotto 32 px (la mod
//     deviava DeferWindowPos; qui lo garantisce WM_WINDOWPOSCHANGING).
//     Serve alla targhetta a due righe dello stile Windows 8.1.
//   - fix-legacy: il testo e' una sigla di 2-4 lettere alfabetiche, disegnata
//     NON ruotata con DrawTextW DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,
//     su sfondo colito dall'ultimo colore buono (GetPixel(w-1,h-1); se non va,
//     COLOR_BTNFACE) e testo COLOR_BTNTEXT. Il font arriva da CreateFontIndi-
//     rectW con lfEscapement = lfOrientation = 0 (la mod "dis-ruotava" il font
//     di Windows: qui il font lo creiamo gia' dritto). La guardia sul thread
//     della mod originale non serve: il disegno avviene nella nostra wndproc.
//
// La meccanica di focus/broadcast resta quella di ManagedShell: 200 ms di
// sondaggio del layout del thread in primo piano (GetGUIThreadInfo) e cambio
// lingua con LoadKeyboardLayout(KLF_SUBSTITUTE_OK|KLF_ACTIVATE) + broadcast
// di WM_INPUTLANGCHANGEREQUEST.
// ============================================================================

#include "LanguageBar.h"

#include "Common.h"
#include "ShellMenu.h"
#include "Strings.h"

#include <windows.h>
#include <windowsx.h>
#include <strsafe.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace w7t {
namespace langbar {

#ifndef SPI_GETSYSTEMLANGUAGEBAR
#define SPI_GETSYSTEMLANGUAGEBAR 0x2019
#endif
#ifndef SPI_SETSYSTEMLANGUAGEBAR
#define SPI_SETSYSTEMLANGUAGEBAR 0x2020
#endif

#ifndef WM_INPUTLANGCHANGEREQUEST
#define WM_INPUTLANGCHANGEREQUEST 0x0050
#endif

static constexpr wchar_t kFrameClass[] = L"TrayInputIndicatorWClass";
static constexpr wchar_t kTextClass[]  = L"InputIndicatorButton";

static constexpr UINT kPollTimerId  = 0xB6;   /* 200 ms: layout attivo + primo piano */
static constexpr UINT kPollTimerMs  = 200;

/* I 4 modi del layout-control (nomi identici alla mod di riferimento). */
enum class Config {
    keepLayoutOnly,
    hide,
    show,
    windowsDefault,
};

struct State {
    HINSTANCE hInst = nullptr;
    HWND hwndFrame = nullptr;
    HWND hwndText = nullptr;         /* figlio-testo, creato una volta sola */
    HWND hwndOwner = nullptr;        /* la barra (livello gestito) */
    UINT pollTimerId = 0;
    std::atomic<int> mode{ 1 };      /* 0 nascosta, 1 Win7, 2 Win8.1, 3 Win10/11 */
    Config config = Config::keepLayoutOnly;
    bool windowsShowConfig = true;   /* copia della configurazione di Windows */
    bool doNotReadWindowsConfigDuringShowWindow = false;
    bool showWindowWasOverriddenDuringLastCall = false;
    bool temporarilyHidden = false;  /* RDP in primo piano: il "nascondi temporaneo" di Windows */
    WORD currentLangId = 0;
    COLORREF lastGoodBg = CLR_INVALID;  /* lo sfondo buono della mod fix-legacy */
    int placeMode = 1;
};

static State g;

/* ------------------------------------------------------------------ */
/*  Configurazione di Windows (SPI), identica alla mod                 */
/* ------------------------------------------------------------------ */

static void GetWindowsConfig() {
    BOOL value = FALSE;
    if (!SystemParametersInfoW(SPI_GETSYSTEMLANGUAGEBAR, 0, &value, 0)) {
        /* Il parametro deve puntare a una variabile BOOL. */
        LogTagged(L"LANGBAR", L"SystemParametersInfoW(GET) non riuscito");
        return;
    }
    /* INVERTITO, come nella mod: FALSE => mostra. */
    g.windowsShowConfig = (value == FALSE);
}

static void SetWindowsConfig(bool show) {
    /* Non rileggere la configurazione finche' la cambiamo noi. */
    g.doNotReadWindowsConfigDuringShowWindow = true;

    const BOOL value = show ? FALSE : TRUE;
    if (!SystemParametersInfoW(SPI_SETSYSTEMLANGUAGEBAR, 0,
            /* La documentazione sbaglia: NON e' un puntatore a BOOL, e'
             * il BOOL castato a PVOID, altrimenti non ha effetto. */
            (PVOID)(INT_PTR)value,
            SPIF_SENDCHANGE | SPIF_UPDATEINIFILE)) {
        LogTagged(L"LANGBAR", L"SystemParametersInfoW(SET) non riuscito");
    }

    g.doNotReadWindowsConfigDuringShowWindow = false;
}

/* Come ApplyWindowsDefaultConfig della mod: ribalta e riporta la
 * configurazione per obbligare l'area di notifica ad aggiornarsi. */
static void ApplyWindowsDefaultConfig() {
    const bool show = g.windowsShowConfig;

    if (g.showWindowWasOverriddenDuringLastCall) {
        SetWindowsConfig(!show);
        g.showWindowWasOverriddenDuringLastCall = false;
    }
    SetWindowsConfig(show);
}

static void ApplyHideOrShowSettings() {
    const Config config = g.config;
    if (config == Config::windowsDefault) {
        return;
    }

    const bool show =
        config == Config::show
        || config == Config::keepLayoutOnly;

    GetWindowsConfig();      /* copia locale della configurazione di Windows */
    SetWindowsConfig(show);  /* allinea Windows al modo: aggiorna la tray vera */
    ApplyWindowsDefaultConfig();  /* e subito restaura il registro di Windows */
}

static void ConfigFromIndex(int index) {
    /* Ordine del selettore nelle Proprieta'/menu: 0 keepLayoutOnly,
     * 1 hide, 2 show, 3 windowsDefault (come le 4 opzioni della mod). */
    switch (index) {
        case 1: g.config = Config::hide; break;
        case 2: g.config = Config::show; break;
        case 3: g.config = Config::windowsDefault; break;
        default: g.config = Config::keepLayoutOnly; break;
    }
}

/* ------------------------------------------------------------------ */
/*  Linguaggio attivo (meccanica ManagedShell: sondaggio 200 ms)       */
/* ------------------------------------------------------------------ */

static WORD ActiveLangId() {
    /* Il thread di PRIMO PIANO decide la lingua, non il nostro processo:
     * con una finestra di Remote Desktop attiva il layout e' suo. */
    GUITHREADINFO info = {};
    info.cbSize = sizeof(info);
    DWORD tid = 0;
    if (GetGUIThreadInfo(0, &info) && info.hwndActive != nullptr) {
        tid = GetWindowThreadProcessId(info.hwndActive, nullptr);
    }
    HKL hkl = GetKeyboardLayout(tid);
    return LOWORD(reinterpret_cast<uintptr_t>(hkl));
}

static bool IsRemoteDesktopForeground() {
    /* La mod reagiva al ShowWindow(SW_HIDE) che Windows manda quando una
     * finestra di Connessione Desktop remoto arriva in primo piano. Qui
     * l'indicatore e' nostro: si riconosce lo scenario guardando il
     * processo in primo piano (mstsc.exe). */
    HWND fg = GetForegroundWindow();
    if (fg == nullptr) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return false;
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) {
        return false;
    }
    WCHAR path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    bool isRdp = false;
    if (QueryFullProcessImageNameW(proc, 0, path, &size) && size > 0) {
        LPCWSTR name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        isRdp = _wcsicmp(name, L"mstsc.exe") == 0;
    }
    CloseHandle(proc);
    return isRdp;
}

/* ------------------------------------------------------------------ */
/*  Le due finestre: cornice + testo                                   */
/* ------------------------------------------------------------------ */

/* La guardia della mod fix-legacy: solo sigle di 2-4 lettere alfabetiche. */
static bool LooksLikeLayoutText(LPCWSTR s, int len) {
    if (s == nullptr || len < 2 || len > 4) {
        return false;
    }
    for (int i = 0; i < len; i++) {
        if (!IsCharAlphaW(s[i])) {
            return false;
        }
    }
    return true;
}

static void LettersForLangId(WORD langId, WCHAR* two, size_t twoCap,
                             WCHAR* three, size_t threeCap) {
    LCID lcid = MAKELCID(langId, SORT_DEFAULT);
    two[0] = three[0] = L'\0';
    if (GetLocaleInfoW(lcid, LOCALE_SISO639LANGNAME, two, (int)twoCap) == 0) {
        StringCchCopyW(two, twoCap, L"--");
    }
    WCHAR upper[16] = {};
    if (GetLocaleInfoW(lcid, LOCALE_SABBREVLANGNAME, upper, 16) >= 3) {
        upper[3] = L'\0';
        StringCchCopyW(three, threeCap, upper);
    } else {
        wcsncpy_s(three, threeCap, two, _TRUNCATE);
    }
    CharUpperBuffW(two, (DWORD)wcslen(two));
    CharUpperBuffW(three, (DWORD)wcslen(three));
}

/* Il colore di sfondo della mod fix-legacy, adattato senza hook: il pixel
 * in basso a destra lo si campiona sul GENITORE (la barra), nell'angolo
 * della nostra area: e' lo stesso gesto di GetPixel(w-1,h-1) del BitBlt
 * della mod, ma la "bitmap sorgente" qui e' il fondo della barra. Il colore
 * buono si tiene in cache (lastGoodBg); se non c'e' niente di buono:
 * COLOR_BTNFACE, come nella mod. */
static COLORREF IndicatorBackground(HWND hwnd, int w, int h) {
    COLORREF bgColor = CLR_INVALID;
    HWND parent = GetParent(hwnd);
    if (parent != nullptr && w > 1 && h > 1) {
        RECT wr = {};
        if (GetWindowRect(hwnd, &wr)) {
            POINT corner = { wr.left + w - 1, wr.top + h - 1 };
            ScreenToClient(parent, &corner);
            HDC pdc = GetDC(parent);
            if (pdc != nullptr) {
                bgColor = GetPixel(pdc, corner.x, corner.y);
                ReleaseDC(parent, pdc);
            }
        }
    }
    if (bgColor == CLR_INVALID || bgColor == 0x000000) {
        bgColor = (g.lastGoodBg != CLR_INVALID)
                      ? g.lastGoodBg
                      : GetSysColor(COLOR_BTNFACE);
    } else {
        g.lastGoodBg = bgColor;
    }
    return bgColor;
}

/* Il testo della mod era COLOR_BTNTEXT (nero) su tile chiara: qui il tile
 * ha il colore DELLA BARRA (a volte scuro), quindi la tinta del testo si
 * sceglie per contrasto: sotto la soglia di luminanza, bianco. */
static COLORREF TextColorFor(COLORREF bg) {
    const LONG lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587
                      + GetBValue(bg) * 114) / 1000;
    return (lum < 128) ? RGB(255, 255, 255) : RGB(0, 0, 0);
}

static HFONT IndicatorFont(int mode, LONG h, bool smallRow) {
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    lf.lfEscapement = 0;   /* Dritto: e' il senso della fix-legacy. */
    lf.lfOrientation = 0;
    /* Altezza del carattere proporzionale all'altezza reale della cornice:
     * resta identica a ogni DPI, come le misure gestite. */
    LONG em;
    if (mode == 1) {
        em = (LONG)(h * 0.40);
        lf.lfWeight = FW_SEMIBOLD;
    } else if (mode == 2) {
        em = smallRow ? (LONG)(h * 0.30) : (LONG)(h * 0.34);
        lf.lfWeight = smallRow ? FW_NORMAL : FW_SEMIBOLD;
    } else {
        em = (LONG)(h * 0.50);
        lf.lfWeight = FW_NORMAL;
    }
    lf.lfHeight = -(em > 8 ? em : 8);
    return CreateFontIndirectW(&lf);
}

static void DrawOneLine(HDC hdc, const RECT& rc, LPCWSTR text, int mode,
                        LONG h, COLORREF textColor) {
    RECT fill = rc;
    HFONT font = IndicatorFont(mode, h, false);
    if (font == nullptr) {
        return;
    }
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textColor);
    DrawTextW(hdc, text, -1, &fill,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

/* Il disegno della sigla (mod fix-legacy): solo sigle di 2-4 lettere
 * alfabetiche, dritte, centrate con DrawTextW
 * DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX. La guardia sul thread
 * della mod originale non serve: il disegno avviene nella nostra wndproc. */
static void PaintIndicator(HWND hwnd, HDC hdc, const RECT& rc) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const COLORREF bgColor = IndicatorBackground(hwnd, w, h);
    const COLORREF textColor = TextColorFor(bgColor);

    RECT fill = rc;
    HBRUSH brush = CreateSolidBrush(bgColor);
    FillRect(hdc, &fill, brush);
    DeleteObject(brush);

    WCHAR two[16] = {}, three[16] = {};
    LettersForLangId(g.currentLangId, two, 16, three, 16);

    const int mode = g.placeMode;
    if (mode == 2) {
        /* La targhetta di Windows 8.1: tre lettere sopra, due sotto (la
         * ragione del minimo di 32 px della mod more-space). */
        const int lenTop = (int)wcslen(three);
        const int lenBottom = (int)wcslen(two);
        if (!LooksLikeLayoutText(three, lenTop)
            || !LooksLikeLayoutText(two, lenBottom)) {
            return;
        }
        RECT top = { rc.left, rc.top, rc.right, rc.top + h / 2 };
        RECT bottom = { rc.left, rc.top + h / 2 - 1, rc.right, rc.bottom };
        HFONT font = IndicatorFont(mode, h, false);
        if (font != nullptr) {
            HFONT oldFont = (HFONT)SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textColor);
            DrawTextW(hdc, three, -1, &top,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            DeleteObject(font);
        }
        font = IndicatorFont(mode, h, true);
        if (font != nullptr) {
            HFONT oldFont = (HFONT)SelectObject(hdc, font);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textColor);
            DrawTextW(hdc, two, -1, &bottom,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldFont);
            DeleteObject(font);
        }
        return;
    }

    LPCWSTR text = (mode == 1) ? two : three;
    const int len = (int)wcslen(text);
    if (!LooksLikeLayoutText(text, len)) {
        return;   /* niente sigle strane: la regola della mod */
    }
    DrawOneLine(hdc, rc, text, mode, h, textColor);
}

/* ------------------------------------------------------------------ */
/*  Visibilita': il cuore del layout-control                           */
/* ------------------------------------------------------------------ */

static void ApplyVisibility() {
    if (g.hwndFrame == nullptr) {
        return;
    }

    const int mode = g.mode.load(std::memory_order_acquire);
    const bool frameExists = IsWindow(g.hwndFrame);

    /* Aggiorna la copia della configurazione di Windows (come la hook di
     * ShowWindow rileggeva a ogni chiamata). */
    if (!g.doNotReadWindowsConfigDuringShowWindow) {
        GetWindowsConfig();
    }

    bool frameVisible = false;
    bool textVisible = false;

    switch (g.config) {
        case Config::hide:
            /* Nascosta SEMPRE: si vince anche all'oblio di Windows dopo
             * il riavvio (il difetto che la mod risolveva). */
            frameVisible = false;
            break;

        case Config::show:
            /* Visibile SEMPRE, anche con Remote Desktop in primo piano. */
            frameVisible = (mode != 0);
            textVisible = true;
            break;

        case Config::windowsDefault:
            /* Windows decide: la scelta di Windows e' INVERTITA (SPI
             * FALSE = mostra). Il "nascondimento temporaneo" (RDP)
             * segue Windows, senza preservare il layout. */
            frameVisible = (mode != 0) && g.windowsShowConfig;
            textVisible = true;
            break;

        case Config::keepLayoutOnly:
        default:
            frameVisible = (mode != 0);
            /* Il trucco della mod: il nascondimento TEMPORANEO va al SOLO
             * testo, la cornice resta al suo posto => l'area di notifica
             * non salta. */
            textVisible = !g.temporarilyHidden;
            break;
    }

    if (!frameExists) {
        return;
    }

    /* Rispecchia la decisione sulle due finestre. Il flag ricorda che un
     * comando e' stato deviato (lo usa ApplyWindowsDefaultConfig). */
    const BOOL wasFrameVisible = IsWindowVisible(g.hwndFrame);
    if (frameVisible != (wasFrameVisible != FALSE)) {
        ShowWindow(g.hwndFrame, frameVisible ? SW_SHOW : SW_HIDE);
        g.showWindowWasOverriddenDuringLastCall = !frameVisible;
    }
    if (IsWindow(g.hwndText)) {
        const BOOL wasTextVisible = IsWindowVisible(g.hwndText);
        if (textVisible != (wasTextVisible != FALSE)) {
            /* Lo ShowWindow DEVIATO al figlio-testo: la cornice resta. */
            ShowWindow(g.hwndText, textVisible ? SW_SHOW : SW_HIDE);
        }
    }
    if (frameVisible) {
        InvalidateRect(g.hwndFrame, nullptr, TRUE);
    }
}

static void OnPollTick(HWND hwnd) {
    /* Il "Windows vuole nascondere temporaneamente" della mod: la finestra
     * di Remote Desktop in primo piano. */
    const bool rdp = IsRemoteDesktopForeground();
    if (rdp != g.temporarilyHidden) {
        g.temporarilyHidden = rdp;
        ApplyVisibility();
    }

    const WORD langId = ActiveLangId();
    if (langId != 0 && langId != g.currentLangId) {
        g.currentLangId = langId;
        InvalidateRect(g.hwndText, nullptr, TRUE);
        /* La cornice disegna nel suo WM_PAINT col pixel-angolo: anche lei
         * si ridisegna quando cambia la sigla. */
        InvalidateRect(g.hwndFrame, nullptr, FALSE);
    }

    /* L'owner e' morto (la barra si sta chiudendo): la finestra va con
     * lui, come l'indicatore di Windows muore con la tray. */
    if (g.hwndOwner != nullptr && !IsWindow(g.hwndOwner)
        && g.hwndFrame != nullptr) {
        DestroyWindow(g.hwndFrame);
        g.hwndFrame = nullptr;
        g.hwndText = nullptr;
        if (hwnd != nullptr) {
            KillTimer(hwnd, kPollTimerId);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Il menu di scelta lingua (stesso aspetto delle altre voci)         */
/* ------------------------------------------------------------------ */

struct LayoutEntry {
    HKL hkl = nullptr;
    std::wstring name;
};

static void InstalledLayouts(std::vector<LayoutEntry>& out) {
    UINT count = GetKeyboardLayoutList(0, nullptr);
    if (count == 0) {
        return;
    }
    std::vector<HKL> hkls(count);
    count = GetKeyboardLayoutList(count, hkls.data());
    out.reserve(count);
    for (UINT i = 0; i < count; i++) {
        LayoutEntry entry;
        entry.hkl = hkls[i];
        const WORD langId = LOWORD(reinterpret_cast<uintptr_t>(hkls[i]));
        WCHAR native[128] = {};
        if (GetLocaleInfoW(MAKELCID(langId, SORT_DEFAULT),
                           LOCALE_SNATIVELANGNAME, native, 128) == 0) {
            StringCchPrintfW(native, 128, L"%04x", (unsigned)langId);
        }
        entry.name = native;
        out.push_back(std::move(entry));
    }
}

static void ShowLayoutMenu(HWND hwndFrame) {
    std::vector<LayoutEntry> layouts;
    InstalledLayouts(layouts);
    if (layouts.empty()) {
        return;
    }

    /* Come nel resto della barra: un solo testo con il mini-linguaggio
     * ('-' separatore, '*' spunta), voci dalla tabella della lingua UI. */
    std::wstring text;
    for (size_t i = 0; i < layouts.size(); i++) {
        if (!text.empty()) {
            text += L'\n';
        }
        const WORD langId = LOWORD(reinterpret_cast<uintptr_t>(layouts[i].hkl));
        if (langId == g.currentLangId) {
            text += L'*';
        }
        text += layouts[i].name;
    }

    /* Le 4 scelte del layout-control (la configurazione della mod, ora
     * nel menu: stessi nomi, stesse semantiche). */
    text += L"\n-\n";
    text += S(StrId::LangBarKeepLayout);
    text += L'\n';
    text += S(StrId::LangBarHide);
    text += L'\n';
    text += S(StrId::LangBarShow);
    text += L'\n';
    text += S(StrId::LangBarWindowsDefault);

    RECT rc = {};
    GetWindowRect(hwndFrame, &rc);
    const int x = rc.left;
    const int y = rc.bottom;

    const int chosen = ShellMenu::ShowContextMenuEx(x, y, true, text.c_str(), true);
    if (chosen <= 0) {
        return;
    }

    if (chosen <= (int)layouts.size()) {
        /* Cambio lingua come ManagedShell (e come il vecchio livello
         * gestito): KLID = parola LINGUA in 8 cifre esadecimali ("00000409"),
         * carica/attiva e manda in broadcast l'HKL RESTITUITO, lo stesso
         * messaggio di Alt+Shift. */
        const HKL chosenHkl = layouts[chosen - 1].hkl;
        const WORD langWord =
            LOWORD(reinterpret_cast<uintptr_t>(chosenHkl));
        WCHAR klid[16] = {};
        StringCchPrintfW(klid, 16, L"%08X", (unsigned)langWord);
        HKL loaded = LoadKeyboardLayoutW(klid,
                                         KLF_SUBSTITUTE_OK | KLF_ACTIVATE);
        if (loaded == nullptr) {
            loaded = chosenHkl;
        }
        PostMessageW(HWND_BROADCAST, WM_INPUTLANGCHANGEREQUEST, 0,
                     reinterpret_cast<LPARAM>(loaded));
        return;
    }

    /* Voci di policy: i separatori NON contano nella numerazione di
     * ShowContextMenuEx, quindi dopo la lista le 4 voci restano
     * chosen = N+1 .. N+4  =>  indici di modo 0..3. */
    const int policy = chosen - (int)layouts.size() - 1;
    const int previous = (int)g.config;
    ConfigFromIndex(policy);
    if ((int)g.config != previous) {
        if (g.config == Config::windowsDefault) {
            GetWindowsConfig();
            ApplyWindowsDefaultConfig();
        } else {
            ApplyHideOrShowSettings();
        }
        ApplyVisibility();
    }
}

/* ------------------------------------------------------------------ */
/*  WndProc della cornice e del testo                                  */
/* ------------------------------------------------------------------ */

static LRESULT CALLBACK TextWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc != nullptr) {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                PaintIndicator(hwnd, hdc, rc);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            /* Il clic passa alla cornice: e' lei che apre il menu. */
            PostMessageW(GetParent(hwnd), msg, wp, lp);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* La ricerca ricorsiva del figlio-testo della mod (EnumChildWindows non
 * puo' essere sostituita da FindWindowEx: serve la profondita'). */
static BOOL CALLBACK FindTextChildProc(HWND hwnd, LPARAM lp) {
    WCHAR cls[32] = {};
    if (GetClassNameW(hwnd, cls, 32) != 0 && wcscmp(cls, kTextClass) == 0) {
        *reinterpret_cast<HWND*>(lp) = hwnd;
        return FALSE;   /* trovato: si ferma */
    }
    return TRUE;        /* continua */
}

static LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowExW(0, kTextClass, nullptr, WS_CHILD | WS_VISIBLE,
                            0, 0, 0, 0, hwnd, nullptr, g.hInst, nullptr);
            /* Il figlio-testo si cerca con la STESSA scansione ricorsiva
             * della mod (cache compresa): cosi' la struttura e' identica
             * anche dopo una ricreazione. */
            HWND found = nullptr;
            EnumChildWindows(hwnd, FindTextChildProc,
                             reinterpret_cast<LPARAM>(&found));
            if (found != nullptr) {
                g.hwndText = found;
            }
            SetTimer(hwnd, kPollTimerId, kPollTimerMs, nullptr);
            ApplyVisibility();
            return 0;
        }
        case WM_SIZE:
            if (IsWindow(g.hwndText)) {
                SetWindowPos(g.hwndText, nullptr, 0, 0, LOWORD(lp), HIWORD(lp),
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        case WM_WINDOWPOSCHANGING: {
            /* more-space: l'altezza della cornice non scende mai sotto 32.
             * La mod lo imponeva deviando DeferWindowPos; qui la stessa
             * regola dentro il messaggio di posizionamento. */
            WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(lp);
            if (pos != nullptr && (pos->flags & SWP_NOSIZE) == 0
                && pos->cy < 32) {
                pos->cy = 32;
            }
            break;
        }
        case WM_PAINT: {
            /* La cornice NON disegna la sigla: il testo sta nel figlio
             * (InputIndicatorButton), come in Windows. Quando keepLayoutOnly
             * nasconde il figlio (Remote Desktop), la cornice mostra il solo
             * fondo: lo spazio resta, l'area di notifica non salta. */
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc != nullptr) {
                RECT rc = {};
                GetClientRect(hwnd, &rc);
                const int w = rc.right - rc.left;
                const int h = rc.bottom - rc.top;
                const COLORREF bgColor = IndicatorBackground(hwnd, w, h);
                RECT fill = rc;
                HBRUSH brush = CreateSolidBrush(bgColor);
                FillRect(hdc, &fill, brush);
                DeleteObject(brush);
                EndPaint(hwnd, &ps);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            ShowLayoutMenu(hwnd);
            return 0;
        case WM_TIMER:
            if (wp == kPollTimerId) {
                OnPollTick(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(hwnd, kPollTimerId);
            if (g.hwndFrame == hwnd) {
                g.hwndFrame = nullptr;
                g.hwndText = nullptr;
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool RegisterClasses(HINSTANCE hInst) {
    if (g.hInst != nullptr) {
        return true;
    }

    WNDCLASSW frame = {};
    frame.lpfnWndProc = FrameWndProc;
    frame.hInstance = hInst;
    frame.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    frame.hbrBackground = nullptr;
    frame.style = CS_HREDRAW | CS_VREDRAW;
    frame.lpszClassName = kFrameClass;
    if (RegisterClassW(&frame) == 0) {
        return false;
    }

    WNDCLASSW text = {};
    text.lpfnWndProc = TextWndProc;
    text.hInstance = hInst;
    text.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    text.hbrBackground = nullptr;
    text.style = CS_HREDRAW | CS_VREDRAW;
    text.lpszClassName = kTextClass;
    if (RegisterClassW(&text) == 0) {
        return false;
    }

    g.hInst = hInst;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Ingresso pubblico                                                  */
/* ------------------------------------------------------------------ */

void Place(uint64_t ownerHwnd, int mode, int x, int y, int w, int h) {
    HWND owner = reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd));
    if (owner == nullptr || !IsWindow(owner)) {
        return;
    }

    /* La mod verificava che Shell_TrayWnd fosse DELLO STESSO processo: qui
     * l'owner deve essere una finestra del nostro processo (la barra). */
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    if (pid != GetCurrentProcessId()) {
        return;
    }

    if (g.hInst == nullptr) {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                           | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&RegisterClasses),
                           &self);
        if (self == nullptr || !RegisterClasses((HINSTANCE)self)) {
            return;
        }
    }

    g.mode.store(mode, std::memory_order_release);
    g.placeMode = mode;
    g.hwndOwner = owner;

    if (mode == 0) {
        /* Nascosta: la cornice resta (per riusarla subito) ma invisibile:
         * e' il modo "hide" del layout-control. */
        ApplyVisibility();
        return;
    }

    if (g.hwndFrame == nullptr || !IsWindow(g.hwndFrame)) {
        g.hwndFrame = CreateWindowExW(WS_EX_NOACTIVATE, kFrameClass, nullptr,
                                      WS_CHILD | WS_CLIPSIBLINGS,
                                      x, y, w, h, owner, nullptr,
                                      g.hInst, nullptr);
        if (g.hwndFrame == nullptr) {
            return;
        }
    } else {
        SetWindowPos(g.hwndFrame, HWND_TOP, x, y, w, h,
                     SWP_NOACTIVATE);
        /* Lo stile puo' essere cambiato a finestra gia' viva (il modo
         * arriva dalle Proprieta'): il figlio-testo deve ridisegnare. */
        InvalidateRect(g.hwndFrame, nullptr, TRUE);
        if (g.hwndText != nullptr && IsWindow(g.hwndText)) {
            InvalidateRect(g.hwndText, nullptr, TRUE);
        }
    }

    ApplyVisibility();
}

void SetPolicyIndex(int index) {
    ConfigFromIndex(index);
    if (g.config == Config::windowsDefault) {
        GetWindowsConfig();
        ApplyWindowsDefaultConfig();
    } else {
        ApplyHideOrShowSettings();
    }
    ApplyVisibility();
}

void Shutdown() {
    if (g.hwndFrame != nullptr && IsWindow(g.hwndFrame)) {
        DestroyWindow(g.hwndFrame);
    }
    g.hwndFrame = nullptr;
    g.hwndText = nullptr;
    g.hwndOwner = nullptr;
}

} // namespace langbar
} // namespace w7t
