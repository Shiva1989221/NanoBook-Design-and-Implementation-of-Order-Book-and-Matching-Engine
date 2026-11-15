#pragma once
#include <iostream>

// A tiny test harness, so the tests need no external library.
//
// We deliberately do NOT use <cassert>: CMake's Release build defines NDEBUG,
// which turns assert() into a no-op, so tests would "pass" while checking nothing.
//
// `inline` variables (C++17) let every test file share one definition of these counters.
inline int g_checks   = 0;
inline int g_failures = 0;

// CHECK(condition): records a failure (and prints where) but keeps running the test.
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::cerr << __FILE__ << ":" << __LINE__                             \
                      << ": CHECK failed: " #cond "\n";                          \
        }                                                                        \
    } while (0)

// RUN(test_function): runs one test and prints PASS/FAIL for it.
#define RUN(fn)                                                                  \
    do {                                                                         \
        int failuresBefore = g_failures;                                         \
        fn();                                                                    \
        std::cout << (g_failures == failuresBefore ? "[PASS] " : "[FAIL] ")      \
                  << #fn << "\n";                                                \
    } while (0)

// Call at the end of main(): prints a summary and returns the process exit code
// (0 = all good), which is what CTest uses to decide pass/fail.
inline int finish() {
    std::cout << g_checks << " checks, " << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
