/*
 * Win7Taskbar - Start Menu search index
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "StartMenuIndex.h"

#include <algorithm>
#include <climits>
#include <cwctype>

namespace w7t {
namespace startmenu {

std::wstring FoldAscii(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size());
    for (wchar_t c : s) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        } else if (c > 127) {
            c = static_cast<wchar_t>(towlower(c));
        }
        o.push_back(c);
    }
    return o;
}

static bool IsWordStart(const std::wstring& name, std::size_t pos) {
    if (pos == 0) {
        return true;
    }
    const wchar_t prev = name[pos - 1];
    return prev == L' ' || prev == L'\t' || prev == L'-' || prev == L'_' ||
           prev == L'.' || prev == L'\\' || prev == L'/';
}

int RankMatch(const std::wstring& foldedName, const std::wstring& foldedQuery,
              int usageCount) {
    if (foldedQuery.empty()) {
        return INT_MAX;
    }
    if (foldedName == foldedQuery) {
        return 0;
    }
    if (foldedName.size() >= foldedQuery.size() &&
        foldedName.compare(0, foldedQuery.size(), foldedQuery) == 0) {
        return 10;
    }
    const std::size_t pos = foldedName.find(foldedQuery);
    if (pos == std::wstring::npos) {
        return INT_MAX;
    }
    int rank = IsWordStart(foldedName, pos) ? 20 : 40;
    if (usageCount > 0) {
        const int boost = usageCount > 100 ? 8 : (usageCount / 15);
        rank -= boost;
        if (rank < 1) {
            rank = 1;
        }
    }
    return rank;
}

bool QueryIndex(const std::vector<IndexedApp>& apps,
                const std::wstring& query,
                std::vector<RankedHit>& out,
                const volatile std::uint32_t* generation,
                std::uint32_t expectedGeneration,
                std::size_t maxHits) {
    out.clear();
    const std::wstring foldedQuery = FoldAscii(query);
    if (foldedQuery.empty()) {
        return true;
    }
    out.reserve(64);
    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (generation && *generation != expectedGeneration) {
            out.clear();
            return false;
        }
        const int rank = RankMatch(FoldAscii(apps[i].name), foldedQuery,
                                   apps[i].usageCount);
        if (rank == INT_MAX) {
            continue;
        }
        RankedHit hit;
        hit.index = static_cast<int>(i);
        hit.rank = rank;
        out.push_back(hit);
    }
    std::stable_sort(out.begin(), out.end(),
                     [&](const RankedHit& a, const RankedHit& b) {
                         if (a.rank != b.rank) {
                             return a.rank < b.rank;
                         }
                         return apps[static_cast<std::size_t>(a.index)].name <
                                apps[static_cast<std::size_t>(b.index)].name;
                     });
    if (out.size() > maxHits) {
        out.resize(maxHits);
    }
    return true;
}

} /* namespace startmenu */
} /* namespace w7t */
