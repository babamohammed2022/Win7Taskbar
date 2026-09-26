/*
 * Win7Taskbar - Core nativo - Icone di RIPIEGO dell'area di notifica
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Vedi TrayFallbackIcons.h per il perche' di questo file.
 */

#include "TrayFallbackIcons.h"

#include "TrayIconAssets.inc"   /* icone nostre: volume e rete            */
                                /* (la batteria si disegna: vedi sotto)   */
#include "AudioService.h"
#include "SehGuard.h"

#include <objbase.h>            /* WIN32_LEAN_AND_MEAN: esplicito         */
#include <netlistmgr.h>         /* CLSID_NetworkListManager / INetworkListManager */
#include <wlanapi.h>            /* WLAN_SIGNAL_QUALITY per le barre di segnale  */
#include <cmath>
#include <cstring>

namespace w7t {
namespace {

/* ------------------------------------------------------------------------ */
/*  GUID delle icone di sistema della shell                                  */
/*                                                                           */
/*  Sono le tre GUID con cui la shell di Windows registra volume, batteria   */
/*  e rete nella NOTIFYICONDATA (guidItem): identificarle cosi' e'           */
/*  indipendente dalla POSIZIONE nella barra, che e' esattamente il          */
/*  requisito. Nel dubbio (GUID assente su qualche build) si ripiega sul     */
/*  modulo proprietario della finestra, verificato cross-process da          */
/*  OwnerModuleIs.                                                           */
/* ------------------------------------------------------------------------ */
const GUID kVolumeGuid  = { 0x7820AE73, 0x23E3, 0x4223,
                            { 0x82, 0xC8, 0xBA, 0x94, 0x8F, 0x79, 0xAF, 0x8F } };
const GUID kBatteryGuid = { 0x7820AE74, 0x23E3, 0x4223,
                            { 0x82, 0xC8, 0xBA, 0x94, 0x8F, 0x79, 0xAF, 0x8F } };
const GUID kNetworkGuid = { 0x7820AE75, 0x23E3, 0x4223,
                            { 0x82, 0xC8, 0xBA, 0x94, 0x8F, 0x79, 0xAF, 0x8F } };

bool SameGuid(const GUID& a, const GUID& b) {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           std::memcmp(a.Data4, b.Data4, sizeof(a.Data4)) == 0;
}

bool GuidIsZero(const GUID& g) {
    const GUID zero = {};
    return SameGuid(g, zero);
}

/* ------------------------------------------------------------------------ */
/*  Cache delle decodifiche (rete e volume)                                  */
/*                                                                           */
/*  Ogni PNG incorporato viene decodificato UNA volta, alla prima           */
/*  richiesta della famiglia rete/volume: le icone di ripiego si            */
/*  disegnano solo in caso di necessita', e chi non ne ha mai bisogno non   */
/*  paga nulla. La batteria NON ha piu' una cache di PNG: si disegna con    */
/*  GDI+ a ogni cambiamento di stato (vedi il blocco piu' sotto).           */
/* ------------------------------------------------------------------------ */
ArgbBitmap g_tray[trayassets::IdxCount];
bool       g_trayReady   = false;
bool       g_trayTried   = false;

bool g_loggedNetwork = false;
bool g_loggedVolume  = false;
bool g_loggedBattery = false;

ArgbBitmap DecodeOne(const char* b64) {
    ArgbBitmap out;
    std::vector<uint32_t> px;
    int w = 0;
    int h = 0;
    /* Nessun ritaglio: il PNG e' gia' la sola icona (il taglio e' stato
     * fatto in fase di generazione, sul bounding-box alpha). */
    if (DecodeEmbeddedPng(b64, px, w, h, false, 0)) {
        out.width  = w;
        out.height = h;
        out.pixels.resize(px.size() * 4);
        for (size_t i = 0; i < px.size(); ++i) {
            const uint32_t p = px[i];
            out.pixels[i * 4 + 0] = static_cast<uint8_t>(p & 0xFF);
            out.pixels[i * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
            out.pixels[i * 4 + 2] = static_cast<uint8_t>((p >> 16) & 0xFF);
            out.pixels[i * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xFF);
        }
    }
    return out;
}

void EnsureTrayGlyphs() {
    if (g_trayTried) {
        return;
    }
    g_trayTried = true;
    bool any = false;
    for (int i = 0; i < trayassets::IdxCount; ++i) {
        g_tray[i] = DecodeOne(trayassets::kAll[i].b64);
        any = any || !g_tray[i].empty();
    }
    g_trayReady = any;
}

const ArgbBitmap* TrayGlyph(trayassets::Idx index) {
    EnsureTrayGlyphs();
    if (!g_trayReady) {
        return nullptr;
    }
    const ArgbBitmap& bmp = g_tray[index];
    return bmp.empty() ? nullptr : &bmp;
}

/* ======================================================================== */
/*  BATTERIA: glifo GENERICO disegnato al volo con GDI+                     */
/*                                                                          */
/*  Il ripiego batteria non usa piu' i ritagli PNG (BatteryAssets.inc):    */
/*  una batteria generica si disegna DA SOLA, cosi' il riempimento segue    */
/*  la percentuale VERA (continua, non a decimi come nelle strisce) e non   */
/*  esiste alcun asset incorporato da tenere allineato ai disegni          */
/*  sorgente. GDI+ arriva da gdiplus.dll caricata dinamicamente, lo stesso  */
/*  approccio di BatteryFlyout.cpp e LanguageSwitcher.cpp: nessun nuovo     */
/*  link, nessun header GDI+ nel progetto, soltanto puntatori piatti.       */
/*                                                                          */
/*  Si disegna sovracampionato (64x64) e si riduce a 16x16, la dimensione   */
/*  delle altre icone di ripiego, con interpolazione bicubic high quality:  */
/*  l'anti-alias resta leggibile alla dimensione della tray. La lettura     */
/*  finale avviene con LockBits in PixelFormat32bppPARGB: esce GIA'         */
/*  premoltiplicato, il contratto BGRA di ArgbBitmap, senza conversioni.    */
/*                                                                          */
/*  RAII: ogni oggetto GDI+ (bitmap, graphics, brush, pen, path) vive in    */
/*  un UniqueGdip che lo distrugge a fine scope, la stessa disciplina       */
/*  degli altri guard del progetto (ScopeGuards.h). Il try/catch copre le   */
/*  allocazioni C++: il ripiego non puo' mai far salire un'eccezione        */
/*  verso i punti di ingresso C del core, un fallimento ritorna false e     */
/*  il chiamante lascia stare la bitmap che ha.                             */
/* ======================================================================== */

/* Costanti piatte di GDI+ (gdiplus.h non e' incluso nel progetto). */
constexpr int           kGdipArgb              = 0x0026200A; /* PixelFormat32bppARGB       */
constexpr int           kGdipPArgb             = 0x0026200E; /* PixelFormat32bppPARGB      */
constexpr int           kGdipAntiAlias         = 4;          /* SmoothingModeAntiAlias     */
constexpr int           kGdipHalfOffset        = 4;          /* PixelOffsetModeHalf        */
constexpr int           kGdipHQBicubic         = 7;          /* InterpolationModeHighQualityBicubic */
constexpr unsigned int  kGdipLockRead          = 1;          /* ImageLockModeRead          */
constexpr int           kGdipFillAlternate     = 0;          /* FillModeAlternate          */

/* Minime riproduzioni dei tipi piatti di GDI+ (layout identico). */
struct GdipPointF  { float x; float y; };
struct GdipRect    { int x; int y; int width; int height; };
struct GdipBitmapData {
    unsigned int width;
    unsigned int height;
    int          stride;
    int          pixelFormat;
    void*        scan0;
    void*        reserved;
};

using GdipStatus = int;

using GdiplusStartupFn              = GdipStatus (WINAPI*)(ULONG_PTR*, const void*, void*);
using GdipCreateBitmapFromScan0Fn   = GdipStatus (WINAPI*)(int, int, int, int, void*, void**);
using GdipGetImageGraphicsContextFn = GdipStatus (WINAPI*)(void*, void**);
using GdipSetSmoothingModeFn        = GdipStatus (WINAPI*)(void*, int);
using GdipSetPixelOffsetModeFn      = GdipStatus (WINAPI*)(void*, int);
using GdipSetInterpolationModeFn    = GdipStatus (WINAPI*)(void*, int);
using GdipCreateSolidFillFn         = GdipStatus (WINAPI*)(uint32_t, void**);
using GdipDeleteBrushFn             = GdipStatus (WINAPI*)(void*);
using GdipCreatePen1Fn              = GdipStatus (WINAPI*)(uint32_t, float, int, void**);
using GdipDeletePenFn               = GdipStatus (WINAPI*)(void*);
using GdipCreatePathFn              = GdipStatus (WINAPI*)(int, void**);
using GdipAddPathArcFn              = GdipStatus (WINAPI*)(void*, float, float, float, float, float, float);
using GdipClosePathFigureFn         = GdipStatus (WINAPI*)(void*);
using GdipDeletePathFn              = GdipStatus (WINAPI*)(void*);
using GdipDrawPathFn                = GdipStatus (WINAPI*)(void*, void*, void*);
using GdipFillRectangleFn           = GdipStatus (WINAPI*)(void*, void*, float, float, float, float);
using GdipFillPolygonFn             = GdipStatus (WINAPI*)(void*, void*, const void*, int);
using GdipDrawPolygonFn             = GdipStatus (WINAPI*)(void*, void*, const void*, int);
using GdipFillEllipseFn             = GdipStatus (WINAPI*)(void*, void*, float, float, float, float);
using GdipDrawLineFn                = GdipStatus (WINAPI*)(void*, void*, float, float, float, float);
using GdipDrawImageRectIFn          = GdipStatus (WINAPI*)(void*, void*, int, int, int, int);
using GdipLockBitsFn                = GdipStatus (WINAPI*)(void*, const void*, unsigned int, int, void*);
using GdipUnlockBitsFn              = GdipStatus (WINAPI*)(void*, void*);
using GdipDeleteGraphicsFn          = GdipStatus (WINAPI*)(void*);
using GdipDisposeImageFn            = GdipStatus (WINAPI*)(void*);

/* Tutti i puntatori di cui ha bisogno il disegno, risolti UNA volta. */
struct GdiplusApi {
    bool                            ready = false;
    GdipCreateBitmapFromScan0Fn     createBitmap       = nullptr;
    GdipGetImageGraphicsContextFn   getImageGraphics   = nullptr;
    GdipSetSmoothingModeFn          setSmoothing       = nullptr;
    GdipSetPixelOffsetModeFn        setPixelOffset     = nullptr;
    GdipSetInterpolationModeFn      setInterpolation   = nullptr;
    GdipCreateSolidFillFn           createSolidFill    = nullptr;
    GdipDeleteBrushFn               deleteBrush        = nullptr;
    GdipCreatePen1Fn                createPen1         = nullptr;
    GdipDeletePenFn                 deletePen          = nullptr;
    GdipCreatePathFn                createPath         = nullptr;
    GdipAddPathArcFn                addPathArc         = nullptr;
    GdipClosePathFigureFn           closePathFigure    = nullptr;
    GdipDeletePathFn                deletePath         = nullptr;
    GdipDrawPathFn                  drawPath           = nullptr;
    GdipFillRectangleFn             fillRectangle      = nullptr;
    GdipFillPolygonFn               fillPolygon        = nullptr;
    GdipDrawPolygonFn               drawPolygon        = nullptr;
    GdipFillEllipseFn               fillEllipse        = nullptr;
    GdipDrawLineFn                  drawLine           = nullptr;
    GdipDrawImageRectIFn            drawImageRectI     = nullptr;
    GdipLockBitsFn                  lockBits           = nullptr;
    GdipUnlockBitsFn                unlockBits         = nullptr;
    GdipDeleteGraphicsFn            deleteGraphics     = nullptr;
    GdipDisposeImageFn              disposeImage       = nullptr;
};

/* Inizializzazione pigra e una tantum di gdiplus.dll (magic static:
 * thread-safe anche se la tray arriva da thread diversi). Il token di
 * GdiplusStartup e il modulo restano carichi per tutta la vita del
 * processo, stessa scelta di BatteryFlyout.cpp e LanguageSwitcher.cpp.
 * Ritorna nullptr se GDI+ non e' disponibile: in quel caso il ripiego
 * batteria semplicemente non disegna (false), senza ripiegi strani. */
const GdiplusApi* GdiplusGet() {
    static const GdiplusApi api = [] {
        GdiplusApi a;
        HMODULE mod = LoadLibraryExW(L"gdiplus.dll", nullptr,
                                     LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (mod == nullptr) {
            return a;
        }
        struct Resolve {
            static void* Fn(HMODULE h, const char* name) {
                return reinterpret_cast<void*>(GetProcAddress(h, name));
            }
        };
        a.createBitmap     = reinterpret_cast<GdipCreateBitmapFromScan0Fn>(Resolve::Fn(mod, "GdipCreateBitmapFromScan0"));
        a.getImageGraphics = reinterpret_cast<GdipGetImageGraphicsContextFn>(Resolve::Fn(mod, "GdipGetImageGraphicsContext"));
        a.setSmoothing     = reinterpret_cast<GdipSetSmoothingModeFn>(Resolve::Fn(mod, "GdipSetSmoothingMode"));
        a.setPixelOffset   = reinterpret_cast<GdipSetPixelOffsetModeFn>(Resolve::Fn(mod, "GdipSetPixelOffsetMode"));
        a.setInterpolation = reinterpret_cast<GdipSetInterpolationModeFn>(Resolve::Fn(mod, "GdipSetInterpolationMode"));
        a.createSolidFill  = reinterpret_cast<GdipCreateSolidFillFn>(Resolve::Fn(mod, "GdipCreateSolidFill"));
        a.deleteBrush      = reinterpret_cast<GdipDeleteBrushFn>(Resolve::Fn(mod, "GdipDeleteBrush"));
        a.createPen1       = reinterpret_cast<GdipCreatePen1Fn>(Resolve::Fn(mod, "GdipCreatePen1"));
        a.deletePen        = reinterpret_cast<GdipDeletePenFn>(Resolve::Fn(mod, "GdipDeletePen"));
        a.createPath       = reinterpret_cast<GdipCreatePathFn>(Resolve::Fn(mod, "GdipCreatePath"));
        a.addPathArc       = reinterpret_cast<GdipAddPathArcFn>(Resolve::Fn(mod, "GdipAddPathArc"));
        a.closePathFigure  = reinterpret_cast<GdipClosePathFigureFn>(Resolve::Fn(mod, "GdipClosePathFigure"));
        a.deletePath       = reinterpret_cast<GdipDeletePathFn>(Resolve::Fn(mod, "GdipDeletePath"));
        a.drawPath         = reinterpret_cast<GdipDrawPathFn>(Resolve::Fn(mod, "GdipDrawPath"));
        a.fillRectangle    = reinterpret_cast<GdipFillRectangleFn>(Resolve::Fn(mod, "GdipFillRectangle"));
        a.fillPolygon      = reinterpret_cast<GdipFillPolygonFn>(Resolve::Fn(mod, "GdipFillPolygon"));
        a.drawPolygon      = reinterpret_cast<GdipDrawPolygonFn>(Resolve::Fn(mod, "GdipDrawPolygon"));
        a.fillEllipse      = reinterpret_cast<GdipFillEllipseFn>(Resolve::Fn(mod, "GdipFillEllipse"));
        a.drawLine         = reinterpret_cast<GdipDrawLineFn>(Resolve::Fn(mod, "GdipDrawLine"));
        a.drawImageRectI   = reinterpret_cast<GdipDrawImageRectIFn>(Resolve::Fn(mod, "GdipDrawImageRectI"));
        a.lockBits         = reinterpret_cast<GdipLockBitsFn>(Resolve::Fn(mod, "GdipLockBits"));
        a.unlockBits       = reinterpret_cast<GdipUnlockBitsFn>(Resolve::Fn(mod, "GdipUnlockBits"));
        a.deleteGraphics   = reinterpret_cast<GdipDeleteGraphicsFn>(Resolve::Fn(mod, "GdipDeleteGraphics"));
        a.disposeImage     = reinterpret_cast<GdipDisposeImageFn>(Resolve::Fn(mod, "GdipDisposeImage"));

        const GdiplusStartupFn startup =
            reinterpret_cast<GdiplusStartupFn>(Resolve::Fn(mod, "GdiplusStartup"));
        const bool allResolved =
            a.createBitmap != nullptr && a.getImageGraphics != nullptr &&
            a.setSmoothing != nullptr && a.setPixelOffset != nullptr &&
            a.setInterpolation != nullptr && a.createSolidFill != nullptr &&
            a.deleteBrush != nullptr && a.createPen1 != nullptr &&
            a.deletePen != nullptr && a.createPath != nullptr &&
            a.addPathArc != nullptr && a.closePathFigure != nullptr &&
            a.deletePath != nullptr && a.drawPath != nullptr &&
            a.fillRectangle != nullptr &&
            a.fillPolygon != nullptr && a.drawPolygon != nullptr &&
            a.fillEllipse != nullptr && a.drawLine != nullptr &&
            a.drawImageRectI != nullptr && a.lockBits != nullptr &&
            a.unlockBits != nullptr && a.deleteGraphics != nullptr &&
            a.disposeImage != nullptr && startup != nullptr;
        if (!allResolved) {
            FreeLibrary(mod);
            return a;
        }
        /* Layout di GdiplusStartupInput (i quattro campi, in ordine). */
        struct StartupInput {
            unsigned int version;
            void*        debugEventCallback;
            int          suppressBackgroundThread;
            void*        suppressExternalCodecs;
        } input { 1, nullptr, 0, nullptr };
        ULONG_PTR token = 0;
        if (startup(&token, &input, nullptr) != 0) {
            FreeLibrary(mod);
            return a;
        }
        /* Il token vive fino allo scarico del processo, apposta: come il
         * modulo, che non si puo' liberare mentre GDI+ e' attivo. */
        a.ready = true;
        return a;
    }();
    return api.ready ? &api : nullptr;
}

/* RAII per un oggetto GDI+ (bitmap, graphics, brush, pen, path): ogni
 * famiglia ha la sua funzione di distruzione, che qui viaggia insieme
 * all'handle. Distruttore noexcept, nessuna copia: nessun early return
 * puo' perdere un oggetto GDI+, nemmeno attraverso un'eccezione. */
class UniqueGdip {
public:
    using DisposeFn = GdipStatus (WINAPI*)(void*);
    UniqueGdip() noexcept = default;
    UniqueGdip(void* obj, DisposeFn dispose) noexcept
        : m_obj(obj), m_dispose(dispose) {}
    ~UniqueGdip() noexcept { reset(); }
    UniqueGdip(const UniqueGdip&) = delete;
    UniqueGdip& operator=(const UniqueGdip&) = delete;

    bool  valid() const noexcept { return m_obj != nullptr; }
    void* get()   const noexcept { return m_obj; }
    void  reset() noexcept {
        if (m_obj != nullptr && m_dispose != nullptr) {
            (void)m_dispose(m_obj);   /* il fallimento qui non e' recuperabile */
        }
        m_obj = nullptr;
    }

private:
    void*     m_obj     = nullptr;
    DisposeFn m_dispose = nullptr;
};

/* Geometria del glifo, in coordinate logiche 0..16 (poi scalate):
 * corpo arrotondato con il terminale in alto, come le batterie della
 * tray di Windows 7. Il riempimento parte dal fondo e cresce con la
 * carica vera; dentro c'e' la sola "icona semplice della batteria".   */
constexpr int    kBattIconSize    = 16;   /* come rete e volume            */
constexpr int    kBattSupersample = 4;    /* si disegna a 64x64, poi giu'  */
constexpr float  kBodyLeft        = 2.5f;
constexpr float  kBodyTop         = 3.0f;
constexpr float  kBodyRight       = 13.5f;
constexpr float  kBodyBottom      = 15.0f;
constexpr float  kCornerRadius    = 1.3f;
constexpr float  kStrokeWidth     = 1.05f;
constexpr float  kFillPad         = 0.55f;   /* stacco del riempimento      */
constexpr float  kCapLeft         = 6.25f;
constexpr float  kCapWidth        = 3.5f;
constexpr float  kCapTop          = 0.7f;
constexpr float  kCapHeight       = 2.6f;    /* entra nel corpo: nessun     */
                                             /* discontinuita' di contorno  */
/* Colori in ARGB dritto (la premoltiplicazione avviene solo alla
 * lettura finale): contorno quasi nero come le icone classiche,
 * verde/giallo/rosso per la carica, bianco per il fulmine.            */
constexpr uint32_t kColorOutline  = 0xFF2B2B2B;
constexpr uint32_t kColorFull     = 0xFF46BE3C;   /* verde: carica regolare */
constexpr uint32_t kColorMid      = 0xFFF0C419;   /* giallo: sotto il 30%   */
constexpr uint32_t kColorLow      = 0xFFDD3B2F;   /* rosso: sotto il 15%    */
constexpr uint32_t kColorBolt     = 0xFFFFFFFF;
constexpr uint32_t kColorWarnMark = 0xFFFF8F00;   /* punto esclamativo      */

/* Che cosa mostrare dentro il corpo della batteria. Stessa semantica di
 * prima (gli indici delle vecchie strisce), sola cambia la matita.     */
enum class BatteryView : int {
    Fill      = 0,   /* riempimento proporzionale alla carica        */
    Charging  = 1,   /* riempimento + fulmine                        */
    Unknown   = 2,   /* corpo vuoto + punto esclamativo (stato illeggibile) */
    NoBattery = 3,   /* corpo vuoto + X rossa (nessuna batteria)     */
};

struct BatteryState {
    BatteryView view    = BatteryView::Unknown;
    int         percent = 0;   /* 0..100, usato da Fill e Charging      */
};

/* Stessa fonte e stessa semantica di prima (GetSystemPowerStatus,
 * BatteryFlag/BatteryLifePercent): cambia solo chi disegna. */
BatteryState BatteryStateNow() {
    BatteryState state;
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) {
        /* Stato irraggiungibile: come prima si mostra "senza batteria",
         * ora con la nostra X disegnata. */
        state.view = BatteryView::NoBattery;
        return state;
    }
    const bool noBattery = (sps.BatteryFlag & 128) != 0;
    const bool charging  = (sps.BatteryFlag & 8) != 0;
    const bool unknown   = sps.BatteryFlag == 255 ||
                           sps.BatteryLifePercent == 255;
    int percent = sps.BatteryLifePercent;
    if (percent > 100) {
        percent = 100;
    }
    if (unknown) {
        state.view = BatteryView::Unknown;
    } else if (noBattery) {
        state.view = BatteryView::NoBattery;
    } else if (charging) {
        state.view = BatteryView::Charging;
    } else {
        state.view = BatteryView::Fill;
    }
    state.percent = percent;
    return state;
}

/* Verde sopra il 30%, giallo fino al 15%, rosso sotto: la stessa scala
 * che la shell usa per il colore dell'icona batteria. */
uint32_t BatteryFillColor(int percent) {
    if (percent >= 30) {
        return kColorFull;
    }
    if (percent >= 15) {
        return kColorMid;
    }
    return kColorLow;
}

UniqueGdip SolidBrush(const GdiplusApi& api, uint32_t argb) {
    void* brush = nullptr;
    if (api.createSolidFill(argb, &brush) != 0) {
        brush = nullptr;
    }
    return UniqueGdip(brush, api.deleteBrush);
}

UniqueGdip FlatPen(const GdiplusApi& api, uint32_t argb, float width) {
    void* pen = nullptr;
    if (api.createPen1(argb, width, 0 /*UnitWorld: il canvas e' in pixel*/,
                       &pen) != 0) {
        pen = nullptr;
    }
    return UniqueGdip(pen, api.deletePen);
}

/* Il disegno vero e proprio. Tutto dentro un try/catch: le uniche
 * operazioni che possono lanciare sono le allocazioni del buffer, e in
 * quel caso si ritorna false senza lasciare oggetti GDI+ appesi (RAII). */
bool DrawBatteryGlyph(const GdiplusApi& api, const BatteryState& state,
                      ArgbBitmap& out) {
    try {
        const float s = static_cast<float>(kBattSupersample);

        /* Canvas sovracampionato: si disegna a 64x64 cosi' l'anti-alias
         * non spezza i tratti da un pixel quando si scende a 16x16. */
        void* canvasBmp = nullptr;
        if (api.createBitmap(kBattIconSize * kBattSupersample,
                             kBattIconSize * kBattSupersample, 0,
                             kGdipArgb, nullptr, &canvasBmp) != 0 ||
            canvasBmp == nullptr) {
            return false;
        }
        UniqueGdip canvas(canvasBmp, api.disposeImage);

        void* canvasGfx = nullptr;
        if (api.getImageGraphics(canvas.get(), &canvasGfx) != 0 ||
            canvasGfx == nullptr) {
            return false;
        }
        UniqueGdip gfx(canvasGfx, api.deleteGraphics);
        api.setSmoothing(gfx.get(), kGdipAntiAlias);
        api.setPixelOffset(gfx.get(), kGdipHalfOffset);

        /* Pennelli e penne: uno per colore, tutti RAII. */
        UniqueGdip outlineBrush = SolidBrush(api, kColorOutline);
        UniqueGdip fillBrush    = SolidBrush(api, BatteryFillColor(state.percent));
        UniqueGdip boltBrush    = SolidBrush(api, kColorBolt);
        UniqueGdip warnBrush    = SolidBrush(api, kColorWarnMark);
        UniqueGdip bodyPen      = FlatPen(api, kColorOutline, kStrokeWidth * s);
        UniqueGdip boltPen      = FlatPen(api, kColorOutline, 0.45f * s);
        UniqueGdip crossPen     = FlatPen(api, kColorLow, 1.5f * s);
        if (!outlineBrush.valid() || !fillBrush.valid() ||
            !boltBrush.valid() || !warnBrush.valid() ||
            !bodyPen.valid() || !boltPen.valid() || !crossPen.valid()) {
            return false;
        }

        /* Corpo arrotondato: quattro archi uniti in un path chiuso. */
        void* rawPath = nullptr;
        if (api.createPath(kGdipFillAlternate, &rawPath) != 0 ||
            rawPath == nullptr) {
            return false;
        }
        UniqueGdip body(rawPath, api.deletePath);
        const float r2    = kCornerRadius * 2.0f * s;
        const float left  = kBodyLeft * s;
        const float top   = kBodyTop * s;
        const float right = kBodyRight * s;
        const float bottom = kBodyBottom * s;
        api.addPathArc(body.get(), left, top, r2, r2, 180.0f, 90.0f);
        api.addPathArc(body.get(), right - r2, top, r2, r2, 270.0f, 90.0f);
        api.addPathArc(body.get(), right - r2, bottom - r2, r2, r2, 0.0f, 90.0f);
        api.addPathArc(body.get(), left, bottom - r2, r2, r2, 90.0f, 90.0f);
        api.closePathFigure(body.get());

        /* Terminale (il "tassello" in alto), poi il riempimento, poi il
         * contorno del corpo: copre i bordi del riempimento e salda il
         * terminale al corpo. */
        api.fillRectangle(gfx.get(), outlineBrush.get(),
                          kCapLeft * s, kCapTop * s,
                          kCapWidth * s, kCapHeight * s);

        const float inset  = kStrokeWidth * 0.5f + kFillPad;
        const float innerL = kBodyLeft + inset;
        const float innerR = kBodyRight - inset;
        const float innerT = kBodyTop + inset;
        const float innerB = kBodyBottom - inset;
        const float innerH = innerB - innerT;
        if (state.view == BatteryView::Fill ||
            state.view == BatteryView::Charging) {
            float fillH = innerH * static_cast<float>(state.percent) / 100.0f;
            if (state.percent > 0 && fillH < 1.3f) {
                fillH = 1.3f;   /* una linguetta visibile anche all'1%    */
            }
            if (fillH > 0.0f) {
                api.fillRectangle(gfx.get(), fillBrush.get(),
                                  innerL * s, (innerB - fillH) * s,
                                  (innerR - innerL) * s, fillH * s);
            }
        }
        api.drawPath(gfx.get(), bodyPen.get(), body.get());

        /* Ciò che va DENTRO la batteria, per stato. */
        switch (state.view) {
            case BatteryView::Charging: {
                /* Fulmine bianco bordato di scuro: leggibile su qualunque
                 * colore di riempimento, come la spina delle vecchie
                 * serie "in carica". */
                constexpr int kPts = 6;
                const float boltX[kPts] = { 9.6f, 5.8f, 8.0f, 6.9f, 10.6f, 8.5f };
                const float boltY[kPts] = { 5.2f, 9.9f, 9.9f, 13.6f, 8.6f, 8.3f };
                GdipPointF bolt[kPts];
                for (int i = 0; i < kPts; ++i) {
                    bolt[i].x = boltX[i] * s;
                    bolt[i].y = boltY[i] * s;
                }
                api.fillPolygon(gfx.get(), boltBrush.get(), bolt, kPts);
                api.drawPolygon(gfx.get(), boltPen.get(), bolt, kPts);
            } break;

            case BatteryView::Unknown:
                /* Punto esclamativo arancio: lo stato non si legge. */
                api.fillRectangle(gfx.get(), warnBrush.get(),
                                  7.4f * s, 5.2f * s, 1.3f * s, 4.4f * s);
                api.fillEllipse(gfx.get(), warnBrush.get(),
                                7.3f * s, 11.0f * s, 1.5f * s, 1.5f * s);
                break;

            case BatteryView::NoBattery:
                /* X rossa dentro il corpo: nessuna batteria. */
                api.drawLine(gfx.get(), crossPen.get(),
                             4.8f * s, 5.2f * s, 11.2f * s, 12.8f * s);
                api.drawLine(gfx.get(), crossPen.get(),
                             11.2f * s, 5.2f * s, 4.8f * s, 12.8f * s);
                break;

            case BatteryView::Fill:
            default:
                break;   /* solo riempimento, gia' disegnato sopra */
        }

        /* Riduzione 64x64 -> 16x16 con bicubic high quality. */
        void* outBmp = nullptr;
        if (api.createBitmap(kBattIconSize, kBattIconSize, 0, kGdipArgb,
                             nullptr, &outBmp) != 0 || outBmp == nullptr) {
            return false;
        }
        UniqueGdip scaled(outBmp, api.disposeImage);
        void* scaledGfx = nullptr;
        if (api.getImageGraphics(scaled.get(), &scaledGfx) != 0 ||
            scaledGfx == nullptr) {
            return false;
        }
        UniqueGdip scaledCtx(scaledGfx, api.deleteGraphics);
        api.setInterpolation(scaledCtx.get(), kGdipHQBicubic);
        api.setPixelOffset(scaledCtx.get(), kGdipHalfOffset);
        if (api.drawImageRectI(scaledCtx.get(), canvas.get(),
                               0, 0, kBattIconSize, kBattIconSize) != 0) {
            return false;
        }

        /* Lettura finale: PARGB = BGRA premoltiplicato, il formato del
         * contratto ArgbBitmap. Il buffer si alloca PRIMA del lock, cosi'
         * la copia non puo' lanciare mentre i bit sono bloccati. */
        out.width  = kBattIconSize;
        out.height = kBattIconSize;
        out.pixels.assign(static_cast<size_t>(kBattIconSize) *
                              kBattIconSize * 4, 0);
        GdipBitmapData locked{};
        const GdipRect full { 0, 0, kBattIconSize, kBattIconSize };
        if (api.lockBits(scaled.get(), &full, kGdipLockRead,
                         kGdipPArgb, &locked) != 0 ||
            locked.scan0 == nullptr ||
            locked.stride < kBattIconSize * 4) {   /* solo top-down */
            out.clear();
            return false;
        }
        const uint8_t* src = static_cast<const uint8_t*>(locked.scan0);
        for (int y = 0; y < kBattIconSize; ++y) {
            std::memcpy(&out.pixels[static_cast<size_t>(y) *
                                    kBattIconSize * 4],
                        src + static_cast<size_t>(y) * locked.stride,
                        static_cast<size_t>(kBattIconSize) * 4);
        }
        api.unlockBits(scaled.get(), &locked);
        return true;
    } catch (...) {
        /* std::bad_alloc o altro: niente eccezioni verso i punti di
         * ingresso C, niente oggetti GDI+ appesi (RAII li ha gia' chiusi). */
        out.clear();
        return false;
    }
}

/* Punto d'ingresso del ripiego batteria. La SEH copre eventuali fault
 * dentro gdiplus (la stessa protezione gia' usata sopra per la rete,
 * che non vede le eccezioni C++); il try/catch dentro DrawBatteryGlyph
 * copre quelle. Due reti, una sola regola: mai far salire nulla. */
bool RenderBatteryFallback(ArgbBitmap& out) {
    bool ok = false;
    W7T_SEH_TRY {
        ok = false;
        const GdiplusApi* api = GdiplusGet();
        if (api != nullptr) {
            ok = DrawBatteryGlyph(*api, BatteryStateNow(), out);
        }
    } W7T_SEH_CATCH {
        ok = false;
    } W7T_SEH_END
    return ok;
}

/* ------------------------------------------------------------------------ */
/*  Rete: stato corrente da NLM (+ qualita' del segnale per le barre)        */
/*                                                                           */
/*  NLM e' la stessa fonte che la shell usa per decidere l'icona di rete,   */
/*  quindi lo stato mostrato dal nostro ripiego e' quello vero, non una     */
/*  stima. La qualita' del segnale Wi-Fi arriva da wlanapi, gia' linkata    */
/*  dal progetto.                                                            */
/* ------------------------------------------------------------------------ */
int WirelessBars() {
    HANDLE client = nullptr;
    DWORD version = 0;
    if (WlanOpenHandle(2 /*WLAN_API_VERSION_2_0*/, nullptr, &version, &client)
            != ERROR_SUCCESS) {
        return 0;   /* nessun servizio WLAN: non e' una macchina Wi-Fi */
    }

    int bars = 0;
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (WlanEnumInterfaces(client, nullptr, &list) == ERROR_SUCCESS && list) {
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
            const WLAN_INTERFACE_INFO& info = list->InterfaceInfo[i];
            if (info.isState != wlan_interface_state_connected) {
                continue;
            }
            DWORD size = 0;
            PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
            if (WlanQueryInterface(client, &info.InterfaceGuid,
                                   wlan_intf_opcode_current_connection, nullptr,
                                   &size,
                                   reinterpret_cast<PVOID*>(&attrs), nullptr)
                    == ERROR_SUCCESS && attrs) {
                const int quality = static_cast<int>(
                    attrs->wlanAssociationAttributes.wlanSignalQuality);
                int candidate = (quality * 5 + 99) / 100;
                if (candidate < 1) candidate = 1;
                if (candidate > 5) candidate = 5;
                if (candidate > bars) bars = candidate;
                WlanFreeMemory(attrs);
            }
        }
        WlanFreeMemory(list);
    }
    WlanCloseHandle(client, nullptr);
    return bars;
}

} /* namespace */

int TrayFallbackIcons::NetworkLevel() {
    bool connected  = false;
    bool internet   = false;

    W7T_SEH_TRY {
        /* L'apartment puo' essere gia' inizializzato da un altro modulo del
         * core: in quel caso CoInitializeEx ritorna S_FALSE e NON si deve
         * fare CoUninitialize. */
        const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool owner = SUCCEEDED(hrInit) && hrInit != S_FALSE;

        INetworkListManager* manager = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_NetworkListManager, nullptr,
                                      CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                      IID_PPV_ARGS(&manager));
        if (SUCCEEDED(hr) && manager != nullptr) {
            /* GetConnectivity() e' il metodo usato anche dal flyout di rete:
             * le property IsConnected/IsConnectedToInternet sono dichiarate
             * con nomi diversi dai due compilatori (MSVC le prefissa con
             * get_), mentre questo metodo esiste con lo stesso nome ovunque. */
            NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
            if (SUCCEEDED(manager->GetConnectivity(&connectivity))) {
                const NLM_CONNECTIVITY internetMask =
                    (NLM_CONNECTIVITY)(NLM_CONNECTIVITY_IPV4_INTERNET |
                                       NLM_CONNECTIVITY_IPV6_INTERNET);
                connected = (connectivity != NLM_CONNECTIVITY_DISCONNECTED);
                internet  = (connectivity & internetMask) != 0;
            }
            manager->Release();
        }
        if (owner) {
            CoUninitialize();
        }
    } W7T_SEH_CATCH {
        return -1;
    } W7T_SEH_END

    if (!connected) {
        return -1;
    }
    if (!internet) {
        return 0;   /* collegato ma senza Internet: icona con avviso */
    }
    const int bars = WirelessBars();
    return bars > 0 ? bars : 5;   /* cablata (o senza WLAN): segnale pieno */
}

SystemIconKind TrayFallbackIcons::Identify(uint64_t ownerHwnd, const GUID& guidItem) {
    if (!GuidIsZero(guidItem)) {
        if (SameGuid(guidItem, kNetworkGuid)) return SystemIconKind::Network;
        if (SameGuid(guidItem, kVolumeGuid))  return SystemIconKind::Volume;
        if (SameGuid(guidItem, kBatteryGuid)) return SystemIconKind::Battery;
    }

    /* Secondo criterio: il modulo che possiede la finestra. Stessa verifica
     * cross-process usata dal resto del core, nessuna euristica nuova. */
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd));
    if (hwnd == nullptr) {
        return SystemIconKind::None;
    }
    if (OwnerModuleIs(hwnd, L"pnidui.dll"))    return SystemIconKind::Network;
    if (OwnerModuleIs(hwnd, L"SndVolSSO.dll")) return SystemIconKind::Volume;
    if (OwnerModuleIs(hwnd, L"stobject.dll"))  return SystemIconKind::Battery;
    return SystemIconKind::None;
}

bool TrayFallbackIcons::Render(SystemIconKind kind, ArgbBitmap& out) {
    const ArgbBitmap* glyph = nullptr;

    switch (kind) {
        case SystemIconKind::Network: {
            const int level = NetworkLevel();
            if (level < 0) {
                glyph = TrayGlyph(trayassets::IdxNetworkNotWorking);
            } else if (level == 0) {
                glyph = TrayGlyph(trayassets::IdxNetworkWarning);
            } else {
                const int index = (level >= 1 && level <= 5) ? level - 1 : 4;
                glyph = TrayGlyph(static_cast<trayassets::Idx>(
                    trayassets::IdxNetwork0 + index));
            }
        } break;

        case SystemIconKind::Volume: {
            int32_t level = 0;
            int32_t muted = 0;
            if (AudioService::GetVolume(&level, &muted) != W7T_OK) {
                /* Nessun dispositivo audio: lo stesso stato "muto" che la
                 * shell mostra quando non c'e' un endpoint predefinito. */
                glyph = TrayGlyph(trayassets::IdxVolume0);
            } else if (muted != 0 || level <= 0) {
                glyph = TrayGlyph(trayassets::IdxVolume0);
            } else if (level < 34) {
                glyph = TrayGlyph(trayassets::IdxVolume1);
            } else if (level < 67) {
                glyph = TrayGlyph(trayassets::IdxVolume2);
            } else {
                glyph = TrayGlyph(trayassets::IdxVolume3);
            }
        } break;

        case SystemIconKind::Battery:
            /* v2.63 - IL RIPIEGO BATTERIA SI DISEGNA, NON SI DECODIFICA.
             *
             * Niente piu' ritagli dalla striscia PNG (BatteryAssets.inc):
             * una batteria GENERICA - corpo arrotondato, terminale,
             * riempimento proporzionale alla carica vera, fulmine quando
             * e' in carica, X rossa se la batteria manca, punto
             * esclamativo se lo stato e' illeggibile - si disegna al volo
             * con GDI+ a ogni cambio di stato. Il caso esce qui perche'
             * non esiste un "glifo di ripiego del ripiego": se GDI+ non
             * e' disponibile (non accade sulle Windows supportate, e'
             * lo stesso motore di BatteryFlyout) si ritorna false e il
             * chiamante lascia la bitmap che ha, invece di mostrare
             * l'icona di RETE com'era nella vecchia coda comune. */
            return RenderBatteryFallback(out);

        default:
            return false;
    }

    /* v2.61 - L'ICONA ESISTE SEMPRE.
     *
     * Se il glifo dello stato corrente non c'e' (asset mancante, stato che
     * non ha un disegno) non si torna a mani vuote: si usa il glifo
     * "stato non disponibile" dello stesso tipo, che esiste di sicuro.
     * Prima un fallimento qui lasciava la voce fuori dal modello e l'utente
     * vedeva una tray con due icone invece di tre, senza capire perche'.
     * (Dalla v2.63 la batteria non passa da qui: si disegna con GDI+ e
     * non ha piu' asset che possano mancare, vedi il caso qui sopra.) */
    if (glyph == nullptr || glyph->empty()) {
        switch (kind) {
            case SystemIconKind::Network: glyph = TrayGlyph(trayassets::IdxNetworkNotWorking); break;
            case SystemIconKind::Volume:  glyph = TrayGlyph(trayassets::IdxVolume0); break;
            default: break;
        }
    }

    if (glyph == nullptr || glyph->empty()) {
        return false;
    }
    out = *glyph;
    return true;
}

void TrayFallbackIcons::LogFirstUse(SystemIconKind kind, const wchar_t* reason) {
    bool* flag = nullptr;
    const wchar_t* name = nullptr;
    switch (kind) {
        case SystemIconKind::Network: flag = &g_loggedNetwork; name = L"rete"; break;
        case SystemIconKind::Volume:  flag = &g_loggedVolume;  name = L"volume"; break;
        case SystemIconKind::Battery: flag = &g_loggedBattery; name = L"batteria"; break;
        default: return;
    }
    if (flag == nullptr || *flag) {
        return;
    }
    *flag = true;
    wchar_t line[320];
    wsprintfW(line,
              L"tray: icona di %s non leggibile da Explorer (%s) -> icona di "
              L"ripiego dell'app (una sola volta per tipo)",
              name, reason != nullptr ? reason : L"motivo non specificato");
    AppendCoreLog(line);
}

} /* namespace w7t */
