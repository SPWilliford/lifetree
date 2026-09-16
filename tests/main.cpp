#include <cstdio>

#include "tests/test.hpp"

namespace test {

std::vector<Case>& cases() {
    static std::vector<Case> registry;
    return registry;
}

namespace {
int failures = 0;
const char* current = "";
}  // namespace

void fail(const char* file, int line, const std::string& message) {
    ++failures;
    std::printf("  FAIL %s  %s:%d  %s\n", current, file, line, message.c_str());
}

}  // namespace test

int main() {
    for (const auto& c : test::cases()) {
        test::current = c.name;
        c.fn();
    }
    std::printf("%zu tests, %d failures\n", test::cases().size(), test::failures);
    return test::failures == 0 ? 0 : 1;
}
