// Win7Taskbar - flyout di rete variazione Windows 8 (riquadro ricreato)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// Implementazione della variante Windows 8: Administratox.
//
// Il modulo mostra il riquadro di rete nello stile del pannello laterale
// di Windows 8 (fondo scuro, scivolo dal bordo destro, righe piane), ma
// NON implementa alcuna logica di rete: enumerazione, stato, connect,
// disconnect ed errori arrivano dalla STESSA logica gia' usata dal flyout
// Windows 7 (ponte dichiarato in NetLogicBridge.h). Ogni operazione qui
// e' asincrona: niente attese su API WLAN nel thread di interfaccia.

#pragma once
#include <windows.h>

namespace w7t {

class Win8NetworkFlyout {
public:
    static Win8NetworkFlyout& Instance();

    /* Prepara la classe finestra e il modulo della logica (inizializzato
     * una sola volta sul caller thread). Falso se manca il necessario:
     * il core allora ripiega sul riquadro della shell. */
    bool Init();
    void Uninit();

    /* Rettangolo icona in coordinate schermo: serve a scegliere il
     * monitor di destinazione del pannello. */
    void SetAnchorRect(const RECT& iconRect);

    /* Apre/chiude il pannello (come il tocco sull'icona di rete). */
    void Toggle();
    void Show();
    void Hide();
    bool IsVisible() const;

    /* Lingua dell'app: 0=it,1=en,2=es,3=fr,4=de,5=pt,6=pl,7=ru,8=ja,9=zh,10=ar.
     * Qualunque altro valore ripiega sull'inglese. */
    void SetLanguage(int appLang);

    /* v3.8: rilascio GDI su DLL_PROCESS_DETACH, solo se il modulo e' stato
     * mai costruito (stesso patto di BatteryFlyout::ShutdownIfCreated). */
    static void ShutdownIfCreated();

private:
    Win8NetworkFlyout() = default;
}; // class Win8NetworkFlyout

} // namespace w7t
