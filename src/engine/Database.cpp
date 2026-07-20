#include "engine/Database.hpp"
#include <stdexcept>
#include <ctime>
#include <iostream>

// Flip to 1 to see a message whenever a write silently fails.
#define DATABASE_DEBUG 0
#if DATABASE_DEBUG
#define DB_LOG(x) std::cerr << x << std::endl
#else
#define DB_LOG(x)
#endif

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
            "  start_time INTEGER NOT NULL, "
            "  end_time INTEGER NOT NULL);");
}

Database::~Database() {
    if (m_db) sqlite3_close(m_db);
}

std::string_view Database::table_name(TreeType type) const {
    return (type == TreeType::LIFE) ? "life_tree" : "projects_tree";
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
        DB_LOG("[Database] insert failed for table " << table);
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(m_db));
}

void Database::write_title(TreeType type, int id, std::string_view title) {
    std::string sql = "UPDATE " + std::string(table_name(type)) + " SET title = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        DB_LOG("[Database] write_title failed for id " << id);
    }
    sqlite3_finalize(stmt);
}

void Database::remove(TreeType type, int id) {
    std::string sql = "DELETE FROM " + std::string(table_name(type)) + " WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_int(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        DB_LOG("[Database] remove failed for id " << id);
    }
    sqlite3_finalize(stmt);
}

void Database::insert_root(TreeType type, std::string_view title) {
    auto table = table_name(type);
    std::string sql = "INSERT OR IGNORE INTO " + std::string(table) + " (id, parent_id, position, title) VALUES (0, NULL, 0, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            DB_LOG("[Database] insert_root failed for table " << table);
        }
        sqlite3_finalize(stmt);
    }
}

void Database::insert_work_log(std::string_view title, std::string_view path, time_t start_time, time_t end_time) {
    std::string sql = "INSERT INTO work_log (title, path, start_time, end_time) VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.data(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(start_time));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(end_time));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        DB_LOG("[Database] insert_work_log failed for title " << title);
    }
    sqlite3_finalize(stmt);
}

std::vector<WorkLogRow> Database::load_work_log_for_day(time_t day) {
    std::vector<WorkLogRow> rows;

    // Midnight-to-midnight in local time, computed from the given instant.
    std::tm tm_buf{};
    localtime_r(&day, &tm_buf);
    tm_buf.tm_hour = 0;
    tm_buf.tm_min = 0;
    tm_buf.tm_sec = 0;
    time_t day_start = std::mktime(&tm_buf);
    time_t day_end = day_start + 24 * 60 * 60;

    std::string sql = "SELECT title, path, start_time, end_time FROM work_log "
                       "WHERE start_time >= ? AND start_time < ? ORDER BY start_time ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return rows;

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(day_start));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(day_end));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* title_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* path_ptr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        time_t start_time = static_cast<time_t>(sqlite3_column_int64(stmt, 2));
        time_t end_time = static_cast<time_t>(sqlite3_column_int64(stmt, 3));

        rows.push_back({
            title_ptr ? title_ptr : "",
            path_ptr ? path_ptr : "",
            start_time,
            end_time
        });
    }
    sqlite3_finalize(stmt);
    return rows;
}
