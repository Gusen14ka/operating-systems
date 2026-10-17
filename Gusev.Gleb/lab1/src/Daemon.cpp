#include "Daemon.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

#include "Config.h"

using namespace std::chrono_literals;

namespace {

// Текущее время по точным часам. std::time() в glibc берёт «грубые» часы ядра,
// которые в первые миллисекунды новой секунды ещё показывают предыдущую.
// Тогда событие «18:30», добавленное в 18:30:00.003 по date, демон счёл бы
// ещё не наступившим и показал бы только на следующем тике, через 10 с.
std::time_t wallNow() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec;
}

}  // namespace

Daemon& Daemon::instance() {
    // Локальная static-переменная создаётся при первом вызове, ровно один раз
    // (в C++11+ это потокобезопасно). Конструктор приватный — других не будет.
    static Daemon daemon;
    return daemon;
}

Daemon::Daemon() : pidFile_(defaultPidPath()) {}

std::string Daemon::defaultPidPath() {
    // XDG_RUNTIME_DIR (/run/user/<uid>) — личная папка пользователя для pid-файлов
    // и сокетов, чистится при выходе. В /var/run обычному пользователю писать нельзя.
    const char* dir = std::getenv("XDG_RUNTIME_DIR");
    return std::string(dir && *dir ? dir : "/tmp") + "/" + kName + ".pid";
}

int Daemon::run(const std::string& configPath) {
    // 1. Абсолютный путь к конфигу — пока мы ещё в рабочем каталоге
    char resolved[PATH_MAX];
    if (realpath(configPath.c_str(), resolved) == nullptr) {
        std::cerr << kName << ": конфиг " << configPath << ": " << std::strerror(errno) << '\n';
        return 1;
    }
    configPath_ = resolved;

    // LOG_PID — добавлять pid к каждой записи; LOG_DAEMON — "категория" демонов
    openlog(kName, LOG_PID, LOG_DAEMON);

    // 2. Старый экземпляр — до демонизации, чтобы ошибки ещё видел пользователь
    if (!stopOldInstance()) {
        closelog();
        return 1;
    }

    // 3. Демонизация
    if (!daemonize()) {
        closelog();
        return 1;
    }

    // 4. Сигналы — ДО записи pid-файла. Кто прочитал pid из файла, тот может
    //    сразу слать SIGHUP/SIGTERM, и сигнал не должен потеряться или убить
    //    процесс действием по умолчанию, пока обработка ещё не настроена.
    if (!setupSignals()) {
        closelog();
        return 1;
    }

    // 5. Свой pid — после демонизации (pid окончательный) и после сигналов
    std::string error;
    if (!pidFile_.write(getpid(), error)) {
        syslog(LOG_ERR, "pid-файл: %s", error.c_str());
        close(signalFd_);
        closelog();
        return 1;
    }

    syslog(LOG_INFO, "запущен, конфиг %s, pid-файл %s", configPath_.c_str(),
           pidFile_.path().c_str());
    reloadConfig();
    loop();

    syslog(LOG_INFO, "получен SIGTERM, завершение работы");
    // Удаляем pid-файл, только если он наш: новый экземпляр мог уже записать свой
    if (pidFile_.read() == getpid()) {
        pidFile_.remove();
    }
    close(signalFd_);
    closelog();
    return 0;
}

bool Daemon::stopOldInstance() {
    const auto oldPid = pidFile_.read();
    if (!oldPid || *oldPid == getpid() || !PidFile::isRunning(*oldPid, kName)) {
        return true;  // старого нет (или pid-файл остался от упавшего процесса)
    }

    syslog(LOG_INFO, "найден работающий экземпляр (pid %d), отправляю SIGTERM", *oldPid);
    if (PidFile::terminate(*oldPid, kName, 5s)) {
        return true;
    }
    syslog(LOG_ERR, "экземпляр pid %d не завершился за 5 секунд", *oldPid);
    std::cerr << kName << ": старый экземпляр (pid " << *oldPid << ") не завершился\n";
    return false;
}

bool Daemon::daemonize() {
    // Первый fork: родитель выходит, терминал получает приглашение обратно.
    // Ребёнок гарантированно не лидер группы процессов — иначе setsid не сработает.
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork: %s", std::strerror(errno));
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    // Новая сессия: отрываемся от управляющего терминала. Закрытие терминала
    // больше не пришлёт нам SIGHUP.
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid: %s", std::strerror(errno));
        return false;
    }

    // Второй fork: лидер сессии может случайно снова получить управляющий
    // терминал, открыв tty. Внук лидером не является — не сможет.
    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork: %s", std::strerror(errno));
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    umask(0);       // права создаваемых файлов задаём явно, а не наследуем
    if (chdir("/") != 0) {  // не держим занятым каталог, откуда запустили
        syslog(LOG_ERR, "chdir: %s", std::strerror(errno));
        return false;
    }

    // stdin/stdout/stderr -> /dev/null: терминала больше нет, писать некуда
    const int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0) {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO) close(devNull);
    }
    return true;
}

bool Daemon::setupSignals() {
    // Блокируем сигналы: ядро не прерывает ими программу, а складывает в очередь,
    // которую мы читаем из signalfd как обычный файл. Так обработка идёт в
    // главном цикле, а не в обработчике, где почти ничего нельзя вызывать.
    // Унаследованное SIG_IGN (например, при запуске через nohup) сбрасываем:
    // сигналы должны доходить до signalfd
    for (int sig : {SIGHUP, SIGTERM, SIGINT}) {
        signal(sig, SIG_DFL);
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) != 0) {
        syslog(LOG_ERR, "sigprocmask: %s", std::strerror(errno));
        return false;
    }
    signalFd_ = signalfd(-1, &mask, SFD_CLOEXEC);  // CLOEXEC: не отдавать fd терминалам
    if (signalFd_ < 0) {
        syslog(LOG_ERR, "signalfd: %s", std::strerror(errno));
        return false;
    }
    return true;
}

void Daemon::reloadConfig() {
    std::string error;
    const auto cfg = loadConfig(configPath_, error);
    if (!cfg) {
        // Конфиг пропал — продолжаем со старым расписанием, а не падаем
        syslog(LOG_ERR, "не удалось прочитать конфиг: %s", error.c_str());
        return;
    }
    for (const auto& w : cfg->warnings) {
        syslog(LOG_WARNING, "%s: %s", configPath_.c_str(), w.c_str());
    }
    notifier_ = Notifier(cfg->terminal);

    const auto r = scheduler_.load(cfg->events, wallNow());
    syslog(LOG_INFO, "конфиг загружен: событий %zu, в прошлом пропущено %zu, уже показано %zu",
           r.scheduled, r.expired, r.done);
}

void Daemon::tick() {
    const int reaped = Notifier::reapChildren();
    (void)reaped;

    for (const auto& ev : scheduler_.takeDue(wallNow())) {
        std::string error;
        const pid_t pid = notifier_.show(ev.text, error);
        if (pid > 0) {
            syslog(LOG_INFO, "напоминание (pid окна %d): %s", pid, ev.text.c_str());
        } else {
            syslog(LOG_ERR, "не удалось показать '%s': %s", ev.text.c_str(), error.c_str());
        }
    }
}

void Daemon::loop() {
    using clock = std::chrono::steady_clock;  // монотонные часы: не прыгают при смене времени
    auto nextTick = clock::now();             // первая проверка — сразу

    while (running_) {
        const auto now = clock::now();
        if (now >= nextTick) {
            tick();
            nextTick = now + std::chrono::seconds(kIntervalSeconds);
        }

        // Спим до следующего тика или до сигнала — что раньше
        const auto waitMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(nextTick - clock::now()).count();
        pollfd pfd{signalFd_, POLLIN, 0};
        const int ready = poll(&pfd, 1, static_cast<int>(std::max<long long>(waitMs, 0)));
        if (ready < 0) {
            if (errno == EINTR) continue;
            syslog(LOG_ERR, "poll: %s", std::strerror(errno));
            break;
        }
        if (ready == 0) {
            continue;  // таймаут — пора тикать
        }

        signalfd_siginfo info{};
        if (read(signalFd_, &info, sizeof info) != sizeof info) {
            continue;
        }
        switch (info.ssi_signo) {
            case SIGHUP:
                syslog(LOG_INFO, "получен SIGHUP, перечитываю конфиг");
                reloadConfig();
                nextTick = clock::now();  // новые события проверить сразу
                break;
            case SIGTERM:
            case SIGINT:
                running_ = false;
                break;
        }
    }
}
