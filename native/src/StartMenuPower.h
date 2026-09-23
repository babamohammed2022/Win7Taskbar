/*
 * Win7Taskbar - Start Menu power / session actions
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 */
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>

namespace w7t {
namespace startmenu {

enum class PowerAction : int {
    Shutdown = 0,
    Restart = 1,
    Sleep = 2,
    Hibernate = 3,
    LogOff = 4,
    Lock = 5,
    SwitchUser = 6,
};

bool RunPowerAction(PowerAction action, std::wstring& error);

} /* namespace startmenu */
} /* namespace w7t */
