#pragma once

#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "Event.h"

// Расписание: какие события ждут и какие уже сработали.
// Чистая логика без системных вызовов — время передаётся параметром.
class Scheduler {
public:
    // Запас при загрузке: событие "18:30", добавленное в 18:30:40, ещё покажем
    static constexpr std::time_t kGraceSeconds = 60;

    struct LoadResult {
        std::size_t scheduled = 0;  // будут показаны
        std::size_t expired = 0;    // разовые из прошлого — пропущены
        std::size_t done = 0;       // разовые, уже показанные до перечитывания
    };

    // Заменить список событий (запуск или SIGHUP).
    // Память о сработавших сохраняется: уже показанное разовое событие
    // не покажется снова, а повторяемое продолжит с места, где было.
    LoadResult load(const std::vector<Event>& events, std::time_t now);

    // Забрать события, время которых наступило. Разовые удаляются,
    // повторяемые переносятся на следующий период после now.
    std::vector<Event> takeDue(std::time_t now);

    std::size_t size() const { return events_.size(); }

private:
    std::vector<Event> events_;
    std::map<std::string, std::time_t> fired_;  // source -> когда сработало последний раз
};
