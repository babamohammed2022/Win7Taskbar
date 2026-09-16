/*  Win7Taskbar - ponte MINIMO sulla logica di rete esistente                */
/*  Copyright (c) 2026 Win7Taskbar contributors - GPL v3 o successiva         */
/*                                                                           */
/*  Variante Windows 8 del flyout di rete (implementazione: Administratox).  */
/*                                                                           */
/*  QUESTO HEADER ESPONE SOLO LA LOGICA, NON UNA COPIA. La logica reale di   */
/*  connessione (enumerazione WLAN, stato, connect/disconnect, errori,       */
/*  notifiche) rimane UNICA dentro Win7NetworkFlyout.cpp (porting della mod  */
/*  Windhawk "Windows 7 Network Flyout Recreation", MIT). Qui ci sono solo   */
/*  quattro punti di accesso, implementati in coda a quel file SENZA         */
/*  toccare il codice esistente, cosi' il flyout Windows 8 puo' LEGGERE lo   */
/*  stato e RICHIEDERE le operazioni alla stessa identica logica.            */
/*                                                                           */
/*  Contratto di thread:                                                     */
/*    - CaptureState/MarkInitialized/IsInitialized possono esser chiamate    */
/*      da qualsiasi thread (internamente usano il lock del modulo);         */
/*    - Connect/Disconnect non bloccano: postano il lavoro sul thread        */
/*      dedicato del modulo, che riesegue la logica ORIGINALE (le stesse     */
/*      funzioni usate dal flyout Windows 7), e tornano subito;              */
/*    - non viene mai trattenuto il lock mentre si tocca la UI.              */

#pragma once
#include <windows.h>

namespace w7tnet {

/* Stato di connessione di una rete: stessi valori di ConnectionState usati
 * dalla logica (IDLE=0, CONNECTING=1, CONNECTED=2, DISCONNECTING=3,
 * ERROR=4). Riesposti come int per non dipendere dalle strutture interne. */
enum W8NetConnState : int {
    W8NET_STATE_IDLE = 0,
    W8NET_STATE_CONNECTING = 1,
    W8NET_STATE_CONNECTED = 2,
    W8NET_STATE_DISCONNECTING = 3,
    W8NET_STATE_ERROR = 4
};

constexpr int kW8NetMaxItems = 50;

struct W8NetItem {
    WCHAR ssid[33];          /* nome rete (gia' con eventuale suffisso " 2") */
    int   displaySuffix;     /* >1 quando piu' reti condividono lo stesso ssid */
    int   secured;           /* 1 = protetta, 0 = aperta */
    int   signalPercent;     /* 0..100 */
    int   connState;         /* W8NetConnState */
    int   hasProfile;        /* 1 = il sistema conosce gia' il profilo */
    int   hasInternet;       /* 1 = dietro c'e' internet (best effort) */
    int   adhoc;             /* 1 = rete ad hoc */
};

struct W8NetSnapshot {
    int      itemCount;                          /* voci valide in items[] */
    W8NetItem items[kW8NetMaxItems];
    int      ethernetConnected;                  /* 1 = cavo attivo */
    WCHAR    ethernetName[64];
    int      ethernetHasInternet;
    int      anyWifiConnected;                   /* 1 = almeno una Wi-Fi su */
};

/* Vero quando il modulo della mod e' inizializzato (InitInternal gia'
 * riuscito): serve a NON inizializzarlo due volte, perche' InitInternal
 * non e' idempotente (thread dedicato, hook, icone di sistema...). */
BOOL W8NetLogic_IsInitialized();

/* Chiede al modulo la finestra della logica nascosta se manca ancora (su
 * questa postazione il riquadro Windows 7 potrebbe non essere mai stato
 * aperto: senza quella finestra niente pompa dei messaggi/notifiche).
 * Asincrono, stessi codici di Connect/Disconnect. */
int W8NetLogic_EnsureLogicReady();

/* Vero se il riquadro Windows 7 e' su schermo (stessa espressione usata
 * dal modulo per "aperto"). Lettura di comodo per l'esclusione reciproca. */
BOOL W8NetLogic_IsWin7FlyoutVisible();

/* Esclusione reciproca: se il riquadro Windows 7 e' visibile lo chiude.
 * Da chiamare quando si apre il riquadro Windows 8: la scelta dello stile
 * e' una sola e lo stesso angolo non deve mai ospitare entrambi. */
void W8NetLogic_HideWin7FlyoutIfOpen();

/* Copia dello stato corrente in out. Falso se il modulo non e' pronto:
 * il chiamante mostra il proprio stato "rete non disponibile". */
BOOL W8NetLogic_CaptureState(W8NetSnapshot* out);

/* Chiede un rinfresco dati al modulo (lista reti, stati). Asincrono,
 * no-op se la finestra del modulo nascosta non c'e' ancora. */
void W8NetLogic_RequestRefresh();

/* Instrada un connect/disconnect sulla logica originale, sul thread del
 * modulo (la finestra del flyout Win7 e' viva, anche se nascosta, da
 * InitInternal in poi: e' il suo messaggio il nostro canale sicuro).
 * Ritorno: 0 = richiesta accettata; 1 = modulo non pronto; 2 = rete non
 * piu' presente; 3 = posta fallita; 4 = ssid mancante. */
int W8NetLogic_Connect(const WCHAR* ssid);
int W8NetLogic_Disconnect(const WCHAR* ssid);

} // namespace w7tnet
