#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <fstream>

#include "PidFile.h"
#include "test.h"

TEST_SUITE("PidFile — pid-файл, проверка процесса через /proc, остановка");

using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

const std::string kSelf = "test_pidfile";  // имя этого бинарника в /proc/<pid>/stat

std::string tempPidPath() {
    static int n = 0;
    return "/tmp/reminder_unit_" + std::to_string(getpid()) + "_" + std::to_string(++n) + ".pid";
}

bool exists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }

// Дочерний процесс, который гарантированно убирается в конце теста,
// даже если тест прервался на ASSERT
class Child {
public:
    // ignoreTerm — игнорировать SIGTERM (имитация «зависшего» демона)
    explicit Child(bool ignoreTerm = false) {
        pid_ = fork();
        if (pid_ == 0) {
            if (ignoreTerm) signal(SIGTERM, SIG_IGN);
            for (;;) pause();  // спать до сигнала
        }
        usleep(50 * 1000);  // дать ребёнку дойти до pause()/SIG_IGN
    }
    ~Child() {
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }
    pid_t pid() const { return pid_; }

private:
    pid_t pid_;
};

}  // namespace

TEST(read_missing_file, "нет pid-файла — read() возвращает nullopt") {
    EXPECT_EQ(PidFile(tempPidPath()).read(), std::optional<pid_t>{});
}

TEST(write_then_read, "write() + read() возвращают тот же pid, временный файл не остаётся") {
    const std::string path = tempPidPath();
    PidFile pf(path);
    std::string error;
    ASSERT_TRUE(pf.write(12345, error));
    EXPECT_EQ(pf.read(), std::optional<pid_t>{12345});
    EXPECT_FALSE(exists(path + ".tmp"));
    pf.remove();
    EXPECT_FALSE(exists(path));
}

TEST(read_rejects_garbage, "мусор в pid-файле — nullopt, а не случайный pid") {
    const std::string path = tempPidPath();
    for (const char* content : {"abc\n", "-5\n", "0\n", "\n", ""}) {
        CONTEXT("содержимое: \"" << content << "\"");
        std::ofstream(path, std::ios::trunc) << content;
        EXPECT_EQ(PidFile(path).read(), std::optional<pid_t>{});
    }
    std::remove(path.c_str());
}

TEST(write_to_missing_dir_fails, "запись в несуществующий каталог — false и причина с путём") {
    std::string error;
    EXPECT_FALSE(PidFile("/nonexistent/x.pid").write(1, error));
    EXPECT_CONTAINS(error, "/nonexistent/x.pid");
}

TEST(is_running_checks_name, "isRunning: свой pid с верным именем — да; с чужим именем — нет") {
    EXPECT_TRUE(PidFile::isRunning(getpid(), kSelf));
    EXPECT_FALSE(PidFile::isRunning(getpid(), "reminder"));
}

TEST(is_running_missing_pid, "isRunning для несуществующего pid — false") {
    EXPECT_FALSE(PidFile::isRunning(999999999, kSelf));
}

TEST(is_running_zombie, "зомби (завершился, но не убран родителем) считается мёртвым") {
    const pid_t pid = fork();
    if (pid == 0) _exit(0);
    usleep(100 * 1000);  // ребёнок завершился, waitpid не вызван — он зомби
    EXPECT_FALSE(PidFile::isRunning(pid, kSelf));
    waitpid(pid, nullptr, 0);
}

TEST(terminate_stops_process, "terminate: SIGTERM доставлен, процесс завершился до таймаута") {
    Child child;
    ASSERT_TRUE(PidFile::isRunning(child.pid(), kSelf));
    const auto start = steady_clock::now();
    EXPECT_TRUE(PidFile::terminate(child.pid(), kSelf, 2s));
    EXPECT_FALSE(PidFile::isRunning(child.pid(), kSelf));
    EXPECT_TRUE(steady_clock::now() - start < 1s);
}

TEST(terminate_ignores_foreign_name, "terminate не трогает процесс с другим именем (pid переиспользован)") {
    Child child;
    EXPECT_TRUE(PidFile::terminate(child.pid(), "reminder", 2s));  // «нечего останавливать»
    EXPECT_TRUE(PidFile::isRunning(child.pid(), kSelf));           // а процесс жив
}

TEST(terminate_times_out, "процесс игнорирует SIGTERM — terminate возвращает false по таймауту") {
    Child child(/*ignoreTerm=*/true);
    const auto start = steady_clock::now();
    EXPECT_FALSE(PidFile::terminate(child.pid(), kSelf, 300ms));
    const auto waited = duration_cast<milliseconds>(steady_clock::now() - start).count();
    CONTEXT("ждали " << waited << " мс");
    EXPECT_TRUE(waited >= 300);
    EXPECT_TRUE(PidFile::isRunning(child.pid(), kSelf));
}
