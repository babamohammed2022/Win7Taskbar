/*
 * Win7Taskbar - Start Menu search index
 * Copyright (c) 2026 Win7Taskbar contributors
 * Licensed under the GNU General Public License version 3 or later.
 *
 * Tokenized matching inspired by Open-Shell's public behavior: every
 * space-separated token of the query must match, either as a word
 * prefix (better rank) or as a substring (worse rank). NLS comparison
 * (FindNLSStringEx, case/diacritic-insensitive) preferred, ASCII fold
 * as the fallback. Written from scratch - Open-Shell source not copied.
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "StartMenuIndex.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cwctype>
#include <vector>

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
    /* Same separator idea used by the classic menu search: a word begins
     * after whitespace or one of the usual punctuation delimiters. The
     * list is author-local (method signature and flags are public API,
     * not source borrowed from anywhere). */
    const wchar_t prev = name[pos - 1];
    return prev == L' ' || prev == L'\t' || prev == L'-' || prev == L'_' ||
           prev == L'.' || prev == L',' || prev == L'$' || prev == L'&' ||
           prev == L'[' || prev == L']' || prev == L'{' || prev == L'}' ||
           prev == L'(' || prev == L')' || prev == L';' || prev == L'|' ||
           prev == L'\\' || prev == L'/';
}

/* Words are split off spaces and the punctuation set above. */
bool IsSearchWordSeparator(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'.' || c == L',' || c == L'$' ||
           c == L'&' || c == L'[' || c == L']' || c == L'{' || c == L'}' ||
           c == L'(' || c == L')' || c == L';' || c == L'|';
}

std::vector<std::wstring> SplitSearchTokens(const std::wstring& s) {
    std::vector<std::wstring> tokens;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && IsSearchWordSeparator(s[i])) {
            ++i;
        }
        std::size_t start = i;
        while (i < s.size() && !IsSearchWordSeparator(s[i])) {
            ++i;
        }
        if (i > start) {
            tokens.push_back(s.substr(start, i - start));
        }
    }
    return tokens;
}

namespace {

std::wstring TrimSearchText(const std::wstring& s) {
    std::size_t b = 0;
    while (b < s.size() && IsSearchWordSeparator(s[b])) {
        ++b;
    }
    std::size_t e = s.size();
    while (e > b && IsSearchWordSeparator(s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

/* Unicode-aware prefix / substring test on UNKNOWN-case text. Public NLS
 * API; on failure the caller falls back to the ASCII-folter version. */
bool NlsStartsWithCi(const wchar_t* text, const wchar_t* token) {
    if (text == nullptr || token == nullptr) {
        return false;
    }
    const DWORD flags = FIND_STARTSWITH | LINGUISTIC_IGNORECASE |
                        LINGUISTIC_IGNOREDIACRITIC;
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, flags, text, -1,
                           token, -1, nullptr, nullptr, nullptr, 0) >= 0;
}

bool WordPrefixMatchNls(const std::wstring& name, const std::wstring& token) {
    std::size_t pos = 0;
    while (pos < name.size()) {
        while (pos < name.size() && IsSearchWordSeparator(name[pos])) {
            ++pos;
        }
        if (pos >= name.size()) {
            break;
        }
        try {
            if (NlsStartsWithCi(name.c_str() + pos, token.c_str())) {
                return true;
            }
        } catch (...) {
            /* the host NLS service refused the call: caller must fall back */
            throw;
        }
        while (pos < name.size() && !IsSearchWordSeparator(name[pos])) {
            ++pos;
        }
    }
    return false;
}

bool WordPrefixMatchFolded(const std::wstring& foldedName,
                           const std::wstring& foldedToken) {
    if (foldedToken.empty()) {
        return true;
    }
    std::size_t pos = 0;
    const std::size_t tl = foldedToken.size();
    while ((pos = foldedName.find(foldedToken, pos)) != std::wstring::npos) {
        if (IsWordStart(foldedName, pos)) {
            return true;
        }
        ++pos;
    }
    (void)tl;
    return false;
}

/* 0: no match. 1: every token is a substring somewhere in the name.
 * 2: every token is a word prefix. 3: the (trimmed) name starts with the
 * whole query. Written from scratch; behavior mirrors what the classic
 * menu search presents to the user. */
int TokensMatch(const std::wstring& name, const std::wstring& query,
                const std::wstring& foldedName,
                const std::wstring& foldedQuery,
                const std::vector<std::wstring>& queryTokens) {
    if (foldedQuery.empty()) {
        return 0;
    }
    if (foldedName.size() >= foldedQuery.size() &&
        foldedName.compare(0, foldedQuery.size(), foldedQuery) == 0) {
        return 3;
    }
    bool allPrefix = true;
    bool allSub = true;
    bool nlsOk = true;
    for (const std::wstring& token : queryTokens) {
        bool prefix = false;
        if (nlsOk) {
            try {
                prefix = WordPrefixMatchNls(name, token);
            } catch (...) {
                nlsOk = false;
            }
        }
        if (!nlsOk) {
            prefix = WordPrefixMatchFolded(foldedName, FoldAscii(token));
        }
        const bool sub = FoldAscii(token).size() <= foldedName.size() &&
                         foldedName.find(FoldAscii(token)) != std::wstring::npos;
        if (!prefix) {
            allPrefix = false;
        }
        if (!sub) {
            allSub = false;
            break;
        }
    }
    if (allPrefix) {
        return 2;
    }
    return allSub ? 1 : 0;
}

} /* namespace */

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
        /* Whole-query substring failed: the multi-token path in
         * RankMatchAll decides whether the item still qualifies. */
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

int RankMatchAll(const std::wstring& name, const std::wstring& query,
                 int usageCount) {
    const std::wstring foldedName = FoldAscii(name);
    /* Trim spaces so a stray leading blank does not kill every match. */
    const std::wstring trimmed = TrimSearchText(query);
    const std::wstring foldedQuery = FoldAscii(trimmed);
    if (foldedQuery.empty()) {
        return INT_MAX;
    }

    /* Fast path: the whole query is one contiguous run in the name. */
    const int quick = RankMatch(foldedName, foldedQuery, usageCount);
    if (quick != INT_MAX) {
        return quick;
    }

    /* Token path, Open-Shell style: every token must match somewhere,
     * word prefixes rank better than plain substrings, so "word pad"
     * finds WordPad while "exe notepad" stays silent. */
    const std::vector<std::wstring> tokens = SplitSearchTokens(trimmed);
    const int kind = TokensMatch(name, trimmed, foldedName, foldedQuery, tokens);
    if (kind == 0) {
        return INT_MAX;
    }
    int rank = kind == 3 ? 10 : (kind == 2 ? 22 : 42);
    if (usageCount > 0) {
        const int boost = usageCount > 100 ? 8 : (usageCount / 15);
        rank -= boost;
        if (rank < 1) {
            rank = 1;
        }
    }
    return rank;
}

bool NameMatchesQuery(const std::wstring& name, const std::wstring& query) {
    return RankMatchAll(name, query, 0) != INT_MAX;
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
    /* Split once outside the loop: the per-item matcher gets both views
     * (raw + ASCII fold) and only does the expensive NLS walk per token. */
    const std::wstring trimmed = TrimSearchText(query);
    const std::wstring foldedTrimmed = FoldAscii(trimmed);
    const std::vector<std::wstring> tokens = SplitSearchTokens(trimmed);

    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (generation && *generation != expectedGeneration) {
            out.clear();
            return false;
        }
        const std::wstring foldedName = FoldAscii(apps[i].name);
        int rank = RankMatch(foldedName, foldedTrimmed, apps[i].usageCount);
        if (rank == INT_MAX) {
            const int kind = TokensMatch(apps[i].name, trimmed, foldedName,
                                         foldedTrimmed, tokens);
            if (kind == 0) {
                continue;
            }
            rank = kind == 3 ? 10 : (kind == 2 ? 22 : 42);
            if (apps[i].usageCount > 0) {
                const int boost = apps[i].usageCount > 100 ? 8
                                                           : (apps[i].usageCount / 15);
                rank -= boost;
                if (rank < 1) {
                    rank = 1;
                }
            }
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
