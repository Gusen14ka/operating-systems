#pragma once
// Мини-фреймворк unit-тестов без внешних зависимостей.
//
//   TEST_SUITE("Event — разбор строки add_event");
//
//   TEST(parse_daily, "флаг -d распознаётся, текст сохраняется целиком") {
//       CONTEXT("строка: " << line);          // попадёт в отчёт, если проверка ниже упадёт
//       ASSERT_TRUE(ev.has_value());          // при провале тест прерывается
//       EXPECT_EQ(ev->text, "Сдать лабу");    // при провале: «ожидалось / получено»
//   }
//
// main() — в runner.cpp, он общий для всех тестовых бинарников.
// Запуск части тестов: ./test_event parse_   (фильтр по подстроке в id)

#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace test {

using TestFn = void (*)();

struct TestInfo {
    const char* id;
    const char* description;
    TestFn fn;
};

inline std::vector<TestInfo>& registry() {
    static std::vector<TestInfo> tests;
    return tests;
}

inline std::string& suiteTitle() {
    static std::string title;
    return title;
}

struct Registrar {
    Registrar(const char* id, const char* description, TestFn fn) {
        registry().push_back({id, description, fn});
    }
};

struct SuiteRegistrar {
    explicit SuiteRegistrar(const char* title) { suiteTitle() = title; }
};

// --- Состояние текущего теста -------------------------------------------------

struct Failure {
    std::string location;              // файл:строка
    std::vector<std::string> details;  // что проверяли, ожидалось, получено
    std::vector<std::string> context;  // активные CONTEXT(...) в момент провала
};

struct State {
    int checks = 0;
    std::vector<Failure> failures;
    std::vector<std::string> context;
};

inline State& state() {
    static State s;
    return s;
}

// Бросается из ASSERT_*, ловится раннером: тест прерывается, остальные идут дальше
struct AbortTest {};

// RAII-контекст: пока объект жив, его текст прикладывается к каждому провалу
class ContextGuard {
public:
    explicit ContextGuard(std::string text) { state().context.push_back(std::move(text)); }
    ~ContextGuard() { state().context.pop_back(); }
    ContextGuard(const ContextGuard&) = delete;
    ContextGuard& operator=(const ContextGuard&) = delete;
};

// --- Печать значений для отчёта ----------------------------------------------

template <typename T, typename = void>
struct HasOstream : std::false_type {};
template <typename T>
struct HasOstream<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

// Строки — в кавычках и с видимыми \n \r \t: иначе "abc\r" и "abc" в отчёте неотличимы
inline std::string show(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '"':  out += "\\\""; break;
            default:   out += c;
        }
    }
    return out + "\"";
}
inline std::string show(const char* s) { return s ? show(std::string(s)) : "nullptr"; }
inline std::string show(bool b) { return b ? "true" : "false"; }

template <typename T>
std::string show(const T& value);

template <typename T>
std::string show(const std::vector<T>& v) {
    std::string out = "{";
    for (std::size_t i = 0; i < v.size(); ++i) {
        out += (i ? ", " : "") + show(v[i]);
    }
    return out + "}";
}

template <typename T>
std::string show(const std::optional<T>& o) {
    return o ? show(*o) : "nullopt";
}

template <typename T>
std::string show(const T& value) {
    if constexpr (HasOstream<T>::value) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else if constexpr (std::is_enum_v<T>) {
        return "enum(" + std::to_string(static_cast<long long>(value)) + ")";
    } else {
        return "<значение не печатается>";
    }
}

// --- Проверки -----------------------------------------------------------------

inline bool fail(const char* file, int line, std::vector<std::string> details) {
    std::string path = file;
    const auto slash = path.rfind('/');
    if (slash != std::string::npos) path = path.substr(slash + 1);
    state().failures.push_back({path + ":" + std::to_string(line), std::move(details), state().context});
    return false;
}

inline bool check(bool ok, const char* expr, const char* file, int line) {
    ++state().checks;
    return ok || fail(file, line, {"проверка:  " + std::string(expr)});
}

template <typename A, typename E>
bool checkEq(const A& actual, const E& expected, const char* actualExpr, const char* expectedExpr,
             const char* file, int line) {
    ++state().checks;
    if (actual == expected) return true;
    return fail(file, line,
                {"проверка:  " + std::string(actualExpr) + " == " + expectedExpr,
                 "ожидалось: " + show(expected), "получено:  " + show(actual)});
}

inline bool checkContains(const std::string& haystack, const std::string& needle,
                          const char* haystackExpr, const char* file, int line) {
    ++state().checks;
    if (haystack.find(needle) != std::string::npos) return true;
    return fail(file, line,
                {"проверка:  " + std::string(haystackExpr) + " содержит " + show(needle),
                 "получено:  " + show(haystack)});
}

}  // namespace test

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST_SUITE(title) static ::test::SuiteRegistrar test_suite_registrar_(title)

#define TEST(id, description)                                                \
    static void test_fn_##id();                                              \
    static ::test::Registrar test_reg_##id(#id, description, &test_fn_##id); \
    static void test_fn_##id()

// CONTEXT("строка: " << line) — живёт до конца текущего блока { }
#define CONTEXT(message)                                                           \
    ::test::ContextGuard TEST_CONCAT(test_context_, __LINE__)(                     \
        [&] { std::ostringstream test_os_; test_os_ << message; return test_os_.str(); }())

#define EXPECT_TRUE(cond) ::test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define EXPECT_FALSE(cond) ::test::check(!(cond), "!(" #cond ")", __FILE__, __LINE__)
#define EXPECT_EQ(actual, expected) \
    ::test::checkEq((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_CONTAINS(haystack, needle) \
    ::test::checkContains((haystack), (needle), #haystack, __FILE__, __LINE__)

#define ASSERT_TRUE(cond) \
    do { if (!EXPECT_TRUE(cond)) throw ::test::AbortTest{}; } while (0)
#define ASSERT_EQ(actual, expected) \
    do { if (!EXPECT_EQ(actual, expected)) throw ::test::AbortTest{}; } while (0)
