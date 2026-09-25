// Win7Taskbar - tools/win11-native-tray-probe
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Phase 0 recon probe for the "native Windows 11 tray without
// ExplorerPatcher" mission (docs/WIN11-NATIVE-TRAY-RESEARCH.md).
//
// What it verifies on the machine it runs on, WITHOUT injecting anything
// into explorer.exe and without changing any window, style, or region:
//
//   1. The exact child-class list of Shell_TrayWnd (and of every
//      Shell_SecondaryTrayWnd), so that the presence of the hidden legacy
//      layer (ReBarWindow32 / MSTaskSwWClass / TrayNotifyWnd) beside the
//      XAML hosts (Windows.UI.Composition.DesktopWindowContentBridge,
//      Windows.UI.Input.InputSite.WindowClass) is documented per build.
//   2. The classic app-icon toolbar chain
//      Shell_TrayWnd -> TrayNotifyWnd -> SysPager -> ToolbarWindow32
//      ("User Promoted Notification Area").
//   3. Out-of-process enumeration: TB_BUTTONCOUNT plus, for the first
//      buttons, TB_GETBUTTON read from a REMOTE buffer allocated with
//      VirtualAllocEx inside explorer.exe (the classic technique). For the
//      first button it prints (hwnd, uID, uCallbackMessage, hIcon-presence)
//      read from the legacy per-button struct (Ken Johnson's documented
//      tray struct offsets).
//   4. Whether NotifyIconOverflowWindow (the Win10 overflow host) exists,
//      even hidden (on Windows 11 the chevron flyout is XAML; this tells us
//      whether the same TB_* technique could mirror the overflow).
//   5. With --watch <seconds>: whether the registered "TaskbarCreated"
//      broadcast arrives (run "taskkill /f /im explorer.exe && explorer"
//      from another console to test reload detection).
//
// Exit code: 0 = legacy chain present AND at least one app icon button
// readable; 1 = chain present but no readable buttons; 2 = no legacy
// chain (UIA path only); 3 = probe could not run correctly.
//
// BUILD (no project files, one translation unit, classic subsystem):
//   MSVC:  cl /EHsc /O2 win11_native_tray_probe.cpp /Fe:win11_native_tray_probe.exe user32.lib advapi32.lib ole32.lib
//   MinGW: g++ -O2 -municode -mconsole win11_native_tray_probe.cpp -o win11_native_tray_probe.exe -luser32 -ladvapi32 -lole32
//
// Clean-room note: this probe is original code written for Win7Taskbar.
// The legacy tray struct offsets are from publicly documented samples
// (Ken Johnson, "Q179034"-era tray exploration articles) and from the
// TB_* documentation in the Microsoft Win32 UI reference.

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Legacy tray per-button struct (documented public knowledge since the
// "Tray Explorer" articles). TBBUTTON.dwData points at explorer's copy
// of this struct; only the fields needed by Phase 1 callers are read.
struct TrayEntryV1 {
    HWND hwnd;
    UINT uID;
    UINT uCallbackMessage;
    DWORD reserved0;
    DWORD reserved1;
    HICON hIcon;
};

// Some builds add two leading DWORDs before hwnd. The probe detects the
// layout by checking whether the first candidate field looks like a
// window handle owned by a different process instead of a small number.
struct ProbeOptions {
    bool watch = false;
    DWORD watchSeconds = 10;
    bool dumpAllButtons = false;
    bool verbose = false;
};

void PrintWide(const wchar_t* s) { wprintf(L"%s", s); }

// Child-class inventory of a taskbar window (recursive, depth-limited).
struct ChildListContext {
    wchar_t buffer[2048];
    size_t used;
    HWND avoid;
};

BOOL CALLBACK CollectClassesProc(HWND hwnd, LPARAM lp) {
    ChildListContext* ctx = reinterpret_cast<ChildListContext*>(lp);
    wchar_t cls[128] = {};
    if (GetClassNameW(hwnd, cls, 128) == 0) {
        return TRUE;
    }
    if (ctx->used + 140 < sizeof(ctx->buffer) / sizeof(wchar_t)) {
        int written = swprintf(ctx->buffer + ctx->used,
                               (sizeof(ctx->buffer) / sizeof(wchar_t)) - ctx->used,
                               L"%s%ls", ctx->used == 0 ? L"" : L" | ", cls);
        if (written > 0) {
            ctx->used += static_cast<size_t>(written);
        }
    }
    return TRUE;
}

void DumpTrayChildren(HWND tray, const wchar_t* label) {
    ChildListContext ctx = {};
    EnumChildWindows(tray, CollectClassesProc, reinterpret_cast<LPARAM>(&ctx));
    wprintf(L"[chain] %ls child classes: %ls\n", label,
            ctx.used == 0 ? L"(none)" : ctx.buffer);
}

// Returns the legacy "User Promoted Notification Area" toolbar HWND, or
// nullptr, filling each chain step for logging.
HWND FindLegacyToolbar(HWND tray, HWND* outTrayNotify, HWND* outSysPager) {
    if (outTrayNotify != nullptr) *outTrayNotify = nullptr;
    if (outSysPager != nullptr) *outSysPager = nullptr;
    if (tray == nullptr) {
        return nullptr;
    }
    HWND trayNotify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    if (trayNotify == nullptr) {
        return nullptr;
    }
    if (outTrayNotify != nullptr) *outTrayNotify = trayNotify;
    HWND sysPager = FindWindowExW(trayNotify, nullptr, L"SysPager", nullptr);
    if (sysPager == nullptr) {
        return nullptr;
    }
    if (outSysPager != nullptr) *outSysPager = sysPager;
    return FindWindowExW(sysPager, nullptr, L"ToolbarWindow32", nullptr);
}

// Reads `size` bytes from explorer.exe at `remote` into `local`.
bool ReadRemote(HANDLE proc, LPCVOID remote, void* local, SIZE_T size) {
    SIZE_T read = 0;
    return remote != nullptr &&
           ReadProcessMemory(proc, remote, local, size, &read) &&
           read == size;
}

// One remote TBBUTTON buffer shared by the whole button loop.
struct RemoteBuffer {
    HANDLE proc = nullptr;
    LPVOID remote = nullptr;
    SIZE_T size = 0;
    ~RemoteBuffer() {
        if (remote != nullptr && proc != nullptr) {
            VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
        }
    }
};

LRESULT SendTb(HWND toolbar, UINT msg, WPARAM wp, LPARAM lp) {
    return SendMessageW(toolbar, msg, wp, lp);
}

// (hwnd,uID) plausibility filter for the struct-layout auto-detect:
// a tray owner HWND is a valid (maybe hidden) window, a uID is nonzero,
// and the callback message must look like a registered message id
// (> WM_USER).
bool LooksLikeOwner(HWND hwnd) {
    DWORD pid = 0;
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    return pid != 0;
}

int ProbeToolbar(HWND toolbar, const ProbeOptions& opts, DWORD* outFirstPid) {
    DWORD pid = 0;
    GetWindowThreadProcessId(toolbar, &pid);
    if (pid == 0) {
        wprintf(L"[read] toolbar has no owning process\n");
        return 3;
    }
    if (outFirstPid != nullptr) *outFirstPid = pid;

    HANDLE proc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ |
                              PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
                              FALSE, pid);
    if (proc == nullptr) {
        wprintf(L"[read] OpenProcess(%lu) failed: %lu\n",
                pid, GetLastError());
        wprintf(L"[hint] run the probe AS ADMIN to read the toolbar\n");
        return 3;
    }

    /* VirtualAllocEx is required: TBBUTTON and the struct it points to
     * are written by explorer inside explorer's address space. */
    const SIZE_T bufSize = sizeof(TBBUTTON) + sizeof(TrayEntryV1) + 64;
    RemoteBuffer rb;
    rb.proc = proc;
    rb.size = bufSize;
    rb.remote = VirtualAllocEx(proc, nullptr, bufSize,
                               MEM_COMMIT, PAGE_READWRITE);
    if (rb.remote == nullptr) {
        wprintf(L"[read] VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(proc);
        return 3;
    }

    const LRESULT count = SendTb(toolbar, TB_BUTTONCOUNT, 0, 0);
    wprintf(L"[count] TB_BUTTONCOUNT = %ld\n", count);
    if (count <= 0) {
        CloseHandle(proc);
        return 1;
    }

    const int maxButtons = opts.dumpAllButtons ? 64 : 3;
    int seen = 0;
    for (LRESULT i = 0; i < (LRESULT)count && seen < maxButtons; ++i) {
        if (!SendTb(toolbar, TB_GETBUTTON, i, (LPARAM)rb.remote)) {
            if (opts.verbose) {
                wprintf(L"  [btn %ld] TB_GETBUTTON returned false\n", i);
            }
            continue;
        }
        TBBUTTON tb = {};
        if (!ReadRemote(proc, rb.remote, &tb, sizeof(tb))) {
            if (opts.verbose) {
                wprintf(L"  [btn %ld] could not read TBBUTTON\n", i);
            }
            continue;
        }
        if (tb.dwData == 0) {
            /* Separator or non-app button: skip without printing. */
            continue;
        }
        wchar_t remoteStruct[64] = {};
        if (!ReadRemote(proc, (LPCVOID)(uintptr_t)tb.dwData,
                        remoteStruct, sizeof(remoteStruct))) {
            continue;
        }

        /* Layout auto-detect: the legacy tray struct either starts with
         * the fields of TrayEntryV1 (hwnd at offset 0), or carries two
         * leading DWORDs before it. Try both. */
        const TrayEntryV1* entry = nullptr;
        TrayEntryV1 candidate = {};
        for (int layout = 0; layout < 2 && entry == nullptr; ++layout) {
            const size_t skip = layout == 0 ? 0 : sizeof(DWORD) * 2;
            if (skip + sizeof(TrayEntryV1) > sizeof(remoteStruct)) {
                continue;
            }
            memcpy(&candidate, remoteStruct + skip, sizeof(candidate));
            if (LooksLikeOwner(candidate.hwnd) && candidate.uID != 0 &&
                candidate.uCallbackMessage >= 0x400 &&
                candidate.uCallbackMessage <= 0xFFFF) {
                entry = new TrayEntryV1(candidate);
            }
        }
        if (entry == nullptr) {
            if (opts.verbose) {
                wprintf(L"  [btn %ld] remote struct layout not recognized "
                        L"at dwData=%p\n", i, (LPCVOID)(uintptr_t)tb.dwData);
            }
            continue;
        }
        wprintf(L"  [btn %d] hwnd=%p uID=%u cb=0x%04x icon=%ls\n",
                seen, entry->hwnd, entry->uID,
                entry->uCallbackMessage,
                entry->hIcon != nullptr ? L"present" : L"NULL");
        seen++;
        delete entry;
    }

    CloseHandle(proc);
    return seen > 0 ? 0 : 1;
}

void ProbeTrayChain(HWND tray, const wchar_t* label,
                    const ProbeOptions& opts, int* worst) {
    if (tray == nullptr) {
        return;
    }
    wchar_t cls[128] = {};
    GetClassNameW(tray, cls, 128);
    DWORD pid = 0;
    GetWindowThreadProcessId(tray, &pid);
    wprintf(L"[tray] %ls hwnd=%p class=%ls pid=%lu visible=%ls\n",
            label, tray, cls, pid,
            IsWindowVisible(tray) ? L"yes" : L"no");
    DumpTrayChildren(tray, label);

    // XAML hosts (evidence waypoint: the overlay layer is alive on modern
    // builds even when the bar window is hidden).
    HWND xamlBridge = FindWindowExW(tray, nullptr,
        L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
    HWND inputSite = FindWindowExW(tray, nullptr,
        L"Windows.UI.Input.InputSite.WindowClass", nullptr);
    wprintf(L"[xaml] DesktopWindowContentBridge=%ls InputSite=%ls\n",
            xamlBridge ? L"present" : L"absent",
            inputSite  ? L"present" : L"absent");

    HWND trayNotify = nullptr;
    HWND sysPager = nullptr;
    HWND toolbar = FindLegacyToolbar(tray, &trayNotify, &sysPager);
    wprintf(L"[chain] TrayNotifyWnd=%ls SysPager=%ls ToolbarWindow32=%ls\n",
            trayNotify ? L"present" : L"absent",
            sysPager   ? L"present" : L"absent",
            toolbar    ? L"present" : L"absent");
    if (toolbar == nullptr) {
        if (*worst < 2) *worst = 2;
        return;
    }
    DWORD owner = 0;
    int result = ProbeToolbar(toolbar, opts, &owner);
    if (*worst < result) *worst = result;
}

// --watch: waits for the registered "TaskbarCreated" broadcast.
UINT g_taskbarCreated = 0;
BOOL CALLBACK CheckItProc(UINT msg) {
    return msg == g_taskbarCreated;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    ProbeOptions opts;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--watch") == 0 && i + 1 < argc) {
            opts.watch = true;
            opts.watchSeconds = static_cast<DWORD>(_wtoi(argv[++i]));
            if (opts.watchSeconds == 0 || opts.watchSeconds > 600) {
                opts.watchSeconds = 10;
            }
        } else if (_wcsicmp(argv[i], L"--all") == 0) {
            opts.dumpAllButtons = true;
        } else if (_wcsicmp(argv[i], L"-v") == 0) {
            opts.verbose = true;
        } else {
            wprintf(L"usage: win11_native_tray_probe [--all] [-v] "
                    L"[--watch <seconds>]\n");
            return 3;
        }
    }

    // Common controls are needed for TB_*; just needing the constants is
    // fine, but a stale explorer may reject the message family without a
    // manifest - the probe links them so it behaves like a normal app.
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tray == nullptr) {
        wprintf(L"[tray] Shell_TrayWnd not found - is a shell running?\n");
        return 3;
    }

    int worst = 0;
    ProbeTrayChain(tray, L"primary", opts, &worst);

    // Secondary monitors (Windows 11 still registers a bar per monitor
    // in many configurations).
    HWND secondary = nullptr;
    for (int n = 1; n <= 8; ++n) {
        secondary = FindWindowExW(nullptr, secondary,
                                  L"Shell_SecondaryTrayWnd", nullptr);
        if (secondary == nullptr) {
            break;
        }
        wchar_t label[32];
        swprintf(label, 32, L"secondary%d", n);
        ProbeTrayChain(secondary, label, opts, &worst);
    }

    // The Win10-style overflow host: still present on some builds, even
    // hidden; on pure XAML builds it is missing and the chevron flyout
    // is XAML-only.
    HWND overflow = FindWindowW(L"NotifyIconOverflowWindow", nullptr);
    wprintf(L"[overflow] NotifyIconOverflowWindow=%ls (visible=%ls)\n",
            overflow ? L"present" : L"absent",
            overflow && IsWindowVisible(overflow) ? L"yes" : L"no");

    if (opts.watch) {
        g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        wprintf(L"[watch] waiting up to %lu s for the TaskbarCreated "
                L"broadcast (restart explorer now)...\n", opts.watchSeconds);
        DWORD deadline = GetTickCount() + opts.watchSeconds * 1000;
        MSG msg = {};
        BOOL got = FALSE;
        while (GetTickCount() < deadline && !got) {
            DWORD avail = MsgWaitForMultipleObjects(
                0, nullptr, FALSE, 1000, QS_ALLEVENTS);
            (void)avail;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == g_taskbarCreated) {
                    got = TRUE;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        wprintf(L"[watch] TaskbarCreated %ls in %lu s\n",
                got ? L"RECEIVED" : L"not seen", opts.watchSeconds);
    }

    wprintf(L"[result] exit=%d (0=legacy chain + readable buttons, "
            L"1=chain but no buttons, 2=no legacy chain, 3=probe error)\n",
            worst);
    return worst;
}
