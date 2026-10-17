#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Notifier.h"
#include "test.h"

TEST_SUITE("Notifier — запуск окна терминала и уборка зомби");

namespace fs = std::filesystem;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Временный каталог, удаляется в конце теста
struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/reminder_unit_notifier_XXXXXX";
        path = mkdtemp(tmpl);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// Временная подмена PATH
class PathGuard {
public:
    explicit PathGuard(const std::string& value) : old_(std::getenv("PATH")) {
        setenv("PATH", value.c_str(), 1);
    }
    ~PathGuard() { setenv("PATH", old_.c_str(), 1); }

private:
    std::string old_;
};

// Заглушка терминала. Получает: -e sh -c <скрипт> sh <текст>
// и пишет "<метка>:<текст>" в out.
void makeFakeTerminal(const std::string& path, const std::string& out, const std::string& tag) {
    std::ofstream(path) << "#!/bin/sh\n"
                           "shift\n"
                           "printf '%s' '" + tag + ":' \"$5\" > '" + out + "'\n";
    chmod(path.c_str(), 0755);
}

// Дождаться завершения показанного «окна»; true — завершилось с кодом 0
bool waitWindow(pid_t pid) {
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

TEST(find_in_path, "findInPath: находит программу в PATH, проверяет путь со '/'") {
    TempDir dir;
    const std::string fake = dir.path + "/fake-terminal";
    makeFakeTerminal(fake, dir.path + "/out", "t");
    EXPECT_TRUE(!Notifier::findInPath("sh").empty());
    EXPECT_EQ(Notifier::findInPath("definitely-no-such-program"), "");
    EXPECT_EQ(Notifier::findInPath(fake), fake);
    EXPECT_EQ(Notifier::findInPath(dir.path + "/missing"), "");
}

TEST(text_passed_verbatim, "текст с кавычками, $(...) и `...` доходит до окна буква в букву") {
    TempDir dir;
    const std::string fake = dir.path + "/fake-terminal";
    const std::string out = dir.path + "/out";
    makeFakeTerminal(fake, out, "t");
    const std::string tricky = "Сдать \"лабу\" it's $(touch " + dir.path + "/pwned) `touch " +
                               dir.path + "/pwned2` ; | & *";

    std::string error;
    const pid_t pid = Notifier({fake, "-e"}).show(tricky, error);
    ASSERT_TRUE(pid > 0);
    EXPECT_TRUE(waitWindow(pid));
    EXPECT_EQ(readFile(out), "t:" + tricky);
    EXPECT_FALSE(fs::exists(dir.path + "/pwned"));   // $(...) не выполнился
    EXPECT_FALSE(fs::exists(dir.path + "/pwned2"));  // `...` не выполнился
}

TEST(configured_terminal_missing, "заданный терминал не найден — ошибка до fork, детей не создано") {
    std::string error;
    EXPECT_EQ(Notifier({"definitely-no-such-terminal", "-e"}).show("x", error), -1);
    EXPECT_CONTAINS(error, "definitely-no-such-terminal");
    EXPECT_EQ(Notifier::reapChildren(), 0);
}

TEST(default_prefers_x_terminal_emulator, "по умолчанию выбирается x-terminal-emulator, если он есть") {
    TempDir dir;
    const std::string out = dir.path + "/out";
    makeFakeTerminal(dir.path + "/x-terminal-emulator", out, "x-terminal-emulator");
    makeFakeTerminal(dir.path + "/xterm", out, "xterm");
    PathGuard path(dir.path + ":/usr/bin:/bin");

    std::string error;
    const pid_t pid = Notifier().show("по умолчанию", error);
    ASSERT_TRUE(pid > 0);
    EXPECT_TRUE(waitWindow(pid));
    EXPECT_EQ(readFile(out), "x-terminal-emulator:по умолчанию");
}

TEST(default_falls_back_to_xterm, "нет x-terminal-emulator — запасной вариант xterm") {
    TempDir dir;
    const std::string out = dir.path + "/out";
    makeFakeTerminal(dir.path + "/xterm", out, "xterm");
    PathGuard path(dir.path);  // только наш каталог: системного x-terminal-emulator не видно

    std::string error;
    const pid_t pid = Notifier().show("запасной", error);
    ASSERT_TRUE(pid > 0);
    EXPECT_TRUE(waitWindow(pid));
    EXPECT_EQ(readFile(out), "xterm:запасной");
}

TEST(no_terminal_at_all, "нет ни одного терминала — понятная ошибка") {
    TempDir dir;
    PathGuard path(dir.path);
    std::string error;
    EXPECT_EQ(Notifier().show("x", error), -1);
    EXPECT_CONTAINS(error, "не найден терминал");
}

TEST(reap_children, "reapChildren убирает всех завершившихся детей и только их") {
    TempDir dir;
    const std::string fake = dir.path + "/fake-terminal";
    makeFakeTerminal(fake, dir.path + "/out", "t");
    Notifier n({fake, "-e"});
    std::string error;
    ASSERT_TRUE(n.show("1", error) > 0);
    ASSERT_TRUE(n.show("2", error) > 0);
    usleep(300 * 1000);
    EXPECT_EQ(Notifier::reapChildren(), 2);
    EXPECT_EQ(Notifier::reapChildren(), 0);
}
