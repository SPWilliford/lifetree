#include <string>
#include <vector>

#include "core/TreeController.hpp"
#include "tests/test.hpp"

using Titles = std::vector<std::string>;

TEST(expand_plain_title_is_itself) {
    CHECK_EQ(TreeController::expand("Read chapter 3").size(), 1u);
    CHECK_EQ(TreeController::expand("Read chapter 3").front(), "Read chapter 3");
}

TEST(expand_hyphen_range) {
    const Titles out = TreeController::expand("Problem {1-3}");
    CHECK_EQ(out, (Titles{"Problem 1", "Problem 2", "Problem 3"}));
}

TEST(expand_dotdot_range_with_suffix) {
    const Titles out = TreeController::expand("Set {1..2} done");
    CHECK_EQ(out, (Titles{"Set 1 done", "Set 2 done"}));
}

TEST(expand_counts_down) {
    CHECK_EQ(TreeController::expand("{3..1}"), (Titles{"3", "2", "1"}));
}

TEST(expand_unparseable_stays_literal) {
    for (const char* title : {"{1..}", "{a..z}", "{..3}", "{1", "1}", "x{1-2}y{3-4}", "{1-2-3}"}) {
        const Titles out = TreeController::expand(title);
        CHECK_EQ(out.size(), 1u);
        CHECK_EQ(out.front(), title);
    }
}

TEST(expand_over_cap_stays_literal) {
    const Titles out = TreeController::expand("{1-100000}");
    CHECK_EQ(out.size(), 1u);
    CHECK_EQ(TreeController::expand("{1-200}").size(), 200u);
}
