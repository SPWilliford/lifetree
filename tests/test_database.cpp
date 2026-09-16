#include <cstdio>
#include <memory>
#include <string>

#include <sqlite3.h>

#include "core/Database.hpp"
#include "tests/test.hpp"

namespace {

// A project_links table as version 4 wrote it, with the old column name.
void write_v4_links_table(const std::string& path) {
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
        "PRAGMA user_version = 4;";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

}  // namespace

TEST(link_weight_column_is_renamed_on_open) {
    const std::string path = "/tmp/lifetree_migration_test.db";
    std::remove(path.c_str());
    write_v4_links_table(path);

    {
        Database db(path);
        const auto links = db.load_project_links();
        CHECK_EQ(links.size(), 1u);
        if (links.empty()) return;
        CHECK_EQ(links.front().project_root_id, 7);
        CHECK_EQ(links.front().leaf_id, 3);
        CHECK_EQ(links.front().project_share, 40.0);
        CHECK_EQ(links.front().goal_share, 60.0);
    }

    // Reopening runs migrate() again; the rename must not fire twice.
    {
        Database db(path);
        CHECK_EQ(db.load_project_links().size(), 1u);
    }
    std::remove(path.c_str());
}

TEST(fresh_database_round_trips_a_link) {
    Database db(":memory:");
    db.insert_root(TreeType::LIFE, "Root");
    db.insert_root(TreeType::PROJECTS, "Root");
    const int leaf = db.insert(TreeType::LIFE, 0, 0, "Leaf");
    const int project = db.insert(TreeType::PROJECTS, 0, 0, "Project");

    CHECK(db.set_project_link(project, leaf, 25.0, 75.0));
    const auto links = db.load_project_links();
    CHECK_EQ(links.size(), 1u);
    if (links.empty()) return;
    CHECK_EQ(links.front().project_share, 25.0);
    CHECK_EQ(links.front().goal_share, 75.0);
}
