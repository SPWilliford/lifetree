#include <algorithm>
#include <vector>

#include "core/Scheduler.hpp"
#include "tests/test.hpp"

namespace {
std::vector<scheduler::Candidate> tasks(int project, int first_task, int count) {
    std::vector<scheduler::Candidate> out;
    for (int i = 0; i < count; ++i) out.push_back({first_task + i, project});
    return out;
}
}  // namespace

TEST(interleave_returns_every_candidate_once) {
    auto candidates = tasks(1, 100, 3);
    auto more = tasks(2, 200, 4);
    candidates.insert(candidates.end(), more.begin(), more.end());

    auto ordered = scheduler::interleave(candidates, {{1, 50.0}, {2, 50.0}});
    CHECK_EQ(ordered.size(), 7u);
    std::sort(ordered.begin(), ordered.end());
    CHECK_EQ(ordered, (std::vector<int>{100, 101, 102, 200, 201, 202, 203}));
}

TEST(interleave_alternates_equal_shares) {
    auto candidates = tasks(1, 100, 2);
    auto more = tasks(2, 200, 2);
    candidates.insert(candidates.end(), more.begin(), more.end());

    auto ordered = scheduler::interleave(candidates, {{1, 50.0}, {2, 50.0}});
    CHECK_EQ(ordered, (std::vector<int>{100, 200, 101, 201}));
}

TEST(interleave_weights_by_share) {
    auto candidates = tasks(1, 100, 4);
    auto more = tasks(2, 200, 4);
    candidates.insert(candidates.end(), more.begin(), more.end());

    // 2:1 — project 1 gets two turns for each of project 2's.
    auto ordered = scheduler::interleave(candidates, {{1, 66.0}, {2, 33.0}});
    int ones_in_first_six = 0;
    for (int i = 0; i < 6; ++i) ones_in_first_six += (ordered[i] < 200);
    CHECK_EQ(ones_in_first_six, 4);
}

TEST(interleave_unlinked_project_goes_last_not_missing) {
    auto candidates = tasks(1, 100, 2);
    auto more = tasks(9, 900, 2);
    candidates.insert(candidates.end(), more.begin(), more.end());

    auto ordered = scheduler::interleave(candidates, {{1, 100.0}});
    CHECK_EQ(ordered.size(), 4u);
    CHECK_EQ(ordered[0], 100);
    CHECK_EQ(ordered[1], 101);
}

TEST(interleave_preserves_order_within_project) {
    auto ordered = scheduler::interleave(tasks(1, 5, 3), {{1, 100.0}});
    CHECK_EQ(ordered, (std::vector<int>{5, 6, 7}));
}
