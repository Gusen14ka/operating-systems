#include "Scheduler.h"

#include <algorithm>
#include <set>

Scheduler::LoadResult Scheduler::load(const std::vector<Event>& events, std::time_t now) {
    LoadResult result;
    std::vector<Event> fresh;
    std::set<std::string> seen;

    for (Event ev : events) {
        // Две одинаковые строки в конфиге — одно событие
        if (!seen.insert(ev.source).second) {
            continue;
        }

        std::time_t notBefore = now - kGraceSeconds;
        const auto fired = fired_.find(ev.source);
        if (fired != fired_.end()) {
            if (ev.repeat == Repeat::None) {
                ++result.done;  // уже показали — второй раз не надо
                continue;
            }
            // Повторяемое: следующее срабатывание строго после последнего
            notBefore = std::max(notBefore, fired->second + 1);
        }

        if (scheduleFrom(ev, notBefore)) {
            fresh.push_back(std::move(ev));
            ++result.scheduled;
        } else {
            ++result.expired;
        }
    }

    // Забываем события, которых больше нет в конфиге, — память не растёт
    for (auto it = fired_.begin(); it != fired_.end();) {
        it = seen.count(it->first) ? std::next(it) : fired_.erase(it);
    }

    events_ = std::move(fresh);
    return result;
}

std::vector<Event> Scheduler::takeDue(std::time_t now) {
    std::vector<Event> due;

    for (auto it = events_.begin(); it != events_.end();) {
        if (it->when > now) {
            ++it;
            continue;
        }
        due.push_back(*it);
        fired_[it->source] = it->when;

        if (it->repeat == Repeat::None) {
            it = events_.erase(it);
            continue;
        }
        // Если демон "проспал" несколько периодов (ноутбук был во сне),
        // показываем один раз и прыгаем сразу в будущее
        while (it->when <= now) {
            it->when = advance(it->when, it->repeat);
        }
        ++it;
    }
    return due;
}
