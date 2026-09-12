// Win7Taskbar - Pinned Application Model (sorgente autoritativa dei pin)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// La scoperta e la normalizzazione dei pin vivono QUI (C++/Win32/Shell),
// non nella UI XAML: la UI riceve solo il modello gia' normalizzato.
//
// Sorgente: %APPDATA%\Microsoft\Internet Explorer\Quick Launch\User
// Pinned\TaskBar (la directory REALE dei pin della shell).
//
// Flusso (a ogni refresh): enumera .lnk -> valida -> risolve identita'
// -> deduplica -> modello ordinato -> evento al managed layer.
// I .lnk invalidi o duplicati restano sul disco: vengono solo ignorati.

#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

struct W7T_PinnedInfo;

namespace w7t {

struct PinnedApp {
    std::wstring identity;      // chiave di dedup/grouping
    std::wstring lnkPath;       // primo .lnk valido per questa identita'
    std::wstring target;        // target normalizzato (puo' essere vuoto)
    std::wstring displayName;   // nome mostrato (file .lnk senza est.)
};

class PinnedApps {
public:
    static PinnedApps& Instance();

    /* Prima enumerazione + watcher sulla cartella pin. */
    void Start();
    void Stop();

    /* Riesegue enumerate->validate->identity->dedup->model. */
    void Refresh();

    int32_t CopyTo(W7T_PinnedInfo* buffer, int32_t capacity);
    int32_t GetCount();

private:
    PinnedApps() = default;
    void WatcherLoop();

    std::mutex m_mutex;
    std::vector<PinnedApp> m_apps;

    std::thread m_watchThread;
    HANDLE m_watchDir = nullptr;
    HANDLE m_stopEvent = nullptr;
    std::atomic<bool> m_running{ false };
};

} /* namespace w7t */
