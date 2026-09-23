/*
 * Win7Taskbar - Start Menu program scanner
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Enumerates per-user and all-users Start Menu Programs plus AppsFolder
 * (UWP). User entries win on duplicate targets (stable_sort + explicit
 * walk, never std::unique). SHGetKnownFolderPath is preferred; CSIDL is
 * the fallback.
 */
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "StartMenuIndex.h"

#include <mutex>
#include <vector>

namespace w7t {
namespace startmenu {

bool ScanPrograms(std::vector<IndexedApp>& out, std::wstring& error);

class ProgramCache {
public:
    bool Refresh(std::wstring& error);
    std::vector<IndexedApp> Snapshot() const;
    std::size_t Count() const;

private:
    mutable std::mutex mutex_;
    std::vector<IndexedApp> apps_;
};

} /* namespace startmenu */
} /* namespace w7t */
