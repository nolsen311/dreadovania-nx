// SPDX-License-Identifier: GPL-3.0-or-later
// A minimal test harness for the host build: no third-party test framework.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Test {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Test>& registry() {
    static std::vector<Test> tests;
    return tests;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

inline void fail(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    ++failures();
}

inline int run_all() {
    for (const Test& test : registry()) {
        const int before = failures();
        test.body();
        std::printf("%s %s\n", failures() == before ? "ok  " : "FAIL", test.name);
    }
    std::printf("%zu tests, %d failed checks\n", registry().size(), failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK_CONCAT_(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_(a, b)
#define TEST(name)                                                             \
    static void name();                                                        \
    static ::check::Register CHECK_CONCAT(register_, name)(#name, name);      \
    static void name()
#define CHECK(expr) ((expr) ? (void)0 : ::check::fail(__FILE__, __LINE__, #expr))
