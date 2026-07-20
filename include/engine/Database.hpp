#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <sqlite3.h>
#include <string>
#include <ctime>
#include <vector>
#include <string_view>
#include "model/Entities.hpp"

struct Row {
    int id;
    int parent_id;
    int position;
    std::string title;
};

// One completed work session, permanently recorded. title/path are a
// snapshot taken at completion time — the tree node itself is gone by
// then, so this is the only remaining record of what it was and where
// it lived.
struct WorkLogRow {
    std::string title;
    std::string path;
    time_t start_time;
    time_t end_time;
};

class Database {
private:
    sqlite3* m_db = nullptr;

    std::string_view table_name(TreeType type) const;
    void execute(const std::string& sql);

public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::vector<Row> load(TreeType type);
    int insert(TreeType type, int parent_id, int position, std::string_view title);
    void insert_root(TreeType type, std::string_view title);
    void write_title(TreeType type, int id, std::string_view title);
    void remove(TreeType type, int id);

    void insert_work_log(std::string_view title, std::string_view path, time_t start_time, time_t end_time);
    // day: any time_t within the target day (local time) — the day's
    // midnight-to-midnight boundaries are computed from it.
    std::vector<WorkLogRow> load_work_log_for_day(time_t day);
};

#endif
