#include "Event.h"

#include <cctype>
#include <sstream>

namespace {

// "2026-10-12" + "18:30" -> time_t по локальному времени
std::optional<std::time_t> parseDateTime(const std::string& date, const std::string& time) {
    const std::string s = date + " " + time;

    std::tm tm{};
    const char* end = strptime(s.c_str(), "%Y-%m-%d %H:%M", &tm);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;  // формат не совпал или в конце мусор
    }

    // strptime не проверяет календарь: 2026-02-31 он примет.
    // mktime "нормализует" такую дату в 2026-03-03, поэтому сравниваем
    // поля до и после — если что-то сдвинулось, дата была невалидной.
    const std::tm original = tm;
    tm.tm_isdst = -1;  // пусть mktime сам решит, действует ли летнее время
    const std::time_t t = std::mktime(&tm);
    if (t == -1 || tm.tm_year != original.tm_year || tm.tm_mon != original.tm_mon ||
        tm.tm_mday != original.tm_mday || tm.tm_hour != original.tm_hour ||
        tm.tm_min != original.tm_min) {
        return std::nullopt;
    }
    return t;
}

std::optional<Repeat> parseFlag(const std::string& token) {
    if (token == "-h") return Repeat::Hourly;
    if (token == "-d") return Repeat::Daily;
    if (token == "-w") return Repeat::Weekly;
    return std::nullopt;
}

}  // namespace

std::optional<Event> parseEvent(const std::string& line, std::string& error) {
    std::istringstream in(line);
    std::string keyword, date, time;

    if (!(in >> keyword) || keyword != "add_event") {
        error = "строка должна начинаться с add_event";
        return std::nullopt;
    }
    if (!(in >> date >> time)) {
        error = "не указаны дата и время";
        return std::nullopt;
    }

    Event ev;
    const auto when = parseDateTime(date, time);
    if (!when) {
        error = "некорректные дата/время '" + date + " " + time + "', нужно YYYY-MM-DD HH:MM";
        return std::nullopt;
    }
    ev.when = *when;

    // Остаток строки: [флаг] текст. Текст берём целиком, с пробелами внутри.
    std::string rest;
    std::getline(in >> std::ws, rest);

    // Смотрим первое слово остатка: если это флаг — отрезаем его
    const std::size_t space = rest.find_first_of(" \t");
    const std::string first = rest.substr(0, space);
    if (const auto flag = parseFlag(first)) {
        ev.repeat = *flag;
        rest = (space == std::string::npos) ? "" : rest.substr(space);
        rest.erase(0, rest.find_first_not_of(" \t"));
    } else if (first.size() == 2 && first[0] == '-' &&
               std::isalpha(static_cast<unsigned char>(first[1]))) {
        // "-x" похоже на опечатку во флаге; "-5 градусов" — это просто текст
        error = "неизвестный флаг '" + first + "', допустимы -h -d -w";
        return std::nullopt;
    }

    // Убираем хвостовые пробелы и \r (если файл правили в Windows)
    rest.erase(rest.find_last_not_of(" \t\r") + 1);
    if (rest.empty()) {
        error = "пустой текст напоминания";
        return std::nullopt;
    }
    ev.text = rest;

    ev.source = line;
    ev.source.erase(ev.source.find_last_not_of(" \t\r") + 1);
    ev.source.erase(0, ev.source.find_first_not_of(" \t"));
    return ev;
}

std::time_t advance(std::time_t t, Repeat repeat) {
    switch (repeat) {
        case Repeat::None:
            return t;
        case Repeat::Hourly:
            return t + 60 * 60;
        case Repeat::Daily:
        case Repeat::Weekly: {
            // Сдвигаем день в календаре, а mktime сам разберётся
            // с концом месяца (31 окт + 1 = 1 ноя) и летним временем.
            std::tm tm{};
            localtime_r(&t, &tm);
            tm.tm_mday += (repeat == Repeat::Daily) ? 1 : 7;
            tm.tm_isdst = -1;
            return std::mktime(&tm);
        }
    }
    return t;  // недостижимо, но без этого gcc ругается на -Wreturn-type
}

bool scheduleFrom(Event& ev, std::time_t notBefore) {
    if (ev.when >= notBefore) {
        return true;
    }
    if (ev.repeat == Repeat::None) {
        return false;
    }
    // Шагаем по одному периоду. Даже ежечасное событие годовалой давности —
    // это ~9000 итераций, мгновенно.
    while (ev.when < notBefore) {
        ev.when = advance(ev.when, ev.repeat);
    }
    return true;
}
