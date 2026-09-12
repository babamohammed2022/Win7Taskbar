/*
 * Win7Taskbar - Core nativo - Flyout immersivi di sistema
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
 * Sequenza di invocazione e identificatori COM adattati da
 * ExplorerPatcher/ImmersiveFlyouts.{h,c} di valinet (GPL-2.0-or-later).
 * Struttura del codice, gestione degli errori e caching sono originali.
 * Vedi l'intestazione di ImmersiveFlyouts.h e THIRD-PARTY-NOTICES.md.
 */

#include "ImmersiveFlyouts.h"

#include <objbase.h>
#include <tlhelp32.h>

#include <set>
#include <servprov.h>
#include <cstdlib>

namespace w7t {

/* ------------------------------------------------------------------------- */
/*  Identificatori COM                                                       */
/* ------------------------------------------------------------------------- */

/* Non sono materiale creativo protetto: sono identificatori di
 * interoperabilita' con interfacce COM di sistema, gli stessi che compaiono
 * in ExplorerPatcher e nelle dichiarazioni pubbliche di ManagedShell. */

/* CImmersiveShell: il servizio della shell che ospita tutte le esperienze
 * XAML (riquadri di orologio, volume, rete, batteria, centro notifiche). */
static const CLSID kClsidImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA,
    { 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 }
};

/* Fabbrica degli experience manager. */
static const GUID kClsidShellExperienceManagerFactory = {
    0x2E8FCB18, 0xA0EE, 0x41AD,
    { 0x8E, 0xF8, 0x77, 0xFB, 0x3A, 0x37, 0x0C, 0xA5 }
};

/* IID_TrayBatteryFlyoutExperienceManager */
static const IID kIidTrayBatteryFlyoutManager = {
    0x0A73AEDC, 0x1C68, 0x410D,
    { 0x8D, 0x53, 0x63, 0xAF, 0x80, 0x95, 0x1E, 0x8F }
};

/* IID_TrayClockFlyoutExperienceManager */
static const IID kIidTrayClockFlyoutManager = {
    0xB1604325, 0x6B59, 0x427B,
    { 0xBF, 0x1B, 0x80, 0xA2, 0xDB, 0x02, 0xD3, 0xD8 }
};

/* IID_TrayMtcUvcFlyoutExperienceManager (volume / Media Transport Controls) */
static const IID kIidTrayMtcUvcFlyoutManager = {
    0x7154C95D, 0xC519, 0x49BD,
    { 0xA9, 0x7E, 0x64, 0x5B, 0xBF, 0xAB, 0xE1, 0x11 }
};

/* IID_NetworkFlyoutExperienceManager */
static const IID kIidNetworkFlyoutManager = {
    0xC9DDC674, 0xB44B, 0x4C67,
    { 0x9D, 0x79, 0x2B, 0x23, 0x7D, 0x9B, 0xE0, 0x5A }
};

/* ------------------------------------------------------------------------- */
/*  Nomi runtime delle esperienze                                           */
/* ------------------------------------------------------------------------- */

namespace {

const wchar_t* RuntimeName(FlyoutKind kind) {
    switch (kind) {
    case FlyoutKind::Network: return L"Windows.Internal.ShellExperience.NetworkFlyout";
    case FlyoutKind::Clock:   return L"Windows.Internal.ShellExperience.TrayClockFlyout";
    case FlyoutKind::Battery: return L"Windows.Internal.ShellExperience.TrayBatteryFlyout";
    case FlyoutKind::Sound:   return L"Windows.Internal.ShellExperience.MtcUvc";
    }
    return nullptr;
}

const IID* ManagerIid(FlyoutKind kind) {
    switch (kind) {
    case FlyoutKind::Network:      return &kIidNetworkFlyoutManager;
    case FlyoutKind::Clock:        return &kIidTrayClockFlyoutManager;
    case FlyoutKind::Battery:      return &kIidTrayBatteryFlyoutManager;
    case FlyoutKind::Sound:        return &kIidTrayMtcUvcFlyoutManager;
    }
    return nullptr;
}

/* ------------------------------------------------------------------------- */
/*  combase.dll caricato a runtime                                          */
/* ------------------------------------------------------------------------- */

/* Le funzioni HSTRING vivono in combase.dll, che su Windows 7 non esiste.
 * Le risolviamo con GetProcAddress invece di linkare runtimeobject.lib:
 * cosi' la DLL si carica anche dove i flyout immersivi non ci sono, e
 * IsSupported() puo' semplicemente rispondere false. */

using WindowsCreateStringReferenceFn =
    HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
using WindowsDeleteStringFn = HRESULT(WINAPI*)(HSTRING);

WindowsCreateStringReferenceFn g_createStringReference = nullptr;
WindowsDeleteStringFn          g_deleteString          = nullptr;
bool                           g_combaseTried          = false;

bool LoadCombase() {
    if (g_combaseTried) {
        return g_createStringReference != nullptr;
    }
    g_combaseTried = true;

    HMODULE combase = LoadLibraryW(L"combase.dll");
    if (combase == nullptr) {
        return false;
    }

    g_createStringReference = reinterpret_cast<WindowsCreateStringReferenceFn>(
        reinterpret_cast<void*>(
            GetProcAddress(combase, "WindowsCreateStringReference")));
    g_deleteString = reinterpret_cast<WindowsDeleteStringFn>(
        reinterpret_cast<void*>(GetProcAddress(combase, "WindowsDeleteString")));

    return g_createStringReference != nullptr && g_deleteString != nullptr;
}

/* ------------------------------------------------------------------------- */
/*  Cache degli oggetti COM                                                  */
/* ------------------------------------------------------------------------- */

/* Una casella per ogni FlyoutKind, piu' la fabbrica. */
IShellExperienceManagerFactory* g_factory = nullptr;
IExperienceManager*             g_managers[4] = { nullptr, nullptr, nullptr, nullptr };

/* Indice di cache per i quattro flyout che condividono IExperienceManager. */
int ManagerSlot(FlyoutKind kind) {
    switch (kind) {
    case FlyoutKind::Network: return 0;
    case FlyoutKind::Clock:   return 1;
    case FlyoutKind::Battery: return 2;
    case FlyoutKind::Sound:   return 3;
    }
    return -1;
}

void DropManager(FlyoutKind kind) {
    const int slot = ManagerSlot(kind);
    if (slot >= 0) {
        if (g_managers[slot] != nullptr) {
            g_managers[slot]->Release();
            g_managers[slot] = nullptr;
        }
    }
}

/* Butta via tutto: si usa quando la shell e' stata riavviata e gli oggetti
 * che avevamo in cache puntano a un server COM che non esiste piu'. */
void DropAll() {
    DropManager(FlyoutKind::Network);
    DropManager(FlyoutKind::Clock);
    DropManager(FlyoutKind::Battery);
    DropManager(FlyoutKind::Sound);

    if (g_factory != nullptr) {
        g_factory->Release();
        g_factory = nullptr;
    }
}

/* ------------------------------------------------------------------------- */
/*  Preparazione                                                            */
/* ------------------------------------------------------------------------- */

/* La shell prova a portare il proprio riquadro in primo piano. Se non le e'
 * consentito, il riquadro puo' non comparire o non ricevere input: la nostra
 * barra e' WS_EX_NOACTIVATE e il processo della shell non ha il diritto di
 * foreground. Glielo concediamo esplicitamente. */
void AllowShellForeground() {
    HWND progman = FindWindowExW(nullptr, nullptr, L"Progman", L"Program Manager");
    if (progman == nullptr) {
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(progman, &pid);
    if (pid != 0) {
        AllowSetForegroundWindow(pid);
    }
}

IShellExperienceManagerFactory* AcquireFactory() {
    if (g_factory != nullptr) {
        return g_factory;
    }

    /* CLSCTX_NO_CODE_DOWNLOAD come in ExplorerPatcher: non vogliamo che la
     * creazione dell'oggetto scarichi o esegua codice remoto. */
    detail::ComPtr<IServiceProvider> shell;
    HRESULT hr = CoCreateInstance(kClsidImmersiveShell, nullptr,
                                  CLSCTX_NO_CODE_DOWNLOAD | CLSCTX_LOCAL_SERVER,
                                  IID_IServiceProvider,
                                  reinterpret_cast<void**>(shell.Put()));
    if (FAILED(hr) || !shell) {
        return nullptr;
    }

    /* Equivalente di IUnknown_QueryService(punk, sid, riid, &out):
     * il servizio e' esposto sulla stessa interfaccia che lo fornisce. */
    hr = shell->QueryService(kClsidShellExperienceManagerFactory,
                             kClsidShellExperienceManagerFactory,
                             reinterpret_cast<void**>(&g_factory));
    if (FAILED(hr) || g_factory == nullptr) {
        g_factory = nullptr;
        return nullptr;
    }

    return g_factory;
}

/* Crea (non in cache) l'oggetto che gestisce il flyout richiesto. */
IUnknown* AcquireExperience(FlyoutKind kind) {
    IShellExperienceManagerFactory* factory = AcquireFactory();
    if (factory == nullptr || !LoadCombase()) {
        return nullptr;
    }

    const wchar_t* name = RuntimeName(kind);
    if (name == nullptr) {
        return nullptr;
    }

    detail::HStringRef hstring;
    if (!hstring.Create(name)) {
        return nullptr;
    }

    detail::ComPtr<IUnknown> unknown;
    const HRESULT hr = factory->GetExperienceManager(hstring.Get(), unknown.Put());
    if (FAILED(hr) || !unknown) {
        return nullptr;
    }

    return unknown.Detach();
}

/* Restituisce l'IExperienceManager del flyout, dalla cache se c'e'. */
IExperienceManager* GetFlyoutManager(FlyoutKind kind) {
    const int slot = ManagerSlot(kind);
    if (slot < 0) {
        return nullptr;
    }

    if (g_managers[slot] != nullptr) {
        return g_managers[slot];
    }

    detail::ComPtr<IUnknown> unknown(AcquireExperience(kind));
    if (!unknown) {
        return nullptr;
    }

    const IID* iid = ManagerIid(kind);
    if (iid == nullptr) {
        return nullptr;
    }

    IExperienceManager* manager = nullptr;
    if (FAILED(unknown->QueryInterface(*iid, reinterpret_cast<void**>(&manager)))
        || manager == nullptr) {
        return nullptr;
    }

    g_managers[slot] = manager;
    return manager;
}

/* ------------------------------------------------------------------------- */
/*  Invocazione                                                             */
/* ------------------------------------------------------------------------- */

/* --------------------------------------------------------------------- */
/*  Sonda: il flyout si e' davvero materializzato?                        */
/*                                                                       */
/*  ShowFlyout puo' riferire successo senza aprire nulla (host assente,   */
/*  build che non ha piu' quell'esperienza). L'unico modo onesto per      */
/*  saperlo da fuori e' guardare se compare una finestra nuova del host:  */
/*  ShellExperienceHost.exe su Windows 10, isole XAML dentro Explorer su  */
/*  Windows 11. Se non compare, il chiamante passa al suo ripiego invece  */
/*  di lasciare il clic senza effetto.                                    */
/* --------------------------------------------------------------------- */

struct FlyoutWindowSnapshot {
    std::set<DWORD> hostPids;
    std::set<HWND>  windows;
};

BOOL CALLBACK CollectFlyoutWindows(HWND hwnd, LPARAM lParam) {
    auto* snap = reinterpret_cast<FlyoutWindowSnapshot*>(lParam);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t className[64] = {};
    GetClassNameW(hwnd, className, _countof(className));

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    const bool hostProcess = snap->hostPids.count(pid) > 0;
    const bool xamlIsland = wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0
                         || wcscmp(className, L"XamlExplorerHostIslandWindow") == 0
                         || wcscmp(className, L"ControlCenterWindow") == 0;
    if (hostProcess || xamlIsland) {
        snap->windows.insert(hwnd);
    }
    return TRUE;
}

FlyoutWindowSnapshot TakeFlyoutSnapshot() {
    FlyoutWindowSnapshot snap;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry = {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, L"ShellExperienceHost.exe") == 0) {
                    snap.hostPids.insert(entry.th32ProcessID);
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    EnumWindows(CollectFlyoutWindows, reinterpret_cast<LPARAM>(&snap));
    return snap;
}

bool WaitForNewFlyoutWindow(const FlyoutWindowSnapshot& before, int timeoutMs) {
    for (int waited = 0; waited < timeoutMs; waited += 100) {
        FlyoutWindowSnapshot now = TakeFlyoutSnapshot();
        for (HWND hwnd : now.windows) {
            if (before.windows.count(hwnd) == 0) {
                return true;
            }
        }
        Sleep(100);
    }
    return false;
}

HRESULT InvokeFlyoutManager(FlyoutKind kind, FlyoutAction action,
                            const WinRtRect& anchor) {
    IExperienceManager* manager = GetFlyoutManager(kind);
    if (manager == nullptr) {
        return E_NOINTERFACE;
    }

    AllowShellForeground();

    if (action == FlyoutAction::Hide) {
        return manager->HideFlyout();
    }

    /* ShowFlyout vuole un puntatore a Windows.Foundation.Rect, non una RECT
     * Win32: l'interfaccia e' quella del Windows Runtime. */
    WinRtRect rect = anchor;
    return manager->ShowFlyout(&rect, nullptr);
}

} /* namespace */

/* ------------------------------------------------------------------------- */
/*  HStringRef                                                              */
/* ------------------------------------------------------------------------- */

bool detail::HStringRef::Create(const wchar_t* text) {
    if (text == nullptr || g_createStringReference == nullptr) {
        return false;
    }

    Reset();

    const size_t length = wcslen(text);
    if (length > 0xFFFFFFFFu) {
        return false;
    }

    /* La stringa sorgente e' sempre un letterale statico: vive quanto il
     * processo, quindi il riferimento resta valido. */
    return SUCCEEDED(g_createStringReference(text, static_cast<UINT32>(length),
                                             &m_header, &m_value));
}

void detail::HStringRef::Reset() {
    if (m_value != nullptr && g_deleteString != nullptr) {
        g_deleteString(m_value);
    }
    m_value = nullptr;
}

/* ------------------------------------------------------------------------- */
/*  API pubblica del modulo                                                 */
/* ------------------------------------------------------------------------- */

HRESULT ImmersiveFlyouts::Invoke(FlyoutKind kind, FlyoutAction action,
                                 const WinRtRect& anchor) {
    if (!IsSupported()) {
        return E_NOTIMPL;
    }

    if (action == FlyoutAction::Hide) {
        HRESULT hr = InvokeFlyoutManager(kind, action, anchor);
        if (SUCCEEDED(hr)) {
            return hr;
        }
        DropAll();
        return InvokeFlyoutManager(kind, action, anchor);
    }

    /* Mostrare: prima fotografia del host, poi chiamata, poi verifica che
     * qualcosa sia comparso davvero. Budget corto: mezzo secondo. Ogni
     * secondo di attesa prima del ripiego si sente come un clic rotto. */
    const FlyoutWindowSnapshot before = TakeFlyoutSnapshot();

    HRESULT hr = InvokeFlyoutManager(kind, action, anchor);
    if (SUCCEEDED(hr) && WaitForNewFlyoutWindow(before, 500)) {
        return S_OK;
    }

    /* Secondo tentativo con oggetti nuovi.
     *
     * La cache e' un'ottimizzazione: se Explorer e' stato riavviato (crash,
     * "Riavvia" in Gestione attivita', cambio di risoluzione) gli oggetti
     * che avevamo puntano a un server COM scomparso e ogni chiamata fallisce
     * per sempre. ExplorerPatcher non ha questo problema perche' ricrea
     * tutto a ogni invocazione; noi buttiamo via la cache e riproviamo una
     * volta sola, cosi' il primo clic dopo un riavvio della shell funziona
     * senza pagare la creazione a ogni clic. */
    /* Secondo tentativo con oggetti nuovi SOLO se a fallire e' stata la
     * chiamata COM (server riavviato, cache morta): se la chiamata riesce
     * ma non compare nulla, ricreare gli oggetti non cambia l'esito e
     * raddoppierebbe l'attesa. */
    if (FAILED(hr)) {
        DropAll();
        hr = InvokeFlyoutManager(kind, action, anchor);
        if (SUCCEEDED(hr) && WaitForNewFlyoutWindow(before, 500)) {
            return S_OK;
        }
    }

    /* Nessuna finestra del host: riferire successo sarebbe mentire, e il
     * clic resterebbe senza effetto. Il chiamante apre il suo ripiego. */
    return E_FAIL;
}

bool ImmersiveFlyouts::IsSupported() {
    return IsWindows10OrBetter() && LoadCombase();
}

void ImmersiveFlyouts::Shutdown() {
    DropAll();
}

} /* namespace w7t */
