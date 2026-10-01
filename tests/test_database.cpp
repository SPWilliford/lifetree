#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "core/Database.hpp"
#include "tests/test.hpp"

namespace {

// A database as version 4 left it, leaf links and all.
void write_v4_database(const std::string& path) {
    sqlite3* db = nullptr;
    sqlite3_open(path.c_str(), &db);
    const char* sql =
        "CREATE TABLE life_tree (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER, "
        "  position INTEGER NOT NULL DEFAULT 0, title TEXT NOT NULL);"
        "CREATE TABLE projects_tree (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER, "
        "  position INTEGER NOT NULL DEFAULT 0, title TEXT NOT NULL);"
        "CREATE TABLE project_links (project_root_id INTEGER NOT NULL, leaf_id INTEGER NOT NULL, "
        "  weight REAL NOT NULL, goal_share REAL NOT NULL DEFAULT 0, "
        "  PRIMARY KEY (project_root_id, leaf_id));"
        "INSERT INTO project_links VALUES (7, 3, 40.0, 60.0);"
        "INSERT INTO life_tree (id, parent_id, position, title) VALUES (0, NULL, 0, 'Root');"
        "PRAGMA user_version = 4;";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

bool table_exists(const std::string& path, const std::string& table) {
    sqlite3* db = nullptr;
    sqlite3_open(path.c_str(), &db);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", -1, &stmt,
                       nullptr);
    sqlite3_bind_text(stmt, 1, table.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

}  // namespace

TEST(opening_a_v4_database_drops_the_old_leaf_links) {
    const std::string path = "/tmp/lifetree_migration_test.db";
    std::remove(path.c_str());
    write_v4_database(path);

    { Database db(path); }
    CHECK(!table_exists(path, "project_links"));
    CHECK(table_exists(path, "requirements"));

    // Reopening runs migrate() again on the dropped table.
    {
        Database db(path);
        CHECK_EQ(db.load(TreeType::LIFE).size(), 1u);
    }
    std::remove(path.c_str());
}

TEST(requirement_round_trips_with_both_links) {
    Database db(":memory:");
    db.insert_root(TreeType::LIFE, "Root");
    db.insert_root(TreeType::PROJECTS, "Root");
    const int leaf = db.insert(TreeType::LIFE, 0, 0, "Health");
    const int project = db.insert(TreeType::PROJECTS, 0, 0, "Sleep Routine");

    const int sleep = db.insert_requirement("Adequate sleep");
    CHECK(sleep != -1);
    CHECK(db.set_leaf_requirement_link(leaf, sleep, 0));
    CHECK(db.set_requirement_project_link(sleep, project));
    CHECK(db.write_requirement_title(sleep, "Enough sleep"));

    const auto requirements = db.load_requirements();
    CHECK_EQ(requirements.size(), 1u);
    if (!requirements.empty()) CHECK_EQ(requirements.front().title, std::string("Enough sleep"));

    const auto leaf_links = db.load_leaf_requirement_links();
    CHECK_EQ(leaf_links.size(), 1u);
    if (!leaf_links.empty()) {
        CHECK_EQ(leaf_links.front().leaf_id, leaf);
        CHECK_EQ(leaf_links.front().requirement_id, sleep);
    }

    const auto project_links = db.load_requirement_project_links();
    CHECK_EQ(project_links.size(), 1u);
    if (!project_links.empty()) CHECK_EQ(project_links.front().project_root_id, project);
}

TEST(leaf_requirements_load_in_position_order) {
    Database db(":memory:");
    db.insert_root(TreeType::LIFE, "Root");
    const int leaf = db.insert(TreeType::LIFE, 0, 0, "Health");
    const int sleep = db.insert_requirement("Adequate sleep");
    const int exercise = db.insert_requirement("Regular exercise");

    db.set_leaf_requirement_link(leaf, sleep, 1);
    db.set_leaf_requirement_link(leaf, exercise, 0);

    const auto links = db.load_leaf_requirement_links();
    CHECK_EQ(links.size(), 2u);
    if (links.size() == 2) {
        CHECK_EQ(links[0].requirement_id, exercise);
        CHECK_EQ(links[1].requirement_id, sleep);
    }
}

TEST(orphan_requirement_is_removed_with_its_project_links) {
    Database db(":memory:");
    db.insert_root(TreeType::LIFE, "Root");
    db.insert_root(TreeType::PROJECTS, "Root");
    const int health = db.insert(TreeType::LIFE, 0, 0, "Health");
    const int career = db.insert(TreeType::LIFE, 0, 1, "Career");
    const int project = db.insert(TreeType::PROJECTS, 0, 0, "Sleep Routine");

    const int shared = db.insert_requirement("Adequate sleep");
    const int solo = db.insert_requirement("Regular exercise");
    db.set_leaf_requirement_link(health, shared, 0);
    db.set_leaf_requirement_link(career, shared, 0);
    db.set_leaf_requirement_link(health, solo, 1);
    db.set_requirement_project_link(solo, project);

    // Deleting a leaf takes its links; the requirements themselves stay
    // until the orphan sweep.
    CHECK(db.remove(TreeType::LIFE, health));
    CHECK_EQ(db.load_leaf_requirement_links().size(), 1u);
    CHECK_EQ(db.load_requirements().size(), 2u);

    CHECK(db.remove_orphan_requirements());
    const auto requirements = db.load_requirements();
    CHECK_EQ(requirements.size(), 1u);
    if (!requirements.empty()) CHECK_EQ(requirements.front().id, shared);
    CHECK_EQ(db.load_requirement_project_links().size(), 0u);
}

TEST(requirements_move_down_to_a_new_child_in_order) {
    Database db(":memory:");
    db.insert_root(TreeType::LIFE, "Root");
    const int health = db.insert(TreeType::LIFE, 0, 0, "Health");
    const int sleep = db.insert_requirement("Adequate sleep");
    const int exercise = db.insert_requirement("Regular exercise");
    db.set_leaf_requirement_link(health, sleep, 0);
    db.set_leaf_requirement_link(health, exercise, 1);

    const int body = db.insert(TreeType::LIFE, health, 0, "Body");
    CHECK(db.move_leaf_requirement_links(health, body));

    const auto links = db.load_leaf_requirement_links();
    CHECK_EQ(links.size(), 2u);
    if (links.size() == 2) {
        CHECK_EQ(links[0].leaf_id, body);
        CHECK_EQ(links[0].requirement_id, sleep);
        CHECK_EQ(links[1].leaf_id, body);
        CHECK_EQ(links[1].requirement_id, exercise);
    }
}

TEST(moved_requirements_follow_existing_ones_and_merge_duplicates) {
    Database db(":memory:");
    db.insert_root(TreeType::LIFE, "Root");
    const int from = db.insert(TreeType::LIFE, 0, 0, "From");
    const int to = db.insert(TreeType::LIFE, 0, 1, "To");
    const int sleep = db.insert_requirement("Adequate sleep");
    const int exercise = db.insert_requirement("Regular exercise");
    const int diet = db.insert_requirement("Healthy diet");

    db.set_leaf_requirement_link(to, diet, 0);
    db.set_leaf_requirement_link(to, sleep, 1);
    db.set_leaf_requirement_link(from, sleep, 0);
    db.set_leaf_requirement_link(from, exercise, 1);

    CHECK(db.move_leaf_requirement_links(from, to));

    const auto links = db.load_leaf_requirement_links();
    std::vector<int> order;
    for (const auto& link : links) {
        CHECK_EQ(link.leaf_id, to);
        order.push_back(link.requirement_id);
    }
    CHECK_EQ(order, (std::vector<int>{diet, sleep, exercise}));
}
