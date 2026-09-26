/*
 * Win7Taskbar - ToolbarWindow32 spy opt-in
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 */

#include "LegacyToolbarShim.h"
#include "../include/RaiiWrappers.h"
#include "ScopeGuards.h"

#include <algorithm>
#include <cstring>

namespace w7t {
namespace {

void EnsureCommonControls() noexcept {
    static INITCOMMONCONTROLSEX controls = {
        sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES
    };
    static bool initialized = false;
    if (!initialized) {
        InitCommonControlsEx(&controls);
        initialized = true;
    }
}

} /* namespace */

LegacyToolbarShim& LegacyToolbarShim::Instance() {
    static LegacyToolbarShim instance;
    return instance;
}

LegacyToolbarShim::~LegacyToolbarShim() {
    Destroy();
}

bool LegacyToolbarShim::IsCreated() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_toolbar != nullptr && IsWindow(m_toolbar) != FALSE;
}

bool LegacyToolbarShim::Create(HWND pagerParent, HINSTANCE instance) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar != nullptr) {
        return true;
    }
    if (pagerParent == nullptr || !IsWindow(pagerParent)) {
        return false;
    }

    EnsureCommonControls();
    m_parent = pagerParent;
    m_iconSize = GetSystemMetrics(SM_CXSMICON);
    if (m_iconSize < 16) {
        m_iconSize = 16;
    }

    /* Il guard garantisce che anche un fallimento di ImageList o di una
     * successiva inizializzazione lasci il parent senza una finestra orfana. */
    auto cleanup = raii::on_scope_exit([this]() noexcept {
        if (m_toolbar != nullptr) {
            DestroyWindow(m_toolbar);
            m_toolbar = nullptr;
        }
        if (m_images != nullptr) {
            ImageList_Destroy(m_images);
            m_images = nullptr;
        }
        m_parent = nullptr;
        m_imageByCommand.clear();
        m_itemsByCommand.clear();
    });

    m_toolbar = CreateWindowExW(
        WS_EX_NOACTIVATE,
        TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
            CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
        0, 0, 0, 0, pagerParent, nullptr, instance, nullptr);
    if (m_toolbar == nullptr) {
        return false;
    }
    /* TB_BUTTONSTRUCTSIZE configura la dimensione della struttura; il
     * controllo non documenta un valore di ritorno utile per il successo. */
    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

    m_images = ImageList_Create(m_iconSize, m_iconSize, ILC_COLOR32, 4, 16);
    if (m_images == nullptr) {
        return false;
    }
    SendMessageW(m_toolbar, TB_SETIMAGELIST, 0,
                 reinterpret_cast<LPARAM>(m_images));

    cleanup.dismiss();
    return true;
}

void LegacyToolbarShim::Destroy() noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar != nullptr) {
        if (m_images != nullptr) {
            SendMessageW(m_toolbar, TB_SETIMAGELIST, 0, 0);
        }
        DestroyWindow(m_toolbar);
        m_toolbar = nullptr;
    }
    if (m_images != nullptr) {
        ImageList_Destroy(m_images);
        m_images = nullptr;
    }
    m_parent = nullptr;
    m_imageByCommand.clear();
    m_itemsByCommand.clear();
}

int LegacyToolbarShim::EnsureImage(uint32_t command) {
    auto known = m_imageByCommand.find(command);
    if (known != m_imageByCommand.end()) {
        return known->second;
    }
    if (m_images == nullptr) {
        return -1;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = m_iconSize;
    info.bmiHeader.biHeight = -m_iconSize;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    raii::BitmapHandle bitmap(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                                &bits, nullptr, 0));
    if (!bitmap || bits == nullptr) {
        return -1;
    }
    memset(bits, 0, static_cast<size_t>(m_iconSize) *
                     static_cast<size_t>(m_iconSize) * 4u);
    const int image = ImageList_Add(m_images, bitmap.get(), nullptr);
    if (image >= 0) {
        m_imageByCommand[command] = image;
    }
    return image;
}

void LegacyToolbarShim::Sync(const std::vector<LegacyToolbarShimItem>& items) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_toolbar == nullptr || !IsWindow(m_toolbar)) {
        return;
    }

    std::vector<uint32_t> desired;
    desired.reserve(items.size());
    std::map<uint32_t, LegacyToolbarShimItem> desiredByCommand;
    for (const LegacyToolbarShimItem& item : items) {
        if (item.command == 0 || desiredByCommand.count(item.command) != 0) {
            continue;
        }
        desired.push_back(item.command);
        desiredByCommand.emplace(item.command, item);
    }

    /* Legge lo stato del controllo vero, poi usa TB_DELETEBUTTON per ciò che
     * il modello non contiene più. */
    const int count = static_cast<int>(
        SendMessageW(m_toolbar, TB_BUTTONCOUNT, 0, 0));
    std::vector<uint32_t> current(static_cast<size_t>((std::max)(count, 0)), 0);
    for (int index = 0; index < count; ++index) {
        TBBUTTON button = {};
        if (SendMessageW(m_toolbar, TB_GETBUTTON, index,
                         reinterpret_cast<LPARAM>(&button)) != FALSE) {
            current[static_cast<size_t>(index)] =
                static_cast<uint32_t>(button.idCommand);
        }
    }
    for (int index = count - 1; index >= 0; --index) {
        if (desiredByCommand.count(current[static_cast<size_t>(index)]) == 0) {
            SendMessageW(m_toolbar, TB_DELETEBUTTON,
                         static_cast<WPARAM>(index), 0);
        }
    }

    /* TB_ADDBUTTONSW è intenzionale: il controllo è il solo mirror e ogni
     * idCommand resta il command UID assegnato alla TrayIconKey. */
    for (size_t wanted = 0; wanted < desired.size(); ++wanted) {
        const uint32_t command = desired[wanted];
        int found = -1;
        const int nowCount = static_cast<int>(
            SendMessageW(m_toolbar, TB_BUTTONCOUNT, 0, 0));
        for (int index = 0; index < nowCount; ++index) {
            TBBUTTON button = {};
            if (SendMessageW(m_toolbar, TB_GETBUTTON, index,
                             reinterpret_cast<LPARAM>(&button)) != FALSE &&
                static_cast<uint32_t>(button.idCommand) == command) {
                found = index;
                break;
            }
        }
        if (found < 0) {
            const int image = EnsureImage(command);
            TBBUTTON button = {};
            button.iBitmap = image >= 0 ? image : I_IMAGECALLBACK;
            button.idCommand = static_cast<INT_PTR>(command);
            button.fsState = TBSTATE_ENABLED;
            button.fsStyle = BTNS_BUTTON;
            button.iString = -1;
            if (SendMessageW(m_toolbar, TB_ADDBUTTONSW, 1,
                             reinterpret_cast<LPARAM>(&button)) == FALSE) {
                continue;
            }
            found = nowCount;
        }
        if (found != static_cast<int>(wanted)) {
            SendMessageW(m_toolbar, TB_MOVEBUTTON, found,
                         static_cast<LPARAM>(wanted));
        }
    }

    /* Aggiorna i bitmap senza introdurre una seconda sorgente di stato. */
    for (const auto& pair : desiredByCommand) {
        const int image = EnsureImage(pair.first);
        if (image < 0 || pair.second.bitmap.empty()) {
            continue;
        }
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = m_iconSize;
        info.bmiHeader.biHeight = -m_iconSize;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        raii::BitmapHandle bitmap(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                                    &bits, nullptr, 0));
        if (!bitmap || bits == nullptr) {
            continue;
        }
        memset(bits, 0, static_cast<size_t>(m_iconSize) *
                         static_cast<size_t>(m_iconSize) * 4u);
        const int copyW = (std::min)(m_iconSize, pair.second.bitmap.width);
        const int copyH = (std::min)(m_iconSize, pair.second.bitmap.height);
        const int sourceX = ((std::max)(0, pair.second.bitmap.width - copyW)) / 2;
        const int sourceY = ((std::max)(0, pair.second.bitmap.height - copyH)) / 2;
        const int targetX = ((m_iconSize - copyW) / 2);
        const int targetY = ((m_iconSize - copyH) / 2);
        auto* target = static_cast<uint8_t*>(bits);
        for (int y = 0; y < copyH; ++y) {
            const uint8_t* source = pair.second.bitmap.pixels.data() +
                static_cast<size_t>(sourceY + y) *
                    static_cast<size_t>(pair.second.bitmap.width) * 4u +
                static_cast<size_t>(sourceX) * 4u;
            memcpy(target + (static_cast<size_t>(targetY + y) *
                             static_cast<size_t>(m_iconSize) +
                             static_cast<size_t>(targetX)) * 4u,
                   source, static_cast<size_t>(copyW) * 4u);
        }
        ImageList_Replace(m_images, image, bitmap.get(), nullptr);
    }

    m_itemsByCommand.swap(desiredByCommand);

    /* TBSTATE_HIDDEN mantiene la semantica del modello senza rimuovere la
     * voce dal mirror. La fonte è ancora lo snapshot di TrayService, non il
     * controllo e non Explorer. */
    for (const auto& pair : m_itemsByCommand) {
        int index = -1;
        const int finalCount = static_cast<int>(
            SendMessageW(m_toolbar, TB_BUTTONCOUNT, 0, 0));
        for (int candidate = 0; candidate < finalCount; ++candidate) {
            TBBUTTON button = {};
            if (SendMessageW(m_toolbar, TB_GETBUTTON, candidate,
                             reinterpret_cast<LPARAM>(&button)) != FALSE &&
                static_cast<uint32_t>(button.idCommand) == pair.first) {
                index = candidate;
                break;
            }
        }
        if (index >= 0) {
            const BYTE state = static_cast<BYTE>(
                TBSTATE_ENABLED |
                (pair.second.hidden ? TBSTATE_HIDDEN : 0));
            SendMessageW(m_toolbar, TB_SETSTATE,
                         static_cast<WPARAM>(pair.first),
                         static_cast<LPARAM>(state));
        }
    }

    /* Anche uno snapshot vuoto deve ridimensionare il controllo reale. */
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

} /* namespace w7t */
