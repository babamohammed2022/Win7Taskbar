// Win7Taskbar - Jump List stile Windows 7 per i pulsanti della barra
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Solo API pubbliche documentate: IApplicationDocumentLists (lista
// Recenti dell'app) per i dati reali, IShellLink per creare il pin nella
// cartella REALE dei pin della shell, DWM per il bordo Aero condiviso.

#include "JumpListWindow.h"
#include "FlyoutLauncher.h"
#include "SehGuard.h"
#include "Common.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <dwmapi.h>
#include <cstring>
#include <algorithm>

namespace w7t {

namespace {
constexpr wchar_t kClassName[] = L"W7T_JumpList";
/* v2.40: proporzioni riviste per aderire alla Jump List di Windows 7:
 * piu' larga e con righe piu' alte (icona app 32 px, voci ariose). */
constexpr int kWidth = 300;
constexpr int kRowApp = 48;
constexpr int kRowPin = 34;
constexpr int kRowRecent = 28;

/* RAII generico per interfacce COM: Release() garantito anche sui
 * percorsi di errore (stesso principio di IconGuard per le HICON). */
template <typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    explicit ComPtr(T* ptr) : p(ptr) {}
    ~ComPtr() { if (p) p->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; }
        return *this;
    }
    T* operator->() const { return p; }
    T** operator&() { return &p; }
    explicit operator bool() const { return p != nullptr; }
};

struct JumpStr {
    const wchar_t* recent;   /* intestazione "Recenti" */
    const wchar_t* pin;
    const wchar_t* unpin;
};
const JumpStr& Str(int lang) {
    static const JumpStr kIt = {
        L"Recenti",
        L"Fissa questo programma alla barra delle applicazioni",
        L"Rimuovi questo programma dalla barra delle applicazioni" };
    static const JumpStr kEn = {
        L"Recent",
        L"Pin this program to the taskbar",
        L"Unpin this program from the taskbar" };
    static const JumpStr kEs = {
        L"Reciente",
        L"Anclar este programa a la barra de tareas",
        L"Desanclar este programa de la barra de tareas" };
    static const JumpStr kFr = {
        L"Récent",
        L"Épingler ce programme à la barre des tâches",
        L"Détacher ce programme de la barre des tâches" };
    static const JumpStr kDe = {
        L"Zuletzt verwendet",
        L"Dieses Programm an die Taskleiste anheften",
        L"Dieses Programm von der Taskleiste lösen" };
    static const JumpStr kPt = {
        L"Recente",
        L"Fixar este programa na barra de tarefas",
        L"Desafixar este programa da barra de tarefas" };
    static const JumpStr kPl = {
        L"Ostatnie",
        L"Przypnij ten program do paska zadań",
        L"Odepnij ten program od paska zadań" };
    static const JumpStr kRu = {
        L"Недавние",
        L"Закрепить эту программу на панели задач",
        L"Открепить эту программу от панели задач" };
    static const JumpStr kJa = {
        L"最近使ったもの",
        L"このプログラムをタスクバーに表示する",
        L"このプログラムをタスクバーに表示しない" };
    static const JumpStr kZh = {
        L"最近使用",
        L"将此程序固定到任务栏",
        L"将此程序从任务栏取消固定" };
    /* v3.6: l'arabo mancava del tutto e cadeva sull'italiano (indice 10
     * dell'elenco unico, Strings.cpp). */
    static const JumpStr kAr = {
        L"أخيرة",
        L"ثبت هذا البرنامج إلى شريط المهام",
        L"إلغاء تثبيت هذا البرنامج من شريط المهام" };
    switch (lang) {
        case 1: return kEn; case 2: return kEs; case 3: return kFr;
        case 4: return kDe; case 5: return kPt; case 6: return kPl;
        case 7: return kRu; case 8: return kJa; case 9: return kZh;
        case 10: return kAr;
        default: return kIt;
    }
}

/* Cartella REALE dei pin della shell (la stessa letta da PinnedApps). */
std::wstring PinnedFolder() {
    wchar_t base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, base)))
        return std::wstring();
    return std::wstring(base) +
        L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
}
} // namespace

/* ------------------------------------------------------------------ */
/*  Lettura VERA della jump list (IApplicationDocumentLists, Recenti).  */
/*  L'API pubblica per leggere i documenti recenti/frequenti di un'app  */
/*  e' IApplicationDocumentLists::GetList(ADLT_RECENT,...), NON          */
/*  ICustomDestinationList (che non espone alcun GetList).              */
/* ------------------------------------------------------------------ */
namespace {
/* Estrae il percorso da un elemento della lista: di norma e' un
 * IShellLink verso il documento; se non lo e' si tenta IShellItem.
 * Ritorna true se ha ricavato un path non vuoto. */
bool ExtractDocPath(IUnknown* unk, std::wstring& outPath) {
    if (!unk) return false;
    ComPtr<IShellLinkW> lnk;
    if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&lnk))) && lnk) {
        wchar_t buf[MAX_PATH]{};
        if (SUCCEEDED(lnk->GetPath(buf, MAX_PATH, nullptr, SLGP_RAWPATH))
            && buf[0]) {
            outPath = buf;
            return true;
        }
    }
    ComPtr<IShellItem> si;
    if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&si))) && si) {
        wchar_t* disp = nullptr;
        if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &disp)) && disp) {
            outPath = disp;
            CoTaskMemFree(disp);
            return !outPath.empty();
        }
    }
    return false;
}
} // namespace

std::vector<JumpListItem> JumpListWindow::ReadRecentItems(
        const wchar_t* appUserModelId) {
    std::vector<JumpListItem> result;
    W7T_SEH_TRY
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool coUn = SUCCEEDED(hrCo);
        try {
            ComPtr<IApplicationDocumentLists> adl;
            if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationDocumentLists,
                    nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&adl)))
                && adl) {
                if (appUserModelId && *appUserModelId) {
                    adl->SetAppID(appUserModelId);
                }
                /* API documentata per i Recenti registrati dall'app. */
                ComPtr<IObjectArray> items;
                if (SUCCEEDED(adl->GetList(ADLT_RECENT, 10,
                        IID_PPV_ARGS(&items))) && items) {
                    UINT count = 0;
                    items->GetCount(&count);
                    for (UINT i = 0; i < count && result.size() < 10; ++i) {
                        /* un elemento malformato non ferma gli altri */
                        ComPtr<IUnknown> unk;
                        if (FAILED(items->GetAt(i, IID_PPV_ARGS(&unk))) ||
                            !unk) continue;
                        JumpListItem entry;
                        if (!ExtractDocPath(unk.p, entry.path)) continue;
                        size_t slash = entry.path.find_last_of(L"\\/");
                        entry.displayName =
                            (slash == std::wstring::npos)
                                ? entry.path
                                : entry.path.substr(slash + 1);
                        size_t dot = entry.displayName.find_last_of(L'.');
                        if (dot != std::wstring::npos && dot > 0)
                            entry.displayName =
                                entry.displayName.substr(0, dot);
                        result.push_back(std::move(entry));
                    }
                }
            }
        } catch (...) { result.clear(); }
        if (coUn) CoUninitialize();
    W7T_SEH_CATCH
    W7T_SEH_END
    return result;
}

JumpListWindow& JumpListWindow::Instance() {
    static JumpListWindow instance;
    return instance;
}

void JumpListWindow::RegisterClassOnce() {
    if (m_classRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    m_classRegistered = true;
}

void JumpListWindow::Layout() {
    int y = 8;
    if (kEnableRecentSection && !m_recent.empty()) {
        y += 20 + static_cast<int>(m_recent.size()) * kRowRecent + 6;
    }
    m_recentTop = y;
    m_totalH = y + kRowApp + 4 + kRowPin + 8;
}

RECT JumpListWindow::AppRow() const {
    return RECT{ 8, m_recentTop, kWidth - 8, m_recentTop + kRowApp };
}
RECT JumpListWindow::PinRow() const {
    const int y = m_recentTop + kRowApp + 4;
    return RECT{ 8, y, kWidth - 8, y + kRowPin };
}

void JumpListWindow::Show(const RECT& buttonRect,
                          const std::wstring& title,
                          const std::wstring& launchPath,
                          const std::wstring& pinnedLnkPath,
                          bool isPinned,
                          const uint32_t* iconArgb, int iconW, int iconH,
                          int lang) {
    W7T_SEH_TRY
        RegisterClassOnce();
        m_title = title;
        m_launchPath = launchPath;
        m_pinnedLnk = pinnedLnkPath;
        m_pinned = isPinned;
        m_lang = (lang >= 0 && lang <= 9) ? lang : 0;

        if (m_appIcon) { DeleteObject(m_appIcon); m_appIcon = nullptr; }
        if (iconArgb && iconW > 0 && iconH > 0) {
            std::vector<uint32_t> px(iconArgb,
                iconArgb + static_cast<size_t>(iconW) * iconH);
            m_appIcon = MakeHBitmapFromArgb(px, iconW, iconH);
        }

        m_recent.clear();
        if (kEnableRecentSection) {
            /* AUMID non noto in v1: lettura generica; mai inventare. */
            m_recent = ReadRecentItems(nullptr);
        }
        Layout();

        if (m_hwnd == nullptr) {
            m_hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                kClassName, L"", WS_POPUP,
                0, 0, kWidth, m_totalH,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m_hwnd == nullptr) return;
            ApplyAeroFlyoutStyle(m_hwnd);   /* bordo Aero condiviso */
        }
        /* Come Windows 7: sopra il pulsante, allineata a sinistra. */
        int x = buttonRect.left;
        int y = buttonRect.top - m_totalH - 4;
        int sw = GetSystemMetrics(SM_CXSCREEN);
        if (x + kWidth > sw - 4) x = sw - kWidth - 4;
        if (x < 4) x = 4;
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, kWidth, m_totalH,
                     SWP_SHOWWINDOW);
        SetForegroundWindow(m_hwnd);
        InvalidateRect(m_hwnd, nullptr, TRUE);
    W7T_SEH_CATCH
    W7T_SEH_END
}

void JumpListWindow::Hide() {
    W7T_SEH_TRY
        if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
    W7T_SEH_CATCH
    W7T_SEH_END
}

bool JumpListWindow::IsVisible() const {
    return m_hwnd != nullptr && IsWindowVisible(m_hwnd);
}

void JumpListWindow::LaunchApp() {
    W7T_SEH_TRY
        if (!m_launchPath.empty()) {
            ShellExecuteW(nullptr, L"open", m_launchPath.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
    W7T_SEH_CATCH
    W7T_SEH_END
    Hide();
}

void JumpListWindow::PerformPinOrUnpin() {
    W7T_SEH_TRY
        if (m_pinned) {
            /* Unpin: cancella il .lnk reale; il watcher di PinnedApps
             * aggiorna il modello da solo. */
            if (!m_pinnedLnk.empty()) {
                DeleteFileW(m_pinnedLnk.c_str());
            }
        } else {
            /* Pin: crea il .lnk nella cartella reale dei pin. */
            const std::wstring dir = PinnedFolder();
            if (!dir.empty() && !m_launchPath.empty()) {
                CreateDirectoryW(dir.c_str(), nullptr); /* ignora esito */
                std::wstring name = m_title;
                for (wchar_t& c : name) {
                    if (c == L'/' || c == L'\\' || c == L':' || c == L'*' ||
                        c == L'?' || c == L'"' || c == L'<' || c == L'>' ||
                        c == L'|') c = L'_';
                }
                if (name.empty()) name = L"App";
                const std::wstring lnk = dir + L"\\" + name + L".lnk";

                HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                bool coUn = SUCCEEDED(hrCo);
                try {
                    ComPtr<IShellLinkW> link;
                    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) && link) {
                        link->SetPath(m_launchPath.c_str());
                        link->SetIconLocation(m_launchPath.c_str(), 0);
                        ComPtr<IPersistFile> pf;
                        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))
                            && pf) {
                            pf->Save(lnk.c_str(), TRUE);
                        }
                    }
                } catch (...) { /* mai propagare */ }
                if (coUn) CoUninitialize();
            }
        }
    W7T_SEH_CATCH
    W7T_SEH_END
    Hide();
}

void JumpListWindow::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(0xF2, 0xF6, 0xFB));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    HFONT font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT fontBold = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, font);
    const JumpStr& S = Str(m_lang);

    auto hline = [&](int y) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xC9, 0xD6, 0xE6));
        HPEN op = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, 10, y, nullptr);
        LineTo(hdc, kWidth - 10, y);
        SelectObject(hdc, op);
        DeleteObject(pen);
    };

    int y = 8;
    if (kEnableRecentSection && !m_recent.empty()) {
        HGDIOBJ ob = SelectObject(hdc, fontBold);
        SetTextColor(hdc, RGB(0x40, 0x58, 0x78));
        RECT hr{ 12, y, kWidth - 12, y + 18 };
        DrawTextW(hdc, S.recent, -1, &hr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        SelectObject(hdc, ob);
        y += 20;
        for (const JumpListItem& it : m_recent) {
            /* icona REALE del file (non generica): SHGetFileInfo */
            SHFILEINFOW sfi{};
            W7T_SEH_TRY
            if (SHGetFileInfoW(it.path.c_str(), 0, &sfi, sizeof(sfi),
                               SHGFI_ICON | SHGFI_SMALLICON)) {
                if (sfi.hIcon) {
                    DrawIconEx(hdc, 14, y + (kRowRecent - 16) / 2, sfi.hIcon,
                               16, 16, 0, nullptr, DI_NORMAL);
                    DestroyIcon(sfi.hIcon);
                }
            }
            W7T_SEH_CATCH
            W7T_SEH_END
            SetTextColor(hdc, RGB(0x1E, 0x1E, 0x1E));
            RECT rr{ 36, y, kWidth - 12, y + kRowRecent };
            DrawTextW(hdc, it.displayName.c_str(), -1, &rr,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            y += kRowRecent;
        }
        y += 6;
        hline(y - 3);
    }

    /* riga applicazione: icona grande + nome, cliccabile */
    RECT ar = AppRow();
    if (m_appIcon) {
        DrawBitmapScaled(hdc, m_appIcon, 32, 32, 14, ar.top + (kRowApp - 32) / 2);
    }
    SetTextColor(hdc, RGB(0x1E, 0x1E, 0x1E));
    RECT tr{ 54, ar.top, kWidth - 12, ar.bottom };
    DrawTextW(hdc, m_title.c_str(), -1, &tr,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    hline(ar.bottom + 2);

    /* riga pin/unpin */
    RECT pr = PinRow();
    SetTextColor(hdc, RGB(0x1E, 0x6F, 0xC9));
    RECT ptr{ 14, pr.top, kWidth - 12, pr.bottom };
    DrawTextW(hdc, m_pinned ? S.unpin : S.pin, -1, &ptr,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
    DeleteObject(fontBold);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK JumpListWindow::WndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    W7T_SEH_TRY
        switch (msg) {
            case WM_PAINT:
                Instance().OnPaint(hwnd);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_LBUTTONUP: {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                RECT appRow = Instance().AppRow();
                RECT pinRow = Instance().PinRow();
                if (PtInRect(&appRow, pt)) {
                    Instance().LaunchApp();
                } else if (PtInRect(&pinRow, pt)) {
                    Instance().PerformPinOrUnpin();
                }
                return 0;
            }
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) Instance().Hide();
                return 0;
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) Instance().Hide();
                return 0;
            case WM_NCDESTROY:
                Instance().m_hwnd = nullptr;
                return 0;
        }
    W7T_SEH_CATCH
    W7T_SEH_END
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace w7t
