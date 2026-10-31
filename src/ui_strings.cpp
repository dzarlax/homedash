#include "ui_strings.h"
#include <cstring>

namespace {
struct entry_t { const char *ru; const char *en; const char *sr; };

// Keep the Russian key as the stable source string so existing call sites are
// easy to compare with the former Russian-only UI.
static const entry_t STRINGS[] = {
    {"Задачи на сегодня", "Today's tasks", "Današnji zadaci"},
    {"За день", "Today", "Danas"},
    {"Обновлено %d мин назад", "Updated %d min ago", "Ažurirano pre %d min"},
    {"Все показатели", "All metrics", "Svi pokazatelji"},
    {"Пульс в покое", "Resting heart rate", "Puls u mirovanju"},
    {"уд/мин", "bpm", "otk/min"},
    {"%d ч %02d м", "%d h %02d m", "%d č %02d m"},
    {"Загружаю данные…", "Loading data…", "Učitavanje podataka…"},
    {"Блоки Сводки выключены в настройках", "Summary blocks are disabled in Settings", "Blokovi pregleda su isključeni u podešavanjima"},
    {"Источник выключен", "Source disabled", "Izvor je isključen"},
    {"Не удалось загрузить данные", "Could not load data", "Učitavanje podataka nije uspelo"},
    {"Возраст данных неизвестен", "Data age unknown", "Starost podataka nije poznata"},
    {"Не удалось обновить · данные %d мин назад", "Refresh failed · data from %d min ago", "Osvežavanje nije uspelo · podaci od pre %d min"},
    {"Данные Bridge: %d мин назад", "Bridge data: %d min ago", "Podaci Bridge-a: pre %d min"},
    {"Нет оценки", "No score", "Nema ocene"},
    {"%.1f ч", "%.1f h", "%.1f č"},
    {"%s · %d ч назад", "%s · %d h ago", "%s · pre %d č"},
    {"Ожидает обновления · получено %d мин назад", "Awaiting refresh · received %d min ago", "Čeka osvežavanje · primljeno pre %d min"},
    {"Расписание может быть неполным · %d мин назад", "Schedule may be incomplete · %d min ago", "Raspored možda nije potpun · pre %d min"},
    {"Данные календаря ожидают обновления", "Calendar data awaiting refresh", "Podaci kalendara čekaju osvežavanje"},
    {"Расписание может быть неполным", "Schedule may be incomplete", "Raspored možda nije potpun"},
    {"Проверка подсветки. Возврат через %u с", "Backlight test. Restoring in %u s", "Test osvetljenja. Povratak za %u s"},
    {"Временная яркость. Доступен возврат.", "Temporary brightness. Restore is available.", "Privremeno osvetljenje. Povratak je dostupan."},
    {"Вернуть яркость", "Restore brightness", "Vrati osvetljenje"},
    {"Сначала начните сравнение", "Start a comparison first", "Prvo započnite poređenje"},
    {"Начните с исходной 50. Команды — не проценты.", "Start with reference 50. Commands are not percentages.", "Počnite od početne vrednosti 50. Komande nisu procenti."},
    {"Начать: исходная 50", "Start: reference 50", "Počni: početna vrednost 50"},
    {"Исходная 50 установлена. Экран читается?", "Reference 50 set. Is the screen readable?", "Početna vrednost 50 je postavljena. Da li je ekran čitljiv?"},
    {"Экран читается — начать", "Screen readable — begin", "Ekran je čitljiv — počni"},
    {"Сейчас %u. Возврат на 50 через %u с.", "Current %u. Restore 50 in %u s.", "Trenutno %u. Povratak na 50 za %u s."},
    {"Вернуть 50", "Restore 50", "Vrati 50"},
    {"Выберите шаг. Возврат — через 20 с после нажатия.", "Choose a step. Restore after 20 s without taps.", "Izaberite korak. Povratak posle 20 s bez dodira."},
    {"Возвращено 50. Ниже — сравнение завершённого теста.", "Restored 50. Comparison below is from the completed test.", "Vraćeno na 50. Ispod je poređenje završenog testa."},
    {"Ошибка шага. Сравнение отменено. Начните заново.", "Step failed. Comparison cancelled. Start again.", "Korak nije uspeo. Poređenje je otkazano. Počnite ponovo."},
    {"Предыдущий: %u · Тест: %u", "Previous: %u · Test: %u", "Prethodni: %u · Test: %u"},
    {"Текущий уровень: %u. Предыдущего нет.", "Current level: %u. No previous level.", "Trenutni nivo: %u. Nema prethodnog nivoa."},
    {"Сравнение ещё не начато", "Comparison has not started", "Poređenje još nije počelo"},
    {"Выбрано · яркий: %s · тёмный: %s", "Selected · bright: %s · dim: %s", "Izabrano · svetli: %s · tamni: %s"},
    {"Шаг: %u (нажмите для смены)", "Step: %u (tap to change)", "Korak: %u (dodirnite za promenu)"},
    {"Выбираем тёмный уровень", "Select the dim level", "Izaberite tamni nivo"},
    {"Выбираем яркий уровень", "Select the bright level", "Izaberite svetli nivo"},
    {"Слишком темно — выбрать %u", "Too dim — select %u", "Previše tamno — izaberi %u"},
    {"Разницы нет — выбрать %u", "No difference — select %u", "Nema razlike — izaberi %u"},
    {"Предыдущего уровня нет", "No previous level", "Nema prethodnog nivoa"},
    {"Этот подходит — выбрать %u", "This works — select %u", "Ovaj odgovara — izaberi %u"},
    {"Ярче", "Brighter", "Svetlije"},
    {"Темнее", "Dimmer", "Tamnije"},
    {"Подбор подсветки", "Adjust backlight", "Podešavanje osvetljenja"},
    {"Пошаговый подбор с возвратом на 50", "Step adjustment with return to 50", "Podešavanje po koracima uz povratak na 50"},
    {"20 с без нажатий · сессия до 2 минут · выбор до перезагрузки", "20 s without taps · session up to 2 min · selections last until reboot", "20 s bez dodira · sesija do 2 min · izbor važi do ponovnog pokretanja"},

    {"Экран читается? Выберите уровень теста.", "Is the screen readable? Choose a test level.", "Da li je ekran čitljiv? Izaberite nivo testa."},
    {"Ярче: 20 секунд", "Brighter: 20 seconds", "Svetlije: 20 sekundi"},
    {"Ночной: 20 секунд", "Night: 20 seconds", "Noćni: 20 sekundi"},
    {"Ярче. Возврат через %u с", "Brighter. Restoring in %u s", "Svetlije. Povratak za %u s"},
    {"Ночной. Возврат через %u с", "Night. Restoring in %u s", "Noćni. Povratak za %u s"},
    {"Тест подсветки", "Backlight test", "Test osvetljenja"},
    {"Установить исходную", "Set reference level", "Postavi početni nivo"},
    {"Установит новую исходную яркость, не прежнюю.", "Sets a new reference brightness, not the previous level.", "Postavlja novi početni nivo osvetljenja, ne prethodni."},
    {"Тест подсветки недоступен", "Backlight test unavailable", "Test osvetljenja nije dostupan"},
    {"Экран читается? Подтвердите тест на 20 секунд.", "Is the screen readable? Confirm the 20-second test.", "Da li je ekran čitljiv? Potvrdite test od 20 sekundi."},
    {"Экран читается: тест", "Screen readable: test", "Ekran je čitljiv: test"},
    {"Возврат к исходной через %u с", "Restoring reference in %u s", "Povratak na početni nivo za %u s"},
    {"Вернуть сейчас", "Restore now", "Vrati sada"},
    {"Команда возврата выполнена. Проверьте яркость.", "Restore command completed. Check brightness.", "Komanda vraćanja je izvršena. Proverite osvetljenje."},
    {"Повторить тест", "Repeat test", "Ponovi test"},
    {"Ошибка возврата. Новые тесты заблокированы.", "Restore failed. New tests are blocked.", "Vraćanje nije uspelo. Novi testovi su blokirani."},
    {"Неполное расписание", "Partial schedule", "Nepotpun raspored"},
    {"Часть календарей недоступна", "Some calendars unavailable", "Neki kalendari nisu dostupni"},
    {"Часть календарей недоступна · %d мин назад", "Some calendars unavailable · received %d min ago", "Neki kalendari nisu dostupni · primljeno pre %d min"},
    {"Список событий неполный", "Event list is incomplete", "Spisak događaja nije potpun"},
    {"Нет событий в ответе", "No events in this response", "Nema događaja u ovom odgovoru"},
    {"Предыдущая", "Previous", "Prethodna"},
    {"Следующая", "Next", "Sledeća"},
    {"Пауза", "Pause", "Pauza"},
    {"Продолжить", "Resume", "Nastavi"},
    {"Скрыть данные", "Hide info", "Sakrij podatke"},
    {"Показать данные", "Show info", "Prikaži podatke"},
    {"Повторить", "Retry", "Pokušaj ponovo"},
    {"Выйти", "Exit", "Izađi"},
    {"Погода и транспорт", "Weather and transport", "Vreme i prevoz"},
    {"Не удалось обновить", "Could not refresh", "Osvežavanje nije uspelo"},
    {"Ещё не обновлялось", "Not received yet", "Još nije primljeno"},
    {"Последние данные: %d мин назад", "Last known: received %d min ago", "Poslednji podaci: pre %d min"},
    {"Получено дисплеем только что", "Just received by display", "Ekran je upravo primio podatke"},
    {"Получено дисплеем %d мин назад", "Received by display %d min ago", "Ekran je primio pre %d min"},
    {"Сегодня: %.0f° / %.0f°", "Today: %.0f° / %.0f°", "Danas: %.0f° / %.0f°"},
    {"Проверьте подключение в настройках", "Check the connection in Settings", "Proverite vezu u podešavanjima"},
    {"Нет связи с Bridge. Проверьте настройки", "Bridge offline. Check Settings", "Bridge nije povezan. Proverite podešavanja"},
    {"Нет Wi-Fi. Проверьте настройки", "No Wi-Fi. Check Settings", "Nema Wi-Fi mreže. Proverite podešavanja"},
    {"Включено", "On", "Uključeno"},
    {"Выключено", "Off", "Isključeno"},
    {"Открыть управление", "Open controls", "Otvori kontrole"},
    {"Выбрано %.0f C", "Selected %.0f C", "Izabrano %.0f C"},
    {"Нет доступа. Отклонено", "No access. Rejected", "Nema pristupa. Odbijeno"},
    {"Не поддерживается", "Unsupported", "Nije podržano"},
    {"Нет Wi-Fi. Не отправлено", "No Wi-Fi. Not sent", "Nema Wi-Fi. Nije poslato"},
    {"Очередь занята. Не отправлено", "Queue busy. Not sent", "Red zauzet. Nije poslato"},
    {"Время истекло. Не отправлено", "Expired. Not sent", "Isteklo. Nije poslato"},
    {"Отправка...", "Sending...", "Slanje..."},
    {"Проверка...", "Checking...", "Provera..."},
    {"Таймаут. Исход неизвестен", "Timeout. Outcome unknown", "Isteklo. Ishod nepoznat"},
    {"Исход неизвестен", "Outcome unknown", "Ishod nepoznat"},
    {"Нет подтверждения", "Not confirmed", "Nije potvrđeno"},
    {"Подтверждено", "Confirmed", "Potvrđeno"},
    {"Состояние обновлено", "State updated", "Stanje ažurirano"},
    {"Отклонено", "Rejected", "Odbijeno"},
    {"Не отправлено", "Not sent", "Nije poslato"},
    {"Настройки", "Settings", "Podešavanja"},
    {"Веб-панель и локальное восстановление", "Web panel and local recovery", "Veb-panel i lokalni oporavak"},
    {"Веб-панель Bridge", "Bridge web panel", "Veb-panel Bridge"},
    {"Экран, языки, фотографии и сервисы", "Display, languages, photos and services", "Ekran, jezici, fotografije i servisi"},
    {"Вход через пароль или Cloudflare", "Sign in with password or Cloudflare", "Prijava lozinkom ili preko Cloudflare-a"},
    {"Откройте веб-панель Bridge в браузере", "Open the Bridge web panel in a browser", "Otvorite Bridge veb-panel u pregledaču"},
    {"Локальное восстановление", "Local recovery", "Lokalni oporavak"},
    {"Показать пароль", "Show password", "Prikaži lozinku"},
    {"Скрыть пароль", "Hide password", "Sakrij lozinku"},
    {"Выключить сеть", "Turn off network", "Isključi mrežu"},
    {"Включить на 10 минут", "Enable for 10 minutes", "Uključi na 10 minuta"},
    {"Временная сеть выключена", "Temporary network is off", "Privremena mreža je isključena"},
    {"Для восстановления Wi-Fi или связи с Bridge.", "To recover Wi-Fi or the Bridge connection.", "Za oporavak Wi-Fi ili veze sa Bridge-om."},
    {"Пароль открывается на 15 секунд по нажатию.", "Tap to show the password for 15 seconds.", "Dodirnite za prikaz lozinke na 15 sekundi."},
    {"Сеть: %s", "Network: %s", "Mreža: %s"},
    {"Устройство", "Device", "Uređaj"},
    {"Проверить обновление", "Check for updates", "Proveri ažuriranja"},
    {"Подключён", "Connected", "Povezano"},
    {"На связи", "Online", "Na mreži"},
    {"Нет связи", "Offline", "Nije povezano"},
    {"Wi-Fi: %s\nBridge: %s", "Wi-Fi: %s\nBridge: %s", "Wi-Fi: %s\nBridge: %s"},
    {"Охлаждение", "Cool", "Hlađenje"}, {"Обогрев", "Heat", "Grejanje"},
    {"Осушение", "Dry", "Sušenje"}, {"Вентиляция", "Fan", "Ventilator"},

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
