/* Win7Taskbar - native core - extra settings
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ======================================================================
 * WHY THIS FILE EXISTS
 *
 * The "Impostazioni extra" section of the Properties window collects the
 * secondary options. The user picks them in the Win32 dialog
 * (PropertiesDialog.cpp) and the MANAGED layer saves them, which stays the
 * only configuration of the program (settings.json, like every other entry:
 * no parallel file, no registry key).
 *
 * The native core receives the values with W7T_SetExtraSettings and keeps
 * them here:
 *
 *   - privacy mode -> passed straight to the recreated network flyout
 *     (w7tnet::W7TNetFlyout_SetPrivacyMode): it changes only the text OUR
 *     flyout draws, never the connections or Windows;
 *   - flyout colour (system or custom) -> kept for the recreated
 *     Windows 8-style flyout, which does NOT exist yet in this version
 *     (native/src/Win8NetworkFlyout.cpp is not compiled: see
 *     CMakeLists.txt). The value is persisted and queryable, applied to
 *     nothing: no Windows 7 flyout is touched by this setting.
 *
 * QuerySystemAccentColor() resolves the system accent colour when needed
 * (DwmGetColorizationColor, with a registry fallback). The Properties window
 * uses it too: the swatch next to "System colour" shows the real colour, read
 * from the system when it is drawn and re-read when Windows changes it.
 * ======================================================================
 */

#pragma once

#include <cstdint>

namespace w7t {
namespace extras {

/* 0 = system colour (default), 1 = custom colour. */
void     SetFlyoutColorMode(int32_t mode);
int32_t  FlyoutColorMode();

/* Custom colour as 0x00RRGGBB. */
void     SetFlyoutCustomColor(uint32_t rgb);
uint32_t FlyoutCustomColor();

/* 0 = normal (default), 1 = privacy. */
void     SetConnectionPrivacyMode(int32_t mode);
int32_t  ConnectionPrivacyMode();

/* System accent colour, requested from the system EVERY time: no copy is
 * stored in the configuration, so it follows the personalization changes of
 * Windows. false when the system does not provide it (the caller decides the
 * fallback). */
bool QuerySystemAccentColor(uint32_t* outRgb);

/* Resolved colour for the current mode: the system accent when the mode is
 * "system colour", the chosen colour otherwise. */
uint32_t ResolveFlyoutColor();

} /* namespace extras */
} /* namespace w7t */
