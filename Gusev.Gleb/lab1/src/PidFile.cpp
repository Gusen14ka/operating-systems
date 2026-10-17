#include "PidFile.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

std::optional<pid_t> PidFile::read() const {
    std::ifstream file(path_);
    long value = 0;
    if (!(file >> value) || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

bool PidFile::write(pid_t pid, std::string& error) const {
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream file(tmp, std::ios::trunc);
        if (!(file << pid << '\n') || !file.flush()) {
            error = "не удалось записать " + tmp + ": " + std::strerror(errno);
            return false;
        }
    }
    // rename в пределах одной файловой системы атомарен: в любой момент
    // по пути path_ лежит либо старый файл целиком, либо новый целиком
    if (std::rename(tmp.c_str(), path_.c_str()) != 0) {
        error = "не удалось переименовать " + tmp + ": " + std::strerror(errno);
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

void PidFile::remove() const {
    std::remove(path_.c_str());
}

bool PidFile::isRunning(pid_t pid, const std::string& expectedName) {
    // /proc/<pid>/stat: "1234 (reminder) S 1 ..." — pid, (имя), состояние, ...
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string stat;
    if (!std::getline(file, stat)) {
        return false;  // каталога /proc/<pid> нет — процесса нет
    }

    // Имя в скобках может само содержать скобки и пробелы,
    // поэтому ищем первую '(' и ПОСЛЕДНЮЮ ')'
    const auto open = stat.find('(');
    const auto close = stat.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close + 2 >= stat.size()) {
        return false;
    }
    const std::string name = stat.substr(open + 1, close - open - 1);
    const char state = stat[close + 2];

    // Ядро хранит имя обрезанным до 15 символов
    return state != 'Z' && name == expectedName.substr(0, 15);
}

bool PidFile::terminate(pid_t pid, const std::string& expectedName,
                        std::chrono::milliseconds timeout) {
    if (!isRunning(pid, expectedName)) {
        return true;
    }
    if (kill(pid, SIGTERM) != 0) {
        return errno == ESRCH;  // ESRCH — процесс исчез сам, это успех
    }

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    while (clock::now() < deadline) {
        // Если это наш дочерний процесс (так бывает в тестах), сами убираем
        // зомби. Для чужого процесса waitpid просто вернёт ошибку ECHILD.
        waitpid(pid, nullptr, WNOHANG);
        if (!isRunning(pid, expectedName)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
