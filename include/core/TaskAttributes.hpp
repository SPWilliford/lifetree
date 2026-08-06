#ifndef TASKATTRIBUTES_HPP
#define TASKATTRIBUTES_HPP
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <sigc++/signal.h>
#include "core/Database.hpp"

class TreeController;

// A point-in-time copy of everything display and history need to know
// about a task. Deliberately a value, not a reference to a live node:
// it stays valid and correct after the node itself is edited or deleted,
// which is what lets worked time be recorded for a task that no longer
// exists. Empty title means "no such task."
struct TaskSnapshot {
    std::string title;
    std::string path;
    std::string color;

    // Which top-level project this task belongs to. Captured here rather
    // than looked up later because that lookup needs the node to still
    // exist — and the whole point of a snapshot is to outlive it. This is
    // the only durable key tying recorded time back to a project: title
    // and path are display text, and source_task_id dangles once the task
    // is completed and removed. -1 when there's no project root.
    int project_root_id = -1;
};

// Home for optional things a task can have beyond its title — started
// with repeating (repeated_tasks), now also project_colors, rather than
// needing a new, differently-named component every time another
// attribute type shows up.
//
// A generator is any projects-tree node with a row in repeated_tasks —
// that row IS the flag; there's no separate is_generator column on the
// node itself. A generator's children are never anything but its own
// spawned instances, which is what makes run_spawn_scan() safe to just
// clear all of them before respawning, no extra bookkeeping needed.
class TaskAttributes {
public:
    TaskAttributes(std::shared_ptr<Database> db, TreeController& projects);

    // Pulls every repeated_tasks row into the in-memory cache. Call once
    // at startup, after the projects tree itself has loaded.
    void load();

    bool is_generator(int id) const;

    // The stored schedule for a generator, so an editor can open showing
    // what's actually set rather than a fresh default. weekday_mask is 0
    // when id isn't a generator — check is_generator() to tell that from a
    // real row, though a mask of 0 would never repeat anyway.
    RepeatedTaskRow repeat_settings(int id) const;

    // INSERT OR REPLACE semantics — calling this again on an existing
    // generator updates its schedule and resets last_spawned_date, so
    // the new schedule takes effect on the next scan.
    void mark_repeating(int id, int weekday_mask, int count_per_day, bool accumulates);

    // Only removes the repeat config — whatever instances currently
    // exist under id are left alone, now ordinary untracked tasks.
    void unmark_repeating(int id);

    // A sequential node's descendants are worked in order: only the first
    // incomplete one is a candidate for the backlog, and the rest stay
    // hidden until it's done. Lets a chapter be broken into sections
    // without all of them competing for attention at once.
    //
    // Marks the PARENT — "these children are ordered" — not the children.
    void mark_sequential(int id);
    void unmark_sequential(int id);
    bool is_sequential(int id) const;

    // Descendants of a sequential ancestor that aren't next in line. These
    // are real tasks, deliberately withheld: the backlog skips them, and
    // nothing else should treat them as absent.
    //
    // Returned as a set because the caller is filtering a list against it.
    std::unordered_set<int> blocked_tasks() const;

    // Checks every generator against today's date and weekday, and
    // spawns fresh instances for any that are due and haven't already
    // spawned today.
    void run_spawn_scan();

    // The top-level project a node belongs to — the ancestor sitting
    // directly under the hidden root. Returns -1 if id doesn't exist, or
    // if id IS the hidden root, which isn't a project and has nothing to
    // attribute to. Colors, and now recorded time, both hang off this.
    int project_root_of(int id) const;

    // Colors are set on a top-level project only, and inherited by
    // walking up to find it — a leaf never stores its own copy. Returns
    // "" if id's project (or id itself, if id doesn't exist) has none set.
    std::string get_color(int id) const;

    // No is_generator-style existence check needed here — any id works,
    // set_project_color is meant to be called with a top-level project's
    // own id specifically (enforced at the call site, not here).
    void set_project_color(int project_root_id, const std::string& color);
    void clear_project_color(int project_root_id);

    // Everything about a task worth copying out of the tree, in one go.
    // Lives here rather than in each panel because the path rule is
    // generator semantics, not a display detail: an instance's immediate
    // parent, if it's a generator, has an identical title (that's how
    // spawning works), so showing it would just duplicate the title —
    // the path skips straight to that generator's own ancestors instead.
    // Was independently reimplemented in two view files before.
    TaskSnapshot snapshot(int id) const;

    // Fires after anything above that changes what a row should display —
    // a color or a repeat status. Panels use this to know when an
    // already-bound, already-visible row needs to be re-rendered, since
    // nothing else tells them that on its own.
    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;
    TreeController& m_projects;
    std::unordered_map<int, RepeatedTaskRow> m_generators;

    // Nodes whose children are ordered. Membership is the whole flag.
    std::unordered_set<int> m_sequential;

    // Walks a sequential node's subtree in position order, adding every
    // task after the first to out.
    void collect_blocked(int node_id, bool& found_first, std::unordered_set<int>& out) const;
    std::unordered_map<int, std::string> m_project_colors;
    sigc::signal<void()> m_changed;

    // Shared by mark_repeating() (so marking something gives immediate
    // feedback instead of waiting for the next scan) and run_spawn_scan()
    // (so every generator gets the same check, in a loop).
    void spawn_if_due(int id, RepeatedTaskRow& row);
};
#endif
