/* Win7Taskbar - native core - TaskbarButtonCreated (implementation)
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 */

#include "TaskbarButtonNotify.h"
#include "AppBarService.h"
#include "Common.h"   /* w7t::LogTagged */

#include <mutex>
#include <unordered_set>

namespace w7t {
namespace {

std::mutex g_mutex;
UINT g_msg = 0;
bool g_resolved = false;
std::unordered_set<DWORD> g_notifiedPids;

constexpr DWORD kPidSetCap = 4096;

UINT Resolve() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_resolved) {
        g_msg = ::RegisterWindowMessageW(L"TaskbarButtonCreated");
        g_resolved = true;
        if (g_msg == 0) {
            LogTagged(L"TASKBAR",
                      L"TaskbarButtonCreated: registration failed, no notification");
        }
    }
    return g_msg;
}

} /* namespace */

void NotifyTaskbarButton(HWND hwnd, DWORD pid) {
    (void)hwnd;
    if (pid == 0) {
        return;
    }

    /* The native taskbar owns the buttons: nothing to announce. */
    if (AppBarService::Instance().IsNativeTaskbarHidden() == false) {
        return;
    }

    const UINT msg = Resolve();
    if (msg == 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_notifiedPids.count(pid) != 0) {
            return;
        }
        if (g_notifiedPids.size() >= kPidSetCap) {
            g_notifiedPids.clear();
        }
        g_notifiedPids.insert(pid);
    }

    /* Fire-and-forget: HWND_BROADCAST never blocks on a dead receiver. */
    ::PostMessageW(HWND_BROADCAST, msg, 0, 0);
    LogTagged(L"TASKBAR",
              L"TaskbarButtonCreated broadcast (pid=%lu, hwnd=%p, msg=0x%x)",
              (unsigned long)pid, (void*)hwnd, (unsigned)msg);
}

void ResetTaskbarButtonNotify() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_notifiedPids.clear();
}

UINT GetTaskbarButtonMessageId() {
    return Resolve();
}

bool TaskbandProbeEnabled() {
    static const bool enabled = [] {
        /* Win32 lookup: no CRT "unsafe" deprecation, no allocation.
         * n = chars written (0 = absent, required size = too small). */
        wchar_t buf[8]{};
        const DWORD n =
            ::GetEnvironmentVariableW(L"W7T_TASKBAND_PROBE", buf, 8);
        return n > 0 && n < 8 && buf[0] != L'0';
    }();
    return enabled;
}

} /* namespace w7t */
