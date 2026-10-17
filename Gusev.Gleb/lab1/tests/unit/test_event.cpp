#include <cstdlib>
#include <ctime>
#include <string>

#include "Event.h"
#include "test.h"

TEST_SUITE("Event — разбор строки add_event и расчёт повторов");

// Для отчёта: печатать Repeat словами, а не числом
static std::ostream& operator<<(std::ostream& os, Repeat r) {
    switch (r) {
        case Repeat::None:   return os << "None";
        case Repeat::Hourly: return os << "Hourly";
        case Repeat::Daily:  return os << "Daily";
        case Repeat::Weekly: return os << "Weekly";
    }
    return os << "?";
}

namespace {

struct Parsed {
    std::optional<Event> event;
    std::string error;
};

Parsed parse(const std::string& line) {
    Parsed p;
    p.event = parseEvent(line, p.error);
    return p;
}

std::string fmt(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

// Момент времени по строке "ГГГГ-ММ-ДД ЧЧ:ММ" (через тот же парсер)
std::time_t at(const std::string& dateTime) {
    return parse("add_event " + dateTime + " x").event.value().when;
}

Event event(const std::string& line) { return parse(line).event.value(); }

// Временно сменить часовой пояс процесса; при выходе из блока — вернуть
class TimezoneGuard {
public:
    explicit TimezoneGuard(const char* tz) {
        const char* old = std::getenv("TZ");
        hadOld_ = old != nullptr;
        if (hadOld_) old_ = old;
        setenv("TZ", tz, 1);
        tzset();
    }
    ~TimezoneGuard() {
        if (hadOld_) setenv("TZ", old_.c_str(), 1); else unsetenv("TZ");
        tzset();
    }

private:
    bool hadOld_ = false;
    std::string old_;
};

}  // namespace

// --- parseEvent: корректные строки ---------------------------------------------

TEST(parse_one_time, "строка без флага — разовое событие, дата и текст разобраны") {
    const auto p = parse("add_event 2026-10-12 18:30 Позвонить маме");
    ASSERT_TRUE(p.event.has_value());
    EXPECT_EQ(p.event->repeat, Repeat::None);
    EXPECT_EQ(fmt(p.event->when), "2026-10-12 18:30");
    EXPECT_EQ(p.event->text, "Позвонить маме");
}

TEST(parse_repeat_flags, "флаги -h -d -w задают повтор каждый час / день / неделю") {
    const struct { const char* flag; Repeat expected; } cases[] = {
        {"-h", Repeat::Hourly}, {"-d", Repeat::Daily}, {"-w", Repeat::Weekly}};
    for (const auto& c : cases) {
        CONTEXT("флаг " << c.flag);
        const auto p = parse(std::string("add_event 2026-10-12 18:30 ") + c.flag + " текст");
        ASSERT_TRUE(p.event.has_value());
        EXPECT_EQ(p.event->repeat, c.expected);
        EXPECT_EQ(p.event->text, "текст");
    }
}

TEST(parse_keeps_inner_spaces, "пробелы внутри текста сохраняются как есть") {
    const auto p = parse("add_event 2026-10-12 18:30 -d Сдать  лабу   по ОС");
    ASSERT_TRUE(p.event.has_value());
    EXPECT_EQ(p.event->text, "Сдать  лабу   по ОС");
}

TEST(parse_trims_edges_and_crlf, "краевые пробелы и \\r (конфиг из Windows) отрезаются") {
    const auto p = parse("  add_event   2026-10-12   18:30   созвон  \r");
    ASSERT_TRUE(p.event.has_value());
    EXPECT_EQ(p.event->text, "созвон");
    EXPECT_EQ(p.event->source, "add_event   2026-10-12   18:30   созвон");
}

TEST(parse_negative_number_is_text, "«-5 градусов» — это текст, а не флаг") {
    for (const char* text : {"-5 градусов", "-10 градусов", "- тире"}) {
        CONTEXT("текст: " << text);
        const auto p = parse(std::string("add_event 2026-10-12 18:30 ") + text);
        ASSERT_TRUE(p.event.has_value());
        EXPECT_EQ(p.event->repeat, Repeat::None);
        EXPECT_EQ(p.event->text, text);
    }
}

// --- parseEvent: ошибки --------------------------------------------------------

TEST(parse_rejects_invalid, "некорректная строка отклоняется с понятной причиной") {
    const struct { const char* line; const char* reason; } cases[] = {
        {"", "add_event"},
        {"event 2026-10-12 18:30 x", "add_event"},
        {"add_event 2026-10-12", "не указаны дата и время"},
        {"add_event 2026-10-12 18:30", "пустой текст"},
        {"add_event 2026-10-12 18:30 -w", "пустой текст"},
        {"add_event 2026-10-12 18:30 -d    ", "пустой текст"},
        {"add_event 2026-02-31 18:30 x", "некорректные дата/время"},
        {"add_event 2026-13-01 18:30 x", "некорректные дата/время"},
        {"add_event 2026-10-12 25:00 x", "некорректные дата/время"},
        {"add_event 2026-10-12 18:60 x", "некорректные дата/время"},
        {"add_event 2026-10-12 18:30x x", "некорректные дата/время"},
        {"add_event 12.10.2026 18:30 x", "некорректные дата/время"},
        {"add_event 2026-10-12 18:30 -x опечатка", "неизвестный флаг '-x'"},
    };
    for (const auto& c : cases) {
        CONTEXT("строка: \"" << c.line << "\"");
        const auto p = parse(c.line);
        EXPECT_FALSE(p.event.has_value());
        EXPECT_CONTAINS(p.error, c.reason);
    }
}

// --- advance -------------------------------------------------------------------

TEST(advance_none_keeps_time, "Repeat::None не сдвигает время") {
    const auto t = at("2026-10-31 18:30");
    EXPECT_EQ(advance(t, Repeat::None), t);
}

TEST(advance_hourly, "-h: ровно +1 час, в том числе через полночь и Новый год") {
    EXPECT_EQ(fmt(advance(at("2026-10-31 18:30"), Repeat::Hourly)), "2026-10-31 19:30");
    EXPECT_EQ(fmt(advance(at("2026-10-31 23:30"), Repeat::Hourly)), "2026-11-01 00:30");
    EXPECT_EQ(fmt(advance(at("2026-12-31 23:30"), Repeat::Hourly)), "2027-01-01 00:30");
}

TEST(advance_daily_weekly, "-d / -w: то же время через 1 / 7 календарных дней") {
    EXPECT_EQ(fmt(advance(at("2026-10-31 18:30"), Repeat::Daily)), "2026-11-01 18:30");
    EXPECT_EQ(fmt(advance(at("2026-10-31 18:30"), Repeat::Weekly)), "2026-11-07 18:30");
    EXPECT_EQ(fmt(advance(at("2026-12-28 09:00"), Repeat::Weekly)), "2027-01-04 09:00");
    EXPECT_EQ(fmt(advance(at("2028-02-28 09:00"), Repeat::Daily)), "2028-02-29 09:00");  // високосный
}

TEST(advance_daily_dst, "-d при переходе на летнее время: на часах то же время, прошло 23 ч") {
    TimezoneGuard tz("Europe/Berlin");  // 29.03.2026 в Берлине часы переводят вперёд
    const auto before = at("2026-03-28 10:00");
    const auto after = advance(before, Repeat::Daily);
    EXPECT_EQ(fmt(after), "2026-03-29 10:00");
    EXPECT_EQ(after - before, static_cast<std::time_t>(23 * 60 * 60));
}

// --- scheduleFrom --------------------------------------------------------------

TEST(schedule_future_unchanged, "будущее событие не сдвигается") {
    Event e = event("add_event 2026-10-11 09:00 будущее");
    EXPECT_TRUE(scheduleFrom(e, at("2026-10-10 12:00")));
    EXPECT_EQ(fmt(e.when), "2026-10-11 09:00");
}

TEST(schedule_past_one_time_rejected, "разовое событие из прошлого — неактуально (false)") {
    Event e = event("add_event 2026-10-09 09:00 прошлое");
    EXPECT_FALSE(scheduleFrom(e, at("2026-10-10 12:00")));
}

TEST(schedule_past_repeating_advanced, "повторяемое из прошлого переносится на ближайшее будущее") {
    const auto now = at("2026-10-10 12:00");
    const struct { const char* line; const char* expected; } cases[] = {
        {"add_event 2026-10-01 09:00 -d x", "2026-10-11 09:00"},
        {"add_event 2026-10-10 08:15 -h x", "2026-10-10 12:15"},
        {"add_event 2026-09-12 15:00 -w x", "2026-10-10 15:00"},
        {"add_event 2026-10-03 12:00 -w x", "2026-10-10 12:00"},  // ровно now — подходит
    };
    for (const auto& c : cases) {
        CONTEXT(c.line << ", now = 2026-10-10 12:00");
        Event e = event(c.line);
        EXPECT_TRUE(scheduleFrom(e, now));
        EXPECT_EQ(fmt(e.when), c.expected);
    }
}
