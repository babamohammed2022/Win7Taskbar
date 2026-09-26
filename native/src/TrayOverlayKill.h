/* Win7Taskbar - installer of W7TTrayOverlayKill.dll into explorer.exe
 * Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
 *
 * Product-integrated injection (same LoadLibrary + SetWindowsHookEx
 * WH_CALLWNDPROC pattern as W7TInject). Lives in the native core; the
 * payload DLL is a separate module so a fault inside Explorer cannot
 * unwind our process.
 */

#pragma once

#include <windows.h>
#include <cstdint>

namespace w7t {

void TrayOverlayKill_SetEnabled(bool enabled);
bool TrayOverlayKill_Enabled();
void TrayOverlayKill_Install();
void TrayOverlayKill_Uninstall();
void TrayOverlayKill_OnTaskbarCreated();
void TrayOverlayKill_OnSessionEnding();
void TrayOverlayKill_PushWorkArea(int32_t edge, const RECT& barRect,
                                  const RECT& workRect);

} /* namespace w7t */
