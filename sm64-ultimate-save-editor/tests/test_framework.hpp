// Minimal dependency-free test framework (registry + CHECK macros).
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace sm64test {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++failureCount();
    std::fprintf(stderr, "    FAIL %s:%d: %s\n", file, line, message.c_str());
}

inline int runAll() {
    int failedTests = 0;
    for (const TestCase& test : registry()) {
        const int before = failureCount();
        test.body();
        const bool ok = failureCount() == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", test.name);
        if (!ok) ++failedTests;
    }
    std::printf("\n%zu test(s), %d failed, %d failed check(s)\n", registry().size(), failedTests, failureCount());
    return failedTests == 0 ? 0 : 1;
}

}  // namespace sm64test

#define SM64_CONCAT_INNER(a, b) a##b
#define SM64_CONCAT(a, b) SM64_CONCAT_INNER(a, b)
#define TEST(name)                                                                                          \
    static void SM64_CONCAT(test_body_, __LINE__)();                                                        \
    static ::sm64test::Registrar SM64_CONCAT(test_reg_, __LINE__)(name, SM64_CONCAT(test_body_, __LINE__)); \
    static void SM64_CONCAT(test_body_, __LINE__)()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) ::sm64test::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (false)

#define CHECK_EQ(a, b)                                                                               \
    do {                                                                                             \
        const auto sm64_a = (a);                                                                     \
        const auto sm64_b = (b);                                                                     \
        if (!(sm64_a == sm64_b))                                                                     \
            ::sm64test::reportFailure(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ") values differ"); \
    } while (false)
