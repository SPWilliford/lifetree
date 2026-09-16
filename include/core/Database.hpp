#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "core/Tree.hpp"

// One segment of worked time, recorded when it ends. title/path/color are a
// snapshot: the node may be gone by the time this is read back.
// completed_at is 0 until the task is completed.
struct WorkLogRow {
    std::string title;
    std::string path;
    std::string color;
    int source_task_id;   // dangles once the node is gone
    int project_root_id;  // -1 if none, or on rows written before the column
    time_t start_time;
    time_t end_time;
    time_t completed_at;
};

// One completed task's segments summed.
struct CompletedTaskSummary {
    std::string title;
    std::string path;
    std::string color;
    long total_seconds;
    time_t completed_at;
};

// One completion event. Outlives the node; title/path/color/project_root_id
// are a snapshot. date is the local civil day 'YYYY-MM-DD'.
struct TaskCompletionRow {
    int node_id = -1;
    std::string date;
    std::string title;
    std::string path;
    std::string color;
    int project_root_id = -1;
    time_t completed_at = 0;
};

struct SeedRow {
    int node_id = -1;
    std::string seed;
};

// One day's working hours in minutes since midnight. Rows are sparse; a day
// without one inherits the most recent earlier day's.
struct DayHoursRow {
    static constexpr int NO_HOURS = -1;

    std::string date;
    int start_minutes = NO_HOURS;
    int end_minutes = NO_HOURS;

    bool defined() const { return start_minutes != NO_HOURS && end_minutes != NO_HOURS; }
};

// A date a task is tied to. date is 'YYYY-MM-DD' (sorts as text); times are
// minutes since midnight. An end without a start is never stored.
struct TaskDateRow {
    static constexpr int NO_TIME = -1;

    int node_id = -1;
    std::string date;
    int time_start = NO_TIME;
    int time_end = NO_TIME;
};

// A repeat mark on a node. weekday_mask bit i = tm_wday i (Sunday = 0).
// Defaults matter: a value-initialized row means "repeats never".
struct RepeatedTaskRow {
    int node_id = -1;
    int weekday_mask = 0;
    int count_per_day = 1;
};

struct ProjectColorRow {
    int project_root_id;
    std::string color;
};

// A life node's share of its parent, 0..100. Every node except the root has
// a row — see Priority::normalize.
struct LifeWeightRow {
    int node_id;
    double weight;
};

// One project-to-leaf link. See Priority for what each share feeds.
struct ProjectLinkRow {
    int project_root_id;
    int leaf_id;
    double project_share;
    double goal_share;
};

// SQLite access. Reads return empty on failure; writes return false and
// log to stderr. Only the constructor throws.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // --- trees ---
    std::vector<Row> load(TreeType type);
    int insert(TreeType type, int parent_id, int position, std::string_view title);
    bool set_position(TreeType type, int id, int position);
    void insert_root(TreeType type, std::string_view title);
    bool write_title(TreeType type, int id, std::string_view title);
    bool remove(TreeType type, int id);

    // An empty seed deletes the row.
    bool set_seed(TreeType type, int id, std::string_view seed);
    std::vector<SeedRow> load_seeds(TreeType type);

    // --- work log ---
    // Always inserts with completed_at = 0.
    void insert_work_log(const WorkLogRow& row);

    // day: any instant within the target day.
    std::vector<WorkLogRow> load_work_log_for_day(time_t day);

    // Stamps every open segment sharing source_task_id.
    bool mark_work_log_completed(int source_task_id, time_t completed_at);

    // Grouped by source_task_id, filtered on completed_at.
    std::vector<CompletedTaskSummary> load_completed_tasks_for_day(time_t day);

    // Sum of open (completed_at = 0) segments for one task, across days.
    long open_work_seconds(int source_task_id);

    // --- completions ---
    bool insert_completion(const TaskCompletionRow& row);
    std::vector<TaskCompletionRow> load_completions_for_date(const std::string& date);

    // --- day hours ---
    bool set_day_hours(const std::string& date, int start_minutes, int end_minutes);
    bool clear_day_hours(const std::string& date);

    // This day's own row, or the most recent earlier day's; all NO_HOURS
    // when nothing has ever been set.
    DayHoursRow load_day_hours(const std::string& date);

    // --- task attributes (row existence is the flag) ---
    bool insert_sequential(int node_id);
    bool remove_sequential(int node_id);
    std::vector<int> load_sequential();

    bool insert_container(int node_id);
    bool remove_container(int node_id);
    std::vector<int> load_containers();

    // NO_TIME stores as NULL.
    bool set_task_date(int node_id, const std::string& date, int time_start, int time_end);
    bool clear_task_date(int node_id);
    std::vector<TaskDateRow> load_task_dates();

    bool insert_repeated_task(int node_id, int weekday_mask, int count_per_day);
    bool remove_repeated_task(int node_id);
    std::vector<RepeatedTaskRow> load_repeated_tasks();

    // --- priority ---
    bool set_project_link(int project_root_id, int leaf_id, double project_share,
                          double goal_share);
    bool clear_project_link(int project_root_id, int leaf_id);
    std::vector<ProjectLinkRow> load_project_links();

    bool set_life_weight(int node_id, double weight);
    std::vector<LifeWeightRow> load_life_weights();

    bool set_project_color(int project_root_id, const std::string& color);
    bool clear_project_color(int project_root_id);
    std::vector<ProjectColorRow> load_project_colors();

private:
    // Bumped whenever a versioned step is added to migrate().
    static constexpr int SCHEMA_VERSION = 4;

    sqlite3* m_db = nullptr;

    std::string_view table_name(TreeType type) const;
    std::string_view seed_table_name(TreeType type) const;

    // Throws on failure. Schema work only.
    void execute(const std::string& sql);

    // In this order, from the constructor only.
    void create_schema();  // every CREATE TABLE; safe to repeat
    void migrate();        // may assume create_schema() has run

    // Both require foreign keys OFF — the constructor arranges it.
    void rebuild_trees_with_autoincrement();
    void clear_spawned_instances();

    bool has_table(const std::string& name);
    bool has_column(const std::string& table, const std::string& column);

    // PRAGMA user_version; 0 on a database from before versioning.
    int schema_version();
    void set_schema_version(int version);
};

#endif
