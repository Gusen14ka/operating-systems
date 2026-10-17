// Общий main() для всех unit-тестов: запускает тесты из реестра,
// печатает отчёт в консоль и дублирует его в $REMINDER_TEST_LOG_DIR/<бинарник>.log.

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "test.h"

namespace {

const char* const kGreen = "\033[32m";
const char* const kRed = "\033[31m";
const char* const kBold = "\033[1m";
const char* const kDim = "\033[2m";
const char* const kReset = "\033[0m";

// Вывод в консоль (с цветом, если это терминал) и в лог-файл (без цвета)
class Report {
public:
    Report() : color_(isatty(STDOUT_FILENO) && std::getenv("NO_COLOR") == nullptr) {}

    void openLog(const std::string& path) {
        file_.open(path, std::ios::trunc);
        if (!file_) std::cerr << "не удалось открыть лог " << path << '\n';
    }

    void line(const std::string& text, const char* color = nullptr) {
        if (color_ && color) {
            std::cout << color << text << kReset << '\n';
        } else {
            std::cout << text << '\n';
        }
        if (file_) file_ << text << '\n';
    }

    void flush() {
        std::cout.flush();
        if (file_) file_.flush();
    }

private:
    bool color_;
    std::ofstream file_;
};

std::string formatMs(double ms) {
    char buf[32];
    std::snprintf(buf, sizeof buf, ms < 10 ? "%.2f мс" : "%.0f мс", ms);
    return buf;
}

std::string nowStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* slash = std::strrchr(argv[0], '/');
    const std::string binary = slash ? slash + 1 : argv[0];
    const std::string filter = argc > 1 ? argv[1] : "";

    Report out;
    if (const char* dir = std::getenv("REMINDER_TEST_LOG_DIR")) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        out.openLog(std::string(dir) + "/" + binary + ".log");
    }

    out.line("══ " + binary + ": " + test::suiteTitle() + " ══", kBold);
    out.line("запуск " + nowStamp() + (filter.empty() ? "" : ", фильтр: '" + filter + "'"), kDim);

    using clock = std::chrono::steady_clock;
    const auto suiteStart = clock::now();
    int passed = 0;
    int checks = 0;
    std::vector<std::string> failedIds;

    for (const auto& t : test::registry()) {
        if (!filter.empty() && std::string(t.id).find(filter) == std::string::npos) {
            continue;
        }
        test::state() = test::State{};
        out.line("[ RUN  ] " + std::string(t.id) + " — " + t.description);

        const auto start = clock::now();
        try {
            t.fn();
        } catch (const test::AbortTest&) {
            // ASSERT_* уже записал провал
        } catch (const std::exception& e) {
            test::state().failures.push_back({"(вне проверок)", {"исключение: " + std::string(e.what())},
                                              {}});
        } catch (...) {
            test::state().failures.push_back({"(вне проверок)", {"неизвестное исключение"}, {}});
        }
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();

        const auto& st = test::state();
        checks += st.checks;
        const std::string stats = "  (" + std::to_string(st.checks) + " пров., " + formatMs(ms) + ")";
        if (st.failures.empty()) {
            ++passed;
            out.line("[  OK  ] " + std::string(t.id) + stats, kGreen);
            continue;
        }

        failedIds.push_back(t.id);
        out.line("[ FAIL ] " + std::string(t.id) + stats, kRed);
        for (const auto& f : st.failures) {
            out.line("         " + f.location, kRed);
            for (const auto& d : f.details) out.line("           " + d, kRed);
            for (const auto& c : f.context) out.line("           контекст: " + c, kRed);
        }
    }

    const double totalMs =
        std::chrono::duration<double, std::milli>(clock::now() - suiteStart).count();
    const int total = passed + static_cast<int>(failedIds.size());

    out.line("── итог " + binary + " ──", kBold);
    if (total == 0) {
        out.line("нет тестов, подходящих под фильтр '" + filter + "'", kRed);
        out.flush();
        return 1;
    }
    out.line("тестов: " + std::to_string(total) + ", пройдено: " + std::to_string(passed) +
                 ", упало: " + std::to_string(failedIds.size()) + ", проверок: " +
                 std::to_string(checks) + ", время: " + formatMs(totalMs),
             failedIds.empty() ? kGreen : kRed);
    if (!failedIds.empty()) {
        std::string list;
        for (const auto& id : failedIds) list += (list.empty() ? "" : ", ") + id;
        out.line("упали: " + list, kRed);
        out.line("повторить один: ./" + binary + " " + failedIds.front(), kDim);
    }
    out.flush();
    return failedIds.empty() ? 0 : 1;
}
