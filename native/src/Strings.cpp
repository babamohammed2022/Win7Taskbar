/*
 * Win7Taskbar - Core nativo - Lingue e tabelle delle stringhe
 * Copyright (c) 2026 Win7Taskbar contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Vedi Strings.h per il progetto di questo file: UNA sola sorgente per le
 * lingue supportate e per le stringhe generate dall'app.
 *
 * Le tabelle "lunghe" (Proprieta' e flyout batteria) sono state spostate qui
 * dai rispettivi .cpp senza cambiare una parola delle traduzioni esistenti:
 * e' stato aggiunto l'arabo, che prima ripiegava sull'inglese.
 */

#include "Strings.h"
#include "Common.h"

#include <windows.h>
#include <atomic>
#include <cstring>

namespace w7t {
namespace {

/* Codice a due lettere -> indice. Unica tabella delle lingue: la usa anche
 * il selettore della finestra Proprieta', che la scorre invece di elencare
 * le voci a mano. */
const LanguageEntry kLanguages[kLangCount] = {
    { "it", L"Italiano" },
    { "en", L"English" },
    { "es", L"Español" },
    { "fr", L"Français" },
    { "de", L"Deutsch" },
    { "pt", L"Português (Brasil)" },
    { "pl", L"Polski" },
    { "ru", L"Русский" },
    { "ja", L"日本語" },
    { "zh", L"中文 (简体)" },
    { "ar", L"العربية" },
};

/* ------------------------------------------------------------------------ */
/*  Finestra Proprieta'                                                      */
/* ------------------------------------------------------------------------ */
constexpr PropStrings kPropIt = {
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
    /* v3.5: indicatore della lingua di input. */
    L"Indicatore lingua:", L"Nascosto", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Gestione attività:", L"Automatico (predefinito)", L"Gestione attività", L"Gestione attività (classico)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (ricreato)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Esegui automaticamente all'accesso",
};
constexpr PropStrings kPropEn = {
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
    /* v3.5: input language indicator. */
    L"Input indicator:", L"Hidden", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Task Manager:", L"Automatic (default)", L"Task Manager (Windows 11)", L"Task Manager (Legacy)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (recreated)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Automatically start at logon",
};
constexpr PropStrings kPropEs = {
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
    L"Indicador de idioma:", L"Oculto", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Administrador de tareas:", L"Automático (predeterminado)", L"Administrador de tareas (Windows 11)", L"Administrador de tareas (heredado)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (recreado)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Iniciar automáticamente al iniciar sesión",
};
constexpr PropStrings kPropFr = {
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
    L"Indicateur de langue :", L"Masqué", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Gestionnaire des tâches:", L"Automatique (par défaut)", L"Gestionnaire des tâches (Windows 11)", L"Gestionnaire des tâches (hérité)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (recréé)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Démarrer automatiquement à la connexion",
};
constexpr PropStrings kPropDe = {
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
    L"Eingabeindikator:", L"Ausgeblendet", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Task-Manager:", L"Automatisch (Standard)", L"Task-Manager (Windows 11)", L"Task-Manager (Legacy)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (nachgebaut)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Automatisch mit Windows starten",
};
constexpr PropStrings kPropPt = {
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
    L"Indicador de idioma:", L"Oculto", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Gestor de Tarefas:", L"Automático (predefinição)", L"Gestor de Tarefas (Windows 11)", L"Gestor de Tarefas (Legado)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (recriado)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Iniciar automaticamente no logon",
};
constexpr PropStrings kPropPl = {
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
    L"Wskaźnik języka:", L"Ukryty", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Menedżer zadań:", L"Automatycznie (domyślne)", L"Menedżer zadań (Windows 11)", L"Menedżer zadań (starszy)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (odtworzony)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Uruchamiaj podczas startu systemu",
};
constexpr PropStrings kPropRu = {
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
    L"Индикатор языка:", L"Скрыт", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"Диспетчер задач:", L"Автоматически (по умолчанию)", L"Диспетчер задач (Windows 11)", L"Диспетчер задач (классический)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (воссозданный)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"Запускать при входе в систему",
};
constexpr PropStrings kPropJa = {
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
    L"入力インジケーター:", L"非表示", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"タスク マネージャー:", L"自動 (既定)", L"タスク マネージャー (Windows 11)", L"タスク マネージャー (レガシ)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (再現)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"ログオン時に自動的に起動する",
};
constexpr PropStrings kPropZh = {
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
    L"输入指示器:", L"隐藏", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
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
    /* Task Manager selector (Windows 11 only). */
    L"任务管理器:", L"自动（默认）", L"任务管理器 (Windows 11)", L"任务管理器（旧版）",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (重新创建)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"开机时自动启动",
};

/* Arabic - contributed by mahmogamer (see CREDITS.txt). */
constexpr PropStrings kPropAr = {
    L"خصائص",
    L"شريط المهام", L"حول",
    L"الساعة", L"إظهار الثواني في الساعة",
    L"القائمة المنبثقة:", L"السمة الكلاسيكية (أُعيد إنشاؤها)", L"Windows 7",
    L"البحث عن التطبيقات",
    L"تفعيل البحث عن التطبيقات (إعادة تنفيذ اختيارية)",
    L"فتح البحث",
    L"اللغة",
    L"خروج", L"إغلاق Win7Taskbar",
            L"‏Win7Taskbar مشروع قيد التحسين المستمر يحاكي شريط مهام ويندوز ٧ فوق الشريط الحديث: الدقة ستتحسّن بمرور الوقت. من القيود المعروفة: تدوير الشريط (إلى الجانب أو إلى الأعلى) غير مدعوم، لذا فإن الخيار المتعلق بذلك شكلي حالياً؛ ونوافذ النظام (القوائم المنبثقة وقائمة ابدأ) تُربط ويُعاد تحديد موضعها، ولا يُعاد إنشاؤها.",
    L"شكر وتقدير: 3ds لبعض الموارد الرسومية وللإلهام وراء فكرة XAML. الترجمة العربية من إعداد mahmogamer.",
    L"موافق", L"إلغاء", L"تطبيق",
    L"القائمة المنبثقة للشبكة", L"القائمة المنبثقة:",
    L"Windows 7 (أُعيد إنشاؤها)", L"Windows 10/11",
    L"القوائم المنبثقة للنظام",
    L"مازج الصوت الكلاسيكي (SndVol)",
    L"القائمة المنبثقة للبطارية (بنمط Windows 7)",
    /* v2.47 */
    L"أشرطة الأدوات",
    L"القوائم المنبثقة",
    L"الساعة:",
    L"الشبكة:",
    L"الصوت:",
    L"البطارية:",
    L"شريط المهام",
    L"اللغة:",
    L"مؤشر الإدخال:", L"مخفي", L"Windows 7", L"Windows 8.1", L"Windows 10/11",
    L"منطقة الإشعارات",
    L"تخصيص الأيقونات والإشعارات التي تظهر في منطقة الإشعارات.",
    L"تخصيص...",
    L"معاينة سطح المكتب باستخدام Aero Peek",
    L"عرض سطح المكتب مؤقتًا عند نقل مؤشر الفأرة إلى زر «إظهار سطح المكتب» في نهاية شريط المهام.",
    L"استخدام Aero Peek لمعاينة سطح المكتب",
    L"<a>كيف أخصّص شريط المهام؟</a>",
    L"إضافة أشرطة أدوات التعديل إلى شريط المهام. العناصر نفسها متاحة في قائمة شريط المهام.",
    L"شريط أدوات سطح المكتب",
    L"العنوان",
    L"الارتباطات",
    /* Task Manager selector (Windows 11 only). */
    L"إدارة المهام:", L"تلقائي (افتراضي)", L"إدارة المهام (Windows 11)", L"إدارة المهام (قديم)",
    /* v3.8: terza voce della tendina rete. */
    L"Windows 8 (معاد إنشاؤه)",
    /* v1.21.37: avvio automatico (stringa "autostart" di RetroBar). */
    L"بدء التشغيل تلقائيًا عند تسجيل الدخول",
};

/* ------------------------------------------------------------------------ */
/*  Flyout batteria                                                          */
/* ------------------------------------------------------------------------ */
constexpr BattStrings kBattIt = {
        L"%d%% di carica rimanente",
        L"Tempo restante: %d h %02d min (%d%%)",
        L"In carica (%d%%)",
        L"Carica completa",
        L"Batteria non rilevata",
        L"Altre opzioni di risparmio energia"
};
constexpr BattStrings kBattEn = {
        L"%d%% battery remaining",
        L"Time remaining: %d h %02d min (%d%%)",
        L"Charging (%d%%)",
        L"Fully charged",
        L"No battery detected",
        L"More power options"
};
constexpr BattStrings kBattEs = {
        L"%d%% de batería restante",
        L"Tiempo restante: %d h %02d min (%d%%)",
        L"Cargando (%d%%)",
        L"Batería cargada",
        L"Batería no detectada",
        L"Más opciones de energía"
};
constexpr BattStrings kBattFr = {
        L"%d%% de batterie restante",
        L"Temps restant : %d h %02d min (%d%%)",
        L"En charge (%d%%)",
        L"Batterie chargée",
        L"Aucune batterie détectée",
        L"Plus d'options d'alimentation"
};
constexpr BattStrings kBattDe = {
        L"%d%% Akkuladung verbleibend",
        L"Verbleibend: %d h %02d min (%d%%)",
        L"Wird aufgeladen (%d%%)",
        L"Voll aufgeladen",
        L"Kein Akku erkannt",
        L"Weitere Energieoptionen"
};
constexpr BattStrings kBattPt = {
        L"%d%% de bateria restante",
        L"Tempo restante: %d h %02d min (%d%%)",
        L"Carregando (%d%%)",
        L"Bateria carregada",
        L"Nenhuma bateria detectada",
        L"Mais opções de energia"
};
constexpr BattStrings kBattPl = {
        L"Pozostało %d%% baterii",
        L"Pozostały czas: %d h %02d min (%d%%)",
        L"Ładowanie (%d%%)",
        L"W pełni naładowana",
        L"Nie wykryto baterii",
        L"Więcej opcji zasilania"
};
constexpr BattStrings kBattRu = {
        L"Осталось %d%% заряда",
        L"Осталось: %d ч %02d мин (%d%%)",
        L"Зарядка (%d%%)",
        L"Батарея заряжена",
        L"Батарея не обнаружена",
        L"Дополнительные параметры питания"
};
constexpr BattStrings kBattJa = {
        L"残り %d%%",
        L"残り時間: %d 時間 %02d 分 (%d%%)",
        L"充電中 (%d%%)",
        L"満充電",
        L"バッテリーが見つかりません",
        L"その他の電源オプション"
};
constexpr BattStrings kBattZh = {
        L"剩余 %d%%",
        L"剩余时间: %d 小时 %02d 分钟 (%d%%)",
        L"正在充电 (%d%%)",
        L"电量已满",
        L"未检测到电池",
        L"更多电源选项"
};

/* Arabic - contributed by mahmogamer (see CREDITS.txt). */
constexpr BattStrings kBattAr = {
    L"متبقٍ %d%% من البطارية",
    L"الوقت المتبقي: %d س %02d د (%d%%)",
    L"جارٍ الشحن (%d%%)",
    L"مشحونة بالكامل",
    L"لم يتم العثور على بطارية",
    L"مزيد من خيارات الطاقة"
};

/* ------------------------------------------------------------------------ */
/*  Stringhe brevi condivise                                                 */
/*                                                                            */
/*  Ricerca applicazioni e menu contestuali: sono generate dall'app, quindi  */
/*  seguono la lingua scelta. L'ordine dei campi e' quello di StrId.         */
/* ------------------------------------------------------------------------ */
struct ShortStrings {
    const wchar_t* bestMatch;
    const wchar_t* programs;
    const wchar_t* recentFiles;
    const wchar_t* noResults;
    const wchar_t* scanning;
    const wchar_t* open;
    const wchar_t* runAsAdmin;
    const wchar_t* openLocation;
    const wchar_t* appItem;

    const wchar_t* sysRestore;
    const wchar_t* sysMove;
    const wchar_t* sysSize;
    const wchar_t* sysMinimize;
    const wchar_t* sysMaximize;
    const wchar_t* sysClose;

    const wchar_t* groupMinimize;
    const wchar_t* groupClose;

    const wchar_t* overflowCustomize;

    /* v3.5: menu contestuali delle icone di sistema ricreate. */
    const wchar_t* ctxVolMixer;
    const wchar_t* ctxPlayback;
    const wchar_t* ctxRecording;
    const wchar_t* ctxSounds;
    const wchar_t* ctxTroubleshoot;
    const wchar_t* ctxNetCenter;
    const wchar_t* ctxMobility;
    const wchar_t* ctxPower;
};

/* it: "Apri", "Esegui come amministratore", "Apri percorso file",
 * "Corrispondenza migliore", "Programmi", "File recenti",
 * "Nessun elemento corrisponde alla ricerca.", "Scansione applicazioni...". */
constexpr ShortStrings kShortIt = {
    L"Corrispondenza migliore", L"Programmi", L"File recenti",
    L"Nessun elemento corrisponde alla ricerca.", L"Scansione applicazioni...",
    L"Apri", L"Esegui come amministratore", L"Apri percorso file",
    L"Applicazione",
    L"&Ripristina", L"&Sposta", L"&Ridimensiona", L"R&iduci a icona",
    L"I&ngrandisci", L"&Chiudi",
    L"Riduci a icona gruppo", L"Chiudi gruppo",
    L"Personalizza...",
    /* v3.5: menu contestuali delle icone di sistema ricreate. */
    L"Apri Mixer volume", L"Dispositivi di riproduzione",
    L"Dispositivi di registrazione", L"Suoni",
    L"Risoluzione dei problemi",
    L"Apri Centro connessioni di rete e condivisione",
    L"Centro PC portatile Windows", L"Opzioni risparmio energia",

};

constexpr ShortStrings kShortEn = {
    L"Best match", L"Programs", L"Recent files",
    L"No items match your search.", L"Scanning applications...",
    L"Open", L"Run as administrator", L"Open file location",
    L"App",
    L"&Restore", L"&Move", L"&Size", L"Mi&nimize", L"Ma&ximize", L"&Close",
    L"Minimize group", L"Close group",
    L"Customize...",
    /* v3.5: context menus of the recreated system icons. */
    L"Open Volume Mixer", L"Playback devices",
    L"Recording devices", L"Sounds",
    L"Troubleshoot problems",
    L"Open Network and Sharing Center",
    L"Windows Mobility Center", L"Power Options",

};

constexpr ShortStrings kShortEs = {
    L"Mejor coincidencia", L"Programas", L"Archivos recientes",
    L"Ningún elemento coincide con la búsqueda.", L"Buscando aplicaciones...",
    L"Abrir", L"Ejecutar como administrador", L"Abrir ubicación del archivo",
    L"Aplicación",
    L"&Restaurar", L"&Mover", L"&Tamaño", L"Mi&nimizar", L"Ma&ximizar", L"&Cerrar",
    L"Minimizar grupo", L"Cerrar grupo",
    L"Personalizar...",
    L"Abrir mezclador de volumen", L"Dispositivos de reproducción",
    L"Dispositivos de grabación", L"Sonidos",
    L"Solucionar problemas",
    L"Abrir Centro de redes y recursos compartidos",
    L"Centro de movilidad de Windows", L"Opciones de energía",

};

constexpr ShortStrings kShortFr = {
    L"Meilleure correspondance", L"Programmes", L"Fichiers récents",
    L"Aucun élément ne correspond à votre recherche.", L"Analyse des applications...",
    L"Ouvrir", L"Exécuter en tant qu’administrateur", L"Ouvrir l’emplacement du fichier",
    L"Application",
    L"&Restaurer", L"&Déplacer", L"&Taille", L"Réduire", L"Agrandir", L"&Fermer",
    L"Réduire le groupe", L"Fermer le groupe",
    L"Personnaliser...",
    L"Ouvrir le mixtureur de volume", L"Périphériques de lecture",
    L"Périphériques d'enregistrement", L"Sons",
    L"Résoudre les problèmes",
    L"Ouvrir le Centre Réseau et partage",
    L"Centre de mobilité Windows", L"Options d'alimentation",

};

constexpr ShortStrings kShortDe = {
    L"Beste Übereinstimmung", L"Programme", L"Zuletzt verwendete Dateien",
    L"Keine Elemente entsprechen Ihrer Suche.", L"Anwendungen werden durchsucht...",
    L"Öffnen", L"Als Administrator ausführen", L"Dateispeicherort öffnen",
    L"App",
    L"&Wiederherstellen", L"&Verschieben", L"&Größe", L"Mi&nimieren",
    L"Ma&ximieren", L"&Schließen",
    L"Gruppe minimieren", L"Gruppe schließen",
    L"Anpassen...",
    L"Lautstärkemixer öffnen", L"Wiedergabegeräte",
    L"Aufnahmegeräte", L"Sounds",
    L"Problembehandlung",
    L"Netzwerk- und Freigabecenter öffnen",
    L"Windows-Mobilitätscenter", L"Energieoptionen",

};

constexpr ShortStrings kShortPt = {
    L"Melhor correspondência", L"Programas", L"Ficheiros recentes",
    L"Nenhum item corresponde à sua pesquisa.", L"A pesquisar aplicações...",
    L"Abrir", L"Executar como administrador", L"Abrir localização do ficheiro",
    L"Aplicação",
    L"&Restaurar", L"&Mover", L"&Tamanho", L"Mi&nimizar", L"Ma&ximizar", L"&Fechar",
    L"Minimizar grupo", L"Fechar grupo",
    L"Personalizar...",
    L"Abrir Mixer de Volume", L"Dispositivos de reprodução",
    L"Dispositivos de gravação", L"Sons",
    L"Solucionar problemas",
    L"Abrir Central de Rede e Compartilhamento",
    L"Central de Mobilidade do Windows", L"Opções de Energia",

};

constexpr ShortStrings kShortPl = {
    L"Najlepsze dopasowanie", L"Programy", L"Ostatnio używane pliki",
    L"Żadne elementy nie pasują do wyszukiwania.", L"Skanowanie aplikacji...",
    L"Otwórz", L"Uruchom jako administrator", L"Otwórz lokalizację pliku",
    L"Aplikacja",
    L"P&rzywróć", L"&Przenieś", L"&Rozmiar", L"Z&minimalizuj",
    L"Ma&ksymalizuj", L"&Zamknij",
    L"Minimalizuj grupę", L"Zamknij grupę",
    L"Dostosuj...",
    L"Otwórz mikser głośności", L"Urządzenia odtwarzające",
    L"Urządzenia rejestrujące", L"Dźwięki",
    L"Rozwiąż problemy",
    L"Otwórz Centrum sieci i udostępniania",
    L"Centrum mobilności Windows", L"Opcje zasilania",

};

constexpr ShortStrings kShortRu = {
    L"Лучшее совпадение", L"Программы", L"Последние файлы",
    L"Нет элементов, соответствующих поиску.", L"Сканирование приложений...",
    L"Открыть", L"Запуск от имени администратора", L"Открыть расположение файла",
    L"Приложение",
    L"&Восстановить", L"&Переместить", L"&Размер", L"С&вернуть",
    L"Р&азвернуть", L"&Закрыть",
    L"Свернуть группу", L"Закрыть группу",
    L"Настройка...",
    L"Открыть микшер громкости", L"Устройства воспроизведения",
    L"Устройства записи", L"Звуки",
    L"Устранение неполадок",
    L"Открыть Центр управления сетями и общим доступом",
    L"Центр мобильности Windows", L"Электропитание",

};

constexpr ShortStrings kShortJa = {
    L"最も一致する項目", L"プログラム", L"最近使ったファイル",
    L"検索に一致する項目はありません。", L"アプリケーションをスキャンしています...",
    L"開く", L"管理者として実行", L"ファイルの場所を開く",
    L"アプリ",
    L"元のサイズに戻す(&R)", L"移動(&M)", L"サイズ変更(&S)", L"最小化(&N)",
    L"最大化(&X)", L"閉じる(&C)",
    L"グループを最小化", L"グループを閉じる",
    L"カスタマイズ...",
    L"音量ミキサーを開く", L"再生デバイス",
    L"録音デバイス", L"サウンド",
    L"問題のトラブルシューティング",
    L"ネットワークと共有センターを開く",
    L"Windows モビリティ センター", L"電源オプション",

};

constexpr ShortStrings kShortZh = {
    L"最佳匹配", L"程序", L"最近的文件",
    L"没有与搜索匹配的项目。", L"正在扫描应用程序...",
    L"打开", L"以管理员身份运行", L"打开文件位置",
    L"应用",
    L"还原(&R)", L"移动(&M)", L"大小(&S)", L"最小化(&N)", L"最大化(&X)",
    L"关闭(&C)",
    L"最小化组", L"关闭组",
    L"自定义...",
    L"打开音量合成器", L"播放设备",
    L"录制设备", L"声音",
    L"疑难解答",
    L"打开网络和共享中心",
    L"Windows 移动中心", L"电源选项",

};

/* Arabic - contributed by mahmogamer (see CREDITS.txt). */
constexpr ShortStrings kShortAr = {
    L"أفضل تطابق", L"البرامج", L"الملفات الأخيرة",
    L"لا توجد عناصر تطابق بحثك.", L"جارٍ فحص التطبيقات...",
    L"فتح", L"تشغيل كمسؤول", L"فتح موقع الملف",
    L"تطبيق",
    L"استعادة(&R)", L"نقل(&M)", L"الحجم(&S)", L"تصغير(&N)", L"تكبير(&X)",
    L"إغلاق(&C)",
    L"تصغير المجموعة", L"إغلاق المجموعة",
    L"تخصيص...",
    L"فتح أداة مزج الصوت", L"أجهزة التشغيل",
    L"أجهزة التسجيل", L"الأصوات",
    L"استكشاف الأخطاء وإصلاحها",
    L"فتح مركز الشبكة والمشاركة",
    L"مركز تنقل Windows", L"خيارات الطاقة",

};

/* ------------------------------------------------------------------------ */
/*  Extra settings section (its own tab of the Properties window)            */
/*                                                                           */
/*  Same rule as the rest of the file: languages outside the list answer in  */
/*  English, and product names ("Windows 7", "Windows 8.1") are not          */
/*  translated.                                                              */
/* ------------------------------------------------------------------------ */
constexpr ExtraStrings kExtraIt = {
    L"Impostazioni extra",
    L"Flyout",
    L"Colore del flyout di connessione:",
    L"Colore di sistema",
    L"Colore personalizzato",
    L"Scegli colore...",
    L"Solo il flyout di connessione di Windows 8 (ricreato) quando è attivo: Colore di sistema segue il colore "
    L"di Windows, Colore personalizzato usa quello scelto. Windows non viene modificato.",
    L"Modalità privacy:",
    L"Normale (nomi reali)",
    L"Privacy (nomi generici)",
    L"Nasconde i nomi delle reti nel flyout di connessione ricreato. Non "
    L"modifica le connessioni di rete né Windows.",
    L"Barra delle applicazioni",
    L"Tema:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Ordine delle icone:",
    L"Trascina le icone sulla barra delle applicazioni per cambiarne l'ordine. "
    L"L'ordine è salvato solo in Win7Taskbar: la barra delle applicazioni di "
    L"Windows non viene modificata.",
    L"Posizione:",
    L"In basso",
    L"In alto",
    L"A sinistra",
    L"A destra",
    L"Blocca la barra delle applicazioni"

};
constexpr ExtraStrings kExtraEn = {
    L"Extra settings",
    L"Flyout",
    L"Connection flyout color:",
    L"System color",
    L"Custom color",
    L"Choose color...",
    L"Only the recreated Windows 8 connection flyout uses it when it is active: System color follows the "
    L"Windows color, Custom color uses the one you pick. Windows is not modified.",
    L"Privacy mode:",
    L"Normal (real names)",
    L"Privacy (generic names)",
    L"Hides network names in the recreated connection flyout. It does not "
    L"change network connections or Windows.",
    L"Taskbar",
    L"Theme:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Icon order:",
    L"Drag the icons on the taskbar to change their order. The order is saved "
    L"only in Win7Taskbar: the Windows taskbar is not modified.",
    L"Position:",
    L"Bottom",
    L"Top",
    L"Left",
    L"Right",
    L"Lock the taskbar"

};
constexpr ExtraStrings kExtraEs = {
    L"Configuración extra",
    L"Flyout",
    L"Color del flyout de conexión:",
    L"Color del sistema",
    L"Color personalizado",
    L"Elegir color...",
    L"Solo lo usa el flyout de conexión de Windows 8 (recreado) cuando está activo: Color del sistema sigue "
    L"el color de Windows y Color personalizado usa el elegido. Windows no se modifica.",
    L"Modo de privacidad:",
    L"Normal (nombres reales)",
    L"Privacidad (nombres genéricos)",
    L"Oculta los nombres de las redes en el flyout de conexión recreado. No "
    L"modifica las conexiones de red ni Windows.",
    L"Barra de tareas",
    L"Tema:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Orden de los iconos:",
    L"Arrastra los iconos en la barra de tareas para cambiar su orden. El "
    L"orden se guarda solo en Win7Taskbar: la barra de tareas de Windows no se "
    L"modifica.",
    L"Posición:",
    L"Abajo",
    L"Arriba",
    L"Izquierda",
    L"Derecha",
    L"Bloquear la barra de tareas"

};
constexpr ExtraStrings kExtraFr = {
    L"Paramètres supplémentaires",
    L"Flyout",
    L"Couleur du flyout de connexion :",
    L"Couleur du système",
    L"Couleur personnalisée",
    L"Choisir une couleur...",
    L"Seul le flyout de connexion Windows 8 (recréé) l'utilise lorsqu'il est actif : Couleur du système "
    L"suit la couleur de Windows, Couleur personnalisée utilise celle choisie. Windows n'est pas modifié.",
    L"Mode confidentialité :",
    L"Normal (noms réels)",
    L"Confidentialité (noms génériques)",
    L"Masque les noms des réseaux dans le flyout de connexion recréé. Ne "
    L"modifie ni les connexions réseau ni Windows.",
    L"Barre des tâches",
    L"Thème :",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Ordre des icônes :",
    L"Faites glisser les icônes sur la barre des tâches pour changer leur "
    L"ordre. L'ordre est enregistré uniquement dans Win7Taskbar : la barre des "
    L"tâches de Windows n'est pas modifiée.",
    L"Position :",
    L"En bas",
    L"En haut",
    L"À gauche",
    L"À droite",
    L"Verrouiller la barre des tâches"

};
constexpr ExtraStrings kExtraDe = {
    L"Weitere Einstellungen",
    L"Flyout",
    L"Farbe des Verbindungs-Flyouts:",
    L"Systemfarbe",
    L"Benutzerdefinierte Farbe",
    L"Farbe wählen...",
    L"Nur das nachgebildete Windows 8-Verbindungs-Flyout nutzt sie, wenn es aktiv ist: Systemfarbe folgt der "
    L"Windows-Farbe, Benutzerdefinierte Farbe nutzt die gewählte. Windows wird nicht verändert.",
    L"Datenschutzmodus:",
    L"Normal (echte Namen)",
    L"Datenschutz (generische Namen)",
    L"Blendet die Netzwerknamen im nachgebildeten Verbindungs-Flyout aus. "
    L"Ändert weder Netzwerkverbindungen noch Windows.",
    L"Taskleiste",
    L"Design:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Symbolreihenfolge:",
    L"Ziehen Sie die Symbole auf der Taskleiste, um ihre Reihenfolge zu "
    L"ändern. Die Reihenfolge wird nur in Win7Taskbar gespeichert: die "
    L"Windows-Taskleiste wird nicht geändert.",
    L"Position:",
    L"Unten",
    L"Oben",
    L"Links",
    L"Rechts",
    L"Taskleiste sperren"

};
constexpr ExtraStrings kExtraPt = {
    L"Configurações extras",
    L"Flyout",
    L"Cor do flyout de conexão:",
    L"Cor do sistema",
    L"Cor personalizada",
    L"Escolher cor...",
    L"Só o flyout de conexão do Windows 8 (recriado) a usa quando está ativo: Cor do sistema segue a cor "
    L"do Windows, Cor personalizada usa a escolhida. O Windows não é alterado.",
    L"Modo de privacidade:",
    L"Normal (nomes reais)",
    L"Privacidade (nomes genéricos)",
    L"Oculta os nomes das redes no flyout de conexão recriado. Não altera as "
    L"conexões de rede nem o Windows.",
    L"Barra de tarefas",
    L"Tema:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Ordem dos ícones:",
    L"Arraste os ícones na barra de tarefas para alterar a ordem. A ordem é "
    L"salva apenas no Win7Taskbar: a barra de tarefas do Windows não é "
    L"modificada.",
    L"Posição:",
    L"Em baixo",
    L"Em cima",
    L"À esquerda",
    L"À direita",
    L"Bloquear a barra de tarefas"

};
constexpr ExtraStrings kExtraPl = {
    L"Ustawienia dodatkowe",
    L"Flyout",
    L"Kolor okna wysuwanego połączeń:",
    L"Kolor systemowy",
    L"Kolor niestandardowy",
    L"Wybierz kolor...",
    L"Używa go tylko odtworzone okno wysuwane połączeń Windows 8, gdy jest aktywne: Kolor systemowy "
    L"podąża za kolorem Windows, Kolor niestandardowy używa wybranego. Windows nie jest zmieniany.",
    L"Tryb prywatności:",
    L"Normalny (prawdziwe nazwy)",
    L"Prywatność (nazwy ogólne)",
    L"Ukrywa nazwy sieci w odtworzonym oknie połączeń. Nie zmienia połączeń "
    L"sieciowych ani Windows.",
    L"Pasek zadań",
    L"Motyw:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Kolejność ikon:",
    L"Przeciągnij ikony na pasku zadań, aby zmienić ich kolejność. Kolejność "
    L"jest zapisywana tylko w Win7Taskbar: pasek zadań Windows nie jest "
    L"zmieniany.",
    L"Pozycja:",
    L"Na dole",
    L"Na górze",
    L"Na lewo",
    L"Na prawo",
    L"Zablokuj pasek zadań"

};
constexpr ExtraStrings kExtraRu = {
    L"Дополнительные параметры",
    L"Flyout",
    L"Цвет панели подключений:",
    L"Системный цвет",
    L"Пользовательский цвет",
    L"Выбрать цвет...",
    L"Используется только панелью подключений Windows 8 (воссозданной), когда она активна: системный "
    L"цвет следует за цветом Windows, пользовательский — за выбранным. Windows не изменяется.",
    L"Режим конфиденциальности:",
    L"Обычный (реальные имена)",
    L"Конфиденциальность (общие имена)",
    L"Скрывает имена сетей в воссозданной панели подключений. Не изменяет "
    L"сетевые подключения и Windows.",
    L"Панель задач",
    L"Тема:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"Порядок значков:",
    L"Перетаскивайте значки на панели задач, чтобы изменить их порядок. "
    L"Порядок сохраняется только в Win7Taskbar: панель задач Windows не "
    L"изменяется.",
    L"Расположение:",
    L"Внизу",
    L"Вверху",
    L"Слева",
    L"Справа",
    L"Заблокировать панель задач"

};
constexpr ExtraStrings kExtraJa = {
    L"詳細設定",
    L"フライアウト",
    L"接続フライアウトの色:",
    L"システムの色",
    L"カスタム色",
    L"色を選択...",
    L"再現版 Windows 8 の接続フライアウトだけが有効時に使用します。システムの色は Windows の色に従い、"
    L"カスタム色は選択した色を使います。Windows は変更されません。",
    L"プライバシーモード:",
    L"通常（実際の名前）",
    L"プライバシー（一般的な名前）",
    L"再現された接続フライアウトでネットワーク名を隠します。ネットワーク接続や "
    L"Windows は変更しません。",
    L"タスクバー",
    L"テーマ:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"アイコンの順序:",
    L"タスクバー上のアイコンをドラッグして順序を変更します。順序は "
    L"Win7Taskbar にのみ保存され、Windows のタスクバーは変更されません。",
    L"位置:",
    L"下",
    L"上",
    L"左",
    L"右",
    L"タスク バーをロックする"

};
constexpr ExtraStrings kExtraZh = {
    L"额外设置",
    L"浮出控件",
    L"连接浮出控件颜色:",
    L"系统颜色",
    L"自定义颜色",
    L"选择颜色...",
    L"启用时仅由重建的 Windows 8 连接浮出控件使用：系统颜色跟随 Windows 的颜色，"
    L"自定义颜色使用所选颜色。不会修改 Windows。",
    L"隐私模式:",
    L"普通（真实名称）",
    L"隐私（通用名称）",
    L"在重建的连接浮出控件中隐藏网络名称。不会更改网络连接或 Windows。",
    L"任务栏",
    L"主题:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"图标顺序:",
    L"在任务栏上拖动图标以更改其顺序。顺序仅保存在 Win7Taskbar 中：不会修改 "
    L"Windows 任务栏。",
    L"位置:",
    L"下",
    L"上",
    L"左",
    L"右",
    L"锁定任务栏"

};
constexpr ExtraStrings kExtraAr = {
    L"إعدادات إضافية",
    L"القائمة المنبثقة",
    L"لون قائمة الاتصال المنبثقة:",
    L"لون النظام",
    L"لون مخصص",
    L"اختيار لون...",
    L"تستخدمها قائمة الاتصال في Windows 8 (المُعاد إنشاؤها) وحدها عند تفعيلها: لون النظام يتبع لون "
    L"Windows، واللون المخصص يستخدم اللون المختار. لا يتغيّر Windows.",
    L"وضع الخصوصية:",
    L"عادي (أسماء حقيقية)",
    L"الخصوصية (أسماء عامة)",
    L"يخفي أسماء الشبكات في قائمة الاتصال المعاد إنشاؤها. لا يغيّر اتصالات "
    L"الشبكة ولا Windows.",
    L"شريط المهام",
    L"السمة:",
    L"Windows 7",
    L"Windows 8.1",
    L"Windows 7 Aero Basic",
    L"Windows 8 Beta 8148",
    L"ترتيب الأيقونات:",
    L"اسحب الأيقونات على شريط المهام لتغيير ترتيبها. يُحفظ الترتيب في "
    L"Win7Taskbar فقط: لا يتم تعديل شريط مهام Windows.",
    L"الموضع:",
    L"أسفل",
    L"أعلى",
    L"يسار",
    L"يمين",
    L"قفل شريط المهام"

};

const PropStrings& PickProp(Lang lang) {
    switch (lang) {
        case Lang::It: return kPropIt;
        case Lang::Es: return kPropEs;
        case Lang::Fr: return kPropFr;
        case Lang::De: return kPropDe;
        case Lang::Pt: return kPropPt;
        case Lang::Pl: return kPropPl;
        case Lang::Ru: return kPropRu;
        case Lang::Ja: return kPropJa;
        case Lang::Zh: return kPropZh;
        case Lang::Ar: return kPropAr;
        default:       return kPropEn;
    }
}

const BattStrings& PickBatt(Lang lang) {
    switch (lang) {
        case Lang::It: return kBattIt;
        case Lang::Es: return kBattEs;
        case Lang::Fr: return kBattFr;
        case Lang::De: return kBattDe;
        case Lang::Pt: return kBattPt;
        case Lang::Pl: return kBattPl;
        case Lang::Ru: return kBattRu;
        case Lang::Ja: return kBattJa;
        case Lang::Zh: return kBattZh;
        case Lang::Ar: return kBattAr;
        default:       return kBattEn;
    }
}

const ShortStrings& PickShort(Lang lang) {
    switch (lang) {
        case Lang::It: return kShortIt;
        case Lang::Es: return kShortEs;
        case Lang::Fr: return kShortFr;
        case Lang::De: return kShortDe;
        case Lang::Pt: return kShortPt;
        case Lang::Pl: return kShortPl;
        case Lang::Ru: return kShortRu;
        case Lang::Ja: return kShortJa;
        case Lang::Zh: return kShortZh;
        case Lang::Ar: return kShortAr;
        default:       return kShortEn;
    }
}

const wchar_t* PickShortId(const ShortStrings& s, StrId id) {
    switch (id) {
        case StrId::BestMatch:            return s.bestMatch;
        case StrId::Programs:             return s.programs;
        case StrId::RecentFiles:          return s.recentFiles;
        case StrId::NoSearchResults:      return s.noResults;
        case StrId::ScanningApplications: return s.scanning;
        case StrId::Open:                 return s.open;
        case StrId::RunAsAdministrator:   return s.runAsAdmin;
        case StrId::OpenFileLocation:     return s.openLocation;
        case StrId::AppItem:              return s.appItem;
        case StrId::SysRestore:           return s.sysRestore;
        case StrId::SysMove:              return s.sysMove;
        case StrId::SysSize:              return s.sysSize;
        case StrId::SysMinimize:          return s.sysMinimize;
        case StrId::SysMaximize:          return s.sysMaximize;
        case StrId::SysClose:             return s.sysClose;
        case StrId::GroupMinimize:        return s.groupMinimize;
        case StrId::GroupClose:           return s.groupClose;
        case StrId::OverflowCustomize:    return s.overflowCustomize;
        /* v3.5: menu contestuali delle icone di sistema ricreate. */
        case StrId::CtxVolMixer:          return s.ctxVolMixer;
        case StrId::CtxPlayback:          return s.ctxPlayback;
        case StrId::CtxRecording:         return s.ctxRecording;
        case StrId::CtxSounds:            return s.ctxSounds;
        case StrId::CtxTroubleshoot:      return s.ctxTroubleshoot;
        case StrId::CtxNetCenter:         return s.ctxNetCenter;
        case StrId::CtxMobility:          return s.ctxMobility;
        case StrId::CtxPower:             return s.ctxPower;

    }
    return L"";
}

std::string Narrow(const wchar_t* wide) {
    if (wide == nullptr) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) {
        return std::string();
    }
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], size, nullptr, nullptr);
    return out;
}

/* Lingua corrente del core. Il default e' l'inglese: finche' il managed non
 * dice nulla (e se non dicesse mai nulla) il nativo non mostra italiano su
 * un sistema che non lo e'. */
std::atomic<Lang> g_lang{Lang::En};

} /* namespace */

const LanguageEntry* Languages() {
    return kLanguages;
}

const char* LangCode(Lang lang) {
    const int index = LangIndex(lang);
    return kLanguages[index].code;
}

const wchar_t* LangName(Lang lang) {
    const int index = LangIndex(lang);
    return kLanguages[index].nativeName;
}

Lang LangFromCode(const char* twoLetterCode) {
    if (twoLetterCode == nullptr) {
        return Lang::En;
    }
    for (int i = 0; i < kLangCount; ++i) {
        if (_stricmp(twoLetterCode, kLanguages[i].code) == 0) {
            return static_cast<Lang>(i);
        }
    }
    return Lang::En;
}

Lang LangFromIndex(int index) {
    return (index >= 0 && index < kLangCount) ? static_cast<Lang>(index) : Lang::En;
}

int LangIndex(Lang lang) {
    const int index = static_cast<int>(lang);
    return (index >= 0 && index < kLangCount) ? index : static_cast<int>(Lang::En);
}

Lang DetectSystemLanguage() {
    /* GetUserDefaultUILanguage: la lingua dell'interfaccia di Windows, che
     * non e' per forza quella del formato di data/numero. Il codice a due
     * lettere viene poi filtrato dall'elenco: lingua non supportata ->
     * inglese, mai italiano. */
    const LANGID id = GetUserDefaultUILanguage();
    wchar_t code[16] = {};
    if (GetLocaleInfoW(MAKELCID(id, SORT_DEFAULT), LOCALE_SISO639LANGNAME,
                       code, 16) > 0) {
        return LangFromCode(Narrow(code).c_str());
    }
    switch (PRIMARYLANGID(id)) {
        case LANG_ITALIAN:    return Lang::It;
        case LANG_SPANISH:    return Lang::Es;
        case LANG_FRENCH:     return Lang::Fr;
        case LANG_GERMAN:     return Lang::De;
        case LANG_PORTUGUESE: return Lang::Pt;
        case LANG_POLISH:     return Lang::Pl;
        case LANG_RUSSIAN:    return Lang::Ru;
        case LANG_JAPANESE:   return Lang::Ja;
        case LANG_CHINESE:    return Lang::Zh;
        case LANG_ARABIC:     return Lang::Ar;
        default:              return Lang::En;
    }
}

void SetLanguage(const char* twoLetterCode) {
    g_lang.store(LangFromCode(twoLetterCode), std::memory_order_release);
}

void SetLanguageByIndex(int index) {
    g_lang.store(LangFromIndex(index), std::memory_order_release);
}

Lang CurrentLanguage() {
    return g_lang.load(std::memory_order_acquire);
}

const wchar_t* S(StrId id) {
    return PickShortId(PickShort(CurrentLanguage()), id);
}

const wchar_t* GetString(StrId id) {
    return S(id);
}

const PropStrings& PropStringsFor(Lang lang) {
    return PickProp(lang);
}

const ExtraStrings& ExtraStringsFor(Lang lang) {
    switch (lang) {
        case Lang::It: return kExtraIt;
        case Lang::Es: return kExtraEs;
        case Lang::Fr: return kExtraFr;
        case Lang::De: return kExtraDe;
        case Lang::Pt: return kExtraPt;
        case Lang::Pl: return kExtraPl;
        case Lang::Ru: return kExtraRu;
        case Lang::Ja: return kExtraJa;
        case Lang::Zh: return kExtraZh;
        case Lang::Ar: return kExtraAr;
        default:       return kExtraEn;
    }
}

const BattStrings& BattStringsFor(Lang lang) {
    return PickBatt(lang);
}

} // namespace w7t
