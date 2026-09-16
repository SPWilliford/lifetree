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

// A value, not a reference to a live node: stays correct after the node is
// edited or deleted, which is what lets worked time be recorded for a task
// that no longer exists. Empty title means "no such task".
struct TaskSnapshot {
    std::string title;
    std::string path;
    std::string color;

    // The only durable key tying recorded time back to a project: title and
    // path are display text, and source_task_id dangles once the task is
    // completed and removed. -1 when there's no project root.
    int project_root_id = -1;
};

// Everything a projects-tree node can have beyond its title.
//
// Each flag is stored as row-existence in its own table, so there's no
// column on the node itself and nothing to keep in step.
class TaskAttributes {
public:
    TaskAttributes(std::shared_ptr<Database> db, TreeController& projects);

    // Call once at startup, after the projects tree has loaded.
    void load();

    // --- Repeating ---
    //
    // A repeat mark makes the marked node's whole SUBTREE recurring.
    // Nothing spawns and nothing is copied: the structure you author is the
    // structure you do, once per day it falls due. Completing a leaf inside
    // one records a completion against today rather than deleting the node,
    // so tomorrow it is simply outstanding again.
    //
    // Inherited by walking up, like dates and colors, rather than stored on
    // every node — a step added to a routine next week is part of it
    // without anyone having to remember to mark it. It also can't drift:
    // there's one row to be right about.

    // Whether this node is INSIDE a recurring subtree — its own row or any
    // ancestor's. What completion and eligibility ask.
    bool recurs(int id) const;

    // The row governing this node: its own, or the nearest ancestor's.
    // node_id is -1 when nothing above it repeats.
    RepeatedTaskRow governing_repeat(int id) const;

    // A row of this node's own, rather than one inherited. Only says the
    // schedule was set here explicitly — the grid dims the rows where it
    // wasn't, so "following the routine" and "decided about" can be told
    // apart.
    bool has_own_repeat(int id) const;

    // The top of the routine this node belongs to: the highest ancestor
    // carrying a row, or the node itself. -1 if it doesn't recur. Only this
    // node offers the schedule grid and the mark can only be lifted here.
    int repeat_root_of(int id) const;

    // Whether this node IS that top. What the row's marker asks — a glyph
    // on every step of a routine would read as several routines.
    bool is_repeat_root(int id) const;

    // This exact node's row. A zeroed row when it has none — check
    // has_own_repeat to tell that from a real one (a 0 mask never repeats).
    RepeatedTaskRow repeat_settings(int id) const;

    // One node's line in the schedule grid.
    //
    // Writing a mask CASCADES: every override below is dropped, so the
    // subtree follows this node again. That's what makes "set all of Math
    // to Mon/Wed/Fri" one gesture. Editing only the count leaves the
    // subtree alone, since nothing below it changed.
    //
    // A mask matching what this node would have inherited anyway stores
    // nothing — the row is removed instead, which is how a node gets back
    // to following its parent without a separate control for it.
    void apply_repeat(int id, int weekday_mask, int count_per_day);

    // Ends the routine: this node's row and every one below it. The subtree
    // stays exactly as it is and its nodes become ordinary tasks again,
    // deleted on completion as usual. Completions already recorded stay.
    void unmark_repeating(int id);

    // --- Completions ---
    //
    // A completion is a durable record of something finished on a civil
    // day, written for every task whether or not it recurs and whether or
    // not the timer was ever run. Outlives the node: renaming, reparenting
    // and deleting all leave the record intact.

    // Writes the completion and updates today's count. Call while the node
    // still exists — the row carries a snapshot taken here.
    void record_completion(int id);

    // How many times this node has been completed today. 0 for anything
    // never completed, which is the overwhelmingly common answer.
    int completions_today(int id) const;

    // How many completions today make this node done: the governing mark's
    // count_per_day, or 1 for anything that doesn't recur.
    int target_count(int id) const;

    // Done for today. Only ever true inside a recurring subtree — anywhere
    // else a completed task is a deleted one, so there is nothing to ask.
    bool satisfied_today(int id) const;

    // --- Ordering ---
    //
    // Marks the PARENT — "these children are ordered" — not the children.
    // Only the first incomplete descendant is a backlog candidate.
    void mark_sequential(int id);
    void unmark_sequential(int id);
    bool is_sequential(int id) const;

    // --- Container or action ---
    //
    // A container organises rather than gets done and never enters the
    // backlog; everything else is an action. The invariant is that
    // containers only sit above actions. Neither call is the other's
    // inverse, and that's what keeps the invariant without a validation
    // pass: marking a container walks UP, marking an action walks DOWN.
    // Both cascade silently.
    bool is_container(int id) const;
    void mark_container(int id);
    void mark_action(int id);

    // --- Dates ---
    //
    // A date gates: a task isn't a candidate until that day arrives. Three
    // states from two optional times — date only (available that day), plus
    // a start (available from that moment), plus an end (also occupies a
    // block). A past date still passes, so a missed appointment stays put.
    //
    // Inherits down: dating a node dates its whole subtree.
    bool is_available(int id) const;
    bool has_date(int id) const;

    // Whether a dated node's date is today — the view needs this to tell
    // "at three o'clock" from "Wednesday at three".
    bool date_is_today(int id) const;

    // Whether this node's OWN date has come and gone: an earlier day, or a
    // start time on today that has passed. A dateless node is never past.
    //
    // Its own row rather than an inherited one, because the caller is the
    // row's marker and a marker speaks for the row it sits on.
    //
    // An all-day date today counts as still ahead — it gates nothing more,
    // but it's true all day that this is a today task.
    bool date_has_passed(int id) const;

    TaskDateRow date_settings(int id) const;
    void set_date(int id, const std::string& date, int time_start, int time_end);
    void clear_date(int id);

    // Today's tasks carrying a start time, soonest first. Availability is
    // deliberately not applied: something at three should be visible all
    // morning, which is the point of drawing it.
    std::vector<TaskDateRow> scheduled_today() const;

    // --- Derived views ---

    // A leaf, an action, past its date, due today if it recurs, not already
    // done its quota today, and not held behind an earlier step. Here
    // because the backlog and the tree need one answer.
    std::unordered_set<int> eligible_today() const;

    // Descendants of a sequential ancestor that aren't next in line. Real
    // tasks, deliberately withheld — nothing should treat them as absent.
    std::unordered_set<int> blocked_tasks() const;

    // The ancestor sitting directly under the hidden root. -1 if id doesn't
    // exist or IS the root.
    int project_root_of(int id) const;

    // Colors are set on a top-level project and inherited by walking up; a
    // leaf never stores its own. "" if the project has none.
    std::string get_color(int id) const;
    void set_project_color(int project_root_id, const std::string& color);
    void clear_project_color(int project_root_id);

    TaskSnapshot snapshot(int id) const;

    // Fires when something changes how a row should look. Panels use this
    // to re-render already-bound rows; nothing else tells them.
    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;
    TreeController& m_projects;

    std::unordered_map<int, RepeatedTaskRow> m_repeats;
    std::unordered_set<int> m_sequential;
    std::unordered_set<int> m_containers;
    std::unordered_map<int, TaskDateRow> m_dates;
    std::unordered_map<int, std::string> m_project_colors;

    // Today's completion counts, node id to count. Mutable because the day
    // can turn while the app is open: every read checks the date first and
    // re-reads if it has moved on, which is what makes the backlog correct
    // after midnight rather than stuck on yesterday. Empty date forces a
    // load on first use.
    mutable std::string m_completions_date;
    mutable std::unordered_map<int, int> m_completions;

    // Re-reads the counts if the civil day has changed since they were
    // loaded. Cheap in the normal case: one string compare.
    void ensure_today() const;

    // Drops every repeat row strictly below this node, so the subtree
    // follows it again.
    void clear_repeats_below(int id);

    // Walks a sequential node's subtree in position order, adding every
    // task after the first still-outstanding one to out.
    void collect_blocked(int node_id, bool& found_first, std::unordered_set<int>& out) const;

    sigc::signal<void()> m_changed;
};
#endif
