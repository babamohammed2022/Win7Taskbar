#include "Strings.h"

extern "C" __declspec(dllexport) void __stdcall W7T_SetLanguage(const char* twoLetterCode) {
    w7t::SetLanguage(twoLetterCode);
}
