#pragma once
// Мини-фреймворк для unit-тестов без внешних зависимостей.
// CHECK(cond) — при провале печатает файл:строку и условие, тест продолжается.
// В конце main: return checkResult();

#include <cstdio>

inline int& failedChecks() {
    static int n = 0;
    return n;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
            ++failedChecks();                                                \
        }                                                                    \
    } while (0)

inline int checkResult() {
    if (failedChecks() == 0) {
        std::puts("OK");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed\n", failedChecks());
    return 1;
}
