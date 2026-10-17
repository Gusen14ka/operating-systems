#pragma once

#include <sys/types.h>

#include <chrono>
#include <optional>
#include <string>

// pid-файл: текстовый файл с номером процесса работающего демона.
// Нужен, чтобы новый экземпляр нашёл старый и остановил его.
class PidFile {
public:
    explicit PidFile(std::string path) : path_(std::move(path)) {}

    const std::string& path() const { return path_; }

    // pid из файла; nullopt, если файла нет или в нём не число
    std::optional<pid_t> read() const;

    // Записать pid. Пишем во временный файл и переименовываем —
    // так никто не прочитает наполовину записанный файл.
    bool write(pid_t pid, std::string& error) const;

    // Удалить файл (при штатном выходе демона)
    void remove() const;

    // Жив ли процесс pid и называется ли он expectedName — по /proc/<pid>/stat.
    // Зомби (завершился, но не убран родителем) считается мёртвым.
    // Проверка имени защищает от убийства чужого процесса, если старый
    // демон упал, а его pid ОС успела отдать другой программе.
    static bool isRunning(pid_t pid, const std::string& expectedName);

    // Послать SIGTERM и ждать, пока процесс исчезнет. false — не успел за timeout.
    static bool terminate(pid_t pid, const std::string& expectedName,
                          std::chrono::milliseconds timeout);

private:
    std::string path_;
};
