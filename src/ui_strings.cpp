#include "ui_strings.h"
#include <cstring>

namespace {
struct entry_t { const char *ru; const char *en; const char *sr; };

// Keep the Russian key as the stable source string so existing call sites are
// easy to compare with the former Russian-only UI.
static const entry_t STRINGS[] = {
    {"День", "Day", "Dan"}, {"Сводка", "Summary", "Pregled"}, {"Дом", "Home", "Dom"},
    {"План на день", "Day plan", "Plan za dan"}, {"НЕДЕЛЯ", "WEEK", "NEDELJA"},
    {"РАСПИСАНИЕ", "SCHEDULE", "RASPORED"}, {"Расписание", "Schedule", "Raspored"},
    {"Загружаю расписание", "Loading schedule", "Učitavanje rasporeda"},
    {"Подключение…", "Connecting…", "Povezivanje…"}, {"Сегодня", "Today", "Danas"},
    {"Следующее", "Next", "Sledeće"}, {"Нет событий", "No events", "Nema događaja"},
    {"Нет данных календаря", "No calendar data", "Nema podataka kalendara"},
    {"Свободный день", "Free day", "Slobodan dan"},
    {"Выбранный день", "Selected day", "Izabrani dan"},
    {"Выбранная дата", "Selected date", "Izabrani datum"},
    {"событие", "event", "događaj"}, {"события", "events", "događaja"},
    {"событий", "events", "događaja"}, {"Назад", "Back", "Nazad"},
    {"Суммаризация недоступна.", "Summary unavailable.", "Sažetak nije dostupan."},
    {"Новости", "News", "Vesti"}, {"Задачи", "Tasks", "Zadaci"},
    {"Срок:", "Due:", "Rok:"}, {"Погода", "Weather", "Vreme"},
    {"Срок: %s", "Due: %s", "Rok: %s"},
    {"Нет данных о погоде", "No weather data", "Nema podataka o vremenu"},
    {"ТРАНСПОРТ", "TRANSPORT", "PREVOZ"}, {"ВОЗДУХ ДОМА", "HOME AIR", "VAZDUH U KUĆI"},
    {"Гостиная: нет данных CO2", "Living room: no CO2 data", "Dnevna soba: nema CO2 podataka"},
    {"Кабинет: нет данных CO2", "Study: no CO2 data", "Radna soba: nema CO2 podataka"},
    {"WiFi: ---", "Wi-Fi: ---", "Wi-Fi: ---"}, {"Календарь", "Calendar", "Kalendar"},
    {"Нет данных", "No data", "Nema podataka"}, {"Весь день", "All day", "Ceo dan"},
    {"Сейчас", "Now", "Sada"}, {"На сегодня больше событий нет", "No more events today", "Nema više događaja za danas"},
    {"На эту дату больше событий нет", "No more events on this date", "Nema više događaja za ovaj datum"},
    {"События на весь день ниже", "All-day events below", "Celodnevni događaji su ispod"},
    {"Нет событий в расписании", "No events scheduled", "Nema zakazanih događaja"},
    {"На эту дату нет событий", "No events on this date", "Nema događaja za ovaj datum"},
    {"день почти свободен", "day is nearly free", "dan je skoro slobodan"},
    {"Больше событий нет", "No more events", "Nema više događaja"},
    {"нет данных", "no data", "nema podataka"}, {"Из центра  ", "From downtown  ", "Iz centra  "},
    {"В центр    ", "To downtown    ", "Ka centru    "},
    {"Здоровье", "Health", "Zdravlje"}, {"Готовность", "Readiness", "Spremnost"},
    {"Сон", "Sleep", "San"}, {"Шаги", "Steps", "Koraci"}, {"Пульс", "Heart rate", "Puls"},
    {"ЧСС покоя", "Resting HR", "Puls u mirovanju"}, {"ВСР", "HRV", "HRV"},
    {"Отлично", "Excellent", "Odlično"}, {"Норма", "Normal", "Normalno"},
    {"Низко", "Low", "Nisko"}, {"Нет задач", "No tasks", "Nema zadataka"},
    {"Нет новостей", "No news", "Nema vesti"},
    {"Дом · удерживайте для локальной настройки", "Home · hold for local setup", "Dom · držite za lokalno podešavanje"},
    {"Авто: через 5 мин без касаний", "Auto: after 5 min without touch", "Automatski: posle 5 min bez dodira"},
    {"Авто: после простоя без касаний", "Auto: after inactivity", "Automatski: posle neaktivnosti"},
    {"Настройки HomeDash: откройте Bridge", "HomeDash settings: open Bridge", "HomeDash podešavanja: otvorite Bridge"},
    {"Настройки HomeDash: %s", "HomeDash settings: %s", "HomeDash podešavanja: %s"},
    {"Настройки HomeDash: подключитесь к Bridge", "HomeDash settings: connect to Bridge", "HomeDash podešavanja: povežite se na Bridge"},
    {"Фоторамка", "Photo frame", "Foto-ram"}, {"Обновить", "Update", "Ažuriraj"},
    {"Воздух дома", "Home air", "Vazduh u kući"}, {"Гостиная CO2", "Living room CO2", "CO2 u dnevnoj sobi"},
    {"Кабинет CO2", "Study CO2", "CO2 u radnoj sobi"}, {"Влажность", "Humidity", "Vlažnost"},
    {"Кондиционер", "Air conditioner", "Klima-uređaj"}, {"Сейчас ---", "Current ---", "Trenutno ---"},
    {"Цель ---", "Target ---", "Cilj ---"}, {"Вкл / выкл", "On / off", "Uključi/isklj."},
    {"Охл.", "Cool", "Hlađ."}, {"Тпл.", "Heat", "Topl."},
    {"Суш.", "Dry", "Suš."}, {"Вен.", "Fan", "Vent."},
    {"Датчик", "Sensor", "Senzor"}, {"Влажн.", "Humid.", "Vlaž."},
    {"Темп.", "Temp.", "Temp."}, {"Темп.(очист.)", "Temp. (purifier)", "Temp. (prečišćivač)"},
    {"Влажн.(очист.)", "Humid. (purifier)", "Vlaž. (prečišćivač)"},
    {"Сейчас нет данных", "No current data", "Nema trenutnih podataka"},
    {"Цель нет данных", "No target data", "Nema podataka o cilju"},
    {"Нет связи с источником фото", "No connection to photo source", "Nema veze sa izvorom fotografija"},
    {"Фоторамка ещё не готова", "Photo frame is not ready yet", "Foto-ram još nije spreman"},
    {"Сначала дождитесь обновления", "Wait for the update to finish first", "Sačekajte da se ažuriranje završi"},
    {"Фоторамка выключена в настройках", "Photo frame is disabled in settings", "Foto-ram je isključen u podešavanjima"},
    {"Запускаем фоторамку...", "Starting photo frame...", "Pokretanje foto-rama..."},
    {"Получаем список фото...", "Loading photo list...", "Učitavanje spiska fotografija..."},
    {"Получаем изображения...", "Loading images...", "Učitavanje slika..."},
    {"Фото открыто. Касание: выход", "Photo open. Touch to exit", "Fotografija je otvorena. Dodirnite za izlaz"},
    {"Недостаточно памяти для фото", "Not enough memory for photo", "Nema dovoljno memorije za fotografiju"},
    {"Фоторамка выключена на мосту", "Photo frame disabled on Bridge", "Foto-ram je isključen na Bridge-u"},
    {"В подборке нет фотографий", "No photos in this album", "Nema fotografija u ovom albumu"},
    {"Мост готовит фотографии...", "Bridge is preparing photos...", "Bridge priprema fotografije..."},
    {"Фотографии пока недоступны", "Photos are not available yet", "Fotografije još nisu dostupne"},
    {"Список фото:", "Photo list:", "Spisak fotografija:"}, {"Фото:", "Photo:", "Fotografija:"},
    {"Сейчас %.1f C", "Current %.1f C", "Trenutno %.1f C"},
    {"Цель %.0f C", "Target %.0f C", "Cilj %.0f C"},
    {"Весь день  %s", "All day  %s", "Ceo dan  %s"},
    {"%02d:%02d-%02d:%02d  до %02d:%02d", "%02d:%02d-%02d:%02d  until %02d:%02d", "%02d:%02d-%02d:%02d  do %02d:%02d"},
    {"%02d:%02d-%02d:%02d  через %d мин", "%02d:%02d-%02d:%02d  in %d min", "%02d:%02d-%02d:%02d  za %d min"},
    {"%02d:%02d  через %dч %02dм", "%02d:%02d  in %dh %02dm", "%02d:%02d  za %dč %02dmin"},
    {"%02d:%02d-%02d:%02d  день почти свободен", "%02d:%02d-%02d:%02d  day is nearly free", "%02d:%02d-%02d:%02d  dan je skoro slobodan"},
    {"%dм", "%d min", "%d min"}, {"%d bpm", "%d bpm", "%d otk/min"}, {"Датчик %d", "Sensor %d", "Senzor %d"},
    {"Подключение к Bridge", "Connecting to Bridge", "Povezivanje sa Bridge-om"},
    {"Проверка обновлений…", "Checking for updates…", "Provera ažuriranja…"},
    {"Обновление недоступно", "Update unavailable", "Ažuriranje nije dostupno"},
    {"Обновление доступно", "Update available", "Dostupno je ažuriranje"},
    {"Установка обновления…", "Installing update…", "Instaliranje ažuriranja…"},
    {"Обновление установлено", "Update installed", "Ažuriranje je instalirano"},
    {"Ошибка обновления", "Update error", "Greška pri ažuriranju"},
    {"Локальная сеть %s · пароль %s · %s", "Local network %s · password %s · %s", "Lokalna mreža %s · lozinka %s · %s"},
    {"Локальная сеть", "Local network", "Lokalna mreža"},
    {"пароль", "password", "lozinka"},
    {"Гостиная", "Living room", "Dnevna soba"}, {"Кабинет", "Study", "Radna soba"},
    {"Спальня", "Bedroom", "Spavaća soba"}, {"Кухня", "Kitchen", "Kuhinja"},
};

static const char *DAYS[3][7] = {
    {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"},
    {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"},
    {"Po", "Ut", "Sr", "Če", "Pe", "Su", "Ne"},
};
static const char *MONTHS[3][12] = {
    {"Янв", "Фев", "Мар", "Апр", "Май", "Июн", "Июл", "Авг", "Сен", "Окт", "Ноя", "Дек"},
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
    {"Jan", "Feb", "Mar", "Apr", "Maj", "Jun", "Jul", "Avg", "Sep", "Okt", "Nov", "Dec"},
};
static const char *MONTHS_FULL[3][12] = {
    {"Январь", "Февраль", "Март", "Апрель", "Май", "Июнь", "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"},
    {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"},
    {"januar", "februar", "mart", "april", "maj", "jun", "jul", "avgust", "septembar", "oktobar", "novembar", "decembar"},
};
static const char *LOCALES[] = {"ru", "en", "sr-Latn"};
static int active = 0;
}

void ui_strings_set_locale(const char *locale)
{
    active = 0;
    if (locale && !std::strcmp(locale, "en")) active = 1;
    else if (locale && !std::strcmp(locale, "sr-Latn")) active = 2;
}

const char *ui_strings_locale(void) { return LOCALES[active]; }

const char *ui_text(const char *russian)
{
    if (!russian) return "";
    for (const auto &item : STRINGS) {
        if (!std::strcmp(russian, item.ru) || !std::strcmp(russian, item.en) || !std::strcmp(russian, item.sr))
            return active == 0 ? item.ru : active == 1 ? item.en : item.sr;
    }
    return russian;
}

const char *ui_day_name(int day)
{
    return day >= 0 && day < 7 ? DAYS[active][day] : "";
}

const char *ui_month_name(int month)
{
    return month >= 0 && month < 12 ? MONTHS[active][month] : "";
}

const char *ui_month_name_full(int month)
{
    return month >= 0 && month < 12 ? MONTHS_FULL[active][month] : "";
}
