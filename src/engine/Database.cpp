#include "engine/Database.hpp"
#include <stdexcept>
#include <ctime>
#include <iostream>

// Unconditional — a write that actually failed isn't debug noise, it's
// the UI and the database about to disagree with each other. Always
// visible in the terminal.
#define DB_ERR(x) std::cerr << x << std::endl

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &m_db) != SQLITE_OK) {
        throw std::runtime_error(std::string("Failed to open db: ") + sqlite3_errmsg(m_db));
    }

    // Enable foreign keys for automatic cascading tree deletions
    execute("PRAGMA foreign_keys = ON;");

    execute("CREATE TABLE IF NOT EXISTS life_tree ("
            "  id INTEGER PRIMARY KEY, "
            "  parent_id INTEGER REFERENCES life_tree(id) ON DELETE CASCADE, "
            "  position INTEGER DEFAULT 0, "
            "  title TEXT NOT NULL);");

    execute("CREATE TABLE IF NOT EXISTS projects_tree ("
            "  id INTEGER PRIMARY KEY, "
            "  parent_id INTEGER REFERENCES projects_tree(id) ON DELETE CASCADE, "
            "  position INTEGER DEFAULT 0, "
            "  title TEXT NOT NULL);");

    execute("CREATE TABLE IF NOT EXISTS work_log ("
            "  id INTEGER PRIMARY KEY, "
            "  title TEXT NOT NULL, "
            "  path TEXT NOT NULL, "
            "  color TEXT NOT NULL DEFAULT '', "
            "  source_task_id INTEGER NOT NULL DEFAULT -1, "
            "  start_time INTEGER NOT NULL, "
            "  end_time INTEGER NOT NULL, "
            "  completed_at INTEGER NOT NULL DEFAULT 0);");

    // CREATE TABLE IF NOT EXISTS above only creates work_log fresh — an
    // existing database (created before these columns existed) already
    // has the table without them, and won't pick them up retroactively.
    // Checked first rather than just attempting the ALTER and catching
    // the failure — that would mean throwing and catching a C++
    // exception as routine, expected control flow on every single
    // startup after the first, forever.
    if (!has_column("work_log", "color")) {
        execute("ALTER TABLE work_log ADD COLUMN color TEXT NOT NULL DEFAULT '';");
    }
    if (!has_column("work_log", "source_task_id")) {
        execute("ALTER TABLE work_log ADD COLUMN source_task_id INTEGER NOT NULL DEFAULT -1;");
    }
    if (!has_column("work_log", "completed_at")) {
        execute("ALTER TABLE work_log ADD COLUMN completed_at INTEGER NOT NULL DEFAULT 0;");
    }

    execute("CREATE TABLE IF NOT EXISTS repeated_tasks ("
            "  generator_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE, "
            "  weekday_mask INTEGER NOT NULL, "
            "  count_per_day INTEGER NOT NULL DEFAULT 1, "
            "  last_spawned_date TEXT NOT NULL DEFAULT '');");

    execute("CREATE TABLE IF NOT EXISTS project_colors ("
            "  project_root_id INTEGER PRIMARY KEY REFERENCES projects_tree(id) ON DELETE CASCADE, "
            "  color TEXT NOT NULL);");
}

Database::~Database() {
    if (m_db) sqlite3_close(m_db);
}

std::string_view Database::table_name(TreeType type) const {
    return (type == TreeType::LIFE) ? "life_tree" : "projects_tree";
}

bool Database::has_column(const std::string& table, const std::string& column) {
    std::string sql = "PRAGMA table_info(" + table + ");";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // table_info's columns are (cid, name, type, notnull, dflt_value, pk) —
        // name is column index 1.
        const char* name_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name_ptr && column == name_ptr) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

void Database::execute(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = std::string("SQL Error: ") + err;
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

std::vector<Row> Database::load(TreeType type) {
    std::vector<Row> rows;
    std::string sql = "SELECT id, parent_id, position, title FROM " + std::string(table_name(type)) + " ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);

        int parent_id = -1;
        if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
            parent_id = sqlite3_column_int(stmt, 1);
        }

        int position = sqlite3_column_int(stmt, 2);

        const char* text_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        std::string title = text_ptr ? text_ptr : "";

        rows.push_back({ id, parent_id, position, title });
    }
    sqlite3_finalize(stmt);
    return rows;
}

int Database::insert(TreeType type, int parent_id, int position, std::string_view title) {
    auto table = table_name(type);
    std::string sql = "INSERT INTO " + std::string(table) + " (parent_id, position, title) VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return -1;

    if (parent_id == -1) {
        sqlite3_bind_null(stmt, 1);
    } else {
        sqlite3_bind_int(stmt, 1, parent_id);
    }
    sqlite3_bind_int(stmt, 2, position);
    sqlite3_bind_text(stmt, 3, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);

    // The row only exists if this step actually completed. Trusting
    // last_insert_rowid() without checking here would return a stale id
    // from a previous successful insert on failure.
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] insert failed for table " << table);
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(m_db));
}

bool Database::write_title(TreeType type, int id, std::string_view title) {
    std::string sql = "UPDATE " + std::string(table_name(type)) + " SET title = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] write_title: failed to prepare statement for id " << id);
        return false;
    }

    sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] write_title failed for id " << id);
    }
    return ok;
}

bool Database::remove(TreeType type, int id) {
    std::string sql = "DELETE FROM " + std::string(table_name(type)) + " WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] remove: failed to prepare statement for id " << id);
        return false;
    }

    sqlite3_bind_int(stmt, 1, id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] remove failed for id " << id);
    }
    return ok;
}

void Database::insert_root(TreeType type, std::string_view title) {
    auto table = table_name(type);
    std::string sql = "INSERT OR IGNORE INTO " + std::string(table) + " (id, parent_id, position, title) VALUES (0, NULL, 0, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            DB_ERR("[Database] insert_root failed for table " << table);
        }
        sqlite3_finalize(stmt);
    }
}

void Database::insert_work_log(std::string_view title, std::string_view path, std::string_view color, int source_task_id, time_t start_time, time_t end_time) {
    std::string sql = "INSERT INTO work_log (title, path, color, source_task_id, start_time, end_time, completed_at) "
                       "VALUES (?, ?, ?, ?, ?, ?, 0);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.data(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, color.data(), static_cast<int>(color.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, source_task_id);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(start_time));
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(end_time));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        DB_ERR("[Database] insert_work_log failed for title " << title);
    }
    sqlite3_finalize(stmt);
}

void Database::day_bounds(time_t day, time_t& start, time_t& end) const {
    std::tm tm_buf{};
    localtime_r(&day, &tm_buf);
    tm_buf.tm_hour = 0;
    tm_buf.tm_min = 0;
    tm_buf.tm_sec = 0;
    start = std::mktime(&tm_buf);
    end = start + 24 * 60 * 60;
}

std::vector<WorkLogRow> Database::load_work_log_for_day(time_t day) {
    std::vector<WorkLogRow> rows;

    time_t day_start, day_end;
    day_bounds(day, day_start, day_end);

    std::string sql = "SELECT title, path, color, source_task_id, start_time, end_time, completed_at FROM work_log "
                       "WHERE start_time >= ? AND start_time < ? ORDER BY start_time ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(day_start));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(day_end));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* path_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* color_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int source_task_id = sqlite3_column_int(stmt, 3);
        time_t start_time = static_cast<time_t>(sqlite3_column_int64(stmt, 4));
        time_t end_time = static_cast<time_t>(sqlite3_column_int64(stmt, 5));
        time_t completed_at = static_cast<time_t>(sqlite3_column_int64(stmt, 6));

        rows.push_back({
            title_ptr ? title_ptr : "",
            path_ptr ? path_ptr : "",
            color_ptr ? color_ptr : "",
            source_task_id,
            start_time,
            end_time,
            completed_at
        });
    }
    sqlite3_finalize(stmt);
    return rows;
}

bool Database::insert_repeated_task(int generator_id, int weekday_mask, int count_per_day) {
    std::string sql = "INSERT OR REPLACE INTO repeated_tasks "
                       "(generator_id, weekday_mask, count_per_day, last_spawned_date) "
                       "VALUES (?, ?, ?, '');";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] insert_repeated_task: failed to prepare statement for id " << generator_id);
        return false;
    }

    sqlite3_bind_int(stmt, 1, generator_id);
    sqlite3_bind_int(stmt, 2, weekday_mask);
    sqlite3_bind_int(stmt, 3, count_per_day);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] insert_repeated_task failed for id " << generator_id);
    }
    return ok;
}

bool Database::remove_repeated_task(int generator_id) {
    std::string sql = "DELETE FROM repeated_tasks WHERE generator_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] remove_repeated_task: failed to prepare statement for id " << generator_id);
        return false;
    }

    sqlite3_bind_int(stmt, 1, generator_id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] remove_repeated_task failed for id " << generator_id);
    }
    return ok;
}

bool Database::update_last_spawned(int generator_id, const std::string& date) {
    std::string sql = "UPDATE repeated_tasks SET last_spawned_date = ? WHERE generator_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] update_last_spawned: failed to prepare statement for id " << generator_id);
        return false;
    }

    sqlite3_bind_text(stmt, 1, date.c_str(), static_cast<int>(date.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, generator_id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] update_last_spawned failed for id " << generator_id);
    }
    return ok;
}

std::vector<RepeatedTaskRow> Database::load_repeated_tasks() {
    std::vector<RepeatedTaskRow> rows;
    std::string sql = "SELECT generator_id, weekday_mask, count_per_day, last_spawned_date FROM repeated_tasks;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int generator_id = sqlite3_column_int(stmt, 0);
        int weekday_mask = sqlite3_column_int(stmt, 1);
        int count_per_day = sqlite3_column_int(stmt, 2);
        const char* date_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

        rows.push_back({ generator_id, weekday_mask, count_per_day, date_ptr ? date_ptr : "" });
    }
    sqlite3_finalize(stmt);
    return rows;
}

bool Database::set_project_color(int project_root_id, const std::string& color) {
    std::string sql = "INSERT OR REPLACE INTO project_colors (project_root_id, color) VALUES (?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] set_project_color: failed to prepare statement for id " << project_root_id);
        return false;
    }

    sqlite3_bind_int(stmt, 1, project_root_id);
    sqlite3_bind_text(stmt, 2, color.c_str(), static_cast<int>(color.size()), SQLITE_TRANSIENT);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] set_project_color failed for id " << project_root_id);
    }
    return ok;
}

bool Database::clear_project_color(int project_root_id) {
    std::string sql = "DELETE FROM project_colors WHERE project_root_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] clear_project_color: failed to prepare statement for id " << project_root_id);
        return false;
    }

    sqlite3_bind_int(stmt, 1, project_root_id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] clear_project_color failed for id " << project_root_id);
    }
    return ok;
}

std::vector<ProjectColorRow> Database::load_project_colors() {
    std::vector<ProjectColorRow> rows;
    std::string sql = "SELECT project_root_id, color FROM project_colors;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int project_root_id = sqlite3_column_int(stmt, 0);
        const char* color_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        rows.push_back({ project_root_id, color_ptr ? color_ptr : "" });
    }
    sqlite3_finalize(stmt);
    return rows;
}

bool Database::mark_work_log_completed(int source_task_id, time_t completed_at) {
    std::string sql = "UPDATE work_log SET completed_at = ? WHERE source_task_id = ? AND completed_at = 0;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        DB_ERR("[Database] mark_work_log_completed: failed to prepare statement for task " << source_task_id);
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(completed_at));
    sqlite3_bind_int(stmt, 2, source_task_id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);

    if (!ok) {
        DB_ERR("[Database] mark_work_log_completed failed for task " << source_task_id);
    }
    return ok;
}

std::vector<CompletedTaskSummary> Database::load_completed_tasks_for_day(time_t day) {
    std::vector<CompletedTaskSummary> rows;

    time_t day_start, day_end;
    day_bounds(day, day_start, day_end);

    // Grouped by source_task_id, not by title/path — a generator's
    // spawned instances can share an identical title and path, so
    // grouping by name would incorrectly merge separate days' instances
    // of the same habit into one row. title/path/color are taken from
    // whichever row happens to be picked for the group (MIN(id) just
    // makes that deterministic) — every segment sharing a source_task_id
    // was written from the same snapshot anyway, so they're identical
    // across the group regardless of which one SQLite picks.
    std::string sql =
        "SELECT title, path, color, SUM(end_time - start_time) AS total_seconds, MAX(completed_at) AS completed_at "
        "FROM work_log "
        // completed_at = 0 (not yet completed) is always outside any
        // real day's [day_start, day_end) range, so this excludes
        // still-in-progress segments without needing a separate check.
        "WHERE completed_at >= ? AND completed_at < ? "
        "GROUP BY source_task_id "
        "ORDER BY completed_at ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(day_start));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(day_end));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* path_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* color_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        long total_seconds = static_cast<long>(sqlite3_column_int64(stmt, 3));
        time_t completed_at = static_cast<time_t>(sqlite3_column_int64(stmt, 4));

        rows.push_back({
            title_ptr ? title_ptr : "",
            path_ptr ? path_ptr : "",
            color_ptr ? color_ptr : "",
            total_seconds,
            completed_at
        });
    }
    sqlite3_finalize(stmt);
    return rows;
}
