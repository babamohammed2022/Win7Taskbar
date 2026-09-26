// Win7Taskbar - Windows 7 Network Flyout (porting integrato)
// Interfaccia verso il resto di Win7TaskbarCore. L'implementazione e' il
// porting fedele della mod Windhawk "Windows 7 Network Flyout Recreation"
// v5.0.0 (MIT) in Win7NetworkFlyout.cpp.
//
// Regola fondamentale: l'icona di rete nella taskbar resta SEMPRE quella
// originale di Windows (importata dal tray reale). Questo modulo fornisce
// soltanto il flyout mostrato al click quando l'impostazione e'
// "Windows 7 (ricreato)".

#pragma once

#include <windows.h>

namespace w7tnet {

/* Inizializza il modulo (thread dedicato, GDI+, icone, font). Chiamare
 * una volta all'avvio di Win7Taskbar. Ritorna TRUE in caso di successo. */
BOOL W7TNetFlyout_InitInternal();

/* Teardown completo e sicuro (join dei thread, chiusura WLAN/COM). */
void W7TNetFlyout_UninitInternal();

/* Imposta il rettangolo (coordinate schermo) dell'icona di rete nel tray
 * di Win7Taskbar, usato per ancorare il flyout. NULL = discovery
 * originale della mod. */
void W7TNetFlyout_SetAnchorRect(const RECT* rc);

/* Apre/chiude il flyout (toggle), con marshalling sul thread proprietario
 * gia' gestito dalla logica originale. */
void W7TNetFlyout_Toggle();

/* Apertura/chiusura esplicite e idempotenti. */
void W7TNetFlyout_Show();
void W7TNetFlyout_Hide();

/* v2.37 punto 16: imposta la lingua del flyout a partire dall'indice
 * lingua dell'app (0=it, 1=en, 2=es, 3=fr, 4=de, 5=pt, 6=pl, 7=ru,
 * 8=ja, 9=zh). Usa esclusivamente le traduzioni gia' presenti nella
 * tabella interna della mod; ja/zh ripiegano sull'inglese. */
void W7TNetFlyout_SetLanguage(int appLanguageIndex);

/* v1.21.7: privacy mode of the extra settings
 * (0 = real network names, 1 = generic "Network 1", "Network 2"...).
 * It changes ONLY the text drawn by the recreated flyout: no network API, no
 * Windows setting. */
void W7TNetFlyout_SetPrivacyMode(int mode);

} // namespace w7tnet
