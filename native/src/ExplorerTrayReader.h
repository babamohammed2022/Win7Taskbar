/*
 * Win7Taskbar - Core nativo - Lettura delle icone gia' presenti nella tray
 *                             di Explorer
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 * PERCHE' QUESTO FILE ESISTE
 *
 * Registrando le window class Shell_TrayWnd/TrayNotifyWnd riceviamo le icone
 * che vengono create DA QUEL MOMENTO IN POI. Tutte quelle gia' presenti
 * quando la barra parte (antivirus, OneDrive, driver audio, ecc.) restano
 * invisibili, perche' le loro applicazioni hanno gia' inviato il proprio
 * NIM_ADD a Explorer e non lo ripeteranno.
 *
 * La soluzione usata dalle shell alternative e' leggere direttamente la
 * ToolbarWindow32 dell'area di notifica di Explorer: ogni pulsante di quella
 * toolbar ha un dwData che punta a una struttura TrayItem nello spazio di
 * indirizzamento di Explorer, contenente hWnd, uID, tooltip e HICON.
 *
 * Logica equivalente a ManagedShell.WindowsTray/ExplorerTrayService.cs
 * (progetto ManagedShell di cairoshell, licenza Apache 2.0), usato da
 * RetroBar. Reimplementata qui in C++ nativo: nessuna riga di codice C#
 * copiata. Vedi CREDITS.txt.
 * ---------------------------------------------------------------------------
 */

#ifndef W7T_EXPLORER_TRAY_READER_H
#define W7T_EXPLORER_TRAY_READER_H

#include "Common.h"

namespace w7t {

/* Una icona letta dalla tray di Explorer. */
struct ExplorerTrayItem {
    uint64_t     ownerHwnd       = 0;
    uint32_t     uid             = 0;
    uint32_t     callbackMessage = 0;
    uint32_t     version         = 0;   /* NOTIFYICON_VERSION dell'icona */
    bool         hidden          = false;  /* nell'overflow di Explorer */
    std::wstring tooltip;
    std::wstring exeName;
    GUID         guidItem        = {};
    ArgbBitmap   bitmap;

    /* True se i pixel vengono dal DISEGNO della toolbar (PrintWindow +
     * ritaglio del pulsante), false se dall'HICON della NOTIFYICONDATA.
     * Il servizio usa questo flag per decidere se il fotogramma e'
     * autorevole per il confronto d'hash. */
    bool         capturedPixels  = false;

    /* v2.4: l'HICON vivo dichiarato da app/Explorer (CopyIcon sul campo
     * hIcon della NOTIFYICONDATA letta dalla memoria di Explorer). E' la
     * fonte che stobject aggiorna con NIM_MODIFY quando cambia lo stato
     * della batteria: prima veniva letta ma SCARTATA nel giro di
     * aggiornamento (si adottava solo capturedPixels), quindi l'icona
     * restava congelata. Il servizio ora adotta la fonte che cambia. */
    ArgbBitmap   iconBitmap;
    bool         hasIconBitmap   = false;

    /* Windows 11 conserva la scelta visibile/overflow nella chiave privata
     * NotifyIconSettings. Il nome della sottochiave e' un identificatore
     * decimale opaco; la decisione si ricava dai valori UID, ExecutablePath
     * e IsPromoted, non dal tentativo di ricalcolare l'id. Se la voce non e'
     * presente o il formato non e' verificabile, hidden resta la lettura
     * della toolbar. */
    bool         promotionKnown  = false;
    bool         promoted        = true;
};

/* Esito di una passata di lettura: `okVisible` dice se la toolbar delle
 * icone VISIBLE e' stata letta davvero, INCLUSI tutti i suoi pulsanti.
 * E' il flag che rende POSSIBILE o MENO la rimozione per assenza: una
 * lettura fallita non autorizza mai a cancellare icone dal modello.
 *
 * v2.1: la sola raggiungibilita' della toolbar non basta. Se un singolo
 * TB_GETBUTTON va in timeout o la lettura della memoria di Explorer
 * fallisce, quel pulsante manca all'appello pur esistendo: la lettura e'
 * da considerarsi INCOMPLETA (`okVisible`/`okOverflow` restano false) e
 * la riconciliazione non deve ne' rimuovere ne' contare assenze. Il
 * timeout per-pulsante su una barra occupata era la causa reale delle
 * icone che sparivano (v. CORREZIONI-v2.1.txt, P1).
 *
 * `emptyVisible`: la toolbar visibile e' stata letta con successo ma
 * riporta ZERO pulsanti. Su Windows 10/11 e' possibile solo in stati
 * transitori (Explorer in ricostruzione) o con tutte le icone promosse
 * nell'overflow: comunque non e' mai una base sicura per rimuovere. */
struct ExplorerTrayReadResult {
    bool okVisible = false;
    bool okOverflow = true;    /* nessuna finestra di overflow = lettura OK */
    bool emptyVisible = false; /* toolbar visibile valida ma con 0 pulsanti */
    std::vector<ExplorerTrayItem> items;
};

/* v2.37 punto 15: chiusura garantita. Richiede l'interruzione delle
 * letture della toolbar di Explorer: la passata in corso si ferma al
 * pulsante successivo e risulta "non valida" (okVisible=false), quindi
 * la riconciliazione non rimuove nulla. Il processo non resta piu'
 * appeso a una lettura lenta durante l'uscita. */
void RequestAbortReads();
bool ReadsAbortRequested();

class ExplorerTrayReader {
public:
    /**
     * Legge tutte le icone attualmente presenti nell'area di notifica di
     * Explorer, comprese quelle nascoste nell'overflow.
     *
     * Lettura SOLA-lettura: nessun toggle COM, nessun messaggio privato,
     * niente scrittura in Explorer. Stampa della toolbar (PrintWindow)
     * eseguita solo per le icone in `forceCapture` (o tutte se `captureAll`),
     * perche' e' l'unica parte costosa del giro.
     *
     * @return numero di icone lette, oppure un codice d'errore negativo.
     */
    static int32_t ReadAll(std::vector<ExplorerTrayItem>& out);

    /* Passata con politica di cattura: vedi ExplorerTrayReadResult. */
    static ExplorerTrayReadResult ReadAllEx(bool capture = false);
};

} /* namespace w7t */

#endif /* W7T_EXPLORER_TRAY_READER_H */
