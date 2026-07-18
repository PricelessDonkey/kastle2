#pragma once

// Zero-dependency host-side test harness for the Kastle 2 DSP library.
// No gtest/Catch2 — just enough to register tests and assert on them,
// so the suite builds with a plain host g++/clang++, no pico-sdk needed.

#include <cstdio>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace testkit
{

struct TestCase
{
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar(const std::string &name, std::function<void()> fn)
    {
        registry().push_back({name, std::move(fn)});
    }
};

[[noreturn]] inline void fail(const std::string &message, const char *file, int line)
{
    std::ostringstream os;
    os << message << " (" << file << ":" << line << ")";
    throw std::runtime_error(os.str());
}

template <typename A, typename B>
void assert_eq(const A &a, const B &b, const char *a_expr, const char *b_expr, const char *file, int line)
{
    if (!(a == b))
    {
        std::ostringstream os;
        os << "ASSERT_EQ failed: " << a_expr << " == " << b_expr << " (" << a << " != " << b << ")";
        fail(os.str(), file, line);
    }
}

inline void assert_near(double a, double b, double eps, const char *a_expr, const char *b_expr, const char *file, int line)
{
    double diff = a > b ? a - b : b - a;
    if (diff > eps)
    {
        std::ostringstream os;
        os << "ASSERT_NEAR failed: " << a_expr << " ~= " << b_expr << " (" << a << " vs " << b << ", diff " << diff << " > eps " << eps << ")";
        fail(os.str(), file, line);
    }
}

inline void assert_true(bool cond, const char *expr, const char *file, int line)
{
    if (!cond)
    {
        fail(std::string("ASSERT_TRUE failed: ") + expr, file, line);
    }
}

inline int run_all()
{
    int passed = 0;
    int failed = 0;
    for (auto &t : registry())
    {
        try
        {
            t.fn();
            printf("  PASS  %s\n", t.name.c_str());
            passed++;
        }
        catch (const std::exception &e)
        {
            printf("  FAIL  %s\n        %s\n", t.name.c_str(), e.what());
            failed++;
        }
    }
    printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
    return failed == 0 ? 0 : 1;
}

} // namespace testkit

// Usage: TEST(AdsrEnv_AttackRisesTowardMax) { ... ASSERT_TRUE(...); ... }
#define TEST(name)                                                             \
    static void name();                                                       \
    static testkit::Registrar registrar_##name(#name, name);                  \
    static void name()

#define ASSERT_TRUE(cond) testkit::assert_true((cond), #cond, __FILE__, __LINE__)
#define ASSERT_FALSE(cond) testkit::assert_true(!(cond), "!(" #cond ")", __FILE__, __LINE__)
#define ASSERT_EQ(a, b) testkit::assert_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define ASSERT_NEAR(a, b, eps) testkit::assert_near((a), (b), (eps), #a, #b, __FILE__, __LINE__)
