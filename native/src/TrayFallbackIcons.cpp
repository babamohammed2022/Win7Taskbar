/*
 * Win7Taskbar - Core nativo - Icone di RIPIEGO dell'area di notifica
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Vedi TrayFallbackIcons.h per il perche' di questo file.
 */

#include "TrayFallbackIcons.h"

#include "TrayIconAssets.inc"   /* icone nostre: volume e rete            */
#include "BatteryAssets.inc"    /* glifi batteria ritagliati dalla striscia */
#include "AudioService.h"
#include "SehGuard.h"

#include <objbase.h>            /* WIN32_LEAN_AND_MEAN: esplicito         */
#include <netlistmgr.h>         /* CLSID_NetworkListManager / INetworkListManager */
#include <wlanapi.h>            /* WLAN_SIGNAL_QUALITY per le barre di segnale  */
#include <cmath>

namespace w7t {
namespace {

/* ------------------------------------------------------------------------ */
/*  GUID delle icone di sistema della shell                                  */
/*                                                                           */
/*  Sono le tre GUID con cui la shell di Windows registra volume, batteria   */
/*  e rete nella NOTIFYICONDATA (guidItem): identificarle cosi' e'           */
/*  indipendente dalla POSIZIONE nella barra, che e' esattamente il          */
/*  requisito. Nel dubbio (GUID assente su qualche build) si ripiega sul     */
/*  modulo proprietario della finestra, verificato cross-process da          */
/*  OwnerModuleIs.                                                           */
/* ------------------------------------------------------------------------ */
const GUID kVolumeGuid  = { 0x7820AE73, 0x23E3, 0x4223,
                            { 0x82, 0xC8, 0xBA, 0x94, 0x8F, 0x79, 0xAF, 0x8F } };
const GUID kBatteryGuid = { 0x7820AE74, 0x23E3, 0x4223,
                            { 0x82, 0xC8, 0xBA, 0x94, 0x8F, 0x79, 0xAF, 0x8F } };
const GUID kNetworkGuid = { 0x7820AE75, 0x23E3, 0x4223,
                            { 0x82, 0xC8, 0xBA, 0x94, 0x8F, 0x79, 0xAF, 0x8F } };

bool SameGuid(const GUID& a, const GUID& b) {
    return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
           std::memcmp(a.Data4, b.Data4, sizeof(a.Data4)) == 0;
}

bool GuidIsZero(const GUID& g) {
    const GUID zero = {};
    return SameGuid(g, zero);
}

/* ------------------------------------------------------------------------ */
/*  Cache delle decodifiche                                                  */
/*                                                                           */
/*  Ogni PNG incorporato viene decodificato UNA volta, alla prima           */
/*  richiesta di quella famiglia: le icone di ripiego si disegnano solo in  */
/*  caso di necessita', e chi non ne ha mai bisogno non paga nulla.          */
/* ------------------------------------------------------------------------ */
ArgbBitmap g_tray[trayassets::IdxCount];
bool       g_trayReady   = false;
bool       g_trayTried   = false;

ArgbBitmap g_batt[battassets::IdxCount];
bool       g_battReady   = false;
bool       g_battTried   = false;

bool g_loggedNetwork = false;
bool g_loggedVolume  = false;
bool g_loggedBattery = false;

ArgbBitmap DecodeOne(const char* b64) {
    ArgbBitmap out;
    std::vector<uint32_t> px;
    int w = 0;
    int h = 0;
    /* Nessun ritaglio: il PNG e' gia' la sola icona (il taglio e' stato
     * fatto in fase di generazione, sul bounding-box alpha). */
    if (DecodeEmbeddedPng(b64, px, w, h, false, 0)) {
        out.width  = w;
        out.height = h;
        out.pixels.resize(px.size() * 4);
        for (size_t i = 0; i < px.size(); ++i) {
            const uint32_t p = px[i];
            out.pixels[i * 4 + 0] = static_cast<uint8_t>(p & 0xFF);
            out.pixels[i * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xFF);
            out.pixels[i * 4 + 2] = static_cast<uint8_t>((p >> 16) & 0xFF);
            out.pixels[i * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xFF);
        }
    }
    return out;
}

void EnsureTrayGlyphs() {
    if (g_trayTried) {
        return;
    }
    g_trayTried = true;
    bool any = false;
    for (int i = 0; i < trayassets::IdxCount; ++i) {
        g_tray[i] = DecodeOne(trayassets::kAll[i].b64);
        any = any || !g_tray[i].empty();
    }
    g_trayReady = any;
}

void EnsureBatteryGlyphs() {
    if (g_battTried) {
        return;
    }
    g_battTried = true;
    bool any = false;
    for (int i = 0; i < battassets::IdxCount; ++i) {
        g_batt[i] = DecodeOne(battassets::kAll[i].b64);
        any = any || !g_batt[i].empty();
    }
    g_battReady = any;
}

const ArgbBitmap* TrayGlyph(trayassets::Idx index) {
    EnsureTrayGlyphs();
    if (!g_trayReady) {
        return nullptr;
    }
    const ArgbBitmap& bmp = g_tray[index];
    return bmp.empty() ? nullptr : &bmp;
}

const ArgbBitmap* BatteryGlyph(int index) {
    EnsureBatteryGlyphs();
    if (!g_battReady || index < 0 || index >= battassets::IdxCount) {
        return nullptr;
    }
    const ArgbBitmap& bmp = g_batt[index];
    return bmp.empty() ? nullptr : &bmp;
}

/* ------------------------------------------------------------------------ */
/*  Rete: stato corrente da NLM (+ qualita' del segnale per le barre)        */
/*                                                                           */
/*  NLM e' la stessa fonte che la shell usa per decidere l'icona di rete,   */
/*  quindi lo stato mostrato dal nostro ripiego e' quello vero, non una     */
/*  stima. La qualita' del segnale Wi-Fi arriva da wlanapi, gia' linkata    */
/*  dal progetto.                                                            */
/* ------------------------------------------------------------------------ */
int WirelessBars() {
    HANDLE client = nullptr;
    DWORD version = 0;
    if (WlanOpenHandle(2 /*WLAN_API_VERSION_2_0*/, nullptr, &version, &client)
            != ERROR_SUCCESS) {
        return 0;   /* nessun servizio WLAN: non e' una macchina Wi-Fi */
    }

    int bars = 0;
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (WlanEnumInterfaces(client, nullptr, &list) == ERROR_SUCCESS && list) {
        for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
            const WLAN_INTERFACE_INFO& info = list->InterfaceInfo[i];
            if (info.isState != wlan_interface_state_connected) {
                continue;
            }
            DWORD size = 0;
            PWLAN_CONNECTION_ATTRIBUTES attrs = nullptr;
            if (WlanQueryInterface(client, &info.InterfaceGuid,
                                   wlan_intf_opcode_current_connection, nullptr,
                                   &size,
                                   reinterpret_cast<PVOID*>(&attrs), nullptr)
                    == ERROR_SUCCESS && attrs) {
                const int quality = static_cast<int>(
                    attrs->wlanAssociationAttributes.wlanSignalQuality);
                int candidate = (quality * 5 + 99) / 100;
                if (candidate < 1) candidate = 1;
                if (candidate > 5) candidate = 5;
                if (candidate > bars) bars = candidate;
                WlanFreeMemory(attrs);
            }
        }
        WlanFreeMemory(list);
    }
    WlanCloseHandle(client, nullptr);
    return bars;
}

} /* namespace */

int TrayFallbackIcons::NetworkLevel() {
    bool connected  = false;
    bool internet   = false;

    W7T_SEH_TRY {
        /* L'apartment puo' essere gia' inizializzato da un altro modulo del
         * core: in quel caso CoInitializeEx ritorna S_FALSE e NON si deve
         * fare CoUninitialize. */
        const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool owner = SUCCEEDED(hrInit) && hrInit != S_FALSE;

        INetworkListManager* manager = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_NetworkListManager, nullptr,
                                      CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                      IID_PPV_ARGS(&manager));
        if (SUCCEEDED(hr) && manager != nullptr) {
            VARIANT_BOOL value = VARIANT_FALSE;
            if (SUCCEEDED(manager->IsConnected(&value))) {
                connected = (value != VARIANT_FALSE);
            }
            value = VARIANT_FALSE;
            if (SUCCEEDED(manager->IsConnectedToInternet(&value))) {
                internet = (value != VARIANT_FALSE);
            }
            manager->Release();
        }
        if (owner) {
            CoUninitialize();
        }
    } W7T_SEH_CATCH {
        return -1;
    } W7T_SEH_END

    if (!connected) {
        return -1;
    }
    if (!internet) {
        return 0;   /* collegato ma senza Internet: icona con avviso */
    }
    const int bars = WirelessBars();
    return bars > 0 ? bars : 5;   /* cablata (o senza WLAN): segnale pieno */
}

SystemIconKind TrayFallbackIcons::Identify(uint64_t ownerHwnd, const GUID& guidItem) {
    if (!GuidIsZero(guidItem)) {
        if (SameGuid(guidItem, kNetworkGuid)) return SystemIconKind::Network;
        if (SameGuid(guidItem, kVolumeGuid))  return SystemIconKind::Volume;
        if (SameGuid(guidItem, kBatteryGuid)) return SystemIconKind::Battery;
    }

    /* Secondo criterio: il modulo che possiede la finestra. Stessa verifica
     * cross-process usata dal resto del core, nessuna euristica nuova. */
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(ownerHwnd));
    if (hwnd == nullptr) {
        return SystemIconKind::None;
    }
    if (OwnerModuleIs(hwnd, L"pnidui.dll"))    return SystemIconKind::Network;
    if (OwnerModuleIs(hwnd, L"SndVolSSO.dll")) return SystemIconKind::Volume;
    if (OwnerModuleIs(hwnd, L"stobject.dll"))  return SystemIconKind::Battery;
    return SystemIconKind::None;
}

bool TrayFallbackIcons::Render(SystemIconKind kind, ArgbBitmap& out) {
    const ArgbBitmap* glyph = nullptr;

    switch (kind) {
        case SystemIconKind::Network: {
            const int level = NetworkLevel();
            if (level < 0) {
                glyph = TrayGlyph(trayassets::IdxNetworkNotWorking);
            } else if (level == 0) {
                glyph = TrayGlyph(trayassets::IdxNetworkWarning);
            } else {
                const int index = (level >= 1 && level <= 5) ? level - 1 : 4;
                glyph = TrayGlyph(static_cast<trayassets::Idx>(
                    trayassets::IdxNetwork0 + index));
            }
        } break;

        case SystemIconKind::Volume: {
            int32_t level = 0;
            int32_t muted = 0;
            if (AudioService::GetVolume(&level, &muted) != W7T_OK) {
                /* Nessun dispositivo audio: lo stesso stato "muto" che la
                 * shell mostra quando non c'e' un endpoint predefinito. */
                glyph = TrayGlyph(trayassets::IdxVolume0);
            } else if (muted != 0 || level <= 0) {
                glyph = TrayGlyph(trayassets::IdxVolume0);
            } else if (level < 34) {
                glyph = TrayGlyph(trayassets::IdxVolume1);
            } else if (level < 67) {
                glyph = TrayGlyph(trayassets::IdxVolume2);
            } else {
                glyph = TrayGlyph(trayassets::IdxVolume3);
            }
        } break;

        case SystemIconKind::Battery: {
            SYSTEM_POWER_STATUS sps{};
            int index = battassets::IdxNoBatt;
            if (GetSystemPowerStatus(&sps)) {
                const bool noBattery = (sps.BatteryFlag & 128) != 0;
                const bool charging  = (sps.BatteryFlag & 8) != 0;
                const bool unknown   = sps.BatteryFlag == 255 ||
                                       sps.BatteryLifePercent == 255;
                int percent = sps.BatteryLifePercent;
                if (percent > 100) {
                    percent = 100;
                }
                /* 1..10 come i dieci glifi di ogni colore della striscia. */
                int level = (percent + 9) / 10;
                if (level < 1) level = 1;
                if (level > 10) level = 10;

                if (unknown) {
                    index = battassets::IdxWarn;
                } else if (noBattery) {
                    index = battassets::IdxNoBatt;
                } else if (charging || percent >= 30) {
                    index = battassets::IdxGreenBase + (level - 1);
                } else if (percent >= 10) {
                    index = battassets::IdxYellowBase + (level - 1);
                } else {
                    index = battassets::IdxRedBase + (level - 1);
                    if (index > battassets::IdxRedBase + 5) {
                        index = battassets::IdxRedBase + 5;
                    }
                }
            }
            glyph = BatteryGlyph(index);
        } break;

        default:
            return false;
    }

    if (glyph == nullptr || glyph->empty()) {
        return false;
    }
    out = *glyph;
    return true;
}

void TrayFallbackIcons::LogFirstUse(SystemIconKind kind, const wchar_t* reason) {
    bool* flag = nullptr;
    const wchar_t* name = nullptr;
    switch (kind) {
        case SystemIconKind::Network: flag = &g_loggedNetwork; name = L"rete"; break;
        case SystemIconKind::Volume:  flag = &g_loggedVolume;  name = L"volume"; break;
        case SystemIconKind::Battery: flag = &g_loggedBattery; name = L"batteria"; break;
        default: return;
    }
    if (flag == nullptr || *flag) {
        return;
    }
    *flag = true;
    wchar_t line[320];
    wsprintfW(line,
              L"tray: icona di %s non leggibile da Explorer (%s) -> icona di "
              L"ripiego dell'app (una sola volta per tipo)",
              name, reason != nullptr ? reason : L"motivo non specificato");
    AppendCoreLog(line);
}

} /* namespace w7t */
