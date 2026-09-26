/*
 * Win7Taskbar - sincronizzazione della pagina legacy "Icone dell'area di
 * notifica".
 *
 * IconStreams e PastIconsStream sono dati binari proprietari di Explorer e
 * non esiste una struttura pubblica Microsoft da riscrivere in sicurezza.
 * Il codice usa quindi solo API Win32 documentate: legge le impostazioni
 * moderne, salva l'intera chiave TrayNotify in un file .reg, elimina i due
 * valori della cache e invia il messaggio registrato TaskbarCreated con
 * Explorer attivo. Non crea finestre e non usa hook o injection.
 */

#include "NotificationPageSync.h"

#include "Common.h"
#include "LocalAppDataStore.h"
#include "../include/RaiiWrappers.h"

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <shellapi.h>
#include <utility>

namespace w7t {
namespace {

constexpr wchar_t kTrayNotifyKey[] =
    L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\"
    L"CurrentVersion\\TrayNotify";
constexpr wchar_t kNotifyIconSettingsKey[] =
    L"Control Panel\\NotifyIconSettings";
constexpr wchar_t kBackupFileName[] = L"traynotify-backup.reg";
constexpr wchar_t kNotificationTag[] = L"[notification-page] ";

void LogStep(const std::wstring& message) {
    const std::wstring line = std::wstring(kNotificationTag) + message;
    AppendCoreLog(line.c_str());
}

std::wstring BaseName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring FriendlyNameOf(const std::wstring& path) {
    if (path.empty()) {
        return std::wstring();
    }

    SHFILEINFOW info = {};
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES &&
        SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                       SHGFI_DISPLAYNAME) != 0 &&
        info.szDisplayName[0] != L'\0') {
        return info.szDisplayName;
    }
    return BaseName(path);
}

bool SamePath(const std::wstring& first, const std::wstring& second) {
    if (first.empty() || second.empty()) {
        return false;
    }
    return _wcsicmp(first.c_str(), second.c_str()) == 0;
}

bool SameDisplayName(const std::wstring& first, const std::wstring& second) {
    return !first.empty() && !second.empty() &&
           _wcsicmp(first.c_str(), second.c_str()) == 0;
}

bool ReadStringValue(HKEY key, const wchar_t* name, std::wstring& out) {
    DWORD type = 0;
    DWORD bytes = 0;
    LONG status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        return false;
    }

    std::vector<BYTE> buffer(bytes + sizeof(wchar_t));
    status = RegQueryValueExW(key, name, nullptr, &type, buffer.data(), &bytes);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    const wchar_t* text = reinterpret_cast<const wchar_t*>(buffer.data());
    const size_t characters = bytes / sizeof(wchar_t);
    out.assign(text, characters);
    while (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return !out.empty();
}

bool ReadDwordValue(HKEY key, const wchar_t* name, DWORD& out) {
    DWORD type = 0;
    DWORD bytes = sizeof(out);
    return RegQueryValueExW(key, name, nullptr, &type,
                            reinterpret_cast<BYTE*>(&out), &bytes) ==
               ERROR_SUCCESS &&
           type == REG_DWORD && bytes == sizeof(out);
}

bool ReadNotifyIconSettings(std::vector<NotificationPageIcon>& registryIcons) {
    LogStep(L"lettura HKCU\\" + std::wstring(kNotifyIconSettingsKey));

    HKEY rootRaw = nullptr;
    const LONG openStatus = RegOpenCurrentUser(KEY_READ, &rootRaw);
    raii::RegKeyHandle root(rootRaw);
    if (openStatus != ERROR_SUCCESS || !root) {
        LogStep(L"lettura NotifyIconSettings non disponibile, codice " +
                std::to_wstring(openStatus));
        return false;
    }

    HKEY settingsRaw = nullptr;
    const LONG status = RegOpenKeyExW(root.get(), kNotifyIconSettingsKey, 0,
                                      KEY_READ, &settingsRaw);
    raii::RegKeyHandle settings(settingsRaw);
    if (status == ERROR_FILE_NOT_FOUND) {
        LogStep(L"NotifyIconSettings assente");
        return true;
    }
    if (status != ERROR_SUCCESS || !settings) {
        LogStep(L"apertura NotifyIconSettings fallita, codice " +
                std::to_wstring(status));
        return false;
    }

    DWORD subkeyCount = 0;
    DWORD maxSubkeyLength = 0;
    const LONG infoStatus = RegQueryInfoKeyW(
        settings.get(), nullptr, nullptr, nullptr, &subkeyCount,
        &maxSubkeyLength, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr);
    if (infoStatus != ERROR_SUCCESS) {
        LogStep(L"enumerazione NotifyIconSettings fallita, codice " +
                std::to_wstring(infoStatus));
        return false;
    }

    std::vector<wchar_t> subkeyName(static_cast<size_t>(maxSubkeyLength) + 2,
                                     L'\0');
    for (DWORD index = 0; index < subkeyCount; ++index) {
        DWORD nameLength = maxSubkeyLength + 1;
        FILETIME lastWrite = {};
        LONG enumStatus = RegEnumKeyExW(
            settings.get(), index, subkeyName.data(), &nameLength, nullptr,
            nullptr, nullptr, &lastWrite);
        if (enumStatus != ERROR_SUCCESS) {
            LogStep(L"voce NotifyIconSettings saltata, codice " +
                    std::to_wstring(enumStatus));
            continue;
        }

        HKEY itemRaw = nullptr;
        enumStatus = RegOpenKeyExW(settings.get(), subkeyName.data(), 0,
                                   KEY_READ, &itemRaw);
        raii::RegKeyHandle itemKey(itemRaw);
        if (enumStatus != ERROR_SUCCESS || !itemKey) {
            continue;
        }

        std::wstring executable;
        DWORD promoted = 0;
        if (!ReadStringValue(itemKey.get(), L"ExecutablePath", executable)) {
            continue;
        }
        const bool hasPromotion = ReadDwordValue(
            itemKey.get(), L"IsPromoted", promoted);

        NotificationPageIcon icon;
        icon.exePath = executable;
        icon.displayName = FriendlyNameOf(executable);
        icon.promoted = hasPromotion && promoted != 0;
        icon.order = static_cast<uint32_t>(registryIcons.size());
        registryIcons.push_back(std::move(icon));
    }

    LogStep(L"lettura NotifyIconSettings completata: " +
            std::to_wstring(registryIcons.size()) + L" voci");
    return true;
}

std::wstring EscapeRegString(const std::wstring& input) {
    std::wstring escaped;
    escaped.reserve(input.size() + 8);
    for (wchar_t character : input) {
        switch (character) {
        case L'\\': escaped += L"\\\\"; break;
        case L'"':  escaped += L"\\\""; break;
        case L'\r': escaped += L"\\r"; break;
        case L'\n': escaped += L"\\n"; break;
        case L'\t': escaped += L"\\t"; break;
        default:    escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::wstring FormatHex(const BYTE* bytes, DWORD count, DWORD type) {
    std::wstring result = L"hex";
    if (type != REG_BINARY) {
        wchar_t typeText[24] = {};
        swprintf(typeText, std::size(typeText), L"(%lu)",
                 static_cast<unsigned long>(type));
        result += typeText;
    }
    result += L":";

    for (DWORD index = 0; index < count; ++index) {
        wchar_t byteText[8] = {};
        swprintf(byteText, std::size(byteText), L"%02x",
                 static_cast<unsigned>(bytes[index]));
        if (index != 0) {
            result += L",";
        }
        result += byteText;
    }
    return result;
}

std::wstring FormatValue(const std::wstring& name, DWORD type,
                         const std::vector<BYTE>& data) {
    const std::wstring prefix = name.empty()
        ? L"@="
        : L"\"" + EscapeRegString(name) + L"\"=";

    if (type == REG_SZ && data.size() >= sizeof(wchar_t)) {
        const wchar_t* text = reinterpret_cast<const wchar_t*>(data.data());
        size_t count = data.size() / sizeof(wchar_t);
        while (count > 0 && text[count - 1] == L'\0') {
            --count;
        }
        return prefix + L"\"" +
               EscapeRegString(std::wstring(text, count)) + L"\"\r\n";
    }
    if (type == REG_DWORD && data.size() >= sizeof(DWORD)) {
        const DWORD value = *reinterpret_cast<const DWORD*>(data.data());
        wchar_t valueText[32] = {};
        swprintf(valueText, std::size(valueText), L"dword:%08lx",
                 static_cast<unsigned long>(value));
        return prefix + valueText + L"\r\n";
    }

    return prefix + FormatHex(data.data(), static_cast<DWORD>(data.size()),
                              type) + L"\r\n";
}

bool AppendRegistryKey(HKEY key, const std::wstring& path,
                       std::wstring& output) {
    output += L"[HKEY_CURRENT_USER\\" + path + L"]\r\n";

    DWORD valueCount = 0;
    DWORD maxValueNameLength = 0;
    DWORD maxValueDataLength = 0;
    LONG status = RegQueryInfoKeyW(
        key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &valueCount, &maxValueNameLength, &maxValueDataLength, nullptr,
        nullptr);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    std::vector<wchar_t> valueName(static_cast<size_t>(maxValueNameLength) + 2,
                                   L'\0');
    std::vector<BYTE> data(static_cast<size_t>(maxValueDataLength) + 1, 0);
    for (DWORD index = 0; index < valueCount; ++index) {
        DWORD nameLength = maxValueNameLength + 1;
        DWORD dataLength = maxValueDataLength;
        DWORD type = 0;
        status = RegEnumValueW(key, index, valueName.data(), &nameLength,
                               nullptr, &type, data.data(), &dataLength);
        if (status != ERROR_SUCCESS) {
            return false;
        }
        output += FormatValue(std::wstring(valueName.data(), nameLength),
                              type,
                              std::vector<BYTE>(data.begin(),
                                                data.begin() + dataLength));
    }
    output += L"\r\n";

    DWORD subkeyCount = 0;
    DWORD maxSubkeyLength = 0;
    status = RegQueryInfoKeyW(
        key, nullptr, nullptr, nullptr, &subkeyCount, &maxSubkeyLength,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (status != ERROR_SUCCESS) {
        return false;
    }

    std::vector<wchar_t> subkeyName(static_cast<size_t>(maxSubkeyLength) + 2,
                                    L'\0');
    for (DWORD index = 0; index < subkeyCount; ++index) {
        DWORD nameLength = maxSubkeyLength + 1;
        FILETIME lastWrite = {};
        status = RegEnumKeyExW(key, index, subkeyName.data(), &nameLength,
                               nullptr, nullptr, nullptr, &lastWrite);
        if (status != ERROR_SUCCESS) {
            return false;
        }

        HKEY childRaw = nullptr;
        status = RegOpenKeyExW(key, subkeyName.data(), 0, KEY_READ,
                               &childRaw);
        raii::RegKeyHandle child(childRaw);
        if (status != ERROR_SUCCESS || !child ||
            !AppendRegistryKey(child.get(), path + L"\\" +
                                      std::wstring(subkeyName.data(), nameLength),
                               output)) {
            return false;
        }
    }
    return true;
}

bool WriteUtf16File(const std::wstring& path, const std::wstring& content) {
    raii::GenericHandle file(CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return false;
    }

    const WORD bom = 0xFEFF;
    DWORD written = 0;
    if (!WriteFile(file.get(), &bom, sizeof(bom), &written, nullptr) ||
        written != sizeof(bom)) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>(content.size() * sizeof(wchar_t));
    if (bytes == 0) {
        return true;
    }
    return WriteFile(file.get(), content.data(), bytes, &written, nullptr) !=
               FALSE &&
           written == bytes;
}

bool BackupTrayNotifyKey(const std::wstring& path) {
    LogStep(L"backup chiave TrayNotify in " + path);

    std::wstring content = L"Windows Registry Editor Version 5.00\r\n\r\n";
    HKEY keyRaw = nullptr;
    const LONG status = RegOpenCurrentUser(KEY_READ, &keyRaw);
    raii::RegKeyHandle key(keyRaw);
    if (status != ERROR_SUCCESS || !key) {
        LogStep(L"backup: RegOpenCurrentUser fallita, codice " +
                std::to_wstring(status));
        return false;
    }

    HKEY trayNotifyRaw = nullptr;
    const LONG openStatus = RegOpenKeyExW(
        key.get(), kTrayNotifyKey, 0, KEY_READ, &trayNotifyRaw);
    raii::RegKeyHandle trayNotify(trayNotifyRaw);
    if (openStatus == ERROR_FILE_NOT_FOUND) {
        /* Il file resta un backup valido della situazione "chiave assente". */
        if (!WriteUtf16File(path, content)) {
            LogStep(L"backup della chiave assente non scrivibile");
            return false;
        }
        LogStep(L"backup completato: la chiave TrayNotify non esisteva");
        return true;
    }
    if (openStatus != ERROR_SUCCESS || !trayNotify) {
        LogStep(L"apertura TrayNotify per backup fallita, codice " +
                std::to_wstring(openStatus));
        return false;
    }

    if (!AppendRegistryKey(trayNotify.get(), kTrayNotifyKey, content) ||
        !WriteUtf16File(path, content)) {
        LogStep(L"scrittura backup TrayNotify fallita");
        return false;
    }
    LogStep(L"backup completato");
    return true;
}

bool ExplorerIsActive() {
    struct SearchState {
        HWND found = nullptr;
    } state;

    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        SearchState* search = reinterpret_cast<SearchState*>(parameter);
        wchar_t className[64] = {};
        if (GetClassNameW(hwnd, className, std::size(className)) <= 0 ||
            _wcsicmp(className, L"Shell_TrayWnd") != 0) {
            return TRUE;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        const std::wstring image = GetProcessImagePath(pid);
        if (_wcsicmp(BaseName(image).c_str(), L"explorer.exe") == 0) {
            search->found = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&state));

    return state.found != nullptr;
}

bool DeleteLegacyCacheValues() {
    LogStep(L"cancellazione valori IconStreams e PastIconsStream");

    HKEY rootRaw = nullptr;
    LONG status = RegOpenCurrentUser(KEY_READ | KEY_SET_VALUE, &rootRaw);
    raii::RegKeyHandle root(rootRaw);
    if (status != ERROR_SUCCESS || !root) {
        LogStep(L"apertura TrayNotify in scrittura fallita, codice " +
                std::to_wstring(status));
        return false;
    }

    HKEY keyRaw = nullptr;
    status = RegOpenKeyExW(root.get(), kTrayNotifyKey, 0,
                           KEY_QUERY_VALUE | KEY_SET_VALUE, &keyRaw);
    raii::RegKeyHandle key(keyRaw);
    if (status == ERROR_FILE_NOT_FOUND) {
        LogStep(L"TrayNotify assente: nessun valore da cancellare");
        return true;
    }
    if (status != ERROR_SUCCESS || !key) {
        LogStep(L"apertura TrayNotify per cancellazione fallita, codice " +
                std::to_wstring(status));
        return false;
    }

    bool success = true;
    for (const wchar_t* valueName : { L"IconStreams", L"PastIconsStream" }) {
        status = RegDeleteValueW(key.get(), valueName);
        if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) {
            LogStep(std::wstring(L"valore ") + valueName +
                    (status == ERROR_FILE_NOT_FOUND ? L" gia' assente" :
                                                       L" cancellato"));
        } else {
            LogStep(std::wstring(L"cancellazione ") + valueName +
                    L" fallita, codice " + std::to_wstring(status));
            success = false;
            break;
        }
    }
    return success;
}

void LogRereadResult() {
    LogStep(L"rilettura della chiave TrayNotify dopo il broadcast");
    HKEY rootRaw = nullptr;
    const LONG rootStatus = RegOpenCurrentUser(KEY_READ, &rootRaw);
    raii::RegKeyHandle root(rootRaw);
    if (rootStatus != ERROR_SUCCESS || !root) {
        LogStep(L"rilettura HKCU fallita");
        return;
    }
    HKEY keyRaw = nullptr;
    const LONG status = RegOpenKeyExW(root.get(), kTrayNotifyKey, 0, KEY_READ,
                                      &keyRaw);
    raii::RegKeyHandle key(keyRaw);
    if (status != ERROR_SUCCESS || !key) {
        LogStep(L"rilettura TrayNotify fallita, codice " +
                std::to_wstring(status));
        return;
    }
    for (const wchar_t* valueName : { L"IconStreams", L"PastIconsStream" }) {
        DWORD type = 0;
        DWORD bytes = 0;
        const LONG valueStatus = RegQueryValueExW(
            key.get(), valueName, nullptr, &type, nullptr, &bytes);
        if (valueStatus == ERROR_SUCCESS) {
            LogStep(std::wstring(L"rilettura ") + valueName + L": presente, " +
                    std::to_wstring(bytes) + L" byte");
        } else {
            LogStep(std::wstring(L"rilettura ") + valueName +
                    L": assente, codice " + std::to_wstring(valueStatus));
        }
    }
}

bool DeleteAndBroadcast() {
    if (!ExplorerIsActive()) {
        LogStep(L"Explorer non attivo: reset annullato");
        return false;
    }

    const std::wstring backup = LocalAppDataStore::File(kBackupFileName);
    if (backup.empty() || !BackupTrayNotifyKey(backup)) {
        LogStep(L"reset annullato: backup non disponibile");
        return false;
    }
    if (!DeleteLegacyCacheValues()) {
        LogStep(L"reset interrotto dopo il backup");
        return false;
    }

    const UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreated == 0) {
        LogStep(L"RegisterWindowMessage(TaskbarCreated) fallita");
        return false;
    }

    LogStep(L"broadcast TaskbarCreated a HWND_BROADCAST");
    if (SendNotifyMessageW(HWND_BROADCAST, taskbarCreated, 0, 0) == FALSE) {
        LogStep(L"broadcast TaskbarCreated rifiutato, codice " +
                std::to_wstring(GetLastError()));
        return false;
    }
    LogStep(L"broadcast TaskbarCreated inviato, attendo la rilettura di Explorer");
    Sleep(250);
    LogRereadResult();
    return true;
}

} /* namespace */

std::vector<NotificationPageIcon> NotificationPageSync::Normalize(
    std::vector<NotificationPageIcon> liveIcons) {
    try {
        std::vector<NotificationPageIcon> registryIcons;
        ReadNotifyIconSettings(registryIcons);

        for (NotificationPageIcon& live : liveIcons) {
            if (live.displayName.empty() && !live.exePath.empty()) {
                live.displayName = FriendlyNameOf(live.exePath);
            }
            if (live.displayName.empty()) {
                const std::wstring name = BaseName(live.exePath);
                if (!name.empty()) {
                    live.displayName = name;
                }
            }
            if (live.displayName.empty()) {
                wchar_t fallback[32] = {};
                swprintf(fallback, std::size(fallback), L"App %lu",
                         static_cast<unsigned long>(live.pid));
                live.displayName = fallback;
            }
        }

        for (const NotificationPageIcon& registry : registryIcons) {
            auto match = std::find_if(
                liveIcons.begin(), liveIcons.end(),
                [&registry](const NotificationPageIcon& live) {
                    return SamePath(live.exePath, registry.exePath) ||
                           SameDisplayName(live.displayName,
                                           registry.displayName);
                });
            if (match == liveIcons.end()) {
                /* NotifyIconSettings conserva anche cronologia: senza un
                 * owner vivo non la trasformiamo in una falsa icona attiva.
                 * La pagina legacy ricevera' comunque il proprio PastIcons
                 * cache da Explorer dopo il reset. */
                LogStep(L"voce registro senza owner vivo ignorata: " +
                        registry.displayName);
            } else {
                if (match->exePath.empty()) {
                    match->exePath = registry.exePath;
                }
                if (match->displayName.empty()) {
                    match->displayName = registry.displayName;
                }
                match->promoted = registry.promoted;
            }
        }

        std::sort(liveIcons.begin(), liveIcons.end(),
                  [](const NotificationPageIcon& first,
                     const NotificationPageIcon& second) {
                      return first.order < second.order;
                  });
        for (size_t index = 0; index < liveIcons.size(); ++index) {
            liveIcons[index].order = static_cast<uint32_t>(index);
        }
        LogStep(L"elenco normalizzato: " +
                std::to_wstring(liveIcons.size()) + L" voci");
        for (const NotificationPageIcon& icon : liveIcons) {
            LogStep(L"voce " + std::to_wstring(icon.order) + L": " +
                    icon.displayName + L" [" +
                    (icon.promoted ? L"promossa" : L"overflow") + L"]");
        }
        return liveIcons;
    } catch (...) {
        LogStep(L"normalizzazione fallita per eccezione");
        return std::vector<NotificationPageIcon>();
    }
}

int32_t NotificationPageSync::BackfillLegacyPage(
    const std::vector<NotificationPageIcon>& liveIcons) {
    try {
        LogStep(L"inizio backfill pagina legacy");
        const std::vector<NotificationPageIcon> normalized = Normalize(liveIcons);
        if (normalized.empty()) {
            LogStep(L"nessuna icona normalizzata; il reset non viene eseguito");
            return W7T_ERR_NOT_FOUND;
        }
        if (!DeleteAndBroadcast()) {
            return W7T_ERR_NOT_FOUND;
        }
        LogStep(L"backfill pagina legacy completato");
        return W7T_OK;
    } catch (...) {
        LogStep(L"backfill pagina legacy fallito per eccezione");
        return W7T_ERR_NOT_FOUND;
    }
}

} /* namespace w7t */
