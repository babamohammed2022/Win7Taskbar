// Win7Taskbar - indicatore della lingua di input: port completo delle tre
// mod Windhawk (interfaccia del modulo).
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Struttura identica a quella di Windows (cornice TrayInputIndicatorWClass
// + figlio-testo InputIndicatorButton), regole delle tre mod portate una a
// una (minimo 32 px, layout-control a 4 modi con SPI, testo dritto in stile
// fix-legacy). Vedi l'intestazione del .cpp per il dettaglio.

#pragma once

#include <cstdint>

namespace w7t {
namespace langbar {

/* Crea/riposiziona l'indicatore dentro la finestra della barra. mode:
 * 0 nascosta, 1 stile Windows 7, 2 targhetta Windows 8.1 (minimo 32 px),
 * 3 stile Windows 10/11. Le coordinate sono fisiche, relativi all'owner. */
void Place(uint64_t ownerHwnd, int mode, int x, int y, int width, int height);

/* Imposta il modo del layout-control (0 keepLayoutOnly, 1 hide, 2 show,
 * 3 windowsDefault) con le stesse chiamate SPI della mod. */
void SetPolicyIndex(int index);

/* Distrugge le finestre dell'indicatore (chiusura del core). */
void Shutdown();

} // namespace langbar
} // namespace w7t
