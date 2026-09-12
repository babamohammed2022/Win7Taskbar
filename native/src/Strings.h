#pragma once

#include <string>

namespace w7t {

enum class StrId {
    BestMatch,
    Programs,
    RecentFiles,
    NoSearchResults,
    ScanningApplications,
    Open,
    RunAsAdministrator,
    OpenFileLocation,
};

void SetLanguage(const char* twoLetterCode);
const wchar_t* GetString(StrId id);

} // namespace w7t
