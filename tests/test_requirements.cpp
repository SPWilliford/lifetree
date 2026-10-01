#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/Database.hpp"
#include "core/Requirements.hpp"
#include "core/TreeController.hpp"
#include "tests/test.hpp"

namespace {

// Wired the way App wires it.
struct Model {
    std::shared_ptr<Database> db;
    TreeController life;
    TreeController projects;
    Requirements requirements;

    explicit Model(const std::string& path = ":memory:")
        : db(std::make_shared<Database>(path)),
          life(db, TreeType::LIFE),
          projects(db, TreeType::PROJECTS),
          requirements(db, life, projects) {
        db->insert_root(TreeType::LIFE, "Root");
        db->insert_root(TreeType::PROJECTS, "Root");
        life.load();
        projects.load();
        requirements.load();
        requirements.normalize();
        life.connect_changed([this]() { requirements.normalize(); });
        projects.connect_changed([this]() { requirements.normalize(); });
    }
};

std::vector<int> requirements_on(Database& db, int leaf_id) {
    std::vector<int> out;
    for (const auto& link : db.load_leaf_requirement_links()) {
        if (link.leaf_id == leaf_id) out.push_back(link.requirement_id);
    }
    return out;
}

std::vector<int> all_requirements(Database& db) {
    std::vector<int> out;
    for (const auto& row : db.load_requirements()) out.push_back(row.id);
    return out;
}

}  // namespace

TEST(a_leafs_requirements_move_to_its_first_child_in_order) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int sleep = m.db->insert_requirement("Adequate sleep");
    const int exercise = m.db->insert_requirement("Regular exercise");
    m.db->set_leaf_requirement_link(health, sleep, 0);
    m.db->set_leaf_requirement_link(health, exercise, 1);

    const int body = m.life.add(health, "Body");

    CHECK_EQ(requirements_on(*m.db, health), std::vector<int>{});
    CHECK_EQ(requirements_on(*m.db, body), (std::vector<int>{sleep, exercise}));
}

TEST(later_children_start_with_no_requirements) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int sleep = m.db->insert_requirement("Adequate sleep");
    m.db->set_leaf_requirement_link(health, sleep, 0);

    const int body = m.life.add(health, "Body");
    const int mind = m.life.add(health, "Mind");

    CHECK_EQ(requirements_on(*m.db, body), std::vector<int>{sleep});
    CHECK_EQ(requirements_on(*m.db, mind), std::vector<int>{});
}

TEST(deleting_a_leaf_deletes_requirements_only_it_used) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int project = m.projects.add(0, "Running");
    const int shared = m.db->insert_requirement("Adequate sleep");
    const int solo = m.db->insert_requirement("Regular exercise");
    m.db->set_leaf_requirement_link(health, shared, 0);
    m.db->set_leaf_requirement_link(career, shared, 0);
    m.db->set_leaf_requirement_link(health, solo, 1);
    m.db->set_requirement_project_link(solo, project);

    m.life.remove(health);

    CHECK_EQ(all_requirements(*m.db), std::vector<int>{shared});
    CHECK_EQ(m.db->load_requirement_project_links().size(), 0u);
}

TEST(deleting_a_branch_deletes_its_leaves_requirements) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int body = m.life.add(health, "Body");
    const int sleep = m.db->insert_requirement("Adequate sleep");
    m.db->set_leaf_requirement_link(body, sleep, 0);

    m.life.remove(health);

    CHECK_EQ(all_requirements(*m.db), std::vector<int>{});
}

TEST(startup_repairs_requirements_left_on_a_branch) {
    const std::string path = "/tmp/lifetree_requirements_test.db";
    std::remove(path.c_str());

    int body = -1;
    int sleep = -1;
    {
        Database db(path);
        db.insert_root(TreeType::LIFE, "Root");
        const int health = db.insert(TreeType::LIFE, 0, 0, "Health");
        body = db.insert(TreeType::LIFE, health, 0, "Body");
        sleep = db.insert_requirement("Adequate sleep");
        db.insert_requirement("Unused");
        db.set_leaf_requirement_link(health, sleep, 0);
    }

    {
        Model m(path);
        CHECK_EQ(requirements_on(*m.db, body), std::vector<int>{sleep});
        CHECK_EQ(all_requirements(*m.db), std::vector<int>{sleep});
    }
    std::remove(path.c_str());
}

TEST(add_appends_and_shares_a_matching_title) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");

    const int sleep = m.requirements.add(health, "Adequate sleep");
    const int exercise = m.requirements.add(health, "Regular exercise");
    CHECK_EQ(m.requirements.requirements_of(health), (std::vector<int>{sleep, exercise}));

    CHECK_EQ(m.requirements.add(career, "  adequate SLEEP "), sleep);
    CHECK_EQ(m.requirements.requirements_of(career), std::vector<int>{sleep});
    CHECK_EQ(all_requirements(*m.db).size(), 2u);

    CHECK_EQ(m.requirements.add(health, "Adequate sleep"), sleep);
    CHECK_EQ(m.requirements.requirements_of(health).size(), 2u);
}

TEST(add_refuses_branches_the_root_and_blank_titles) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int body = m.life.add(health, "Body");

    CHECK_EQ(m.requirements.add(health, "Adequate sleep"), -1);
    CHECK_EQ(m.requirements.add(0, "Adequate sleep"), -1);
    CHECK_EQ(m.requirements.add(body, "   "), -1);
    CHECK_EQ(all_requirements(*m.db).size(), 0u);
}

TEST(detach_deletes_only_from_the_last_leaf) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    m.requirements.attach(career, sleep);

    m.requirements.detach(health, sleep);
    CHECK_EQ(all_requirements(*m.db), std::vector<int>{sleep});
    CHECK_EQ(m.requirements.requirements_of(health), std::vector<int>{});

    m.requirements.detach(career, sleep);
    CHECK_EQ(all_requirements(*m.db), std::vector<int>{});
    CHECK_EQ(m.requirements.title(sleep), std::string());
}

TEST(move_reorders_within_one_leaf) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int a = m.requirements.add(health, "A");
    const int b = m.requirements.add(health, "B");
    const int c = m.requirements.add(health, "C");

    m.requirements.move(health, c, 0);
    CHECK_EQ(m.requirements.requirements_of(health), (std::vector<int>{c, a, b}));

    m.requirements.move(health, c, 9);
    CHECK_EQ(m.requirements.requirements_of(health), (std::vector<int>{a, b, c}));

    CHECK_EQ(requirements_on(*m.db, health), (std::vector<int>{a, b, c}));
}

TEST(rename_shows_everywhere_and_ignores_blank) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int sleep = m.requirements.add(health, "Sleep");
    m.requirements.attach(career, sleep);

    m.requirements.rename(sleep, "Adequate sleep");
    m.requirements.rename(sleep, "  ");
    CHECK_EQ(m.requirements.title(sleep), std::string("Adequate sleep"));
    CHECK_EQ(m.requirements.requirements_of(career), std::vector<int>{sleep});
}

TEST(suggestions_skip_the_leafs_own_and_put_prefix_matches_first) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int deep = m.requirements.add(career, "Deep sleep");
    const int sleep = m.requirements.add(career, "Sleep schedule");
    m.requirements.add(health, "Sleep in");
    m.requirements.add(career, "Exercise");

    CHECK_EQ(m.requirements.suggestions(health, "SLEEP"), (std::vector<int>{sleep, deep}));
    CHECK_EQ(m.requirements.suggestions(health, " "), std::vector<int>{});
}

TEST(only_real_changes_are_signalled) {
    Model m;
    const int health = m.life.add(0, "Health");
    int signals = 0;
    m.requirements.connect_changed([&signals]() { ++signals; });

    m.life.add(0, "Career");
    CHECK_EQ(signals, 0);

    m.requirements.add(health, "Adequate sleep");
    CHECK_EQ(signals, 1);

    m.life.add(health, "Body");
    CHECK_EQ(signals, 2);
}

TEST(ranking_splits_a_leaf_by_position_and_sums_shared_requirements) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    const int exercise = m.requirements.add(health, "Regular exercise");
    const int diet = m.requirements.add(health, "Healthy diet");
    const int deep = m.requirements.add(career, "Deep work");
    m.requirements.attach(career, sleep);

    const auto ranked = m.requirements.ranked({{health, 60.0}, {career, 40.0}});
    CHECK_EQ(ranked.size(), 4u);
    if (ranked.size() != 4) return;

    // Health: 30 / 20 / 10. Career: deep work 40 * 2/3, sleep 40 * 1/3.
    CHECK_EQ(ranked[0].requirement_id, sleep);
    CHECK_NEAR(ranked[0].share, 30.0 + 40.0 / 3.0, 1e-9);
    CHECK_EQ(ranked[0].leaf_id, health);
    CHECK_EQ(ranked[1].requirement_id, deep);
    CHECK_NEAR(ranked[1].share, 80.0 / 3.0, 1e-9);
    CHECK_EQ(ranked[2].requirement_id, exercise);
    CHECK_NEAR(ranked[2].share, 20.0, 1e-9);
    CHECK_EQ(ranked[3].requirement_id, diet);
    CHECK_NEAR(ranked[3].share, 10.0, 1e-9);

    double total = 0.0;
    for (const auto& line : ranked) total += line.share;
    CHECK_NEAR(total, 100.0, 1e-9);
}

TEST(a_leaf_with_no_requirements_keeps_its_share_in_the_ranking) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int sleep = m.requirements.add(health, "Adequate sleep");

    const auto ranked = m.requirements.ranked({{health, 30.0}, {career, 70.0}});
    CHECK_EQ(ranked.size(), 2u);
    if (ranked.size() != 2) return;
    CHECK_EQ(ranked[0].requirement_id, -1);
    CHECK_EQ(ranked[0].leaf_id, career);
    CHECK_NEAR(ranked[0].share, 70.0, 1e-9);
    CHECK_EQ(ranked[1].requirement_id, sleep);
}

TEST(projects_link_at_the_root_only) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    const int routine = m.projects.add(0, "Sleep Routine");
    const int task = m.projects.add(routine, "Buy blackout curtains");

    m.requirements.link_project(sleep, task);
    CHECK(!m.requirements.is_served(sleep));

    m.requirements.link_project(sleep, routine);
    CHECK_EQ(m.requirements.projects_of(sleep), std::vector<int>{routine});

    m.requirements.unlink_project(sleep, routine);
    CHECK(!m.requirements.is_served(sleep));
}

TEST(deleting_a_project_unlinks_it) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    const int routine = m.projects.add(0, "Sleep Routine");
    m.requirements.link_project(sleep, routine);

    m.projects.remove(routine);

    CHECK(!m.requirements.is_served(sleep));
    CHECK_EQ(m.db->load_requirement_project_links().size(), 0u);
}

TEST(a_link_to_a_project_below_the_root_is_dropped) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    const int routine = m.projects.add(0, "Sleep Routine");
    const int task = m.projects.add(routine, "Buy blackout curtains");
    m.db->set_requirement_project_link(sleep, task);

    m.requirements.normalize();

    CHECK_EQ(m.db->load_requirement_project_links().size(), 0u);
}

TEST(project_priorities_split_a_requirement_evenly_among_its_projects) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    const int exercise = m.requirements.add(health, "Regular exercise");
    const int routine = m.projects.add(0, "Sleep Routine");
    const int running = m.projects.add(0, "Running");
    const int idle = m.projects.add(0, "Unlinked");
    m.requirements.link_project(sleep, routine);
    m.requirements.link_project(sleep, running);
    m.requirements.link_project(exercise, running);

    // Health 90: sleep 60, exercise 30.
    const auto pp = m.requirements.project_priorities({{health, 90.0}});
    CHECK_NEAR(pp.at(routine), 30.0, 1e-9);
    CHECK_NEAR(pp.at(running), 60.0, 1e-9);
    CHECK_EQ(pp.at(idle), 0.0);
}

TEST(time_split_goes_evenly_across_requirements_then_by_leaf_contribution) {
    Model m;
    const int health = m.life.add(0, "Health");
    const int career = m.life.add(0, "Career");
    const int sleep = m.requirements.add(health, "Adequate sleep");
    m.requirements.attach(career, sleep);
    const int deep = m.requirements.add(career, "Deep work");
    const int running = m.projects.add(0, "Running");
    const int routine = m.projects.add(0, "Sleep Routine");
    m.requirements.link_project(sleep, routine);
    m.requirements.link_project(deep, routine);

    // Health 60 gives sleep 60; Career 40 gives sleep 40 * 2/3, deep work 40 * 1/3.
    const auto split = m.requirements.time_split(routine, {{health, 60.0}, {career, 40.0}});
    const double sleep_from_career = 40.0 * 2.0 / 3.0;
    const double sleep_total = 60.0 + sleep_from_career;
    CHECK_NEAR(split.at(health), 0.5 * 60.0 / sleep_total, 1e-9);
    CHECK_NEAR(split.at(career), 0.5 * sleep_from_career / sleep_total + 0.5, 1e-9);

    double total = 0.0;
    for (const auto& [leaf, fraction] : split) total += fraction;
    CHECK_NEAR(total, 1.0, 1e-9);

    CHECK(m.requirements.time_split(running, {{health, 60.0}, {career, 40.0}}).empty());
}
