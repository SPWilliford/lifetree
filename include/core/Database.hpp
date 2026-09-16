#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "core/Tree.hpp"

struct Row {
    int id;
    int parent_id;
    int position;
    std::string title;
};

// One segment of worked time, recorded the moment it ends — a pause or the
// final Complete. title/path/color are a snapshot: the node may be gone by
// the time anyone reads this back.
//
// completed_at stays 0 until the task itself is completed; a task can
// accumulate several segments (pause, resume, pause) before that happens.
struct WorkLogRow {
    std::string title;
    std::string path;
    std::string color;

    // Ties segments to the node they were worked against. Title and path
    // can't: two tasks in different projects may share both.
    int source_task_id;

    // The only column durable enough to total time by project. -1 on rows
    // written before it existed, and on time with no project root.
    int project_root_id;

    time_t start_time;
    time_t end_time;
    time_t completed_at;
};

// One completed task's segments collapsed into a single total — one row per
// task, not one per pause/resume.
struct CompletedTaskSummary {
    std::string title;
    std::string path;
    std::string color;
    long total_seconds;
    time_t completed_at;
};

// One completion event: this task, finished on this civil day.
//
// The durable record of what got done, independent of whether the timer
// was ever run — work_log only knows about time that was measured.
//
// title/path/color/project_root_id are a snapshot, same as WorkLogRow's and
// for the same reason: the node may be renamed, reparented or deleted
// afterwards, and the record has to survive all three. node_id is the live
// key while the node exists and dangles once it doesn't.
//
// date is the local civil day 'YYYY-MM-DD', which is what today's count is
// asked against; completed_at is the instant, for ordering within a day.
struct TaskCompletionRow {
    int node_id = -1;
    std::string date;
    std::string title;
    std::string path;
    std::string color;
    int project_root_id = -1;
    time_t completed_at = 0;
};

// A node's seed: the precise, intention-style statement of what it means,
// as distinct from the short title that labels it.
//
// Its own table rather than a column, and sparse: most nodes have no seed
// yet, and an empty string in every row would say nothing. A row exists
// where somebody wrote one.
struct SeedRow {
    int node_id = -1;
    std::string seed;
};

// One day's working hours, in minutes since midnight — the same unit as
// TaskDateRow, so the two compare without conversion.
//
// A row per date rather than one row of settings, and rows are sparse: a day
// with no row of its own inherits the most recent day that has one. So
// setting 9 to 6 once covers every day after it, and changing it on a
// Friday leaves the record of what the previous weeks actually were.
//
// That history is the point. Review can only say "you have started later
// every day this week" if each day's intended start was written down at the
// time, and a single settings row can never be made retroactive.
struct DayHoursRow {
    std::string date;
    int start_minutes = NO_HOURS;
    int end_minutes = NO_HOURS;

    // No day has ever been defined. Distinct from a zero, which is midnight.
    static constexpr int NO_HOURS = -1;

    bool defined() const { return start_minutes != NO_HOURS && end_minutes != NO_HOURS; }
};

// A date a task is tied to, and optionally a time on that date.
//
// date is TEXT 'YYYY-MM-DD' because a date is a civil day, not an instant —
// as text it sorts and compares correctly and is directly comparable to
// sqlite's date('now','localtime').
//
// The times are minutes since midnight, so a band's height is arithmetic
// with no date embedded to go stale. NO_TIME on time_start means due that
// day with no particular hour. An end without a start is never stored.
struct TaskDateRow {
    static constexpr int NO_TIME = -1;

    int node_id = -1;
    std::string date;
    int time_start = NO_TIME;
    int time_end = NO_TIME;
};

// One repeat mark. weekday_mask bit i is set for the day whose
// std::tm::tm_wday == i (Sunday=0), so checking "is today included" needs
// no conversion. count_per_day is how many times it is outstanding on a day
// it falls due.
//
// The mark governs the marked node's whole subtree. Nothing is spawned or
// copied, so there is no spawn history to keep.
struct RepeatedTaskRow {
    // Defaults matter: repeat_settings returns a value-initialized row for
    // an unmarked node, and a mask of 0 correctly means "repeats never".
    int node_id = -1;
    int weekday_mask = 0;
    int count_per_day = 1;
};

struct ProjectColorRow {
    int project_root_id;
    std::string color;
};

// A life node's share of its parent, 0..100. Complete, not sparse: every
// node except the root has a row. See Priority.
struct LifeWeightRow {
    int node_id;
    double weight;
};

// One project-to-goal association, weighted in both directions. `weight` is
// the project's share, `goal_share` the goal's — see Priority for what each
// one feeds.
struct ProjectLinkRow {
    int project_root_id;
    int leaf_id;
    double weight;
    double goal_share;
};

class Database {
private:
    // Bumped whenever a versioned step is added to migrate(). A fresh
    // database is stamped with this and never migrates.
    static constexpr int SCHEMA_VERSION = 4;

    sqlite3* m_db = nullptr;

    std::string_view table_name(TreeType type) const;
    std::string_view seed_table_name(TreeType type) const;
    void execute(const std::string& sql);

    // Called in this order, only from the constructor. Keeping them apart
    // is what guarantees migrate() never runs against a table that doesn't
    // exist yet.
    void create_schema();  // every CREATE TABLE; safe to run repeatedly
    void migrate();        // may assume create_schema() has run

    // Rebuilds both tree tables with AUTOINCREMENT, preserving every id.
    // Requires foreign keys OFF — see the definition.
    void rebuild_trees_with_autoincrement();

    // Deletes every subtree hanging off a repeat mark. One-time: those
    // nodes were spawned instances, and a marked node's children mean
    // authored structure now. Requires foreign keys OFF — see the
    // definition.
    void clear_spawned_instances();

    // Only answerable before create_schema() runs.
    bool has_table(const std::string& name);

    bool has_column(const std::string& table, const std::string& column);

    // PRAGMA user_version. For migrations a column check can't express —
    // changing what values in an existing column MEAN. 0 on any database
    // written before versioning started.
    int schema_version();
    void set_schema_version(int version);

    // Midnight-to-midnight local, from any instant within the day. Shared
    // by every "for this day" query so they can't drift apart.
    void day_bounds(time_t day, time_t& start, time_t& end) const;

public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::vector<Row> load(TreeType type);
    int insert(TreeType type, int parent_id, int position, std::string_view title);

    // Moves a node within its current parent. Needed by anything that
    // inserts BETWEEN siblings rather than after them, which has to push
    // the later ones down to make room.
    bool set_position(TreeType type, int id, int position);

    // INSERT OR REPLACE. An empty seed removes the row instead of storing
    // one — no seed and a blank seed are the same state, and only one of
    // them should be representable.
    bool set_seed(TreeType type, int id, std::string_view seed);
    std::vector<SeedRow> load_seeds(TreeType type);
    void insert_root(TreeType type, std::string_view title);
    bool write_title(TreeType type, int id, std::string_view title);
    bool remove(TreeType type, int id);

    // Always inserts with completed_at = 0; completion is a separate step.
    // Takes the whole row because half its fields are adjacent
    // interchangeable ints, which is the kind of swap a compiler can't catch.
    void insert_work_log(const WorkLogRow& row);

    // day: any time_t within the target day. Returns every segment
    // regardless of completion — bands care about "worked today", not "done".
    std::vector<WorkLogRow> load_work_log_for_day(time_t day);

    // Stamps every not-yet-completed segment sharing source_task_id.
    bool mark_work_log_completed(int source_task_id, time_t completed_at);

    // Grouped by source_task_id, filtered on completed_at rather than any
    // segment's start_time — a task's segments can span days if it was
    // paused overnight.
    std::vector<CompletedTaskSummary> load_completed_tasks_for_day(time_t day);

    // Every segment ever recorded for one task, summed. Deliberately not
    // day-scoped: a task paused overnight and picked up again carries time
    // on both days, and the elapsed figure has to be the whole of it.
    //
    // Safe against a dangling id because the tree tables are AUTOINCREMENT
    // — a completed task's id is never handed to a later node.
    // Banked seconds not yet accounted to a completion — see the
    // definition. Not a lifetime total: a recurring task's node outlives
    // every one of its completions.
    long open_work_seconds(int source_task_id);

    // Deliberately no ON DELETE CASCADE on this table: a completion
    // outlives the node it came from. Same contract as work_log.
    bool insert_completion(const TaskCompletionRow& row);

    // Every completion recorded on one civil day, oldest first. date is
    // 'YYYY-MM-DD' local — the caller says which day, so there's no clock
    // reading buried in here.
    std::vector<TaskCompletionRow> load_completions_for_date(const std::string& date);

    // INSERT OR REPLACE: setting a day's hours twice updates that day.
    bool set_day_hours(const std::string& date, int start_minutes, int end_minutes);
    bool clear_day_hours(const std::string& date);

    // This day's hours, or the most recent earlier day's if it has none of
    // its own. An all-NO_HOURS row when nothing has ever been set.
    DayHoursRow load_day_hours(const std::string& date);

    // Row existence IS the flag for these three; there's nothing else to
    // store. Containers are stored rather than actions because containers
    // are the small set.
    bool insert_sequential(int node_id);
    bool remove_sequential(int node_id);
    std::vector<int> load_sequential();

    bool insert_container(int node_id);
    bool remove_container(int node_id);
    std::vector<int> load_containers();

    // INSERT OR REPLACE. time_start/time_end of NO_TIME store as SQL NULL.
    bool set_task_date(int node_id, const std::string& date, int time_start, int time_end);
    bool clear_task_date(int node_id);
    std::vector<TaskDateRow> load_task_dates();

    // INSERT OR REPLACE: re-marking a node updates its schedule in place.
    bool insert_repeated_task(int node_id, int weekday_mask, int count_per_day);
    bool remove_repeated_task(int node_id);
    std::vector<RepeatedTaskRow> load_repeated_tasks();

    bool set_project_link(int project_root_id, int leaf_id, double weight, double goal_share);
    bool clear_project_link(int project_root_id, int leaf_id);
    std::vector<ProjectLinkRow> load_project_links();

    bool set_life_weight(int node_id, double weight);
    std::vector<LifeWeightRow> load_life_weights();

    bool set_project_color(int project_root_id, const std::string& color);
    bool clear_project_color(int project_root_id);
    std::vector<ProjectColorRow> load_project_colors();
};

#endif
