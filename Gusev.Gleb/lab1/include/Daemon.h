#pragma once

#include <string>

#include "Notifier.h"
#include "PidFile.h"
#include "Scheduler.h"

// Демон-напоминалка. Синглтон: в процессе существует ровно один экземпляр,
// создать второй или скопировать нельзя — как и сам демон в системе один.
class Daemon {
public:
    static constexpr int kIntervalSeconds = 10;  // период проверки (вариант 19)
    static constexpr const char* kName = "reminder";

    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    // Полный жизненный цикл: остановить старый экземпляр, демонизироваться,
    // работать до SIGTERM. Возвращает код выхода процесса.
    int run(const std::string& configPath);

private:
    Daemon();

    bool stopOldInstance();
    bool daemonize();
    bool setupSignals();
    void reloadConfig();
    void tick();
    void loop();

    static std::string defaultPidPath();

    std::string configPath_;  // абсолютный — после chdir("/") относительный сломается
    PidFile pidFile_;
    Scheduler scheduler_;
    Notifier notifier_;
    int signalFd_ = -1;
    bool running_ = true;
};
