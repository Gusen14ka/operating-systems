#include "Notifier.h"

#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace {

// Скрипт, который выполнится внутри окна. Текст приходит в $1, а не
// вклеивается в строку, поэтому кавычки и $(...) в тексте безопасны.
// "read _" — у dash (/bin/sh в Ubuntu) read требует имя переменной.
const char* const kWindowScript =
    "printf '\\n  %s\\n\\n' \"$1\"; printf '  Нажмите Enter, чтобы закрыть'; read _";

}  // namespace

Notifier::Notifier(std::vector<std::string> terminal) : terminal_(std::move(terminal)) {}

std::string Notifier::findInPath(const std::string& program) {
    if (program.find('/') != std::string::npos) {
        return access(program.c_str(), X_OK) == 0 ? program : "";
    }
    const char* path = std::getenv("PATH");
    std::istringstream dirs(path ? path : "/usr/local/bin:/usr/bin:/bin");
    for (std::string dir; std::getline(dirs, dir, ':');) {
        const std::string full = (dir.empty() ? "." : dir) + "/" + program;
        if (access(full.c_str(), X_OK) == 0) {  // X_OK — есть право на запуск
            return full;
        }
    }
    return "";
}

pid_t Notifier::show(const std::string& text, std::string& error) const {
    // Выбираем терминал ДО fork: в родителе можно нормально сообщить об ошибке
    std::vector<std::string> terminal = terminal_;
    if (terminal.empty()) {
        for (const char* candidate : {"x-terminal-emulator", "xterm"}) {
            if (!findInPath(candidate).empty()) {
                terminal = {candidate, "-e"};
                break;
            }
        }
        if (terminal.empty()) {
            error = "не найден терминал (x-terminal-emulator или xterm)";
            return -1;
        }
    } else if (findInPath(terminal[0]).empty()) {
        error = "терминал '" + terminal[0] + "' не найден или не исполняемый";
        return -1;
    }

    // argv: <терминал...> sh -c <скрипт> sh <текст>
    std::vector<std::string> args = terminal;
    args.insert(args.end(), {"sh", "-c", kWindowScript, "sh", text});

    // execvp хочет массив char*, оканчивающийся nullptr.
    // Готовим его до fork, чтобы в ребёнке не выделять память.
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        return -1;
    }

    if (pid == 0) {
        // --- ребёнок ---
        // Маска заблокированных сигналов и SIG_IGN переживают exec.
        // Если демон что-то блокирует или игнорирует, терминал не должен
        // это унаследовать — возвращаем всё к умолчаниям.
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int sig : {SIGHUP, SIGTERM, SIGINT, SIGCHLD, SIGPIPE}) {
            signal(sig, SIG_DFL);
        }

        // Своя сессия: окно не связано с демоном и переживёт его остановку
        setsid();

        execvp(argv[0], argv.data());

        // Сюда попадаем, только если exec не удался
        syslog(LOG_ERR, "exec %s: %s", argv[0], std::strerror(errno));
        _exit(127);  // _exit, а не exit: не вызывать деструкторы копии демона
    }

    // --- родитель (демон) ---
    return pid;
}

int Notifier::reapChildren() {
    int reaped = 0;
    int status = 0;
    // -1 — любой ребёнок; WNOHANG — не ждать, если никто не завершился
    while (waitpid(-1, &status, WNOHANG) > 0) {
        ++reaped;
    }
    return reaped;
}
