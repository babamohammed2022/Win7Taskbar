#pragma once

#include "Strings.h"
#include <cwchar>

namespace w7t {
inline const wchar_t* LocalizeNativeSearchText(const wchar_t* text) {
    if (!text) return text;
    if (std::wcscmp(text, L"Corrispondenza migliore") == 0) return GetString(StrId::BestMatch);
    if (std::wcscmp(text, L"Programmi") == 0) return GetString(StrId::Programs);
    if (std::wcscmp(text, L"File recenti") == 0) return GetString(StrId::RecentFiles);
    if (std::wcscmp(text, L"Nessun elemento corrisponde alla ricerca.") == 0) return GetString(StrId::NoSearchResults);
    if (std::wcscmp(text, L"Scansione applicazioni...") == 0) return GetString(StrId::ScanningApplications);
    if (std::wcscmp(text, L"Apri") == 0) return GetString(StrId::Open);
    if (std::wcscmp(text, L"Esegui come amministratore") == 0) return GetString(StrId::RunAsAdministrator);
    if (std::wcscmp(text, L"Apri percorso file") == 0) return GetString(StrId::OpenFileLocation);
    return text;
}
}

// AppSearchWindow.cpp contains the legacy literals above. Redirect only its
// DrawTextW calls so the UI gets the selected native language without changing
// unrelated Win32 APIs or behavior.
#define W7T_LOCALIZED_DRAWTEXTW(hdc, text, count, rect, format) \
    DrawTextW((hdc), w7t::LocalizeNativeSearchText((text)), (count), (rect), (format))
#define DrawTextW W7T_LOCALIZED_DRAWTEXTW
