/*
 * Win7Taskbar - Core nativo - utilita' interne
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "SehGuard.h"
#include "ScopeGuards.h"      /* v3.7.2: RAII per DC/GDI */

#include <cstdarg>      /* v2.63: LogTagged */
#include <string>
#include <cstring>
#include <vector>
#include <map>          /* v1.21.32: taskbar-list protocol overrides */
#include <mutex>        /* v1.21.32: taskbar-list protocol overrides */
#include <psapi.h>
#include <wincodec.h>   /* v2.38: WIC per i PNG incorporati condivisi */

/* MinGW non conosce HighQualityCubic (valore 4 nell'SDK recente). */
#ifndef WICBitmapInterpolationModeHighQualityCubic
#define WICBitmapInterpolationModeHighQualityCubic \
    static_cast<WICBitmapInterpolationMode>(4)
#endif

#include <strsafe.h>   /* v1.21.8: StringCchCopyW nel resolver dei percorsi */
#include <propsys.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <propkey.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <tlhelp32.h>
#include <cwchar>

/* Alcuni SDK MinGW non definiscono DWMWA_CLOAKED. */
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

namespace w7t {

/* ------------------------------------------------------------------ */
/*  CoreState                                                          */
/* ------------------------------------------------------------------ */

CoreState& CoreState::Instance() {
    static CoreState instance;
    return instance;
}

void CoreState::QueueEvent(int32_t evt, uint64_t a, uint64_t b) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_queue.size() >= kMaxQueue) {
        /* Backpressure: scarta i piu' vecchi per non crescere all'infinito. */
        m_queue.pop_front();
    }
    QueuedEvent e;
    e.evt = evt;
    e.a = a;
    e.b = b;
    m_queue.push_back(e);
}

std::vector<QueuedEvent> CoreState::DrainEvents(int32_t maxEvents) {
    std::vector<QueuedEvent> out;
    if (maxEvents <= 0) {
        maxEvents = 256;
    }
    std::lock_guard<std::mutex> lock(m_queueMutex);
    const int32_t count = static_cast<int32_t>(
        (std::min)(static_cast<size_t>(maxEvents), m_queue.size()));
    out.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i) {
        out.push_back(m_queue.front());
        m_queue.pop_front();
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  Stringhe                                                           */
/* ------------------------------------------------------------------ */

void CopyToFixed(wchar_t* dest, size_t destCount, const wchar_t* src) {
    if (dest == nullptr || destCount == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = L'\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < destCount && src[i] != L'\0'; ++i) {
        dest[i] = src[i];
    }
    dest[i] = L'\0';
}

void CopyToFixed(wchar_t* dest, size_t destCount, const std::wstring& src) {
    CopyToFixed(dest, destCount, src.c_str());
}

/* ------------------------------------------------------------------ */
/*  Icone                                                              */
/* ------------------------------------------------------------------ */

bool IconToArgb(HICON icon, ArgbBitmap& out) {
    out.clear();
    if (icon == nullptr) {
        return false;
    }

    ICONINFO info = {};
    if (!GetIconInfo(icon, &info)) {
        return false;
    }

    /* I bitmap restituiti da GetIconInfo vanno sempre rilasciati. */
    struct BitmapGuard {
        HBITMAP color;
        HBITMAP mask;
        ~BitmapGuard() {
            if (color) DeleteObject(color);
            if (mask)  DeleteObject(mask);
        }
    } guard{ info.hbmColor, info.hbmMask };

    BITMAP bm = {};
    HBITMAP source = info.hbmColor ? info.hbmColor : info.hbmMask;
    if (source == nullptr || GetObjectW(source, sizeof(bm), &bm) == 0) {
        return false;
    }

    const int width  = bm.bmWidth;
    /* Le icone monocromatiche impacchettano AND+XOR nella stessa bitmap. */
    const int height = info.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
        return false;
    }

    /* v3.7.2: guardie RAII da ScopeGuards.h per DC dello schermo, DC di
     * memoria, DIB e selezione: nessun cleanup manuale sui quattro
     * percorsi di uscita, niente leak nemmeno con modifiche future. */
    WindowDcGuard screenDc(nullptr, GetDC(nullptr));
    if (!screenDc.valid()) {
        return false;
    }
    MemDcGuard memDc(screenDc);
    if (!memDc.valid()) {
        return false;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = width;
    bi.bmiHeader.biHeight      = -height; /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    UniqueGdiObject dib(CreateDIBSection(memDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!dib.valid() || bits == nullptr) {
        return false;
    }

    SelectGuard dibSel(memDc, dib);
    ZeroMemory(bits, static_cast<size_t>(width) * height * 4);

    /* DrawIconEx compone correttamente sia le icone a 32bpp con canale alfa
     * sia quelle legacy con maschera AND. */
    const BOOL drawn = DrawIconEx(memDc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);

    bool ok = false;
    if (drawn) {
        const size_t byteCount = static_cast<size_t>(width) * height * 4;
        out.width  = width;
        out.height = height;
        out.pixels.resize(byteCount);
        memcpy(out.pixels.data(), bits, byteCount);

        /* Se l'icona non ha canale alfa, DrawIconEx lascia alpha = 0 ovunque:
         * in quel caso ricostruiamo l'alfa dalla maschera AND. */
        bool hasAlpha = false;
        for (size_t i = 3; i < byteCount; i += 4) {
            if (out.pixels[i] != 0) {
                hasAlpha = true;
                break;
            }
        }

        if (!hasAlpha && info.hbmMask != nullptr) {
            std::vector<uint8_t> maskBits(byteCount, 0);
            BITMAPINFO mbi = bi;
            MemDcGuard maskDc(screenDc);
            if (maskDc.valid()) {
                GetDIBits(maskDc, info.hbmMask, 0, static_cast<UINT>(height),
                          maskBits.data(), &mbi, DIB_RGB_COLORS);
                for (size_t i = 0; i < byteCount; i += 4) {
                    /* maschera AND: nero (0) = opaco, bianco = trasparente */
                    const bool transparent = maskBits[i] != 0;
                    out.pixels[i + 3] = transparent ? 0 : 255;
                    if (transparent) {
                        out.pixels[i + 0] = 0;
                        out.pixels[i + 1] = 0;
                        out.pixels[i + 2] = 0;
                    }
                }
            } else {
                for (size_t i = 3; i < byteCount; i += 4) {
                    out.pixels[i] = 255;
                }
            }
        }

        /* WPF Pbgra32 richiede il colore premoltiplicato per l'alfa. */
        for (size_t i = 0; i < byteCount; i += 4) {
            const uint32_t a = out.pixels[i + 3];
            if (a == 255) {
                continue;
            }
            out.pixels[i + 0] = static_cast<uint8_t>(out.pixels[i + 0] * a / 255);
            out.pixels[i + 1] = static_cast<uint8_t>(out.pixels[i + 1] * a / 255);
            out.pixels[i + 2] = static_cast<uint8_t>(out.pixels[i + 2] * a / 255);
        }
        ok = true;
    }

    /* dibSel, dib, memDc e screenDc escono di scope qui: deselezione,
     * DeleteObject, DeleteDC e ReleaseDC li fanno le guardie. */
    return ok;
}

int32_t EmitBitmap(const ArgbBitmap& bmp, int32_t* width, int32_t* height,
                   uint8_t* pixels, int32_t pixelsBytes) {
    if (width == nullptr || height == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    if (bmp.empty()) {
        *width = 0;
        *height = 0;
        return W7T_ERR_NOT_FOUND;
    }

    *width  = bmp.width;
    *height = bmp.height;

    const int32_t needed = bmp.width * bmp.height * 4;
    if (pixels == nullptr) {
        /* Sola interrogazione delle dimensioni. */
        return needed;
    }
    if (pixelsBytes < needed) {
        return W7T_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(pixels, bmp.pixels.data(), static_cast<size_t>(needed));
    return needed;
}

/* ------------------------------------------------------------------ */
/*  Processi                                                           */
/* ------------------------------------------------------------------ */

std::wstring GetProcessImagePath(DWORD pid) {
    if (pid == 0) {
        return std::wstring();
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return std::wstring();
    }
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        result.assign(buffer, size);
    }
    CloseHandle(process);
    return result;
}

namespace {
constexpr int kResolveMaxPath = 520;
} /* namespace */

/* ------------------------------------------------------------------ */
/*  v1.7.2: icone reali delle app PACCHETTIZZATE (UWP/Store)            */
/* ------------------------------------------------------------------ */

namespace {

/* HBITMAP 32bpp -> HICON con canale alpha preservato. Le bitmap che
 * IShellItemImageFactory::GetImage consegna non sono icone: serve il
 * giro ICONINFO (maschera tutta zero = opaco, il canale alpha del
 * bitmap colore fa il resto). CreateIconIndirect copia i bitmap: quelli
 * temporanei si possono distruggere subito. */
HICON HiconFromArgbDib(HBITMAP source) {
    BITMAP bm = {};
    if (GetObjectW(source, sizeof(bm), &bm) == 0) {
        return nullptr;
    }
    const int w = bm.bmWidth;
    const int h = bm.bmHeight > 0 ? bm.bmHeight : -bm.bmHeight;
    if (w <= 0 || h <= 0 || w > 512 || h > 512) {
        return nullptr;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;      /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BITMAPINFO mi = bi;
    mi.bmiHeader.biHeight = h;            /* la maschera resta bottom-up */

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) return nullptr;
    void* colorBits = nullptr;
    void* maskBits = nullptr;
    HBITMAP color = CreateDIBSection(screen, &bi, DIB_RGB_COLORS,
                                     &colorBits, nullptr, 0);
    HBITMAP mask = CreateDIBSection(screen, &mi, DIB_RGB_COLORS,
                                    &maskBits, nullptr, 0);
    bool ok = false;
    if (color != nullptr && mask != nullptr && colorBits != nullptr &&
        maskBits != nullptr) {
        HDC mem = CreateCompatibleDC(screen);
        if (mem != nullptr) {
            HGDIOBJ old = SelectObject(mem, color);
            /* copia i pixel 32bpp cosi' come sono (nessun filtraggio GDI) */
            if (GetDIBits(mem, source, 0, static_cast<UINT>(h),
                          colorBits, &bi, DIB_RGB_COLORS) != 0) {
                memset(maskBits, 0,
                       static_cast<size_t>((w + 15) / 16 * 2) * h);
                ok = true;
            }
            SelectObject(mem, old);
            DeleteDC(mem);
        }
    }
    ReleaseDC(nullptr, screen);

    HICON out = nullptr;
    if (ok) {
        ICONINFO ii = {};
        ii.fIcon    = TRUE;
        ii.hbmMask  = mask;
        ii.hbmColor = color;
        out = CreateIconIndirect(&ii);
    }
    if (color != nullptr) DeleteObject(color);
    if (mask != nullptr) DeleteObject(mask);
    return out;
}

/* Dall'AppUserModelID all'icona: l'elemento della cartella
 * shell:AppsFolder esposto dall'app. API pubbliche e documentate, lo
 * stesso percorso di RetroBar/ManagedShell per le app dello Store. */
HICON GetAppsFolderIcon(const wchar_t* aumid, int size) {
    if (aumid == nullptr || aumid[0] == 0) {
        return nullptr;
    }
    try {
        std::wstring path = L"shell:AppsFolder\\";
        path += aumid;

        IShellItem* item = nullptr;
        if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                               IID_PPV_ARGS(&item))) ||
            item == nullptr) {
            return nullptr;
        }
        struct ReleaseItem {
            IShellItem* p;
            ~ReleaseItem() { if (p) p->Release(); }
        } itemGuard{ item };

        HICON out = nullptr;
        IShellItemImageFactory* factory = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&factory))) &&
            factory != nullptr) {
            struct ReleaseFactory {
                IShellItemImageFactory* p;
                ~ReleaseFactory() { if (p) p->Release(); }
            } factoryGuard{ factory };
            HBITMAP bmp = nullptr;
            const SIZE box = { size > 0 ? size : 32, size > 0 ? size : 32 };
            if (SUCCEEDED(factory->GetImage(box, SIIGBF_ICONONLY, &bmp)) &&
                bmp != nullptr) {
                UniqueGdiObject bmpGuard(bmp);
                out = HiconFromArgbDib(bmp);
            }
        }
        return out;
    } catch (...) {
        return nullptr;
    }
}

/* COM per-thread: se il thread non l'ha ancora, lo inizializza MTA (le
 * factory shell sono agnostiche); se il thread ha gia' una modalita'
 * diversa, RPC_E_CHANGED_MODE va bene: il COM del thread e' gia' pronto. */
class ComScope {
public:
    ComScope() : m_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    HRESULT m_hr;
};

} /* namespace */

HICON GetWindowPackagedIcon(HWND hwnd, int size) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return nullptr;
    }
    W7T_SEH_TRY {
        ComScope com;
        IPropertyStore* store = nullptr;
        if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) ||
            store == nullptr) {
            return nullptr;
        }
        PROPVARIANT pv;
        PropVariantInit(&pv);
        HICON out = nullptr;
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal != nullptr &&
            pv.pwszVal[0] != L'\0') {
            out = GetAppsFolderIcon(pv.pwszVal, size);
        }
        PropVariantClear(&pv);
        store->Release();
        return out;
    } W7T_SEH_CATCH {} W7T_SEH_END
    return nullptr;
}

HICON GetLnkPackagedIcon(const wchar_t* lnk, int size) {
    if (lnk == nullptr || lnk[0] == 0) {
        return nullptr;
    }
    W7T_SEH_TRY {
        ComScope com;
        IPropertyStore* store = nullptr;
        if (FAILED(SHGetPropertyStoreFromParsingName(
                lnk, nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))) ||
            store == nullptr) {
            return nullptr;
        }
        PROPVARIANT pv;
        PropVariantInit(&pv);
        HICON out = nullptr;
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal != nullptr &&
            pv.pwszVal[0] != L'\0') {
            out = GetAppsFolderIcon(pv.pwszVal, size);
        }
        PropVariantClear(&pv);
        store->Release();
        return out;
    } W7T_SEH_CATCH {} W7T_SEH_END
    return nullptr;
}

/* v1.21.8: resolves a path STORED INSIDE A SHORTCUT (icon location) the way
 * the shell does, before handing it to ExtractIconExW.
 *
 * IShellLink::GetIconLocation returns the string the shortcut holds, and that
 * string is not necessarily an absolute, unquoted path:
 *
 *   - it can be quoted ("C:\icons\my icon.ico",0) - the shortcut property
 *     sheet accepts it and Explorer resolves it;
 *   - it can be relative ("Discord.ico"), which the shell resolves against the
 *     folder of the shortcut itself;
 *   - it can contain environment variables (%ProgramFiles%\...).
 *
 * ExtractIconExW resolves none of the three, so a custom icon stored in one of
 * these forms used to fail and the caller fell back to the icon of the target
 * executable - i.e. the user's custom icon was replaced by the default one.
 * Same rule as the target normalisation above; nothing is invented: a path
 * that cannot be resolved returns false and the caller keeps its fallbacks. */
bool ResolveStoredShortcutPath(const wchar_t* lnk, const wchar_t* stored,
                               wchar_t* out, size_t cch) {
    if (stored == nullptr || stored[0] == 0 || out == nullptr || cch == 0) {
        return false;
    }

    wchar_t expanded[kResolveMaxPath]{};
    if (ExpandEnvironmentStringsW(stored, expanded, kResolveMaxPath) == 0) {
        StringCchCopyW(expanded, ARRAYSIZE(expanded), stored);
    }
    PathUnquoteSpacesW(expanded);
    if (expanded[0] == 0) {
        return false;
    }

    wchar_t full[kResolveMaxPath]{};
    if (PathIsRelativeW(expanded)) {
        /* Documented shell behaviour: a relative icon location is relative
         * to the folder that contains the shortcut. */
        if (lnk == nullptr || lnk[0] == 0) {
            return false;
        }
        wchar_t folder[kResolveMaxPath]{};
        StringCchCopyW(folder, ARRAYSIZE(folder), lnk);
        PathRemoveFileSpecW(folder);
        if (folder[0] == 0 ||
            PathCombineW(full, folder, expanded) == nullptr) {
            return false;
        }
    } else {
        StringCchCopyW(full, ARRAYSIZE(full), expanded);
    }

    wchar_t canonical[kResolveMaxPath]{};
    if (GetFullPathNameW(full, ARRAYSIZE(canonical), canonical, nullptr) == 0 ||
        canonical[0] == 0) {
        StringCchCopyW(canonical, ARRAYSIZE(canonical), full);
    }
    return SUCCEEDED(StringCchCopyW(out, cch, canonical));
}

HICON ResolveAppIcon(const wchar_t* lnk, const wchar_t* target, bool large) {
    /* 1.0.0-alpha: 'small' NON e' un nome sicuro per una variabile locale.
     * Il Windows SDK (rpcndr.h) definisce, quando si compila con MSVC,
     *     #define small char
     * per compatibilita' con il vecchio MIDL: il compilatore vede quindi
     * "char = nullptr" e si ferma. MinGW-w64 non definisce quella macro,
     * per questo la build di riferimento (le DLL in dist/) compilava e
     * quella con MSVC no. Da qui in avanti: smallIcon / bigIcon. */
    /* v2.26: MAI SHGetFileInfo sul .lnk: la shell ci compone sopra la
     * freccia "collegamento". L'icona dell'applicazione si estrae dal
     * percorso dichiarato dal lnk (GetIconLocation + ExtractIconEx) e,
     * in subordine, dall'eseguibile target: entrambe prive di overlay.
     * E' lo stesso criterio usato da RetroBar/ExplorerPatcher per la
     * taskbar e da Open-Shell per i menu. */
    const DWORD f = SHGFI_ICON | (large ? SHGFI_LARGEICON : SHGFI_SMALLICON);

    /* v2.29: normalizza il target (variabili d'ambiente, virgolette):
     * la scansione della ricerca passa percorsi grezzi come
     * %ProgramFiles%\... che altrimenti fallirebbero in ExtractIconEx. */
    std::wstring normTarget;
    if (target != nullptr && target[0] != 0) {
        wchar_t exp[kResolveMaxPath]{};
        ExpandEnvironmentStringsW(target, exp, kResolveMaxPath);
        PathUnquoteSpacesW(exp);
        normTarget = exp;
        target = normTarget.c_str();
    }

    if (lnk != nullptr && lnk[0] != 0) {
        /* v1.21.8: filled when the shortcut names an icon that
         * ExtractIconExW cannot read. The shell is asked for that same file
         * after the COM scope below, so the SEH guard is never nested. */
        std::wstring iconFallbackPath;

        W7T_SEH_TRY {
            IShellLinkW* link = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
                IPersistFile* pf = nullptr;
                if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
                    if (SUCCEEDED(pf->Load(lnk, STGM_READ))) {
                        wchar_t iconPath[MAX_PATH]{};
                        int index = 0;
                        if (SUCCEEDED(link->GetIconLocation(iconPath,
                                MAX_PATH, &index)) && iconPath[0] != 0) {
                            /* v1.21.8: quoted or relative icon locations are
                             * resolved before extraction, so a custom icon is
                             * not lost (see ResolveStoredShortcutPath). */
                            wchar_t iconResolved[kResolveMaxPath]{};
                            if (ResolveStoredShortcutPath(lnk, iconPath,
                                    iconResolved, kResolveMaxPath)) {
                                HICON bigIcon = nullptr, smallIcon = nullptr;
                                if (ExtractIconExW(iconResolved, index, &bigIcon,
                                        &smallIcon, 1) > 0) {
                                    HICON pick = large ? bigIcon : smallIcon;
                                    HICON other = large ? smallIcon : bigIcon;
                                    if (pick) {
                                        if (other) DestroyIcon(other);
                                        pf->Release();
                                        link->Release();
                                        return pick;
                                    }
                                    if (bigIcon) DestroyIcon(bigIcon);
                                    if (smallIcon) DestroyIcon(smallIcon);
                                }

                                /* The extraction failed even though the
                                 * shortcut names an icon: remember the path
                                 * and let the shell have a try below. */
                                iconFallbackPath = iconResolved;
                            }
                        }
                    }
                    pf->Release();
                }
                link->Release();
            }
        } W7T_SEH_CATCH {} W7T_SEH_END

        /* Second chance, on the file the shortcut itself names and never on
         * the .lnk (which would carry the link overlay): the shell can draw an
         * icon for a file ExtractIconExW does not read. Only reached when the
         * extraction above failed, so a working icon is never replaced by a
         * generic one, and it keeps priority over the packaged and target
         * fallbacks below because the shortcut is the user's own choice. */
        if (!iconFallbackPath.empty()) {
            SHFILEINFOW sfiIcon{};
            W7T_SEH_TRY {
                if (SHGetFileInfoW(iconFallbackPath.c_str(), 0, &sfiIcon,
                        sizeof(sfiIcon), f) && sfiIcon.hIcon != nullptr) {
                    return sfiIcon.hIcon;
                }
            } W7T_SEH_CATCH {} W7T_SEH_END
        }

        /* v1.7.2: scorciatoie di app pacchettizzate (UWP): il lnk non ha
         * GetIconLocation utile (il glifo e' nel pacchetto). L'AppUserModelID
         * salvato nel lnk + la cartella shell:AppsFolder danno l'icona vera. */
        HICON packaged = GetLnkPackagedIcon(lnk, large ? 48 : 16);
        if (packaged != nullptr) {
            return packaged;
        }
    }

    if (target != nullptr && target[0] != 0) {
        W7T_SEH_TRY {
            HICON bigIcon = nullptr, smallIcon = nullptr;
            if (ExtractIconExW(target, 0, &bigIcon, &smallIcon, 1) > 0) {
                HICON pick = large ? bigIcon : smallIcon;
                HICON other = large ? smallIcon : bigIcon;
                if (pick) {
                    if (other) DestroyIcon(other);
                    return pick;
                }
                if (bigIcon) DestroyIcon(bigIcon);
                if (smallIcon) DestroyIcon(smallIcon);
            }
        } W7T_SEH_CATCH {} W7T_SEH_END

        /* Eseguibile reale: SHGetFileInfo su un file normale non ha
         * overlay di collegamento. */
        SHFILEINFOW sfi{};
        W7T_SEH_TRY {
            if (SHGetFileInfoW(target, 0, &sfi, sizeof(sfi), f)) {
                return sfi.hIcon;
            }
        } W7T_SEH_CATCH {} W7T_SEH_END
    }

    /* Ultima spiaggia: icona generica per estensione (non richiede che
     * il file esista). */
    SHFILEINFOW sfi2{};
    W7T_SEH_TRY {
        if (SHGetFileInfoW(L".exe", 0, &sfi2, sizeof(sfi2),
                           f | SHGFI_USEFILEATTRIBUTES)) {
            return sfi2.hIcon;
        }
    } W7T_SEH_CATCH {} W7T_SEH_END
    return nullptr;
}

/* v2.28: alcuni processi (taskmgr.exe e altri processi protetti) negano
 * OpenProcess anche con PROCESS_QUERY_LIMITED_INFORMATION: senza percorso
 * eseguibile l'identita' finiva a "pid:N" e ogni finestra del programma
 * sembrava un'app diversa. Lo snapshot Toolhelp legge i dati dal kernel
 * senza aprire handle: funziona anche sui processi protetti. */
std::wstring GetProcessNameFromSnapshot(DWORD pid) {
    if (pid == 0) return {};
    std::wstring name;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                name = pe.szExeFile;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return name;
}

std::wstring ComputeAppId(HWND hwnd, DWORD pid, const std::wstring& exePath) {
    /* control.exe hosts classic Control Panel applets. Give only that real
     * executable a stable identity before consulting any window AUMID; all
     * Explorer, UWP and ordinary fallback grouping remains untouched. */
    const size_t identitySlash = exePath.find_last_of(L"\\/");
    const std::wstring identityExeName =
        (identitySlash == std::wstring::npos)
            ? exePath : exePath.substr(identitySlash + 1);

    if (_wcsicmp(identityExeName.c_str(), L"control.exe") == 0) {
        return std::wstring(L"w7t:control-exe");
    }

    /* Read the window properties once, but don't return the AUMID yet.
     * Explorer-hosted Control Panel windows commonly publish Explorer's own
     * AUMID, so returning it here would make the more specific test below
     * unreachable. */
    std::wstring explicitAppId;
    std::wstring relaunchCommand;
    IPropertyStore* store = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) &&
        store != nullptr) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
            explicitAppId = pv.pwszVal;
        }
        PropVariantClear(&pv);

        PropVariantInit(&pv);
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_RelaunchCommand, &pv)) &&
            pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
            relaunchCommand = pv.pwszVal;
        }
        PropVariantClear(&pv);
        store->Release();
    }

    /* 1) An Explorer-hosted Control Panel page is not File Explorer.
     * Restrict this to explorer.exe + CabinetWClass, then recognize either
     * the canonical shell namespace in the public relaunch command or the
     * localized root caption. Ordinary folders, UWP windows and Explorer's
     * normal grouping never enter this branch. */
    if (hwnd != nullptr &&
        _wcsicmp(identityExeName.c_str(), L"explorer.exe") == 0) {
        wchar_t cls[64] = {};
        if (GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) != 0 &&
            _wcsicmp(cls, L"CabinetWClass") == 0) {
            std::wstring commandLower = relaunchCommand;
            std::transform(commandLower.begin(), commandLower.end(),
                           commandLower.begin(), [](wchar_t c) {
                               return static_cast<wchar_t>(towlower(c));
                           });
            const bool controlNamespace =
                commandLower.find(L"{26ee0668-a00a-44d7-9371-beb064c98683}") !=
                    std::wstring::npos ||
                commandLower.find(L"{21ec2020-3aea-1069-a2dd-08002b30309d}") !=
                    std::wstring::npos;

            bool controlCaption = false;
            wchar_t title[W7T_MAX_TITLE] = {};
            GetWindowTextW(hwnd, title, ARRAYSIZE(title));
            static const wchar_t* const kControlPanelNames[] = {
                L"Pannello di controllo",     /* it */
                L"Control Panel",             /* en */
                L"Panel de control",          /* es */
                L"Panneau de configuration",  /* fr */
                L"Systemsteuerung",           /* de */
                L"Painel de Controle",        /* pt-br */
                L"Painel de Controlo",        /* pt */
                L"Panel sterowania",          /* pl */
                L"\x041F\x0430\x043D\x0435\x043B\x044C \x0443\x043F\x0440\x0430\x0432\x043B\x0435\x043D\x0438\x044F", /* ru */
                L"\x30B3\x30F3\x30C8\x30ED\x30FC\x30EB \x30D1\x30CD\x30EB", /* ja */
                L"\x63A7\x5236\x9762\x677F",  /* zh */
                L"\x0644\x0648\x062D\x0629 \x0627\x0644\x062A\x062D\x0643\x0645", /* ar */
            };
            for (const wchar_t* name : kControlPanelNames) {
                if (_wcsnicmp(title, name, wcslen(name)) == 0) {
                    controlCaption = true;
                    break;
                }
            }

            if (controlNamespace || controlCaption) {
                return std::wstring(L"w7t:control-panel");
            }
        }
    }

    /* 2) Explicit AppUserModelID for every other window. */
    if (!explicitAppId.empty()) {
        return explicitAppId;
    }

    /* 3) Fallback: percorso dell'eseguibile, normalizzato in minuscolo. */
    if (!exePath.empty()) {
        std::wstring id = exePath;
        std::transform(id.begin(), id.end(), id.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return id;
    }

    /* 3) v2.28: nome eseguibile dallo snapshot Toolhelp (processi
     *    protetti): tutte le finestre dello stesso programma restano
     *    nello stesso gruppo. */
    std::wstring snapName = GetProcessNameFromSnapshot(pid);
    if (!snapName.empty()) {
        std::transform(snapName.begin(), snapName.end(), snapName.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return snapName;
    }

    /* 4) Ultima risorsa: il PID, cosi' ogni finestra resta a se'. */
    wchar_t buffer[32] = {};
    swprintf(buffer, 32, L"pid:%lu", static_cast<unsigned long>(pid));
    return std::wstring(buffer);
}

/* ------------------------------------------------------------------ */
/*  Monitor                                                            */
/* ------------------------------------------------------------------ */

namespace {

struct MonitorSearch {
    HMONITOR target;
    int32_t  index;
    int32_t  current;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MonitorSearch* search = reinterpret_cast<MonitorSearch*>(param);
    if (monitor == search->target) {
        search->index = search->current;
        return FALSE;
    }
    search->current++;
    return TRUE;
}

} /* namespace */

int32_t GetMonitorIndexForWindow(HWND hwnd) {
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MonitorSearch search{ monitor, 0, 0 };
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&search));
    return search.index;
}

/* Effective DPI of the monitor that owns a screen rectangle (device pixels
 * per 96 DIP). Popups that must be sized BEFORE their window exists (the
 * Jump List) cannot wait for WM_DPICHANGED, so the scale comes from the
 * monitor of the anchor. The loader ladder is the one AppSearchWindow uses:
 * shcore!GetDpiForMonitor (Win8.1+) with GetDeviceCaps (always present) as
 * the fallback; never below 96. */
UINT GetDpiForScreenRect(const RECT& screenRect) {
    HMONITOR mon = MonitorFromRect(&screenRect, MONITOR_DEFAULTTONEAREST);

    typedef HRESULT(WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorFn fn = []() -> GetDpiForMonitorFn {
        HMODULE m = LoadLibraryW(L"shcore.dll");
        if (!m) return nullptr;
        return reinterpret_cast<GetDpiForMonitorFn>(
            GetProcAddress(m, "GetDpiForMonitor"));
    }();

    UINT dpiX = 0, dpiY = 0;
    if (fn && mon &&
        SUCCEEDED(fn(mon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) &&
        dpiX >= 96) {
        return dpiX;
    }
    HDC dc = GetDC(nullptr);
    dpiX = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    if (dpiX < 96) dpiX = 96;
    return dpiX;
}

/* Effective DPI of a window (device pixels per 96 DIP), the same ladder
 * as GetDpiForScreenRect but anchored to the window instead of a screen
 * rectangle. GetDpiForWindow is a Windows 10 1607+ entry point and is
 * resolved dynamically here on purpose: a direct import would put
 * user32!GetDpiForWindow in the import table of Win7TaskbarCore.dll, and
 * the Windows 8.1 loader refuses a DLL whose imports it cannot bind, so
 * the whole app would fail to start there. On systems without the
 * function (Windows 8.1) the scale comes from GetDeviceCaps on the
 * window's DC (screen DC when the handle is null). Never returns < 96.
 * Behavior on Windows 10/11 is unchanged: the same function is resolved
 * and called with the same handle. */
UINT GetDpiForWindowSafe(HWND hwnd) {
    typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = []() -> GetDpiForWindowFn {
        HMODULE m = GetModuleHandleW(L"user32.dll");
        if (!m) return nullptr;
        return reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(m, "GetDpiForWindow"));
    }();

    UINT dpi = (fn != nullptr && hwnd != nullptr) ? fn(hwnd) : 0;
    if (dpi < 96) {
        HDC dc = GetDC(hwnd);
        if (dc != nullptr) {
            dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
            ReleaseDC(hwnd, dc);
        }
    }
    if (dpi < 96) dpi = 96;
    return dpi;
}

/* ------------------------------------------------------------------ */
/*  Filtro finestre                                                    */
/* ------------------------------------------------------------------ */

/* v1.21.32: explicit overrides requested through the taskbar-list protocol.
 *
 * An application may ask the taskbar to show or remove the button of one of
 * its windows (ITaskbarList::AddTab/DeleteTab). The request reaches this
 * registry from the protocol responder in TrayService. It is small and
 * process-local: a map behind a mutex, with the entries of dead windows
 * dropped as soon as the map is touched. */
namespace {

std::mutex& TaskbarOverrideMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<HWND, int>& TaskbarOverrides() {
    static std::map<HWND, int> overrides;
    return overrides;
}

} /* namespace */

void SetTaskbarListOverride(HWND hwnd, int state) {
    if (hwnd == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(TaskbarOverrideMutex());
    std::map<HWND, int>& overrides = TaskbarOverrides();
    if (state == 0) {
        overrides.erase(hwnd);
    } else {
        overrides[hwnd] = (state > 0) ? 1 : -1;
    }
}

int TaskbarListOverride(HWND hwnd) {
    if (hwnd == nullptr) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(TaskbarOverrideMutex());
    std::map<HWND, int>& overrides = TaskbarOverrides();
    auto it = overrides.find(hwnd);
    if (it == overrides.end()) {
        return 0;
    }
    if (!IsWindow(hwnd)) {
        overrides.erase(it);
        return 0;
    }
    return it->second;
}

void PruneTaskbarOverrides() {
    std::lock_guard<std::mutex> lock(TaskbarOverrideMutex());
    std::map<HWND, int>& overrides = TaskbarOverrides();
    for (auto it = overrides.begin(); it != overrides.end();) {
        if (!IsWindow(it->first)) {
            it = overrides.erase(it);
        } else {
            ++it;
        }
    }
}

bool IsTaskbarWindow(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }
    if (!IsWindowVisible(hwnd)) {
        return false;
    }

    /* Le finestre "cloaked" (UWP sospese, finestre su desktop virtuali
     * differenti) non compaiono nella taskbar. */
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
        && cloaked != 0) {
        return false;
    }

    const LONG_PTR style   = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    /* A child window is never a taskbar item, not even when an application
     * asks for it explicitly: the button belongs to the top-level window
     * that owns it. */
    if (style & WS_CHILD) {
        return false;
    }

    /* v1.21.32: an explicit request wins over the heuristics. DeleteTab
     * removes the button; AddTab adds it, including for a window the
     * heuristics below would drop - the documented case of ITaskbarList
     * ("Any type of window can be added to the taskbar"). */
    const int taskbarListCall = TaskbarListOverride(hwnd);
    if (taskbarListCall < 0) {
        return false;
    }
    if (taskbarListCall > 0) {
        return true;
    }

    if (exStyle & WS_EX_TOOLWINDOW) {
        return false;
    }

    /* Our Start Menu is a top-level WPF window in this process. It must
     * never appear as a Superbar app, even if ShowInTaskbar/TOOLWINDOW
     * did not stick on a given Windows build. */
    wchar_t caption[64] = {};
    if (GetWindowTextW(hwnd, caption, 64) > 0 &&
        wcscmp(caption, L"Win7Taskbar Start Menu") == 0) {
        return false;
    }
    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) > 0 &&
        wcsncmp(cls, L"HwndWrapper[", 12) == 0) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) {
            return false;
        }
    }

    /* Regola di RetroBar/ManagedShell (ApplicationWindow.CanAddToTaskbar):
     * le finestre WS_EX_NOACTIVATE non compaiono nella taskbar, a meno che
     * WS_EX_APPWINDOW non lo imponga. E' cosi' che RetroBar non scambia per
     * un programma il flyout dell'orologio/calendario della shell
     * (ShellExperienceHost), che e' una finestra non attivabile. */
    if ((exStyle & WS_EX_NOACTIVATE) != 0 && (exStyle & WS_EX_APPWINDOW) == 0) {
        return false;
    }

    /* WS_EX_APPWINDOW forza la presenza in taskbar anche con un owner. */
    if (exStyle & WS_EX_APPWINDOW) {
        return true;
    }

    /* Una finestra posseduta non appare, a meno che il proprietario non sia
     * a sua volta nascosto (pattern comune dei dialog "principali"). */
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner != nullptr && IsWindowVisible(owner)) {
        return false;
    }

    /* Scarta le finestre senza area client utile. */
    RECT rect = {};
    if (GetWindowRect(hwnd, &rect)) {
        if ((rect.right - rect.left) <= 1 || (rect.bottom - rect.top) <= 1) {
            return false;
        }
    }

    /* Una finestra senza titolo non compare nella taskbar di Windows: sono
     * quasi sempre finestre di servizio (popup dei menu, host di layered
     * window, finestre di messaggio dei framework grafici) che vengono create
     * e distrutte di continuo. Senza questo filtro comparirebbero pulsanti
     * fantasma per la durata di un menu aperto.
     *
     * v1.21.32: application windows, though, can be published BEFORE their
     * title arrives. Chromium/Electron (VS Codium and the other editors) and
     * tao/Tauri (Windhawk) create the window, show it and set the caption
     * only afterwards: rejecting it here kept it off the bar until the next
     * safety enumeration (seconds later) and, when a window returns to a
     * previously used title, kept it off for good. Microsoft documents that
     * an application window is meant to carry WS_CAPTION ("Any type of
     * window can be added to the taskbar, but it is recommended that the
     * window at least have the WS_CAPTION style", see ITaskbarList::AddTab
     * and the taskbar overview), so a window with a caption frame is an
     * application window even while its caption is still empty, whereas the
     * service popups that must stay off the bar (menus, tooltips, panels)
     * have no caption. The rule below therefore accepts an empty title ONLY
     * together with WS_CAPTION. */
    if (GetWindowTextLengthW(hwnd) == 0) {
        if ((style & WS_CAPTION) == 0) {
            return false;
        }
    } else {
        wchar_t title[8] = {};
        if (GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) == 0 &&
            GetLastError() != ERROR_SUCCESS) {
            return false;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  Versione di Windows                                                */
/* ------------------------------------------------------------------ */

namespace {

/* Letta una volta sola: la versione del sistema non cambia mentre il
 * processo e' in vita (e RtlGetVersion costa comunque poco, ma cosi' non
 * si ripete a ogni clic sul tray). */
struct OsVersion {
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;
    bool  valid = false;
};

const OsVersion& ReadOsVersion() {
    static const OsVersion cached = [] {
        OsVersion out;

        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll != nullptr) {
            auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
            if (fn != nullptr) {
                RTL_OSVERSIONINFOW info = {};
                info.dwOSVersionInfoSize = sizeof(info);
                if (fn(&info) == 0) {
                    out.major = info.dwMajorVersion;
                    out.minor = info.dwMinorVersion;
                    out.build = info.dwBuildNumber;
                    out.valid = true;
                }
            }
        }

        /* Se il rilevamento fallisce assumiamo il sistema piu' recente:
         * questo codice gira solo su Windows 10/11, e rispondere "no"
         * disattiverebbe funzionalita' che invece ci sono. */
        if (!out.valid) {
            out.major = 10;
            out.minor = 0;
            out.build = 26000;
            out.valid = true;
        }

        return out;
    }();

    return cached;
}

} /* namespace */

uint32_t GetWindowsBuildNumber() {
    return static_cast<uint32_t>(ReadOsVersion().build);
}

bool IsWindows8OrBetter() {
    const OsVersion& v = ReadOsVersion();
    return v.major > 6 || (v.major == 6 && v.minor >= 2);
}

bool IsWindows10OrBetter() {
    return ReadOsVersion().major >= 10;
}

bool IsWindows11OrBetter() {
    /* Windows 11 e' NT 10.0 con build >= 22000: il numero di versione
     * principale non e' cambiato, quindi l'unico discriminante e' la build. */
    const OsVersion& v = ReadOsVersion();
    return v.major > 10 || (v.major == 10 && v.build >= 22000);
}




/* v2.38: decodificatori PNG incorporati condivisi (ricerca, batteria,
 * jump list). Traslocati da AppSearchWindow.cpp. */
/* Decodificatore base64 tollerante: ignora gli spazi; un carattere
 * non valido o dati tronchi NON devono crashare (punto 14): ritorna
 * false e il chiamante rinuncia all'icona. */
bool Base64Decode(const char* s, std::vector<BYTE>& out) {
    static int tblInit = 0;
    static signed char tbl[256];
    if (!tblInit) {
        for (int i = 0; i < 256; ++i) tbl[i] = -1;
        const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; A[i]; ++i) tbl[static_cast<unsigned char>(A[i])] = static_cast<signed char>(i);
        tblInit = 1;
    }
    out.clear();
    if (!s) return false;
    out.reserve((std::strlen(s) / 4) * 3 + 3);
    uint32_t acc = 0;
    int bits = 0;
    for (const char* p = s; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (c == '=') break;
        const int v = tbl[c];
        if (v < 0) return false;          /* input corrotto */
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<BYTE>((acc >> bits) & 0xFF));
        }
    }
    return true;
}


/* Decodifica PNG base64 -> pixel 0xAARRGGBB (alpha dritto) via WIC.
 * opzionalmente: ritaglio sul bounding-box dell'alpha + riduzione ad
 * altezza target mantenendo le proporzioni (alta qualita', una volta
 * sola all'avvio). Ogni errore -> false, nessun crash (punto 14). */
bool DecodeEmbeddedPng(const char* b64, std::vector<uint32_t>& out,
                       int& outW, int& outH,
                       bool cropAlpha, int targetH) {
    out.clear(); outW = outH = 0;
    std::vector<BYTE> png;
    if (!Base64Decode(b64, png) || png.size() < 8) return false;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needCoUninit = SUCCEEDED(hrCo);

    bool ok = false;
    try {
        IWICImagingFactory* fac = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&fac));
        if (SUCCEEDED(hr) && fac) {
            IStream* stream = SHCreateMemStream(png.data(),
                                                static_cast<UINT>(png.size()));
            if (stream) {
                IWICBitmapDecoder* dec = nullptr;
                if (SUCCEEDED(fac->CreateDecoderFromStream(
                        stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && dec) {
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
                                    std::vector<uint32_t> full(static_cast<size_t>(w) * h);
                                    for (size_t i = 0; i < full.size(); ++i) {
                                        const BYTE* p = &raw[i * 4];
                                        full[i] = (static_cast<uint32_t>(p[3]) << 24) |
                                                  (static_cast<uint32_t>(p[0]) << 16) |
                                                  (static_cast<uint32_t>(p[1]) << 8) |
                                                  static_cast<uint32_t>(p[2]);
                                    }
                                    UINT cx = 0, cy = 0, cw = w, ch = h;
                                    if (cropAlpha) {
                                        UINT x0 = w, y0 = h, x1 = 0, y1 = 0;
                                        for (UINT yy = 0; yy < h; ++yy)
                                            for (UINT xx = 0; xx < w; ++xx)
                                                if ((full[yy * w + xx] >> 24) > 8) {
                                                    if (xx < x0) x0 = xx;
                                                    if (yy < y0) y0 = yy;
                                                    if (xx > x1) x1 = xx;
                                                    if (yy > y1) y1 = yy;
                                                }
                                        if (x1 >= x0 && y1 >= y0) {
                                            cx = x0; cy = y0;
                                            cw = x1 - x0 + 1; ch = y1 - y0 + 1;
                                        }
                                    }
                                    UINT tw = cw, th = ch;
                                    if (targetH > 0 && static_cast<int>(ch) > targetH) {
                                        th = static_cast<UINT>(targetH);
                                        tw = std::max<UINT>(1, (static_cast<uint64_t>(cw) * th) / ch);
                                    }
                                    if (cw == w && ch == h && tw == w && th == h) {
                                        out = std::move(full);
                                        outW = static_cast<int>(w);
                                        outH = static_cast<int>(h);
                                        ok = true;
                                    } else {
                                        /* Ritaglio + scala: ownership chiara:
                                         * 'base' ha sempre UNA ref di troppo
                                         * che rilasciamo alla fine. */
                                        IWICBitmapClipper* clip = nullptr;
                                        bool haveCrop = false;
                                        if (cx != 0 || cy != 0 || cw != w || ch != h) {
                                            WICRect rc{ static_cast<INT>(cx), static_cast<INT>(cy),
                                                        static_cast<INT>(cw), static_cast<INT>(ch) };
                                            haveCrop =
                                                SUCCEEDED(fac->CreateBitmapClipper(&clip)) &&
                                                clip &&
                                                SUCCEEDED(clip->Initialize(conv, &rc));
                                            if (!haveCrop && clip) { clip->Release(); clip = nullptr; }
                                        }
                                        IWICBitmapSource* base =
                                            (haveCrop && clip)
                                                ? static_cast<IWICBitmapSource*>(clip)
                                                : static_cast<IWICBitmapSource*>(conv);
                                        if (!haveCrop) conv->AddRef();
                                        IWICBitmapScaler* scaler = nullptr;
                                        if (SUCCEEDED(fac->CreateBitmapScaler(&scaler)) && scaler &&
                                            SUCCEEDED(scaler->Initialize(base, tw, th,
                                                                         WICBitmapInterpolationModeHighQualityCubic))) {
                                            IWICFormatConverter* conv2 = nullptr;
                                            if (SUCCEEDED(fac->CreateFormatConverter(&conv2)) && conv2 &&
                                                SUCCEEDED(conv2->Initialize(scaler, GUID_WICPixelFormat32bppRGBA,
                                                                            WICBitmapDitherTypeNone, nullptr,
                                                                            0.0, WICBitmapPaletteTypeCustom))) {
                                                UINT fw = 0, fh = 0;
                                                if (SUCCEEDED(conv2->GetSize(&fw, &fh)) && fw > 0 && fh > 0) {
                                                    std::vector<BYTE> fin(static_cast<size_t>(fw) * fh * 4);
                                                    if (SUCCEEDED(conv2->CopyPixels(nullptr, fw * 4,
                                                                                    static_cast<UINT>(fin.size()),
                                                                                    fin.data()))) {
                                                        out.resize(static_cast<size_t>(fw) * fh);
                                                        for (size_t i = 0; i < out.size(); ++i) {
                                                            const BYTE* p = &fin[i * 4];
                                                            out[i] = (static_cast<uint32_t>(p[3]) << 24) |
                                                                     (static_cast<uint32_t>(p[0]) << 16) |
                                                                     (static_cast<uint32_t>(p[1]) << 8) |
                                                                     static_cast<uint32_t>(p[2]);
                                                        }
                                                        outW = static_cast<int>(fw);
                                                        outH = static_cast<int>(fh);
                                                        ok = true;
                                                    }
                                                }
                                                conv2->Release();
                                            }
                                            scaler->Release();
                                        }
                                        base->Release();
                                    }
                                }
                            }
                        }
                        if (conv) conv->Release();
                        frame->Release();
                    }
                    dec->Release();
                }
                stream->Release();
            }
            fac->Release();
        }
    } catch (...) { ok = false; out.clear(); outW = outH = 0; }

    if (needCoUninit) CoUninitialize();
    return ok;
}


/* v2.38: HBITMAP 32bpp PREMULTIPLICATO da pixel ARGB ad alpha dritto. */
HBITMAP MakeHBitmapFromArgb(const std::vector<uint32_t>& px, int w, int h) {
    if (w <= 0 || h <= 0 || px.size() < static_cast<size_t>(w) * h) return nullptr;
    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h;          /* top-down */
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&bih),
                                  DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hb || !bits) { if (hb) DeleteObject(hb); return nullptr; }
    uint32_t* dst = static_cast<uint32_t*>(bits);
    for (int i = 0; i < w * h; ++i) {
        const uint32_t s = px[i];
        const uint32_t a = (s >> 24) & 0xFF;
        const uint32_t r = ((s >> 16) & 0xFF) * a / 255;
        const uint32_t g = ((s >> 8) & 0xFF) * a / 255;
        const uint32_t b = (s & 0xFF) * a / 255;
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return hb;
}

void DrawBitmapScaled(HDC hdc, HBITMAP hb, int dw, int dh, int dx, int dy) {
    if (!hb) return;
    BITMAP bm{};
    if (GetObjectW(hb, sizeof(bm), &bm) != sizeof(bm)) return;
    HDC src = CreateCompatibleDC(hdc);
    HBITMAP ob = static_cast<HBITMAP>(SelectObject(src, hb));
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(hdc, dx, dy, dw, dh, src, 0, 0, bm.bmWidth, bm.bmHeight, bf);
    SelectObject(src, ob);
    DeleteDC(src);
}

bool OwnerModuleIs(HWND ownerHwnd, const wchar_t* moduleName) {
    if (ownerHwnd == nullptr || moduleName == nullptr) return false;
    if (!IsWindow(ownerHwnd)) return false;
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(ownerHwnd, GWLP_HINSTANCE));
    if (hInst == nullptr) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(ownerHwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                               FALSE, pid);
    if (hProc == nullptr) {
        hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (hProc == nullptr) return false;
    wchar_t path[MAX_PATH] = {};
    bool result = false;
    if (GetModuleFileNameExW(hProc, hInst, path, MAX_PATH) > 0) {
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        result = (_wcsicmp(name, moduleName) == 0);
    }
    CloseHandle(hProc);
    return result;
}

} /* namespace w7t */

bool w7t::BitmapSane(const ArgbBitmap& bmp) {
    if (bmp.width < 1 || bmp.width > 4096 || bmp.height < 1 || bmp.height > 4096) {
        return false;
    }
    return bmp.pixels.size() == static_cast<size_t>(bmp.width) * bmp.height * 4u;
}

void w7t::AppendCoreLog(const wchar_t* line) {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return;
    }
    std::wstring full(path);
    const size_t cut = full.find_last_of(L"\\//");
    if (cut != std::wstring::npos) {
        full.resize(cut + 1);
    } else {
        full.clear();
    }
    full += L"log-core.txt";

    HANDLE file = CreateFileW(full.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    /* v1.21.18: one header line per process, so every log says which build
     * wrote it. "local" means the DLL was compiled without the release stamp. */
    static volatile LONG s_stampWritten = 0;
    if (InterlockedCompareExchange(&s_stampWritten, 1, 0) == 0) {
        wchar_t header[192] = {};
        const int headerChars = wsprintfW(
            header, L"=== Win7Taskbar core build: %s ===\r\n", W7T_BUILD_STAMP);
        if (headerChars > 0) {
            DWORD done = 0;
            WriteFile(file, header,
                      static_cast<DWORD>(headerChars) * sizeof(wchar_t), &done, nullptr);
        }
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t buffer[640];
    const int written = wsprintfW(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
                                  now.wYear, now.wMonth, now.wDay, now.wHour,
                                  now.wMinute, now.wSecond, now.wMilliseconds, line);
    if (written > 0) {
        DWORD done = 0;
        SetFilePointer(file, 0, nullptr, FILE_END);
        WriteFile(file, buffer, static_cast<DWORD>(written) * sizeof(wchar_t), &done, nullptr);
    }
    CloseHandle(file);
}

/* v2.63: log con etichetta fissa. Serve a isolare classi di problemi
 * (impostazioni, porta dei riquadri, clic della tray) dal resto del file:
 * l'utente allega log-core.txt e si legge solo quello che serve. */
void w7t::LogTagged(const wchar_t* tag, const wchar_t* fmt, ...) {
    if (tag == nullptr || fmt == nullptr) {
        return;
    }
    wchar_t body[512] = {};
    va_list args;
    va_start(args, fmt);
    const int written = wvsprintfW(body, fmt, args);
    va_end(args);

    wchar_t line[640] = {};
    wsprintfW(line, L"[%s] %s", tag, body);
    AppendCoreLog(line);
}


/* Lettura della stessa chiave che legge la shell:
 * HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\EnableAutoTray.
 * Assente = 1 ("Nascondi le icone e le notifiche inattive" attivo), come
 * nel sistema reale. Nessuna euristica sul contenuto dell'icona: la
 * decisione di visibilita' di un'icona nuova usa la regola espressa
 * dall'utente nel pannello di personalizzazione della tray. */
bool w7t::IsAutoTrayEnabled() {
    DWORD value = 1;
    DWORD size  = sizeof(value);
    DWORD type  = 0;
    HKEY  key   = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, L"EnableAutoTray", nullptr, &type,
                             reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS
            || type != REG_DWORD || size != sizeof(value)) {
            value = 1;
        }
        RegCloseKey(key);
    }
    return value != 0;
}

/* Hash FNV-1a sui pixel: due fotogrammi identici producono lo stesso hash.
 * E' il criterio con cui si decide se un'icona va ricaricata: la revisione
 * aumenta solo quando i pixel cambiano davvero, cosi' il livello gestito
 * non ricostruisce mai un BitmapSource inutile (= nessun flickering). */
uint64_t w7t::ArgbHash(const ArgbBitmap& bmp) {
    if (bmp.pixels.empty()) {
        return 0;
    }
    uint64_t hash = 1469598103934665603ull;   /* offset FNV-1a 64 */
    const uint8_t* p = bmp.pixels.data();
    for (size_t i = 0; i < bmp.pixels.size(); ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    /* Le dimensioni fanno parte dell'identita' del fotogramma. */
    hash ^= static_cast<uint64_t>(static_cast<uint32_t>(bmp.width));
    hash *= 1099511628211ull;
    hash ^= static_cast<uint64_t>(static_cast<uint32_t>(bmp.height));
    hash *= 1099511628211ull;
    return hash;
}
