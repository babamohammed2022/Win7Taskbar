#include "Strings.h"

#include <cstring>
#include <mutex>

namespace w7t {
namespace {

struct Strings {
    const wchar_t* bestMatch;
    const wchar_t* programs;
    const wchar_t* recentFiles;
    const wchar_t* noResults;
    const wchar_t* scanning;
    const wchar_t* open;
    const wchar_t* runAsAdmin;
    const wchar_t* openLocation;
};

constexpr Strings kIt{
    L"Corrispondenza migliore", L"Programmi", L"File recenti",
    L"Nessun elemento corrisponde alla ricerca.", L"Scansione applicazioni...",
    L"Apri", L"Esegui come amministratore", L"Apri percorso file"};
constexpr Strings kEn{
    L"Best match", L"Programs", L"Recent files",
    L"No items match your search.", L"Scanning applications...",
    L"Open", L"Run as administrator", L"Open file location"};
constexpr Strings kEs{
    L"Mejor coincidencia", L"Programas", L"Archivos recientes",
    L"Ningún elemento coincide con la búsqueda.", L"Buscando aplicaciones...",
    L"Abrir", L"Ejecutar como administrador", L"Abrir ubicación del archivo"};
constexpr Strings kFr{
    L"Meilleure correspondance", L"Programmes", L"Fichiers récents",
    L"Aucun élément ne correspond à votre recherche.", L"Analyse des applications...",
    L"Ouvrir", L"Exécuter en tant qu’administrateur", L"Ouvrir l’emplacement du fichier"};
constexpr Strings kDe{
    L"Beste Übereinstimmung", L"Programme", L"Zuletzt verwendete Dateien",
    L"Keine Elemente entsprechen Ihrer Suche.", L"Anwendungen werden durchsucht...",
    L"Öffnen", L"Als Administrator ausführen", L"Dateispeicherort öffnen"};
constexpr Strings kPt{
    L"Melhor correspondência", L"Programas", L"Ficheiros recentes",
    L"Nenhum item corresponde à sua pesquisa.", L"A pesquisar aplicações...",
    L"Abrir", L"Executar como administrador", L"Abrir localização do ficheiro"};
constexpr Strings kPl{
    L"Najlepsze dopasowanie", L"Programy", L"Ostatnio używane pliki",
    L"Żadne elementy nie pasują do wyszukiwania.", L"Skanowanie aplikacji...",
    L"Otwórz", L"Uruchom jako administrator", L"Otwórz lokalizację pliku"};
constexpr Strings kRu{
    L"Лучшее совпадение", L"Программы", L"Последние файлы",
    L"Нет элементов, соответствующих поиску.", L"Сканирование приложений...",
    L"Открыть", L"Запуск от имени администратора", L"Открыть расположение файла"};
constexpr Strings kJa{
    L"最も一致する項目", L"プログラム", L"最近使ったファイル",
    L"検索に一致する項目はありません。", L"アプリケーションをスキャンしています...",
    L"開く", L"管理者として実行", L"ファイルの場所を開く"};
constexpr Strings kZh{
    L"最佳匹配", L"程序", L"最近的文件",
    L"没有与搜索匹配的项目。", L"正在扫描应用程序...",
    L"打开", L"以管理员身份运行", L"打开文件位置"};
constexpr Strings kAr{
    L"أفضل تطابق", L"البرامج", L"الملفات الأخيرة",
    L"لا توجد عناصر تطابق بحثك.", L"جارٍ فحص التطبيقات...",
    L"فتح", L"تشغيل كمسؤول", L"فتح موقع الملف"};

const Strings* Current() {
    static const Strings* value = &kEn;
    return value;
}

const Strings* Pick(const char* code) {
    if (!code) return &kEn;
    if (std::strcmp(code, "it") == 0) return &kIt;
    if (std::strcmp(code, "en") == 0) return &kEn;
    if (std::strcmp(code, "es") == 0) return &kEs;
    if (std::strcmp(code, "fr") == 0) return &kFr;
    if (std::strcmp(code, "de") == 0) return &kDe;
    if (std::strcmp(code, "pt") == 0) return &kPt;
    if (std::strcmp(code, "pl") == 0) return &kPl;
    if (std::strcmp(code, "ru") == 0) return &kRu;
    if (std::strcmp(code, "ja") == 0) return &kJa;
    if (std::strcmp(code, "zh") == 0) return &kZh;
    if (std::strcmp(code, "ar") == 0) return &kAr;
    return &kEn;
}

const wchar_t* Select(const Strings& s, StrId id) {
    switch (id) {
        case StrId::BestMatch: return s.bestMatch;
        case StrId::Programs: return s.programs;
        case StrId::RecentFiles: return s.recentFiles;
        case StrId::NoSearchResults: return s.noResults;
        case StrId::ScanningApplications: return s.scanning;
        case StrId::Open: return s.open;
        case StrId::RunAsAdministrator: return s.runAsAdmin;
        case StrId::OpenFileLocation: return s.openLocation;
    }
    return L"";
}

} // namespace

void SetLanguage(const char* code) {
    // The pointer targets immutable static tables; replacing it is atomic on
    // supported Windows targets and keeps reads allocation-free.
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const Strings* selected = Pick(code);
    // Reuse the function-local storage through a stable pointer.
    struct Holder { const Strings* value = &kEn; };
    static Holder holder;
    holder.value = selected;
    (void)mutex;
}

const wchar_t* GetString(StrId id) {
    static const Strings* current = &kEn;
    return Select(*current, id);
}

} // namespace w7t
