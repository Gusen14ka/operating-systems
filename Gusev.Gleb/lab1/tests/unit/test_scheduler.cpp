#include "Scheduler.h"
#include "test.h"

TEST_SUITE("Scheduler — расписание и память о показанных событиях");

namespace {

Event ev(const std::string& line) {
    std::string err;
    return parseEvent(line, err).value();
}

std::time_t at(const std::string& dateTime) { return ev("add_event " + dateTime + " x").when; }

const std::time_t t1830 = at("2026-10-10 18:30");

std::vector<std::string> texts(const std::vector<Event>& events) {
    std::vector<std::string> out;
    for (const auto& e : events) out.push_back(e.text);
    return out;
}

using Texts = std::vector<std::string>;

}  // namespace

TEST(one_time_fires_once_at_its_time, "разовое событие: до времени — нет, в момент — да, потом — нет") {
    Scheduler s;
    const auto r = s.load({ev("add_event 2026-10-10 18:30 раз")}, t1830 - 600);
    EXPECT_EQ(r.scheduled, 1u);
    EXPECT_EQ(texts(s.takeDue(t1830 - 1)), Texts{});
    EXPECT_EQ(texts(s.takeDue(t1830 + 5)), Texts{"раз"});
    EXPECT_EQ(s.size(), 0u);
    EXPECT_EQ(texts(s.takeDue(t1830 + 15)), Texts{});
}

TEST(grace_period_on_load, "запас 60 с: добавленное в 18:30:40 «на 18:30» показывается, в 18:31:30 — нет") {
    Scheduler inTime;
    EXPECT_EQ(inTime.load({ev("add_event 2026-10-10 18:30 a")}, t1830 + 40).scheduled, 1u);
    EXPECT_EQ(texts(inTime.takeDue(t1830 + 40)), Texts{"a"});

    Scheduler late;
    const auto r = late.load({ev("add_event 2026-10-10 18:30 a")}, t1830 + 90);
    EXPECT_EQ(r.scheduled, 0u);
    EXPECT_EQ(r.expired, 1u);
}

TEST(reload_does_not_repeat_shown, "SIGHUP сразу после показа: разовое событие не показывается снова") {
    Scheduler s;
    const std::vector<Event> config = {ev("add_event 2026-10-10 18:30 a"),
                                       ev("add_event 2026-10-10 19:00 b")};
    s.load(config, t1830 - 5);
    EXPECT_EQ(texts(s.takeDue(t1830 + 5)), Texts{"a"});

    const auto r = s.load(config, t1830 + 15);  // перечитали тот же конфиг
    EXPECT_EQ(r.done, 1u);
    EXPECT_EQ(r.scheduled, 1u);
    EXPECT_EQ(texts(s.takeDue(t1830 + 25)), Texts{});
}

TEST(reload_keeps_repeat_progress, "SIGHUP: ежечасное продолжает со следующего часа, а не заново") {
    Scheduler s;
    const std::vector<Event> config = {ev("add_event 2026-10-10 18:30 -h час")};
    s.load(config, t1830 - 5);
    EXPECT_EQ(texts(s.takeDue(t1830 + 5)), Texts{"час"});
    s.load(config, t1830 + 15);
    EXPECT_EQ(texts(s.takeDue(t1830 + 25)), Texts{});
    EXPECT_EQ(texts(s.takeDue(t1830 + 3600)), Texts{"час"});
    EXPECT_EQ(s.size(), 1u);
}

TEST(missed_periods_collapse, "демон «проспал» 3 часа: ежечасное показывается один раз, а не три") {
    Scheduler s;
    s.load({ev("add_event 2026-10-10 18:30 -h час")}, t1830 - 5);
    EXPECT_EQ(texts(s.takeDue(t1830 + 3 * 3600 + 60)), Texts{"час"});
    EXPECT_EQ(texts(s.takeDue(t1830 + 3 * 3600 + 70)), Texts{});
    EXPECT_EQ(texts(s.takeDue(t1830 + 4 * 3600)), Texts{"час"});
}

TEST(duplicates_collapse, "две одинаковые строки конфига — одно событие") {
    Scheduler s;
    const auto r = s.load({ev("add_event 2026-10-10 18:30 a"), ev("add_event 2026-10-10 18:30 a")},
                          t1830 - 5);
    EXPECT_EQ(r.scheduled, 1u);
    EXPECT_EQ(texts(s.takeDue(t1830)), Texts{"a"});
}

TEST(removed_event_forgotten, "строку удалили из конфига и вернули — память стёрта, покажется снова") {
    Scheduler s;
    s.load({ev("add_event 2026-10-10 18:30 a")}, t1830 - 5);
    s.takeDue(t1830);
    s.load({}, t1830 + 1);
    const auto r = s.load({ev("add_event 2026-10-10 18:30 a")}, t1830 + 2);
    EXPECT_EQ(r.scheduled, 1u);
    EXPECT_EQ(r.done, 0u);
}

TEST(same_minute_events_all_fire, "несколько событий на одну минуту показываются все, в порядке конфига") {
    Scheduler s;
    s.load({ev("add_event 2026-10-10 18:30 первое"), ev("add_event 2026-10-10 18:30 второе"),
            ev("add_event 2026-10-10 18:31 позже")},
           t1830 - 5);
    EXPECT_EQ(texts(s.takeDue(t1830 + 1)), (Texts{"первое", "второе"}));
    EXPECT_EQ(s.size(), 1u);
}
