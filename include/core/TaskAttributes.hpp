#ifndef TASKATTRIBUTES_HPP
#define TASKATTRIBUTES_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"

class TreeController;

// A copy of what identifies a task, taken while the node exists. Empty title
// means "no such task".
struct TaskSnapshot {
    std::string title;
    std::string path;
    std::string color;
    int project_root_id = -1;
};

// Everything a projects-tree node has beyond its title. Each attribute is
// row-existence in its own table.
class TaskAttributes {
public:
    TaskAttributes(std::shared_ptr<Database> db, TreeController& projects);

    // Call once at startup, after the projects tree has loaded.
    void load();

    // --- Repeating ---
    //
    // A repeat mark makes the marked node's whole subtree recurring; nothing
    // is spawned or copied. Rows are inherited by walking up, and the
    // NEAREST row governs, so a row below the routine's top is an override.

    // Inside a recurring subtree: its own row or any ancestor's.
    bool recurs(int id) const;

    // The nearest row at or above id. node_id is -1 when nothing repeats.
    RepeatedTaskRow governing_repeat(int id) const;

    // Has a row of its own rather than an inherited one.
    bool has_own_repeat(int id) const;

    // The HIGHEST ancestor carrying a row, or the node itself; -1 if it
    // doesn't recur. Only this node offers the schedule grid.
    int repeat_root_of(int id) const;
    bool is_repeat_root(int id) const;

    // This node's own row, or a zeroed row (a 0 mask never repeats).
    RepeatedTaskRow repeat_settings(int id) const;

    // Writes one node's line of the schedule grid. A changed mask CASCADES:
    // every override below is dropped. A mask equal to what the node would
    // inherit removes its row instead of storing a duplicate.
    void apply_repeat(int id, int weekday_mask, int count_per_day);

    // Removes this node's row and every one below it. Completions stay.
    void unmark_repeating(int id);

    // --- Completions ---
    //
    // A durable record of something finished on a civil day, written for
    // every task whether or not it recurs or was timed.

    // Call while the node still exists — the row carries a snapshot.
    void record_completion(int id);

    int completions_today(int id) const;

    // The governing mark's count_per_day, or 1 if the node doesn't recur.
    int target_count(int id) const;

    // Done for today. Only ever true inside a recurring subtree.
    bool satisfied_today(int id) const;

    // --- Ordering ---
    //
    // Marks the PARENT: its children happen in order, and only the first
    // outstanding descendant is a candidate.
    void mark_sequential(int id);
    void unmark_sequential(int id);
    bool is_sequential(int id) const;

    // --- Container or action ---
    //
    // A container organises and never enters the task list. Invariant:
    // containers only sit above actions. mark_container walks UP,
    // mark_action walks DOWN, and both cascade silently.
    bool is_container(int id) const;
    void mark_container(int id);
    void mark_action(int id);

    // --- Dates ---
    //
    // A date gates: a task isn't a candidate until that day (and start
    // time, if any) arrives. A past date still passes. Inherits down.
    bool is_available(int id) const;
    bool has_date(int id) const;
    bool date_is_today(int id) const;

    // Whether this node's OWN date is an earlier day or a start time
    // earlier today. An all-day date today is not past.
    bool date_has_passed(int id) const;

    TaskDateRow date_settings(int id) const;
    void set_date(int id, const std::string& date, int time_start, int time_end);
    void clear_date(int id);

    // Today's tasks carrying a start time, soonest first. Availability is
    // not applied.
    std::vector<TaskDateRow> scheduled_today() const;

    // --- Derived views ---

    // Leaves that are actions, available, due today if they recur, short of
    // today's quota, and not held behind an earlier sequential step.
    std::unordered_set<int> eligible_today() const;

    // Descendants of a sequential ancestor that aren't next in line.
    std::unordered_set<int> blocked_tasks() const;

    // The ancestor directly under the hidden root. -1 for the root or an
    // unknown id.
    int project_root_of(int id) const;

    // Colors live on a top-level project and are inherited. "" if none.
    std::string get_color(int id) const;
    void set_project_color(int project_root_id, const std::string& color);
    void clear_project_color(int project_root_id);

    TaskSnapshot snapshot(int id) const;

    // Something changed how a row should look.
    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;
    TreeController& m_projects;

    std::unordered_map<int, RepeatedTaskRow> m_repeats;
    std::unordered_set<int> m_sequential;
    std::unordered_set<int> m_containers;
    std::unordered_map<int, TaskDateRow> m_dates;
    std::unordered_map<int, std::string> m_project_colors;

    // Today's completion counts. Mutable cache keyed on the civil day, which
    // can turn while the app is open; every read checks the date first.
    mutable std::string m_completions_date;
    mutable std::unordered_map<int, int> m_completions;
    void ensure_today() const;

    void clear_repeats_below(int id);
    void collect_blocked(int node_id, bool& found_first, std::unordered_set<int>& out) const;

    sigc::signal<void()> m_changed;
};

#endif
