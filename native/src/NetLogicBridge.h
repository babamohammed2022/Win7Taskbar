/*  Win7Taskbar - ponte MINIMO tra i due flyout di rete integrati           */
/*  Copyright (c) 2026 Win7Taskbar contributors - GPL v3 o successiva       */
/*                                                                           */
/*  Con il porting COMPLETO della mod "Windows 8x Network Flyout              */
/*  Recreation" (AdmXP8/Administratox, MIT) in Win8NetworkFlyout.cpp il       */
/*  riquadro variante Windows 8 ha la SUA logica nativa (enumerazione,       */
/*  stato, connect/disconnect, notifiche): non serve piu' leggere lo stato   */
/*  del modulo Windows 7. Resta solo l'esclusione reciproca: la scelta del   */
/*  riquadro di rete e' una sola, quindi aprendo il riquadro Windows 8 si    */
/*  chiude quello Windows 7 se per qualche percorso laterale era aperto.     */
/*  Implementazioni in coda a Win7NetworkFlyout.cpp (blocco "v4.0").         */

#pragma once
#include <windows.h>

namespace w7tnet {

/* Vero se il riquadro Windows 7 e' su schermo (stessa espressione usata
 * dal modulo per "aperto"). Lettura di comodo per l'esclusione reciproca. */
BOOL W8NetLogic_IsWin7FlyoutVisible();

/* Esclusione reciproca: se il riquadro Windows 7 e' visibile lo chiude.
 * Da chiamare quando si apre il riquadro Windows 8: mai entrambi su
 * schermo insieme. */
void W8NetLogic_HideWin7FlyoutIfOpen();

} // namespace w7tnet
