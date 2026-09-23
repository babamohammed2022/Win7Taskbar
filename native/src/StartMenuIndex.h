/*
 * Win7Taskbar - Start Menu search index
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Prefix / substring / recently-used ranking. Pure C++ so the unit tests
 * can run without COM. Target: Query() < 16 ms on 5 000 entries.
 */
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace w7t {
namespace startmenu {

struct IndexedApp {
    std::wstring name;
    std::wstring path;
    std::wstring target;
    std::wstring folder;
    int source = 0;          /* 0 = per-user, 1 = all-users, 2 = UWP */
    int usageCount = 0;
};

struct RankedHit {
    int index = -1;
    int rank = 0;
};

std::wstring FoldAscii(const std::wstring& s);

/* Lower rank is better. rank == INT_MAX means no match. */
int RankMatch(const std::wstring& foldedName, const std::wstring& foldedQuery,
              int usageCount);

/*
 * Fill `out` with hits sorted by rank then name. `generation` is a
 * cancellation token: if *generation != expectedGeneration the walk
 * aborts early and returns false.
 */
bool QueryIndex(const std::vector<IndexedApp>& apps,
                const std::wstring& query,
                std::vector<RankedHit>& out,
                const volatile std::uint32_t* generation,
                std::uint32_t expectedGeneration,
                std::size_t maxHits = 64);

} /* namespace startmenu */
} /* namespace w7t */
