#pragma once

#include <istream>
#include <optional>
#include <string>
#include <vector>

#include "Event.h"

// Содержимое конфигурационного файла.
//
// Формат (по строке на запись):
//   # комментарий
//   terminal = xterm -e
//   add_event 2026-10-12 18:30 -d Сдать лабу по ОС
struct Config {
    std::vector<Event> events;

    // Команда терминала, разбитая на слова: {"xterm", "-e"}.
    // К ней демон допишет "sh -c ..." с текстом. Пусто = выбрать по умолчанию.
    // Кавычки не поддерживаются: слова делятся по пробелам.
    std::vector<std::string> terminal;

    // Нефатальные проблемы ("строка 5: ..."): плохие строки пропускаются,
    // остальной конфиг применяется. Демон пишет их в syslog.
    std::vector<std::string> warnings;
};

// Разбор из потока — без работы с файлами, удобно тестировать.
Config parseConfig(std::istream& in);

// Чтение файла. nullopt, если файл не открылся (причина — в error).
std::optional<Config> loadConfig(const std::string& path, std::string& error);
