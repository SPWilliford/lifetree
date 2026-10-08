#include <algorithm>
#include <cstddef>
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

namespace {
std::vector<int> rows_of(const std::vector<int>& ordered, int task_id) {
    std::vector<int> rows;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (ordered[i] == task_id) rows.push_back(static_cast<int>(i));
    }
    return rows;
}
}  // namespace

TEST(repeated_task_keeps_a_rotation_between_appearances) {
    // Water x3 in a top-priority project, two other projects: gap of 3.
    std::vector<scheduler::Candidate> candidates{{7, 1}, {7, 1}, {7, 1}};
    auto more = tasks(2, 200, 6);
    candidates.insert(candidates.end(), more.begin(), more.end());
    more = tasks(3, 300, 6);
    candidates.insert(candidates.end(), more.begin(), more.end());

    const auto ordered = scheduler::interleave(candidates, {{1, 80.0}, {2, 10.0}, {3, 10.0}});
    CHECK_EQ(ordered.size(), 15u);
    const auto water = rows_of(ordered, 7);
    CHECK_EQ(water.size(), 3u);
    if (water.size() != 3) return;
    CHECK_EQ(water[0], 0);
    CHECK(water[1] - water[0] >= 3);
    CHECK(water[2] - water[1] >= 3);
}

TEST(a_just_finished_repeat_starts_a_rotation_down) {
    std::vector<scheduler::Candidate> candidates{{7, 1, 0}, {7, 1, 0}};
    auto more = tasks(2, 200, 4);
    candidates.insert(candidates.end(), more.begin(), more.end());
    more = tasks(3, 300, 4);
    candidates.insert(candidates.end(), more.begin(), more.end());

    // Gap 3; just finished counts as the row above the top.
    const auto ordered = scheduler::interleave(candidates, {{1, 80.0}, {2, 10.0}, {3, 10.0}});
    CHECK_EQ(rows_of(ordered, 7).front(), 2);
}

TEST(a_repeat_finished_a_while_ago_is_not_held_back) {
    std::vector<scheduler::Candidate> candidates{{7, 1, 5}, {7, 1, 5}};
    auto more = tasks(2, 200, 4);
    candidates.insert(candidates.end(), more.begin(), more.end());

    const auto ordered = scheduler::interleave(candidates, {{1, 80.0}, {2, 20.0}});
    CHECK_EQ(rows_of(ordered, 7).front(), 0);
}

TEST(a_held_back_repeat_does_not_hold_up_its_project) {
    // One project: gap is the floor of 2, so the repeat alternates.
    const std::vector<scheduler::Candidate> candidates{{7, 1}, {7, 1}, {8, 1}, {9, 1}};
    CHECK_EQ(scheduler::interleave(candidates, {{1, 100.0}}), (std::vector<int>{7, 8, 7, 9}));
}

TEST(repeats_with_nothing_to_space_them_still_all_appear) {
    const std::vector<scheduler::Candidate> candidates{{7, 1}, {7, 1}, {7, 1}};
    CHECK_EQ(scheduler::interleave(candidates, {{1, 100.0}}), (std::vector<int>{7, 7, 7}));
}
