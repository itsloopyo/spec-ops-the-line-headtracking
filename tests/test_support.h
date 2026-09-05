#pragma once

#include <cmath>
#include <cstdio>

// Minimal runner, matching the style cameraunlock-core's own tests use: each file
// exposes one RunXxxTests() returning a failure count.
inline int ReportCheck(bool ok, const char* expr, const char* file, int line) {
    if (ok) {
        return 0;
    }
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
    return 1;
}

inline bool NearlyEqual(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

#define CHECK(expr) failures += ReportCheck((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) \
    failures += ReportCheck(NearlyEqual((a), (b), (tol)), #a " ~= " #b, __FILE__, __LINE__)
