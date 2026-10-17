#include <iostream>
#include <string>

#include "Daemon.h"

int main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && (std::string(argv[1]) == "-h" ||
                                   std::string(argv[1]) == "--help"))) {
        std::cerr << "Использование: " << argv[0] << " [конфиг]\n"
                  << "По умолчанию конфиг — reminder.conf в текущем каталоге.\n"
                  << "SIGHUP — перечитать конфиг, SIGTERM — завершить работу.\n";
        return argc > 2 ? 1 : 0;
    }
    const std::string config = (argc == 2) ? argv[1] : "reminder.conf";
    return Daemon::instance().run(config);
}
