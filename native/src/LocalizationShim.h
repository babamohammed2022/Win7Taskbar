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
inline int W7T_DrawTextW(HDC hdc, const wchar_t* text, int count, RECT* rect, UINT format) {
    auto fn = static_cast<int (WINAPI*)(HDC, LPCWSTR, int, LPRECT, UINT)>(&DrawTextW);
    return fn(hdc, LocalizeNativeSearchText(text), count, rect, format);
}
}
#define DrawTextW(hdc, text, count, rect, format) w7t::W7T_DrawTextW((hdc), (text), (count), (rect), (format))
