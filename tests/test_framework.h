#pragma once

// ─────────────────────────────────────────────
// Minimal test framework - no external libraries!
// Inspired by Catch2 but simpler
// ─────────────────────────────────────────────

#include <iostream>
#include <string>
#include <functional>
#include <vector>

struct TestCase
{
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &getTests()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int passed_count = 0;
inline int failed_count = 0;

// Register a test
#define TEST(name)                            \
    static void test_##name();                \
    static bool registered_##name = [] { \
        getTests().push_back({#name, test_##name}); \
        return true; }(); \
    static void test_##name()

// Assert macros
#define ASSERT(condition)                                     \
    do                                                        \
    {                                                         \
        if (!(condition))                                     \
        {                                                     \
            throw std::runtime_error(                         \
                std::string("ASSERT failed: ") + #condition + \
                " at line " + std::to_string(__LINE__));      \
        }                                                     \
    } while (0)

#define ASSERT_EQ(a, b)                                                \
    do                                                                 \
    {                                                                  \
        if ((a) != (b))                                                \
        {                                                              \
            throw std::runtime_error(                                  \
                std::string("ASSERT_EQ failed: ") + #a + " != " + #b + \
                " at line " + std::to_string(__LINE__));               \
        }                                                              \
    } while (0)

#define ASSERT_TRUE(x) ASSERT(x)
#define ASSERT_FALSE(x) ASSERT(!(x))
#define ASSERT_NULL(x) ASSERT(!(x).has_value())

// Run all registered tests
inline int runAllTests()
{
    std::cout << "\n=== pearDB Test Suite ===\n\n";

    for (auto &test : getTests())
    {
        std::cout << "[ RUN  ] " << test.name << "\n";
        try
        {
            test.fn();
            std::cout << "[ PASS ] " << test.name << "\n";
            passed_count++;
        }
        catch (const std::exception &e)
        {
            std::cout << "[ FAIL ] " << test.name << "\n";
            std::cout << "         " << e.what() << "\n";
            failed_count++;
        }
    }

    std::cout << "\n=========================\n";
    std::cout << "Passed: " << passed_count << "\n";
    std::cout << "Failed: " << failed_count << "\n";
    std::cout << "Total:  " << (passed_count + failed_count) << "\n";
    std::cout << "=========================\n\n";

    return failed_count > 0 ? 1 : 0;
}