#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

// Показ напоминания в новом окне терминала.
class Notifier {
public:
    // terminal — команда терминала со своим флагом запуска, например {"xterm", "-e"}.
    // Пусто — берём первый найденный в PATH: x-terminal-emulator, затем xterm.
    explicit Notifier(std::vector<std::string> terminal = {});

    // Запускает терминал, который печатает text и ждёт Enter.
    // Возвращает pid запущенного процесса или -1 (причина — в error).
    // Не ждёт закрытия окна: демон продолжает работать.
    pid_t show(const std::string& text, std::string& error) const;

    // Убирает завершившиеся дочерние процессы (окна, которые закрыли),
    // чтобы они не висели зомби. Вызывать регулярно. Возвращает, сколько убрано.
    static int reapChildren();

    // Полный путь к программе из PATH или "", если не найдена.
    // Для имени со '/' проверяет только сам путь.
    static std::string findInPath(const std::string& program);

private:
    std::vector<std::string> terminal_;
};
