#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "Config.h"
#include "test.h"

TEST_SUITE("Config — чтение конфигурационного файла");

namespace {

Config parse(const std::string& text) {
    std::istringstream in(text);
    return parseConfig(in);
}

using Words = std::vector<std::string>;

}  // namespace

TEST(full_config, "комментарии, пустые строки, отступы и CRLF; события и terminal разобраны") {
    const Config c = parse(
        "# напоминалка\n"
        "\n"
        "terminal = xterm -e\n"
        "add_event 2026-10-12 18:30 -d Сдать лабу\r\n"
        "   add_event 2026-10-13 09:00   Позвонить  \n"
        "  # комментарий с отступом\n");
    EXPECT_EQ(c.warnings, Words{});
    EXPECT_EQ(c.terminal, (Words{"xterm", "-e"}));
    ASSERT_EQ(c.events.size(), 2u);
    EXPECT_EQ(c.events[0].source, "add_event 2026-10-12 18:30 -d Сдать лабу");
    EXPECT_EQ(c.events[1].source, "add_event 2026-10-13 09:00   Позвонить");
}

TEST(bad_lines_skipped_with_line_numbers, "плохие строки пропускаются, предупреждение с номером строки") {
    const Config c = parse(
        "add_event 2026-10-12 18:30 ok\n"                 // 1 — корректная
        "add_event 2026-02-31 18:30 плохая дата\n"        // 2
        "terminal\n"                                      // 3
        "terminal =   \n"                                 // 4
        "foo bar\n"                                       // 5
        "add_events 2026-10-12 18:30 опечатка в ключе\n");  // 6
    ASSERT_EQ(c.events.size(), 1u);
    EXPECT_EQ(c.events[0].text, "ok");
    EXPECT_EQ(c.terminal, Words{});
    ASSERT_EQ(c.warnings.size(), 5u);

    const char* reasons[] = {"некорректные дата/время", "terminal = <команда>", "terminal = <команда>",
                             "неизвестная запись 'foo bar'", "неизвестная запись 'add_events"};
    for (std::size_t i = 0; i < 5; ++i) {
        CONTEXT("предупреждение #" << i << ": " << c.warnings[i]);
        EXPECT_EQ(c.warnings[i].rfind("строка " + std::to_string(i + 2) + ": ", 0), 0u);
        EXPECT_CONTAINS(c.warnings[i], reasons[i]);
    }
}

TEST(terminal_variants, "terminal без пробелов вокруг '='; при повторе побеждает последний") {
    EXPECT_EQ(parse("terminal=lxterm -e\n").terminal, (Words{"lxterm", "-e"}));
    EXPECT_EQ(parse("terminal=lxterm -e\nterminal = x-terminal-emulator -e\n").terminal,
              (Words{"x-terminal-emulator", "-e"}));
}

TEST(empty_config_valid, "пустой конфиг и конфиг из одних комментариев валидны") {
    for (const char* text : {"", "\n\n", "# только комментарий\n"}) {
        CONTEXT("конфиг: \"" << text << "\"");
        const Config c = parse(text);
        EXPECT_TRUE(c.events.empty());
        EXPECT_TRUE(c.warnings.empty());
        EXPECT_TRUE(c.terminal.empty());
    }
}

TEST(load_existing_file, "loadConfig читает файл с диска") {
    const std::string path = "/tmp/reminder_unit_config_" + std::to_string(getpid()) + ".conf";
    std::ofstream(path) << "add_event 2026-10-12 18:30 из файла\n";
    std::string error;
    const auto c = loadConfig(path, error);
    std::remove(path.c_str());
    ASSERT_TRUE(c.has_value());
    ASSERT_EQ(c->events.size(), 1u);
    EXPECT_EQ(c->events[0].text, "из файла");
}

TEST(load_missing_file, "loadConfig для отсутствующего файла: nullopt и причина с путём") {
    std::string error;
    EXPECT_FALSE(loadConfig("/nonexistent/reminder.conf", error).has_value());
    EXPECT_CONTAINS(error, "/nonexistent/reminder.conf");
    EXPECT_CONTAINS(error, "No such file");
}
