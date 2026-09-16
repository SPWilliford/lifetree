#include <cmath>
#include <memory>
#include <unordered_map>

#include "core/Database.hpp"
#include "core/Priority.hpp"
#include "core/TreeController.hpp"
#include "tests/test.hpp"

namespace {

// A fresh in-memory model, wired the way App wires it.
struct Model {
    std::shared_ptr<Database> db = std::make_shared<Database>(":memory:");
    TreeController life{db, TreeType::LIFE};
    TreeController projects{db, TreeType::PROJECTS};
    Priority priority{db, life, projects};

    Model() {
        db->insert_root(TreeType::LIFE, "Root");
        db->insert_root(TreeType::PROJECTS, "Root");
        life.load();
        projects.load();
        priority.load();
        priority.normalize();
        life.connect_changed([this]() { priority.normalize(); });
    }
};

double sum_of(const Priority& priority, const std::vector<int>& ids) {
    double total = 0.0;
    for (int id : ids) total += priority.weight_of(id);
    return total;
}

bool all_whole(const Priority& priority, const std::vector<int>& ids) {
    for (int id : ids) {
        const double w = priority.weight_of(id);
        if (std::abs(w - std::round(w)) > 1e-9) return false;
    }
    return true;
}

}  // namespace

TEST(siblings_always_sum_to_total_in_whole_numbers) {
    Model m;
    m.life.add(0, "A");
    m.life.add(0, "B");
    m.life.add(0, "C");

    const auto kids = m.life.children_of(0);
    CHECK_NEAR(sum_of(m.priority, kids), Priority::TOTAL, 1e-9);
    CHECK(all_whole(m.priority, kids));
}

TEST(a_sibling_added_to_an_even_set_joins_it_evenly) {
    Model m;
    const int a = m.life.add(0, "A");
    CHECK_EQ(m.priority.weight_of(a), 100.0);

    const int b = m.life.add(0, "B");
    CHECK_EQ(m.priority.weight_of(a), 50.0);
    CHECK_EQ(m.priority.weight_of(b), 50.0);

    const int c = m.life.add(0, "C");
    CHECK_NEAR(m.priority.weight_of(a), 33.0, 1.0);
    CHECK_NEAR(m.priority.weight_of(b), 33.0, 1.0);
    CHECK_NEAR(m.priority.weight_of(c), 33.0, 1.0);
    CHECK_NEAR(sum_of(m.priority, {a, b, c}), Priority::TOTAL, 1e-9);
}

TEST(a_sibling_added_to_a_hand_weighted_set_arrives_at_zero) {
    Model m;
    const int a = m.life.add(0, "A");
    const int b = m.life.add(0, "B");
    m.priority.set_weight(a, 70.0);

    const int c = m.life.add(0, "C");
    CHECK_EQ(m.priority.weight_of(a), 70.0);
    CHECK_EQ(m.priority.weight_of(b), 30.0);
    CHECK_EQ(m.priority.weight_of(c), 0.0);

    // Giving it a weight pulls from the others proportionally.
    m.priority.set_weight(c, 10.0);
    CHECK_EQ(m.priority.weight_of(c), 10.0);
    CHECK_EQ(m.priority.weight_of(a), 63.0);
    CHECK_EQ(m.priority.weight_of(b), 27.0);
}

TEST(normalize_is_idempotent) {
    Model m;
    const int a = m.life.add(0, "A");
    const int b = m.life.add(0, "B");
    m.life.add(0, "C");
    m.priority.set_weight(a, 45.0);
    const double before_a = m.priority.weight_of(a);
    const double before_b = m.priority.weight_of(b);

    m.priority.normalize();
    m.priority.normalize();
    CHECK_EQ(m.priority.weight_of(a), before_a);
    CHECK_EQ(m.priority.weight_of(b), before_b);
}

TEST(set_weight_keeps_the_set_at_total_and_the_edited_value_exact) {
    Model m;
    const int a = m.life.add(0, "A");
    const int b = m.life.add(0, "B");
    const int c = m.life.add(0, "C");

    m.priority.set_weight(a, 50.0);
    CHECK_EQ(m.priority.weight_of(a), 50.0);
    CHECK_NEAR(m.priority.weight_of(b) + m.priority.weight_of(c), 50.0, 1e-9);
    CHECK(all_whole(m.priority, {a, b, c}));
}

TEST(set_weight_scales_siblings_proportionally) {
    Model m;
    const int a = m.life.add(0, "A");
    const int b = m.life.add(0, "B");
    const int c = m.life.add(0, "C");
    m.priority.set_weight(b, 60.0);  // a and c share 40: 20/20
    m.priority.set_weight(a, 10.0);  // b and c share 90 in a 60:20 ratio

    CHECK_EQ(m.priority.weight_of(a), 10.0);
    CHECK_NEAR(m.priority.weight_of(b), 67.0, 1.0);
    CHECK_NEAR(m.priority.weight_of(c), 23.0, 1.0);
    CHECK_NEAR(sum_of(m.priority, {a, b, c}), Priority::TOTAL, 1e-9);
}

TEST(only_child_always_holds_the_whole_parent) {
    Model m;
    const int a = m.life.add(0, "A");
    m.priority.set_weight(a, 30.0);
    CHECK_EQ(m.priority.weight_of(a), Priority::TOTAL);
}

TEST(removing_a_sibling_hands_its_share_to_the_survivors) {
    Model m;
    const int a = m.life.add(0, "A");
    const int b = m.life.add(0, "B");
    m.priority.set_weight(a, 70.0);
    m.life.remove(b);
    CHECK_EQ(m.priority.weight_of(a), Priority::TOTAL);
}

TEST(priorities_cascade_and_leaves_sum_to_total) {
    Model m;
    const int a = m.life.add(0, "A");
    const int b = m.life.add(0, "B");
    const int a1 = m.life.add(a, "A1");
    const int a2 = m.life.add(a, "A2");
    m.priority.set_weight(a, 40.0);
    m.priority.set_weight(a1, 25.0);

    const auto p = m.priority.priorities();
    CHECK_NEAR(p.at(a), 40.0, 1e-9);
    CHECK_NEAR(p.at(b), 60.0, 1e-9);
    CHECK_NEAR(p.at(a1), 10.0, 1e-9);
    CHECK_NEAR(p.at(a2), 30.0, 1e-9);
    CHECK_NEAR(p.at(a1) + p.at(a2) + p.at(b), Priority::TOTAL, 1e-9);
}

TEST(links_split_a_goal_between_its_projects) {
    Model m;
    const int goal = m.life.add(0, "Goal");
    const int p1 = m.projects.add(0, "P1");
    const int p2 = m.projects.add(0, "P2");

    m.priority.set_link(p1, goal);
    CHECK_EQ(m.priority.goal_share(p1, goal), Priority::TOTAL);
    CHECK_EQ(m.priority.project_share(p1, goal), Priority::TOTAL);

    m.priority.set_link(p2, goal);
    CHECK_EQ(m.priority.goal_share(p1, goal), 50.0);
    CHECK_EQ(m.priority.goal_share(p2, goal), 50.0);

    const auto pp = m.priority.project_priorities();
    CHECK_NEAR(pp.at(p1), 50.0, 1e-9);
    CHECK_NEAR(pp.at(p2), 50.0, 1e-9);
}

TEST(set_goal_share_rebalances_only_that_axis) {
    Model m;
    const int g1 = m.life.add(0, "G1");
    const int g2 = m.life.add(0, "G2");
    const int p1 = m.projects.add(0, "P1");
    const int p2 = m.projects.add(0, "P2");
    m.priority.set_link(p1, g1);
    m.priority.set_link(p1, g2);
    m.priority.set_link(p2, g1);

    m.priority.set_goal_share(p1, g1, 80.0);
    CHECK_EQ(m.priority.goal_share(p1, g1), 80.0);
    CHECK_EQ(m.priority.goal_share(p2, g1), 20.0);
    // p1's project axis was not touched by a goal-axis edit.
    CHECK_EQ(m.priority.project_share(p1, g1), 50.0);
    CHECK_EQ(m.priority.project_share(p1, g2), 50.0);
}

TEST(unlinked_project_ranks_at_zero_and_unserved_leaf_is_reported) {
    Model m;
    const int served = m.life.add(0, "Served");
    const int unserved = m.life.add(0, "Unserved");
    const int p1 = m.projects.add(0, "P1");
    const int p2 = m.projects.add(0, "P2");
    m.priority.set_link(p1, served);

    const auto pp = m.priority.project_priorities();
    CHECK_NEAR(pp.at(p1), 50.0, 1e-9);
    CHECK_EQ(pp.at(p2), 0.0);

    const auto gaps = m.priority.unserved_leaves();
    CHECK_EQ(gaps.size(), 1u);
    CHECK_EQ(gaps.front().first, unserved);
}

TEST(link_is_dropped_when_its_leaf_gains_children) {
    Model m;
    const int goal = m.life.add(0, "Goal");
    const int p1 = m.projects.add(0, "P1");
    m.priority.set_link(p1, goal);
    CHECK(m.priority.has_link(p1, goal));

    m.life.add(goal, "Sub-goal");  // normalize() runs on the tree signal
    CHECK(!m.priority.has_link(p1, goal));
    CHECK(m.priority.leaves_for(p1).empty());
}

TEST(link_is_dropped_when_its_project_is_demoted) {
    Model m;
    const int goal = m.life.add(0, "Goal");
    const int p1 = m.projects.add(0, "P1");
    const int p2 = m.projects.add(0, "P2");
    m.priority.set_link(p1, goal);
    m.priority.set_link(p2, goal);
    m.projects.remove(p2);
    m.priority.normalize();

    CHECK_EQ(m.priority.goal_share(p1, goal), Priority::TOTAL);
    CHECK_EQ(m.priority.projects_for(goal).size(), 1u);
}
