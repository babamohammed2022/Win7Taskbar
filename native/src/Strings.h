/*
 * Win7Taskbar - Core nativo - Lingue e tabelle delle stringhe
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ====================================================================
 * PERCHE' QUESTO FILE ESISTE
 *
 * Il nativo disegna da se' menu contestuali, finestra Proprieta' e ricerca
 * applicazioni: sono stringhe generate dall'app, e devono seguire la lingua
 * scelta dall'utente esattamente come le risorse XAML del livello gestito.
 *
 * Prima ogni modulo aveva la sua tabella (o, peggio, stringhe italiane
 * fisse nel codice). Qui c'e' UNA sola sorgente per:
 *
 *   - l'elenco delle lingue supportate (codice a due lettere, nome nativo,
 *     indice): lo usano il selettore di Proprieta', il core e il managed;
 *   - le stringhe brevi condivise (ricerca, menu di sistema e di gruppo),
 *     scelte con S(StrId::...);
 *   - le due tabelle lunghe tipizzate: PropStrings (finestra Proprieta') e
 *     BattStrings (flyout batteria), ognuna con accessor per lingua.
 *
 * Regola di ripiego: qualunque codice fuori dalle 11 lingue note -> INGLESE.
 * L'italiano non e' mai il ripiego implicito (lo e' solo l'indice 0 quando
 * il chiamante chiede esplicitamente quella lingua).
 *
 * Come si sceglie la lingua:
 *   - l'utente l'ha scelta in Proprieta' -> W7T_SetLanguage() dal managed;
 *   - nessuna scelta (primo avvio)        -> DetectSystemLanguage();
 *   - lingua di sistema non supportata    -> inglese.
 * ====================================================================
 */

#pragma once

#include <string>

namespace w7t {

/* ------------------------------------------------------------------------ */
/*  Lingue supportate                                                        */
/*                                                                           */
/*  L'ORDINE E' UN CONTRATTO: l'indice e' quello che viaggia fra livello     */
/*  nativo e gestito (selettore di Proprieta', flyout, jump list).          */
/*  Le lingue nuove si aggiungono IN CODA, mai in mezzo.                    */
/* ------------------------------------------------------------------------ */
enum class Lang : int {
    It = 0,
    En,
    Es,
    Fr,
    De,
    Pt,
    Pl,
    Ru,
    Ja,
    Zh,
    Ar,
    Count,
};

constexpr int kLangCount = static_cast<int>(Lang::Count);

struct LanguageEntry {
    const char*    code;        /* codice a due lettere, minuscolo */
    const wchar_t* nativeName;  /* nome nel selettore di Proprieta' */
};

/* Elenco completo (kLangCount voci), unica sorgente per il selettore. */
const LanguageEntry* Languages();

const char*    LangCode(Lang lang);
const wchar_t* LangName(Lang lang);

/* Conversioni, con ripiego INGLESE su qualunque valore ignoto. */
Lang LangFromCode(const char* twoLetterCode);
Lang LangFromIndex(int index);
int  LangIndex(Lang lang);

/* Lingua dell'interfaccia di Windows, gia' filtrata sulle lingue
 * supportate: se non c'e' corrispondenza ritorna En. */
Lang DetectSystemLanguage();

/* ------------------------------------------------------------------------ */
/*  Stringhe brevi condivise                                                 */
/* ------------------------------------------------------------------------ */
enum class StrId {
    /* Ricerca applicazioni (AppSearchWindow.cpp). */
    BestMatch,
    Programs,
    RecentFiles,
    NoSearchResults,
    ScanningApplications,
    Open,
    RunAsAdministrator,
    OpenFileLocation,
    AppItem,

    /* Menu di sistema della finestra (ShellMenu.cpp): servono solo come
     * ripiego quando Windows non fornisce il testo della voce. */
    SysRestore,
    SysMove,
    SysSize,
    SysMinimize,
    SysMaximize,
    SysClose,

    /* Menu di gruppo della barra (ShellMenu.cpp). */
    GroupMinimize,
    GroupClose,

    /* Pannello delle icone nascoste (TrayOverflowWindow.cpp): il collegamento
     * in fondo, quello che in Windows 7 si chiama "Personalizza...". */
    OverflowCustomize,

    /* v3.5: menu contestuali delle icone di sistema ricreate (volume,
     * rete, batteria). Sono le voci che Windows 7 mostrava con il tasto
     * destro su quelle tre icone della tray. */
    CtxVolMixer,
    CtxPlayback,
    CtxRecording,
    CtxSounds,
    CtxTroubleshoot,
    CtxNetCenter,
    CtxMobility,
    CtxPower,
};

/* Imposta la lingua corrente (dal managed, o all'avvio col rilevamento). */
void SetLanguage(const char* twoLetterCode);
void SetLanguageByIndex(int index);
Lang CurrentLanguage();

/* Stringa nella lingua corrente. Mai nullptr. */
const wchar_t* S(StrId id);

/* Nome storico della stessa funzione: usato dai moduli scritti prima. */
const wchar_t* GetString(StrId id);

/* ------------------------------------------------------------------------ */
/*  Tabella della finestra Proprieta'                                        */
/*                                                                           */
/*  I nomi dei campi sono quelli del dialogo: chi disegna legge S.title,     */
/*  S.grpClock, ... senza conversioni intermedie.                            */
/* ------------------------------------------------------------------------ */
struct PropStrings {
    const wchar_t* title;
    const wchar_t* tab1; const wchar_t* tab2;
    const wchar_t* grpClock; const wchar_t* chkSeconds;
    const wchar_t* txtFlyout; const wchar_t* flyRecreated; const wchar_t* flyNative;
    const wchar_t* grpSearch; const wchar_t* chkSearch; const wchar_t* btnOpen;
    const wchar_t* grpLang;
    const wchar_t* grpExit; const wchar_t* btnExit;
    const wchar_t* about; const wchar_t* credits;
    const wchar_t* ok; const wchar_t* cancel; const wchar_t* apply;
    const wchar_t* grpNetFlyout; const wchar_t* txtNetFlyout;
    const wchar_t* netWin7; const wchar_t* netModern;
    const wchar_t* grpSysFly; const wchar_t* chkClassicVol;
    const wchar_t* chkBattFlyout;
    const wchar_t* tab3;
    const wchar_t* grpFlyouts;
    const wchar_t* lblClock; const wchar_t* lblNetwork;
    const wchar_t* lblVolume; const wchar_t* lblBattery;
    const wchar_t* grpTaskbar; const wchar_t* lblLang;
    /* v3.5: indicatore della lingua di input (riga del gruppo lingua). */
    const wchar_t* lblLangBar;
    const wchar_t* langHidden;
    const wchar_t* langWin7;
    const wchar_t* langWin81;
    const wchar_t* langWin10;
    const wchar_t* grpNotif; const wchar_t* txtNotif; const wchar_t* btnCustomize;
    const wchar_t* grpAero; const wchar_t* txtAero; const wchar_t* chkAeroPeek;
    const wchar_t* linkHelp;
    const wchar_t* txtToolbars;
    const wchar_t* tbDesktop; const wchar_t* tbAddress; const wchar_t* tbLinks;
};

const PropStrings& PropStringsFor(Lang lang);

/* ------------------------------------------------------------------------ */
/*  Tabella del flyout batteria                                              */
/* ------------------------------------------------------------------------ */
struct BattStrings {
    const wchar_t* remaining;
    const wchar_t* timeLeft;
    const wchar_t* charging;
    const wchar_t* full;
    const wchar_t* noBattery;
    const wchar_t* link;
};

const BattStrings& BattStringsFor(Lang lang);

} // namespace w7t
