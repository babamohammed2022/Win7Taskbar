// Win7Taskbar - cornice 9-slice delle anteprime (Aero thumbnail frame)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.9: implementazione del contratto di AeroThumbnailFrame.h. Le slice
// si caricano UNA volta (std::once_flag) dalla cartella Resources/
// accanto all'eseguibile, si decodificano con WIC e si disegnano con
// AlphaBlend (DrawBitmapScaled, lo stesso helper condiviso degli altri
// flyout). Nessun hook a Explorer, nessuna dipendenza esterna.

#include "AeroThumbnailFrame.h"
#include "Common.h"          /* AppendCoreLog, MakeHBitmapFromArgb, DrawBitmapScaled */
#include "ScopeGuards.h"     /* UniqueGdiObject, SelectGuard */

#include <windows.h>
#include <objbase.h>         /* CoInitializeEx/CoCreateInstance: WIN32_LEAN_AND_MEAN esclude ole2 */
#include <wincodec.h>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace w7t {

namespace {

/* Le otto porzioni, nell'ordine fisso in cui vanno montate. */
enum SliceIndex {
    kSliceTopLeft, kSliceTopCenter, kSliceTopRight,
    kSliceMidLeft, kSliceMidRight,
    kSliceBottomLeft, kSliceBottomCenter, kSliceBottomRight,
    kSliceCount
};

constexpr const wchar_t* kSliceFiles[kSliceCount] = {
    L"top_left.png",      L"top_center.png",    L"top_right.png",
    L"mid_left.png",      L"mid_right.png",
    L"bottom_left.png",   L"bottom_center.png", L"bottom_right.png",
};

struct Slice {
    std::vector<uint32_t> px;   /* pixel dritti 0xAARRGGBB da disco */
    int w = 0;
    int h = 0;
    HBITMAP hb = nullptr;       /* premoltiplicato, (ri)costruito per accent */
};

Slice             s_slices[kSliceCount];
std::once_flag    s_loadOnce;
bool              s_slicesOk = false;   /* tutte e otto caricate */
COLORREF          s_builtFor = 0;       /* accent per cui gli hb valgono */
bool              s_built    = false;

/* Cartella Resources/ accanto all'eseguibile (la DLL vive li' anche lei:
 * il pacchetto pubblicato tiene Resources\ accanto al .exe). */
std::wstring ResourcesDir() {
    wchar_t exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return std::wstring();
    }
    std::wstring dir(exe, n);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    return dir.substr(0, slash + 1) + L"Resources\\";
}

/* Decodifica un PNG da file -> pixel dritti 0xAARRGGBB via WIC.
 * Stessa trafila di DecodeEmbeddedPng (Common.cpp), ingresso da file. */
bool DecodePngFile(const std::wstring& path, std::vector<uint32_t>& out,
                   int& outW, int& outH) {
    out.clear();
    outW = outH = 0;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needCoUninit = SUCCEEDED(hrCo);

    bool ok = false;
    try {
        IWICImagingFactory* fac = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&fac));
        if (SUCCEEDED(hr) && fac) {
            IWICBitmapDecoder* dec = nullptr;
            if (SUCCEEDED(fac->CreateDecoderFromFilename(
                    path.c_str(), nullptr, GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand, &dec)) && dec) {
                IWICBitmapFrameDecode* frame = nullptr;
                if (SUCCEEDED(dec->GetFrame(0, &frame)) && frame) {
                    IWICFormatConverter* conv = nullptr;
                    if (SUCCEEDED(fac->CreateFormatConverter(&conv)) && conv &&
                        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                                   WICBitmapDitherTypeNone, nullptr,
                                                   0.0, WICBitmapPaletteTypeCustom))) {
                        UINT w = 0, h = 0;
                        if (SUCCEEDED(conv->GetSize(&w, &h)) && w > 0 && h > 0 &&
                            w <= 4096 && h <= 4096) {
                            std::vector<BYTE> raw(static_cast<size_t>(w) * h * 4);
                            if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4,
                                                           static_cast<UINT>(raw.size()),
                                                           raw.data()))) {
                                /* RGBA byte -> 0xAARRGGBB */
                                out.resize(static_cast<size_t>(w) * h);
                                for (size_t i = 0; i < out.size(); ++i) {
                                    const BYTE* p = &raw[i * 4];
                                    out[i] = (static_cast<uint32_t>(p[3]) << 24) |
                                             (static_cast<uint32_t>(p[0]) << 16) |
                                             (static_cast<uint32_t>(p[1]) << 8) |
                                             static_cast<uint32_t>(p[2]);
                                }
                                outW = static_cast<int>(w);
                                outH = static_cast<int>(h);
                                ok = true;
                            }
                        }
                    }
                    if (conv) conv->Release();
                    frame->Release();
                }
                dec->Release();
            }
            fac->Release();
        }
    } catch (...) {
        ok = false;
        out.clear();
        outW = outH = 0;
    }

    if (needCoUninit) CoUninitialize();
    return ok;
}

/* Caricamento una-tantum: se ANCHE UNA slice manca o non si decodifica,
 * l'intero set e' dichiarato inapplicabile (log) e il chiamante usera'
 * sempre il rettangolo tradizionale. */
void LoadAllSlices() {
    const std::wstring dir = ResourcesDir();
    if (dir.empty()) {
        AppendCoreLog(L"aero-frame: cartella Resources non trovata: "
                      L"ripiego rettangolo tradizionale");
        return;
    }

    bool all = true;
    for (int i = 0; i < kSliceCount; ++i) {
        const std::wstring path = dir + kSliceFiles[i];
        if (!DecodePngFile(path, s_slices[i].px, s_slices[i].w, s_slices[i].h)) {
            const std::wstring msg = std::wstring(L"aero-frame: slice non caricabile: ") +
                                     kSliceFiles[i] +
                                     L" - ripiego rettangolo tradizionale";
            AppendCoreLog(msg.c_str());
            all = false;
        }
    }
    s_slicesOk = all;
}

/* (Ri)costruisce gli HBITMAP premoltiplicati per l'accent richiesto.
 * accent == 0: le slice restano come sono su disco; altrimenti la
 * maschera grigia viene tinta col colore accent mantenendo l'alfa
 * (equivalente GDI del percorso WPF: pennello accent + opacity mask). */
void BuildBitmapsFor(COLORREF accent) {
    for (int i = 0; i < kSliceCount; ++i) {
        Slice& s = s_slices[i];
        if (s.hb != nullptr) {
            DeleteObject(s.hb);
            s.hb = nullptr;
        }
        if (s.px.empty()) {
            continue;
        }

        if (accent == 0) {
            s.hb = MakeHBitmapFromArgb(s.px, s.w, s.h);
            continue;
        }

        const uint32_t ar = GetRValue(accent);
        const uint32_t ag = GetGValue(accent);
        const uint32_t ab = GetBValue(accent);
        std::vector<uint32_t> tinted(s.px.size());
        for (size_t k = 0; k < s.px.size(); ++k) {
            tinted[k] = (s.px[k] & 0xFF000000u) |
                        (ar << 16) | (ag << 8) | ab;
        }
        s.hb = MakeHBitmapFromArgb(tinted, s.w, s.h);
    }
    s_builtFor = accent;
    s_built = true;
}

void FreeSlices() {
    for (int i = 0; i < kSliceCount; ++i) {
        if (s_slices[i].hb != nullptr) {
            DeleteObject(s_slices[i].hb);
            s_slices[i].hb = nullptr;
        }
    }
    s_built = false;
}

} /* namespace anonimo */

bool DrawAeroThumbnailFrame9Slice(HDC hdc, const RECT& dst, COLORREF accent) {
    if (hdc == nullptr) {
        return false;
    }

    std::call_once(s_loadOnce, LoadAllSlices);
    if (!s_slicesOk) {
        return false;
    }

    const int dw = dst.right - dst.left;
    const int dh = dst.bottom - dst.top;

    /* Controllo preliminare: sotto la somma degli spessori il 9-slice
     * non e' rappresentabile. Non e' un errore: si segnala "non
     * applicabile" e il chiamante disegna il rettangolo tradizionale. */
    if (dw < kAeroFrameLeft + kAeroFrameRight ||
        dh < kAeroFrameTop + kAeroFrameBottom) {
        return false;
    }

    if (!s_built || s_builtFor != accent) {
        BuildBitmapsFor(accent);
    }
    for (int i = 0; i < kSliceCount; ++i) {
        if (s_slices[i].hb == nullptr) {
            /* Una bitmap non e' costruibile: niente disegno parziale. */
            FreeSlices();
            s_slicesOk = false;
            AppendCoreLog(L"aero-frame: bitmap slice non costruibile: "
                          L"ripiego rettangolo tradizionale");
            return false;
        }
    }

    const int x = dst.left;
    const int y = dst.top;
    const int midW = dw - kAeroFrameLeft - kAeroFrameRight;
    const int midH = dh - kAeroFrameTop - kAeroFrameBottom;

    /* Angoli: 1:1, senza scaling, ancorati ai quattro angoli. Le misure
     * delle slice coincidono con gli spessori del bordo sorgente. */
    DrawBitmapScaled(hdc, s_slices[kSliceTopLeft].hb,
                     s_slices[kSliceTopLeft].w, s_slices[kSliceTopLeft].h,
                     x, y);
    DrawBitmapScaled(hdc, s_slices[kSliceTopRight].hb,
                     s_slices[kSliceTopRight].w, s_slices[kSliceTopRight].h,
                     x + dw - kAeroFrameRight, y);
    DrawBitmapScaled(hdc, s_slices[kSliceBottomLeft].hb,
                     s_slices[kSliceBottomLeft].w, s_slices[kSliceBottomLeft].h,
                     x, y + dh - kAeroFrameBottom);
    DrawBitmapScaled(hdc, s_slices[kSliceBottomRight].hb,
                     s_slices[kSliceBottomRight].w, s_slices[kSliceBottomRight].h,
                     x + dw - kAeroFrameRight, y + dh - kAeroFrameBottom);

    /* Bordi: stirati SOLO nella direzione lungo cui corrono; lo spessore
     * (38 sopra, 19 sotto, 17 ai fianchi) resta quello sorgente. */
    DrawBitmapScaled(hdc, s_slices[kSliceTopCenter].hb,
                     midW, s_slices[kSliceTopCenter].h,
                     x + kAeroFrameLeft, y);
    DrawBitmapScaled(hdc, s_slices[kSliceBottomCenter].hb,
                     midW, s_slices[kSliceBottomCenter].h,
                     x + kAeroFrameLeft, y + dh - kAeroFrameBottom);
    DrawBitmapScaled(hdc, s_slices[kSliceMidLeft].hb,
                     s_slices[kSliceMidLeft].w, midH,
                     x, y + kAeroFrameTop);
    DrawBitmapScaled(hdc, s_slices[kSliceMidRight].hb,
                     s_slices[kSliceMidRight].w, midH,
                     x + dw - kAeroFrameRight, y + kAeroFrameTop);

    /* Il centro NON viene disegnato: li' ci vive il contenuto vero della
     * thumbnail (o il fill di ripiego scelto dal chiamante). */
    return true;
}

void DrawAeroThumbnailFrameFallback(HDC hdc, const RECT& dst) {
    if (hdc == nullptr) {
        return;
    }

    /* Rettangolo tradizionale: fill pieno scuro (come il ripiego
     * "identity" delle anteprime gestite) e bordo semplice. Le guardie
     * RAII restituiscono pennello e penna al DC su ogni percorso. */
    UniqueGdiObject fill(CreateSolidBrush(RGB(0x20, 0x20, 0x20)));
    UniqueGdiObject edge(CreatePen(PS_SOLID, 1, RGB(0x6E, 0xA5, 0xD2)));
    SelectGuard fillSel(hdc, fill);
    SelectGuard edgeSel(hdc, edge);
    Rectangle(hdc, dst.left, dst.top, dst.right, dst.bottom);
}

} /* namespace w7t */
