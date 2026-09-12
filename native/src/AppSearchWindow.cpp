// Win7Taskbar - ricerca applicazioni opzionale
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.8: finestra LAYERED con GRADIENTE DI TRASPARENZA disegnato da noi:
// sfondo blu della foto con alpha che varia da ~222 (alto) a ~242
// (basso): il desktop traspare "un po'", il testo resta leggibile.
// Tecnica: doppia scena (colori + maschera alpha in scala di grigi con
// lo stesso anti-aliasing GDI), composizione per-pixel premoltiplicata
// e UpdateLayeredWindow. Icona propria con maschera 1bpp ricavata
// dall'alpha (niente piu' riquadro bianco). Scansione app RICORSIVA
// sulle sottocartelle di Start Menu\Programs (come Open-Shell): ora
// trova Blocco note, Paint, ecc.

#include "AppSearchWindow.h"
#include "AeroGlass.h"
#include "FlyoutLauncher.h"
#include "SehGuard.h"
#include "Common.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <windowsx.h>
#include <objbase.h>
#include <wincodec.h>     /* v2.37: decodifica PNG incorporati (WIC) */

/* MinGW non conosce HighQualityCubic (valore 4 nell'SDK recente); il
 * runtime WIC di Windows 10/11 lo supporta. Fallback definito a mano. */
#ifndef WICBitmapInterpolationModeHighQualityCubic
#define WICBitmapInterpolationModeHighQualityCubic \
    static_cast<WICBitmapInterpolationMode>(4)
#endif
#include <algorithm>
#include <commoncontrols.h>
#include <cctype>
#include <cstring>
#include <functional>
#include <set>
#include <vector>

namespace w7t {

namespace {
constexpr wchar_t kClassName[] = L"Win7Taskbar_AppSearch";

/* Gradiente di trasparenza "un po' basso": quasi opaco ma il desktop
 * traspare leggermente, piu' trasparente in alto. */
constexpr int kAlphaTop    = 222;
constexpr int kAlphaBottom = 242;

constexpr COLORREF kBgTop    = RGB(0x86, 0xAE, 0xD6);   // blu della foto
constexpr COLORREF kBgBottom = RGB(0x57, 0x8B, 0xBE);
constexpr COLORREF kText     = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kTextDim  = RGB(0xE8, 0xF2, 0xFB);
constexpr COLORREF kSep      = RGB(0xC6, 0xDC, 0xF1);
constexpr COLORREF kSelTop   = RGB(0x9C, 0xC4, 0xEC);
constexpr COLORREF kSelBot   = RGB(0x7F, 0xB0, 0xE2);
constexpr COLORREF kSelEdge  = RGB(0xD9, 0xEA, 0xFA);
constexpr COLORREF kHover    = RGB(0x6F, 0xA0, 0xD4);
constexpr COLORREF kEditBg   = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF kEditEdge = RGB(0x4E, 0x6E, 0x92);
constexpr COLORREF kEditText = RGB(0x1E, 0x1E, 0x1E);

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

/* v2.37 bug #1: nome del file (senza percorso) del target, SENZA
 * estensione, minuscolo. "C:\Windows\notepad.exe" -> "notepad": cosi'
 * la query "notepad" trova anche "Blocco note.lnk". */
std::wstring TargetBaseNameNoExt(const std::wstring& path) {
    if (path.empty()) return std::wstring();
    const size_t bs = path.find_last_of(L"\\/");
    std::wstring base = (bs == std::wstring::npos) ? path : path.substr(bs + 1);
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) base = base.substr(0, dot);
    return Lower(base);
}

void SafeDrawIconEx(HDC hdc, int x, int y, HICON icon, int w, int h) {
    W7T_SEH_TRY
        DrawIconEx(hdc, x, y, icon, w, h, 0, nullptr, DI_NORMAL);
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* Nella scena-maschera disegna l'alpha dell'icona: parti opache bianche
 * (255), trasparenti al grigio locale (cosi' il fondo resta intatto). */
void SafeDrawIconMask(HDC hdc, int x, int y, HICON icon, int w, int h,
                      COLORREF localGray) {
    W7T_SEH_TRY
        COLORREF ob = SetBkColor(hdc, RGB(0xFF, 0xFF, 0xFF));
        COLORREF ot = SetTextColor(hdc, localGray);
        DrawIconEx(hdc, x, y, icon, w, h, 0, nullptr, DI_MASK);
        SetBkColor(hdc, ob);
        SetTextColor(hdc, ot);
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* v2.34: fix definitivo icone ricerca. DrawIconEx su un memory DC non
 * compone le icone 32bpp con alpha (maschera AND ignorata/indefinita:
 * il risultato sono rettangoli bianchi). Metodo noto usato dai progetti
 * che compongono manualmente su finestre layered:
 *   GetIconInfo -> GetDIBits (32bpp top-down) -> blend per-pixel
 *   premoltiplicato nei buffer di scena.
 * Scena colori: over-composizione RGB. Scena maschera: over-composizione
 * dell'alpha sopra il gradiente alpha esistente. Ritorna false per le
 * icone non-32bpp (basate su maschera): li' DrawIconEx funziona bene. */
bool BlitIcon32(uint32_t* bits, int W, int H, HICON icon,
                int x, int y, bool mask) {
    if (bits == nullptr || icon == nullptr) return false;
    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii)) return false;
    bool done = false;
    W7T_SEH_TRY {
        BITMAP bm{};
        if (ii.hbmColor != nullptr &&
            GetObjectW(ii.hbmColor, sizeof(bm), &bm) == sizeof(bm) &&
            bm.bmBitsPixel == 32 && bm.bmWidth > 0 && bm.bmHeight > 0 &&
            bm.bmWidth <= 256 && bm.bmHeight <= 256) {
            const int iw = bm.bmWidth, ih = bm.bmHeight;
            std::vector<uint32_t> px(static_cast<size_t>(iw) * ih);
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = iw;
            bmi.bmiHeader.biHeight = -ih;   /* top-down come le scene */
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            HDC scr = GetDC(nullptr);
            const int got = GetDIBits(scr, ii.hbmColor, 0, ih, px.data(),
                                      &bmi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, scr);
            if (got == ih) {
                for (int j = 0; j < ih; ++j) {
                    const int dy = y + j;
                    if (dy < 0 || dy >= H) continue;
                    const uint32_t* row = &px[static_cast<size_t>(j) * iw];
                    uint32_t* drow = &bits[static_cast<size_t>(dy) * W];
                    for (int i = 0; i < iw; ++i) {
                        const int dx = x + i;
                        if (dx < 0 || dx >= W) continue;
                        const uint32_t src = row[i];      /* 0xAARRGGBB */
                        const uint32_t a = (src >> 24) & 0xFF;
                        if (a == 0) continue;
                        if (!mask) {
                            if (a == 255) {
                                drow[dx] = src & 0x00FFFFFF;
                            } else {
                                const uint32_t dst = drow[dx];
                                const uint32_t inv = 255 - a;
                                const uint32_t r =
                                    (((src >> 16) & 0xFF) * a +
                                     ((dst >> 16) & 0xFF) * inv) / 255;
                                const uint32_t g =
                                    (((src >> 8) & 0xFF) * a +
                                     ((dst >> 8) & 0xFF) * inv) / 255;
                                const uint32_t b =
                                    ((src & 0xFF) * a +
                                     (dst & 0xFF) * inv) / 255;
                                drow[dx] = (r << 16) | (g << 8) | b;
                            }
                        } else {
                            const uint32_t bg = (drow[dx] >> 16) & 0xFF;
                            const uint32_t m = a + (bg * (255 - a)) / 255;
                            drow[dx] = (m << 16) | (m << 8) | m;
                        }
                    }
                }
                done = true;
            }
        }
    } W7T_SEH_CATCH {} W7T_SEH_END
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return done;
}

/* ================================================================== *
 *  v2.37: risorse incorporate e utilita' nuove                        *
 * ================================================================== */

/* base64 delle risorse incorporate (lente + scudi UAC): costanti pure,
 * nessun namespace proprio. */
#include "SearchAssets.inc"
#include "SearchActionAssets.inc"   /* v2.41: icone "Apri"/"Apri percorso file" */

/* RAII per HICON: distrugge l'icona in OGNI percorso di uscita
 * (punto 14). Move-only, come richiesto. */
struct IconGuard {
    HICON h = nullptr;
    explicit IconGuard(HICON i = nullptr) : h(i) {}
    IconGuard(const IconGuard&) = delete;
    IconGuard& operator=(const IconGuard&) = delete;
    IconGuard(IconGuard&& o) noexcept : h(o.h) { o.h = nullptr; }
    IconGuard& operator=(IconGuard&& o) noexcept {
        if (this != &o) { if (h) DestroyIcon(h); h = o.h; o.h = nullptr; }
        return *this;
    }
    ~IconGuard() { if (h) DestroyIcon(h); }
    HICON release() { HICON t = h; h = nullptr; return t; }
};

/* Blit di pixel ARGB ad alpha DRITTO dentro le scene (stessa
 * composizione premoltiplicata di BlitIcon32). */
void BlitArgb(uint32_t* bits, int W, int H, const uint32_t* src,
              int sw, int sh, int x, int y, bool mask) {
    if (!bits || !src || sw <= 0 || sh <= 0) return;
    for (int j = 0; j < sh; ++j) {
        const int dy = y + j;
        if (dy < 0 || dy >= H) continue;
        const uint32_t* row = &src[static_cast<size_t>(j) * sw];
        uint32_t* drow = &bits[static_cast<size_t>(dy) * W];
        for (int i = 0; i < sw; ++i) {
            const int dx = x + i;
            if (dx < 0 || dx >= W) continue;
            const uint32_t s = row[i];      /* 0xAARRGGBB */
            const uint32_t a = (s >> 24) & 0xFF;
            if (a == 0) continue;
            if (!mask) {
                if (a == 255) {
                    drow[dx] = s & 0x00FFFFFF;
                } else {
                    const uint32_t dst = drow[dx];
                    const uint32_t inv = 255 - a;
                    const uint32_t r = (((s >> 16) & 0xFF) * a +
                                        ((dst >> 16) & 0xFF) * inv) / 255;
                    const uint32_t g = (((s >> 8) & 0xFF) * a +
                                        ((dst >> 8) & 0xFF) * inv) / 255;
                    const uint32_t b = ((s & 0xFF) * a +
                                        (dst & 0xFF) * inv) / 255;
                    drow[dx] = (r << 16) | (g << 8) | b;
                }
            } else {
                const uint32_t bg = (drow[dx] >> 16) & 0xFF;
                const uint32_t m = a + (bg * (255 - a)) / 255;
                drow[dx] = (m << 16) | (m << 8) | m;
            }
        }
    }
}

/* Variante scalata (nearest): usata solo per lo scudo UAC grande
 * qualora la taglia di disegno superasse i 16 px. */
void BlitArgbScaled(uint32_t* bits, int W, int H, const uint32_t* src,
                    int sw, int sh, int x, int y, int dw, int dh, bool mask) {
    if (!bits || !src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    for (int j = 0; j < dh; ++j) {
        const int sy = j * sh / dh;
        const int dy = y + j;
        if (dy < 0 || dy >= H) continue;
        const uint32_t* row = &src[static_cast<size_t>(sy) * sw];
        uint32_t* drow = &bits[static_cast<size_t>(dy) * W];
        for (int i = 0; i < dw; ++i) {
            const int sx = i * sw / dw;
            const int dx = x + i;
            if (dx < 0 || dx >= W) continue;
            const uint32_t s = row[sx];
            const uint32_t a = (s >> 24) & 0xFF;
            if (a == 0) continue;
            if (!mask) {
                if (a == 255) drow[dx] = s & 0x00FFFFFF;
                else {
                    const uint32_t dst = drow[dx];
                    const uint32_t inv = 255 - a;
                    const uint32_t r = (((s >> 16) & 0xFF) * a +
                                        ((dst >> 16) & 0xFF) * inv) / 255;
                    const uint32_t g = (((s >> 8) & 0xFF) * a +
                                        ((dst >> 8) & 0xFF) * inv) / 255;
                    const uint32_t b = ((s & 0xFF) * a +
                                        (dst & 0xFF) * inv) / 255;
                    drow[dx] = (r << 16) | (g << 8) | b;
                }
            } else {
                const uint32_t bg = (drow[dx] >> 16) & 0xFF;
                const uint32_t m = a + (bg * (255 - a)) / 255;
                drow[dx] = (m << 16) | (m << 8) | m;
            }
        }
    }
}

/* DPI per-finestra come la mod di riferimento (riga ~649/3806):
 * GetDpiForWindow caricato dinamicamente, ripiego GetDeviceCaps,
 * guardia >= 96. Usato per la REGOLA di scelta della variante dello
 * scudo; il disegno resta alla taglia fissa del layout (finestra non
 * scalata). */
UINT GetSearchWindowDpi(HWND hwnd) {
    UINT dpi = 0;
    typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = []() -> GetDpiForWindowFn {
        HMODULE m = GetModuleHandleW(L"user32.dll");
        if (!m) return nullptr;
        return reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(m, "GetDpiForWindow"));
    }();
    if (fn && hwnd) dpi = fn(hwnd);
    if (dpi < 96) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
            ReleaseDC(hwnd, dc);
        }
    }
    if (dpi < 96) dpi = 96;
    return dpi;
}

/* v2.37 punto 5: tolleranza errori di battitura. Distanza di
 * Levenshtein con arresto precoce (soglia 1). */
int LevenshteinAtMost1(const std::wstring& a, const std::wstring& b) {
    const size_t na = a.size(), nb = b.size();
    if (na > nb + 1 || nb > na + 1) return 2;
    if (na == 0) return static_cast<int>(nb) <= 1 ? static_cast<int>(nb) : 2;
    if (nb == 0) return static_cast<int>(na) <= 1 ? static_cast<int>(na) : 2;
    std::vector<int> prev(nb + 1), cur(nb + 1);
    for (size_t j = 0; j <= nb; ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= na; ++i) {
        cur[0] = static_cast<int>(i);
        int rowMin = cur[0];
        for (size_t j = 1; j <= nb; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
            rowMin = std::min(rowMin, cur[j]);
        }
        if (rowMin > 1) return 2;
        prev.swap(cur);
    }
    return prev[nb] <= 1 ? prev[nb] : 2;
}

/* Corrispondenza "morbida" tra query (minuscola) e testo (minuscolo):
 * 1) una PAROLA del testo di almeno 4 caratteri dista <= 1 dalla
 *    query (parola intera: "bloco" -> "blocco");
 * 2) oppure fuzzy leggero in ordine: tutti i caratteri della query
 *    compaiono nel testo nello stesso ordine. */
bool FuzzyMatchText(const std::wstring& text, const std::wstring& q) {
    if (q.size() < 4 || text.empty()) return false;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const bool sep = (i == text.size()) ||
            !(iswalnum(static_cast<wint_t>(text[i])) != 0);
        if (!sep) continue;
        if (i > start) {
            std::wstring word = text.substr(start, i - start);
            if (word.size() >= 4 && LevenshteinAtMost1(word, q) <= 1) return true;
        }
        start = i + 1;
    }
    size_t need = 0;
    for (size_t i = 0; i < text.size() && need < q.size(); ++i) {
        if (text[i] == q[need]) ++need;
    }
    return need == q.size();
}

/* v2.37 punto 7: la casella di ricerca e' disegnata da noi (non e' un
 * vero controllo EDIT), quindi selezione e appunti vanno gestiti a
 * mano. Questi due helper incapsulano gli appunti; ogni errore
 * semplicemente non fa nulla (mai crashare per gli appunti). */
bool ClipboardSetText(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            wchar_t* dst = static_cast<wchar_t*>(GlobalLock(h));
            if (dst) {
                std::memcpy(dst, text.c_str(), bytes);
                GlobalUnlock(h);
                if (SetClipboardData(CF_UNICODETEXT, h)) ok = true;
            }
            if (!ok) GlobalFree(h);
        }
    }
    CloseClipboard();
    return ok;
}

std::wstring ClipboardGetText(HWND hwnd) {
    std::wstring clean;
    if (!OpenClipboard(hwnd)) return clean;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(h));
        if (src) {
            /* casella a riga singola: via CR/LF/tab */
            for (const wchar_t* p = src; *p; ++p) {
                if (*p != L'\r' && *p != L'\n' && *p != L'\t') clean += *p;
            }
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return clean;
}


HICON SafeGetIcon(const wchar_t* path, DWORD flags) {
    SHFILEINFOW sfi{};
    W7T_SEH_TRY
        if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), flags)) {
            return sfi.hIcon;
        }
    W7T_SEH_CATCH
    W7T_SEH_END
    return nullptr;
}

/* Icone della lista: metodo classico della shell (quello usato da
 * Explorer e dalle shell extension): SHGetFileInfo fornisce l'indice
 * dell'icona gia' risolta per il collegamento (senza freccia di
 * collegamento), IImageList della shell fornisce l'icona HICON alla
 * taglia ESATTA richiesta (16/32/48). Nessuna conversione DIB, nessun
 * ridimensionamento manuale: la stessa immagine che mostra Explorer. */
#ifndef SHIL_LARGE
#define SHIL_LARGE 0
#define SHIL_SMALL 1
#define SHIL_EXTRALARGE 2
#define SHIL_JUMBO 3
#endif
#ifndef ILD_IMAGE
#define ILD_IMAGE 0x0020
#endif

HICON ShellItemIcon(const std::wstring& path, int px) {
    HICON out = nullptr;
    W7T_SEH_TRY {
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                SHGFI_SYSICONINDEX | SHGFI_SMALLICON) != 0) {
            const int shil = (px <= 16) ? SHIL_SMALL :
                             (px <= 32) ? SHIL_LARGE : SHIL_EXTRALARGE;
            IImageList* iml = nullptr;
            if (SUCCEEDED(SHGetImageList(shil, IID_PPV_ARGS(&iml))) &&
                iml != nullptr) {
                iml->GetIcon(sfi.iIcon, ILD_TRANSPARENT | ILD_IMAGE, &out);
                iml->Release();
            }
        }
    } W7T_SEH_CATCH {} W7T_SEH_END
    return out;
}

HFONT MakeFont(int px, bool bold) {
    return CreateFontW(-px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

bool HasBestOf(const std::wstring& q, size_t filtered) {
    return !q.empty() && filtered > 0;
}

/* v2.35: Windows 7 mostra i risultati di ricerca con la parte del nome
 * corrispondente alla query IN GRASSETTO. Disegna il nome in tre
 * segmenti (prima / corrispondenza / dopo) con la stessa dimensione di
 * font; se il nome non ci sta, ripiega sul testo intero con puntini. */
void DrawNameWithMatch(HDC hdc, const std::wstring& name,
                       const std::wstring& query, RECT* r,
                       HFONT fntNormal, HFONT fntBold) {
    size_t pos = std::wstring::npos;
    const std::wstring lq = Lower(query);
    if (!lq.empty()) pos = Lower(name).find(lq);
    if (pos == std::wstring::npos) {
        DrawTextW(hdc, name.c_str(), -1, r,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        return;
    }
    const std::wstring pre  = name.substr(0, pos);
    const std::wstring mid  = name.substr(pos, lq.size());
    const std::wstring post = name.substr(pos + lq.size());
    SIZE sp{}, sm{}, st{};
    HFONT old = static_cast<HFONT>(SelectObject(hdc, fntNormal));
    GetTextExtentPoint32W(hdc, pre.c_str(), static_cast<int>(pre.size()), &sp);
    GetTextExtentPoint32W(hdc, post.c_str(), static_cast<int>(post.size()), &st);
    SelectObject(hdc, fntBold);
    GetTextExtentPoint32W(hdc, mid.c_str(), static_cast<int>(mid.size()), &sm);
    SelectObject(hdc, old);
    if (sp.cx + sm.cx + st.cx > r->right - r->left) {
        DrawTextW(hdc, name.c_str(), -1, r,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        return;
    }
    RECT rc = *r;
    rc.right = rc.left + sp.cx;
    SelectObject(hdc, fntNormal);
    DrawTextW(hdc, pre.c_str(), static_cast<int>(pre.size()), &rc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    rc.left = rc.right; rc.right = rc.left + sm.cx;
    SelectObject(hdc, fntBold);
    DrawTextW(hdc, mid.c_str(), static_cast<int>(mid.size()), &rc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    rc.left = rc.right; rc.right = r->right;
    SelectObject(hdc, fntNormal);
    DrawTextW(hdc, post.c_str(), static_cast<int>(post.size()), &rc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}


HBITMAP MakeDib32(int w, int h, void** bits) {
    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bih),
                            DIB_RGB_COLORS, bits, nullptr, 0);
}
} /* namespace */

AppSearchWindow::~AppSearchWindow() {
    Destroy();
}

bool AppSearchWindow::Create(HINSTANCE hInstance, HWND owner,
                             const uint32_t* argbPixels,
                             int32_t iconW, int32_t iconH) {
    if (m_hWnd) return true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    m_hWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        kClassName, L"App Search",
        WS_POPUP, 0, 0, kTotalWidth, kTotalHeight,
        owner, nullptr, hInstance, nullptr);
    if (!m_hWnd) return false;
    SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // Niente frame DWM ne' AdjustWindowRect: su una finestra LAYERED il
    // bordo non-client fa si' che UpdateLayeredWindow (che imposta la
    // dimensione DELLA SUPERFICIE) rimpicciolisca la finestra a ogni
    // paint ("perde dimensioni da sola"). Il bordo Aero lo disegniamo
    // noi dentro la scena; finestra == area client, misura stabile.

    // icona propria: pixel da C#, colore PREMOLTIPLICATO + maschera 1bpp
    // ricavata dall'alpha (trasparente=1): niente riquadro bianco.
    if (argbPixels != nullptr && iconW > 0 && iconH > 0 && iconW <= 256 && iconH <= 256) {
        void* bits = nullptr;
        HBITMAP color = MakeDib32(iconW, iconH, &bits);
        if (color && bits) {
            const size_t n = static_cast<size_t>(iconW) * iconH;
            uint32_t* dst = static_cast<uint32_t*>(bits);
            for (size_t i = 0; i < n; ++i) {
                const uint32_t px = argbPixels[i];
                const uint32_t a = (px >> 24) & 0xFF;
                const uint32_t rr = (px >> 16) & 0xFF;
                const uint32_t gg = (px >> 8) & 0xFF;
                const uint32_t bb = px & 0xFF;
                dst[i] = (a << 24) |
                         (((rr * a) / 255) << 16) |
                         (((gg * a) / 255) << 8) |
                         ((bb * a) / 255);
            }
            // maschera 1bpp bottom-up: 1 = trasparente
            const int stride = ((iconW + 31) / 32) * 4;
            std::vector<BYTE> maskBits(static_cast<size_t>(stride) * iconH, 0);
            for (int yy = 0; yy < iconH; ++yy) {
                const int srcRow = yy;                    // top-down
                const int dstRow = iconH - 1 - yy;        // bottom-up
                for (int xx = 0; xx < iconW; ++xx) {
                    const uint32_t a = (argbPixels[srcRow * iconW + xx] >> 24) & 0xFF;
                    if (a < 128) {
                        maskBits[dstRow * stride + xx / 8] |=
                            static_cast<BYTE>(0x80 >> (xx & 7));
                    }
                }
            }
            HBITMAP mask = CreateBitmap(iconW, iconH, 1, 1, maskBits.data());
            if (mask) {
                ICONINFO ii{};
                ii.fIcon = TRUE;
                ii.hbmColor = color;
                ii.hbmMask = mask;
                m_searchIcon = CreateIconIndirect(&ii);
                DeleteObject(mask);
            }
            DeleteObject(color);
        }
    }
    if (m_searchIcon) {
        SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(m_searchIcon));
    }

    /* v2.37 punti 10/13: lente e scudi UAC decodificati UNA volta qui,
     * mai durante i ridisegni. Un base64 corrotto non deve crashare. */
    DecodeAssetsOnce();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // v3.9: scansione in background (icone/collegamenti sono lenti):
    // la UI resta reattiva e mostra "Scansione..." finche' pronto.
    // v2.43: il cronometro del rescan periodico parte da QUI, cosi' la
    // prima riapertura utile riscansiona solo dopo kRescanInterval.
    m_lastScanTime = std::chrono::steady_clock::now();
    m_scanThread = std::thread([this] {
        ScanInstalledApps();
        m_scanDone = true;
        if (m_hWnd) PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    });
    return true;
}

void AppSearchWindow::Destroy() {
    m_stopScan = true;
    if (m_scanThread.joinable()) m_scanThread.join();
    if (m_hWnd) {
        KillTimer(m_hWnd, 1);
        SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    for (auto& a : m_allApps) {
        if (a.iconSmall) DestroyIcon(a.iconSmall);
        if (a.iconLarge) DestroyIcon(a.iconLarge);
        if (a.iconPreview) DestroyIcon(a.iconPreview);
    }
    m_allApps.clear();
    if (m_searchIcon) { DestroyIcon(m_searchIcon); m_searchIcon = nullptr; }
    /* v2.37: le risorse incorporate sono semplici vettori di pixel: si
     * svuotano qui (nessun oggetto GDI da rilasciare). */
    m_magPixels.clear();
    m_shieldSmall.clear();
    m_shieldLarge.clear();
    m_openPixels.clear();
    m_folderPixels.clear();
    CoUninitialize();
}

/* v2.37 punti 10/13: decodifica UNA tantum delle risorse incorporate.
 * - Lente: ritagliata sul bounding-box dell'alpha e portata a 14 px di
 *   altezza (punto 8: piccola, 12-14 px) con interpolazione di qualita'.
 * - Scudi UAC: le DUE varianti della mod di riferimento (16x16 nativa e
 *   64x64), senza ritaglio ne' scala: la scelta della variante avviene
 *   al momento del disegno con la stessa regola della mod. */
void AppSearchWindow::DecodeAssetsOnce() {
    try {
        if (m_magPixels.empty()) {
            DecodeEmbeddedPng(kMagnifierPngB64, m_magPixels,
                              m_magW, m_magH, true, 14);
        }
        if (m_shieldSmall.empty()) {
            DecodeEmbeddedPng(kShieldSmallPngB64, m_shieldSmall,
                              m_shieldSmallW, m_shieldSmallH, false, 0);
        }
        if (m_shieldLarge.empty()) {
            DecodeEmbeddedPng(kShieldLargePngB64, m_shieldLarge,
                              m_shieldLargeW, m_shieldLargeH, false, 0);
        }
        /* v2.41: icone azioni, stessa logica delle altre incorporate. */
        if (m_openPixels.empty()) {
            DecodeEmbeddedPng(kOpenActionPngB64, m_openPixels,
                              m_openW, m_openH, false, 0);
        }
        if (m_folderPixels.empty()) {
            DecodeEmbeddedPng(kOpenFolderPngB64, m_folderPixels,
                              m_folderW, m_folderH, false, 0);
        }
    } catch (...) { /* nessuna icona e' meglio di un crash */ }
}

/* v2.43: rescan periodico dell'indice (Start Menu puo' cambiare mentre
 * il pannello resta chiuso per ore: senza questo, un programma appena
 * installato non comparirebbe finche' non si riavvia la taskbar).
 * Va chiamato SOLO da Show(), quindi non gira mai mentre il pannello
 * e' aperto: l'utente non vede mai un rescan mentre sta guardando i
 * risultati, solo la prossima volta che riapre la lente se l'indice
 * ha piu' di kRescanInterval. Non e' un timer che gira sempre in
 * background: se la ricerca non si riapre, non scansiona piu' nulla. */
void AppSearchWindow::MaybeRescanIfStale() {
    using namespace std::chrono;

    if (!m_scanDone) {
        return; // scansione (iniziale o precedente) ancora in corso
    }
    if (m_scanThread.joinable()) {
        m_scanThread.join(); // raccogli il thread finito prima di riusarlo
    }

    auto now = steady_clock::now();
    if (now - m_lastScanTime < kRescanInterval) {
        return; // indice ancora fresco, niente da fare
    }

    m_lastScanTime = now;
    m_scanDone = false;
    m_stopScan = false;
    m_scanThread = std::thread([this] {
        ScanInstalledApps();
        m_scanDone = true;
        if (m_hWnd) PostMessageW(m_hWnd, WM_APP + 1, 0, 0);
    });
}

/* Scansione RICORSIVA di Start Menu\Programs (utente + comune), come
 * Open-Shell: entra nelle sottocartelle (Accessori, Windows Accessories,
 * Strumenti di sistema...) cosi' Blocco note/Paint/Calcolatrice
 * compaiono. */
void AppSearchWindow::ScanInstalledApps() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<AppEntry> local;
    /* v2.37 bug #3: dedupe O(log n) per chiave normalizzata (vedi sotto). */
    std::set<std::wstring> seenTargets;
    /* v2.37 bug #2: tetto di sicurezza sulla scansione ricorsiva. Un
     * menu Start enorme (o una giunzione/symlink che forma un ciclo)
     * non deve produrre un elenco senza fine: 500 voci bastano e
     * avanzano per la ricerca del menu Start. */
    constexpr size_t kMaxEntries = 500;
    try {
    std::function<void(const std::wstring&, int)> scanDir =
        [&](const std::wstring& dir, int depth) {
        if (m_stopScan) return;
        if (depth > 6) return;
        if (local.size() >= kMaxEntries) return;   /* bug #2 */
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring lnkPath = dir + L"\\" + fd.cFileName;
                std::wstring target;

                IShellLinkW* link = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
                    IPersistFile* pf = nullptr;
                    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
                        if (SUCCEEDED(pf->Load(lnkPath.c_str(), STGM_READ))) {
                            wchar_t t[MAX_PATH]{};
                            WIN32_FIND_DATAW dummy;
                            link->GetPath(t, MAX_PATH, &dummy, 0);
                            target = t;
                        }
                        pf->Release();
                    }
                    link->Release();
                }
                /* v2.30: non scartare i collegamenti senza percorso
                 * target (app pacchettizzate): l'icona arriva comunque da
                 * IShellItemImageFactory e l'avvio via .lnk funziona. */

                AppEntry e;
                e.name = fd.cFileName;
                if (e.name.size() > 4) e.name.erase(e.name.size() - 4);
                e.targetPath = target;
                e.lnkPath = lnkPath;
                /* v2.37 bug #1: precalcolo UNA volta le chiavi di ricerca
                 * (prima ApplyFilter ricalcolava Lower() a ogni confronto). */
                e.nameLower = Lower(e.name);
                e.targetNameLower = TargetBaseNameNoExt(target);
                if (m_stopScan) return;
                if (local.size() >= kMaxEntries) return;   /* bug #2 */

                /* v2.37 bug #3: dedupe per percorso target normalizzato
                 * (minuscolo, variabili espanse), o percorso .lnk quando il
                 * target non e' risolvibile (app pacchettizzate). std::set:
                 * O(log n) invece del precedente confronto O(n) per ogni
                 * voce; resta valida la PRIMA occorrenza trovata. */
                std::wstring norm = target;
                if (!norm.empty()) {
                    wchar_t exp[MAX_PATH]{};
                    ExpandEnvironmentStringsW(norm.c_str(), exp, MAX_PATH);
                    norm = Lower(exp);
                } else {
                    norm = Lower(lnkPath);
                }
                if (!seenTargets.insert(norm).second) continue;

                /* Icona come la shell (niente freccia), con ripiego sul
                 * resolver condiviso dei pin. v2.37 punto 14: IconGuard
                 * (RAII) cosi' le icone non trapelano in NESSUN percorso
                 * d'uscita (eccezione, stop richiesto, allocazione
                 * fallita); blocco SEH perche' tocchiamo risorse esterne
                 * (shell/COM). */
                W7T_SEH_TRY {
                    IconGuard gLarge(ShellItemIcon(lnkPath, 32));
                    if (!gLarge.h) {
                        gLarge.h = ResolveAppIcon(lnkPath.c_str(),
                                                  target.c_str(), true);
                    }
                    IconGuard gSmall(ShellItemIcon(lnkPath, 16));
                    if (!gSmall.h) {
                        gSmall.h = ResolveAppIcon(lnkPath.c_str(),
                                                  target.c_str(), false);
                    }
                    IconGuard gPreview(ShellItemIcon(lnkPath, 48));
                    if (!gPreview.h) {
                        gPreview.h = ResolveAppIcon(lnkPath.c_str(),
                                                    target.c_str(), true);
                    }
                    e.iconLarge   = gLarge.release();
                    e.iconSmall   = gSmall.release();
                    e.iconPreview = gPreview.release();
                } W7T_SEH_CATCH {
                    e.iconLarge = e.iconSmall = e.iconPreview = nullptr;
                } W7T_SEH_END
                local.push_back(std::move(e));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        // ricorri nelle sottocartelle
        HANDLE hDir = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (hDir != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(fd.cFileName, L".") == 0 ||
                    wcscmp(fd.cFileName, L"..") == 0) continue;
                scanDir(dir + L"\\" + fd.cFileName, depth + 1);
            } while (FindNextFileW(hDir, &fd));
            FindClose(hDir);
        }
    };

    wchar_t path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, path)))
        scanDir(path, 0);
    path[0] = 0;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, path)))
        scanDir(path, 0);
    } catch (...) { /* elenco parziale meglio di nessun elenco */ }
    if (!m_stopScan) {
        std::lock_guard<std::mutex> lk(m_scanMutex);
        /* v2.37 bug #4: se l'elenco esisteva gia' (rescan), le vecchie
         * icone vanno distrutte PRIMA di sostituirle, altrimenti
         * trapelano a ogni nuova scansione. */
        for (auto& a : m_allApps) {
            if (a.iconSmall) DestroyIcon(a.iconSmall);
            if (a.iconLarge) DestroyIcon(a.iconLarge);
            if (a.iconPreview) DestroyIcon(a.iconPreview);
        }
        m_allApps = std::move(local);
    }
    CoUninitialize();
}

void AppSearchWindow::ApplyFilter(const std::wstring& query) {
    m_query = query;
    m_filtered.clear();
    m_scroll = 0;
    const std::wstring q = Lower(query);

    /* v2.37 punto 6: query multi-parola, le parole possono comparire in
     * QUALSIASI ordine. */
    std::vector<std::wstring> words;
    {
        size_t start = 0;
        for (size_t i = 0; i <= q.size(); ++i) {
            const bool sep = (i == q.size()) || q[i] == L' ';
            if (!sep) continue;
            if (i > start) words.push_back(q.substr(start, i - start));
            start = i + 1;
        }
    }

    /* v2.37 punti 1/5: corrispondenze ESATTE (tutte le parole presenti
     * nel nome oppure nel nome del file target senza estensione) e, solo
     * dopo, corrispondenze "morbide" per errori di battitura. Le esatte
     * precedono SEMPRE le morbide. */
    struct Hit { int idx; bool prefix; };
    std::vector<Hit> exact;
    std::vector<Hit> fuzzy;

    std::lock_guard<std::mutex> lk(m_scanMutex);
    for (size_t i = 0; i < m_allApps.size(); ++i) {
        const AppEntry& e = m_allApps[i];
        if (q.empty()) {
            exact.push_back({ static_cast<int>(i), false });
            continue;
        }
        bool allWords = true;
        for (const std::wstring& w : words) {
            if (e.nameLower.find(w) == std::wstring::npos &&
                e.targetNameLower.find(w) == std::wstring::npos) {
                allWords = false;
                break;
            }
        }
        if (allWords) {
            /* bug #1: la query ora confronta anche il nome del target
             * ("notepad" trova "Blocco note.lnk" -> notepad.exe). */
            exact.push_back({ static_cast<int>(i),
                              e.nameLower.rfind(q, 0) == 0 });
            continue;
        }
        /* punto 5: tolleranza errori di battitura SOLO come seconda
         * scelta: una parola >= 4 caratteri a distanza di Levenshtein 1,
         * oppure sotto-sequenza in ordine. Limite: poche voci morbide per
         * non affollare l'elenco. */
        if (words.size() == 1 && fuzzy.size() < 10 &&
            (FuzzyMatchText(e.nameLower, q) ||
             FuzzyMatchText(e.targetNameLower, q))) {
            fuzzy.push_back({ static_cast<int>(i), false });
        }
    }

    /* v2.35+v2.37: ordine come Windows 7: prima i nomi che INIZIANO con
     * la query, poi gli altri risultati esatti, poi quelli "morbidi";
     * alfabetico dentro ciascun gruppo. */
    std::stable_sort(exact.begin(), exact.end(),
        [&](const Hit& a, const Hit& b) {
            if (a.prefix != b.prefix) return a.prefix;
            return m_allApps[a.idx].nameLower < m_allApps[b.idx].nameLower;
        });
    std::stable_sort(fuzzy.begin(), fuzzy.end(),
        [&](const Hit& a, const Hit& b) {
            return m_allApps[a.idx].nameLower < m_allApps[b.idx].nameLower;
        });
    m_filtered.reserve(exact.size() + fuzzy.size());
    for (const Hit& h : exact) m_filtered.push_back(h.idx);
    for (const Hit& h : fuzzy) m_filtered.push_back(h.idx);
    m_selectedRow = m_filtered.empty() ? -1 : 0;
}

void AppSearchWindow::SelectRow(int index) {
    if (index < 0 || index >= static_cast<int>(m_filtered.size())) return;
    m_selectedRow = index;
    if (m_hWnd) InvalidateRect(m_hWnd, nullptr, FALSE);
}

/* v2.37 punto 12: la finestra di ricerca e' LAYERED (UpdateLayeredWindow),
 * quindi non puo' ospitare le scrollbar native di Windows (WS_VSCROLL non
 * viene disegnato dal tema su una superficie composta da noi). La
 * scrollbar e' DISEGNATA in stile Windows 7 (binario + frecce + cursore)
 * e riusa la stessa geometria delle righe gia' usata dal rendering e dal
 * wheel: nessuna logica di scroll duplicata. */
AppSearchWindow::ScrollGeom AppSearchWindow::ComputeScrollGeom() const {
    ScrollGeom g;
    const bool best = HasBestOf(m_query, m_filtered.size());
    g.firstRow = best ? 1 : 0;
    g.totalRows = static_cast<int>(m_filtered.size()) - g.firstRow;
    const int rowsTop = kListTop + 22 + (best ? 46 + 22 : 0);
    const int rowsBottom = kListTop + kListH;
    g.visRows = (rowsBottom - rowsTop) / kRowHeight;
    if (g.totalRows <= g.visRows || g.visRows <= 0) return g;   /* non serve */
    g.maxScroll = g.totalRows - g.visRows;
    g.visible = true;
    g.track = RECT{ kLeftWidth - 18, rowsTop, kLeftWidth - 4, rowsBottom };
    constexpr int kBtn = 16;
    g.upBtn = RECT{ g.track.left, g.track.top, g.track.right, g.track.top + kBtn };
    g.dnBtn = RECT{ g.track.left, g.track.bottom - kBtn, g.track.right, g.track.bottom };
    const int innerTop = g.track.top + kBtn;
    const int innerBot = g.track.bottom - kBtn;
    const int innerH = innerBot - innerTop;
    int thumbH = innerH * g.visRows / g.totalRows;
    if (thumbH < 20) thumbH = 20;
    if (thumbH > innerH) thumbH = innerH;
    const int thumbY = innerTop +
        (g.maxScroll > 0 ? (innerH - thumbH) * m_scroll / g.maxScroll : 0);
    g.thumb = RECT{ g.track.left + 1, thumbY, g.track.right - 1, thumbY + thumbH };
    return g;
}

void AppSearchWindow::ScrollTo(int pos) {
    const ScrollGeom g = ComputeScrollGeom();
    const int maxPos = g.visible ? g.maxScroll : 0;
    m_scroll = std::min(maxPos, std::max(0, pos));
}

void AppSearchWindow::ClampScroll() {
    const int keep = m_scroll;
    ScrollTo(keep);
}

/* v2.37 punto 9: archivia i lanci come "file recenti" (indici in
 * m_allApps, piu' recente per primo, massimo 8, senza duplicati). */
void AppSearchWindow::AddRecentFile(int appIndex) {
    if (appIndex < 0 || appIndex >= static_cast<int>(m_allApps.size())) return;
    for (size_t i = 0; i < m_recentFiles.size(); ++i) {
        if (m_recentFiles[i] == appIndex) {
            m_recentFiles.erase(m_recentFiles.begin() + i);
            break;
        }
    }
    m_recentFiles.insert(m_recentFiles.begin(), appIndex);
    if (m_recentFiles.size() > 8) m_recentFiles.resize(8);
}

void AppSearchWindow::Show(int anchorX, int anchorY) {
    if (!m_hWnd) return;
    /* v2.43: se l'indice ha piu' di kRescanInterval si riscansiona in
     * background: il pannello si apre SUBITO con i risultati vecchi (non
     * si aspetta nulla) e si aggiorna da solo quando arriva WM_APP + 1. */
    MaybeRescanIfStale();
    /* v2.37: stato interaction della sessione precedente non sopravvive. */
    m_hasSelection = false;
    m_hoverMag = false;
    m_hoverClear = false;
    m_scrollDragging = false;
    SetWindowPos(m_hWnd, HWND_TOPMOST,
                 anchorX, anchorY - kTotalHeight - 6,
                 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    ApplyFilter(L"");
    InvalidateRect(m_hWnd, nullptr, TRUE);
    SetFocus(m_hWnd);
    SetTimer(m_hWnd, 1, 538, nullptr);
}

void AppSearchWindow::Hide() {
    if (m_hWnd) {
        KillTimer(m_hWnd, 1);
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

int AppSearchWindow::HitTestRow(POINT p) const {
    if (p.x < 0 || p.x >= kLeftWidth) return -1;
    const bool best = HasBestOf(m_query, m_filtered.size());

    if (best && p.y >= kListTop + 22 && p.y < kListTop + 22 + 46) {
        return 0;
    }
    const int rowsTop = kListTop + (best ? 22 + 46 + 22 : 22);
    if (p.y < rowsTop || p.y >= kListTop + kListH) return -1;
    const int first = best ? 1 : 0;
    int row = first + m_scroll + (p.y - rowsTop) / kRowHeight;
    return (row >= first && row < static_cast<int>(m_filtered.size())) ? row : -1;
}

int AppSearchWindow::HitTestOption(POINT p) const {
    if (p.x < kLeftWidth || m_selectedRow < 0) return -1;
    int localY = p.y - (kHeaderH + 8);
    if (localY < 0) return -1;
    int idx = localY / kOptionHeight;
    return (idx >= 0 && idx < 3) ? idx : -1;
}

bool AppSearchWindow::HitTestClear(POINT p) const {
    const int eTop = kListTop + kListH + 8;
    RECT r{ kLeftWidth - 32, eTop + 5, kLeftWidth - 14, eTop + 25 };
    return PtInRect(&r, p) != FALSE && !m_query.empty();
}

/* v2.37 punto 11: terza zona hover, la lente d'ingrandimento nella
 * casella di ricerca (stessa area usata dal disegno). */
bool AppSearchWindow::HitTestMagnifier(POINT p) const {
    const int eTop = kListTop + kListH + 8;
    RECT r{ 16, eTop + 4, 38, eTop + 26 };
    return PtInRect(&r, p) != FALSE;
}

void AppSearchWindow::RunOption(int optionIndex) {
    try {
    if (m_selectedRow < 0) return;
    std::lock_guard<std::mutex> lk(m_scanMutex);
    if (m_selectedRow >= static_cast<int>(m_filtered.size())) return;
    const int appIndex = m_filtered[m_selectedRow];
    const AppEntry& app = m_allApps[appIndex];

    switch (optionIndex) {
    case 0: {
        /* v2.35: le app pacchettizzate non hanno target risolto: si
         * avvia direttamente il collegamento (come gia' previsto in
         * v2.30 ma il percorso .lnk non veniva conservato). */
        const std::wstring& launch =
            app.targetPath.empty() ? app.lnkPath : app.targetPath;
        ShellExecuteW(nullptr, L"open", launch.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
        AddRecentFile(appIndex);   /* v2.37 punto 9 */
        break;
    }
    case 1:
        ShellExecuteW(nullptr, L"runas", app.targetPath.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
        AddRecentFile(appIndex);   /* v2.37 punto 9 */
        break;
    case 2: {
        std::wstring arg = L"/select,\"" + app.targetPath + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe",
                      arg.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    }
    Hide();
    } catch (...) { /* mai propagare */ }
}

/* v2.35: come in Windows 7, Alt+Invio apre le proprieta' del
 * collegamento selezionato (il dialogo Proprietà del collegamento). */
void AppSearchWindow::ShowPropertiesOfSelected() {
    try {
        if (m_selectedRow < 0) return;
        std::wstring path;
        {
            std::lock_guard<std::mutex> lk(m_scanMutex);
            if (m_selectedRow >= static_cast<int>(m_filtered.size())) return;
            const AppEntry& app = m_allApps[m_filtered[m_selectedRow]];
            path = app.lnkPath.empty() ? app.targetPath : app.lnkPath;
        }
        if (path.empty()) return;
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"properties";
        sei.lpFile = path.c_str();
        sei.nShow = SW_SHOWNORMAL;
        W7T_SEH_TRY
            ShellExecuteExW(&sei);
        W7T_SEH_CATCH
        W7T_SEH_END
    } catch (...) { /* mai propagare */ }
}

/* ---------------- scena (colori o maschera alpha) ---------------- */
/* mask=false: colori reali. mask=true: scala di grigi dove il valore e'
 * l'alpha finale del pixel (fondo = gradiente kAlphaTop..kAlphaBottom,
 * contenuto opaco = bianco). Lo stesso codice GDI anti-aliasa entrambe,
 * cosi' i bordi del testo hanno l'alpha giusto. */

void AppSearchWindow::RenderScene(HDC hdc, uint32_t* sceneBits,
                                    int W, int H, bool mask) {
    std::lock_guard<std::mutex> lk(m_scanMutex);
    auto grayAt = [&](int y) -> int {
        return kAlphaTop + (kAlphaBottom - kAlphaTop) * y / (H > 1 ? H - 1 : 1);
    };
    auto col = [&](COLORREF c) -> COLORREF {
        return mask ? RGB(0xFF, 0xFF, 0xFF) : c;
    };

    // sfondo: gradiente blu (colori) o gradiente di alpha (maschera)
    {
        TRIVERTEX vtx[2] = {};
        vtx[0].x = 0; vtx[0].y = 0;
        if (mask) {
            vtx[0].Red = vtx[0].Green = vtx[0].Blue =
                static_cast<COLOR16>(grayAt(0)) << 8;
        } else {
            vtx[0].Red = static_cast<COLOR16>(GetRValue(kBgTop)) << 8;
            vtx[0].Green = static_cast<COLOR16>(GetGValue(kBgTop)) << 8;
            vtx[0].Blue = static_cast<COLOR16>(GetBValue(kBgTop)) << 8;
        }
        vtx[1].x = W; vtx[1].y = H;
        if (mask) {
            vtx[1].Red = vtx[1].Green = vtx[1].Blue =
                static_cast<COLOR16>(grayAt(H - 1)) << 8;
        } else {
            vtx[1].Red = static_cast<COLOR16>(GetRValue(kBgBottom)) << 8;
            vtx[1].Green = static_cast<COLOR16>(GetGValue(kBgBottom)) << 8;
            vtx[1].Blue = static_cast<COLOR16>(GetBValue(kBgBottom)) << 8;
        }
        GRADIENT_RECT gf{ 0, 1 };
        GradientFill(hdc, vtx, 2, &gf, 1, GRADIENT_FILL_RECT_V);
    }
    // bordo "Aero" disegnato dentro (1 px chiaro, opaco)
    {
        RECT b{ 0, 0, W, H };
        HBRUSH hb = CreateSolidBrush(col(kSelEdge));
        FrameRect(hdc, &b, hb);
        DeleteObject(hb);
    }
    SetBkMode(hdc, TRANSPARENT);
    HFONT font     = MakeFont(13, false);
    HFONT fontBold = MakeFont(12, true);
    HFONT fontMatch = MakeFont(13, true);   /* grassetto stessa taglia */
    HFONT oldFont  = static_cast<HFONT>(SelectObject(hdc, font));

    const bool best = HasBestOf(m_query, m_filtered.size());

    auto drawHeader = [&](const wchar_t* text, int y) {
        HFONT ob = static_cast<HFONT>(SelectObject(hdc, fontBold));
        RECT r{ 14, y, kLeftWidth - 10, y + 20 };
        SetTextColor(hdc, col(kTextDim));
        DrawTextW(hdc, text, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(hdc, ob);
    };
    auto makePen = [&](COLORREF c) {
        return CreatePen(PS_SOLID, 1, mask ? RGB(0xFF, 0xFF, 0xFF) : c);
    };
    auto vline = [&](int x, int y0, int y1) {
        HPEN pen = makePen(kSep);
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, x, y0, nullptr);
        LineTo(hdc, x, y1);
        SelectObject(hdc, op);
        DeleteObject(pen);
    };
    auto hline = [&](int y, int x0, int x1) {
        HPEN pen = makePen(kSep);
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, x0, y, nullptr);
        LineTo(hdc, x1, y);
        SelectObject(hdc, op);
        DeleteObject(pen);
    };
    auto drawIcon = [&](int x, int y, HICON ic, int w, int h) {
        /* Icone 32bpp: blend per-pixel nei buffer di scena (DrawIconEx
         * su memory DC produce rettangoli bianchi). Il blit disegna
         * alla taglia nativa dell'icona (16/32/48 richieste alla shell). */
        if (sceneBits != nullptr &&
            BlitIcon32(sceneBits, W, H, ic, x, y, mask)) {
            return;
        }
        if (mask) {
            const int gy = y + h / 2;
            const int g = grayAt(gy > H - 1 ? H - 1 : gy);
            SafeDrawIconMask(hdc, x, y, ic, w, h, RGB(g, g, g));
        } else {
            SafeDrawIconEx(hdc, x, y, ic, w, h);
        }
    };

    /* ---------- colonna sinistra ---------- */
    int yCur = kListTop;

    if (best) {
        drawHeader(L"Corrispondenza migliore", yCur);
        yCur += 22;
        const AppEntry& app = m_allApps[m_filtered[0]];
        RECT selR{ 10, yCur, kLeftWidth - 10, yCur + 46 };
        if (m_selectedRow == 0) {
            if (mask) {
                HBRUSH b = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
                FillRect(hdc, &selR, b);
                DeleteObject(b);
            } else {
                TRIVERTEX vtx[2] = {};
                vtx[0].x = selR.left; vtx[0].y = selR.top;
                vtx[0].Red = static_cast<COLOR16>(GetRValue(kSelTop)) << 8;
                vtx[0].Green = static_cast<COLOR16>(GetGValue(kSelTop)) << 8;
                vtx[0].Blue = static_cast<COLOR16>(GetBValue(kSelTop)) << 8;
                vtx[1].x = selR.right; vtx[1].y = selR.bottom;
                vtx[1].Red = static_cast<COLOR16>(GetRValue(kSelBot)) << 8;
                vtx[1].Green = static_cast<COLOR16>(GetGValue(kSelBot)) << 8;
                vtx[1].Blue = static_cast<COLOR16>(GetBValue(kSelBot)) << 8;
                GRADIENT_RECT gf{ 0, 1 };
                GradientFill(hdc, vtx, 2, &gf, 1, GRADIENT_FILL_RECT_V);
            }
            HBRUSH eb = CreateSolidBrush(col(kSelEdge));
            FrameRect(hdc, &selR, eb);
            DeleteObject(eb);
        }
        if (app.iconLarge) drawIcon(16, yCur + 7, app.iconLarge, 32, 32);
        SetTextColor(hdc, col(kText));
        RECT tr{ 56, yCur + 4, kLeftWidth - 12, yCur + 25 };
        HFONT ob = static_cast<HFONT>(SelectObject(hdc, fontBold));
        DrawTextW(hdc, app.name.c_str(), -1, &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        SelectObject(hdc, ob);
        RECT sr{ 56, yCur + 25, kLeftWidth - 12, yCur + 43 };
        SetTextColor(hdc, col(kTextDim));
        DrawTextW(hdc, L"App", -1, &sr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        yCur += 46;
    }

    if (!m_scanDone && m_allApps.empty() && m_query.empty()) {
        SetTextColor(hdc, col(kTextDim));
        RECT lr{ 10, kListTop + 60, kLeftWidth - 10, kListTop + 140 };
        DrawTextW(hdc, L"Scansione applicazioni...", -1, &lr,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        yCur += 26;
    }
    if (!m_filtered.empty() || m_query.empty()) {
        drawHeader(L"Programmi", yCur);
    }
    yCur += 22;
    const int rowsTop = yCur;
    const int visRows = (kListTop + kListH - rowsTop) / kRowHeight;
    const int first = best ? 1 : 0;
    const int last = std::min<int>(static_cast<int>(m_filtered.size()),
                                   first + m_scroll + visRows);
    for (int row = first + m_scroll; row < last; ++row) {
        const AppEntry& app = m_allApps[m_filtered[row]];
        const int yy = rowsTop + (row - first - m_scroll) * kRowHeight;
        RECT rowR{ 10, yy, kLeftWidth - 10, yy + kRowHeight };
        if (row == m_selectedRow) {
            HBRUSH b = CreateSolidBrush(col(kHover));
            FillRect(hdc, &rowR, b);
            DeleteObject(b);
            HBRUSH eb = CreateSolidBrush(col(kSelEdge));
            FrameRect(hdc, &rowR, eb);
            DeleteObject(eb);
        }
        SetTextColor(hdc, col(kText));
        if (app.iconLarge) drawIcon(14, yy + 3, app.iconLarge, 32, 32);
        else if (app.iconSmall) drawIcon(22, yy + 11, app.iconSmall, 16, 16);
        RECT tr{ 56, yy, kLeftWidth - 14, yy + kRowHeight };
        DrawNameWithMatch(hdc, app.name, m_query, &tr, font, fontMatch);
    }
    if (m_filtered.empty() && !m_query.empty()) {
        SetTextColor(hdc, col(kTextDim));
        RECT nr{ 14, rowsTop + 6, kLeftWidth - 14, rowsTop + 66 };
        DrawTextW(hdc, L"Nessun elemento corrisponde alla ricerca.", -1,
                  &nr, DT_WORDBREAK | DT_LEFT);
    }

    /* v2.37 punto 12: scrollbar in stile Windows 7, DISEGNATA (la
     * finestra layered non puo' usare WS_VSCROLL). Compare solo quando
     * l'elenco supera le righe visibili; frecce, binario e cursore. */
    {
        const ScrollGeom sg = ComputeScrollGeom();
        if (sg.visible) {
            HBRUSH tb = CreateSolidBrush(col(RGB(0xA8, 0xC6, 0xE4)));
            FillRect(hdc, &sg.track, tb);
            DeleteObject(tb);
            HBRUSH te = CreateSolidBrush(col(kSelEdge));
            FrameRect(hdc, &sg.track, te);
            DeleteObject(te);
            HBRUSH th = CreateSolidBrush(col(RGB(0xEC, 0xF4, 0xFC)));
            FillRect(hdc, &sg.thumb, th);
            DeleteObject(th);
            HBRUSH thE = CreateSolidBrush(col(kSelEdge));
            FrameRect(hdc, &sg.thumb, thE);
            DeleteObject(thE);
            auto arrow = [&](const RECT& r, bool up) {
                const int cx = (r.left + r.right) / 2;
                const int cy = (r.top + r.bottom) / 2;
                POINT pts[3];
                if (up) {
                    pts[0] = POINT{ cx, cy - 3 };
                    pts[1] = POINT{ cx - 4, cy + 3 };
                    pts[2] = POINT{ cx + 4, cy + 3 };
                } else {
                    pts[0] = POINT{ cx, cy + 3 };
                    pts[1] = POINT{ cx - 4, cy - 3 };
                    pts[2] = POINT{ cx + 4, cy - 3 };
                }
                HBRUSH ab = CreateSolidBrush(col(kEditEdge));
                HBRUSH ob = static_cast<HBRUSH>(SelectObject(hdc, ab));
                Polygon(hdc, pts, 3);
                SelectObject(hdc, ob);
                DeleteObject(ab);
            };
            arrow(sg.upBtn, true);
            arrow(sg.dnBtn, false);
        }
    }

    /* ---------- separatore verticale ---------- */
    vline(kLeftWidth, 8, H - 8);

    /* ---------- pannello destro ---------- */
    if (m_selectedRow >= 0) {
        const AppEntry& app = m_allApps[m_filtered[m_selectedRow]];
        HICON big = app.iconPreview ? app.iconPreview
                    : (app.iconLarge ? app.iconLarge : app.iconSmall);
        if (big) drawIcon(kLeftWidth + (kRightWidth - 48) / 2, 16, big, 48, 48);

        SetTextColor(hdc, col(kText));
        RECT nr{ kLeftWidth + 10, 70, W - 10, 90 };
        HFONT ob = static_cast<HFONT>(SelectObject(hdc, fontBold));
        DrawTextW(hdc, app.name.c_str(), -1, &nr,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
        SelectObject(hdc, ob);
        RECT ar{ kLeftWidth + 10, 88, W - 10, 102 };
        SetTextColor(hdc, col(kTextDim));
        DrawTextW(hdc, L"App", -1, &ar, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        hline(kHeaderH + 2, kLeftWidth + 14, W - 14);

        const wchar_t* labels[3] = {
            L"Apri",
            L"Esegui come amministratore",
            L"Apri percorso file"
        };
        int oy = kHeaderH + 8;
        for (int i = 0; i < 3; ++i) {
            RECT itemRc{ kLeftWidth + 10, oy, W - 10, oy + kOptionHeight };
            if (i == m_hoverOption) {
                HBRUSH b = CreateSolidBrush(col(kHover));
                FillRect(hdc, &itemRc, b);
                DeleteObject(b);
            }
            SetTextColor(hdc, col(kText));
            RECT textRc = itemRc;
            if (i == 1) {
                /* v2.37 punto 13: scudo UAC accanto a "Esegui come
                 * amministratore". Due varianti incorporate (16x16 nativa
                 * e 64x64) con la STESSA regola di selezione della mod
                 * MIT di riferimento: taglia di disegno <= 16 px ->
                 * variante nativa piccola; altrimenti la grande scalata.
                 * (Il layout della ricerca e' a taglia fissa, quindi in
                 * pratica vince la 16x16 nativa; la 64x64 resta pronta.) */
                const int iconSize = 16;
                const int sx = kLeftWidth + 12;
                const int sy = oy + (kOptionHeight - iconSize) / 2;
                if (iconSize <= 16 && !m_shieldSmall.empty() && sceneBits) {
                    BlitArgb(sceneBits, W, H, m_shieldSmall.data(),
                             m_shieldSmallW, m_shieldSmallH, sx, sy, mask);
                } else if (!m_shieldLarge.empty() && sceneBits) {
                    BlitArgbScaled(sceneBits, W, H, m_shieldLarge.data(),
                                   m_shieldLargeW, m_shieldLargeH,
                                   sx, sy, iconSize, iconSize, mask);
                }
                textRc.left += iconSize + 6;
            } else if ((i == 0 || i == 2) && sceneBits) {
                /* v2.41: icone incorporate per "Apri" (openicon.png) e
                 * "Apri percorso file" (image-2.png): stessa logica di
                 * blit delle altre icone (16 px, centrate sulla riga). */
                const std::vector<uint32_t>& px =
                    (i == 0) ? m_openPixels : m_folderPixels;
                const int pw = (i == 0) ? m_openW : m_folderW;
                const int ph = (i == 0) ? m_openH : m_folderH;
                if (!px.empty()) {
                    const int iconSize = 16;
                    const int sx = kLeftWidth + 12;
                    const int sy = oy + (kOptionHeight - iconSize) / 2;
                    BlitArgbScaled(sceneBits, W, H, px.data(), pw, ph,
                                   sx, sy, iconSize, iconSize, mask);
                    textRc.left += iconSize + 6;
                }
            }
            DrawTextW(hdc, labels[i], -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            oy += kOptionHeight;
        }

        /* v2.37 punto 9: intestazione "File recenti" E separatore
         * spariscono del tutto quando l'elenco dei recenti e' vuoto: il
         * pannello destro si adatta, niente spazio vuoto. */
        if (!m_recentFiles.empty()) {
            hline(oy + 6, kLeftWidth + 14, W - 14);
            RECT fr{ kLeftWidth + 10, oy + 12, W - 10, oy + 30 };
            HFONT ob2 = static_cast<HFONT>(SelectObject(hdc, fontBold));
            SetTextColor(hdc, col(kTextDim));
            DrawTextW(hdc, L"File recenti", -1, &fr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            SelectObject(hdc, ob2);
            int ry = oy + 32;
            for (int idxApp : m_recentFiles) {
                if (idxApp < 0 || idxApp >= static_cast<int>(m_allApps.size())) continue;
                if (ry + 24 > H - 8) break;   /* mai uscire dal pannello */
                const AppEntry& ra = m_allApps[idxApp];
                if (ra.iconSmall) drawIcon(kLeftWidth + 14, ry + 4, ra.iconSmall, 16, 16);
                SetTextColor(hdc, col(kText));
                RECT rr{ kLeftWidth + 36, ry, W - 12, ry + 24 };
                DrawTextW(hdc, ra.name.c_str(), -1, &rr,
                          DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
                ry += 24;
            }
        }
    }

    /* ---------- casella di ricerca in basso ---------- */
    const int eTop = kListTop + kListH + 8;
    RECT editR{ 12, eTop, kLeftWidth - 12, eTop + 30 };
    HBRUSH wb = CreateSolidBrush(col(kEditBg));
    FillRect(hdc, &editR, wb);
    DeleteObject(wb);
    HBRUSH eb = CreateSolidBrush(col(kEditEdge));
    FrameRect(hdc, &editR, eb);
    DeleteObject(eb);
    /* v2.37 punti 10/11: lente d'ingrandimento INCORPORATA (base64,
     * decodificata una volta all'avvio, ~14 px: punto 8), terza zona
     * hover col solito colore kHover. Niente piu' indici di icona da
     * shell32. */
    if (m_hoverMag) {
        RECT mr{ editR.left + 4, eTop + 4, editR.left + 26, eTop + 26 };
        HBRUSH b = CreateSolidBrush(col(kHover));
        FillRect(hdc, &mr, b);
        DeleteObject(b);
    }
    if (!m_magPixels.empty() && sceneBits) {
        const int mx = editR.left + 8 + (16 - m_magW) / 2;
        const int my = eTop + 7 + (16 - m_magH) / 2;
        BlitArgb(sceneBits, W, H, m_magPixels.data(), m_magW, m_magH,
                 mx, my, mask);
    } else if (m_searchIcon) {
        /* ripiego: icona-finestra ricevuta da C# */
        drawIcon(editR.left + 7, eTop + 7, m_searchIcon, 16, 16);
    }
    SIZE ts{};
    GetTextExtentPoint32W(hdc, m_query.c_str(),
                          static_cast<int>(m_query.size()), &ts);
    /* v2.37 punto 7: evidenzia la selezione (Ctrl+A) come un vero
     * controllo EDIT: fondo blu e testo bianco. */
    RECT txR{ editR.left + 28, eTop, editR.right - 26, eTop + 30 };
    if (m_hasSelection && !m_query.empty()) {
        RECT selR{ txR.left - 2, eTop + 4,
                   std::min<int>(txR.left + ts.cx + 2, editR.right - 4),
                   eTop + 26 };
        HBRUSH b = CreateSolidBrush(mask ? RGB(0xFF, 0xFF, 0xFF)
                                         : RGB(0x33, 0x99, 0xFF));
        FillRect(hdc, &selR, b);
        DeleteObject(b);
        SetTextColor(hdc, mask ? RGB(0xFF, 0xFF, 0xFF) : RGB(0xFF, 0xFF, 0xFF));
    } else {
        SetTextColor(hdc, mask ? RGB(0xFF, 0xFF, 0xFF) : kEditText);
    }
    DrawTextW(hdc, m_query.c_str(), -1, &txR, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    if (m_caretOn) {
        const int cx = std::min<int>(editR.left + 28 + ts.cx, editR.right - 28);
        HPEN cp = CreatePen(PS_SOLID, 1, mask ? RGB(0xFF, 0xFF, 0xFF) : kEditText);
        HPEN op = static_cast<HPEN>(SelectObject(hdc, cp));
        MoveToEx(hdc, cx, eTop + 7, nullptr);
        LineTo(hdc, cx, eTop + 23);
        SelectObject(hdc, op);
        DeleteObject(cp);
    }
    if (!m_query.empty()) {
        RECT cr{ kLeftWidth - 32, eTop + 5, kLeftWidth - 14, eTop + 25 };
        if (m_hoverClear) {
            HBRUSH b = CreateSolidBrush(mask ? RGB(0xFF, 0xFF, 0xFF)
                                             : RGB(0xD8, 0x4A, 0x3C));
            FillRect(hdc, &cr, b);
            DeleteObject(b);
        }
        SetTextColor(hdc, mask ? RGB(0xFF, 0xFF, 0xFF)
                               : (m_hoverClear ? RGB(0xFF, 0xFF, 0xFF)
                                               : RGB(0x66, 0x66, 0x66)));
        HFONT obx = static_cast<HFONT>(SelectObject(hdc, fontBold));
        DrawTextW(hdc, L"X", -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        SelectObject(hdc, obx);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteObject(fontBold);
    DeleteObject(fontMatch);
}

/* ---------------- composizione layered ---------------- */

void AppSearchWindow::OnPaint(HDC hdcWindow) {
    (void)hdcWindow;
    RECT rc{};
    GetClientRect(m_hWnd, &rc);
    const int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;

    void* colorBits = nullptr;
    void* maskBits = nullptr;
    HBITMAP colorBmp = MakeDib32(W, H, &colorBits);
    HBITMAP maskBmp = MakeDib32(W, H, &maskBits);
    if (!colorBmp || !maskBmp || !colorBits || !maskBits) {
        if (colorBmp) DeleteObject(colorBmp);
        if (maskBmp) DeleteObject(maskBmp);
        return;
    }

    HDC screen = GetDC(nullptr);
    HDC dcC = CreateCompatibleDC(screen);
    HDC dcM = CreateCompatibleDC(screen);
    HBITMAP obC = static_cast<HBITMAP>(SelectObject(dcC, colorBmp));
    HBITMAP obM = static_cast<HBITMAP>(SelectObject(dcM, maskBmp));

    RenderScene(dcC, static_cast<uint32_t*>(colorBits), W, H, false);
    RenderScene(dcM, static_cast<uint32_t*>(maskBits), W, H, true);

    // composizione: alpha dalla maschera, colori dalla scena, premultiply
    uint32_t* c = static_cast<uint32_t*>(colorBits);
    const uint32_t* m = static_cast<uint32_t*>(maskBits);
    const size_t n = static_cast<size_t>(W) * H;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t a = (m[i] >> 16) & 0xFF;   // grigio: R=G=B
        const uint32_t rr = (c[i] >> 16) & 0xFF;
        const uint32_t gg = (c[i] >> 8) & 0xFF;
        const uint32_t bb = c[i] & 0xFF;
        c[i] = (a << 24) |
               (((rr * a) / 255) << 16) |
               (((gg * a) / 255) << 8) |
               ((bb * a) / 255);
    }

    SIZE sz{ W, H };
    POINT ptSrc{ 0, 0 };
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.AlphaFormat = AC_SRC_ALPHA;
    bf.SourceConstantAlpha = 255;
    UpdateLayeredWindow(m_hWnd, screen, nullptr, &sz, dcC, &ptSrc,
                        0, &bf, ULW_ALPHA);

    SelectObject(dcC, obC);
    SelectObject(dcM, obM);
    DeleteDC(dcC);
    DeleteDC(dcM);
    ReleaseDC(nullptr, screen);
    DeleteObject(colorBmp);
    DeleteObject(maskBmp);
}

/* ---------------- messaggi ---------------- */

LRESULT CALLBACK AppSearchWindow::WndProc(HWND hWnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<AppSearchWindow*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hWnd, msg, wParam, lParam);

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
        return 1;   // composizioniamo noi
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(hWnd, msg, wParam, lParam);
        switch (r) {
            case HTTOP: case HTTOPLEFT: case HTTOPRIGHT:
            case HTBOTTOM: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
            case HTLEFT: case HTRIGHT:
                return HTBORDER;
            default: return r;
        }
    }
    case WM_APP + 1:
        // fine scansione: rifiltrare per mostrare l'elenco completo
        self->ApplyFilter(self->m_query);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        self->m_caretOn = !self->m_caretOn;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_CHAR:
        if (wParam >= 32 && wParam < 0xD800) {
            self->m_hasSelection = false;   /* v2.37 punto 7 */
            self->m_query += static_cast<wchar_t>(wParam);
            self->ApplyFilter(self->m_query);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_BACK:
        case VK_DELETE:
            /* v2.37 punto 7: con selezione attiva si cancella quella. */
            if (self->m_hasSelection && !self->m_query.empty()) {
                self->m_query.clear();
                self->m_hasSelection = false;
                self->ApplyFilter(self->m_query);
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_BACK && !self->m_query.empty()) {
                self->m_query.pop_back();
                self->ApplyFilter(self->m_query);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        case 'A':
            /* v2.37 punto 7: Ctrl+A seleziona tutto (la selezione della
             * nostra casella disegnata copre l'intero testo). */
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                !self->m_query.empty()) {
                self->m_hasSelection = true;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        case 'C':
            /* v2.37 punto 7: Ctrl+C copia la selezione. */
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
                self->m_hasSelection && !self->m_query.empty()) {
                ClipboardSetText(hWnd, self->m_query);
            }
            return 0;
        case 'V':
            /* v2.37 punto 7: Ctrl+V incolla (sostituisce la selezione). */
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                const std::wstring paste = ClipboardGetText(hWnd);
                if (!paste.empty()) {
                    if (self->m_hasSelection) self->m_query.clear();
                    self->m_hasSelection = false;
                    self->m_query += paste;
                    self->ApplyFilter(self->m_query);
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
            return 0;
        case VK_UP:
            self->SelectRow(self->m_selectedRow - 1);
            return 0;
        case VK_DOWN:
            self->SelectRow(self->m_selectedRow + 1);
            return 0;
        case VK_RETURN:
            /* Windows 7: Alt+Invio apre le proprieta' dell'elemento. */
            if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
                self->ShowPropertiesOfSelected();
            } else {
                self->RunOption(0);
            }
            return 0;
        case VK_ESCAPE:
            self->Hide();
            return 0;
        }
        return 0;
    case WM_MOUSEWHEEL: {
        /* v2.37 punto 12: stessa geometria della scrollbar disegnata. */
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
        self->ScrollTo(self->m_scroll + delta);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        /* v2.37 punto 12: trascinamento del cursore della scrollbar. */
        if (self->m_scrollDragging) {
            const ScrollGeom sg = self->ComputeScrollGeom();
            if (sg.visible && sg.maxScroll > 0) {
                constexpr int kBtn = 16;
                const int innerTop = sg.track.top + kBtn;
                const int innerH = (sg.track.bottom - kBtn) - innerTop;
                const int thumbH = sg.thumb.bottom - sg.thumb.top;
                const int range = innerH - thumbH;
                if (range > 0) {
                    const int dy = p.y - self->m_scrollDragAnchorY;
                    self->ScrollTo(self->m_scrollDragAnchorPos +
                                   dy * sg.maxScroll / range);
                    InvalidateRect(hWnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        int opt = self->HitTestOption(p);
        bool clr = self->HitTestClear(p);
        const bool mag = self->HitTestMagnifier(p);   /* v2.37 punto 11 */
        if (opt != self->m_hoverOption || clr != self->m_hoverClear ||
            mag != self->m_hoverMag) {
            self->m_hoverOption = opt;
            self->m_hoverClear = clr;
            self->m_hoverMag = mag;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
        /* v2.37 punto 12: click sulla scrollbar disegnata (frecce,
         * cursore, binario). */
        {
            const ScrollGeom sg = self->ComputeScrollGeom();
            if (sg.visible) {
                if (PtInRect(&sg.upBtn, p)) {
                    self->ScrollTo(self->m_scroll - 1);
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (PtInRect(&sg.dnBtn, p)) {
                    self->ScrollTo(self->m_scroll + 1);
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (PtInRect(&sg.thumb, p)) {
                    self->m_scrollDragging = true;
                    self->m_scrollDragAnchorY = p.y;
                    self->m_scrollDragAnchorPos = self->m_scroll;
                    SetCapture(hWnd);
                    return 0;
                }
                if (PtInRect(&sg.track, p)) {
                    const int page = sg.visRows > 1 ? sg.visRows - 1 : 1;
                    self->ScrollTo(self->m_scroll +
                                   (p.y < sg.thumb.top ? -page : page));
                    InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
            }
        }
        if (self->HitTestClear(p)) {
            self->m_query.clear();
            self->m_hasSelection = false;
            self->ApplyFilter(self->m_query);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (self->HitTestMagnifier(p)) {
            /* v2.37 punto 11: click sulla lente = focus alla casella. */
            SetFocus(hWnd);
            return 0;
        }
        int row = self->HitTestRow(p);
        if (row >= 0) self->SelectRow(row);
        int opt = self->HitTestOption(p);
        if (opt >= 0) self->RunOption(opt);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        if (self->m_scrollDragging) {
            self->m_scrollDragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        self->m_scrollDragging = false;
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (self->m_scrollDragging) {
                self->m_scrollDragging = false;
                ReleaseCapture();
            }
            self->Hide();
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
    } catch (...) {
        return 0;
    }
}

} /* namespace w7t */
