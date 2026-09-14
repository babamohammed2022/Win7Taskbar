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

#ifndef W7T_COMMON_H
#define W7T_COMMON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <shellapi.h>
#include <mutex>
#include <deque>
#include <string>
#include <vector>
#include <cstdint>

#include "Win7TaskbarCore.h"

namespace w7t {

/* Evento accodato in attesa di essere pompato dal thread UI. */
struct QueuedEvent {
    int32_t  evt;
    uint64_t a;
    uint64_t b;
};

/* Bitmap ARGB premoltiplicata, top-down. */
struct ArgbBitmap {
    int32_t              width  = 0;
    int32_t              height = 0;
    std::vector<uint8_t> pixels; /* BGRA, width*height*4 */

    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
    void clear() {
        width = height = 0;
        pixels.clear();
        pixels.shrink_to_fit();
    }
};

/* Stato globale condiviso fra i moduli del core. */
class CoreState {
public:
    static CoreState& Instance();

    bool IsInitialized() const { return m_initialized; }
    void SetInitialized(bool v) { m_initialized = v; }

    void SetCallback(W7T_EventCallback cb) { m_callback = cb; }
    W7T_EventCallback Callback() const { return m_callback; }

    /* Accoda un evento (thread-safe, non rientra nel managed layer). */
    void QueueEvent(int32_t evt, uint64_t a, uint64_t b);

    /* Estrae fino a maxEvents eventi. */
    std::vector<QueuedEvent> DrainEvents(int32_t maxEvents);

private:
    CoreState() = default;
    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;

    bool                    m_initialized = false;
    W7T_EventCallback       m_callback    = nullptr;
    std::mutex              m_queueMutex;
    std::deque<QueuedEvent> m_queue;
    static const size_t     kMaxQueue = 4096;
};

/* Copia una stringa wide in un buffer fisso, sempre NUL-terminata. */
void CopyToFixed(wchar_t* dest, size_t destCount, const wchar_t* src);
void CopyToFixed(wchar_t* dest, size_t destCount, const std::wstring& src);

/* Converte un HICON in bitmap BGRA premoltiplicata top-down. */
bool IconToArgb(HICON icon, ArgbBitmap& out);

/* Una bitmap malformata (dimensioni nulle o folli, buffer che non
 * corrisponde a larghezza*altezza*4) farebbe fault dentro milcore/WPF al
 * primo disegno: si scarta prima di consegnarla al livello gestito. */
bool BitmapSane(const ArgbBitmap& bmp);



/* Diagnostica: una riga di log accanto all'eseguibile. Se un fault ritorna,
 * l'ultima riga dice esattamente in quale messaggio e' avvenuto. */
void AppendCoreLog(const wchar_t* line);

/* v2.63: riga di log CON ETICHETTA, per ritrovare una classe di problemi
 * senza leggere tutto il file: LogTagged(L"SETTINGS", L"...") scrive
 * "[SETTINGS] ...". Formattazione printf-style come wsprintfW (che il core
 * usa gia'): nessuna macro variadica, cosi' MSVC e MinGW-w64 si comportano
 * allo stesso modo. */
void LogTagged(const wchar_t* tag, const wchar_t* fmt, ...);

/* v2.26: resolver icone UNICO del progetto (pin + ricerca). Estrae
 * l'icona SENZA overlay freccia: GetIconLocation/ExtractIconEx dal lnk,
 * poi icona dell'eseguibile target. Mai SHGetFileInfo sul .lnk (la shell
 * comporrebbe la freccia "collegamento"). Restituisce HICON di proprieta'
 * del chiamante (DestroyIcon) o nullptr. */
HICON ResolveAppIcon(const wchar_t* lnk, const wchar_t* target, bool large);

/* v1.7.2: real icon for packaged (UWP) apps. Their windows are hosted by
 * ApplicationFrameHost.exe and store shortcuts carry no usable icon path,
 * so the classic chain ends in a generic glyph. Both helpers query the
 * window's (or the shortcut's) AppUserModelID with public property-store
 * APIs and ask the shell Apps Folder item for the tile image
 * (IShellItemImageFactory): the same icon the Start menu shows. They
 * return nullptr when the app is not packaged: callers fall through to
 * the classic chain unchanged. */
HICON GetWindowPackagedIcon(HWND hwnd, int size);
HICON GetLnkPackagedIcon(const wchar_t* lnk, int size);

/* Copia una ArgbBitmap nel buffer del chiamante secondo il protocollo
 * "query size con pixels == nullptr". */
int32_t EmitBitmap(const ArgbBitmap& bmp, int32_t* width, int32_t* height,
                   uint8_t* pixels, int32_t pixelsBytes);

/* Percorso completo dell'eseguibile di un processo. */
std::wstring GetProcessImagePath(DWORD pid);

/* Chiave di raggruppamento (AppUserModelID se presente, altrimenti exe path). */
std::wstring ComputeAppId(HWND hwnd, DWORD pid, const std::wstring& exePath);

/* Indice del monitor che contiene la finestra. */
int32_t GetMonitorIndexForWindow(HWND hwnd);

/* Effective DPI (device pixels per 96 DIP) of the monitor that owns a
 * screen rectangle, in PHYSICAL pixels. Centralized helper for popups that
 * must be sized/placed before their HWND exists (the Jump List): resolves
 * the monitor with MonitorFromRect, then GetDpiForMonitor (shcore, loaded
 * dynamically - Win8.1+), falling back to GetDpiForWindow on the desktop
 * window (Win10 1607+) and finally to GetDeviceCaps(LOGPIXELSX), same
 * ladder AppSearchWindow uses for its own panel. Never returns < 96.
 * The rect argument is in screen physical pixels, like every coordinate
 * crossing the managed boundary of this project. */
UINT GetDpiForScreenRect(const RECT& screenRect);

/* ------------------------------------------------------------------ */
/*  Versione di Windows                                                */
/*                                                                     */
/*  Rilevata con RtlGetVersion, perche' GetVersionEx e' sottoposto al   */
/*  manifest di compatibilita' e senza la dichiarazione esplicita di    */
/*  Windows 8.1/10 restituisce sempre 6.2.                              */
/* ------------------------------------------------------------------ */

bool IsWindows8OrBetter();
bool IsWindows10OrBetter();
bool IsWindows11OrBetter();

/* Numero di build, 0 se non rilevabile. */
uint32_t GetWindowsBuildNumber();

/* Finestra "da taskbar" secondo le regole della shell. */
bool IsTaskbarWindow(HWND hwnd);

/* ------------------------------------------------------------------ */
/*  Impostazioni della tray lette dalla STESSA chiave di Explorer      */
/*                                                                     */
/*  "Nascondi icone e notifiche inattive" di Windows sara'  il          */
/*  valore DWORD EnableAutoTray sotto                                     */
/*  HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer (1 =        */
/*  raggruppamento attivo, predefinito; assente = 1). E' la stessa      */
/*  chiave che legge la shell, non un'euristica: le icone nuove         */
/*  partono nell'overflow quando e' attiva, sulla barra quando e'        */
/*  disattiva, esattamente come nel sistema reale.                      */
/* ------------------------------------------------------------------ */
bool IsAutoTrayEnabled();

/* Hash FNV-1a dei pixel di una bitmap ARGB: serve a decidere se un'icona
 * e' CAMBIATA DAVVERO (niente refresh inutili, niente flickering). */
uint64_t ArgbHash(const ArgbBitmap& bmp);

/* v2.38: decodificatori condivisi per PNG incorporati in base64 (WIC).
 * Usati dalla ricerca applicazioni, dal flyout batteria e dalla jump
 * list: decodificati UNA volta, mai per ridisegno. Un base64 corrotto o
 * una decodifica fallita ritornano false: mai crashare. */
bool Base64Decode(const char* s, std::vector<BYTE>& out);
bool DecodeEmbeddedPng(const char* b64, std::vector<uint32_t>& out,
                       int& outW, int& outH,
                       bool cropAlpha, int targetH);

/* v2.38: HBITMAP 32bpp PREMULTIPLICATO da pixel ARGB ad alpha dritto, e
 * disegno con AlphaBlend scalato. Condivisi da flyout batteria e jump
 * list (le icone arrivano come pixel, non come HICON di shell). */
HBITMAP MakeHBitmapFromArgb(const std::vector<uint32_t>& px, int w, int h);
void DrawBitmapScaled(HDC hdc, HBITMAP hb, int dw, int dh, int dx, int dy);

/* v2.38: vero se la finestra proprietaria di un'icona tray appartiene al
 * modulo indicato (pnidui.dll=rete, SndVolSSO.dll=volume, stobject.dll=
 * batteria). Cross-process con psapi; false su qualsiasi errore. */
bool OwnerModuleIs(HWND ownerHwnd, const wchar_t* moduleName);

} /* namespace w7t */

#endif /* W7T_COMMON_H */
