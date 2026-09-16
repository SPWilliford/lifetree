#ifndef TESTS_TEST_HPP
#define TESTS_TEST_HPP

#include <sstream>
#include <string>
#include <vector>

// A minimal test harness: TEST(name) registers a case, CHECK records a
// failure without aborting. tests/main.cpp runs every registered case.
namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& cases();
void fail(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

template <typename T>
concept Streamable = requires(std::ostream& out, const T& value) { out << value; };

template <Streamable T>
void print(std::ostream& out, const T& value) {
    out << value;
}

template <typename T>
void print(std::ostream& out, const std::vector<T>& values) {
    out << "{";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        print(out, values[i]);
    }
    out << "}";
}

template <typename A, typename B>
std::string describe(const char* expr, const A& actual, const B& expected) {
    std::ostringstream out;
    out << expr << "  (got ";
    print(out, actual);
    out << ", expected ";
    print(out, expected);
    out << ")";
    return out.str();
}

}  // namespace test

#define TEST(name)                                              \
    static void name();                                         \
    static const test::Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(expr)                                         \
    do {                                                    \
        if (!(expr)) test::fail(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_EQ(actual, expected)                                                        \
    do {                                                                                  \
        const auto a_ = (actual);                                                         \
        const auto e_ = (expected);                                                       \
        if (!(a_ == e_)) test::fail(__FILE__, __LINE__, test::describe(#actual, a_, e_)); \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                              \
    do {                                                                     \
        const double a_ = (actual);                                          \
        const double e_ = (expected);                                        \
        if (a_ < e_ - (tolerance) || a_ > e_ + (tolerance))                  \
            test::fail(__FILE__, __LINE__, test::describe(#actual, a_, e_)); \
    } while (0)

#endif
