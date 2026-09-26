/*
 * Win7Taskbar - Core nativo - Sorveglianza eventi di sistema per la tray
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Nessuna llamada privata o inventata: Network List Manager,
 * RegNotifyChangeKeyValue e WTSRegisterSessionNotification sono API
 * pubbliche documentate. L'unica dichiarazione "fatta a mano" qui sotto e'
 * il layout pubblico di INetworkListManagerEvents (Advise/Unadvise), che
 * l'header MinGW di netlistmgr.h non espone: e' il contratto COM pubblicato
 * da Microsoft, non codice proprietario.
 */

#include "SystemEventsWatch.h"
#include "../include/RaiiWrappers.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <ocidl.h>
#include <wtsapi32.h>

#include <atomic>
#include <system_error>
#include <thread>

/* INetworkEvents e il gestore di connettivita' arrivano dall'header del
 * kit. La registrazione agli eventi segue il meccanismo pubblicamente
 * documentato da Microsoft per NLM (msdn: "Listening for Network Events"):
 * IConnectionPointContainer -> FindConnectionPoint(IID_
 * INetworkListManagerEvents) -> IConnectionPoint::Advise(sink). */
#include <netlistmgr.h>

namespace w7t {
namespace {

/* ------------------------------------------------------------------ */
/*  Registro                                                           */
/* ------------------------------------------------------------------ */

struct RegistryWatchState {
    std::atomic<bool> running{ false };
    raii::RegKeyHandle key;             // Explorer, aperto con KEY_NOTIFY
    raii::RegKeyHandle controlPanelKey; // NotifyIconSettings nel sottoalbero
    raii::GenericHandle event;
    std::thread       thread;
    HWND              notifyWnd = nullptr;
    UINT              notifyMsg = 0;
};

RegistryWatchState& RegistryWatch() {
    static RegistryWatchState state;
    return state;
}

/* ------------------------------------------------------------------ */
/*  Network List Manager                                               */
/* ------------------------------------------------------------------ */

class NetworkEventSink final : public INetworkEvents {
public:
    explicit NetworkEventSink(HWND targetWnd, UINT message)
        : m_wnd(targetWnd), m_msg(message) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_INetworkEvents) {
            *ppv = static_cast<INetworkEvents*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IUnknown) {
            *ppv = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = m_ref--;
        if (left == 0) {
            delete this;
        }
        return left;
    }

    // INetworkEvents: la sola cosa da fare e' svegliare il consumer.
    // Nulla di pesante qui: il PostMessage e' l'unico lavoro, e i quattro
    // eventi seguono lo stesso canale (chi riceve fara' una passata sola).
    HRESULT STDMETHODCALLTYPE NetworkAdded(GUID /*networkId*/) override {
        Notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NetworkDeleted(GUID /*networkId*/) override {
        Notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NetworkConnectivityChanged(
        GUID /*networkId*/, NLM_CONNECTIVITY /*newConnectivity*/) override {
        Notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE NetworkPropertyChanged(
        GUID /*networkId*/, NLM_NETWORK_PROPERTY_CHANGE /*flags*/) override {
        Notify();
        return S_OK;
    }

private:
    void Notify() {
        // Coalescenza a 2 s: NLM spara piu' eventi in rapida successione
        // durante una transizione (disconnesso -> identificato -> connesso).
        const DWORD now = GetTickCount();
        if (now - m_lastTick < 2000) {
            return;
        }
        m_lastTick = now;
        PostMessageW(m_wnd, m_msg, 0, 0);
    }

    std::atomic<ULONG> m_ref{ 1 };
    HWND               m_wnd;
    UINT               m_msg;
    DWORD              m_lastTick = 0;
};

struct NetworkWatchState {
    std::atomic<bool> running{ false };
    std::thread       thread;
};

NetworkWatchState& NetworkWatch() {
    static NetworkWatchState state;
    return state;
}

struct SessionWatchState {
    HWND registeredWnd = nullptr;
};

SessionWatchState& SessionWatch() {
    static SessionWatchState state;
    return state;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/*  Avvio / arresto                                                    */
/* ------------------------------------------------------------------ */

void SystemEventsWatch::StartRegistryWatch(HWND notifyWnd, UINT notifyMsg) {
    RegistryWatchState& s = RegistryWatch();
    if (s.running.exchange(true)) {
        return;   // gia' attivo
    }
    /* Un tentativo precedente può essere terminato perché il ramo di
     * registro non esisteva ancora: il thread è comunque joinable e va
     * raccolto prima di riutilizzare lo stato RAII. */
    if (s.thread.joinable()) {
        s.thread.join();
    }
    s.notifyWnd = notifyWnd;
    s.notifyMsg = notifyMsg;
    s.event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!s.event) {
        s.running.store(false);
        return;
    }

    try {
        s.thread = std::thread([] {
            RegistryWatchState& st = RegistryWatch();
            try {

                // Stessa chiave che legge la shell per EnableAutoTray e per le
                // impostazioni del cassetto (HKCU CurrentVersion Explorer sotto
                // HKEY_CURRENT_USER), aperta con KEY_NOTIFY.
                HKEY rawExplorer = nullptr;
                const bool explorerOpen =
                    RegOpenKeyExW(HKEY_CURRENT_USER,
                                  L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer",
                                  0, KEY_NOTIFY, &rawExplorer) == ERROR_SUCCESS;
                if (explorerOpen) {
                    st.key.reset(rawExplorer);
                }
                /* NotifyIconSettings e' sotto Control Panel e non sotto Explorer.
                 * Si osserva il ramo padre con bWatchSubtree=TRUE, così la
                 * modifica di ogni IsPromoted riattiva la stessa riconciliazione
                 * senza inventare un timer o scrivere nel registro. */
                HKEY rawControlPanel = nullptr;
                const bool controlPanelOpen =
                    RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel", 0,
                                  KEY_NOTIFY, &rawControlPanel) == ERROR_SUCCESS;
                if (controlPanelOpen) {
                    st.controlPanelKey.reset(rawControlPanel);
                }
                if (!explorerOpen && !controlPanelOpen) {
                    st.running.store(false);
                    return;
                }

                while (st.running.load()) {
                    // Richiesta una-sola-volta: dopo ogni notifica va riarmata.
                    if (explorerOpen && st.key) {
                        RegNotifyChangeKeyValue(st.key.get(), FALSE,
                                                REG_NOTIFY_CHANGE_LAST_SET,
                                                st.event.get(), TRUE);
                    }
                    if (controlPanelOpen && st.controlPanelKey) {
                        RegNotifyChangeKeyValue(st.controlPanelKey.get(), TRUE,
                                                REG_NOTIFY_CHANGE_LAST_SET,
                                                st.event.get(), TRUE);
                    }
                    const DWORD wait = WaitForSingleObject(st.event.get(), 1000);
                    if (wait == WAIT_OBJECT_0) {
                        ResetEvent(st.event.get());
                        if (st.notifyMsg != 0 && st.notifyWnd != nullptr) {
                            PostMessageW(st.notifyWnd, st.notifyMsg, 0, 0);
                        }
                    }
                }
                st.key.reset();
                st.controlPanelKey.reset();
            } catch (...) {
                st.running.store(false);
                st.key.reset();
                st.controlPanelKey.reset();
            }
        });
    } catch (const std::system_error&) {
        s.running.store(false);
        return;
    }
}

void SystemEventsWatch::StopRegistryWatch() {
    RegistryWatchState& s = RegistryWatch();
    const bool wasRunning = s.running.exchange(false);
    if (!wasRunning && !s.thread.joinable()) {
        s.event.reset();
        return;
    }
    if (s.event) {
        SetEvent(s.event.get());
    }
    if (s.thread.joinable()) {
        s.thread.join();
    }
    s.event.reset();
}

void SystemEventsWatch::StartNetworkWatch(HWND notifyWnd, UINT notifyMsg) {
    NetworkWatchState& s = NetworkWatch();
    if (s.running.exchange(true)) {
        return;
    }
    /* Anche un thread che ha fallito CoInitializeEx termina normalmente:
     * prima di assegnare un nuovo std::thread bisogna raccogliere quello
     * precedente, altrimenti std::thread termina il processo. */
    if (s.thread.joinable()) {
        s.thread.join();
    }

    try {
        s.thread = std::thread([notifyWnd, notifyMsg] {
            NetworkWatchState& st = NetworkWatch();
            try {
                /* STA con message pump: il connection point di NLM arriva dal
                 * LocalServer del Network List Service (svchost), quindi le
                 * chiamate in entrata alla sink vengono marshallsate
                 * nell'apartment: un single-threaded apartment senza pump non le
                 * consegnerebbe mai. Il pump qui e' esplicito (MsgWait + Peek), il
                 * lavoro della sink resta un PostMessage da un soffio. */
                raii::ComInitializer com(COINIT_APARTMENTTHREADED);
                if (!com.succeeded()) {
                    st.running.store(false);
                    return;
                }

                raii::ComPtr<INetworkEvents> sink(
                    new NetworkEventSink(notifyWnd, notifyMsg));
                raii::ComPtr<INetworkListManager> manager;
                raii::ComPtr<IConnectionPointContainer> cpc;
                raii::ComPtr<IConnectionPoint> point;
                DWORD cookie = 0;
                bool advising = false;
                const auto unadvise = raii::on_scope_exit([&]() noexcept {
                    if (advising && point) {
                        (void)point->Unadvise(cookie);
                    }
                });

                HRESULT hr = CoCreateInstance(
                    CLSID_NetworkListManager, nullptr,
                    CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                    IID_INetworkListManager,
                    reinterpret_cast<void**>(manager.Put()));
                if (SUCCEEDED(hr)) {
                    hr = manager->QueryInterface(
                        IID_IConnectionPointContainer,
                        reinterpret_cast<void**>(cpc.Put()));
                    if (SUCCEEDED(hr)) {
                        // Il connection point degli eventi NLM e' pubblicato con
                        // l'IID della interfaccia-sorgente INetworkListManagerEvents.
                        hr = cpc->FindConnectionPoint(
                            IID_INetworkListManagerEvents, point.Put());
                        if (SUCCEEDED(hr) && point) {
                            advising = SUCCEEDED(point->Advise(
                                static_cast<IUnknown*>(sink.Get()), &cookie));
                        }
                    }
                }

                while (st.running.load()) {
                    MsgWaitForMultipleObjectsEx(0, nullptr, 250, QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE);
                    MSG msg;
                    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }

                /* ComPtr, la guardia di Unadvise e ComInitializer rilasciano
                 * le risorse anche se una chiamata COM o una nuova allocazione
                 * solleva un'eccezione. */
            } catch (...) {
                st.running.store(false);
            }
        });
    } catch (const std::system_error&) {
        s.running.store(false);
        return;
    }
}

void SystemEventsWatch::StopNetworkWatch() {
    NetworkWatchState& s = NetworkWatch();
    const bool wasRunning = s.running.exchange(false);
    if (!wasRunning && !s.thread.joinable()) {
        return;
    }
    if (s.thread.joinable()) {
        s.thread.join();
    }
}

void SystemEventsWatch::StartSessionWatch(HWND notifyWnd) {
    /* WM_WTSSESSION_CHANGE arriva solo se registrati. Notify per questa
     * sessione: lock/unlock/remote connect/disconnect. */
    SessionWatchState& s = SessionWatch();
    if (s.registeredWnd != nullptr) {
        WTSUnRegisterSessionNotification(s.registeredWnd);
        s.registeredWnd = nullptr;
    }
    if (notifyWnd != nullptr &&
        WTSRegisterSessionNotification(notifyWnd, NOTIFY_FOR_THIS_SESSION)) {
        s.registeredWnd = notifyWnd;
    }
}

void SystemEventsWatch::StopSessionWatch() {
    SessionWatchState& s = SessionWatch();
    if (s.registeredWnd != nullptr) {
        WTSUnRegisterSessionNotification(s.registeredWnd);
        s.registeredWnd = nullptr;
    }
}

} /* namespace w7t */
