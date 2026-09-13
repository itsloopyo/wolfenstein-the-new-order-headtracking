// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once

// The check harness every C++ suite in this repo runs on, and the counterpart
// of tests/TestSupport.psm1 for the PowerShell ones. Each suite previously
// carried its own copy of the counter, the two check functions and the summary
// block, which is four places for a failure to stop being counted.
//
// No GoogleTest: the suites are plain executables run by ctest and by their own
// pixi tasks, and a message plus a count is the whole reporting need.

#include <cmath>
#include <cstdio>

namespace checks {

inline int g_failures = 0;
inline bool g_reportPasses = false;

// Prints a line for the checks that pass as well as the ones that do not. The
// tracker recovery suite turns this on because its output IS the measurement
// report - the rest stay quiet so a failure is the only thing on screen.
inline void ReportPasses(bool on) { g_reportPasses = on; }

inline void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++g_failures;
        return;
    }
    if (g_reportPasses) std::printf("  [PASS] %s\n", what);
}

inline void CheckNear(float actual, float expected, float tolerance, const char* what) {
    if (!(std::fabs(actual - expected) <= tolerance)) {
        std::printf("  [FAIL] %s (got %.6f, expected %.6f)\n", what, actual, expected);
        ++g_failures;
        return;
    }
    if (g_reportPasses) std::printf("  [PASS] %s\n", what);
}

// Prints the run summary and answers with the process exit code.
inline int Summarize(const char* suite) {
    if (g_failures == 0) {
        std::printf("%s: all checks passed\n", suite);
        return 0;
    }
    std::printf("%s: %d check(s) failed\n", suite, g_failures);
    return 1;
}

}  // namespace checks
