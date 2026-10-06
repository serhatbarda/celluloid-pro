// tests/license runner:  cel_license_tests [--out DIR] [--filter SUBSTR]
// Images are written to DIR (or $CEL_TEST_OUT) when given.
#include "TestUtil.h"
#include <chrono>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    std::string filter;
    if (const char* e = std::getenv("CEL_TEST_OUT")) celtest::outDir() = e;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) celtest::outDir() = argv[++i];
        else if (!std::strcmp(argv[i], "--filter") && i + 1 < argc) filter = argv[++i];
        else { std::fprintf(stderr, "usage: %s [--out DIR] [--filter SUBSTR]\n", argv[0]); return 2; }
    }
    auto& reg = celtest::Registry::get();
    int run = 0;
    for (auto& t : reg.tests) {
        if (!filter.empty() && t.first.find(filter) == std::string::npos) continue;
        const int before = reg.failures;
        const auto t0 = std::chrono::steady_clock::now();
        std::printf("[ RUN  ] %s\n", t.first.c_str());
        std::fflush(stdout);
        t.second();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[ %s ] %s (%.1f ms)\n", reg.failures == before ? " OK " : "FAIL", t.first.c_str(), ms);
        ++run;
    }
    std::printf("\n%d tests, %d checks, %d failures\n", run, reg.checks, reg.failures);
    return reg.failures == 0 && run > 0 ? 0 : 1;
}
