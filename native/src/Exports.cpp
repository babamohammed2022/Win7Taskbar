/*
 * Win7Taskbar - Core nativo - implementazione dell'ABI extern "C"
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

#define W7T_BUILDING_DLL 1

#include "Common.h"
#include "Win8NetworkFlyout.h"
#include <shellapi.h>
#include "SehGuard.h"
#include "WindowManager.h"
#include "TrayService.h"
#include "PinnedApps.h"
#include "AppBarService.h"
#include "ShellMenu.h"
#include "TrayOverflowWindow.h"
#include "Win11TrayReader.h"   /* v2.60: flyout vero della tray di Windows 11 */
#include "AppSearchWindow.h"
#include "PropertiesDialog.h"
#include "FlyoutLauncher.h"
#include "ExtraSettings.h"     /* v1.21.7: extra settings of the taskbar */
#include "AudioService.h"
#include "JumpListWindow.h"
#include "PreviewPolicy.h"
#include "PinVerbs.h"
#include <shlobj.h>     /* v2.38 */
#include "LanguageSwitcher.h"   /* v1.4: selettore della lingua */
#include "AeroThumbnailFrame.h" /* v3.10: cornice 9-slice delle anteprime */
#include "BatteryFlyout.h"      /* v2.38 */
#include "RaiiWrappers.h"
#include <thread>
#include <atomic>
#include <psapi.h>

using namespace w7t;

namespace {

inline HWND ToHwnd(uint64_t value) {
    return reinterpret_cast<HWND>(static_cast<uintptr_t>(value));
}

} /* namespace */

extern "C" W7T_API int32_t W7T_CALL W7T_ShowContextMenu(int32_t x, int32_t y,
                                                        int32_t bottomEdge,
                                                        const wchar_t* items) {
    return ShellMenu::ShowContextMenu(x, y, bottomEdge != 0, items);
}


extern "C" W7T_API int32_t W7T_CALL W7T_ShowContextMenuEx(int32_t x, int32_t y,
                                                          int32_t bottomEdge,
                                                          const wchar_t* items,
                                                          int32_t anchorAtCursor) {
    return ShellMenu::ShowContextMenuEx(x, y, bottomEdge != 0, items,
                                        anchorAtCursor != 0);
}

extern "C" W7T_API int32_t W7T_CALL W7T_TrayImportExplorerIcons(void) {
    return TrayService::Instance().ImportExplorerIcons();
}

/* ------------------------------------------------------------------ */
/*  Ciclo di vita                                                      */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_Initialize(W7T_EventCallback cb) {
    CoreState& state = CoreState::Instance();
    if (state.IsInitialized()) {
        return W7T_ERR_ALREADY_INIT;
    }

    state.SetCallback(cb);
    WindowManager::Instance().Start();
    w7t::PinnedApps::Instance().Start();
    state.SetInitialized(true);
    return W7T_OK;
}

extern "C" W7T_API void W7T_CALL W7T_Shutdown(void) {
    w7t::PinnedApps::Instance().Stop();
    CoreState& state = CoreState::Instance();
    if (!state.IsInitialized()) {
        return;
    }

    TrayService::Instance().Stop();
    WindowManager::Instance().Stop();

    /* Ripristina sempre la shell prima di sparire. */
    AppBarService& appBar = AppBarService::Instance();
    appBar.Unregister(nullptr);
    if (appBar.IsNativeTaskbarHidden()) {
        appBar.SetNativeTaskbarHidden(false);
    }

    state.SetCallback(nullptr);
    state.SetInitialized(false);
}

extern "C" W7T_API uint32_t W7T_CALL W7T_GetVersion(void) {
    /* 0xMMmmpppp */
    return 0x00010000u;
}

extern "C" W7T_API int32_t W7T_CALL W7T_PumpEvents(int32_t maxEvents) {
    CoreState& state = CoreState::Instance();
    W7T_EventCallback cb = state.Callback();
    if (cb == nullptr) {
        return 0;
    }

    std::vector<QueuedEvent> events = state.DrainEvents(maxEvents);
    for (const QueuedEvent& e : events) {
        cb(e.evt, e.a, e.b);
    }
    return static_cast<int32_t>(events.size());
}

/* ------------------------------------------------------------------ */
/*  Superbar                                                           */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_RefreshWindows(void) {
    return WindowManager::Instance().Refresh();
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetWindowCount(void) {
    return WindowManager::Instance().GetCount();
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetWindows(W7T_WindowInfo* buffer, int32_t capacity) {
    /* v3.6: anche questo percorso passa dalla cinghia: e' la via con cui
     * il gestito scopre le finestre, e il crash del Centro connessioni
     * arrivava proprio mentre questa lista si faceva. */
    W7T_SEH_TRY {
        return WindowManager::Instance().CopyTo(buffer, capacity);
    } W7T_SEH_CATCH {} W7T_SEH_END
    return 0;
}

/* v2.25: Pinned Application Model - sorgente autoritativa dei pin. */
/* v2.26: icona di un elemento pinnato SENZA freccia di collegamento:
 * stesso resolver della ricerca (Common.cpp). L'HICON e' di proprieta'
 * del chiamante (DestroyIcon). */
extern "C" W7T_API HICON W7T_CALL W7T_GetLinkIcon(const wchar_t* lnk,
                                                  const wchar_t* target,
                                                  int32_t large) {
    W7T_SEH_TRY {
        return ResolveAppIcon(lnk, target, large != 0);
    } W7T_SEH_CATCH {} W7T_SEH_END
    return nullptr;
}

/* v2.28: avvio di un elemento (lnk/exe) via ShellExecute con retry:
 * ogni tanto la shell risponde SE_ERR_* transitori (DDE occupato,
 * antivirus che blocca l'istante dell'apertura) e il programma "non si
 * apre al primo colpo". Tre tentativi a 200 ms coprono il caso senza
 * cambiare altro. */
extern "C" W7T_API int32_t W7T_CALL W7T_ShellOpen(const wchar_t* path) {
    if (path == nullptr || path[0] == 0) return 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        HINSTANCE r = nullptr;
        W7T_SEH_TRY {
            r = ShellExecuteW(nullptr, L"open", path, nullptr, nullptr,
                              SW_SHOWNORMAL);
        } W7T_SEH_CATCH { r = nullptr; } W7T_SEH_END
        if (reinterpret_cast<intptr_t>(r) > 32) {
            return 1;
        }
        wchar_t line[400];
        _snwprintf_s(line, _TRUNCATE,
                     L"shell-open: tentativo %d fallito (%p) su %.300s",
                     attempt + 1, (void*)r, path);
        AppendCoreLog(line);
        if (attempt < 2) Sleep(200);
    }
    return 0;
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetPinnedCount(void) {
    return w7t::PinnedApps::Instance().GetCount();
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetPinnedApps(W7T_PinnedInfo* buffer, int32_t capacity) {
    return w7t::PinnedApps::Instance().CopyTo(buffer, capacity);
}

extern "C" W7T_API void W7T_CALL W7T_PinnedRefresh(void) {
    w7t::PinnedApps::Instance().Refresh();
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetWindowInfo(uint64_t hwnd, W7T_WindowInfo* out) {
    if (out == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    return WindowManager::Instance().GetInfo(ToHwnd(hwnd), out) ? W7T_OK : W7T_ERR_NOT_FOUND;
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetWindowIconBitmap(uint64_t hwnd, int32_t desiredSize,
                                                            int32_t* width, int32_t* height,
                                                            uint8_t* pixels, int32_t pixelsBytes) {
    return WindowManager::Instance().GetIconBitmap(ToHwnd(hwnd), desiredSize,
                                                   width, height, pixels, pixelsBytes);
}

/* Aero preview frame rendered by the core (see Win7TaskbarCore.h). The
 * frontend calls it once per frame size and accent colour and wraps the bytes
 * in a Pbgra32 BitmapSource; every failure - including "the slice PNGs are not
 * next to the executable" - is reported as a negative code and the frontend
 * keeps the frame its XAML template draws. */
extern "C" W7T_API int32_t W7T_CALL W7T_RenderAeroThumbnailFrame(int32_t width, int32_t height,
                                                                 uint32_t accentArgb,
                                                                 uint8_t* pixels,
                                                                 int32_t pixelsBytes) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        pixelsBytes < 0) {
        return W7T_ERR_INVALID_ARG;
    }

    /* Query call: report how big the caller's buffer has to be. 4096*4096*4
     * is the largest value the checks above allow, so it cannot overflow. */
    if (pixels == nullptr) {
        if (pixelsBytes != 0) {
            return W7T_ERR_INVALID_ARG;
        }
        return width * height * 4;
    }

    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        /* The frontend passes the DWM colorization colour as 0x00RRGGBB; the
         * renderer wants a COLORREF, whose alpha it ignores because the slices
         * keep their own (luminance-modulated). 0 means "no tint": the slices
         * exactly as they are on disk. */
        const COLORREF accent = (accentArgb == 0)
            ? 0
            : RGB((accentArgb >> 16) & 0xFFu, (accentArgb >> 8) & 0xFFu,
                  accentArgb & 0xFFu);
        if (w7t::RenderAeroThumbnailFramePbgra(width, height, accent, pixels,
                                               static_cast<size_t>(pixelsBytes))) {
            result = width * height * 4;
        }
    } W7T_SEH_CATCH { result = W7T_ERR_NOT_FOUND; } W7T_SEH_END
    return result;
}

extern "C" W7T_API int32_t W7T_CALL W7T_ExecuteWindowCommand(uint64_t hwnd, int32_t cmd) {
    return WindowManager::Instance().ExecuteCommand(ToHwnd(hwnd), cmd);
}

extern "C" W7T_API int32_t W7T_CALL W7T_MinimizeGroup(const wchar_t* appId) {
    if (appId == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    return WindowManager::Instance().MinimizeGroup(std::wstring(appId));
}

extern "C" W7T_API int32_t W7T_CALL W7T_CloseGroup(const wchar_t* appId) {
    if (appId == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    return WindowManager::Instance().CloseGroup(std::wstring(appId));
}

extern "C" W7T_API int32_t W7T_CALL W7T_IsFullScreenAppActive(void) {
    return WindowManager::Instance().IsFullScreenAppActive() ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  System tray                                                        */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_TrayStart(void) {
    return TrayService::Instance().Start();
}

extern "C" W7T_API void W7T_CALL W7T_TrayStop(void) {
    TrayService::Instance().Stop();
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetTrayIconCount(void) {
    return TrayService::Instance().GetCount();
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetTrayIcons(W7T_TrayIconInfo* buffer, int32_t capacity) {
    return TrayService::Instance().CopyTo(buffer, capacity);
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetTrayIconBitmap(uint64_t ownerHwnd, uint32_t uid,
                                                          int32_t* width, int32_t* height,
                                                          uint8_t* pixels, int32_t pixelsBytes) {
    return TrayService::Instance().GetIconBitmap(ownerHwnd, uid, width, height,
                                                 pixels, pixelsBytes);
}

extern "C" W7T_API int32_t W7T_CALL W7T_SendTrayIconClick(uint64_t ownerHwnd, uint32_t uid,
                                                          int32_t clickType, int32_t x, int32_t y) {
    return TrayService::Instance().SendClick(ownerHwnd, uid, clickType, x, y);
}

/* v2.62: il frontend dichiara pronto il riquadro di rete di Windows 7
 * (modulo inizializzato e modo "Windows 7 (ricreato)" attivo). Serve al
 * core per sapere se il clic su un'icona di rete RICREATA puo' aprire quel
 * riquadro invece di quello moderno. */
extern "C" W7T_API void W7T_CALL W7T_SetWin7NetworkFlyout(int32_t ready) {
    TrayService::Instance().SetWin7NetworkFlyout(ready != 0);
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetTrayIconPinned(uint64_t ownerHwnd, uint32_t uid,
                                                          int32_t pinned) {
    return TrayService::Instance().SetPinned(ownerHwnd, uid, pinned);
}

extern "C" W7T_API int32_t W7T_CALL W7T_TrayMoveIcon(uint64_t sourceHwnd, uint32_t sourceUid,
                                                     uint64_t targetHwnd, uint32_t targetUid,
                                                     int32_t insertAfter) {
    return TrayService::Instance().MoveIcon(sourceHwnd, sourceUid,
                                            targetHwnd, targetUid, insertAfter);
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetLastBalloon(W7T_BalloonInfo* out) {
    if (out == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    return TrayService::Instance().GetLastBalloon(out) ? W7T_OK : W7T_ERR_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/*  AppBar                                                             */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarRegister(uint64_t hwnd, int32_t edge, int32_t sizePx) {
    /* v1.21.51: barriera sul confine extern "C" (stessa ragione delle
     * ricerche app): la Register installa anche la sorveglianza Flip 3D,
     * e niente puo' attraversare il confine nativo/managed come
     * eccezione C++. */
    try {
        return AppBarService::Instance().Register(ToHwnd(hwnd), edge, sizePx);
    } catch (...) {
        return W7T_ERR_APPBAR;
    }
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarSetPos(uint64_t hwnd, int32_t edge, int32_t sizePx,
                                                     int32_t* outLeft, int32_t* outTop,
                                                     int32_t* outRight, int32_t* outBottom) {
    RECT rect = {};
    const int32_t result = AppBarService::Instance().SetPos(ToHwnd(hwnd), edge, sizePx, &rect);
    if (result != W7T_OK) {
        return result;
    }
    if (outLeft)   *outLeft   = rect.left;
    if (outTop)    *outTop    = rect.top;
    if (outRight)  *outRight  = rect.right;
    if (outBottom) *outBottom = rect.bottom;
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarUnregister(uint64_t hwnd) {
    /* v1.21.51: barriera come la Register: anche la Unregister scioglie
     * la guardia Flip 3D e stacca gli hook. */
    try {
        return AppBarService::Instance().Unregister(ToHwnd(hwnd));
    } catch (...) {
        return W7T_ERR_APPBAR;
    }
}

/* v3.4: superfici per il protocollo AppBar completo (flusso di
 * ManagedShell/RetroBar). Il messaggio di callback va gestito nella
 * finestra che l'ha registrato: il frontend lo riconosce nel proprio
 * WndProc e lo gira qui. */
extern "C" W7T_API int32_t W7T_CALL W7T_AppBarCallbackMessage(void) {
    return static_cast<int32_t>(AppBarService::Instance().CallbackMessage());
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarIsRegistered(void) {
    return AppBarService::Instance().IsRegistered() ? 1 : 0;
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarNotify(uint32_t wParam, int32_t lParam) {
    /* v1.21.51: barriera come la Register. La HandleCallback ora muove
     * finestre, input e guardia Flip 3D: mai lasciare che un'eccezione
     * risalga il WndProc gestito. */
    try {
        return AppBarService::Instance().HandleCallback(wParam, lParam) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarWindowPosChanged(uint64_t hwnd) {
    AppBarService::Instance().NotifyWindowPosChanged(ToHwnd(hwnd));
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppBarActivate(uint64_t hwnd) {
    AppBarService::Instance().Activate(ToHwnd(hwnd));
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetNativeTaskbarHidden(int32_t hidden) {
    return AppBarService::Instance().SetNativeTaskbarHidden(hidden != 0);
}

extern "C" W7T_API int32_t W7T_CALL W7T_IsNativeTaskbarHidden(void) {
    return AppBarService::Instance().IsNativeTaskbarHidden() ? 1 : 0;
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetPrimaryWorkArea(int32_t* left, int32_t* top,
                                                           int32_t* right, int32_t* bottom) {
    RECT rect = {};
    const int32_t result = AppBarService::GetPrimaryWorkArea(&rect);
    if (result != W7T_OK) {
        return result;
    }
    if (left)   *left   = rect.left;
    if (top)    *top    = rect.top;
    if (right)  *right  = rect.right;
    if (bottom) *bottom = rect.bottom;
    return W7T_OK;
}

/* ------------------------------------------------------------------ */
/*  Shell                                                              */
/* ------------------------------------------------------------------ */

namespace {

/* Mostra/minimizza tutto come il clic su Aero Peek in Windows 7.
 *
 * Il WM_COMMAND 0x1FE alla tray di Explorer era un id interno di Windows 7:
 * sulle build successive Explorer lo ignora o lo interpreta diversamente,
 * e il clic restava senza effetto. La via documentata e stabile da Windows 7
 * a Windows 11 e' IShellDispatch4::ToggleDesktop dell'oggetto
 * "Shell.Application"; se COM non risponde, Win+D fa esattamente la stessa
 * cosa (e ripristina al secondo colpo, come l'originale). */
bool ToggleDesktopViaShell() {
    CLSID clsid = {};
    if (FAILED(CLSIDFromProgID(L"Shell.Application", &clsid))) {
        return false;
    }

    IDispatch* dispatch = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER,
                                IID_IDispatch, reinterpret_cast<void**>(&dispatch)))
        || dispatch == nullptr) {
        return false;
    }

    DISPID dispid = 0;
    OLECHAR* name = const_cast<OLECHAR*>(L"ToggleDesktop");
    const HRESULT found = dispatch->GetIDsOfNames(IID_NULL, &name, 1,
                                                  LOCALE_USER_DEFAULT, &dispid);
    if (FAILED(found)) {
        dispatch->Release();
        return false;
    }

    DISPPARAMS empty = {};
    const HRESULT hr = dispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT,
                                        DISPATCH_METHOD, &empty, nullptr,
                                        nullptr, nullptr);
    dispatch->Release();
    return SUCCEEDED(hr);
}

void ToggleDesktopViaKeyboard() {
    INPUT inputs[4] = {};
    inputs[0].type       = INPUT_KEYBOARD;
    inputs[0].ki.wVk     = VK_LWIN;
    inputs[1].type       = INPUT_KEYBOARD;
    inputs[1].ki.wVk     = 'D';
    inputs[2].type       = INPUT_KEYBOARD;
    inputs[2].ki.wVk     = 'D';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type       = INPUT_KEYBOARD;
    inputs[3].ki.wVk     = VK_LWIN;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

} /* namespace */

extern "C" W7T_API int32_t W7T_CALL W7T_ToggleShowDesktop(void) {
    if (ToggleDesktopViaShell()) {
        return W7T_OK;
    }
    ToggleDesktopViaKeyboard();
    return W7T_OK;
}

/* v2.32: ripiego per l'apertura di Start quando il tap del tasto
 * Windows non arriva (hook di Windhawk/mod che filtrano i tasti
 * iniettati, UIPI con foreground elevato). Ispirato al meccanismo
 * storico di Open-Shell/StartIsBack: WM_SYSCOMMAND con SC_TASKLIST
 * mandato DIRETTAMENTE alla Shell_TrayWnd di Explorer (stessa
 * integrita', niente iniezione di input).
 *
 * v2.60: via il broadcast HWND_BROADCAST (lo ricevevano anche altre
 * finestre della shell e su Windows 11 poteva aprire il menu una seconda
 * volta: era una delle "intermittenze" segnalate) e via il ciclo di
 * ri-nascondi a 25 ms. Il ri-nascondi della barra nativa e' ora un evento
 * (vedi AppBarService::HideWatcherProc). */
extern "C" W7T_API int32_t W7T_CALL W7T_OpenStartFallback(void) {
    const DWORD ourPid = GetCurrentProcessId();
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"Shell_TrayWnd", nullptr)) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(tray, &pid);
        if (pid != ourPid) {
            break;
        }
    }
    if (tray == nullptr) {
        return W7T_ERR_NOT_FOUND;
    }
    PostMessageW(tray, WM_SYSCOMMAND, SC_TASKLIST, 0);
    AppBarService::Instance().ReassertNativeTaskbarHidden();
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowStartMenu(void) {
    /* Simula una singola pressione del tasto Windows.
     * In questo modo Open-Shell puo' intercettarla normalmente;
     * se Open-Shell non e' configurato, Windows apre il proprio Start.
     *
     * NON usare SC_TASKLIST come secondo percorso: puo' produrre una
     * seconda attivazione dello Start dopo che Open-Shell ha gia'
     * intercettato il tasto Windows. */
    const bool wasHidden = AppBarService::Instance().IsNativeTaskbarHidden();

    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_LWIN;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));

    if (wasHidden) {
        /* v2.60: un solo ri-nascondi, subito. Il caso "Explorer rimostra
         * la barra mentre apre Start" lo prende l'hook di sistema di
         * AppBarService (evento SHOW), quindi non serve piu' il ciclo di
         * 20 ri-tentativi a 25 ms: era quello a far lampeggiare la barra
         * nativa sotto la nostra a ogni pressione di Start.
         *
         * (unione con il ramo main: la richiesta "Start = solo il tasto
         * Windows, nessun secondo percorso SC_TASKLIST" e' rispettata -
         * vedi il commento sopra - mentre il ri-nascondi resta quello
         * event-driven, non il ciclo a 25 ms.) */
        AppBarService::Instance().ReassertNativeTaskbarHidden();
    }

    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_GetVolume(int32_t* level, int32_t* muted) {
    return AudioService::GetVolume(level, muted);
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetVolume(int32_t level) {
    return AudioService::SetVolume(level);
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetVolumeMuted(int32_t muted) {
    return AudioService::SetMuted(muted != 0);
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowClockFlyout(uint64_t taskbarHwnd) {
    return FlyoutLauncher::ShowClockFlyout(ToHwnd(taskbarHwnd));
}

/* v2.62: chiude il riquadro dell'orologio della shell se e' aperto (non lo
 * apre mai). Il frontend lo usa su Windows 11, dove il riquadro mostrato e'
 * sempre quello ricreato: se la shell ha aperto il suo per conto, i due non
 * devono convivere. */
extern "C" W7T_API int32_t W7T_CALL W7T_HideClockFlyout(void) {
    return FlyoutLauncher::HideClockFlyout();
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowVolumeFlyout(uint64_t taskbarHwnd) {
    return FlyoutLauncher::ShowVolumeFlyout(ToHwnd(taskbarHwnd));
}

/* ------------------------------------------------------------------ */
/*  Riquadri immersivi: rete, orologio, batteria, volume              */
/*                                                                    */
/*  Un solo export per tutti e quattro. La sequenza di invocazione e' */
/*  adattata da ExplorerPatcher (ImmersiveFlyouts.c di valinet,       */
/*  GPL-2.0-or-later); i dettagli sono in native/src/ImmersiveFlyouts.h */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_InvokeFlyout(uint64_t taskbarHwnd,
                                                     int32_t kind,
                                                     int32_t action) {
    /* I valori arrivano dal managed layer: vanno validati prima di
     * trasformarli in enum, altrimenti un intero fuori scala diventerebbe
     * un FlyoutKind inesistente e la switch dentro il modulo non
     * restituirebbe nulla di sensato. */
    switch (kind) {
    case W7T_FLYOUT_NETWORK:
    case W7T_FLYOUT_CLOCK:
    case W7T_FLYOUT_BATTERY:
    case W7T_FLYOUT_SOUND:
        break;
    default:
        return W7T_ERR_INVALID_ARG;
    }

    if (action != W7T_FLYOUT_SHOW && action != W7T_FLYOUT_HIDE) {
        return W7T_ERR_INVALID_ARG;
    }

    return FlyoutLauncher::InvokeFlyout(static_cast<FlyoutKind>(kind),
                                        static_cast<FlyoutAction>(action),
                                        ToHwnd(taskbarHwnd));
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowVolumeMixer(void) {
    return FlyoutLauncher::ShowVolumeMixer();
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetIconRect(uint64_t ownerHwnd,
                                                    uint32_t uid,
                                                    int32_t left, int32_t top,
                                                    int32_t right, int32_t bottom) {
    RECT rect = { left, top, right, bottom };
    TrayService::Instance().SetIconRect(ownerHwnd, uid, rect);
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetShellRects(int32_t barLeft, int32_t barTop,
                                                      int32_t barRight, int32_t barBottom,
                                                      int32_t notifyLeft, int32_t notifyTop,
                                                      int32_t notifyRight, int32_t notifyBottom) {
    RECT bar    = { barLeft, barTop, barRight, barBottom };
    RECT notify = { notifyLeft, notifyTop, notifyRight, notifyBottom };
    TrayService::Instance().SetShellRects(bar, notify);
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_SetChevronRect(int32_t left, int32_t top,
                                                       int32_t right, int32_t bottom) {
    RECT rect = { left, top, right, bottom };
    TrayService::Instance().SetChevronRect(rect);
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_ReanchorFlyouts(void) {
    TrayService::Instance().ReanchorFlyouts();
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_ReassertNativeTaskbarHidden(void) {
    AppBarService::Instance().ReassertNativeTaskbarHidden();
    return W7T_OK;
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowTaskManagerMode(int32_t mode) {
    try {
        /* The explicit alternatives only exist on Windows 11 21H2+
         * (build 22000). Windows 10 always follows its normal association. */
        if (!IsWindows11OrBetter() || mode < 0 || mode > 2) mode = 0;

        wchar_t windowsDir[MAX_PATH]{};
        std::wstring executable = L"taskmgr.exe"; // Automatic
        if (mode != 0) {
            const UINT length = GetWindowsDirectoryW(
                windowsDir, static_cast<UINT>(std::size(windowsDir)));
            if (length == 0 || length >= std::size(windowsDir))
                return W7T_ERR_NOT_FOUND;
            executable = windowsDir;
            executable += mode == 1
                ? L"\\System32\\Taskmgr.exe"   // Windows 11 modern
                : L"\\SysWOW64\\Taskmgr.exe"; // Win8/10 legacy 32-bit
            if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES)
                return W7T_ERR_NOT_FOUND;
        }

        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.lpVerb = L"open";
        info.lpFile = executable.c_str();
        info.nShow = SW_SHOW;
        /* Request the process handle solely so its ownership is explicit;
         * GenericHandle closes it on every return and exception path. */
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
        if (!ShellExecuteExW(&info)) return W7T_ERR_NOT_FOUND;
        raii::GenericHandle process(info.hProcess);
        return W7T_OK;
    } catch (...) {
        /* No exception is allowed to cross the C ABI boundary; RAII has
         * already released any process handle acquired above. */
        return W7T_ERR_NOT_FOUND;
    }
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowTaskManager(void) {
    return W7T_ShowTaskManagerMode(0);
}

/* v2.1: link "Personalizza..." del riquadro di overflow.
 *
 * Apre la pagina NATIVA di Windows per la scelta delle icone dell'area di
 * notifica passando alla shell il namespace
 *   shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}
 * (CLSID della voce "Icone dell'area di notifica"). E' lo stesso
 * meccanismo che usa Explorer quando si apre quella pagina dal Pannello di
 * controllo: su Windows 7 compare l'applet classica, su Windows 10/11 il
 * sistema reindirizza alla pagina Impostazioni equivalente. Non viene
 * creata NESSUNA finestra sostitutiva: si lascia fare alla shell.
 *
 * Tre tentativi in ordine, perche' il modo in cui la shell accetta il
 * namespace dipende dalla build:
 *   1) ShellExecuteExW sul namespace shell:::{...} (il percorso diretto);
 *   2) explorer.exe che riceve il namespace come argomento (il percorso
 *      che usa l'utente quando lo digita in Esegui);
 *   3) rundll32 shell32.dll,Options_RunDLL non esiste piu' da Vista: non
 *      si forza altro, si lascia al frontend l'ultimo ripiego.
 * Ogni tentativo registra l'esito in log-core.txt per la diagnostica. */
extern "C" W7T_API int32_t W7T_CALL W7T_OpenNotificationIconsSettings(void) {
    /* v2.2: come primo tentativo il percorso completo del Pannello di
     * controllo, nello stesso formato usato dal mod Windhawk "Aero Tray"
     * di aubymori (riferimento riconosciuto nei crediti del progetto). */
    static const wchar_t* const kControlPanelPath =
        L"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}\\0\\"
        L"::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}";
    static const wchar_t* const kNamespace =
        L"shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}";

    /* v2.54: PRIMO tentativo il namespace shell PURO, quello chiesto
     * esplicitamente dall'utente:
     *     shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9}
     * E' l'identificatore della pagina "Icone dell'area di notifica": la
     * shell lo risolve da sola, senza executor intermedi. */
    {
        SHELLEXECUTEINFOW info = {};
        info.cbSize       = sizeof(info);
        info.lpVerb       = L"open";
        info.lpFile       = kNamespace;
        info.nShow        = SW_SHOW;
        info.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        if (ShellExecuteExW(&info)) {
            AppendCoreLog(L"personalizza: aperto via namespace shell (05D7B0F4)");
            return W7T_OK;
        }
        AppendCoreLog(L"personalizza: namespace shell rifiutato, provo explorer.exe");
    }

    /* Tentativo 2: lo stesso namespace passato a explorer.exe (come quando
     * l'utente lo digita in Esegui). */
    {
        SHELLEXECUTEINFOW info = {};
        info.cbSize       = sizeof(info);
        info.lpVerb       = L"open";
        info.lpFile       = L"explorer.exe";
        info.lpParameters = kNamespace;
        info.nShow        = SW_SHOW;
        info.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        if (ShellExecuteExW(&info)) {
            AppendCoreLog(L"personalizza: aperto via explorer.exe col namespace");
            return W7T_OK;
        }
        AppendCoreLog(L"personalizza: explorer.exe non ha aperto il namespace, provo control.exe");
    }

    /* Tentativo 3: l'applet classica via control.exe col nome canonico
     * (quello che usa il Pannello di controllo stesso; su Win10 apre la
     * pagina classica, su Win11 il sistema reindirizza a Impostazioni). */
    {
        SHELLEXECUTEINFOW info = {};
        info.cbSize       = sizeof(info);
        info.lpVerb       = L"open";
        info.lpFile       = L"control.exe";
        info.lpParameters = L"/name Microsoft.NotificationAreaIcons";
        info.nShow        = SW_SHOW;
        info.fMask        = SEE_MASK_NOASYNC;
        if (ShellExecuteExW(&info)) {
            AppendCoreLog(L"personalizza: aperto via control.exe /name Microsoft.NotificationAreaIcons");
            return W7T_OK;
        }
        AppendCoreLog(L"personalizza: control.exe non partito, provo percorso Pannello");
    }

    /* Tentativo 2: percorso completo del Pannello di controllo. */
    {
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.lpVerb = L"open";
        info.lpFile = kControlPanelPath;
        info.nShow  = SW_SHOW;
        info.fMask  = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        if (ShellExecuteExW(&info)) {
            AppendCoreLog(L"personalizza: pagina Pannello aperta (ShellExecuteEx)");
            return W7T_OK;
        }
        AppendCoreLog(L"personalizza: percorso Pannello rifiutato, provo namespace corto");
    }

    /* Tentativo 3: namespace shell corto. */
    {
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.lpVerb = L"open";
        info.lpFile = kNamespace;
        info.nShow  = SW_SHOW;
        info.fMask  = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        if (ShellExecuteExW(&info)) {
            AppendCoreLog(L"personalizza: namespace shell aperto (ShellExecuteEx)");
            return W7T_OK;
        }
        AppendCoreLog(L"personalizza: ShellExecuteEx sul namespace fallito, provo explorer.exe");
    }

    /* Tentativo 4: explorer.exe col namespace come argomento. */
    {
        SHELLEXECUTEINFOW info = {};
        info.cbSize       = sizeof(info);
        info.lpVerb       = L"open";
        info.lpFile       = L"explorer.exe";
        info.lpParameters = kNamespace;
        info.nShow        = SW_SHOW;
        info.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        if (ShellExecuteExW(&info)) {
            AppendCoreLog(L"personalizza: aperto via explorer.exe");
            return W7T_OK;
        }
        AppendCoreLog(L"personalizza: anche explorer.exe ha rifiutato il namespace");
    }

    return W7T_ERR_NOT_FOUND;
}

/* v2.2: riga di log dal lato gestito (diagnostica dei percorsi
 * interattivi - drag&drop, overflow, Personalizza - su Windows vero). */
extern "C" W7T_API void W7T_CALL W7T_Log(const wchar_t* line) {
    if (line != nullptr) {
        AppendCoreLog(line);
    }
}

/* ------------------------------------------------------------------ */
/*  Menu contestuali Win32 nativi                                      */
/* ------------------------------------------------------------------ */

extern "C" W7T_API int32_t W7T_CALL W7T_ShowWindowSystemMenu(uint64_t hwnd, int32_t x,
                                                             int32_t y, int32_t bottomEdge) {
    return ShellMenu::ShowWindowSystemMenu(ToHwnd(hwnd), x, y, bottomEdge != 0);
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowGroupMenu(uint64_t hwnd, int32_t x, int32_t y,
                                                      int32_t bottomEdge,
                                                      const wchar_t* minimizeText,
                                                      const wchar_t* closeText) {
    return ShellMenu::ShowGroupMenu(ToHwnd(hwnd), x, y, bottomEdge != 0,
                                    minimizeText, closeText);
}

extern "C" W7T_API int32_t W7T_CALL W7T_ShowPinMenu(int32_t x, int32_t y,
                                                   int32_t bottomEdge,
                                                   const wchar_t* launchText,
                                                   const wchar_t* pinText,
                                                   const wchar_t* lnkPath,
                                                   const wchar_t* targetPath) {
    return ShellMenu::ShowPinMenu(x, y, bottomEdge != 0, launchText, pinText,
                                  lnkPath, targetPath);
}

/* v2.7: pannello overflow nativo con vetro Aero (fallback: Popup WPF). */
static TrayOverflowWindow g_overflowWindow;

extern "C" W7T_API int32_t W7T_CALL W7T_OverflowInit(uint64_t ownerTaskbar) {
    return g_overflowWindow.Create(GetModuleHandleW(nullptr),
                                   reinterpret_cast<HWND>(ownerTaskbar)) ? 1 : 0;
}

extern "C" W7T_API void W7T_CALL W7T_OverflowShow(int32_t left, int32_t top,
                                                  int32_t right, int32_t bottom) {
    RECT rc{ left, top, right, bottom };

    /* v2.61 - Anche su Windows 11 si apre il pannello nostro, e non piu' il
     * flyout delle icone nascoste della shell.
     *
     * La via "shell" (invocare la freccetta vera e riposizionare la sua
     * isola XAML) si e' rivelata inaffidabile su 24H2: la freccetta non
     * risponde all'invoke e il clic restava senza effetto. Il pannello
     * nostro invece non dipende da nessuna isola: si riempie del modello
     * della tray, che su Windows 11 contiene le icone lette via UI
     * Automation (comprese quelle che la shell tiene nascoste) piu' le tre
     * ricreate da noi. Limite dichiarato in docs/Windows11.md: si vedono le
     * icone che la shell espone, non per forza tutte quelle di Explorer. */
    (void)rc;
    g_overflowWindow.ShowNear(rc);
}

/* v2.60: il frontend deve sapere se il clic sulla freccetta apre il flyout
 * di sistema (nessun pannello nostro da chiudere, nessun rettangolo da
 * escludere dall'hook dei clic esterni).
 *
 * v2.61: sempre 0 - si apre SEMPRE il pannello nostro. Sulle build di
 * Windows 11 24H2 la freccetta della shell non risponde all'invoke UI
 * Automation: il flyout di sistema non si apriva e il clic non faceva
 * nulla. Il pannello nostro e' lo stesso su ogni sistema, si chiude al
 * clic fuori e mostra tutte le icone che il modello conosce. */
extern "C" W7T_API int32_t W7T_CALL W7T_OverflowUsesShellFlyout(void) {
    return 0;
}

/* v2.61: il frontend deve poter distinguere Windows 11 senza indovinare
 * dalla versione gestita (il manifest puo' mentire: senza i GUID supportedOS
 * GetVersionEx riferisce Windows 8.1). Il core lo sa con certezza, perche'
 * legge RtlGetVersion. */
extern "C" W7T_API int32_t W7T_CALL W7T_IsWindows11(void) {
    return w7t::IsWindows11OrBetter() ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/*  v2.63 - Impostazioni dei riquadri: UNA sola pubblicazione             */
/* ---------------------------------------------------------------------- */
/*  Il frontend legge la configurazione e la pubblica qui: da questo       */
/*  momento la decisione "riquadro di Windows 7 oppure della shell" vive    */
/*  in un posto solo (FlyoutLauncher.cpp) e tutti i percorsi di apertura -  */
/*  clic sulle icone ricreate, menu della barra, clic sintetici - la        */
/*  interrogano invece di reinterpretare i propri parametri. E' la cura     */
/*  del difetto per cui la tendina "Windows 10/11" apriva il riquadro di    */
/*  Windows 7 e viceversa: la polarita' era replicata in quattro punti.     */
/*                                                                         */
/*  0 = Windows 7 (classico/Win32), 1 = Windows 10/11 (shell).             */
extern "C" W7T_API void W7T_CALL W7T_SetFlyoutPreferences(
    int32_t clockWin7, int32_t networkWin7, int32_t volumeWin7,
    int32_t batteryWin7) {
    w7t::FlyoutPreferences prefs;
    prefs.clock   = clockWin7   ? w7t::FlyoutStyle::Win7 : w7t::FlyoutStyle::Modern;
    /* v3.8: per la rete c'e' una terza possibilita': 2 = Windows 8
     * (ricreato). Per 0 e 1 il significato e' immutato. */
    prefs.network = networkWin7 == 2 ? w7t::FlyoutStyle::Win8
                    : (networkWin7 ? w7t::FlyoutStyle::Win7 : w7t::FlyoutStyle::Modern);
    prefs.volume  = volumeWin7  ? w7t::FlyoutStyle::Win7 : w7t::FlyoutStyle::Modern;
    prefs.battery = batteryWin7 ? w7t::FlyoutStyle::Win7 : w7t::FlyoutStyle::Modern;
    w7t::SetFlyoutPreferences(prefs);
}

/* ---------------------------------------------------------------------- */
/*  v1.21.7 - Extra settings: ONE single publication point                */
/* ---------------------------------------------------------------------- */
/*  The managed layer stays the only configuration of the program (the     */
/*  entries of the extra settings live in settings.json like all the       */
/*  others). The values arrive here when they change, at startup and at    */
/*  every OK/Apply:
 *
 *    - the privacy mode goes straight to the recreated network flyout,
 *      which changes ONLY the drawn names (no network API, no Windows
 *      setting);
 *    - the flyout colour (system or custom) stays available to the
 *      recreated Windows 8-style flyout, which does not exist yet in this
 *      version (Win8NetworkFlyout.cpp is not compiled). No Windows 7 flyout
 *      reads this value: its look does not change, as required. */
extern "C" W7T_API void W7T_CALL W7T_SetExtraSettings(
    int32_t flyoutColorMode, uint32_t flyoutColorRgb,
    int32_t connectionPrivacyMode) {
    W7T_SEH_TRY
        w7t::extras::SetFlyoutColorMode(flyoutColorMode);
        w7t::extras::SetFlyoutCustomColor(flyoutColorRgb);
        /* The privacy mode is applied here too: this is the call the managed
         * layer makes after saving the choice, so the flyout is always in
         * step with the configuration with no further round trip. */
        w7t::extras::SetConnectionPrivacyMode(connectionPrivacyMode);

        /* v1.21.16: the recreated Windows 8 flyout is the only surface these
         * two settings are allowed to paint, so it is told right away: it
         * re-reads them and repaints itself if it is on screen. Nothing else
         * in the program or in Windows is involved. */
        w7t::Win8NetworkFlyout::Instance().RefreshFlyoutColour();
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* Resolved colour of the recreated flyout: the system accent when the mode
 * is "system colour" (asked to the system EVERY time), the chosen colour
 * otherwise. The frontend uses it to draw the swatch next to the choice; the
 * recreated Windows 8 flyout (Win8NetworkFlyout.cpp, part of this build since
 * v1.21.16) draws itself with it too. */
extern "C" W7T_API int32_t W7T_CALL W7T_GetExtraFlyoutColor(uint32_t* outRgb) {
    if (outRgb == nullptr) return 0;
    W7T_SEH_TRY {
        *outRgb = w7t::extras::ResolveFlyoutColor();
        return 1;
    } W7T_SEH_CATCH {} W7T_SEH_END
    return 0;
}

/* La porta dei riquadri moderni di questa build. Il frontend la usa per non
 * duplicare il giudizio sulla versione di Windows. */
extern "C" W7T_API int32_t W7T_CALL W7T_IsModernFlyoutHostAvailable(void) {
    return w7t::IsModernFlyoutHostAvailable() ? 1 : 0;
}

extern "C" W7T_API void W7T_CALL W7T_OverflowHide(void) {
    g_overflowWindow.Hide();
}

extern "C" W7T_API int32_t W7T_CALL W7T_OverflowIsVisible(void) {
    return g_overflowWindow.IsVisible() ? 1 : 0;
}

/* v3.1: refresh conservativo del pannello (dopo pin/unpin da C#). */
extern "C" W7T_API void W7T_CALL W7T_OverflowRefresh(void) {
    w7t::TrayOverflowWindow::NotifyTrayChanged();
}

extern "C" W7T_API int32_t W7T_CALL W7T_OverflowGetRect(int32_t* left, int32_t* top,
                                                        int32_t* right, int32_t* bottom) {
    HWND h = g_overflowWindow.NativeHandle();
    if (h == nullptr || !IsWindow(h)) return 0;
    RECT rc{};
    if (!GetWindowRect(h, &rc)) return 0;
    if (left) *left = rc.left;
    if (top) *top = rc.top;
    if (right) *right = rc.right;
    if (bottom) *bottom = rc.bottom;
    return 1;
}

/* v3.0: ricerca applicazioni opzionale (si attiva da Proprietà). */
static w7t::AppSearchWindow g_appSearch;

/* v3.3: finestra Proprietà Win32 classica (schede, come la mod di ref). */
static w7t::PropertiesDialog g_properties;

extern "C" W7T_API void W7T_CALL W7T_PropertiesShow(uint64_t ownerTaskbar,
        int32_t lang, int32_t seconds, int32_t nativeFlyout,
        int32_t enableSearch, int32_t netFlyout, int32_t classicVolume,
        int32_t batteryFlyout, int32_t aeroPeek, int32_t toolbarDesktop,
        int32_t toolbarAddress, int32_t toolbarLinks,
        int32_t inputLanguageMode, int32_t taskManagerMode,
        int32_t flyoutColorMode, int32_t flyoutColorRgb,
        int32_t connectionPrivacyMode, int32_t themeSelection,
        int32_t autoStart,
        int32_t taskbarPosition, int32_t lockTaskbar,
        int32_t windowsKeyOpensOurMenu) {
    try {
        /* v3.6: l'ordine DEVE essere quello della firma Show(): nativeFlyout,
         * enableSearch, netFlyout. Prima erano invertiti (netFlyout al posto
         * di enableSearch e viceversa): la spunta "ricerca" accendeva il
         * flyout di rete e il selettore flyout di rete accendeva la ricerca.
         * v1.21.37: parametro = stato corrente dell'avvio automatico
         * con Windows, letto dal registro dal gestito (AutoStart.cs, logica
         * di RetroBar). v1.21.43: posizione barra (0..3) + blocco.
         * v1.3.0: windowsKeyOpensOurMenu in coda. */
        g_properties.Show(reinterpret_cast<HWND>(ownerTaskbar), lang,
                          seconds, nativeFlyout, enableSearch,
                          netFlyout, classicVolume, batteryFlyout, aeroPeek,
                          toolbarDesktop, toolbarAddress, toolbarLinks,
                          inputLanguageMode, taskManagerMode,
                          flyoutColorMode, flyoutColorRgb,
                          connectionPrivacyMode, themeSelection,
                          autoStart, taskbarPosition, lockTaskbar,
                          windowsKeyOpensOurMenu);
    } catch (...) { /* mai propagare */ }
}

extern "C" W7T_API int32_t W7T_CALL W7T_AppSearchInit(uint64_t ownerTaskbar,
        const uint32_t* argbPixels, int32_t iconW, int32_t iconH) {
    /* v1.21.50: mai propagare oltre il confine extern "C" (la Create
     * alloca bitmap, icone e un thread di scansione: una qualsiasi
     * eccezione diventa "ricerca non disponibile", non un crash). */
    try {
        return g_appSearch.Create(GetModuleHandleW(nullptr),
                                  reinterpret_cast<HWND>(ownerTaskbar),
                                  argbPixels, iconW, iconH) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

/* v1.21.30: the managed side passes the active theme (0 Win7, 1 Win8.1)
 * so the search window repaints with the matching skin on every open.
 * v1.21.50: 2 = "Windows 7 Aero Basic": same Win7 skin, fully opaque
 * mask (no glass), see kSkinWin7Basic in AppSearchWindow.cpp.
 * 3 = "Windows 8 Beta 8148": reuse the Windows 8.1 search renderer with
 * a slightly translucent background; taskbar skin resources are unchanged.
 * The two calls are inside a barrier: an exception must never cross the
 * extern "C" edge (Show can join the scan thread and allocate). */
extern "C" W7T_API void W7T_CALL W7T_AppSearchShow(int32_t x, int32_t y,
        int32_t theme) {
    try {
        g_appSearch.SetTheme(theme);
        g_appSearch.Show(x, y);
    } catch (...) { /* mai propagare */ }
}

extern "C" W7T_API void W7T_CALL W7T_AppSearchHide(void) {
    g_appSearch.Hide();
}

/* v2.37 punto 17: il pulsante lente funziona da interruttore: se la
 * ricerca e' gia' aperta, un click la chiude (e viceversa). */
extern "C" W7T_API int32_t W7T_CALL W7T_AppSearchIsVisible(void) {
    return g_appSearch.IsVisible() ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Jump List stile Windows 7: gestico clic sinistro + trascinamento    */
/* verso l'alto (sistema del TaskbarWindow.JumpList.cs gestito).        */
/* Tutte le coordinate sono PIXEL FISICI DELLO SCHERMO: il livello      */
/* gestito converte i DIP WPF con PointToScreen e le inoltra qui;       */
/* il nativo scala la geometria sul DPI del monitor del pulsante.       */
/* ------------------------------------------------------------------ */
extern "C" W7T_API int32_t W7T_CALL W7T_JumpListOpen(
        const RECT* buttonRect, int32_t edge, const wchar_t* title,
        const wchar_t* launchPath, const wchar_t* pinnedLnk,
        int32_t isPinned, uint64_t hwnd, const wchar_t* exePath,
        const uint32_t* iconArgb, int32_t iconW, int32_t iconH,
        int32_t lang, wchar_t* outAppId, int32_t outAppIdCap) {
    if (buttonRect == nullptr) return -3;
    W7T_SEH_TRY {
        return w7t::JumpListWindow::Instance().Open(
            *buttonRect, edge,
            title ? title : L"",
            launchPath ? launchPath : L"",
            pinnedLnk ? pinnedLnk : L"",
            isPinned == 1,
            reinterpret_cast<HWND>(static_cast<uintptr_t>(hwnd)),
            exePath ? exePath : L"",
            iconArgb, iconW, iconH, lang,
            outAppId, outAppIdCap);
    } W7T_SEH_CATCH {
        w7t::LogTagged(L"JUMPLIST", L"fault at the export boundary (open)");
        return -1;
    } W7T_SEH_END
    return -1;
}

/* Movimento del cursore durante il gesto: aggiorna la riga evidenziata
 * e risponde 1 se il punto e' ancora nell'area di interazione. */
extern "C" W7T_API int32_t W7T_CALL W7T_JumpListSetHover(int32_t screenX,
        int32_t screenY) {
    W7T_SEH_TRY {
        return w7t::JumpListWindow::Instance().SetHover(screenX, screenY);
    } W7T_SEH_CATCH {
        w7t::LogTagged(L"JUMPLIST", L"fault at the export boundary (hover)");
        return 1;   /* non interrompere il gesto per un errore di dipendenza */
    } W7T_SEH_END
    return 1;
}

/* v2.62: la riga sotto il punto schermo (>=0 indice, -1 nessuna), senza
 * effetti collaterali: la decisione del rilascio (attivare / lasciare
 * aperta / annullare) e' del gestito. */
extern "C" W7T_API int32_t W7T_CALL W7T_JumpListHitRow(int32_t screenX,
        int32_t screenY) {
    W7T_SEH_TRY {
        return w7t::JumpListWindow::Instance().HitRowAt(screenX, screenY);
    } W7T_SEH_CATCH {
        w7t::LogTagged(L"JUMPLIST",
                       L"fault at the export boundary (hit row)");
        return -1;
    } W7T_SEH_END
    return -1;
}

/* The drag release only transfers ordinary input/focus to the popup. It
 * deliberately does not select the row under that release. */
extern "C" W7T_API void W7T_CALL W7T_JumpListMakeInteractive(void) {
    W7T_SEH_TRY {
        w7t::JumpListWindow::Instance().MakeInteractive();
    } W7T_SEH_CATCH {
        w7t::JumpListWindow::Instance().Hide();
    } W7T_SEH_END
}

/* Ordinary popup clicks activate a row through WndProc. This export stays
 * available for ABI compatibility with older managed builds. */
extern "C" W7T_API int32_t W7T_CALL W7T_JumpListActivateAt(
        int32_t screenX, int32_t screenY, int32_t* outBits) {
    W7T_SEH_TRY {
        return w7t::JumpListWindow::Instance().ActivateRow(
            screenX, screenY, outBits);
    } W7T_SEH_CATCH {
        w7t::LogTagged(L"JUMPLIST",
                       L"fault at the export boundary (activate)");
        if (outBits) *outBits = 0;
        w7t::JumpListWindow::Instance().Hide();
        return 0;
    } W7T_SEH_END
    return 0;
}

/* ------------------------------------------------------------------ */
/* v1.4: selettore della lingua (port del mod switcher).              */
/* ------------------------------------------------------------------ */
extern "C" W7T_API void W7T_CALL W7T_LangSwitcherShow(uint64_t ownerHwnd,
        uint64_t foregroundHwnd, int32_t styleMode) {
    W7T_SEH_TRY {
        w7t::langswitcher::Show(ownerHwnd, foregroundHwnd, styleMode);
    } W7T_SEH_CATCH {} W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_LangSwitcherHide(void) {
    W7T_SEH_TRY {
        w7t::langswitcher::Hide();
    } W7T_SEH_CATCH {} W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_LangSwitcherGetActive(uint32_t* langId,
        wchar_t* threeLetter, int32_t threeCap,
        wchar_t* twoLetter, int32_t twoCap) {
    W7T_SEH_TRY {
        w7t::langswitcher::GetActiveInfo(langId, threeLetter, threeCap,
                                         twoLetter, twoCap);
    } W7T_SEH_CATCH {} W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_LangSwitcherSetChangedCallback(
        W7T_LangChangedCallback callback) {
    W7T_SEH_TRY {
        w7t::langswitcher::SetChangedCallback(callback);
    } W7T_SEH_CATCH {} W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_JumpListHide(void) {
    W7T_SEH_TRY
        w7t::JumpListWindow::Instance().Hide();
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* ------------------------------------------------------------------ */
/* v2.38: flyout batteria ricreato (icone reali ritagliate).           */
/* ------------------------------------------------------------------ */
extern "C" W7T_API void W7T_CALL W7T_BatteryFlyoutShowAt(
        int32_t left, int32_t top, int32_t right, int32_t bottom) {
    W7T_SEH_TRY {
        RECT rc{ left, top, right, bottom };
        w7t::BatteryFlyout::Instance().ShowAt(rc);
    } W7T_SEH_CATCH {} W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_BatteryFlyoutHide(void) {
    W7T_SEH_TRY
        w7t::BatteryFlyout::Instance().Hide();
    W7T_SEH_CATCH
    W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_BatteryFlyoutSetLanguage(int32_t lang) {
    W7T_SEH_TRY
        w7t::BatteryFlyout::Instance().SetLanguage(lang);
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* OPZIONE B: il gestito chiede esplicitamente il restore in chiusura
 * pulita. Idempotente: senza tentativi pendenti e' un no-op. */
extern "C" W7T_API void W7T_CALL W7T_BatteryFlyoutRestoreLegacyKey(void) {
    W7T_SEH_TRY
        TrayService::RestoreBatteryFlyoutKey();
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* ------------------------------------------------------------------ */
/* v2.36: flyout di rete Windows 7 (porting MIT della mod Windhawk    */
/* "Windows 7 Network Flyout Recreation" v5.0.0).                     */
/* ------------------------------------------------------------------ */
#include "Win7NetworkFlyout.h"
#include <psapi.h>

extern "C" W7T_API int32_t W7T_CALL W7T_NetFlyoutInit(void) {
    int32_t r = 0;
    W7T_SEH_TRY
        r = w7tnet::W7TNetFlyout_InitInternal() ? 1 : 0;
    W7T_SEH_CATCH
    W7T_SEH_END
    return r;
}

extern "C" W7T_API void W7T_CALL W7T_NetFlyoutUninit(void) {
    W7T_SEH_TRY
        w7tnet::W7TNetFlyout_UninitInternal();
    W7T_SEH_CATCH
    W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_NetFlyoutToggleAt(const RECT* rcIcon) {
    W7T_SEH_TRY
        /* Esclusione reciproca, anche su questo percorso diretto: mai i
         * due riquadri visibili insieme. */
        w7t::Win8NetworkFlyout::Instance().Hide();
        w7tnet::W7TNetFlyout_SetAnchorRect(rcIcon);
        w7tnet::W7TNetFlyout_Toggle();
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* v2.37 punto 16: lingua del flyout = lingua dell'app (mapping sugli
 * idiomi gia' tradotti dentro la mod). */
extern "C" W7T_API void W7T_CALL W7T_NetFlyoutSetLanguage(int32_t appLanguageIndex) {
    W7T_SEH_TRY
        w7tnet::W7TNetFlyout_SetLanguage(appLanguageIndex);
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* ------------------------------------------------------------------ */
/* v3.8/v4.0: flyout di rete variazione Windows 8 - PORTING COMPLETO   */
/* della mod "Windows 8x Network Flyout Recreation" v1.0.0 (AdmXP8/     */
/* Administratox, MIT): riquadro laterale tipo Charms con propria       */
/* logica WLAN/Ethernet nativa. Esclusa solo la parte Pannello di       */
/* controllo. Facciata: Win8NetworkFlyout.h.                           */

extern "C" W7T_API int32_t W7T_CALL W7T_Net8FlyoutInit(void) {
    int32_t r = 0;
    W7T_SEH_TRY
        r = w7t::Win8NetworkFlyout::Instance().Init() ? 1 : 0;
    W7T_SEH_CATCH
    W7T_SEH_END
    return r;
}

extern "C" W7T_API void W7T_Net8FlyoutUninit(void) {
    W7T_SEH_TRY
        w7t::Win8NetworkFlyout::Instance().Uninit();
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* v3.8: chiude il riquadro Windows 8 senza de-inizializzare il modulo (la
 * modalita' di rete e' cambiata ad altra voce: il riquadro non deve
 * restare sullo schermo sotto un'etichetta diversa). */
extern "C" W7T_API void W7T_Net8FlyoutHide(void) {
    W7T_SEH_TRY
        w7t::Win8NetworkFlyout::Instance().Hide();
    W7T_SEH_CATCH
    W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_Net8FlyoutToggleAt(const RECT* rcIcon) {
    W7T_SEH_TRY
        if (rcIcon != nullptr) {
            w7t::Win8NetworkFlyout::Instance().SetAnchorRect(*rcIcon);
        }
        w7t::Win8NetworkFlyout::Instance().Toggle();
    W7T_SEH_CATCH
    W7T_SEH_END
}

extern "C" W7T_API void W7T_CALL W7T_Net8FlyoutSetLanguage(int32_t appLanguageIndex) {
    W7T_SEH_TRY
        w7t::Win8NetworkFlyout::Instance().SetLanguage(appLanguageIndex);
    W7T_SEH_CATCH
    W7T_SEH_END
}

/* v3.8: il frontend dichiara pronto il riquadro di rete variante Windows 8
 * (modulo inizializzato e modo "Windows 8 (ricreato)" attivo). Stesso patto
 * di W7T_SetWin7NetworkFlyout: senza avviso il core ripiega sulla shell. */
extern "C" W7T_API void W7T_CALL W7T_SetWin8NetworkFlyout(int32_t ready) {
    TrayService::Instance().SetWin8NetworkFlyout(ready != 0);
}

/* v2.36/v2.38: modulo (DLL) che possiede la finestra proprietaria di
 * un'icona tray: pnidui.dll = rete, SndVolSSO.dll = volume, stobject.dll
 * = batteria (tutte dentro explorer.exe). La verifica cross-process con
 * psapi vive in Common.cpp (OwnerModuleIs), condivisa col TrayService. */
extern "C" W7T_API int32_t W7T_CALL W7T_IsNetworkTrayOwner(uint64_t ownerHwnd) {
    W7T_SEH_TRY {
        return w7t::OwnerModuleIs(reinterpret_cast<HWND>(
            static_cast<uintptr_t>(ownerHwnd)), L"pnidui.dll") ? 1 : 0;
    } W7T_SEH_CATCH {} W7T_SEH_END
    return 0;
}

/* v2.38: il managed chiede se l'icona tray appartiene a un modulo di
 * sistema (volume/batteria) per decidere i flyout ricreati. */
extern "C" W7T_API int32_t W7T_CALL W7T_TrayOwnerModuleMatch(
        uint64_t ownerHwnd, const wchar_t* moduleName) {
    W7T_SEH_TRY {
        if (moduleName == nullptr) return 0;
        return w7t::OwnerModuleIs(reinterpret_cast<HWND>(
            static_cast<uintptr_t>(ownerHwnd)), moduleName) ? 1 : 0;
    } W7T_SEH_CATCH {} W7T_SEH_END
    return 0;
}

/* v2.40: il flag -f di SndVol non e' affidabile su tutte le build (su
 * alcune il riquadro parte comunque in alto a sinistra). Dopo il lancio,
 * un thread breve cerca la finestra top-level di SndVol.exe e la sposta
 * SOPRA l'icona che l'ha generata, usando le coordinate schermo passate
 * dal managed. Best-effort: se non la trova entro ~1,5 s rinuncia senza
 * toccare nulla. Mai crashare. */
namespace {
bool WindowIsSndVol(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == 0) return false;
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hp == nullptr) return false;
    wchar_t path[MAX_PATH]{};
    DWORD len = MAX_PATH;
    const bool ok = QueryFullProcessImageNameW(hp, 0, path, &len) != 0;
    CloseHandle(hp);
    if (!ok) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"SndVol.exe") == 0;
}
/* v2.44: il flyout di SndVol avviato con -f e' TRANSIENTE: se non riceve
 * MAI il focus si chiude da solo dopo ~2 s (era il difetto segnalato: il
 * riquadro del volume spariva dopo due secondi). Una sola chiamata a
 * SetForegroundWindow non basta, perche' il sistema la RIFIUTA quando il
 * processo chiamante non e' gia' in primo piano (foreground lock). Qui si
 * insiste, con le stesse due tecniche dei progetti open source:
 *  - tap del tasto Alt per sbloccare il foreground-lock (come
 *    WindowManager::ForegroundUnlock, usato per le finestre minimizzate);
 *  - AttachThreadInput sul thread del riquadro, che consente il cambio di
 *    primo piano anche a un processo non attivo. */
void ForceFlyoutForeground(HWND h) {
    if (h == nullptr || !IsWindow(h)) {
        return;
    }
    if (GetForegroundWindow() == h) {
        return;   /* gia' in primo piano: niente da fare */
    }

    /* Primo tentativo, quello pulito: il clic che ha aperto il riquadro e'
     * arrivato al NOSTRO processo, quindi di norma il sistema ci lascia
     * portare in primo piano la finestra che abbiamo appena avviato. */
    SetForegroundWindow(h);
    if (GetForegroundWindow() == h) {
        return;
    }

    /* Ripiego: il sistema ha RIFIUTATO il cambio di primo piano (locks
     * SPI_GETFOREGROUNDLOCKTIMEOUT). Le due tecniche dei progetti open
     * source, le stesse che questa mod usa gia' per attivare le finestre
     * dei pulsanti della Superbar: tap del tasto Alt con SendInput, che fa
     * risultare il nostro processo come "ultimo input", e AttachThreadInput
     * sul thread del riquadro. */
    INPUT unlock[2] = {};
    unlock[0].type = INPUT_KEYBOARD;
    unlock[0].ki.wVk = VK_MENU;
    unlock[1].type = INPUT_KEYBOARD;
    unlock[1].ki.wVk = VK_MENU;
    unlock[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, unlock, sizeof(INPUT));

    const DWORD targetThread = GetWindowThreadProcessId(h, nullptr);
    const DWORD thisThread = GetCurrentThreadId();
    bool attached = false;
    if (targetThread != 0 && targetThread != thisThread) {
        attached = AttachThreadInput(thisThread, targetThread, TRUE) != FALSE;
    }
    SetForegroundWindow(h);
    BringWindowToTop(h);
    if (attached) {
        AttachThreadInput(thisThread, targetThread, FALSE);
    }
}

/* v2.44: guardiano del riquadro volume.
 *
 * La finestra dei ~2 s e' quella in cui SndVol si autodistrugge quando non
 * ha mai ricevuto il focus: qui si insiste a darglielo finche' non l'ha
 * ottenuto (o finche' non e' finita la finestra critica). Il primo
 * SetForegroundWindow viene spesso RIFIUTATO dal sistema, perche' il
 * processo della barra non e' in primo piano quando parte questo thread
 * (il clic che ha aperto il riquadro e' arrivato a una finestra
 * WS_EX_NOACTIVATE, che non prende il focus): senza questa insistenza il
 * riquadro spariva comunque dopo due secondi.
 *
 * Dopo la finestra critica il guardiano NON tocca piu' nulla: se il focus
 * lo prende un'altra applicazione, il riquadro si chiude, esattamente come
 * in Windows 7 (e un clic sulla nostra barra lo chiude subito via
 * W7T_CloseClassicVolume).
 *
 * Il ciclo si interrompe subito se il riquadro non esiste piu', cosi' non
 * c'e' nessuna attesa residua quando l'utente l'ha gia' chiuso. Nessun
 * blocco sul thread chiamante: gira nel thread dedicato lanciato da
 * W7T_LaunchClassicVolume. */
void KeepSndVolFlyoutAlive(HWND h) {
    /* ~2,5 s complessivi, un tentativo ogni 350 ms. */
    for (int i = 0; i < 7; ++i) {
        if (!IsWindow(h) || !IsWindowVisible(h)) {
            return;   /* chiuso: il guardiano ha finito */
        }
        if (GetForegroundWindow() == h) {
            /* Ha il focus: da qui in poi si tiene aperto da solo, come in
             * Windows 7, finche' l'utente non clicca altrove. */
            AppendCoreLog(L"volume: flyout in primo piano, resta aperto");
            return;
        }
        ForceFlyoutForeground(h);
        Sleep(350);
    }
    AppendCoreLog(L"volume: il flyout non ha ottenuto il primo piano "
                  L"(restera' il comportamento transiente di SndVol: "
                  L"si chiude da solo dopo ~2 s)");
}

void RepositionSndVolAbove(int x, int y) {
    /* v2.55: RIPRISTINATO il comportamento che funzionava.
     *
     * La v2.54 aveva provato a intercettare il riquadro prima che fosse
     * visibile (polling ogni 5 ms, nessuna attesa iniziale, nascondi ->
     * sposta -> mostra). Su Windows vero il risultato e' stato PEGGIORE:
     * il riquadro finiva in alto a sinistra, perche' SndVol completa la
     * propria inizializzazione DOPO di noi e riporta la finestra nella sua
     * posizione di default. Intercettarlo troppo presto, e soprattutto
     * nascondere/rimostrare la finestra, interferisce con quella sequenza.
     *
     * Qui si torna all'ordine precedente: si aspetta che il riquadro sia
     * DAVVERO visibile (SndVol ha finito di posizionarsi), poi lo si sposta
     * sopra l'icona, senza mai nasconderlo. */
    for (int i = 0; i < 30; ++i) {
        Sleep(50);
        struct Ctx { HWND found; };
        Ctx ctx{ nullptr };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            Ctx* c = reinterpret_cast<Ctx*>(lp);
            if (IsWindowVisible(h) && WindowIsSndVol(h)) {
                c->found = h;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.found != nullptr) {
            RECT rc{};
            GetWindowRect(ctx.found, &rc);
            const int w = rc.right - rc.left;
            const int hgt = rc.bottom - rc.top;
            /* v2.42: il riquadro ancora sopra l'icona fluttuando appena:
             * 1,5% della propria altezza sopra il bordo superiore
             * dell'icona (richiesta utente), non il distacco overflow.
             * v2.43: richiesto di alzarlo ancora di un 1,5%: il distacco
             * passa da 1,5% a 3% dell'altezza del riquadro, cosi' il
             * flyout stile Windows 7 fluttua appena piu' in alto sopra
             * l'icona di volume.
             * v2.45: ultimo ritocco richiesto, +1,02%: distacco 4,02%
             * (30 -> 40,2 su 1000) per centrare la posizione voluta. */
            int gap = (hgt * 402) / 10000;
            if (gap < 2) gap = 2;
            SetWindowPos(ctx.found, HWND_TOPMOST,
                         x - w / 2, y - hgt - gap, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            /* v2.42: il flyout -f e' transiente se non riceve mai il
             * focus (si chiude da solo dopo ~2 s). Attivandolo resta
             * aperto finche' l'utente non clicca altrove (e un clic
             * sulla nostra barra lo chiude via W7T_CloseClassicVolume).
             * v2.44: l'attivazione viene ora RIPETUTA per qualche secondo
             * dal guardiano, perche' il primo tentativo puo' essere
             * rifiutato dal sistema e il riquadro sparire comunque. */
            ForceFlyoutForeground(ctx.found);
            KeepSndVolFlyoutAlive(ctx.found);
            return;
        }
    }
}
} /* namespace */

/* v2.38 punto 1: mixer volume classico. SndVol.exe esiste in System32
 * su Win10/11; -f accetta le coordinate impaccate in un DWORD
 * (LOWORD=x, HIWORD=y, come documentato dal comportamento del binario
 * e dal mod windhawk "legacy-sound-flyout", MIT). CreateProcessW senza
 * attese: fire-and-forget; se il binario manca, ritorna 0 e il managed
 * ricade sul flyout attuale. */
extern "C" W7T_API int32_t W7T_CALL W7T_LaunchClassicVolume(
        int32_t x, int32_t y) {
    W7T_SEH_TRY {
        static std::atomic<ULONGLONG> s_last{ 0 };
        const ULONGLONG now = GetTickCount64();
        ULONGLONG last = s_last.load();
        if (now - last < 400) return 1;   /* debounce: gia' lanciato */
        s_last.store(now);
        wchar_t sys32[MAX_PATH]{};
        GetSystemDirectoryW(sys32, MAX_PATH);
        wchar_t cmd[1024]{};
        const DWORD encoded = static_cast<DWORD>(MAKELONG(
            static_cast<SHORT>(x), static_cast<SHORT>(y)));
        wsprintfW(cmd, L"\"%s\\SndVol.exe\" -f %u", sys32, encoded);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_FORCEOFFFEEDBACK;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0,
                           nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            /* v2.40: ancora il riquadro sopra l'icona (best-effort). */
            std::thread([x, y]() { RepositionSndVolAbove(x, y); }).detach();
            return 1;
        }
        return 0;
    } W7T_SEH_CATCH {} W7T_SEH_END
    return 0;
}

/* v2.41: chiude il mixer classico (SndVol) quando un clic sulla nostra
 * barra deve chiudere i riquadri aperti, come per orologio/rete. */
extern "C" W7T_API void W7T_CALL W7T_CloseClassicVolume(void) {
    W7T_SEH_TRY {
        EnumWindows([](HWND h, LPARAM) -> BOOL {
            if (IsWindowVisible(h) && WindowIsSndVol(h))
                PostMessageW(h, WM_CLOSE, 0, 0);
            return TRUE;
        }, 0);
    } W7T_SEH_CATCH {} W7T_SEH_END
}

/* v2.62-alpha: the user's preview configuration (read-only, cached in
 * w7t::GetPreviewPolicy). Delays are -1 when the user value is absent, so
 * the frontend keeps its own project default. */
extern "C" W7T_API int32_t W7T_CALL W7T_GetPreviewPolicy(int32_t* outWindowThumbs,
        int32_t* outDesktopPeek, int32_t* outThumbHoverMs,
        int32_t* outPeekHoverMs) {
    W7T_SEH_TRY {
        const w7t::PreviewPolicy p = w7t::GetPreviewPolicy();
        if (outWindowThumbs != nullptr) *outWindowThumbs = p.windowThumbsEnabled ? 1 : 0;
        if (outDesktopPeek != nullptr) *outDesktopPeek = p.desktopPeekEnabled ? 1 : 0;
        if (outThumbHoverMs != nullptr)
            *outThumbHoverMs = p.thumbHoverMsSet ? (int32_t)p.thumbHoverMs : -1;
        if (outPeekHoverMs != nullptr)
            *outPeekHoverMs = p.peekHoverMsSet ? (int32_t)p.peekHoverMs : -1;
    } W7T_SEH_CATCH { /* defaults stay: the core keeps working */ } W7T_SEH_END
    return W7T_OK;
}

/* v2.62-alpha (G6): canonical pin verb. The on-disk state is refreshed by
 * the PinnedApps folder watcher, which queues W7T_EVT_PINNED_CHANGED only
 * when the model actually changed. */
extern "C" W7T_API int32_t W7T_CALL W7T_ToggleTaskbarPin(const wchar_t* exePath,
        const wchar_t* baseName, int32_t pin) {
    if (exePath == nullptr || baseName == nullptr) {
        return W7T_ERR_INVALID_ARG;
    }
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        const bool changed = w7t::TogglePinnedApp(std::wstring(exePath),
                                                  std::wstring(baseName),
                                                  pin != 0, nullptr);
        result = changed ? 1 : 0;
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}

/* v2.62-alpha (G4): the executable's own icon (the criterion behind
 * "use the executable for the taskbar group icon"). The project's single
 * icon pipeline (IconToArgb/EmitBitmap) does the pixel work, so the bitmap
 * layout is exactly the one W7T_GetWindowIconBitmap exposes. */
extern "C" W7T_API int32_t W7T_CALL W7T_GetExeIconBitmap(const wchar_t* exePath,
        int32_t desiredSize, int32_t* width, int32_t* height,
        uint8_t* pixels, int32_t pixelsBytes) {
    if (exePath == nullptr || exePath[0] == L'\0' || pixelsBytes < 0) {
        return W7T_ERR_INVALID_ARG;
    }
    if (pixels == nullptr && pixelsBytes != 0) {
        return W7T_ERR_INVALID_ARG;
    }
    int32_t result = W7T_ERR_NOT_FOUND;
    W7T_SEH_TRY {
        SHFILEINFOW sfi{};
        const UINT flags = SHGFI_SYSICONINDEX | SHGFI_ICON |
            (desiredSize > 16 ? SHGFI_LARGEICON : SHGFI_SMALLICON);
        if (SHGetFileInfoW(exePath, 0, &sfi, sizeof(sfi), flags) != 0 &&
            sfi.hIcon != nullptr) {
            ArgbBitmap bmp;
            const bool ok = IconToArgb(sfi.hIcon, bmp) && BitmapSane(bmp);
            DestroyIcon(sfi.hIcon);
            if (ok) {
                result = EmitBitmap(bmp, width, height, pixels, pixelsBytes);
            }
        }
    } W7T_SEH_CATCH {
        result = W7T_ERR_NOT_FOUND;
    } W7T_SEH_END
    return result;
}
