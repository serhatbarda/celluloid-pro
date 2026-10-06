// Tiny self-contained test harness for tests/license (no external framework).
#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace celtest {

struct Registry {
    std::vector<std::pair<std::string, std::function<void()>>> tests;
    int failures = 0;
    int checks = 0;
    static Registry& get() { static Registry r; return r; }
};

struct Reg {
    Reg(const char* name, std::function<void()> fn) { Registry::get().tests.emplace_back(name, std::move(fn)); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++Registry::get().failures;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, what.c_str());
}

// Output directory for images (set by main from argv/env).
inline std::string& outDir() { static std::string d; return d; }

} // namespace celtest

#define CEL_TEST(name) \
    static void name(); \
    static celtest::Reg name##_reg(#name, name); \
    static void name()

#define CHECK(cond) do { ++celtest::Registry::get().checks; \
    if (!(cond)) celtest::fail(__FILE__, __LINE__, #cond); } while (0)
#define CHECK_MSG(cond, msg) do { ++celtest::Registry::get().checks; \
    if (!(cond)) celtest::fail(__FILE__, __LINE__, std::string(#cond) + " -- " + (msg)); } while (0)
