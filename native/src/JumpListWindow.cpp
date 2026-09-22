// Win7Taskbar - Windows 7 style Jump List for taskbar buttons
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Only public, documented Shell APIs: IApplicationDocumentLists for the
// application's real Recent/Frequent destinations, the window/shortcut
// property stores for the AppUserModelID, the Start_JumpListItems policy,
// IShellLink for the taskbar pin, and the shared DWM flyout border. The
// PINNED (custom) section is intentionally NOT shown: Windows 7 and later
// expose no public API that reads or removes it (MSDN: the pinned items
// "cannot be removed programmatically; only the user can remove them";
// the Shell reads its own store directly - see docs/JUMPLIST-RE-
// VERIFICATION.md). Nothing is ever invented. Every COM/Shell call
// sits behind an explicit failure path that logs and degrades to "no jump
// list" (or to the section staying hidden); hard faults (dead network
// paths, malformed destination items) stay inside the portable SEH barrier
// the core uses elsewhere. Resources with ownership semantics (HBITMAP,
// HICON, COM interfaces, PROPVARIANTs) are owned by RAII guards, so no
// early return or fault can leak them.
//
// Coordinate contract with the managed side: every RECT and POINT crossing
// Open/SetHover/ActivateRow is a SCREEN PHYSICAL PIXEL value (the space of
// SetWindowPos/GetCursorPos and of WPF PointToScreen on a per-monitor-DPI
// process). Geometry constants are 96-DPI reference values scaled by the
// DPI of the monitor that owns the taskbar button. The compact application
// row uses the compact 21 px application icon and Segoe UI metrics; all
// offsets go through Sc(), so there are no unscaled magic numbers.

#include "JumpListWindow.h"
#include "PinVerbs.h"
#include "FlyoutLauncher.h"
#include "SehGuard.h"
#include "ScopeGuards.h"
#include "Common.h"
#include "WindowManager.h"
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <dwmapi.h>
/* TOOLTIPS_CLASS / TTTOOLINFOW / TTM_* / TTS_ALWAYSTIP: declared in
 * commctrl.h, which windows.h does not pull in under
 * WIN32_LEAN_AND_MEAN (the project build definition) - include it
 * explicitly where it is used. */
#include <commctrl.h>
#include <cstring>
#include <algorithm>
#include <mutex>

namespace w7t {

namespace {

constexpr wchar_t kClassName[] = L"W7T_JumpList";
constexpr UINT kDismissOutsideMessage = WM_APP + 0x177;

/* --- 96-DPI reference geometry (scaled by JumpListWindow::Sc) --------- */
constexpr int kWidth96      = 280;
constexpr int kRowApp96     = 36;
constexpr int kRowClose96   = 26;
constexpr int kRowPin96     = 28;
constexpr int kRowDoc96     = 24;
constexpr int kHeader96     = 17;
constexpr int kPad96        = 6;   /* top/bottom inner padding           */
constexpr int kSep96        = 6;   /* separator band between sections     */
constexpr int kGap96        = 4;   /* popup-to-button gap (Windows 7)     */
constexpr int kEdgeMargin96 = 2;   /* never closer to the work area edge  */
constexpr int kDocIcon96    = 15;
/* Additional compact pass: 22 * 0.97 = 21.34 reference pixels. */
constexpr int kAppIcon96    = 21;
constexpr int kPinIcon96    = 14;
constexpr int kClose96      = 14;
/* Internal cap for the document sections, used only when the user's own
 * shell configuration provides no value (see GetJumpListSectionCap): with
 * no configuration the behaviour is exactly the current one. */
constexpr uint32_t kProjectCapDefault = 10;

/* Safety bounds for a tampered registry: ours, not the shell's. The popup
 * must stay drawable no matter what value is stored. */
constexpr uint32_t kCapSafeMin = 1;
constexpr uint32_t kCapSafeMax = 64;

struct JumpListCapCache {
    std::mutex mutex;
    uint32_t cap = kProjectCapDefault;
    const wchar_t* source = L"internal default";
    bool valid = false;
};

JumpListCapCache& JumpListCapCacheRef() {
    static JumpListCapCache cache;
    return cache;
}

/* One REG_DWORD of the user's shell configuration (read-only; the
 * documented RegGetValueW helper does the type conversion and needs no
 * manual buffer). */
bool ReadUserDword(const wchar_t* subKey, const wchar_t* value,
                   uint32_t* out) {
    DWORD data = 0;
    DWORD size = sizeof(data);
    const LSTATUS st = ::RegGetValueW(HKEY_CURRENT_USER, subKey, value,
                                      RRF_RT_REG_DWORD, nullptr, &data, &size);
    if (st != ERROR_SUCCESS || size != sizeof(data)) {
        return false;
    }
    *out = static_cast<uint32_t>(data);
    return true;
}

uint32_t ClampCap(uint32_t v) {
    if (v < kCapSafeMin) return kCapSafeMin;
    if (v > kCapSafeMax) return kCapSafeMax;
    return v;
}

/* The cap of one document section (recent / frequent). Windows 7 sizes
 * these sections from the user's own shell configuration instead of a
 * fixed constant: the per-application destination count, and when that is
 * not set the Start-menu jump list item count plus the fixed extra rows
 * the shell keeps for the standard entries. Both are user settings; this
 * only READS them (no write to the registry, ever). Cached: the value is
 * resolved once per configuration change, not on every open, and
 * TrayService drops the cache on WM_SETTINGCHANGE. The 3/4 reduction the
 * shell applies in its compact display mode has no counterpart in this
 * popup (its geometry is 96-DPI reference values scaled once by the
 * monitor DPI), so it is intentionally not replicated. */
uint32_t GetJumpListSectionCap() {
    JumpListCapCache& cache = JumpListCapCacheRef();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (cache.valid) {
        return cache.cap;
    }
    uint32_t v = 0;
    if (ReadUserDword(
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
            L"\\ApplicationDestinations",
            L"MaxEntries", &v)) {
        cache.cap = ClampCap(v);
        cache.source = L"MaxEntries (user)";
    } else if (ReadUserDword(
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
            L"\\Advanced",
            L"Start_JumpListItems", &v)) {
        cache.cap = ClampCap(v + 4);
        cache.source = L"Start_JumpListItems + 4 (user)";
    } else {
        cache.cap = kProjectCapDefault;
        cache.source = L"internal default (no user value)";
    }
    cache.valid = true;
    LogTagged(L"JUMPLIST",
              L"section cap=%u (source: %s)",
              (unsigned)cache.cap, cache.source);
    return cache.cap;
}
/* Tooltip delay for the hovered row, ms (Windows 7: about half a second). */
constexpr DWORD kTipDelayMs = 600;

/* Glossy cyan pushpin artwork for the localized Pin row. It is decoded
 * through the same WIC path as the graphical close-button states. */
constexpr char kPinPng[] =
    "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";

/* Exact three-state 14x14 artwork used by the managed DWM preview close
 * button (PreviewAssets.cs). Decoding it here keeps both close controls
 * graphically identical instead of approximating the X with GDI lines. */
constexpr char kCloseNormalPng[] =
    "iVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAYAAAAfSC3RAAABhklEQVR42pXSu04CQRSA4X/ZZVmuWRSNEgslJB"
    "JiIWijYGGstCLBxE5K3kATHoAE3sASbDTRwsonABvxEsB4CdoQQowFBYUQ1rUwLJcKpzuZ882ZOXMEXdc53dpq"
    "5J4b80ywEgFvCVgX8tHo7dxhIrS5ETJNAos39zTzuZKw4/Hpl9d52pW7SRyOlTDx3UMkAF3T0LpdLl8+KdRbAE"
    "QWVICROL48i65pAH/wp9dD63xTqLe4OD8BYP8gCTASxxZd/PR6A/j19MjH1RlBh4/9gyQX5ycG6KNgp8nrVZEl"
    "xQbASEMi7XeCnaZRbRhF2u8jb5UABFHEJMsACCYRgFqtZiQJpqF9URxUFGULZruTotNP1TxDJn1koEz6iKp5hq"
    "LTj9nuRJQtQ1BRkF0qZVQDHaeyHKeyBi6jIrtUREUZXFWy2lHcHsLoRnLYoRsH9GPF7UGy2oehDWVqmtgUxMY+"
    "fDyWrH9dlRIBb6lSa6ytbu9NNDkPlTcSAW9JyEejALe558baf4b8F1/egSzJiuNOAAAAAElFTkSuQmCC";
constexpr char kCloseHoverPng[] =
    "iVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAYAAAAfSC3RAAABnUlEQVR42pWSMUtbYRSGn+9eEwOXqJGrpEFFEm"
    "5JwC3BQROCTXFxcLDYwUKQQkr/gAF/gBL/gLTgoE6Ci5OQQehwl3DTwcVSuVCCmEGkgqSQCJ4O0ZtGaUnP9sJ5"
    "vvc9L58SEfYzGWf322WSHiYfj1SBlNpLp52xwsdk9tVsLxxfTmwuPm9XVc6MStk+Qn587wlUky+Zn12kD4C7Fv"
    "xqsOO4lM/rAMxbLwC69PtUrL0LbVBaTe4bt5TP6xwefALgzdsPAF16NTGK1mq2rXNmVOqlopzGNNmcy8jSckGe"
    "ztJyQTbnMnIa06ReKkrOjIr2Z/6Fmk3q6sxze3RKXZ2xULO7bu37Wwmu6/6zJA1ABfzoQwbHVhZnJEFpY81bKG"
    "2s4YwkOLay6EMGKuDvgHrIwDdhUhmwPKi4vkVxfcuDKwMWvgkTPWR0ourDQfyxMDM/r73lmeC198Cj9ofC6MPB"
    "DugLDxKYGmcFWOHmIaQOz/Q4vvBgG8zHI9Wv7k1y+vU7UE2UaoB2293EfRARA6Sfil0jH49U1V46DfDfn/w3M2"
    "Gc5P4I7F8AAAAASUVORK5CYII=";
constexpr char kClosePressedPng[] =
    "iVBORw0KGgoAAAANSUhEUgAAAA4AAAAOCAYAAAAfSC3RAAABxElEQVR42o3Sv2sTYRzH8fflrrkLd+31Ei6BK7"
    "UKkmhiBAdBJKO7S7DgD0QcMri30D+g0O4dupuh0MVdcImj1WIbSyCDChdaSQIS27vLc3cOsUkjKH22L3xez/N8"
    "n+crvXj8MhbfT3C/HHKZ5dwsoVzJIT3M342fvHpK+U6e5MwMiqKQkKSpcBTHCCEIhkM+f2xR33qNMuh1yRcWOH"
    "j7Bl3TSCYVZDkxBcMwIggEvzyP/P0HDHpdFIBIhER+wPuTmKNeDEDJlgE4/BECcCMtcdscEolR/QcKQt/nqKew"
    "u7MNQHW5BjBVlzSfSIgJdPf32avXsQoVqss1dne2x+AcWW6TvXcNDGsBgKlmcq0Gltscn3YR5VqNqb6Vfz17u9"
    "3+77eMTtQkMCWOyxX6TpGN9ZVxYGN9hb5T5LhcAVMaZc+hqqsYtkk/M0Gra5usrm1OcKaIYZuoujq5ampex1qy"
    "WUy44/A1tTPeAGBRcbGWbFLz+gTq6Tmy1x2ups7Q1K/IiYuT840wivF8wemZg56eG8FZO4vbOeVetYaqhiQ1D1"
    "ke/DU5BoGn4fsyBx86zNpZpGePnsc/PzUZ9LqXGnIjnUG/VeA3lCml9GTco10AAAAASUVORK5CYII=";

/* RAII for COM interfaces: Release() on every path - early returns, C++
 * exceptions and SEH-fault unwinds alike (same principle as IconHandle and
 * BitmapHandle in RaiiWrappers.h; kept local because the project has no
 * shared COM wrapper today). */
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
    T* get() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

/* Popup strings. Wording follows the Windows 7 shell ("Recent items",
 * "Frequent items" and the taskbar pin commands). Language indices: the
 * single project language list (Strings.cpp): 0 it, 1 en, 2 es, 3 fr,
 * 4 de, 5 pt, 6 pl, 7 ru, 8 ja, 9 zh, 10 ar. */
struct JumpStr {
    const wchar_t* recent;
    const wchar_t* frequent;
    const wchar_t* pin;
    const wchar_t* unpin;
    const wchar_t* closeWindow;
    const wchar_t* tasks;      /* section header (Windows 7 "Tasks")     */
    const wchar_t* minimize;
    const wchar_t* maximize;
    const wchar_t* restore;
    const wchar_t* move;
    const wchar_t* size;
};
const JumpStr& Str(int lang) {
    static const JumpStr kIt = {
        L"Voci usate di recente", L"Voci usate di frequente",
        L"Fissa questo programma alla barra delle applicazioni",
        L"Rimuovi questo programma dalla barra delle applicazioni",
        L"Chiudi la finestra",
        L"Attivit\u00e0", L"Minimizza", L"Ingrandisci", L"Ripristina",
        L"Sposta", L"Dimensiona" };
    static const JumpStr kEn = {
        L"Recent items", L"Frequent items",
        L"Pin this program to the taskbar",
        L"Unpin this program from the taskbar",
        L"Close window",
        L"Tasks", L"Minimize", L"Maximize", L"Restore",
        L"Move", L"Size" };
    static const JumpStr kEs = {
        L"Elementos recientes", L"Elementos frecuentes",
        L"Anclar este programa a la barra de tareas",
        L"Desanclar este programa de la barra de tareas",
        L"Cerrar ventana",
        L"Tareas", L"Minimizar", L"Maximizar", L"Restaurar",
        L"Mover", L"Redimensionar" };
    static const JumpStr kFr = {
        L"\u00c9l\u00e9ments r\u00e9cents", L"\u00c9l\u00e9ments fr\u00e9quents",
        L"\u00c9pingler ce programme \u00e0 la barre des t\u00e2ches",
        L"D\u00e9tacher ce programme de la barre des t\u00e2ches",
        L"Fermer la fen\u00eatre",
        L"T\u00e2ches", L"R\u00e9duire", L"Agrandir", L"Restaurer",
        L"D\u00e9placer", L"Redimensionner" };
    static const JumpStr kDe = {
        L"Zuletzt verwendete Elemente", L"H\u00e4ufig verwendete Elemente",
        L"Dieses Programm an die Taskleiste anheften",
        L"Dieses Programm von der Taskleiste l\u00f6sen",
        L"Fenster schlie\u00dfen",
        L"Aufgaben", L"Minimieren", L"Maximieren",
        L"Wiederherstellen",
        L"Verschieben", L"Gr\u00f6\u00dfe \u00e4ndern" };
    static const JumpStr kPt = {
        L"Itens recentes", L"Itens frequentes",
        L"Fixar este programa na barra de tarefas",
        L"Desafixar este programa da barra de tarefas",
        L"Fechar janela",
        L"Tarefas", L"Minimizar", L"Maximizar", L"Restaurar",
        L"Mover", L"Redimensionar" };
    static const JumpStr kPl = {
        L"Ostatnie elementy", L"Cz\u0119ste elementy",
        L"Przypnij ten program do paska zada\u0144",
        L"Odepnij ten program od paska zada\u0144",
        L"Zamknij okno",
        L"Zadania", L"Minimalizuj", L"Maksymalizuj", L"Przywr\u00f3\u0107",
        L"Przesu\u0144", L"Zmie\u0144 rozmiar" };
    static const JumpStr kRu = {
        L"\u041d\u0435\u0434\u0430\u0432\u043d\u0438\u0435 \u044d\u043b\u0435"
        L"\u043c\u0435\u043d\u0442\u044b",
        L"\u0427\u0430\u0441\u0442\u044b\u0435 \u044d\u043b\u0435\u043c"
        L"\u0435\u043d\u0442\u044b",
        L"\u0417\u0430\u043a\u0440\u0435\u043f\u0438\u0442\u044c \u044d"
        L"\u0442\u0443 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c"
        L"\u0443 \u043d\u0430 \u043f\u0430\u043d\u0435\u043b\u0438 \u0437"
        L"\u0430\u0434\u0430\u0447",
        L"\u041e\u0442\u043a\u0440\u0435\u043f\u0438\u0442\u044c \u044d"
        L"\u0442\u0443 \u043f\u0440\u043e\u0433\u0440\u0430\u043c\u043c"
        L"\u0443 \u043e\u0442 \u043f\u0430\u043d\u0435\u043b\u0438 \u0437"
        L"\u0430\u0434\u0430\u0447",
        L"\u0417\u0430\u043a\u0440\u044b\u0442\u044c \u043e\u043a\u043d\u043e",
        L"\u041e\u043f\u0435\u0440\u0430\u0446\u0438\u0438",
        L"\u0421\u0432\u0435\u0440\u043d\u0443\u0442\u044c",
        L"\u0420\u0430\u0437\u0432\u0435\u0440\u043d\u0443\u0442\u044c",
        L"\u0412\u043e\u0441\u0441\u0442\u0430\u043d\u043e\u0432\u0438\u0442\u044c",
        L"\u041f\u0435\u0440\u0435\u043c\u0435\u0441\u0442\u0438\u0442\u044c",
        L"\u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c \u0440\u0430\u0437\u043c\u0435\u0440" };
    static const JumpStr kJa = {
        L"\u6700\u8fd1\u4f7f\u3063\u305f\u9805\u76ee",
        L"\u3088\u304f\u4f7f\u3046\u9805\u76ee",
        L"\u3053\u306e\u30d7\u30ed\u30b0\u30e9\u30e0\u3092\u30bf\u30b9"
        L"\u30af\u30d0\u30fc\u306b\u8868\u793a\u3059\u308b",
        L"\u3053\u306e\u30d7\u30ed\u30b0\u30e9\u30e0\u3092\u30bf\u30b9"
        L"\u30af\u30d0\u30fc\u306b\u8868\u793a\u3057\u306a\u3044",
        L"\u30a6\u30a3\u30f3\u30c9\u30a6\u3092\u9589\u3058\u308b",
        L"\u30bf\u30b9\u30af",
        L"\u6700\u5c0f\u5316",
        L"\u6700\u5927\u5316",
        L"\u5143\u306b\u6253\u3064\u3059",
        L"\u79fb\u52d5",
        L"\u30b5\u30a4\u30ba\u5909\u66f4" };
    static const JumpStr kZh = {
        L"\u6700\u8fd1\u4f7f\u7528\u3057\u305f\u9879\u76ee",
        L"\u7ecf\u5e38\u4f7f\u7528\u3059\u308b\u9879\u76ee",
        L"\u5c06\u6b64\u7a0b\u5e8f\u56fa\u5b9a\u5230\u4efb\u52a1\u680f",
        L"\u5c06\u6b64\u7a0b\u5e8f\u4ece\u4efb\u52a1\u680f\u89e3\u9664",
        L"\u5173\u95ed\u7a97\u53e3",
        L"\u4efb\u52a1",
        L"\u6700\u5c0f\u5316",
        L"\u6700\u5927\u5316",
        L"\u8fd8\u539f",
        L"\u79fb\u52a8",
        L"\u5927\u5c0f" };
    static const JumpStr kAr = {
        L"\u0627\u0644\u0639\u0646\u0627\u0635\u0631 \u0627\u0644\u0623"
        L"\u062e\u064a\u0631\u0629",
        L"\u0627\u0644\u0639\u0646\u0627\u0635\u0631 \u0627\u0644\u0645"
        L"\u062a\u0643\u0631\u0631\u0629",
        L"\u062a\u062b\u0628\u064a\u062a \u0647\u0630\u0627 \u0627\u0644"
        L"\u0628\u0631\u0646\u0627\u0645\u062c \u0625\u0644\u0649 \u0634"
        L"\u0631\u064a\u0637 \u0627\u0644\u0645\u0647\u0627\u0645",
        L"\u0625\u0644\u063a\u0627\u0621 \u062a\u062b\u0628\u064a\u062a"
        L" \u0647\u0630\u0627 \u0627\u0644\u0628\u0631\u0646\u0627\u0645"
        L"\u062c \u0645\u0646 \u0634\u0631\u064a\u0637 \u0627\u0644\u0645"
        L"\u0647\u0627\u0645",
        L"\u0625\u063a\u0644\u0627\u0642 \u0627\u0644\u0646\u0627\u0641\u0630\u0629",
        L"\u0627\u0644\u0645\u0647\u0627\u0645",
        L"\u062a\u0635\u063a\u064a\u0631",
        L"\u062a\u0643\u0628\u064a\u0631",
        L"\u0627\u0633\u062a\u0639\u0627\u062f\u0629",
        L"\u062a\u062d\u0631\u064a\u0643",
        L"\u062a\u063a\u064a\u064a\u0631 \u0627\u0644\u062d\u062c\u0645" };
    switch (lang) {
        case 1: return kEn; case 2: return kEs; case 3: return kFr;
        case 4: return kDe; case 5: return kPt; case 6: return kPl;
        case 7: return kRu; case 8: return kJa; case 9: return kZh;
        case 10: return kAr;
        default: return kIt;
    }
}


/* Read one string property out of a property store. True when the value
 * existed and was a non-empty string. The PROPVARIANT is always released
 * through PropVariantClear (its string is CoTaskMem memory), including the
 * miss paths - that is the shell contract, RAII for PROPVARIANT is exactly
 * what PropVariantClear is for. */
bool ReadStringProp(IPropertyStore* store, const PROPERTYKEY& key,
                    std::wstring& out) {
    if (store == nullptr) return false;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    bool found = false;
    if (SUCCEEDED(store->GetValue(key, &pv)) &&
        pv.vt == VT_LPWSTR && pv.pwszVal != nullptr && pv.pwszVal[0]) {
        out = pv.pwszVal;
        found = true;
    }
    PropVariantClear(&pv);
    return found;
}

/* Extract the document path from one destination item: automatic
 * destinations usually surface as an IShellLink to the document, with an
 * IShellItem fallback. True when a non-empty path came out. */
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

/* The tray window calls this on WM_SETTINGCHANGE: the user's shell
 * configuration may have changed, so the cached cap is dropped and the
 * next list open resolves it again. */
void w7t::InvalidateJumpListCapCache() {
    JumpListCapCache& cache = JumpListCapCacheRef();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.valid = false;
}

/* ------------------------------------------------------------------ */
/*  Application identity - public property-store APIs only.             */
/* ------------------------------------------------------------------ */
std::wstring JumpListWindow::ResolveAppUserModelId(HWND hwnd,
        const std::wstring& lnkPath, const std::wstring& exePath,
        int32_t& outSource) {
    (void)exePath;  /* kept for the caller: it is the implicit fallback id */
    outSource = kSourceNone;

    if (hwnd != nullptr && IsWindow(hwnd)) {
        /* 1. The window-level AppUserModelID - the key the shell groups
         *    taskbar buttons by (documented: a window-level id overrides
         *    the process-level one). SHGetPropertyStoreForWindow is the
         *    public door to it; no private shell classes are touched. */
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd,
                IID_PPV_ARGS(&store))) && store) {
            std::wstring id;
            if (ReadStringProp(store.get(), PKEY_AppUserModel_ID, id)) {
                outSource = kSourceWindow;
                return id;
            }
        }
    }
    if (!lnkPath.empty()) {
        /* 2. Classic Win32 apps: the id carried by the shell metadata of
         *    the pinned shortcut (the .lnk the group was launched from) -
         *    the same source PinnedApps uses for grouping, so the jump
         *    list lands on the application the button represents. */
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(SHGetPropertyStoreFromParsingName(lnkPath.c_str(),
                nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store))) && store) {
            std::wstring id;
            if (ReadStringProp(store.get(), PKEY_AppUserModel_ID, id)) {
                outSource = kSourceShortcut;
                return id;
            }
        }
    }
    return std::wstring();
}

/* ------------------------------------------------------------------ */
/*  Real jump list read: IApplicationDocumentLists.                      */
/*  This is the documented READ side of the Recent/Frequent sections.    */
/*  The PINNED (custom) section has no documented read side at all on    */
/*  Windows 7 and later: IApplicationDestinations only removes Recent/   */
/*  Frequent destinations and ICustomDestinationList builds/replaces a   */
/*  whole list - MSDN is explicit that the pinned items "cannot be       */
/*  removed programmatically; only the user can remove them", and the    */
/*  Windows 7 taskbar itself reads the store directly (reverse-          */
/*  engineering evidence: docs/JUMPLIST-RE-VERIFICATION.md). So the      */
/*  pinned section is not shown here; nothing is ever invented: when the */
/*  Shell exposes no list a section simply stays hidden.                 */
/* ------------------------------------------------------------------ */
int32_t JumpListWindow::ReadDocumentLists(const std::wstring& appUserModelId,
        const std::wstring& exePath, std::vector<JumpListDoc>& outDocs) {
    outDocs.clear();

    /* Which id do we ask the Shell with? An explicit/default AUMID first.
     * For classic apps that never set one, Windows keys the application's
     * documents under the DEFAULT AppUserModelID derived from the
     * executable path ("Application User Model IDs", MSDN), so asking the
     * same storage with that path is the documented fallback, not a
     * fabrication. */
    std::wstring askId = appUserModelId;
    const bool implicitId = askId.empty() && !exePath.empty();
    if (implicitId) askId = exePath;
    if (askId.empty()) {
        LogTagged(L"JUMPLIST",
                  L"no application identity for the button - document"
                  L" sections stay empty (nothing fabricated)");
        return 0;
    }

    W7T_SEH_TRY {
        raii::ComInitializer com;
        if (FAILED(com.result()) && com.result() != RPC_E_CHANGED_MODE) {
            LogTagged(L"JUMPLIST", L"CoInitializeEx failed hr=0x%08X",
                      (unsigned)com.result());
            return -1;
        }
        try {
            ComPtr<IApplicationDocumentLists> adl;
            const HRESULT hrCreate = CoCreateInstance(
                CLSID_ApplicationDocumentLists, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&adl));
            if (FAILED(hrCreate) || !adl) {
                LogTagged(L"JUMPLIST",
                          L"CoCreateInstance(ApplicationDocumentLists)"
                          L" hr=0x%08X", (unsigned)hrCreate);
                return -1;
            }
            const HRESULT hrSet = adl->SetAppID(askId.c_str());
            if (FAILED(hrSet)) {
                LogTagged(L"JUMPLIST", L"SetAppID(\"%s\") failed hr=0x%08X",
                          askId.c_str(), (unsigned)hrSet);
                return -1;
            }
            UINT sectionCounts[2] = { 0, 0 };
            const APPDOCLISTTYPE kinds[2] = { ADLT_RECENT,
                                                        ADLT_FREQUENT };
            const uint32_t cap = GetJumpListSectionCap();
            for (int pass = 0; pass < 2; ++pass) {
                ComPtr<IObjectArray> items;
                const HRESULT hrList = adl->GetList(
                    kinds[pass], (UINT)cap,
                    IID_PPV_ARGS(&items));
                if (FAILED(hrList) || !items) {
                    /* A normal answer for apps that register no automatic
                     * destinations: empty section, not a failure. */
                    LogTagged(L"JUMPLIST",
                              L"GetList(section %d) hr=0x%08X - the app"
                              L" exposes no such list",
                              pass, (unsigned)hrList);
                    continue;
                }
                UINT count = 0;
                items->GetCount(&count);
                for (UINT i = 0; i < count &&
                                sectionCounts[pass] < cap;
                     ++i) {
                    /* One malformed element must not stop the others. */
                    ComPtr<IUnknown> unk;
                    if (FAILED(items->GetAt(i, IID_PPV_ARGS(&unk))) || !unk)
                        continue;
                    JumpListDoc entry;
                    entry.section = pass;
                    if (!ExtractDocPath(unk.get(), entry.path)) continue;
                    const size_t slash = entry.path.find_last_of(L"\\/");
                    entry.displayName = (slash == std::wstring::npos)
                        ? entry.path : entry.path.substr(slash + 1);
                    /* Windows 7 shows the document name without the
                     * extension; same rule, taken from the file name. */
                    const size_t dot = entry.displayName.find_last_of(L'.');
                    if (dot != std::wstring::npos && dot > 0)
                        entry.displayName = entry.displayName.substr(0, dot);
                    ++sectionCounts[pass];
                    outDocs.push_back(std::move(entry));
                }
            }
            LogTagged(L"JUMPLIST",
                      L"entries loaded: recent=%d frequent=%d (id source=%s)",
                      (int)sectionCounts[0], (int)sectionCounts[1],
                      implicitId ? L"implicit, from executable path"
                                 : L"AppUserModelID");
            return 0;
        } catch (...) {
            /* A C++ exception here can only be std::bad_alloc: controlled
             * failure, logged, never propagated to the taskbar thread. */
            outDocs.clear();
            LogTagged(L"JUMPLIST", L"C++ exception while reading the list");
            return -1;
        }
    } W7T_SEH_CATCH {
        outDocs.clear();
        LogTagged(L"JUMPLIST", L"hardware fault while reading the list");
        return -1;
    } W7T_SEH_END
    return -1;
}

/* ------------------------------------------------------------------ */

JumpListWindow& JumpListWindow::Instance() {
    static JumpListWindow instance;
    return instance;
}

LRESULT CALLBACK JumpListWindow::OutsideMouseProc(int code, WPARAM wParam,
                                                   LPARAM lParam) {
    JumpListWindow& j = Instance();
    if (code >= 0 && j.m_interactive && j.IsVisible() &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
         wParam == WM_MBUTTONDOWN)) {
        const MSLLHOOKSTRUCT* mouse =
            reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (mouse != nullptr && !PtInRect(&j.m_popupRect, mouse->pt)) {
            /* Never block the click. Dismiss asynchronously so the target
             * underneath receives its original down message unchanged. */
            PostMessageW(j.m_hwnd, kDismissOutsideMessage, 0, 0);
        }
    }
    return CallNextHookEx(j.m_outsideMouseHook, code, wParam, lParam);
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

int JumpListWindow::Sc(int v96) const {
    return ::MulDiv(v96, (int)m_dpi, 96);
}

void JumpListWindow::ClearContent() {
    m_rows.clear();   /* each Row owns its HICON through raii::IconHandle */
    m_docs.clear();
}

void JumpListWindow::BuildRows() {
    m_rows.clear();

    auto addDocs = [&](int32_t section, Row::Kind kind) {
        for (const JumpListDoc& doc : m_docs) {
            if (doc.section != section) continue;
            Row r;
            r.kind = kind;
            r.label = doc.displayName;
            r.path = doc.path;
            /* The REAL file icon (never a generic placeholder): fetched
             * once per open; the row owns it from here on. SHGetFileInfo
             * has faulted on dead network paths in the past, so the call
             * keeps its own SEH barrier - one bad icon must not take the
             * popup down, the entry simply paints without its icon. */
            SHFILEINFOW sfi{};
            W7T_SEH_TRY {
                if (SHGetFileInfoW(r.path.c_str(), 0, &sfi, sizeof(sfi),
                                   SHGFI_ICON | SHGFI_SMALLICON)
                    && sfi.hIcon != nullptr) {
                    r.icon.reset(sfi.hIcon);  /* ownership moves into the row */
                }
            } W7T_SEH_CATCH {
                /* row survives, iconless; space stays reserved so the
                 * text keeps its alignment either way */
            } W7T_SEH_END
            m_rows.push_back(std::move(r));
        }
    };

    addDocs(0, Row::DocRecent);

    /* The Frequent section only earns its place when it is not a copy of
     * the Recent one (many apps return the same documents in both lists;
     * the Windows 7 jump list then shows a single section). */
    bool frequentDiffers = false;
    for (const JumpListDoc& d : m_docs) {
        if (d.section != 1) continue;
        bool found = false;
        for (const JumpListDoc& e : m_docs) {
            if (e.section == 0 &&
                _wcsicmp(e.path.c_str(), d.path.c_str()) == 0) {
                found = true;
                break;
            }
        }
        if (!found) { frequentDiffers = true; break; }
    }
    if (frequentDiffers) addDocs(1, Row::DocFrequent);

    Row app;
    app.kind = Row::App;
    app.label = m_title;
    m_rows.push_back(std::move(app));

    if (!m_pinned) {
        Row pin;
        pin.kind = Row::Pin;
        pin.label = Str(m_lang).pin;
        m_rows.push_back(std::move(pin));
    }

    /* The Windows 7 Tasks section: only while the group has a live
     * window, and it reuses the existing native window-command path
     * (WindowManager::ExecuteCommand - the same one W7T_ExecuteWindow-
     * Command exposes). The graphical close row below stays the fast
     * close; the section adds the other window commands. */
    if (m_representativeHwnd != nullptr && IsWindow(m_representativeHwnd)) {
        const JumpStr& S = Str(m_lang);
        struct {
            const wchar_t* label;
            int32_t cmd;
        } tasks[] = {
            { S.minimize, W7T_CMD_MINIMIZE },
            { S.maximize, W7T_CMD_MAXIMIZE },
            { S.restore,  W7T_CMD_RESTORE  },
            { S.move,     W7T_CMD_MOVE     },
            { S.size,     W7T_CMD_SIZE     },
        };
        for (const auto& t : tasks) {
            Row r;
            r.kind = Row::Task;
            r.label = t.label;
            r.cmd = t.cmd;
            m_rows.push_back(std::move(r));
        }
        Row close;
        close.kind = Row::Close;
        close.label = S.closeWindow;
        m_rows.push_back(std::move(close));
    }
    /* The "Pin this program to the taskbar" row above (the taskbar pin
     * folder .lnk) is the application's taskbar pin - a different store
     * from the jump list's pinned items, which no public API exposes;
     * see docs/JUMPLIST-RE-VERIFICATION.md. */
}

void JumpListWindow::Layout() {
    m_width = Sc(kWidth96);
    int y = Sc(kPad96);

    int lastKind = -1;   /* -1: no previous row (outside the Kind range) */
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        const bool isDoc =
            r.kind == Row::DocRecent || r.kind == Row::DocFrequent;
        const bool isTask = r.kind == Row::Task;

        /* Section header band before the first row of every section that
         * has one: the document sections and Tasks. */
        if ((isDoc || isTask) && (int)r.kind != lastKind) {
            y += Sc(kHeader96);
        }
        /* Separator band before the application row, the pin row and the
         * Tasks section (the Windows 7 list separates documents, the app
         * link and the tasks). */
        if (!isDoc && (lastKind == (int)Row::DocRecent ||
                       lastKind == (int)Row::DocFrequent ||
                       lastKind == (int)Row::App ||
                       lastKind == (int)Row::Close ||
                       lastKind == (int)Row::Pin)) {
            y += Sc(kSep96);
        }

        const int rowH = (r.kind == Row::App) ? Sc(kRowApp96)
                         : (r.kind == Row::Close) ? Sc(kRowClose96)
                         : (r.kind == Row::Pin) ? Sc(kRowPin96)
                         : Sc(kRowDoc96);
        r.rect = RECT{ 0, y, m_width, y + rowH };
        y += rowH;
        lastKind = (int)r.kind;
    }
    m_totalH = y + Sc(kPad96);
}

RECT JumpListWindow::RowRect(size_t index) const {
    if (index < m_rows.size()) return m_rows[index].rect;
    return RECT{ 0, 0, 0, 0 };
}

RECT JumpListWindow::CloseRect() const {
    if (m_representativeHwnd == nullptr || !IsWindow(m_representativeHwnd))
        return RECT{};
    for (const Row& row : m_rows) {
        if (row.kind == Row::Close) {
            const int size = Sc(kClose96);
            const int left = Sc(14);
            const int top = row.rect.top +
                (row.rect.bottom - row.rect.top - size) / 2;
            return RECT{ left, top, left + size, top + size };
        }
    }
    return RECT{};
}

bool JumpListWindow::HitCloseClient(POINT clientPt) const {
    RECT close = CloseRect();
    return close.right > close.left && PtInRect(&close, clientPt) != FALSE;
}

/* Index of the row under client coordinates, -1 for none. The empty strip
 * below the last row is NOT a target (Windows 7: releasing on free popup
 * space closes the list without activating anything). */
int JumpListWindow::HitRowClient(POINT pt) const {
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (PtInRect(&m_rows[i].rect, pt)) return (int)i;
    }
    return -1;
}

bool JumpListWindow::InInteractionArea(POINT screenPt) const {
    return PtInRect(&m_area, screenPt) != FALSE;
}

/* Work area of the monitor hosting the taskbar button - the same clamp
 * the clock flyout applies (FlyoutLauncher::FixFlyoutPosition). */
RECT JumpListWindow::WorkAreaForButton() const {
    HMONITOR mon = MonitorFromRect(&m_buttonRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon != nullptr && GetMonitorInfoW(mon, &mi)) {
        return mi.rcWork;
    }
    /* No monitor info (rare): fall back to the button neighborhood so
     * the popup is at least visible near its anchor. */
    return RECT{ m_buttonRect.left - Sc(400), m_buttonRect.top - Sc(600),
                 m_buttonRect.right + Sc(400),
                 m_buttonRect.bottom + Sc(80) };
}

/* Placement from the REAL taskbar button rectangle, per edge, clamped to
 * the work area of the monitor that hosts the button. This is the ONLY
 * placement rule: with the bar at the bottom the popup opens ABOVE the
 * button, left-aligned with its left edge and a small gap - exactly where
 * the Windows 7 shell opens the jump view - and it stays there for the
 * whole gesture (the cursor only moves the highlighted row). Gap and
 * margin are DPI-scaled; no unscaled offsets. */
void JumpListWindow::Place(HWND hwnd, const RECT& button, int32_t edge) {
    const int gap = Sc(kGap96);
    const int margin = Sc(kEdgeMargin96);
    const RECT wa = WorkAreaForButton();

    const int w = m_width, h = m_totalH;
    int x = button.left, y = button.top - h - gap;
    switch (edge) {
        case kEdgeTop:    /* bar at the top: the list opens BELOW */
            y = button.bottom + gap;
            break;
        case kEdgeLeft:   /* vertical bar at the left: open to its right */
            x = button.right + gap;
            y = button.top;
            break;
        case kEdgeRight:  /* vertical bar at the right: open to its left */
            x = button.left - w - gap;
            y = button.top;
            break;
        case kEdgeBottom:
        default:
            break;
    }
    if (x + w > wa.right - margin)  x = wa.right - margin - w;
    if (x < wa.left + margin)       x = wa.left + margin;
    if (y + h > wa.bottom - margin) y = wa.bottom - margin - h;
    if (y < wa.top + margin)        y = wa.top + margin;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m_popupRect = RECT{ x, y, x + w, y + h };
}

/* Interaction area of the gesture: popup, taskbar button and everything
 * between them. Leaving it is what the managed side treats as cancel. */
void JumpListWindow::UpdateInteractionArea() {
    RECT u = m_popupRect;
    UnionRect(&u, &u, &m_buttonRect);
    const int pad = Sc(4);
    InflateRect(&u, pad, pad);
    m_area = u;
}

int32_t JumpListWindow::Open(const RECT& buttonRectScreen, int32_t edge,
        const std::wstring& title, const std::wstring& launchPath,
        const std::wstring& pinnedLnkPath, bool isPinned,
        HWND representativeHwnd, const std::wstring& exePath,
        const uint32_t* iconArgb, int iconW, int iconH, int lang,
        wchar_t* outAppId, int outAppIdCap) {
    if (buttonRectScreen.right <= buttonRectScreen.left ||
        buttonRectScreen.bottom <= buttonRectScreen.top) {
        LogTagged(L"JUMPLIST", L"bad button rectangle - refusing to open");
        return -3;
    }

    /* One gesture at a time: drop any stale popup before touching the
     * shared state, so a failure below can never leave yesterday's rows
     * on screen. */
    Hide();
    m_hover = -1;
    m_interactive = false;
    m_closeHot = false;
    m_closeDown = false;
    m_tipRow = -1;
    m_tipShown = false;
    m_tipStart = 0;
    ClearContent();
    m_appIcon.reset();
    m_pinIcon.reset();

    W7T_SEH_TRY {
        RegisterClassOnce();

        m_dpi = GetDpiForScreenRect(buttonRectScreen);
        m_lang = (lang >= 0 && lang <= 10) ? lang : 1;
        m_title = title;
        m_launchPath = launchPath;
        m_pinnedLnk = pinnedLnkPath;
        m_pinned = isPinned;
        m_buttonRect = buttonRectScreen;
        m_edge = edge;
        m_representativeHwnd =
            representativeHwnd != nullptr && IsWindow(representativeHwnd)
                ? representativeHwnd : nullptr;

        /* --- application identity (public Shell APIs only) --- */
        int32_t source = kSourceNone;
        std::wstring aumid = ResolveAppUserModelId(representativeHwnd,
                                                   pinnedLnkPath,
                                                   exePath, source);
        if (source == kSourceNone && !exePath.empty()) {
            LogTagged(L"JUMPLIST",
                      L"application identity resolved (source=implicit"
                      L" default): AppUserModelID=<from executable path>");
        } else {
            LogTagged(L"JUMPLIST",
                      L"application identity resolved (source=%d):"
                      L" AppUserModelID=%s", source,
                      aumid.empty() ? L"(none)" : aumid.c_str());
        }
        if (outAppId != nullptr && outAppIdCap > 0) {
            CopyToFixed(outAppId, (size_t)outAppIdCap, aumid);
        }
        /* --- the Windows 7 policy (Start_JumpListItems) ------------- */
        /* 0 disables the jump lists, exactly as in Windows 7 (the value
         * name is evidenced in the taskbar code of the reverse-
         * engineered explorer, see docs/JUMPLIST-RE-VERIFICATION.md). */
        {
            DWORD policy = 1, policySize = sizeof(policy);
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion"
                    L"\\Explorer\\StartMenu",
                    0, KEY_READ, &key) == ERROR_SUCCESS) {
                (void)RegQueryValueExW(key, L"Start_JumpListItems", nullptr,
                                       nullptr, (BYTE*)&policy, &policySize);
                RegCloseKey(key);
            }
            if (policy == 0) {
                LogTagged(L"JUMPLIST",
                          L"Start_JumpListItems=0: jump lists disabled by"
                          L" policy - not opening");
                return -4;
            }
        }

        /* --- the application's REAL jump list entries --- */
        const int32_t read = ReadDocumentLists(aumid, exePath, m_docs);
        if (read < 0) {
            ClearContent();
            return -1;   /* Shell/COM failure: the managed side cancels */
        }
        const int32_t docCount = (int32_t)m_docs.size();

        /* --- app icon for the application row: pixels handed over by the
         *     managed side (the group's live icon, packaged apps included) */
        if (iconArgb != nullptr && iconW > 0 && iconH > 0) {
            const std::vector<uint32_t> px(
                iconArgb, iconArgb + (size_t)iconW * (size_t)iconH);
            if (HBITMAP hb = MakeHBitmapFromArgb(px, iconW, iconH)) {
                m_appIcon.reset(hb);   /* BitmapHandle owns and deletes it */
            }
        }

        /* Decode the embedded pushpin and the same close PNGs used by
         * PreviewAssets.cs. BitmapHandle owns every resulting HBITMAP. */
        auto loadArtwork = [](const char* png, raii::BitmapHandle& target) {
            if (target) return;
            std::vector<uint32_t> pixels;
            int width = 0, height = 0;
            if (DecodeEmbeddedPng(png, pixels, width, height, false, 0)) {
                target.reset(MakeHBitmapFromArgb(pixels, width, height));
            }
        };
        loadArtwork(kPinPng, m_pinIcon);
        loadArtwork(kCloseNormalPng, m_closeNormal);
        loadArtwork(kCloseHoverPng, m_closeHover);
        loadArtwork(kClosePressedPng, m_closePressed);

        BuildRows();
        Layout();

        if (m_hwnd == nullptr) {
            m_hwnd = CreateWindowExW(
                WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                kClassName, L"", WS_POPUP,
                0, 0, m_width, m_totalH,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (m_hwnd == nullptr) {
                LogTagged(L"JUMPLIST", L"CreateWindowExW failed err=%d",
                          (int)GetLastError());
                return -2;
            }
            ApplyAeroFlyoutStyle(m_hwnd);   /* shared Aero flyout border */
        }
        /* The opening drag is still captured by WPF. Start non-activating;
         * MakeInteractive removes this bit only after that mouse-up. */
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE,
            GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE);
        Place(m_hwnd, m_buttonRect, m_edge);
        UpdateInteractionArea();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        LogTagged(L"JUMPLIST",
                  L"popup opened: rect=(%d,%d)-(%d,%d) screen px, dpi=%d,"
                  L" entries=%d, edge=%d",
                  (int)m_popupRect.left, (int)m_popupRect.top,
                  (int)m_popupRect.right, (int)m_popupRect.bottom,
                  (int)m_dpi, (int)docCount,
                  (int)edge);
        return docCount;
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while opening the popup");
        Hide();
        ClearContent();
        return -1;
    } W7T_SEH_END
}

void JumpListWindow::MakeInteractive() {
    if (!IsVisible()) return;
    W7T_SEH_TRY {
        m_interactive = true;
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE,
            GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED |
                     SWP_NOOWNERZORDER);
        SetForegroundWindow(m_hwnd);
        SetActiveWindow(m_hwnd);

        if (m_outsideMouseHook == nullptr) {
            HMODULE module = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&JumpListWindow::OutsideMouseProc),
                &module);
            m_outsideMouseHook = SetWindowsHookExW(
                WH_MOUSE_LL, OutsideMouseProc, module, 0);
        }
    } W7T_SEH_CATCH {
        Hide();
    } W7T_SEH_END
}

void JumpListWindow::Hide() {
    W7T_SEH_TRY {
        if (m_outsideMouseHook != nullptr) {
            UnhookWindowsHookEx(m_outsideMouseHook);
            m_outsideMouseHook = nullptr;
        }
        if (GetCapture() == m_hwnd) ReleaseCapture();
        if (m_hwnd != nullptr) ShowWindow(m_hwnd, SW_HIDE);
        m_hover = -1;
        m_interactive = false;
        m_closeHot = false;
        m_closeDown = false;
    } W7T_SEH_CATCH {
    } W7T_SEH_END
}

bool JumpListWindow::IsVisible() const {
    return m_hwnd != nullptr && IsWindowVisible(m_hwnd);
}

/* Hover-row update from a screen point, shared by SetHover (the gesture
 * moves) and HitRowAt's geometry. */
void JumpListWindow::UpdateHoverFromScreen(POINT screenPt) {
    POINT cl = screenPt;
    ScreenToClient(m_hwnd, &cl);
    const int hit = HitRowClient(cl);
    if (hit != m_hover) {
        const int previous = m_hover;
        m_hover = hit;
        /* Repaint only the involved row bands (client coordinates);
         * the full-width band keeps the separators and headers intact
         * because the paint pass redraws them from the same geometry. */
        if (m_hover >= 0) {
            RECT r = RowRect((size_t)m_hover);
            r.left = 0;
            r.right = m_width;
            InvalidateRect(m_hwnd, &r, FALSE);
        }
        if (previous >= 0) {
            RECT r = RowRect((size_t)previous);
            r.left = 0;
            r.right = m_width;
            InvalidateRect(m_hwnd, &r, FALSE);
        }
    }
}

int32_t JumpListWindow::SetHover(int32_t screenX, int32_t screenY) {
    if (!IsVisible()) return 0;
    W7T_SEH_TRY {
        POINT pt{ screenX, screenY };
        if (!InInteractionArea(pt)) {
            if (m_hover != -1) {
                m_hover = -1;
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            return 0;
        }
        UpdateHoverFromScreen(pt);
        return 1;
    } W7T_SEH_CATCH {
    } W7T_SEH_END
    return 1;
}

int32_t JumpListWindow::HitRowAt(int32_t screenX, int32_t screenY) const {
    if (m_hwnd == nullptr || !IsWindowVisible(m_hwnd)) return -1;
    W7T_SEH_TRY {
        POINT cl{ screenX, screenY };
        ScreenToClient(m_hwnd, &cl);
        return HitRowClient(cl);
    } W7T_SEH_CATCH {
        return -1;
    } W7T_SEH_END
}

/* Execute the action behind the row under the screen point. Returns 0 when
 * the popup was not open (nothing to do); 1 when the gesture ended, with
 * *outBits telling the managed side what happened. */
int32_t JumpListWindow::ActivateRow(int32_t screenX, int32_t screenY,
                                    int32_t* outBits) {
    int32_t bits = 0;
    if (outBits != nullptr) *outBits = 0;
    if (!IsVisible()) return 0;
    W7T_SEH_TRY {
        POINT cl{ screenX, screenY };
        ScreenToClient(m_hwnd, &cl);
        const int hit = HitRowClient(cl);
        if (hit >= 0) {
            const Row& row = m_rows[(size_t)hit];
            switch (row.kind) {
                case Row::DocRecent:
                case Row::DocFrequent: {
                    /* Open the document with its registered application.
                     * A dead path fails quietly: ShellExecuteW returns an
                     * SE_ERR_* code - that is logged, never reported as a
                     * success. */
                    const HINSTANCE hi = ShellExecuteW(nullptr, L"open",
                        row.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    const INT_PTR rc = (INT_PTR)hi;
                    if (rc <= 32) {
                        LogTagged(L"JUMPLIST",
                                  L"ShellExecuteW(open document) failed"
                                  L" code=%d path=\"%s\"",
                                  (int)rc, row.path.c_str());
                    } else {
                        bits |= BitsOpenedDoc;
                        LogTagged(L"JUMPLIST",
                                  L"item activated: document \"%s\"",
                                  row.path.c_str());
                    }
                    break;
                }
                case Row::Task:
                    ExecuteTask(row.cmd);
                    LogTagged(L"JUMPLIST", L"item activated: task command"
                                           L" %d", row.cmd);
                    break;
                case Row::App:
                    LaunchApp();
                    bits |= BitsLaunchedApp;
                    LogTagged(L"JUMPLIST", L"item activated: application row");
                    break;
                case Row::Close:
                    CloseRunningApplication();
                    LogTagged(L"JUMPLIST", L"item activated: close window");
                    break;
                case Row::Pin:
                    PerformPinOrUnpin();
                    bits |= BitsPinToggled;
                    LogTagged(L"JUMPLIST", L"item activated: taskbar pin"
                                           L" toggled");
                    break;
            }
        } else {
            LogTagged(L"JUMPLIST",
                      L"released over empty popup space - closing");
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while activating a row");
    } W7T_SEH_END
    Hide();
    if (outBits != nullptr) *outBits = bits;
    return 1;
}

void JumpListWindow::LaunchApp() {
    W7T_SEH_TRY {
        if (!m_launchPath.empty()) {
            ShellExecuteW(nullptr, L"open", m_launchPath.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while launching the app");
    } W7T_SEH_END
}

void JumpListWindow::CloseRunningApplication() {
    W7T_SEH_TRY {
        /* The close control exists only while this real representative
         * window exists. WM_CLOSE follows the same path as the graphical
         * close button in the DWM preview. */
        if (m_representativeHwnd != nullptr && IsWindow(m_representativeHwnd)) {
            PostMessageW(m_representativeHwnd, WM_CLOSE, 0, 0);
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while closing the app");
    } W7T_SEH_END
}

void JumpListWindow::PerformPinOrUnpin() {
    W7T_SEH_TRY {
        if (m_pinned) {
            /* Unpin: delete the real .lnk; the PinnedApps watcher refreshes
             * the model on its own, and the managed side also calls
             * InvalidatePins when it sees BitsPinToggled. v2.62-alpha (G6):
             * the write point moved to PinVerbs (shared with the canonical
             * verbs) so the bar can never disagree with itself on disk. */
            if (!m_pinnedLnk.empty()) {
                w7t::DeletePinnedShortcut(m_pinnedLnk);
            }
        } else {
            /* Pin: create the .lnk in the real shell pin folder (the same
             * shared write point). */
            if (!m_launchPath.empty()) {
                w7t::TogglePinnedApp(m_launchPath, m_title, true, nullptr);
            }
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while toggling the pin");
    } W7T_SEH_END
}

/* The Tasks section rows: the same window-command path W7T_Execute-
 * WindowCommand exposes (WindowManager), so the jump list and the rest
 * of the bar can never disagree about what Minimize/Maximize/... do. */
void JumpListWindow::ExecuteTask(int32_t cmd) {
    W7T_SEH_TRY {
        if (m_representativeHwnd == nullptr ||
            !IsWindow(m_representativeHwnd)) {
            return;   /* the window went away; the rows vanish next open */
        }
        const int32_t rc = WindowManager::Instance().ExecuteCommand(
            m_representativeHwnd, cmd);
        if (rc != W7T_OK) {
            LogTagged(L"JUMPLIST", L"task command %d failed code=%d",
                      (int)cmd, (int)rc);
        }
    } W7T_SEH_CATCH {
        LogTagged(L"JUMPLIST", L"hardware fault while executing a task");
    } W7T_SEH_END
}

/* The Windows 7 tooltip of a row: the full path (the name only when the
 * item has no resolvable path - NoJumpListPathTooltip; the application
 * row shows its launch path). */
std::wstring JumpListWindow::TooltipFor(const Row& row) const {
    switch (row.kind) {
        case Row::DocRecent:
        case Row::DocFrequent:
            return row.path.empty() ? row.label : row.path;
        case Row::App:
            return m_launchPath;
        default:
            return std::wstring();
    }
}

void JumpListWindow::ShowRowTooltip(int row, POINT clientPt) {
    if (m_hwnd == nullptr || row < 0 || row >= (int)m_rows.size()) return;
    const std::wstring text = TooltipFor(m_rows[(size_t)row]);
    if (text.empty()) {
        ClearRowTooltip();
        return;
    }
    if (m_tooltip == nullptr) {
        /* The tooltip child is created once per popup window and dies
         * with it (WM_NCDESTROY drops the handle). TTS_ALWAYSTIP because
         * the parent is a NOACTIVATE tool window (the legacy
         * TTS_NOPROGRESS window style is a no-op since Vista and is not
         * declared by the SDK headers). */
        m_tooltip = CreateWindowExW(0, TOOLTIPS_CLASS, nullptr,
                                    TTS_ALWAYSTIP,
                                    0, 0, 0, 0, m_hwnd, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        if (m_tooltip == nullptr) return;
    }
    if (row != m_tipRow) {
        ClearRowTooltip();
        m_tipRow = row;
        m_tipStart = GetTickCount();
    }
    /* The Windows 7 delay: the tip appears only after the row has been
     * hovered for a beat, not on the first mouse move. */
    if (m_tipShown || GetTickCount() - m_tipStart < kTipDelayMs) return;
    /* Manual tracking - the documented TTM_ADDTOOL / TTM_TRACKPOSITION /
     * TTM_TRACKACTIVATE sequence with TTTOOLINFOW (the SDK's declared
     * tool structure; the MSDN-documented TOOLTEXTW of the older
     * TrackToolTip entry point is not declared by the SDK headers). The
     * tool string is a writable pointer in the SDK layout, so a
     * temporary CoTaskMem buffer carries the text (the control copies it
     * on TTM_ADDTOOL); it is released on every path. The position is in
     * the client coordinates of the parent. */
    wchar_t* buf =
        (wchar_t*)CoTaskMemAlloc((text.size() + 1) * sizeof(wchar_t));
    if (buf == nullptr) return;
    lstrcpynW(buf, text.c_str(), (int)text.size() + 1);
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = m_hwnd;
    ti.uId = (UINT_PTR)row;
    ti.hinst = GetModuleHandleW(nullptr);
    ti.lpszText = buf;
    const bool added =
        SendMessageW(m_tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti) != 0;
    bool shown = false;
    if (added) {
        (void)SendMessageW(m_tooltip, TTM_TRACKPOSITION, 0,
                           MAKELPARAM((UINT)(clientPt.x + Sc(10)),
                                      (UINT)(clientPt.y + Sc(16))));
        shown = SendMessageW(m_tooltip, TTM_TRACKACTIVATE, TRUE,
                             (LPARAM)&ti) != 0;
        if (!shown) {
            (void)SendMessageW(m_tooltip, TTM_DELTOOL, 0, (LPARAM)&ti);
        }
    }
    CoTaskMemFree(buf);
    m_tipShown = shown;
}

void JumpListWindow::ClearRowTooltip() {
    if (m_tooltip != nullptr && (m_tipShown || m_tipRow >= 0)) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.hwnd = m_hwnd;
        ti.uId = (UINT_PTR)m_tipRow;
        if (m_tipShown) {
            (void)SendMessageW(m_tooltip, TTM_TRACKACTIVATE, FALSE,
                               (LPARAM)&ti);
        }
        if (m_tipRow >= 0) {
            (void)SendMessageW(m_tooltip, TTM_DELTOOL, 0, (LPARAM)&ti);
        }
    }
    m_tipShown = false;
    m_tipRow = -1;
    m_tipStart = 0;
}

/* ------------------------------------------------------------------ */
/*  Painting. Colors measured from Windows 7 jump list reference         */
/*  screenshots and shared with the project's recreated flyouts: the     */
/*  popup body is the pale blue-white, section headers are bold          */
/*  steel-blue on the same background, separators are the #C9D6E6 of     */
/*  the Aero menus, the hover is the light azure gradient inside its     */
/*  #94C6EF border - the exact hover the Windows 7 menus paint. Every    */
/*  length below goes through Sc(), so the proportions are DPI proof.    */
/* ------------------------------------------------------------------ */
void JumpListWindow::GradientRect(HDC hdc, const RECT& r, COLORREF top,
                                   COLORREF bottom, COLORREF edge) {
    const int h = r.bottom - r.top;
    if (h <= 0 || r.right <= r.left + 4) return;
    /* One horizontal line per scanline with an interpolated color: pure
     * GDI, no msimg32 dependency inside this window (the project's other
     * popups paint the same way), and trivially correct at any DPI because
     * the row rects are already device pixels. */
    for (int y = r.top + 2; y < r.bottom - 2; ++y) {
        const int t = (y - r.top) * 256 / h;
        const COLORREF c = RGB(
            GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * t / 256,
            GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * t / 256,
            GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * t / 256);
        const UniqueGdiObject pen(CreatePen(PS_SOLID, 1, c));
        if (!pen.valid()) continue;
        const SelectGuard sg(hdc, (HGDIOBJ)pen.get());
        MoveToEx(hdc, r.left + 3, y, nullptr);
        LineTo(hdc, r.right - 3, y);
    }
    const UniqueGdiObject epen(CreatePen(PS_SOLID, 1, edge));
    if (epen.valid()) {
        const SelectGuard esg(hdc, (HGDIOBJ)epen.get());
        const int l = r.left + 2, t2 = r.top + 1;
        const int rr = r.right - 3, bb = r.bottom - 2;
        MoveToEx(hdc, l, t2, nullptr);   LineTo(hdc, rr + 1, t2);
        MoveToEx(hdc, l, bb, nullptr);   LineTo(hdc, rr + 1, bb);
        MoveToEx(hdc, l, t2, nullptr);   LineTo(hdc, l, bb);
        MoveToEx(hdc, rr, t2, nullptr);  LineTo(hdc, rr, bb);
    }
}

void JumpListWindow::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (hdc == nullptr) return;
    RECT client{};
    GetClientRect(hwnd, &client);

    const UniqueGdiObject bg(CreateSolidBrush(RGB(0xF2, 0xF6, 0xFB)));
    if (bg.valid()) FillRect(hdc, &client, (HBRUSH)bg.get());

    SetBkMode(hdc, TRANSPARENT);
    const int fontH = -::MulDiv(11, (int)m_dpi, 96);
    const UniqueGdiObject font(CreateFontW(fontH, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    const UniqueGdiObject fontBold(CreateFontW(fontH, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"));
    if (font.valid()) SelectObject(hdc, (HGDIOBJ)font.get());

    auto hline = [&](int lineY) {
        const UniqueGdiObject pen(
            CreatePen(PS_SOLID, 1, RGB(0xC9, 0xD6, 0xE6)));
        if (!pen.valid()) return;
        const SelectGuard pg(hdc, (HGDIOBJ)pen.get());
        MoveToEx(hdc, Sc(10), lineY, nullptr);
        LineTo(hdc, client.right - Sc(10), lineY);
    };

    const JumpStr& S = Str(m_lang);
    const int margin = Sc(12);
    int lastKind = -1;

    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const bool isDoc =
            r.kind == Row::DocRecent || r.kind == Row::DocFrequent;
        const bool isTask = r.kind == Row::Task;

        if ((isDoc || isTask) && (int)r.kind != lastKind) {
            /* Section header ("Recent items" / "Frequent items" /
             * "Tasks"): bold steel-blue with the separator under it -
             * the Windows 7 jump list band. Its y comes from the row
             * band Layout left empty right above this row. */
            const int hy = r.rect.top - Sc(kHeader96);
            if (fontBold.valid()) SelectObject(hdc, (HGDIOBJ)fontBold.get());
            SetTextColor(hdc, RGB(0x40, 0x58, 0x78));
            RECT hr{ margin, hy, client.right - margin, hy + Sc(kHeader96) };
            const wchar_t* header = isTask ? S.tasks
                : (r.kind == Row::DocFrequent ? S.frequent : S.recent);
            DrawTextW(hdc, header, -1, &hr,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            if (font.valid()) SelectObject(hdc, (HGDIOBJ)font.get());
            hline(hy + Sc(kHeader96));
        }
        if (!isDoc && (lastKind == (int)Row::DocRecent ||
                       lastKind == (int)Row::DocFrequent ||
                       lastKind == (int)Row::App ||
                       lastKind == (int)Row::Close ||
                       lastKind == (int)Row::Pin)) {
            hline(r.rect.top - Sc(kSep96) / 2);
        }
        lastKind = (int)r.kind;

        if ((int)i == m_hover) {
            GradientRect(hdc, r.rect,
                         RGB(0xED, 0xF6, 0xFD), RGB(0xC5, 0xE1, 0xF7),
                         RGB(0x94, 0xC6, 0xEE));
        }

        const int iconLeft = Sc(14);
        /* Tasks share the document column (they carry no icon; the
         * column keeps the text aligned). */
        const int textLeft = (isDoc || r.kind == Row::Task)
            ? iconLeft + Sc(kDocIcon96) + Sc(6)
            : (r.kind == Row::App
                ? iconLeft + Sc(kAppIcon96) + Sc(9)
                : (r.kind == Row::Close
                    ? iconLeft + Sc(kClose96) + Sc(7)
                    : (r.kind == Row::Pin
                        ? iconLeft + Sc(kPinIcon96) + Sc(7)
                        : iconLeft)));
        SetTextColor(hdc, (r.kind == Row::Pin) ? RGB(0x1E, 0x6F, 0xC9)
                                                : RGB(0x1E, 0x1E, 0x1E));
        const RECT closeRect = CloseRect();
        RECT tr{ textLeft, r.rect.top, client.right - margin, r.rect.bottom };
        DrawTextW(hdc, r.label.c_str(), -1, &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

        if (isDoc && r.icon.get() != nullptr) {
            const int box = Sc(kDocIcon96);
            DrawIconEx(hdc, iconLeft, r.rect.top + (Sc(kRowDoc96) - box) / 2,
                       r.icon.get(), box, box, 0, nullptr, DI_NORMAL);
        }
        if (r.kind == Row::App && m_appIcon) {
            const int box = Sc(kAppIcon96);
            DrawBitmapScaled(hdc, m_appIcon.get(), box, box,
                             iconLeft,
                             r.rect.top + (Sc(kRowApp96) - box) / 2);
        }
        if (r.kind == Row::Pin && m_pinIcon) {
            const int box = Sc(kPinIcon96);
            DrawBitmapScaled(hdc, m_pinIcon.get(), box, box,
                             iconLeft,
                             r.rect.top + (Sc(kRowPin96) - box) / 2);
        }
        if (r.kind == Row::Close && closeRect.right > closeRect.left) {
            HBITMAP close = m_closeNormal.get();
            if (m_closeDown && m_closePressed) close = m_closePressed.get();
            else if (m_closeHot && m_closeHover) close = m_closeHover.get();
            DrawBitmapScaled(hdc, close,
                             closeRect.right - closeRect.left,
                             closeRect.bottom - closeRect.top,
                             closeRect.left, closeRect.top);
        }
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK JumpListWindow::WndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    W7T_SEH_TRY {
        switch (msg) {
            case WM_PAINT:
                Instance().OnPaint(hwnd);
                return 0;
            case WM_ERASEBKGND:
                return 1;   /* the paint pass fills every band itself */
            case WM_MOUSEMOVE: {
                JumpListWindow& j = Instance();
                if (j.IsVisible()) {
                    POINT cl{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    const int hit = j.HitRowClient(cl);
                    const bool closeHot = j.HitCloseClient(cl);
                    if (hit != j.m_hover || closeHot != j.m_closeHot) {
                        j.m_hover = hit;
                        j.m_closeHot = closeHot;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    /* The Windows 7 row tooltip: the full path under the
                     * hovered row (the name only when the item has no
                     * resolvable path - NoJumpListPathTooltip). */
                    if (hit >= 0) j.ShowRowTooltip(hit, cl);
                    else j.ClearRowTooltip();
                    TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                    TrackMouseEvent(&tme);
                }
                return 0;
            }
            case WM_MOUSELEAVE: {
                JumpListWindow& j = Instance();
                j.m_hover = -1;
                j.m_closeHot = false;
                j.ClearRowTooltip();
                if (!j.m_closeDown) InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            case WM_LBUTTONDOWN: {
                JumpListWindow& j = Instance();
                POINT cl{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (j.HitCloseClient(cl)) {
                    j.m_closeDown = true;
                    j.m_closeHot = true;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            case WM_LBUTTONUP: {
                JumpListWindow& j = Instance();
                if (!j.IsVisible()) return 0;
                POINT cl{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                if (j.m_closeDown) {
                    const bool close = j.HitCloseClient(cl);
                    j.m_closeDown = false;
                    if (GetCapture() == hwnd) ReleaseCapture();
                    if (close) {
                        j.CloseRunningApplication();
                        j.Hide();
                    } else {
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
                POINT sc = cl;
                ClientToScreen(hwnd, &sc);
                int32_t bits = 0;
                j.ActivateRow(sc.x, sc.y, &bits);
                return 0;
            }
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE && Instance().m_interactive)
                    Instance().Hide();
                break;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) Instance().Hide();
                return 0;
            case kDismissOutsideMessage:
                Instance().Hide();
                return 0;
            case WM_NCDESTROY:
                /* Window gone: drop everything bound to it so a later open
                 * starts from a clean state (no stuck hover, no dangling
                 * HWND, no orphan handles). The tooltip child dies with
                 * the parent; only the bookkeeping stays here. */
                Instance().m_hwnd = nullptr;
                Instance().m_hover = -1;
                Instance().m_popupRect = RECT{};
                Instance().m_area = RECT{};
                Instance().ClearContent();
                Instance().m_appIcon.reset();
                Instance().m_pinIcon.reset();
                Instance().m_tooltip = nullptr;
                Instance().m_tipShown = false;
                Instance().m_tipRow = -1;
                Instance().m_tipStart = 0;
                return 0;
        }
    } W7T_SEH_CATCH {
    } W7T_SEH_END
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace w7t
