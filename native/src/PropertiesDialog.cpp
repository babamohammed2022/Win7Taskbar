// Win7Taskbar - finestra Proprieta' Win32 classica (stile Win7)
// Copyright (c) 2026 Win7Taskbar contributors - GPL v3 or later
//
// v3.7: struttura COPIATA dalla mod di riferimento: template dialogo in
// memoria con unita' DLU, stesse dimensioni (262x271), stessi pulsanti
// standard 50x14, tab nativo, texture tema, tema explorer sui figli.

#include "PropertiesDialog.h"
#include "SehGuard.h"
/* v2.48: il pulsante "Personalizza..." dell'area di notifica usa LO STESSO
 * comando del menu di overflow della barra (W7T_OpenNotificationIconsSettings,
 * definito nel core nativo): nessuna pagina sostitutiva, nessun percorso
 * alternativo inventato qui. */
#include "Win7TaskbarCore.h"
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <cstring>
#include <string>   /* v2.58: std::wstring per il testo unico di "Informazioni" */

#ifndef SIID_TASKBAR
#define SIID_TASKBAR 39
#endif
#ifndef ETDT_ENABLE
#define ETDT_ENABLE 0x00000002
#endif
#ifndef ETDT_USETABTEXTURE
#define ETDT_USETABTEXTURE 0x00000004
#endif
#ifndef ETDT_ENABLETAB
#define ETDT_ENABLETAB (ETDT_ENABLE | ETDT_USETABTEXTURE)
#endif

namespace w7t {

namespace {

/* ============================================================================
 * v2.47: RAII anche qui dentro.
 *
 * Il codice Win32 classico e' pieno di coppie "acquisisci/rilascia" e basta
 * una via d'uscita anticipata per perdere la seconda meta'. ScopeExit lega il
 * rilascio alla distruzione dell'oggetto: qualunque return, eccezione o
 * percorso alternativo passa dal distruttore. Si usa per il buffer del
 * template, per l'HDC del dialogo e per tutto cio' che va liberato.
 * ========================================================================== */
template <typename F>
struct ScopeExit {
    F fn;
    explicit ScopeExit(F f) : fn(f) {}
    ~ScopeExit() { fn(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
};
template <typename F>
ScopeExit<F> MakeScopeExit(F f) { return ScopeExit<F>(f); }

/* Stesse dimensioni della mod di riferimento (unita' DLU).
 * v2.47: la finestra si allunga per ospitare la scheda "Barre degli
 * strumenti" e i nuovi gruppi (Flyout, Area di notifica, Aero Peek). */
constexpr short MAIN_WIDTH  = 262;
constexpr short MAIN_HEIGHT = 312;

/* v2.50: le tendine di volume e batteria non si chiamano piu' "mixer
 * classico"/"flyout batteria": dicono a quale VERSIONE del sistema
 * appartiene il flyout, con le stesse parole in ogni lingua (sono nomi di
 * prodotto, come "Windows 7" e "Windows 10/11"). */
constexpr const wchar_t* kFlyoutWin7  = L"Windows 7";
constexpr const wchar_t* kFlyoutWin10 = L"Windows 10/11";

enum CtrlId {
    IDC_TAB_MAIN = 100,
    IDC_GRP_CLOCK, IDC_CHK_SECONDS, IDC_TXT_FLYOUT, IDC_CMB_FLYOUT,
    IDC_GRP_SEARCH, IDC_CHK_SEARCH,
    IDC_GRP_LANG, IDC_CMB_LANG,
    IDC_GRP_NETFLY, IDC_TXT_NETFLY, IDC_CMB_NETFLY,
    IDC_GRP_SYSFLY, IDC_CHK_CLASSIC_VOL, IDC_CHK_BATT_FLYOUT,
    IDC_GRP_EXIT, IDC_BTN_EXIT,
    IDC_TXT_ABOUT, IDC_TXT_CREDITS,
    /* v2.47 */
    IDC_GRP_TASKBAR,
    IDC_LBL_CLOCK, IDC_CMB_CLOCK,
    IDC_LBL_NET, IDC_LBL_LANG, IDC_LBL_VOLUME, IDC_CMB_VOLUME,
    IDC_LBL_BATT, IDC_CMB_BATTERY,
    IDC_GRP_NOTIF, IDC_TXT_NOTIF, IDC_BTN_CUSTOMIZE,
    IDC_GRP_AERO, IDC_TXT_AERO, IDC_CHK_AERO,
    IDC_LINK_HELP,
    IDC_TXT_TB_INFO, IDC_CHK_TB_DESKTOP, IDC_CHK_TB_ADDRESS, IDC_CHK_TB_LINKS,
    IDC_LST_TOOLBARS,   /* v2.50: elenco con caselle come nella mod */
    IDC_BTN_APPLY = 3000,
};

struct Strings {
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
    /* v2.47: campi aggiunti in coda (la finestra segue la foto della mod di
     * riferimento: gruppi Flyout, Area di notifica, Aero Peek, note in basso
     * e terza scheda per le NOSTRE barre degli strumenti). */
    const wchar_t* tab3;
    const wchar_t* grpFlyouts;
    const wchar_t* lblClock; const wchar_t* lblNetwork;
    const wchar_t* lblVolume; const wchar_t* lblBattery;
    const wchar_t* grpTaskbar; const wchar_t* lblLang;
    const wchar_t* grpNotif; const wchar_t* txtNotif; const wchar_t* btnCustomize;
    const wchar_t* grpAero; const wchar_t* txtAero; const wchar_t* chkAeroPeek;
    const wchar_t* linkHelp;
    const wchar_t* txtToolbars;
    const wchar_t* tbDesktop; const wchar_t* tbAddress; const wchar_t* tbLinks;
};

constexpr Strings kIt = {
    L"Proprietà",
    L"Barra delle applicazioni", L"Informazioni",
    L"Orologio", L"Mostra i secondi nell'orologio",
    L"Riquadro:", L"Tema classico (ricreato)", L"Windows 7",
    L"Ricerca applicazioni",
    L"Attiva ricerca applicazioni (reimplementazione opzionale)",
    L"Apri ricerca",
    L"Lingua",
    L"Uscita", L"Chiudi Win7Taskbar",
            L"Win7Taskbar è un progetto in costante sviluppo: ricrea la barra delle "
    L"applicazioni di Windows 7 sopra quella moderna. Non tutte le parti sono "
    L"complete e la precisione al 100% non è ancora garantita: alcune funzioni "
    L"sono ricostruite da zero, altre vengono agganciate e riposizionate. Se "
    L"trovi un difetto o hai un suggerimento, segnalalo: è utile e aiuta a "
    L"rendere le versioni successive più fedeli e più stabili. Limitazioni note: "
    L"la rotazione della barra non è supportata (opzione decorativa) e le "
    L"finestre di sistema (flyout, menu Start) vengono agganciate e "
    L"riposizionate, non ricreate.",
    L"Ringraziamenti: 3ds per alcune risorse grafiche e per l'ispirazione del "
    L"tema XAML.",
    L"OK", L"Annulla", L"Applica",
    L"Flyout di rete", L"Riquadro:",
    L"Windows 7 (ricreato)", L"Windows 10/11",
    L"Flyout di sistema",
    L"Mixer volume classico (SndVol)",
    L"Flyout batteria (stile Windows 7)",
    /* v2.47 */
    L"Barra degli strumenti",
    L"Flyout",
    L"Orologio:",
    L"Rete:",
    L"Volume:",
    L"Batteria:",
    L"Barra delle applicazioni",
    L"Lingua:",
    L"Area di notifica",
    L"Personalizza quali icone e notifiche appaiono nell'area di notifica.",
    L"Personalizza...",
    L"Anteprima del desktop con Aero Peek",
    L"Visualizza temporaneamente il desktop quando si passa il mouse sul pulsante Mostra desktop, alla fine della barra delle applicazioni.",
    L"Usa Aero Peek per visualizzare l'anteprima del desktop",
    L"<a>Come si personalizza la barra delle applicazioni?</a>",
    L"Aggiunge alla barra delle applicazioni le barre degli strumenti della mod. Le stesse voci sono disponibili nel menu contestuale della barra.",
    L"Barra degli strumenti Desktop",
    L"Indirizzi",
    L"Collegamenti",
};

constexpr Strings kEn = {
    L"Properties",
    L"Taskbar", L"About",
    L"Clock", L"Show seconds in the clock",
    L"Flyout:", L"Classic theme (recreated)", L"Windows 7",
    L"App search",
    L"Enable app search (optional reimplementation)",
    L"Open search",
    L"Language",
    L"Exit", L"Close Win7Taskbar",
            L"Win7Taskbar is a project in constant development that recreates the "
    L"Windows 7 taskbar on top of the modern one. Not every part is finished and "
    L"100% precision is not guaranteed yet: some features are rebuilt from "
    L"scratch, others are hooked and repositioned. If you find a bug or have a "
    L"suggestion, reporting it is useful: it helps make the next versions more "
    L"faithful and more stable. Known limitations: taskbar rotation is not "
    L"supported (decorative option); system windows (flyouts, start menu) are "
    L"hooked and repositioned, not recreated.",
    L"Credits: 3ds for some of the graphic resources and for the inspiration "
    L"behind the XAML idea.",
    L"OK", L"Cancel", L"Apply",
    L"Network flyout", L"Flyout:",
    L"Windows 7 (recreated)", L"Windows 10/11",
    L"System flyouts",
    L"Classic volume mixer (SndVol)",
    L"Battery flyout (Windows 7 style)",
    /* v2.47 */
    L"Toolbars",
    L"Flyouts",
    L"Clock:",
    L"Network:",
    L"Volume:",
    L"Battery:",
    L"Taskbar",
    L"Language:",
    L"Notification area",
    L"Customize which icons and notifications appear in the notification area.",
    L"Customize...",
    L"Preview desktop with Aero Peek",
    L"Temporarily view the desktop when you move your mouse to the Show desktop button at the end of the taskbar.",
    L"Use Aero Peek to preview the desktop",
    L"<a>How do I customize the taskbar?</a>",
    L"Adds the mod's toolbars to the taskbar. The same items are available in the taskbar context menu.",
    L"Desktop toolbar",
    L"Address",
    L"Links",
};

/* v2.42: dialogo Proprieta' tradotto in TUTTE le lingue della mod
 * (prima solo it/en). */
constexpr Strings kEs = {
    L"Propiedades",
    L"Barra de tareas", L"Acerca de",
    L"Reloj", L"Mostrar segundos en el reloj",
    L"Panel:", L"Tema clásico (recreado)", L"Windows 7",
    L"Búsqueda de aplicaciones",
    L"Activar búsqueda de aplicaciones (reimplementación opcional)",
    L"Abrir búsqueda",
    L"Idioma",
    L"Salida", L"Cerrar Win7Taskbar",
            L"Win7Taskbar es un proyecto en constante desarrollo que recrea la barra de "
    L"tareas de Windows 7 sobre la moderna. No todas las partes están terminadas "
    L"y la precisión del 100% aún no está garantizada: algunas funciones se "
    L"reconstruyen desde cero, otras se enganchan y se reposicionan. Si "
    L"encuentras un fallo o tienes una sugerencia, comunicarlo es útil: ayuda a "
    L"que las próximas versiones sean más fieles y más estables. Limitaciones "
    L"conocidas: no se admite la rotación de la barra (opción decorativa); las "
    L"ventanas del sistema (flyouts, menú Inicio) se enganchan y se "
    L"reposicionan, no se recrean.",
    L"Créditos: 3ds por algunos recursos gráficos y por la inspiración de la "
    L"idea XAML.",
    L"Aceptar", L"Cancelar", L"Aplicar",
    L"Panel de red", L"Panel:",
    L"Windows 7 (recreado)", L"Windows 10/11",
    L"Paneles del sistema",
    L"Mezclador de volumen clásico (SndVol)",
    L"Panel de batería (estilo Windows 7)",
    /* v2.47 */
    L"Barras de herramientas",
    L"Flyouts",
    L"Reloj:",
    L"Red:",
    L"Volumen:",
    L"Batería:",
    L"Barra de tareas",
    L"Idioma:",
    L"Área de notificación",
    L"Personaliza qué iconos y notificaciones aparecen en el área de notificación.",
    L"Personalizar...",
    L"Vista previa del escritorio con Aero Peek",
    L"Muestra temporalmente el escritorio al pasar el mouse sobre el botón Mostrar escritorio, al final de la barra de tareas.",
    L"Usar Aero Peek para obtener una vista previa del escritorio",
    L"<a>¿Cómo personalizo la barra de tareas?</a>",
    L"Añade las barras de herramientas de la mod a la barra de tareas. Las mismas opciones están en el menú contextual de la barra.",
    L"Barra de herramientas Escritorio",
    L"Direcciones",
    L"Vínculos",
};

constexpr Strings kFr = {
    L"Propriétés",
    L"Barre des tâches", L"À propos",
    L"Horloge", L"Afficher les secondes dans l'horloge",
    L"Panneau :", L"Thème classique (recréé)", L"Windows 7",
    L"Recherche d'applications",
    L"Activer la recherche d'applications (réimplémentation optionnelle)",
    L"Ouvrir la recherche",
    L"Langue",
    L"Quitter", L"Fermer Win7Taskbar",
            L"Win7Taskbar est un projet en développement constant qui recrée la barre "
    L"des tâches de Windows 7 par-dessus celle des versions modernes. Tout n'est "
    L"pas terminé et la précision à 100 % n'est pas encore garantie : certaines "
    L"fonctions sont reconstruites de zéro, d'autres sont accrochées et "
    L"repositionnées. Si vous trouvez un défaut ou avez une suggestion, la "
    L"signaler est utile : cela aide à rendre les prochaines versions plus "
    L"fidèles et plus stables. Limites connues : la rotation de la barre n'est "
    L"pas prise en charge (option décorative) ; les fenêtres système (flyouts, "
    L"menu Démarrer) sont accrochées et repositionnées, pas recréées.",
    L"Crédits : 3ds pour certaines ressources graphiques et l'inspiration de "
    L"l'idée XAML.",
    L"OK", L"Annuler", L"Appliquer",
    L"Panneau réseau", L"Panneau :",
    L"Windows 7 (recréé)", L"Windows 10/11",
    L"Panneaux système",
    L"Mixeur de volume classique (SndVol)",
    L"Panneau batterie (style Windows 7)",
    /* v2.47 */
    L"Barres d'outils",
    L"Volets",
    L"Horloge :",
    L"Réseau :",
    L"Volume :",
    L"Batterie :",
    L"Barre des tâches",
    L"Langue :",
    L"Zone de notification",
    L"Personnalisez les icônes et notifications qui apparaissent dans la zone de notification.",
    L"Personnaliser...",
    L"Aperçu du bureau avec Aero Peek",
    L"Affiche temporairement le bureau lorsque vous passez la souris sur le bouton Afficher le bureau, à l'extrémité de la barre des tâches.",
    L"Utiliser Aero Peek pour prévisualiser le bureau",
    L"<a>Comment personnaliser la barre des tâches ?</a>",
    L"Ajoute les barres d'outils de la mod à la barre des tâches. Les mêmes entrées sont dans le menu contextuel de la barre.",
    L"Barre d'outils Bureau",
    L"Adresses",
    L"Liens",
};

constexpr Strings kDe = {
    L"Eigenschaften",
    L"Taskleiste", L"Info",
    L"Uhr", L"Sekunden in der Uhr anzeigen",
    L"Flyout:", L"Klassisches Thema (nachgebildet)", L"Windows 7",
    L"App-Suche",
    L"App-Suche aktivieren (optionale Nachbildung)",
    L"Suche öffnen",
    L"Sprache",
    L"Beenden", L"Win7Taskbar schließen",
            L"Win7Taskbar ist ein Projekt in ständiger Entwicklung, das die "
    L"Windows-7-Taskleiste über der modernen nachbildet. Nicht alles ist fertig "
    L"und 100 % Genauigkeit ist noch nicht garantiert: einige Funktionen sind "
    L"neu gebaut, andere werden angedockt und verschoben. Wenn du einen Fehler "
    L"findest oder einen Vorschlag hast, hilft eine Meldung: so werden die "
    L"nächsten Versionen treuer und stabiler. Bekannte Grenzen: Drehung der "
    L"Taskleiste wird nicht unterstützt (dekorative Option); Systemfenster "
    L"(Flyouts, Startmenü) werden angedockt und verschoben, nicht nachgebaut.",
    L"Dank: 3ds für einige Grafikressourcen und die XAML-Idee.",
    L"OK", L"Abbrechen", L"Übernehmen",
    L"Netzwerk-Flyout", L"Flyout:",
    L"Windows 7 (nachgebildet)", L"Windows 10/11",
    L"System-Flyouts",
    L"Klassischer Lautstärkemixer (SndVol)",
    L"Akku-Flyout (Windows-7-Stil)",
    /* v2.47 */
    L"Symbolleisten",
    L"Flyouts",
    L"Uhr:",
    L"Netzwerk:",
    L"Lautstärke:",
    L"Akku:",
    L"Taskleiste",
    L"Sprache:",
    L"Infobereich",
    L"Legen Sie fest, welche Symbole und Benachrichtigungen im Infobereich angezeigt werden.",
    L"Anpassen...",
    L"Desktopvorschau mit Aero Peek",
    L"Zeigt den Desktop kurz an, wenn Sie den Mauszeiger auf die Schaltfläche „Desktop anzeigen“ am Ende der Taskleiste bewegen.",
    L"Aero Peek zum Anzeigen der Desktopvorschau verwenden",
    L"<a>Wie passe ich die Taskleiste an?</a>",
    L"Fügt die Symbolleisten der Mod zur Taskleiste hinzu. Dieselben Einträge gibt es im Kontextmenü der Taskleiste.",
    L"Symbolleiste Desktop",
    L"Adressen",
    L"Links",
};

constexpr Strings kPt = {
    L"Propriedades",
    L"Barra de tarefas", L"Sobre",
    L"Relógio", L"Mostrar segundos no relógio",
    L"Painel:", L"Tema clássico (recriado)", L"Windows 7",
    L"Pesquisa de aplicativos",
    L"Ativar pesquisa de aplicativos (reimplementação opcional)",
    L"Abrir pesquisa",
    L"Idioma",
    L"Sair", L"Fechar o Win7Taskbar",
            L"O Win7Taskbar é um projeto em desenvolvimento constante que recria a barra "
    L"de tarefas do Windows 7 sobre a moderna. Nem tudo está concluído e a "
    L"precisão de 100% ainda não é garantida: algumas partes são reconstruídas "
    L"de zero, outras são anexadas e reposicionadas. Se encontrar um defeito ou "
    L"tiver uma sugestão, comunicá-la é útil: ajuda a tornar as próximas versões "
    L"mais fiéis e mais estáveis. Limitações conhecidas: a rotação da barra não "
    L"é suportada (opção decorativa); as janelas do sistema (flyouts, menu "
    L"Iniciar) são anexadas e reposicionadas, não recriadas.",
    L"Créditos: 3ds por alguns recursos gráficos e pela inspiração da ideia "
    L"XAML.",
    L"OK", L"Cancelar", L"Aplicar",
    L"Painel de rede", L"Painel:",
    L"Windows 7 (recriado)", L"Windows 10/11",
    L"Painéis do sistema",
    L"Mixador de volume clássico (SndVol)",
    L"Painel de bateria (estilo Windows 7)",
    /* v2.47 */
    L"Barras de ferramentas",
    L"Flyouts",
    L"Relógio:",
    L"Rede:",
    L"Volume:",
    L"Bateria:",
    L"Barra de tarefas",
    L"Idioma:",
    L"Área de notação",
    L"Personalize quais ícones e notações aparecem na área de notação.",
    L"Personalizar...",
    L"Visualizar a área de trabalho com Aero Peek",
    L"Mostra temporariamente a área de trabalho quando você move o mouse para o botão Mostrar área de trabalho, no fim da barra de tarefas.",
    L"Usar o Aero Peek para visualizar a área de trabalho",
    L"<a>Como personalizo a barra de tarefas?</a>",
    L"Adiciona as barras de ferramentas da mod à barra de tarefas. Os mesmos itens estão no menu de contexto da barra.",
    L"Barra de ferramentas Área de trabalho",
    L"Endereços",
    L"Links",
};

constexpr Strings kPl = {
    L"Właściwości",
    L"Pasek zadań", L"O programie",
    L"Zegar", L"Pokazuj sekundy w zegarze",
    L"Panel:", L"Motyw klasyczny (odtworzony)", L"Windows 7",
    L"Wyszukiwanie aplikacji",
    L"Włącz wyszukiwanie aplikacji (opcjonalna reimplementacja)",
    L"Otwórz wyszukiwanie",
    L"Język",
    L"Zamknij", L"Zamknij Win7Taskbar",
            L"Win7Taskbar to projekt w ciągłym rozwoju, który odtwarza pasek zadań "
    L"Windows 7 na nowoczesnym pasku. Nie wszystko jest gotowe, a 100% zgodności "
    L"nie jest jeszcze gwarantowane: część funkcji jest budowana od zera, a "
    L"część jest zaczepiana i przestawiana. Jeśli znajdziesz błąd lub masz "
    L"sugestię, warto ją zgłosić: pomaga to uczynić kolejne wersje wierniejszymi "
    L"i stabilniejszymi. Znane ograniczenia: obrót paska nie jest obsługiwany "
    L"(opcja dekoracyjna); okna systemowe (flyouty, menu Start) są zaczepiane i "
    L"przestawiane, a nie odtwarzane.",
    L"Podziękowania: 3ds za część zasobów graficznych i inspirację pomysłu XAML.",
    L"OK", L"Anuluj", L"Zastosuj",
    L"Panel sieci", L"Panel:",
    L"Windows 7 (odtworzony)", L"Windows 10/11",
    L"Panele systemowe",
    L"Klasyczny mikser głośności (SndVol)",
    L"Panel baterii (styl Windows 7)",
    /* v2.47 */
    L"Paski narzędzi",
    L"Wysuwane okna",
    L"Zegar:",
    L"Sieć:",
    L"Głośność:",
    L"Bateria:",
    L"Pasek zadań",
    L"Język:",
    L"Obszar powiadomień",
    L"Wybierz, które ikony i powiadomienia mają być wyświetlane w obszarze powiadomień.",
    L"Dostosuj...",
    L"Podgląd pulpitu z Aero Peek",
    L"Tymczasowo pokazuje pulpit po przesunięciu wskaźnika na przycisk Pokaż pulpit na końcu paska zadań.",
    L"Użyj Aero Peek, aby wyświetlić podgląd pulpitu",
    L"<a>Jak dostosować pasek zadań?</a>",
    L"Dodaje paski narzędzi moda do paska zadań. Te same pozycje są w menu kontekstowym paska.",
    L"Pasek narzędzi Pulpit",
    L"Adresy",
    L"Łącza",
};

constexpr Strings kRu = {
    L"Свойства",
    L"Панель задач", L"О программе",
    L"Часы", L"Показывать секунды в часах",
    L"Панель:", L"Классическая тема (воссоздано)", L"Windows 7",
    L"Поиск приложений",
    L"Включить поиск приложений (необязательная реализация)",
    L"Открыть поиск",
    L"Язык",
    L"Выход", L"Закрыть Win7Taskbar",
            L"Проект Win7Taskbar находится в постоянной разработке и воссоздаёт панель "
    L"задач Windows 7 поверх современной. Готово не всё, и точность 100% пока не "
    L"гарантируется: часть функций создана заново, часть подключается и "
    L"перемещается. Если вы нашли ошибку или у вас есть предложение, сообщите о "
    L"них: это помогает делать следующие версии точнее и стабильнее. Известные "
    L"ограничения: поворот панели не поддерживается (декоративная опция); "
    L"системные окна (флайауты, меню «Пуск») подключаются и перемещаются, а не "
    L"воссоздаются.",
    L"Благодарности: 3ds за часть графики и идею XAML.",
    L"OK", L"Отмена", L"Применить",
    L"Панель сети", L"Панель:",
    L"Windows 7 (воссоздано)", L"Windows 10/11",
    L"Системные панели",
    L"Классический микшер громкости (SndVol)",
    L"Панель батареи (стиль Windows 7)",
    /* v2.47 */
    L"Панели инструментов",
    L"Всплывающие панели",
    L"Часы:",
    L"Сеть:",
    L"Громкость:",
    L"Батарея:",
    L"Панель задач",
    L"Язык:",
    L"Область уведомлений",
    L"Настройте, какие значки и уведомления отображаются в области уведомлений.",
    L"Настроить...",
    L"Просмотр рабочего стола с Aero Peek",
    L"Временно показывает рабочий стол при наведении указателя на кнопку «Свернуть все окна» в конце панели задач.",
    L"Использовать Aero Peek для предварительного просмотра рабочего стола",
    L"<a>Как настроить панель задач?</a>",
    L"Добавляет панели инструментов мода на панель задач. Те же пункты есть в контекстном меню панели.",
    L"Панель инструментов «Рабочий стол»",
    L"Адрес",
    L"Ссылки",
};

constexpr Strings kJa = {
    L"プロパティ",
    L"タスクバー", L"情報",
    L"時計", L"時計に秒を表示する",
    L"フライアウト:", L"クラシックテーマ（再現）", L"Windows 7",
    L"アプリ検索",
    L"アプリ検索を有効にする（オプションの実装）",
    L"検索を開く",
    L"言語",
    L"終了", L"Win7Taskbar を閉じる",
            L"Win7Taskbar は、モダンなタスクバーの上に Windows 7 "
    L"のタスクバーを再現する開発中のプロジェクトです。すべての機能が完成しているわけではなく、100% "
    L"の再現精度はまだ保証されていません。一部は新しく作り直し、一部は既存のウィンドウを位置調整して利用しています。不具合や提案があれば報告していただけると助かります。次のバージョンの精度と安定性の向上に役立ちます。既知の制限: "
    L"タスクバーの回転には対応していません（装飾的なオプション）。システムのウィンドウ（フライアウト、スタート メニュー）は再現ではなく位置調整です。",
    L"クレジット: 一部のグラフィックリソースと XAML の発想は 3ds による。",
    L"OK", L"キャンセル", L"適用",
    L"ネットワーク フライアウト", L"フライアウト:",
    L"Windows 7（再現）", L"Windows 10/11",
    L"システム フライアウト",
    L"クラシック ミキサー (SndVol)",
    L"バッテリー フライアウト（Windows 7 スタイル）",
    /* v2.47 */
    L"ツールバー",
    L"ポップアップ",
    L"時計:",
    L"ネットワーク:",
    L"音量:",
    L"バッテリー:",
    L"タスクバー",
    L"言語:",
    L"通知領域",
    L"通知領域に表示するアイコンと通知をカスタマイズします。",
    L"カスタマイズ...",
    L"Aero Peek によるデスクトップのプレビュー",
    L"タスクバーの端にある [デスクトップの表示] ボタンにマウスを合わせると、デスクトップを一時的に表示します。",
    L"Aero Peek を使ってデスクトップをプレビューする",
    L"<a>タスクバーをカスタマイズするには?</a>",
    L"この MOD のツールバーをタスクバーに追加します。同じ項目はタスクバーのコンテキスト メニューにもあります。",
    L"デスクトップ ツールバー",
    L"アドレス",
    L"リンク",
};

constexpr Strings kZh = {
    L"属性",
    L"任务栏", L"关于",
    L"时钟", L"在时钟中显示秒",
    L"面板:", L"经典主题（重制）", L"Windows 7",
    L"应用搜索",
    L"启用应用搜索（可选实现）",
    L"打开搜索",
    L"语言",
    L"退出", L"关闭 Win7Taskbar",
            L"Win7Taskbar 是一个持续开发中的项目，在现代任务栏之上重现 Windows 7 任务栏。并非所有部分都已完成，100% "
    L"的精确度目前仍无法保证：一些功能是重新实现的，另一些则是挂钩并重新定位系统窗口。如果你发现缺陷或有建议，反馈会很有帮助：它能让后续版本更接近原版、也更稳定。已知限制：不支持任务栏旋转（装饰性选项）；系统窗口（浮出控件、开始菜单）为挂钩并重新定位，而非重新创建。",
    L"致谢：3ds 提供部分图形资源及 XAML 灵感。",
    L"确定", L"取消", L"应用",
    L"网络面板", L"面板:",
    L"Windows 7（重制）", L"Windows 10/11",
    L"系统面板",
    L"经典音量合成器 (SndVol)",
    L"电池面板（Windows 7 风格）",
    /* v2.47 */
    L"工具栏",
    L"浮出控件",
    L"时钟:",
    L"网络:",
    L"音量:",
    L"电池:",
    L"任务栏",
    L"语言:",
    L"通知区域",
    L"自定义在通知区域中显示的图标和通知。",
    L"自定义...",
    L"使用 Aero Peek 预览桌面",
    L"将鼠标移到任务栏末端的“显示桌面”按钮时，暂时查看桌面。",
    L"使用 Aero Peek 预览桌面",
    L"<a>如何自定义任务栏?</a>",
    L"将本 MOD 的工具栏添加到任务栏。相同项目也可在任务栏右键菜单中找到。",
    L"桌面工具栏",
    L"地址",
    L"链接",
};

const Strings& StrForLang(int lang) {
    switch (lang) {
        case 1: return kEn;
        case 2: return kEs;
        case 3: return kFr;
        case 4: return kDe;
        case 5: return kPt;
        case 6: return kPl;
        case 7: return kRu;
        case 8: return kJa;
        case 9: return kZh;
        case 10: return kEn; /* Arabic uses the WPF dictionary for the managed UI. */
        default: return kIt;
    }
}

/* v2.41: etichette delle opzioni orologio in TUTTE le lingue della mod.
 * L'opzione nativa si chiama ora "Windows 7"; quella ricreata "Tema
 * classico (ricreato)" (tradotte). */
struct ClockFlyoutLabels { const wchar_t* recreated; const wchar_t* native; };
static ClockFlyoutLabels ClockLabels(int lang) {
    switch (lang) {
        case 1:  return { L"Classic theme (recreated)", L"Windows 7" };
        case 2:  return { L"Tema clásico (recreado)", L"Windows 7" };
        case 3:  return { L"Thème classique (recréé)", L"Windows 7" };
        case 4:  return { L"Klassisches Thema (nachgebildet)", L"Windows 7" };
        case 5:  return { L"Tema clássico (recriado)", L"Windows 7" };
        case 6:  return { L"Motyw klasyczny (odtworzony)", L"Windows 7" };
        case 7:  return { L"Классическая тема (воссоздано)", L"Windows 7" };
        case 8:  return { L"クラシックテーマ（再現）", L"Windows 7" };
        case 9:  return { L"经典主题（重制）", L"Windows 7" };
        default: return { L"Tema classico (ricreato)", L"Windows 7" };
    }
}

HICON GetSystemIcon(int siid) {
    SHSTOCKICONINFO sii{};
    sii.cbSize = sizeof(sii);
    W7T_SEH_TRY
    if (SUCCEEDED(SHGetStockIconInfo(static_cast<SHSTOCKICONID>(siid),
            SHGSI_ICON | SHGSI_SMALLICON, &sii))) {
        return sii.hIcon;
    }
    W7T_SEH_CATCH
    W7T_SEH_END
    return nullptr;
}

/* v2.50: elenco delle barre della pagina 3, costruito come quello della mod
 * (report senza intestazione, caselle di controllo, colonna lunga quanto il
 * controllo). L'ordine Item 0/1/2 e' quello letto in SendApply. */
void InitToolbarsList(HWND hwnd, const Strings& S,
                      bool address, bool desktop, bool links) {
    HWND hList = GetDlgItem(hwnd, IDC_LST_TOOLBARS);
    if (!hList) return;

    ListView_SetExtendedListViewStyle(hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    ListView_DeleteAllItems(hList);
    while (ListView_DeleteColumn(hList, 0)) {}

    LVCOLUMNW col{};
    col.mask = LVCF_WIDTH | LVCF_FMT;
    col.fmt  = LVCFMT_LEFT;
    col.cx   = 226;
    ListView_InsertColumn(hList, 0, &col);

    const wchar_t* names[]  = { S.tbAddress, S.tbDesktop, S.tbLinks };
    const bool     states[] = { address, desktop, links };
    for (int i = 0; i < 3; ++i) {
        LVITEMW lvi{};
        lvi.mask   = LVIF_TEXT;
        lvi.iItem  = i;
        lvi.pszText = const_cast<wchar_t*>(names[i]);
        ListView_InsertItem(hList, &lvi);
        ListView_SetCheckState(hList, i, states[i] ? TRUE : FALSE);
    }
}

/* v2.47: visibilita' delle tre pagine. In un unico posto, cosi' l'apertura e
 * il cambio di scheda non possono divergere (era il difetto classico di
 * questo tipo di dialogo: si aggiunge un controllo e ci si dimentica di
 * nasconderlo in una delle due strade). */
void ShowTabPage(HWND hwnd, int page) {
    const bool p1 = (page == 0);
    const bool p2 = (page == 1);
    const bool p3 = (page == 2);

    auto vis = [&](int idc, bool v) {
        if (HWND h = GetDlgItem(hwnd, idc)) {
            ShowWindow(h, v ? SW_SHOW : SW_HIDE);
        }
    };

    /* Pagina 1: orologio, ricerca, flyout, lingua, area di notifica. */
    vis(IDC_GRP_CLOCK, p1); vis(IDC_CHK_SECONDS, p1);
    vis(IDC_LBL_CLOCK, p1); vis(IDC_CMB_CLOCK, p1);
    vis(IDC_GRP_SEARCH, p1); vis(IDC_CHK_SEARCH, p1);
    vis(IDC_GRP_NETFLY, p1); vis(IDC_TXT_NETFLY, p1); vis(IDC_CMB_NETFLY, p1);
    vis(IDC_LBL_VOLUME, p1); vis(IDC_CMB_VOLUME, p1);
    vis(IDC_LBL_BATT, p1); vis(IDC_CMB_BATTERY, p1);
    vis(IDC_GRP_LANG, p1); vis(IDC_LBL_LANG, p1); vis(IDC_CMB_LANG, p1);
    vis(IDC_GRP_NOTIF, p1); vis(IDC_TXT_NOTIF, p1); vis(IDC_BTN_CUSTOMIZE, p1);

    /* Pagina 2: informazioni + uscita. */
    vis(IDC_TXT_ABOUT, p2);   /* v2.58: i crediti sono dentro questo testo */
    vis(IDC_GRP_EXIT, p2); vis(IDC_BTN_EXIT, p2);

    /* Pagina 3: le nostre barre degli strumenti. */
    vis(IDC_TXT_TB_INFO, p3); vis(IDC_LST_TOOLBARS, p3);
}

BOOL CALLBACK ThemeChildProc(HWND h, LPARAM) {
    SetWindowTheme(h, L"explorer", nullptr);
    return TRUE;
}

} /* namespace */

PropertiesDialog::~PropertiesDialog() {
    /* RAII: il font del dialogo e' un oggetto GDI, si distrugge qui. */
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
}

void PropertiesDialog::Show(HWND owner, int32_t lang, int32_t seconds,
                            int32_t nativeFlyout, int32_t enableSearch,
                            int32_t netFlyout, int32_t classicVolume,
                            int32_t batteryFlyout, int32_t aeroPeek,
                            int32_t toolbarDesktop, int32_t toolbarAddress,
                            int32_t toolbarLinks) {
    try {
        if (m_hWnd && IsWindow(m_hWnd)) {
            SetForegroundWindow(m_hWnd);
            return;
        }
        m_owner = owner;
        m_lang = (lang >= 0 && lang <= 9) ? lang : 0;
        m_seconds = seconds;
        m_nativeFlyout = nativeFlyout;
        m_enableSearch = enableSearch;
        m_netFlyout = (netFlyout == 1) ? 1 : 0;
        m_classicVolume = classicVolume ? 1 : 0;
        m_batteryFlyout = batteryFlyout ? 1 : 0;
        m_aeroPeek = aeroPeek ? 1 : 0;
        m_tbDesktop = toolbarDesktop ? 1 : 0;
        m_tbAddress = toolbarAddress ? 1 : 0;
        m_tbLinks = toolbarLinks ? 1 : 0;

        /* v2.47: oltre alle schede e ai controlli standard serve la classe
         * del controllo collegamento (SysLink) usato in fondo alla prima
         * pagina: senza ICC_LINK_CLASS CreateDialogIndirectParamW non riesce
         * a creare il controllo. */
        INITCOMMONCONTROLSEX icc{ sizeof(icc),
            ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS |
            ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);

        /* ---- template in memoria, identico alla mod ---- */
        BYTE* buf = new BYTE[8192];
        /* RAII: qualunque uscita da qui libera il template. */
        auto bufGuard = MakeScopeExit([buf] { delete[] buf; });
        (void)bufGuard;
        BYTE* p = buf;
        int controlCount = 0;
        auto align4 = [](BYTE*& ptr) { ptr = (BYTE*)(((UINT_PTR)ptr + 3) & ~3); };

        LPDLGTEMPLATEW pDlg = (LPDLGTEMPLATEW)p;
        pDlg->style = DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP |
                      WS_CAPTION | WS_SYSMENU;
        pDlg->dwExtendedStyle = 0;
        pDlg->cdit = 0;
        pDlg->x = 0; pDlg->y = 0;
        pDlg->cx = MAIN_WIDTH;
        pDlg->cy = MAIN_HEIGHT;
        p += sizeof(DLGTEMPLATE);
        *(WORD*)p = 0; p += 2;
        *(WORD*)p = 0; p += 2;
        *(WCHAR*)p = 0; p += 2;
        *(WORD*)p = 9; p += 2;
        const wchar_t* face = L"Segoe UI";
        memcpy(p, face, (wcslen(face) + 1) * 2);
        p += (wcslen(face) + 1) * 2;

        auto addCtrl = [&](DWORD style, DWORD exStyle, short x, short y,
                           short cx, short cy, WORD id, LPCWSTR cls, LPCWSTR cap) {
            align4(p);
            LPDLGITEMTEMPLATE pi = (LPDLGITEMTEMPLATE)p;
            pi->style = WS_CHILD | WS_VISIBLE | style;
            pi->dwExtendedStyle = exStyle;
            pi->x = x; pi->y = y; pi->cx = cx; pi->cy = cy; pi->id = id;
            p += sizeof(DLGITEMTEMPLATE);
            memcpy(p, cls, (wcslen(cls) + 1) * 2);
            p += (wcslen(cls) + 1) * 2;
            memcpy(p, cap, (wcslen(cap) + 1) * 2);
            p += (wcslen(cap) + 1) * 2;
            *(WORD*)p = 0; p += 2;
            controlCount++;
        };

        addCtrl(TCS_TABS | WS_TABSTOP, 0, 6, 6, 250, 278, IDC_TAB_MAIN,
                L"SysTabControl32", L"");
        /* ============================================================
         * PAGINA 1 - "Barra delle applicazioni"
         *
         * Impostazioni NOSTRE nella grafica classica del dialogo: orologio
         * (secondi + riquadro), ricerca applicazioni, flyout
         * (rete/volume/batteria), lingua e l'AREA DI NOTIFICA presa dalla foto
         * di riferimento, con il pulsante "Personalizza..." che apre
         * esattamente quello che apre il menu di overflow della barra.
         * Niente Aero Peek, niente collegamenti esterni.
         * ============================================================ */
        /* GRUPPO 1 - FLYOUT. E' il primo gruppo della pagina, in cima a
         * tutto, come nella foto di riferimento: quattro righe etichetta +
         * tendina (orologio, rete, volume, batteria). Passo fra le righe 16
         * DLU (tendina alta 14 + 2 di aria), etichette a 18, tendine a 72
         * larghe 172: la stessa griglia del resto del dialogo. */
        addCtrl(BS_GROUPBOX, 0, 12, 30, 238, 74, IDC_GRP_NETFLY, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 40, 50, 10, IDC_LBL_CLOCK, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 38, 172, 80, IDC_CMB_CLOCK, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 56, 50, 10, IDC_TXT_NETFLY, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 54, 172, 80, IDC_CMB_NETFLY, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 72, 50, 10, IDC_LBL_VOLUME, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 70, 172, 80, IDC_CMB_VOLUME, L"ComboBox", L"");
        addCtrl(SS_LEFT, 0, 18, 88, 50, 10, IDC_LBL_BATT, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 86, 172, 80, IDC_CMB_BATTERY, L"ComboBox", L"");

        /* GRUPPO 2 - OROLOGIO: una sola casella, gruppo alto 30. */
        addCtrl(BS_GROUPBOX, 0, 12, 108, 238, 30, IDC_GRP_CLOCK, L"Button", L"");
        addCtrl(BS_AUTOCHECKBOX | WS_TABSTOP, 0, 18, 118, 226, 10, IDC_CHK_SECONDS, L"Button", L"");

        /* GRUPPO 3 - RICERCA APPLICAZIONI. La casella e' su DUE righe
         * (BS_MULTILINE, alto 20): la sua etichetta e' lunga e in tedesco,
         * polacco e russo non entrerebbe in una riga sola - prima si leggeva
         * "Attiva ricerca a..." e sembrava che la stringa mancasse. Il
         * pulsante sta sotto, dentro il gruppo (142..188). */
        /* v2.54: il pulsante "Apri ricerca" e' stato TOLTO su richiesta.
         * Il gruppo "Ricerca applicazioni" resta (con la sua casella e la
         * sua opzione), e il gruppo si stringe attorno alla casella: senza
         * il pulsante l'altezza che serviva era 46, ora ne bastano 34. */
        addCtrl(BS_GROUPBOX, 0, 12, 142, 238, 34, IDC_GRP_SEARCH, L"Button", L"");
        addCtrl(BS_AUTOCHECKBOX | WS_TABSTOP | BS_MULTILINE, 0, 18, 152, 226, 20, IDC_CHK_SEARCH, L"Button", L"");

        /* GRUPPO 4 - LINGUA (al posto della sezione Aero Peek della foto). */
        addCtrl(BS_GROUPBOX, 0, 12, 192, 238, 30, IDC_GRP_LANG, L"Button", L"");
        addCtrl(SS_LEFT, 0, 18, 202, 50, 10, IDC_LBL_LANG, L"Static", L"");
        addCtrl(CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 72, 200, 130, 80, IDC_CMB_LANG, L"ComboBox", L"");

        /* GRUPPO 5 - AREA DI NOTIFICA: testo su due righe (20) + pulsante,
         * gruppo 226..278, tutto dentro l'area della scheda (~282). */
        addCtrl(BS_GROUPBOX, 0, 12, 226, 238, 52, IDC_GRP_NOTIF, L"Button", L"");
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 18, 236, 226, 20, IDC_TXT_NOTIF, L"Static", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 18, 258, 76, 14, IDC_BTN_CUSTOMIZE, L"Button", L"");

        /* ============================================================
         * PAGINA 2 - "Informazioni"
         * ============================================================ */
        /* v2.58 - the About page is back to a FIXED template layout, and the
         * whole text (what the project is + the credits) lives in ONE box:
         *
         *   - IDC_TXT_ABOUT holds everything: the code fills it with "about"
         *     followed by the credits, so the two cannot overlap and the page
         *     cannot be left with a hole in the middle (both bugs reported on
         *     the previous layout, where the two blocks were placed from the
         *     measured height of the text);
         *   - the exit group is anchored to the BOTTOM of the page, so the
         *     close button sits at the bottom left of the information area,
         *     just above the OK / Cancel / Apply row, and its position does
         *     not depend on how long the text is;
         *   - the separate credits control is gone: the text box swallowed it.
         *
         * Coordinates are plain dialog units from the template - nothing on
         * this page is measured or computed at run time any more. The page
         * still ends at ~282 units, like the other two. */
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 14, 22, 234, 196, IDC_TXT_ABOUT, L"Static", L"");
        addCtrl(BS_GROUPBOX, 0, 14, 224, 234, 40, IDC_GRP_EXIT, L"Button", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 20, 240, 110, 14, IDC_BTN_EXIT, L"Button", L"");

        /* ============================================================
         * PAGINA 3 - "Toolbar"
         * Le TRE barre della mod, nell'ordine richiesto: Indirizzi,
         * Desktop, Collegamenti. Le caselle chiamano gli stessi comandi del
         * menu "Barre degli strumenti" della barra (SetBandVisible lato
         * gestito): accendono le bande della MOD, non le barre di Windows.
         * ============================================================ */
        /* v2.50: PAGINA 3 copiata DALLA MOD, spaziature comprese: il testo
         * informativo in alto (14,22) e sotto un unico elenco con le caselle
         * (SysListView32 in stile report, senza intestazione), largo 230 e
         * alto 160. Le tre caselle separate di prima erano troppo distanti
         * fra loro: qui le righe hanno il passo compatto della mod. */
        addCtrl(SS_LEFT | SS_EDITCONTROL, 0, 14, 22, 234, 26, IDC_TXT_TB_INFO, L"Static", L"");
        addCtrl(LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | WS_BORDER | WS_TABSTOP,
                0, 16, 52, 230, 160, IDC_LST_TOOLBARS, L"SysListView32", L"");

        // ---- pulsanti standard 50x14, come la mod ----
        addCtrl(BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 88, 292, 50, 14, IDOK, L"Button", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 144, 292, 50, 14, IDCANCEL, L"Button", L"");
        addCtrl(BS_PUSHBUTTON | WS_TABSTOP, 0, 200, 292, 50, 14, IDC_BTN_APPLY, L"Button", L"");

        pDlg->cdit = controlCount;
        m_hWnd = CreateDialogIndirectParamW(GetModuleHandleW(nullptr),
            (LPDLGTEMPLATE)buf, owner, DlgProc,
            reinterpret_cast<LPARAM>(this));

        if (m_hWnd) ShowWindow(m_hWnd, SW_SHOW);
    } catch (...) {
        /* mai propagare */
    }
}

void PropertiesDialog::SendApply(bool openSearch, bool closeApp) {
    if (m_owner == nullptr || !IsWindow(m_owner) || m_hWnd == nullptr) return;

    PropsApplyMsg msg{};
    msg.seconds =
        (SendDlgItemMessageW(m_hWnd, IDC_CHK_SECONDS, BM_GETCHECK, 0, 0)
            & BST_CHECKED) ? 1 : 0;
    msg.enableSearch =
        (SendDlgItemMessageW(m_hWnd, IDC_CHK_SEARCH, BM_GETCHECK, 0, 0)
            & BST_CHECKED) ? 1 : 0;
    msg.classicVolume =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_VOLUME, CB_GETCURSEL, 0, 0) == 1)
            ? 0 : 1;
    msg.batteryFlyout =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_BATTERY, CB_GETCURSEL, 0, 0) == 1)
            ? 0 : 1;
    /* Tendine: indice 0 = versione ricreata dalla mod, 1 = quella del sistema.
     * Ogni impostazione ha la sua polarita' (vedi le colonne nel messaggio). */
    msg.nativeFlyout =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_CLOCK, CB_GETCURSEL, 0, 0) == 1)
            ? 1 : 0;
    msg.netFlyoutMode =
        (SendDlgItemMessageW(m_hWnd, IDC_CMB_NETFLY, CB_GETCURSEL, 0, 0) == 1)
            ? 1 : 0;
    /* La finestra non ha piu' il controllo Aero Peek: il campo resta nel
     * pacchetto (compatibilita' con i campi aggiunti in coda) e rimanda
     * indietro il valore ricevuto all'apertura, senza toccarlo. */
    msg.aeroPeek = m_aeroPeek ? 1 : 0;
    {
        HWND hTL = GetDlgItem(m_hWnd, IDC_LST_TOOLBARS);
        msg.toolbarAddress = (hTL && ListView_GetCheckState(hTL, 0)) ? 1 : 0;
        msg.toolbarDesktop = (hTL && ListView_GetCheckState(hTL, 1)) ? 1 : 0;
        msg.toolbarLinks   = (hTL && ListView_GetCheckState(hTL, 2)) ? 1 : 0;
    }
    {
        const int32_t langSel = static_cast<int32_t>(
            SendDlgItemMessageW(m_hWnd, IDC_CMB_LANG, CB_GETCURSEL, 0, 0));
        msg.lang = (langSel >= 0 && langSel <= 10) ? langSel : 0;
    }
    msg.openSearch = openSearch ? 1 : 0;
    msg.closeApp = closeApp ? 1 : 0;

    COPYDATASTRUCT cds{};
    cds.dwData = kPropsCopyDataId;
    cds.cbData = sizeof(msg);
    cds.lpData = &msg;
    SendMessageW(m_owner, WM_COPYDATA,
                 reinterpret_cast<WPARAM>(m_hWnd),
                 reinterpret_cast<LPARAM>(&cds));
}

INT_PTR CALLBACK PropertiesDialog::DlgProc(HWND hwnd, UINT msg,
                                           WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<PropertiesDialog*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        self = reinterpret_cast<PropertiesDialog*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hWnd = hwnd;

        HICON hIcon = GetSystemIcon(SIID_TASKBAR);
        if (hIcon) {
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        }

        const Strings& S = StrForLang(self->m_lang);   /* v2.42: tutte le lingue */

        {
            /* RAII: l'HDC si rilascia uscendo dal blocco, anche se una delle
             * chiamate fallisse o tornasse prima. */
            HDC hdc = GetDC(hwnd);
            auto dcGuard = MakeScopeExit([hwnd, hdc] { ReleaseDC(hwnd, hdc); });
            (void)dcGuard;
            int ptPx = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);

            /* v2.47: il font e' di proprieta' dell'oggetto dialogo: si crea
             * qui e si distrugge nel distruttore (o alla riapertura). Prima
             * ogni apertura lasciava per strada un HFONT. */
            if (self->m_font) {
                DeleteObject(self->m_font);
                self->m_font = nullptr;
            }
            self->m_font = CreateFontW(ptPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            if (self->m_font) {
                SendMessageW(hwnd, WM_SETFONT, (WPARAM)self->m_font, TRUE);
                EnumChildWindows(hwnd, [](HWND h, LPARAM fp) -> BOOL {
                    SendMessageW(h, WM_SETFONT, fp, TRUE);
                    return TRUE;
                }, (LPARAM)self->m_font);
            }
        }

        SetWindowTextW(hwnd, S.title);

        /* v2.57: the dialog carries the project icon (embedded in this module
         * by resources/app.rc, IDI_APPICON = 101). Loaded from the module of
         * this file, not from the executable: the dialog is created by the
         * native core, so that is where the resource lives. */
        {
            HINSTANCE hMod = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&PropertiesDialog::DlgProc), &hMod);
            if (hMod) {
                HICON hBig = static_cast<HICON>(LoadImageW(hMod, MAKEINTRESOURCEW(101),
                                                          IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
                HICON hSmall = static_cast<HICON>(LoadImageW(hMod, MAKEINTRESOURCEW(101),
                                                            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
                if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
                if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
            }
        }
        EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);
        EnumChildWindows(hwnd, ThemeChildProc, 0);

        /* Tre schede: barra delle applicazioni | informazioni | barre degli
         * strumenti (la terza e' nostra, con le nostre barre). */
        HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
        TCITEMW ti{ TCIF_TEXT, 0, 0, nullptr, 0 };
        ti.pszText = const_cast<wchar_t*>(S.tab1);
        TabCtrl_InsertItem(hTab, 0, &ti);
        ti.pszText = const_cast<wchar_t*>(S.tab2);
        TabCtrl_InsertItem(hTab, 1, &ti);
        ti.pszText = const_cast<wchar_t*>(S.tab3);
        TabCtrl_InsertItem(hTab, 2, &ti);
        TabCtrl_SetCurSel(hTab, 0);

        SetDlgItemTextW(hwnd, IDC_GRP_CLOCK, S.grpClock);
        SetDlgItemTextW(hwnd, IDC_CHK_SECONDS, S.chkSeconds);
        SetDlgItemTextW(hwnd, IDC_LBL_CLOCK, S.lblClock);
        SetDlgItemTextW(hwnd, IDC_GRP_SEARCH, S.grpSearch);
        SetDlgItemTextW(hwnd, IDC_CHK_SEARCH, S.chkSearch);
        SetDlgItemTextW(hwnd, IDC_GRP_NETFLY, S.grpFlyouts);   /* "Flyout" */
        SetDlgItemTextW(hwnd, IDC_TXT_NETFLY, S.lblNetwork);
        SetDlgItemTextW(hwnd, IDC_LBL_VOLUME, S.lblVolume);
        SetDlgItemTextW(hwnd, IDC_LBL_BATT, S.lblBattery);
        SetDlgItemTextW(hwnd, IDC_GRP_LANG, S.grpLang);
        SetDlgItemTextW(hwnd, IDC_LBL_LANG, S.lblLang);
        SetDlgItemTextW(hwnd, IDC_GRP_NOTIF, S.grpNotif);
        SetDlgItemTextW(hwnd, IDC_TXT_NOTIF, S.txtNotif);
        SetDlgItemTextW(hwnd, IDC_BTN_CUSTOMIZE, S.btnCustomize);
        SetDlgItemTextW(hwnd, IDC_GRP_EXIT, S.grpExit);
        SetDlgItemTextW(hwnd, IDC_BTN_EXIT, S.btnExit);
        /* v2.58: one text, two paragraphs - what the project is, then the
         * credits - kept apart by a blank line. */
        {
            std::wstring aboutText = S.about;
            aboutText += L"\n\n";
            aboutText += S.credits;
            SetDlgItemTextW(hwnd, IDC_TXT_ABOUT, aboutText.c_str());
        }
        SetDlgItemTextW(hwnd, IDC_TXT_TB_INFO, S.txtToolbars);
        SetDlgItemTextW(hwnd, IDOK, S.ok);
        SetDlgItemTextW(hwnd, IDCANCEL, S.cancel);
        SetDlgItemTextW(hwnd, IDC_BTN_APPLY, S.apply);

        HWND hCL = GetDlgItem(hwnd, IDC_CMB_LANG);
        ComboBox_AddString(hCL, L"Italiano");
        ComboBox_AddString(hCL, L"English");
        ComboBox_AddString(hCL, L"Español");
        ComboBox_AddString(hCL, L"Français");
        ComboBox_AddString(hCL, L"Deutsch");
        ComboBox_AddString(hCL, L"Português (Brasil)");
        ComboBox_AddString(hCL, L"Polski");
        ComboBox_AddString(hCL, L"Русский");
        ComboBox_AddString(hCL, L"日本語");
        ComboBox_AddString(hCL, L"中文 (简体)");
        ComboBox_AddString(hCL, L"العربية");
        ComboBox_SetCurSel(hCL, self->m_lang);

        /* v2.49: LE QUATTRO TENDINE DEI FLYOUT ERANO VUOTE. Una combo senza
         * voci non ha nulla da mostrare: aprirla faceva comparire un
         * rettangolo bianco vuoto con la barra di scorrimento, che copriva
         * mezza finestra (ed e' il motivo per cui le etichette sotto
         * sembravano "sparite"). Ora ogni tendina ha le sue voci, negli
         * stessi termini del resto del programma, e parte dallo stato
         * corrente: l'indice 0 e' sempre la versione ricreata dalla mod,
         * l'indice 1 quella del sistema (la polarita' di ogni campo e'
         * quella che il pacchetto WM_COPYDATA si aspetta). */
        HWND hCC = GetDlgItem(hwnd, IDC_CMB_CLOCK);
        ComboBox_AddString(hCC, S.flyRecreated);   /* 0 = ricreato */
        ComboBox_AddString(hCC, S.flyNative);      /* 1 = sistema */
        ComboBox_SetCurSel(hCC, self->m_nativeFlyout ? 1 : 0);

        HWND hCN = GetDlgItem(hwnd, IDC_CMB_NETFLY);
        ComboBox_AddString(hCN, S.netWin7);        /* 0 = ricreato */
        ComboBox_AddString(hCN, S.netModern);      /* 1 = sistema */
        ComboBox_SetCurSel(hCN, self->m_netFlyout ? 1 : 0);

        HWND hCV = GetDlgItem(hwnd, IDC_CMB_VOLUME);
        ComboBox_AddString(hCV, kFlyoutWin7);      /* 0 = flyout stile Windows 7 */
        ComboBox_AddString(hCV, kFlyoutWin10);     /* 1 = flyout del sistema */
        ComboBox_SetCurSel(hCV, self->m_classicVolume ? 0 : 1);

        HWND hCB = GetDlgItem(hwnd, IDC_CMB_BATTERY);
        ComboBox_AddString(hCB, kFlyoutWin7);      /* 0 = flyout stile Windows 7 */
        ComboBox_AddString(hCB, kFlyoutWin10);     /* 1 = flyout del sistema */
        ComboBox_SetCurSel(hCB, self->m_batteryFlyout ? 0 : 1);

        SendDlgItemMessageW(hwnd, IDC_CHK_SECONDS, BM_SETCHECK,
                            self->m_seconds ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(hwnd, IDC_CHK_SEARCH, BM_SETCHECK,
                            self->m_enableSearch ? BST_CHECKED : BST_UNCHECKED, 0);
        InitToolbarsList(hwnd, S, self->m_tbAddress != 0,
                         self->m_tbDesktop != 0, self->m_tbLinks != 0);

        // Applica parte disattivato, come nella mod
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), FALSE);

        // pagina iniziale: la 1, con le altre due nascoste
        ShowTabPage(hwnd, 0);

        return TRUE;
    }
    case WM_GETMINMAXINFO: {
        // non ridimensionabile: min=max=attuale, come la mod
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        RECT rc; GetWindowRect(hwnd, &rc);
        mmi->ptMinTrackSize.x = mmi->ptMaxTrackSize.x = rc.right - rc.left;
        mmi->ptMaxTrackSize.y = mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD act = HIWORD(wp);
        if (!self) return FALSE;
        if ((act == BN_CLICKED || act == CBN_SELCHANGE) &&
            id != IDOK && id != IDCANCEL && id != IDC_BTN_APPLY &&
            id != IDC_BTN_EXIT &&
            id != IDC_BTN_CUSTOMIZE) {
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), TRUE);
        }
        if (id == IDOK) {
            self->SendApply(false, false);
            DestroyWindow(hwnd);
        } else if (id == IDCANCEL) {
            DestroyWindow(hwnd);
        } else if (id == IDC_BTN_APPLY) {
            self->SendApply(false, false);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), FALSE);
        } else if (id == IDC_BTN_EXIT) {
            self->SendApply(false, true);
            DestroyWindow(hwnd);
        } else if (id == IDC_BTN_CUSTOMIZE) {
            /* "Personalizza..." dell'area di notifica: STESSO comando del menu
             * di overflow della barra. E' il core nativo che apre la pagina
             * vera di Windows (CLSID shell:::{05D7B0F4-2121-4EFF-BF6B-ED3F69B894D9},
             * con i suoi ripieghi): qui non si inventa nessun percorso
             * alternativo, perche' il comportamento deve essere identico a
             * quello che l'utente ottiene dall'altro ingresso. */
            if (W7T_OpenNotificationIconsSettings() != W7T_OK) {
                /* v2.50: ULTIMO ripiego identico a quello del link
                 * "Personalizza..." della barra (OpenNotificationAreaIconsApplet):
                 * se la pagina classica non si apre, si apre l'equivalente
                 * moderno. Cosi' i due ingressi fanno esattamente la stessa
                 * cosa, nello stesso ordine. */
                ShellExecuteW(nullptr, L"open", L"ms-settings:taskbar",
                              nullptr, nullptr, SW_SHOW);
            }
        }
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lp;
        if (hdr->idFrom == IDC_LST_TOOLBARS && hdr->code == LVN_ITEMCHANGED) {
            /* v2.50: le caselle dell'elenco non passano da WM_COMMAND:
             * qualunque modifica riaccende "Applica" come le altre voci. */
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_APPLY), TRUE);
        }
        if (hdr->idFrom == IDC_TAB_MAIN && hdr->code == TCN_SELCHANGE) {
            int sel = (int)SendDlgItemMessageW(hwnd, IDC_TAB_MAIN,
                                               TCM_GETCURSEL, 0, 0);
            ShowTabPage(hwnd, (sel >= 0 && sel <= 2) ? sel : 0);
        }
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return TRUE;
    case WM_DESTROY:
        if (self) self->m_hWnd = nullptr;
        return TRUE;
    }
    return FALSE;
}

} /* namespace w7t */
