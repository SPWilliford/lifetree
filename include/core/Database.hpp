#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <sqlite3.h>
#include <string>
#include <ctime>
#include <vector>
#include <string_view>
#include "core/Tree.hpp"

struct Row {
    int id;
    int parent_id;
    int position;
    std::string title;
};

// One segment of worked time, permanently recorded the moment it ends —
// whether that's a pause or the final Complete. title/path/color are a
// snapshot taken when recorded, since the tree node may be gone by the
// time anyone reads this back (color is "" if the project had none set).
// completed_at is 0 until the task this segment belongs to is actually
// completed — a task can accumulate several segments (pause, resume,
// pause again) all sitting with completed_at == 0 before that happens.
// source_task_id ties segments back to the specific task instance that
// produced them — title/path alone can't do this, since a generator's
// spawned instances can share an identical title and path.
struct WorkLogRow {
    std::string title;
    std::string path;
    std::string color;
    int source_task_id;

    // The top-level project this time belongs to, captured when the
    // segment was written. The only column that survives well enough to
    // total time by project: title and path are display snapshots that a
    // rename invalidates, and source_task_id points at a row that
    // completing the task deletes. -1 on rows written before this column
    // existed, and on time that had no project root.
    int project_root_id;

    time_t start_time;
    time_t end_time;
    time_t completed_at;
};

// One completed task's daily summary — every one of its segments (see
// WorkLogRow) collapsed into a single total, the shape Completed Today
// actually wants (one row per task, not one per pause/resume segment).
struct CompletedTaskSummary {
    std::string title;
    std::string path;
    std::string color;
    long total_seconds;
    time_t completed_at;
};

// One generator's repeat config. weekday_mask bit i is set if the
// generator should spawn on the day whose std::tm::tm_wday == i
// (Sunday=0 .. Saturday=6) — chosen to match tm_wday directly so
// checking "is today included" needs no conversion. last_spawned_date
// is "YYYY-MM-DD", empty until the first scan touches this generator.
struct RepeatedTaskRow {
    int generator_id;
    int weekday_mask;
    int count_per_day;
    std::string last_spawned_date;
};

// A color set on a top-level project — sub-tasks inherit it by walking
// up to find their project root, rather than every node storing its own
// copy. See TaskAttributes::get_color().
struct ProjectColorRow {
    int project_root_id;
    std::string color;
};

// An explicit weight the user placed on a life tree node: how much of its
// parent's priority it takes, on a 0..100 scale. Sparse on purpose — a row
// exists only where the user actually made a choice, and nodes without one
// share out whatever their weighted siblings left over. See Priority.
struct LifeWeightRow {
    int node_id;
    double weight;
};

// An association between a top-level project and a life tree leaf it
// serves, with a rough weight for how much of that project is really about
// that leaf. Many-to-many: a project can serve several leaves, and a leaf
// can be served by several projects. See Priority.
struct ProjectLinkRow {
    int project_root_id;
    int leaf_id;
    double weight;
};

class Database {
private:
    sqlite3* m_db = nullptr;

    std::string_view table_name(TreeType type) const;
    void execute(const std::string& sql);
    // Used for schema migrations — checking first avoids relying on a
    // thrown-and-caught exception as routine, expected control flow on
    // every single startup.
    bool has_column(const std::string& table, const std::string& column);
    // Midnight-to-midnight in local time, computed from any instant
    // within that day. Shared by every "for this day" query, so they
    // can't independently drift out of sync with each other.
    void day_bounds(time_t day, time_t& start, time_t& end) const;

public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::vector<Row> load(TreeType type);
    int insert(TreeType type, int parent_id, int position, std::string_view title);
    void insert_root(TreeType type, std::string_view title);
    bool write_title(TreeType type, int id, std::string_view title);
    bool remove(TreeType type, int id);

    // Always inserts with completed_at = 0 — completion is a separate
    // step (mark_work_log_completed), not part of recording a segment.
    // A segment gets written here every time one ends, whether that's a
    // pause or the final one right before Complete.
    // Takes the whole row rather than a list of loose arguments — half of
    // them are ints that would sit adjacent and interchangeable in a
    // signature (source_task_id, project_root_id), which is the kind of
    // swap a compiler can't catch. row.completed_at is ignored: a segment
    // is always written open and stamped later by mark_work_log_completed.
    void insert_work_log(const WorkLogRow& row);
    // day: any time_t within the target day (local time) — the day's
    // midnight-to-midnight boundaries are computed from it. Returns
    // every segment for the day regardless of completion status — bands
    // on the timeline care about "was this worked today," not "is the
    // task done yet."
    std::vector<WorkLogRow> load_work_log_for_day(time_t day);

    // Stamps completed_at onto every not-yet-completed segment sharing
    // source_task_id — called once, when the task is actually completed,
    // after its final segment has already been written via
    // insert_work_log above.
    bool mark_work_log_completed(int source_task_id, time_t completed_at);

    // One row per completed task (grouped by source_task_id, durations
    // summed across all its segments), filtered to tasks completed on
    // the given day — completed_at is what's checked against the day
    // boundary, not any segment's own start_time, since a task's
    // segments can span multiple days if it was paused overnight.
    std::vector<CompletedTaskSummary> load_completed_tasks_for_day(time_t day);

    // INSERT OR REPLACE — calling this again on an id that's already a
    // generator updates its config and resets last_spawned_date, so a
    // changed schedule takes effect on the next scan rather than waiting
    // for whatever the old schedule would have done.
    bool insert_repeated_task(int generator_id, int weekday_mask, int count_per_day);
    bool remove_repeated_task(int generator_id);
    bool update_last_spawned(int generator_id, const std::string& date);
    std::vector<RepeatedTaskRow> load_repeated_tasks();

    // INSERT OR REPLACE — same idempotent-update pattern as
    // insert_repeated_task above.
    bool set_project_link(int project_root_id, int leaf_id, double weight);
    bool clear_project_link(int project_root_id, int leaf_id);
    std::vector<ProjectLinkRow> load_project_links();

    bool set_life_weight(int node_id, double weight);
    bool clear_life_weight(int node_id);
    std::vector<LifeWeightRow> load_life_weights();

    bool set_project_color(int project_root_id, const std::string& color);
    bool clear_project_color(int project_root_id);
    std::vector<ProjectColorRow> load_project_colors();
};

#endif
