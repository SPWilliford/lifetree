#include "core/Database.hpp"

#include <ctime>
#include <iostream>
#include <stdexcept>

namespace {

// One prepared statement, finalized when it goes out of scope. Parameters
// bind in CALL ORDER, not by index — there is no index here to get wrong.
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : m_db(db), m_sql(sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &m_stmt, nullptr) != SQLITE_OK) {
            m_stmt = nullptr;
            log("prepare failed");
        }
    }

    ~Statement() {
        if (m_stmt) sqlite3_finalize(m_stmt);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    // Callers check this first, so a bad statement never reaches a bind.
    explicit operator bool() const { return m_stmt != nullptr; }

    // bool and smaller integer types land on the int overload by
    // promotion; time_t lands on the 64-bit one.
    Statement& bind(int value) {
        sqlite3_bind_int(m_stmt, m_next++, value);
        return *this;
    }
    Statement& bind(sqlite3_int64 value) {
        sqlite3_bind_int64(m_stmt, m_next++, value);
        return *this;
    }
    Statement& bind(double value) {
        sqlite3_bind_double(m_stmt, m_next++, value);
        return *this;
    }
    Statement& bind(std::string_view value) {
        // TRANSIENT: sqlite copies, so the caller's buffer needn't outlive
        // the step. Length passed explicitly — a string_view may not be
        // null-terminated.
        sqlite3_bind_text(m_stmt, m_next++, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT);
        return *this;
    }
    Statement& bind_null() {
        sqlite3_bind_null(m_stmt, m_next++);
        return *this;
    }

    // True while a row is available: while (stmt.step()) { ... }
    bool step() { return m_stmt && sqlite3_step(m_stmt) == SQLITE_ROW; }

    // For statements returning no rows.
    bool run() {
        if (!m_stmt) return false;
        if (sqlite3_step(m_stmt) == SQLITE_DONE) return true;
        log("step failed");
        return false;
    }

    // Reads keep an explicit index: it sits against the SELECT list a few
    // lines above, so there's nothing distant to agree with.
    int column_int(int i) const { return sqlite3_column_int(m_stmt, i); }
    double column_double(int i) const { return sqlite3_column_double(m_stmt, i); }
    long column_long(int i) const { return static_cast<long>(sqlite3_column_int64(m_stmt, i)); }
    time_t column_time(int i) const { return static_cast<time_t>(sqlite3_column_int64(m_stmt, i)); }
    bool column_is_null(int i) const { return sqlite3_column_type(m_stmt, i) == SQLITE_NULL; }

    // NULL and "" both come back as "": every text column here is either
    // NOT NULL or wants "" as its absence.
    std::string column_text(int i) const {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, i));
        return text ? text : "";
    }

private:
    void log(const char* what) const {
        std::cerr << "[Database] " << what << ": " << sqlite3_errmsg(m_db) << "\n    " << m_sql
                  << std::endl;
    }

    sqlite3* m_db;
    std::string m_sql;  // for the error message, not for re-preparing
    sqlite3_stmt* m_stmt = nullptr;
    int m_next = 1;  // next parameter position; sqlite counts from 1
};

// Prepare, bind in order, step once. Covers every write that returns
// nothing but success or failure.
template <typename... Params>
bool write(sqlite3* db, const std::string& sql, const Params&... params) {
    Statement stmt(db, sql);
    if (!stmt) return false;
    (stmt.bind(params), ...);
    return stmt.run();
}

}  // namespace

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to open db: ") + sqlite3_errmsg(m_db));
    }

    // BEFORE create_schema(), which would make every database look like an
    // existing one that had nothing to add.
    const bool fresh = !has_table("life_tree");

    // Outside any transaction — PRAGMA foreign_keys is a silent no-op
    // inside one. The rebuild in migrate() drops both tree tables, and a
    // cascade there would take every weight, link and color with them.
    execute("PRAGMA foreign_keys = OFF;");

    // Separate, ordered phases: bringing a table into existence in the
    // first, changing an existing one in the second. Interleaved, a fresh
    // database runs ALTER TABLE against tables that don't exist yet.
    create_schema();
    if (fresh)
        set_schema_version(SCHEMA_VERSION);
    else
        migrate();

    execute("PRAGMA foreign_keys = ON;");
}

Database::~Database() {
    if (m_db) sqlite3_close(m_db);
}

// ---------------------------------------------------------------------
// Schema and migrations
// ---------------------------------------------------------------------

void Database::create_schema() {
    execute(
        "CREATE TABLE IF NOT EXISTS life_tree ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  parent_id INTEGER REFERENCES life_tree(id) ON DELETE CASCADE, "
        "  position INTEGER DEFAULT 0, "
        "  title TEXT NOT NULL);");

    execute(
        "CREATE TABLE IF NOT EXISTS projects_tree ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "  parent_id INTEGER REFERENCES projects_tree(id) ON DELETE CASCADE, "
        "  position INTEGER DEFAULT 0, "
        "  title TEXT NOT NULL);");

    execute(
        "CREATE TABLE IF NOT EXISTS work_log ("
        "  id INTEGER PRIMARY KEY, "
        "  title TEXT NOT NULL, "
        "  path TEXT NOT NULL, "
        "  color TEXT NOT NULL DEFAULT '', "
        "  source_task_id INTEGER NOT NULL DEFAULT -1, "
        "  project_root_id INTEGER NOT NULL DEFAULT -1, "
        "  start_time INTEGER NOT NULL, "
        "  end_time INTEGER NOT NULL, "
        "  completed_at INTEGER NOT NULL DEFAULT 0);");

    // A mark, not a spawner: one row says "this node and everything under
    // it recurs". Cascades, unlike task_completions — the mark is config
    // about a live node, so it goes when the node does.
    execute(
        "CREATE TABLE IF NOT EXISTS repeated_tasks ("
        "  node_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE, "
        "  weekday_mask INTEGER NOT NULL, "
        "  count_per_day INTEGER NOT NULL DEFAULT 1);");

    // No REFERENCES clause, and that's the design: a completion is a record
    // of something that happened, so deleting the task must not delete the
    // evidence. node_id is left to dangle exactly as work_log's
    // source_task_id does. The index is what makes "how many times today"
    // a lookup rather than a scan of every completion ever recorded.
    execute(
        "CREATE TABLE IF NOT EXISTS task_completions ("
        "  id INTEGER PRIMARY KEY, "
        "  node_id INTEGER NOT NULL, "
        "  date TEXT NOT NULL, "
        "  title TEXT NOT NULL, "
        "  path TEXT NOT NULL, "
        "  color TEXT NOT NULL DEFAULT '', "
        "  project_root_id INTEGER NOT NULL DEFAULT -1, "
        "  completed_at INTEGER NOT NULL);");

    execute(
        "CREATE INDEX IF NOT EXISTS idx_completions_node_date "
        "ON task_completions(node_id, date);");

    execute(
        "CREATE INDEX IF NOT EXISTS idx_completions_date "
        "ON task_completions(date);");

    // Keyed on the date, so a day is defined by having a row. Nothing
    // references it and nothing cascades: a day's hours are a fact about
    // that date, not about anything in the trees.
    // One per tree, mirroring the two tree tables, so each can carry a real
    // foreign key and cascade. A single table keyed by (tree, id) would have
    // to drop the cascade, since a row can only reference one table.
    //
    // Both trees, though only the life tree offers an editor today: a seed
    // is what a node MEANS, and that question is as valid of a project as of
    // a goal. Cheaper to have the column than to migrate one in later.
    for (const std::string_view tree : {"life", "projects"}) {
        const std::string table = (tree == "life") ? "life_seeds" : "project_seeds";
        const std::string parent = (tree == "life") ? "life_tree" : "projects_tree";
        execute("CREATE TABLE IF NOT EXISTS " + table +
                " ("
                "  node_id INTEGER PRIMARY KEY REFERENCES " +
                parent +
                "(id) ON DELETE CASCADE, "
                "  seed TEXT NOT NULL);");
    }

    execute(
        "CREATE TABLE IF NOT EXISTS day_hours ("
        "  date TEXT PRIMARY KEY, "
        "  start_minutes INTEGER NOT NULL, "
        "  end_minutes INTEGER NOT NULL);");

    execute(
        "CREATE TABLE IF NOT EXISTS sequential_nodes ("
        "  node_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE);");

    execute(
        "CREATE TABLE IF NOT EXISTS container_nodes ("
        "  node_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE);");

    execute(
        "CREATE TABLE IF NOT EXISTS task_dates ("
        "  node_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE,"
        "  date TEXT NOT NULL,"
        "  time_start INTEGER,"
        "  time_end INTEGER);");

    execute(
        "CREATE TABLE IF NOT EXISTS project_colors ("
        "  project_root_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE, "
        "  color TEXT NOT NULL);");

    execute(
        "CREATE TABLE IF NOT EXISTS life_weights ("
        "  node_id INTEGER PRIMARY KEY REFERENCES life_tree(id) ON DELETE CASCADE, "
        "  weight REAL NOT NULL);");

    // Cascades from both sides, so deleting either end removes the link
    // rather than leaving it pointing at nothing.
    execute(
        "CREATE TABLE IF NOT EXISTS project_links ("
        "  project_root_id INTEGER NOT NULL REFERENCES projects_tree(id) ON DELETE CASCADE, "
        "  leaf_id INTEGER NOT NULL REFERENCES life_tree(id) ON DELETE CASCADE, "
        "  weight REAL NOT NULL, "
        "  goal_share REAL NOT NULL DEFAULT 0, "
        "  PRIMARY KEY (project_root_id, leaf_id));");
}

void Database::migrate() {
    // Independent of schema_version: has_column asks about the database in
    // front of us rather than counting migrations.
    if (!has_column("work_log", "color")) {
        execute("ALTER TABLE work_log ADD COLUMN color TEXT NOT NULL DEFAULT '';");
    }
    if (!has_column("work_log", "source_task_id")) {
        execute("ALTER TABLE work_log ADD COLUMN source_task_id INTEGER NOT NULL DEFAULT -1;");
    }
    if (!has_column("work_log", "completed_at")) {
        execute("ALTER TABLE work_log ADD COLUMN completed_at INTEGER NOT NULL DEFAULT 0;");
    }
    // Older rows keep -1 and can't be backfilled: the tasks they came from
    // are deleted, so there's nothing to walk up from.
    if (!has_column("work_log", "project_root_id")) {
        execute("ALTER TABLE work_log ADD COLUMN project_root_id INTEGER NOT NULL DEFAULT -1;");
    }
    // The second direction of a link — see Priority. Seeded to 0 and
    // normalized per leaf on load, which becomes an even split.
    if (!has_column("project_links", "goal_share")) {
        execute("ALTER TABLE project_links ADD COLUMN goal_share REAL NOT NULL DEFAULT 0;");
    }
    // A repeat mark stopped generating anything, so the column naming it a
    // generator and the two columns serving the spawn loop all go. Guarded
    // by has_column rather than by version so each runs at most once and a
    // database created today skips all three.
    if (has_column("repeated_tasks", "generator_id")) {
        execute("ALTER TABLE repeated_tasks RENAME COLUMN generator_id TO node_id;");
    }
    if (has_column("repeated_tasks", "accumulates")) {
        execute("ALTER TABLE repeated_tasks DROP COLUMN accumulates;");
    }
    if (has_column("repeated_tasks", "last_spawned_date")) {
        execute("ALTER TABLE repeated_tasks DROP COLUMN last_spawned_date;");
    }

    // Versioned steps, for what a column check can't express: what values
    // MEAN, or how a table is built.
    if (schema_version() < 1) {
        // Link weights were a fixed 1.0 placeholder; they're percentages
        // now. Version-guarded rather than value-guarded: 1.0 is a
        // legitimate weight today, and a value check would keep promoting
        // it on every launch.
        execute("UPDATE project_links SET weight = 100.0;");
        set_schema_version(1);
    }

    if (schema_version() < 2) {
        rebuild_trees_with_autoincrement();
        set_schema_version(2);
    }

    // v3 seeded every top-level project into container_nodes, back when
    // container-ness was depth. It isn't: mark_container walks UP, so a
    // container comes into being by having something under it, and a
    // top-level node with no children is an ordinary task. Seeding that
    // rule into a new database would now be wrong.
    //
    // The stamp stays, and has to: without it a v2 database never reaches
    // 3 and re-runs the tree rebuild above on every launch.
    if (schema_version() < 3) {
        set_schema_version(3);
    }

    // v4: a marked node's children used to be its spawned instances, and
    // now they're structure the user authored. Every existing instance has
    // to go, or "Pushups" comes back as a container holding three phantom
    // sub-tasks that would never be completable.
    //
    // Must run after the spawner is gone from the code, not before: the old
    // scan would simply recreate them on the next launch.
    if (schema_version() < 4) {
        clear_spawned_instances();
        set_schema_version(4);
    }
}

void Database::clear_spawned_instances() {
    // Recursive rather than a single DELETE on parent_id: foreign keys are
    // OFF for the whole of migrate(), so ON DELETE CASCADE will not fire and
    // anything below an instance would be orphaned rather than removed.
    execute("BEGIN;");

    execute(
        "CREATE TEMP TABLE doomed AS "
        "WITH RECURSIVE descendants(id) AS ("
        "  SELECT id FROM projects_tree "
        "    WHERE parent_id IN (SELECT node_id FROM repeated_tasks) "
        "  UNION ALL "
        "  SELECT p.id FROM projects_tree p "
        "    JOIN descendants d ON p.parent_id = d.id) "
        "SELECT id FROM descendants;");

    // By hand, for the same reason: the cascade that normally does this is
    // switched off. Links and colors attach to project roots only, and an
    // instance is never a root, so they have nothing to clean up here.
    for (const std::string table : {"sequential_nodes", "container_nodes", "task_dates"}) {
        execute("DELETE FROM " + table + " WHERE node_id IN (SELECT id FROM doomed);");
    }
    execute("DELETE FROM projects_tree WHERE id IN (SELECT id FROM doomed);");
    execute("DROP TABLE doomed;");

    // A marked node that is now a leaf is a task in its own right — three
    // sets of pushups, not a heading. The container flag it may be carrying
    // was inert under the old model, since a generator never entered the
    // backlog anyway; under this one it would silently swallow the task.
    // Run after the delete, so "leaf" means leaf as of now.
    execute(
        "DELETE FROM container_nodes WHERE node_id IN ("
        "  SELECT node_id FROM repeated_tasks r "
        "    WHERE NOT EXISTS (SELECT 1 FROM projects_tree p WHERE p.parent_id = r.node_id));");

    execute("COMMIT;");
}

void Database::rebuild_trees_with_autoincrement() {
    // SQLite can't ALTER into AUTOINCREMENT, so each tree is rebuilt beside
    // itself: create, copy, drop, rename. Ids copy verbatim.
    //
    // Copying explicit ids seeds sqlite_sequence at the highest present,
    // which is the point: without it, deleting the highest node frees its
    // id and the next node inherits the deleted one's color, schedule and
    // links from the caches still holding them.
    //
    // Requires foreign keys OFF — the constructor arranges it.
    execute("BEGIN;");
    for (const std::string table : {"life_tree", "projects_tree"}) {
        execute("CREATE TABLE " + table +
                "_migrating ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "  parent_id INTEGER REFERENCES " +
                table +
                "(id) ON DELETE CASCADE, "
                "  position INTEGER DEFAULT 0, "
                "  title TEXT NOT NULL);");
        execute("INSERT INTO " + table +
                "_migrating (id, parent_id, position, title) "
                "SELECT id, parent_id, position, title FROM " +
                table + ";");
        execute("DROP TABLE " + table + ";");
        execute("ALTER TABLE " + table + "_migrating RENAME TO " + table + ";");
    }
    execute("COMMIT;");
}

bool Database::has_table(const std::string& name) {
    Statement stmt(m_db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;");
    if (!stmt) return false;
    return stmt.bind(name).step();
}

bool Database::has_column(const std::string& table, const std::string& column) {
    // PRAGMA won't take a bound parameter. Safe: every caller passes a
    // literal from this file.
    Statement stmt(m_db, "PRAGMA table_info(" + table + ");");
    if (!stmt) return false;

    // (cid, name, type, notnull, dflt_value, pk)
    while (stmt.step()) {
        if (stmt.column_text(1) == column) return true;
    }
    return false;
}

int Database::schema_version() {
    Statement stmt(m_db, "PRAGMA user_version;");
    if (!stmt || !stmt.step()) return 0;
    return stmt.column_int(0);
}

void Database::set_schema_version(int version) {
    // Same splice, same reason: a constant from this file.
    execute("PRAGMA user_version = " + std::to_string(version) + ";");
}

void Database::execute(const std::string& sql) {
    // Throws, unlike everything below: only used for schema work in the
    // constructor, where a failure means the database is unusable. main()
    // catches it.
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = std::string("SQL Error: ") + err;
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

std::string_view Database::table_name(TreeType type) const {
    return (type == TreeType::LIFE) ? "life_tree" : "projects_tree";
}

std::string_view Database::seed_table_name(TreeType type) const {
    return (type == TreeType::LIFE) ? "life_seeds" : "project_seeds";
}

// ---------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------

std::vector<Row> Database::load(TreeType type) {
    std::vector<Row> rows;

    Statement stmt(m_db, "SELECT id, parent_id, position, title FROM " +
                             std::string(table_name(type)) + " ORDER BY id ASC;");
    if (!stmt) return rows;

    while (stmt.step()) {
        // The root's parent_id is NULL, which the in-memory tree spells -1.
        const int parent_id = stmt.column_is_null(1) ? -1 : stmt.column_int(1);
        rows.push_back({stmt.column_int(0), parent_id, stmt.column_int(2), stmt.column_text(3)});
    }
    return rows;
}

int Database::insert(TreeType type, int parent_id, int position, std::string_view title) {
    Statement stmt(m_db, "INSERT INTO " + std::string(table_name(type)) +
                             " (parent_id, position, title) VALUES (?, ?, ?);");
    if (!stmt) return -1;

    if (parent_id == -1)
        stmt.bind_null();
    else
        stmt.bind(parent_id);
    stmt.bind(position).bind(title);

    // The row only exists if this actually completed. Trusting
    // last_insert_rowid() without checking would return a stale id from a
    // previous successful insert.
    if (!stmt.run()) return -1;
    return static_cast<int>(sqlite3_last_insert_rowid(m_db));
}

bool Database::set_position(TreeType type, int id, int position) {
    return write(m_db,
                 "UPDATE " + std::string(table_name(type)) + " SET position = ? WHERE id = ?;",
                 position, id);
}

void Database::insert_root(TreeType type, std::string_view title) {
    write(m_db,
          "INSERT OR IGNORE INTO " + std::string(table_name(type)) +
              " (id, parent_id, position, title) VALUES (0, NULL, 0, ?);",
          title);
}

bool Database::write_title(TreeType type, int id, std::string_view title) {
    return write(m_db, "UPDATE " + std::string(table_name(type)) + " SET title = ? WHERE id = ?;",
                 title, id);
}

bool Database::remove(TreeType type, int id) {
    return write(m_db, "DELETE FROM " + std::string(table_name(type)) + " WHERE id = ?;", id);
}

// ---------------------------------------------------------------------
// Work log
// ---------------------------------------------------------------------

void Database::day_bounds(time_t day, time_t& start, time_t& end) const {
    std::tm tm_buf{};
    localtime_r(&day, &tm_buf);
    tm_buf.tm_hour = 0;
    tm_buf.tm_min = 0;
    tm_buf.tm_sec = 0;

    // -1 rather than the tm_isdst localtime_r filled in for `day` itself:
    // on the two DST transition days those disagree at midnight and the
    // boundary lands an hour off.
    tm_buf.tm_isdst = -1;

    start = std::mktime(&tm_buf);
    end = start + 24 * 60 * 60;
}

void Database::insert_work_log(const WorkLogRow& row) {
    // A literal 0, not bound from the row: segments are always recorded
    // open and stamped later.
    write(m_db,
          "INSERT INTO work_log (title, path, color, source_task_id, project_root_id, "
          "start_time, end_time, completed_at) VALUES (?, ?, ?, ?, ?, ?, ?, 0);",
          row.title, row.path, row.color, row.source_task_id, row.project_root_id,
          static_cast<sqlite3_int64>(row.start_time), static_cast<sqlite3_int64>(row.end_time));
}

std::vector<WorkLogRow> Database::load_work_log_for_day(time_t day) {
    std::vector<WorkLogRow> rows;

    time_t day_start, day_end;
    day_bounds(day, day_start, day_end);

    Statement stmt(m_db,
                   "SELECT title, path, color, source_task_id, project_root_id, "
                   "start_time, end_time, completed_at FROM work_log "
                   "WHERE start_time >= ? AND start_time < ? ORDER BY start_time ASC;");
    if (!stmt) return rows;

    stmt.bind(static_cast<sqlite3_int64>(day_start)).bind(static_cast<sqlite3_int64>(day_end));

    while (stmt.step()) {
        rows.push_back(WorkLogRow{.title = stmt.column_text(0),
                                  .path = stmt.column_text(1),
                                  .color = stmt.column_text(2),
                                  .source_task_id = stmt.column_int(3),
                                  .project_root_id = stmt.column_int(4),
                                  .start_time = stmt.column_time(5),
                                  .end_time = stmt.column_time(6),
                                  .completed_at = stmt.column_time(7)});
    }
    return rows;
}

bool Database::mark_work_log_completed(int source_task_id, time_t completed_at) {
    return write(m_db,
                 "UPDATE work_log SET completed_at = ? "
                 "WHERE source_task_id = ? AND completed_at = 0;",
                 static_cast<sqlite3_int64>(completed_at), source_task_id);
}

std::vector<CompletedTaskSummary> Database::load_completed_tasks_for_day(time_t day) {
    std::vector<CompletedTaskSummary> rows;

    time_t day_start, day_end;
    day_bounds(day, day_start, day_end);

    // By source_task_id, not title/path: two tasks in different projects
    // may share both, and grouping by name would merge them. title/path/
    // color come from whichever row the group yields — every segment
    // sharing a source_task_id was written from one snapshot.
    Statement stmt(m_db,
                   "SELECT title, path, color, SUM(end_time - start_time) AS total_seconds, "
                   "MAX(completed_at) AS completed_at "
                   "FROM work_log "
                   // completed_at = 0 falls outside any real day's range, so this
                   // excludes in-progress segments without a separate check.
                   "WHERE completed_at >= ? AND completed_at < ? "
                   "GROUP BY source_task_id "
                   "ORDER BY completed_at ASC;");
    if (!stmt) return rows;

    stmt.bind(static_cast<sqlite3_int64>(day_start)).bind(static_cast<sqlite3_int64>(day_end));

    while (stmt.step()) {
        rows.push_back({stmt.column_text(0), stmt.column_text(1), stmt.column_text(2),
                        stmt.column_long(3), stmt.column_time(4)});
    }
    return rows;
}

long Database::open_work_seconds(int source_task_id) {
    // completed_at = 0 means "not yet accounted to a completion", which is
    // exactly the span the dock wants: work since this task was last
    // finished. mark_work_log_completed stamps only the zeroes, so the
    // boundary maintains itself.
    //
    // One rule covering two cases that look unrelated. A one-off task is
    // never completed until it's gone, so this is its whole history —
    // paused Monday and resumed Tuesday still reads as one running total.
    // A recurring task is completed repeatedly, so each set of pushups
    // starts from zero. Neither case needs to know which it is.
    //
    // Summing everything regardless of completion was correct only by
    // accident under the old generator model, where a fresh node id each day
    // meant a day's work was all there ever was under that id. The node
    // survives now, so an unfiltered sum is a lifetime total.
    //
    // COALESCE because SUM over no rows is NULL, and a task never worked
    // should read as zero rather than as an error.
    Statement stmt(m_db,
                   "SELECT COALESCE(SUM(end_time - start_time), 0) FROM work_log "
                   "WHERE source_task_id = ? AND completed_at = 0;");
    if (!stmt) return 0;

    stmt.bind(source_task_id);
    return stmt.step() ? stmt.column_long(0) : 0;
}

// ---------------------------------------------------------------------
// Completions
// ---------------------------------------------------------------------

bool Database::insert_completion(const TaskCompletionRow& row) {
    // Append-only: a completion is never updated or replaced. Doing a task
    // twice in one day is two rows, which is what makes counting them the
    // answer to "how many of the three sets are done".
    return write(m_db,
                 "INSERT INTO task_completions "
                 "(node_id, date, title, path, color, project_root_id, completed_at) "
                 "VALUES (?, ?, ?, ?, ?, ?, ?);",
                 row.node_id, row.date, row.title, row.path, row.color, row.project_root_id,
                 static_cast<sqlite3_int64>(row.completed_at));
}

std::vector<TaskCompletionRow> Database::load_completions_for_date(const std::string& date) {
    std::vector<TaskCompletionRow> rows;

    // Matched on the stored civil day rather than on completed_at against
    // a computed midnight: the day is what was recorded, so a task finished
    // either side of a DST shift still belongs to the day it was done.
    Statement stmt(m_db,
                   "SELECT node_id, date, title, path, color, project_root_id, completed_at "
                   "FROM task_completions WHERE date = ? ORDER BY completed_at ASC;");
    if (!stmt) return rows;

    stmt.bind(date);

    while (stmt.step()) {
        rows.push_back(TaskCompletionRow{.node_id = stmt.column_int(0),
                                         .date = stmt.column_text(1),
                                         .title = stmt.column_text(2),
                                         .path = stmt.column_text(3),
                                         .color = stmt.column_text(4),
                                         .project_root_id = stmt.column_int(5),
                                         .completed_at = stmt.column_time(6)});
    }
    return rows;
}

// ---------------------------------------------------------------------
// Seeds
// ---------------------------------------------------------------------

bool Database::set_seed(TreeType type, int id, std::string_view seed) {
    // Cleared rather than blanked: a row saying "" would be indistinguishable
    // from one nobody wrote, and the sparse table is what makes "which nodes
    // still need a seed" a query rather than a scan.
    if (seed.empty()) {
        return write(
            m_db, "DELETE FROM " + std::string(seed_table_name(type)) + " WHERE node_id = ?;", id);
    }
    return write(m_db,
                 "INSERT OR REPLACE INTO " + std::string(seed_table_name(type)) +
                     " (node_id, seed) VALUES (?, ?);",
                 id, seed);
}

std::vector<SeedRow> Database::load_seeds(TreeType type) {
    std::vector<SeedRow> rows;
    Statement stmt(m_db, "SELECT node_id, seed FROM " + std::string(seed_table_name(type)) + ";");
    if (!stmt) return rows;

    while (stmt.step()) {
        rows.push_back(SeedRow{stmt.column_int(0), stmt.column_text(1)});
    }
    return rows;
}

// ---------------------------------------------------------------------
// Day hours
// ---------------------------------------------------------------------

bool Database::set_day_hours(const std::string& date, int start_minutes, int end_minutes) {
    return write(m_db,
                 "INSERT OR REPLACE INTO day_hours (date, start_minutes, end_minutes) "
                 "VALUES (?, ?, ?);",
                 date, start_minutes, end_minutes);
}

bool Database::clear_day_hours(const std::string& date) {
    return write(m_db, "DELETE FROM day_hours WHERE date = ?;", date);
}

DayHoursRow Database::load_day_hours(const std::string& date) {
    DayHoursRow row;
    row.date = date;

    // date <= ? ordered descending: this day's own row if it has one, and
    // otherwise the last day that set any. That inheritance is what makes
    // the rows sparse — a default costs no second concept and no second
    // table, it's just the most recent thing you said.
    //
    // String comparison, as everywhere else 'YYYY-MM-DD' is stored: it
    // orders the way the calendar does, with no parsing.
    Statement stmt(m_db,
                   "SELECT start_minutes, end_minutes FROM day_hours "
                   "WHERE date <= ? ORDER BY date DESC LIMIT 1;");
    if (!stmt) return row;

    stmt.bind(date);
    if (stmt.step()) {
        row.start_minutes = stmt.column_int(0);
        row.end_minutes = stmt.column_int(1);
    }
    return row;
}

// ---------------------------------------------------------------------
// Sequential nodes
// ---------------------------------------------------------------------

bool Database::insert_sequential(int node_id) {
    return write(m_db, "INSERT OR REPLACE INTO sequential_nodes (node_id) VALUES (?);", node_id);
}

bool Database::remove_sequential(int node_id) {
    return write(m_db, "DELETE FROM sequential_nodes WHERE node_id = ?;", node_id);
}

std::vector<int> Database::load_sequential() {
    std::vector<int> ids;
    Statement stmt(m_db, "SELECT node_id FROM sequential_nodes;");
    if (!stmt) return ids;

    while (stmt.step()) ids.push_back(stmt.column_int(0));
    return ids;
}

// ---------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------

bool Database::insert_container(int node_id) {
    return write(m_db, "INSERT OR REPLACE INTO container_nodes (node_id) VALUES (?);", node_id);
}

bool Database::remove_container(int node_id) {
    return write(m_db, "DELETE FROM container_nodes WHERE node_id = ?;", node_id);
}

std::vector<int> Database::load_containers() {
    std::vector<int> ids;
    Statement stmt(m_db, "SELECT node_id FROM container_nodes;");
    if (!stmt) return ids;

    while (stmt.step()) ids.push_back(stmt.column_int(0));
    return ids;
}

// ---------------------------------------------------------------------
// Task dates
// ---------------------------------------------------------------------

bool Database::set_task_date(int node_id, const std::string& date, int time_start, int time_end) {
    // Not write(): absent times bind as NULL, which write() can't express.
    Statement stmt(m_db,
                   "INSERT OR REPLACE INTO task_dates "
                   "(node_id, date, time_start, time_end) VALUES (?, ?, ?, ?);");
    if (!stmt) return false;

    stmt.bind(node_id).bind(date);
    if (time_start == TaskDateRow::NO_TIME)
        stmt.bind_null();
    else
        stmt.bind(time_start);
    if (time_end == TaskDateRow::NO_TIME)
        stmt.bind_null();
    else
        stmt.bind(time_end);
    return stmt.run();
}

bool Database::clear_task_date(int node_id) {
    return write(m_db, "DELETE FROM task_dates WHERE node_id = ?;", node_id);
}

std::vector<TaskDateRow> Database::load_task_dates() {
    std::vector<TaskDateRow> rows;
    Statement stmt(m_db, "SELECT node_id, date, time_start, time_end FROM task_dates;");
    if (!stmt) return rows;

    while (stmt.step()) {
        TaskDateRow row;
        row.node_id = stmt.column_int(0);
        row.date = stmt.column_text(1);
        if (!stmt.column_is_null(2)) row.time_start = stmt.column_int(2);
        if (!stmt.column_is_null(3)) row.time_end = stmt.column_int(3);
        rows.push_back(row);
    }
    return rows;
}

// ---------------------------------------------------------------------
// Repeat marks
// ---------------------------------------------------------------------

bool Database::insert_repeated_task(int node_id, int weekday_mask, int count_per_day) {
    // Nothing to preserve across a re-mark now that there is no spawn
    // history: the row is the whole of the schedule.
    return write(m_db,
                 "INSERT OR REPLACE INTO repeated_tasks "
                 "(node_id, weekday_mask, count_per_day) VALUES (?, ?, ?);",
                 node_id, weekday_mask, count_per_day);
}

bool Database::remove_repeated_task(int node_id) {
    return write(m_db, "DELETE FROM repeated_tasks WHERE node_id = ?;", node_id);
}

std::vector<RepeatedTaskRow> Database::load_repeated_tasks() {
    std::vector<RepeatedTaskRow> rows;
    Statement stmt(m_db, "SELECT node_id, weekday_mask, count_per_day FROM repeated_tasks;");
    if (!stmt) return rows;

    while (stmt.step()) {
        rows.push_back(RepeatedTaskRow{.node_id = stmt.column_int(0),
                                       .weekday_mask = stmt.column_int(1),
                                       .count_per_day = stmt.column_int(2)});
    }
    return rows;
}

// ---------------------------------------------------------------------
// Project links and life weights
// ---------------------------------------------------------------------

bool Database::set_project_link(int project_root_id, int leaf_id, double weight,
                                double goal_share) {
    return write(m_db,
                 "INSERT OR REPLACE INTO project_links "
                 "(project_root_id, leaf_id, weight, goal_share) VALUES (?, ?, ?, ?);",
                 project_root_id, leaf_id, weight, goal_share);
}

bool Database::clear_project_link(int project_root_id, int leaf_id) {
    return write(m_db, "DELETE FROM project_links WHERE project_root_id = ? AND leaf_id = ?;",
                 project_root_id, leaf_id);
}

std::vector<ProjectLinkRow> Database::load_project_links() {
    std::vector<ProjectLinkRow> rows;
    Statement stmt(m_db, "SELECT project_root_id, leaf_id, weight, goal_share FROM project_links;");
    if (!stmt) return rows;

    while (stmt.step()) {
        rows.push_back(
            {stmt.column_int(0), stmt.column_int(1), stmt.column_double(2), stmt.column_double(3)});
    }
    return rows;
}

bool Database::set_life_weight(int node_id, double weight) {
    return write(m_db, "INSERT OR REPLACE INTO life_weights (node_id, weight) VALUES (?, ?);",
                 node_id, weight);
}

std::vector<LifeWeightRow> Database::load_life_weights() {
    std::vector<LifeWeightRow> rows;
    Statement stmt(m_db, "SELECT node_id, weight FROM life_weights;");
    if (!stmt) return rows;

    while (stmt.step()) {
        rows.push_back({stmt.column_int(0), stmt.column_double(1)});
    }
    return rows;
}

// ---------------------------------------------------------------------
// Project colors
// ---------------------------------------------------------------------

bool Database::set_project_color(int project_root_id, const std::string& color) {
    return write(m_db,
                 "INSERT OR REPLACE INTO project_colors (project_root_id, color) "
                 "VALUES (?, ?);",
                 project_root_id, color);
}

bool Database::clear_project_color(int project_root_id) {
    return write(m_db, "DELETE FROM project_colors WHERE project_root_id = ?;", project_root_id);
}

std::vector<ProjectColorRow> Database::load_project_colors() {
    std::vector<ProjectColorRow> rows;
    Statement stmt(m_db, "SELECT project_root_id, color FROM project_colors;");
    if (!stmt) return rows;

    while (stmt.step()) {
        rows.push_back({stmt.column_int(0), stmt.column_text(1)});
    }
    return rows;
}
