#pragma once

#include <ctime>
#include <optional>
#include <string>

// Как часто повторять напоминание
enum class Repeat { None, Hourly, Daily, Weekly };

struct Event {
    std::time_t when = 0;          // момент срабатывания (секунды с 1970, UTC)
    Repeat repeat = Repeat::None;  // флаг -h / -d / -w
    std::string text;              // текст напоминания
    std::string source;            // исходная строка без краевых пробелов —
                                   // по ней узнаём событие после перечитывания конфига
};

// Разбирает строку вида "add_event YYYY-MM-DD HH:MM [-h|-d|-w] text".
// Дата и время трактуются как локальное время.
// При ошибке возвращает std::nullopt и пишет причину в error.
std::optional<Event> parseEvent(const std::string& line, std::string& error);

// Момент через один период повтора после t.
// -h: ровно +1 час; -d/-w: то же время на часах через 1/7 календарных дней
// (при переходе на летнее время сутки бывают 23 или 25 часов).
// Для Repeat::None возвращает t без изменений.
std::time_t advance(std::time_t t, Repeat repeat);

// Готовит событие к планированию: ev.when должен быть не раньше notBefore.
// Повторяемое событие из прошлого проматывается вперёд по периоду.
// Разовое из прошлого трогать нельзя — тогда возвращается false
// (событие уже неактуально, его надо пропустить).
bool scheduleFrom(Event& ev, std::time_t notBefore);
