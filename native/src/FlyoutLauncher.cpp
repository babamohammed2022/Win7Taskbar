/*
 * Win7Taskbar - Core nativo - Apertura dei riquadri (flyout) di sistema
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
 *
 * Provenienza della logica:
 *   - via moderna (ImmersiveShell): ImmersiveFlyouts, adattato da
 *     ExplorerPatcher/ImmersiveFlyouts.{h,c} di valinet (GPL-2.0-or-later);
 *   - via classica (Aero Clock): equivalente a
 *     RetroBar/Utilities/ClockFlyoutLauncher.cs (dremin/RetroBar, Apache-2.0)
 *     e a ManagedShell.UWPInterop/ImmersiveShellHelper.cs
 *     (cairoshell/ManagedShell, Apache-2.0).
 * In entrambi i casi il codice C++ e' stato scritto da zero; nessuna riga di
 * C o di C# e' stata copiata. Vedi CREDITS.txt e THIRD-PARTY-NOTICES.md.
 */

#include "FlyoutLauncher.h"
#include "SehGuard.h"
#include <commctrl.h>

#include <objbase.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <algorithm>

namespace w7t {

void GrowClockFlyoutHeight(HWND flyout);  /* definita piu' sotto */

/* NOTA IMPORTANTE - non spostare queste dichiarazioni dentro il namespace
 * anonimo che segue.
 *
 * Sono interfacce COM: le implementazioni vivono dentro Windows, non in
 * questo file. Se vengono dichiarate in un namespace anonimo, il
 * compilatore vede metodi virtuali puri di un tipo che nessuna classe
 * visibile deriva, ne deduce che una simile chiamata non puo' mai
 * avvenire e sostituisce ogni invocazione con __cxa_pure_virtual (il
 * gestore di errore che termina il processo), eliminando insieme a essa
 * anche gli IID diventati "inutilizzati". Il codice compila senza un solo
 * avviso e il percorso nativo smette silenziosamente di funzionare.
 * Verificato con GCC 14 a -O2. Lo stesso vale per le interfacce dichiarate
 * in ImmersiveFlyouts.h. */

/* Aero Clock: il calendario classico di Vista/7 (primo parametro intero)
 * e di Windows 8+ (primo parametro HWND). Stesso CLSID, due IID. */
struct IAeroClock : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ShowFlyout(HWND hwnd, RECT* rect) = 0;
};

struct IAeroClockLegacy : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ShowFlyout(int unused, RECT* rect) = 0;
};

namespace {

/* ================================================================== */
/*  Via classica: Aero Clock (Vista, 7, 8)                            */
/* ================================================================== */

const CLSID kClsidAeroClock = {
    0xA323554A, 0x0FE1, 0x4E49,
    { 0xAE, 0xE1, 0x67, 0x22, 0x46, 0x5D, 0x79, 0x9F }
};

const IID kIidAeroClock = {
    0x7A5FCA8A, 0x76B1, 0x44C8,
    { 0xA9, 0x7C, 0xE7, 0x17, 0x3C, 0xCA, 0x5F, 0x4F }
};

const IID kIidAeroClockLegacy = {
    0x4376DF10, 0xA662, 0x420B,
    { 0xB3, 0x0D, 0x95, 0x88, 0x81, 0x46, 0x1E, 0xF9 }
};

IAeroClock*       g_aeroClock       = nullptr;
IAeroClockLegacy* g_aeroClockLegacy = nullptr;

/* Riposiziona il riquadro appena aperto.
 *
 * Serve perche' la shell lo colloca rispetto alla PROPRIA taskbar, che noi
 * abbiamo nascosto. Aggancio ai bordi con soglia 15 px e margine 7 px, come
 * in Windows 7. */
/* Riposiziona il riquadro appena aperto, come fa RetroBar ma con aggancio
 * deterministico invece che a soglia.
 *
 * Due tempi, perche' la shell crea e anima il riquadro in modo asincrono:
 * prima si aspetta che esista E abbia smesso di cambiare dimensione (due
 * letture identiche consecutive), poi lo si colloca dove lo metterebbe
 * Windows 7: bordo destro allineato al bordo destro della barra, bordo
 * inferiore appoggiato sopra la barra. La soglia a 15 px che c'era prima
 * agganciava il riquadro ai bordi del monitor quando capitava li' vicino:
 * erano le "posizioni casuali". */
void WaitForFlyoutStable(const wchar_t* className, HWND& outFlyout) {
    RECT last = {};
    RECT current = {};
    outFlyout = nullptr;

    for (int attempt = 0; attempt < 20; ++attempt) {
        HWND flyout = FindWindowW(className, nullptr);
        if (flyout != nullptr && IsWindowVisible(flyout)
            && GetWindowRect(flyout, &current)) {
            if (outFlyout == flyout
                && current.left == last.left && current.top == last.top
                && current.right == last.right && current.bottom == last.bottom) {
                outFlyout = flyout;
                return;  /* dimensione stabile per due letture consecutive */
            }
            last = current;
            outFlyout = flyout;
        }
        Sleep(15);
    }
}





/* v2.63: la stessa collocazione, ma su un riquadro GIA' trovato: serve al
 * percorso dell'orologio, che deve sapere se la finestra di Windows 7
 * (classe ClockFlyoutWindow) e' davvero comparsa prima di dichiarare
 * riuscita l'apertura. Prima bastava che ShowFlyout rispondesse S_OK: su
 * Windows 11 la shell risponde S_OK e apre la SUA isola XAML, e il
 * frontend - convinto che il riquadro di Windows 7 fosse aperto - non
 * apriva nulla. */
void PositionFoundFlyout(HWND flyout, HWND taskbarHwnd, const RECT& barRect) {
    if (flyout == nullptr || !IsWindow(flyout)) {
        return;
    }
    ApplyAeroFlyoutStyle(flyout);
    GrowClockFlyoutHeight(flyout);

    RECT rect = {};
    if (!GetWindowRect(flyout, &rect)) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(taskbarHwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return;
    }

    const RECT wa = mi.rcWork;
    const LONG width  = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;

    /* Come Windows 7: calendario allineato a destra sopra l'orologio. */
    LONG x = barRect.right - width;
    LONG y = barRect.top - height;

    x = (std::max)(wa.left, (std::min)(x, wa.right - width));
    y = (std::max)(wa.top, (std::min)(y, wa.bottom - height));

    SetWindowPos(flyout, nullptr, static_cast<int>(x), static_cast<int>(y),
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

void FixFlyoutPosition(const wchar_t* className, HWND taskbarHwnd,
                       const RECT& barRect) {
    HWND flyout = nullptr;
    WaitForFlyoutStable(className, flyout);
    if (flyout == nullptr) {
        return;
    }

    /* Bordo Aero dal compositor, senza cornice ridimensionabile. */
    ApplyAeroFlyoutStyle(flyout);

    /* Altezza come il flyout di Windows 7: +7%, verso l'alto. */
    GrowClockFlyoutHeight(flyout);

    RECT rect = {};
    if (!GetWindowRect(flyout, &rect)) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(taskbarHwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return;
    }

    const RECT wa = mi.rcWork;
    const LONG width  = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;

    /* Come Windows 7: calendario allineato a destra sopra l'orologio. */
    LONG x = barRect.right - width;
    LONG y = barRect.top - height;

    x = (std::max)(wa.left, (std::min)(x, wa.right - width));
    y = (std::max)(wa.top, (std::min)(y, wa.bottom - height));

    SetWindowPos(flyout, nullptr, static_cast<int>(x), static_cast<int>(y),
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

bool ShowAeroClock(HWND taskbarHwnd, RECT& barRect) {
    /* v2.63: S_OK non basta piu'.
     *
     * Su Windows 11 la shell risponde S_OK anche quando ad aprire e' la SUA
     * isola XAML (classe XamlExplorerHostIslandWindow, non ClockFlyoutWindow):
     * dichiarare riuscita l'apertura in quel caso lasciava l'utente senza il
     * riquadro che aveva chiesto, perche' il frontend non apriva il nostro
     * (credeva di aver gia' aperto quello di Windows 7). Quindi: si chiede il
     * riquadro, si ASPETTA che compaia la finestra classica dell'orologio e
     * solo allora si risponde di si'. Se non compare, e' un no e il frontend
     * apre il calendario ricreato. */
    HWND flyout = nullptr;
    bool requested = false;

    W7T_SEH_TRY
    if (IsWindows8OrBetter()) {
        if (g_aeroClock == nullptr) {
            CoCreateInstance(kClsidAeroClock, nullptr, CLSCTX_INPROC_SERVER,
                             kIidAeroClock, reinterpret_cast<void**>(&g_aeroClock));
        }
        if (g_aeroClock != nullptr) {
            requested = SUCCEEDED(g_aeroClock->ShowFlyout(taskbarHwnd, &barRect));
        }
    } else {
        if (g_aeroClockLegacy == nullptr) {
            CoCreateInstance(kClsidAeroClock, nullptr, CLSCTX_INPROC_SERVER,
                             kIidAeroClockLegacy,
                             reinterpret_cast<void**>(&g_aeroClockLegacy));
        }
        if (g_aeroClockLegacy != nullptr) {
            requested = SUCCEEDED(g_aeroClockLegacy->ShowFlyout(0, &barRect));
        }
    }
    W7T_SEH_CATCH
    AppendCoreLog(L"orologio: eccezione nel riquadro Aero classico");
    return false;
    W7T_SEH_END

    WaitForFlyoutStable(L"ClockFlyoutWindow", flyout);
    if (flyout == nullptr) {
        /* Nessuna finestra dell'orologio classico.
         *
         * Su Windows 8/10 la shell puo' impiegare piu' del tempo che
         * aspettiamo e la chiamata era comunque riuscita: li' si mantiene il
         * comportamento di sempre (riuscita = la shell ha accettato), cosi'
         * nessuna build precedente cambia comportamento.
         *
         * Su Windows 11 no: la shell risponde S_OK e apre la SUA isola XAML,
         * che non e' il riquadro di Windows 7 chiesto dall'utente. Li' si
         * dice "non riuscito" e il frontend apre il calendario ricreato. */
        if (requested && !IsWindows11OrBetter()) {
            LogTagged(L"GATE", L"orologio: la shell ha accettato (Windows 10, riquadro non ancora visibile)");
            return true;
        }
        LogTagged(L"GATE", L"orologio: il riquadro di Windows 7 (ClockFlyoutWindow) non e' comparso");
        return false;
    }

    PositionFoundFlyout(flyout, taskbarHwnd, barRect);
    LogTagged(L"GATE", L"orologio: aperto il riquadro di Windows 7");
    return true;
}

/* Prepara COM sul thread chiamante. RPC_E_CHANGED_MODE non e' un errore:
 * significa solo che qualcun altro l'ha gia' inizializzato. */
class ComScope {
public:
    ComScope() {
        m_owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    }
    ~ComScope() {
        if (m_owned) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    bool m_owned = false;
};

/* Traduce un HRESULT in uno dei codici interi dell'ABI pubblica. */
int32_t ToCoreResult(HRESULT hr) {
    return SUCCEEDED(hr) ? W7T_OK : W7T_ERR_NOT_FOUND;
}

/* Rettangolo della nostra barra, che fa da ancora per i riquadri. */
bool TryGetBarRect(HWND taskbarHwnd, RECT& out) {
    if (taskbarHwnd == nullptr || !IsWindow(taskbarHwnd)) {
        return false;
    }
    return GetWindowRect(taskbarHwnd, &out) != FALSE;
}

} /* namespace */

/* ====================================================================== */
/*  v2.63 - Preferenze dei riquadri e porta dei riquadri moderni          */
/* ====================================================================== */

namespace {

/* Un solo posto dove vive la scelta dell'utente. Aggiornata dal frontend con
 * W7T_SetFlyoutPreferences; letta da tutti i percorsi di apertura. */
FlyoutPreferences g_flyoutPrefs;
bool g_flyoutPrefsPublished = false;

/* Diagnostica: l'ultima riga pubblicata, per non ripetere lo stesso log a
 * ogni clic (il gate viene interrogato molto spesso). */
wchar_t g_lastGateLine[256] = {};

const wchar_t* StyleName(FlyoutStyle style) {
    return style == FlyoutStyle::Modern ? L"Windows10/11" : L"Windows7";
}

const wchar_t* KindName(FlyoutKind kind) {
    switch (kind) {
        case FlyoutKind::Network: return L"network";
        case FlyoutKind::Clock:   return L"clock";
        case FlyoutKind::Battery: return L"battery";
        case FlyoutKind::Sound:   return L"sound";
        default:                  return L"?";
    }
}

} /* namespace */

void SetFlyoutPreferences(const FlyoutPreferences& prefs) {
    g_flyoutPrefs = prefs;
    g_flyoutPrefsPublished = true;
    g_lastGateLine[0] = L'\0';   /* nuova riga di log alla prossima domanda */

    LogTagged(L"SETTINGS",
              L"preferenze riquadri dal frontend: orologio=%s rete=%s volume=%s batteria=%s",
              StyleName(prefs.clock), StyleName(prefs.network),
              StyleName(prefs.volume), StyleName(prefs.battery));
    LogFlyoutGate(L"SetFlyoutPreferences");
}

FlyoutPreferences GetFlyoutPreferences() {
    return g_flyoutPrefs;
}

FlyoutStyle PreferredStyle(FlyoutKind kind) {
    switch (kind) {
        case FlyoutKind::Clock:   return g_flyoutPrefs.clock;
        case FlyoutKind::Network: return g_flyoutPrefs.network;
        case FlyoutKind::Battery: return g_flyoutPrefs.battery;
        case FlyoutKind::Sound:   return g_flyoutPrefs.volume;
        default:                  return FlyoutStyle::Win7;
    }
}

bool IsImmersiveFlyoutHostUsable() {
    /* Condizione tecnica: Windows 10 o successivo E la fabbrica
     * ShellExperience raggiungibile (combase + CLSID). Su un Windows 11
     * "spogliato" la seconda risponde no e il chiamante usa il percorso
     * classico senza pagare l'attesa. */
    return IsWindows10OrBetter() && ImmersiveFlyouts::IsSupported();
}

bool IsModernFlyoutHostAvailable() {
    /* I riquadri immersivi di Windows 11 sono isole XAML: esistono, ma
     * compaiono quando decide la shell e non si ancorano come quelli di
     * Windows 10. Questa e' la differenza che il resto del codice usa per
     * scegliere fra riquadro della shell e riquadro ricreato. */
    return IsImmersiveFlyoutHostUsable() && GetWindowsBuildNumber() >= 22000u;
}

FlyoutRoute ChooseFlyoutRoute(FlyoutKind kind) {
    const FlyoutStyle style = PreferredStyle(kind);
    if (style == FlyoutStyle::Modern && IsImmersiveFlyoutHostUsable()) {
        return FlyoutRoute::Immersive;
    }
    return FlyoutRoute::Classic;
}

void LogFlyoutGate(const wchar_t* where) {
    wchar_t line[256] = {};
    wsprintfW(line,
              L"[GATE] %s: build=%u modernHost=%s orologio=%s rete=%s volume=%s batteria=%s prefs=%s",
              where != nullptr ? where : L"?",
              GetWindowsBuildNumber(),
              IsModernFlyoutHostAvailable() ? L"si" : L"no",
              StyleName(g_flyoutPrefs.clock), StyleName(g_flyoutPrefs.network),
              StyleName(g_flyoutPrefs.volume), StyleName(g_flyoutPrefs.battery),
              g_flyoutPrefsPublished ? L"frontend" : L"default");

    if (wcscmp(line, g_lastGateLine) == 0) {
        return;   /* identica all'ultima: niente righe ripetute nel log */
    }
    lstrcpynW(g_lastGateLine, line, ARRAYSIZE(g_lastGateLine));
    AppendCoreLog(line);
}

/* v2.29: i flyout classici ricevono WS_THICKFRAME per il bordo Aero dal
 * compositor, ma NON devono essere ridimensionabili: come il pannello
 * overflow, i bordi di resize vengono rimappati su HTBORDER (il cursore
 * resta una freccia, il frame Aero resta identico). */
LRESULT CALLBACK FlyoutNoResizeProc(HWND hWnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam, UINT_PTR uIdSubclass,
                                    DWORD_PTR dwRefData) {
    (void)uIdSubclass;
    (void)dwRefData;

    /* v2.32: il pannello overflow ha il proprio gestore (bordi Aero +
     * HTBORDER + resize di layout legittimo quando cambiano le icone):
     * il clamp qui sotto lo congelava a una larghezza sbagliata. Lo si
     * esclude: vale solo per i flyout di sistema. */
    if (msg == WM_WINDOWPOSCHANGING || msg == WM_NCHITTEST) {
        wchar_t cls[48]{};
        GetClassNameW(hWnd, cls, 47);
        if (wcscmp(cls, L"Win7Taskbar_TrayOverflow") == 0) {
            return DefSubclassProc(hWnd, msg, wParam, lParam);
        }
    }
    if (msg == WM_NCHITTEST) {
        LRESULT r = DefSubclassProc(hWnd, msg, wParam, lParam);
        switch (r) {
            case HTTOP: case HTTOPLEFT: case HTTOPRIGHT:
            case HTBOTTOM: case HTBOTTOMLEFT: case HTBOTTOMRIGHT:
            case HTLEFT: case HTRIGHT:
                return HTBORDER;
            default:
                return r;
        }
    }
    /* v2.31: clamp definitivo: qualunque percorso (hit-test custom del
     * flyout, SC_SIZE, codice interno dell'oggetto AeroClock) provi a
     * cambiare la dimensione viene neutralizzato. Un solo cambio di
     * dimensione e' consentito ed e' quello iniziale del nostro grow
     * (+7% stile Win7), marcato con la proprieta' W7T_AllowOneResize. */
    if (msg == WM_WINDOWPOSCHANGING) {
        WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
        if ((wp->flags & SWP_NOSIZE) == 0) {
            if (GetPropW(hWnd, L"W7T_AllowOneResize") != nullptr) {
                RemovePropW(hWnd, L"W7T_AllowOneResize");
            } else {
                wp->flags |= SWP_NOSIZE;
            }
        }
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// Applica bordi Aero (thick frame) a un flyout classico gia' individuato via
// FindWindowW, senza injection: solo bit di stile + refresh del frame.
void ApplyAeroFlyoutStyle(HWND hFlyout)
{
    if (hFlyout == nullptr || !IsWindow(hFlyout)) {
        return;
    }

    /* v2.30: i flyout immersivi moderni (orologio/calendario XAML di
     * Win10/11) hanno gia' il proprio chrome: aggiungendo WS_THICKFRAME
     * diventavano RIDIMENSIONABILI a mano (era il bug segnalato). Su di
     * loro non si tocca nulla: il resize e' inoltre negato dal watcher
     * della tray che ne riafferma la dimensione fissa. */
    wchar_t cls[80]{};
    GetClassNameW(hFlyout, cls, 79);
    if (wcsstr(cls, L"XamlExplorerHostIslandWindow") != nullptr ||
        wcsstr(cls, L"Windows.UI.Core.CoreWindow") != nullptr) {
        return;
    }

    LONG_PTR style = GetWindowLongPtrW(hFlyout, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hFlyout, GWL_EXSTYLE);

    SetWindowLongPtrW(hFlyout, GWL_STYLE, style | WS_THICKFRAME);
    SetWindowLongPtrW(hFlyout, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);

    // Costringe Windows a ricalcolare il frame non-client (bordo/ombra Aero)
    // senza spostare o ridimensionare la finestra.
    SetWindowPos(hFlyout, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                 SWP_NOZORDER | SWP_NOACTIVATE);

    /* v2.29: non ridimensionabile, bordo Aero conservato. */
    SetWindowSubclass(hFlyout, FlyoutNoResizeProc, 0, 0);
}

void GrowClockFlyoutHeight(HWND flyout) {
    RECT r = {};
    if (flyout == nullptr || !GetWindowRect(flyout, &r)) {
        return;
    }
    const LONG w = r.right - r.left;
    const LONG h = r.bottom - r.top;
    if (w <= 0 || h <= 0) {
        return;
    }
    const LONG nh = h + MulDiv(h, 7, 100);
    /* v2.31: il grow iniziale e' l'unico resize consentito dal clamp. */
    SetPropW(flyout, L"W7T_AllowOneResize", reinterpret_cast<HANDLE>(1));
    SetWindowPos(flyout, nullptr, static_cast<int>(r.left),
                 static_cast<int>(r.bottom - nh), static_cast<int>(w),
                 static_cast<int>(nh), SWP_NOZORDER | SWP_NOACTIVATE);
}


/* Il flyout dell'orologio di Windows 7 era un 7% piu' alto di quanto queste
 * build lo mostrino: lo si estende verso l'alto tenendo fermo il bordo
 * inferiore (quello che poggia sulla barra). */


/* ====================================================================== */
/*  API pubblica del modulo                                               */
/* ====================================================================== */

int32_t FlyoutLauncher::InvokeFlyoutAt(FlyoutKind kind, FlyoutAction action,
                                       const RECT& anchorRect) {
    /* v2.63: nessuna eccezione e nessun fault attraversa questa porta.
     *
     * Il percorso immersivo parla con la shell (COM + WinRT): un guasto li'
     * dentro non deve far cadere la barra. Si registra e si risponde
     * "non disponibile", cosi' il chiamante ripiega sul riquadro ricreato. */
    int32_t result = W7T_ERR_NOT_FOUND;

    /* ComScope vive FUORI dal blocco protetto: nessun oggetto con
     * distruttore attraversa il salto di setjmp. */
    ComScope com;

    W7T_SEH_TRY
    result = ToCoreResult(ImmersiveFlyouts::Invoke(kind, action,
                                                   MakeWinRtRect(anchorRect)));
    W7T_SEH_CATCH
    LogTagged(L"GATE", L"riquadro %s: fault nel percorso immersivo", KindName(kind));
    result = W7T_ERR_NOT_FOUND;
    W7T_SEH_END

    return result;
}

int32_t FlyoutLauncher::ShowVolumeFlyoutAt(const RECT& anchorRect) {
    return InvokeFlyoutAt(FlyoutKind::Sound, FlyoutAction::Show, anchorRect);
}

int32_t FlyoutLauncher::InvokeFlyout(FlyoutKind kind, FlyoutAction action,
                                     HWND taskbarHwnd) {
    RECT barRect = {};

    /* Per nascondere non serve alcuna ancora: il riquadro sa dov'e'. */
    if (action == FlyoutAction::Show && !TryGetBarRect(taskbarHwnd, barRect)) {
        return W7T_ERR_INVALID_ARG;
    }

    return InvokeFlyoutAt(kind, action, barRect);
}

int32_t FlyoutLauncher::ShowClockFlyout(HWND taskbarHwnd) {
    /* ------------------------------------------------------------------ */
    /*  v2.63 - LA PREFERENZA DECIDE IL PERCORSO, NON LA BUILD.            */
    /*                                                                    */
    /*  Prima qui c'era un rifiuto incondizionato su Windows 11: la scelta */
    /*  "Windows 7" delle Proprieta' restava scritta ma non veniva mai     */
    /*  eseguita, e l'utente vedeva sempre il calendario ricreato (era il  */
    /*  difetto segnalato: "il riquadro dell'orologio apre quello ricreato */
    /*  invece di quello di Windows 7").                                  */
    /*                                                                    */
    /*  Ora:                                                            */
    /*   - preferenza "Windows 7"  -> calendario classico Aero (finestra   */
    /*     ClockFlyoutWindow), che esiste anche su Windows 11 e si         */
    /*     riposiziona come in Windows 7; se non compare, no e il frontend */
    /*     apre il calendario ricreato;                                    */
    /*   - preferenza "Windows 10/11" -> riquadro della shell (isola XAML  */
    /*     su Windows 11, riquadro immersivo su Windows 10), con la        */
    /*     chiusura del riquadro di sistema prima di aprire il nostro.     */
    /*                                                                    */
    /*  La sonda "il riquadro immersivo esiste su questa build" non e' piu' */
    /*  un flag permanente: un fallimento transitorio (shell non ancora    */
    /*  pronta dopo l'accesso) non deve valere per tutta la sessione.      */
    /* ------------------------------------------------------------------ */
    LogFlyoutGate(L"ShowClockFlyout");

    RECT barRect = {};
    if (!TryGetBarRect(taskbarHwnd, barRect)) {
        return W7T_ERR_INVALID_ARG;
    }

    if (ChooseFlyoutRoute(FlyoutKind::Clock) == FlyoutRoute::Immersive) {
        ComScope com;

        /* Sonda con memoria a tempo: se la shell non ha risposto, si riprova
         * dopo mezzo minuto invece di rinunciare per sempre. */
        static bool  probeFailed = false;
        static DWORD probeFailedAt = 0;
        const DWORD now = GetTickCount();
        if (probeFailed && (now - probeFailedAt) > 30000u) {
            probeFailed = false;
        }

        if (!probeFailed) {
            const int32_t outcome = ToCoreResult(ImmersiveFlyouts::Invoke(
                FlyoutKind::Clock, FlyoutAction::Show, MakeWinRtRect(barRect)));
            if (outcome == W7T_OK) {
                return W7T_OK;
            }
            probeFailed = true;
            probeFailedAt = now;
            LogTagged(L"GATE", L"orologio: il riquadro immersivo non ha risposto su questa build");
        }

        /* La shell puo' materializzare il SUO calendario anche quando la
         * sonda non l'ha visto in tempo: si chiude prima di aprire il nostro. */
        ImmersiveFlyouts::Invoke(FlyoutKind::Clock, FlyoutAction::Hide,
                                 MakeWinRtRect(barRect));
        return W7T_ERR_NOT_FOUND;
    }

    /* Preferenza "Windows 7": il calendario classico, senza passare dai
     * riquadri immersivi della shell. */
    ComScope com;
    if (ShowAeroClock(taskbarHwnd, barRect)) {
        return W7T_OK;
    }

    /* Il calendario classico non e' comparso.
     *
     * NON si ripiega sul riquadro della shell: chi ha scelto "Windows 7" ha
     * chiesto quel riquadro, e su Windows 11 la shell risponde S_OK aprendo la
     * sua isola XAML - sarebbe di nuovo il difetto segnalato ("apre il nativo
     * invece di quello di Windows 7"), per giunta con il rischio dei due
     * riquadri sovrapposti. Si risponde "non riuscito" e il frontend apre il
     * calendario ricreato, che e' il ripiego dichiarato. */
    LogTagged(L"GATE", L"orologio: nessun riquadro di Windows 7, si apre il ricreato");
    return W7T_ERR_NOT_FOUND;
}

int32_t FlyoutLauncher::HideClockFlyout() {
    /* Solo la chiusura: nessuna sonda, nessuna attesa. Se il riquadro non
     * c'e' la chiamata non ha effetto e torna subito. */
    const RECT none{};
    ComScope com;
    return ToCoreResult(ImmersiveFlyouts::Invoke(FlyoutKind::Clock,
                                                 FlyoutAction::Hide,
                                                 MakeWinRtRect(none)));
}

int32_t FlyoutLauncher::ShowVolumeFlyout(HWND taskbarHwnd) {
    return InvokeFlyout(FlyoutKind::Sound, FlyoutAction::Show, taskbarHwnd);
}

int32_t FlyoutLauncher::ShowVolumeMixer() {
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.lpVerb = L"open";
    info.lpFile = L"SndVol.exe";
    info.nShow  = SW_SHOWNORMAL;
    info.fMask  = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;

    return ShellExecuteExW(&info) ? W7T_OK : W7T_ERR_NOT_FOUND;
}

void FlyoutLauncher::Shutdown() {
    ImmersiveFlyouts::Shutdown();

    if (g_aeroClock != nullptr) {
        g_aeroClock->Release();
        g_aeroClock = nullptr;
    }
    if (g_aeroClockLegacy != nullptr) {
        g_aeroClockLegacy->Release();
        g_aeroClockLegacy = nullptr;
    }
}

} /* namespace w7t */
