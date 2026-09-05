#include <cstdio>

int RunRotationTests();
int RunProjectionTests();
int RunConfigSanitizeTests();
int RunConfigLoadTests();

int main() {
    std::printf("SpecOpsTheLineHeadTracking tests\n");

    int failures = 0;
    failures += RunRotationTests();
    failures += RunProjectionTests();
    failures += RunConfigSanitizeTests();
    failures += RunConfigLoadTests();

    if (failures == 0) {
        std::printf("All tests passed\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", failures);
    return 1;
}
