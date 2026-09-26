/*
 * Win7Taskbar - controllo ToolbarWindow32 opzionale per compatibilita'
 *
 * Questo oggetto e' una finestra spy reale, non una fonte di stato: riceve
 * solo snapshot gia' normalizzati dal TrayService e non ascolta
 * Shell_NotifyIcon, WM_COPYDATA o UI Automation.
 */
#pragma once

#include "Common.h"
#include <windows.h>
#include <commctrl.h>
#include <map>
#include <mutex>
#include <vector>

namespace w7t {

struct LegacyToolbarShimItem {
    uint64_t ownerHwnd = 0;
    uint32_t uid = 0;
    uint32_t command = 0;
    bool hidden = false;
    ArgbBitmap bitmap;
};

class LegacyToolbarShim {
public:
    static LegacyToolbarShim& Instance();

    /* Crea il vero ToolbarWindow32 come figlio del SysPager gia' creato dal
     * modello TrayToolbar. Il controllo resta nascosto di proposito: il
     * percorso WPF continua a disegnare la barra e lo shim non deve
     * interferire con AppBar, overflow reale, UIA o WM_COPYDATA. */
    bool Create(HWND pagerParent, HINSTANCE instance);
    void Destroy() noexcept;
    bool IsCreated() const noexcept;

    /* Allinea il controllo alla fotografia del modello: nessuna lettura
     * autonoma e nessun parsing di messaggi della shell. */
    void Sync(const std::vector<LegacyToolbarShimItem>& items);

private:
    LegacyToolbarShim() = default;
    ~LegacyToolbarShim();
    LegacyToolbarShim(const LegacyToolbarShim&) = delete;
    LegacyToolbarShim& operator=(const LegacyToolbarShim&) = delete;

    int EnsureImage(uint32_t command);

    mutable std::mutex m_mutex;
    HWND m_toolbar = nullptr;
    HWND m_parent = nullptr;
    HIMAGELIST m_images = nullptr;
    std::map<uint32_t, int> m_imageByCommand;
    std::map<uint32_t, LegacyToolbarShimItem> m_itemsByCommand;
    int m_iconSize = 16;
};

} /* namespace w7t */
