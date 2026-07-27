#ifndef TASKATTRIBUTES_HPP
#define TASKATTRIBUTES_HPP
#include <memory>
#include <unordered_map>
#include <sigc++/signal.h>
#include "engine/Database.hpp"

class ITreeController;

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
    TaskAttributes(std::shared_ptr<Database> db, ITreeController& projects);

    // Pulls every repeated_tasks row into the in-memory cache. Call once
    // at startup, after the projects tree itself has loaded.
    void load();

    bool is_generator(int id) const;

    // INSERT OR REPLACE semantics — calling this again on an existing
    // generator updates its schedule and resets last_spawned_date, so
    // the new schedule takes effect on the next scan.
    void mark_repeating(int id, int weekday_mask, int count_per_day);

    // Only removes the repeat config — whatever instances currently
    // exist under id are left alone, now ordinary untracked tasks.
    void unmark_repeating(int id);

    // Checks every generator against today's date and weekday, and
    // spawns fresh instances for any that are due and haven't already
    // spawned today.
    void run_spawn_scan();

    // Colors are set on a top-level project only, and inherited by
    // walking up to find it — a leaf never stores its own copy. Returns
    // "" if id's project (or id itself, if id doesn't exist) has none set.
    std::string get_color(int id) const;

    // No is_generator-style existence check needed here — any id works,
    // set_project_color is meant to be called with a top-level project's
    // own id specifically (enforced at the call site, not here).
    void set_project_color(int project_root_id, const std::string& color);
    void clear_project_color(int project_root_id);

    // Fires after anything above that changes what a row should display —
    // a color or a repeat status. Panels use this to know when an
    // already-bound, already-visible row needs to be re-rendered, since
    // nothing else tells them that on its own.
    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;
    ITreeController& m_projects;
    std::unordered_map<int, RepeatedTaskRow> m_generators;
    std::unordered_map<int, std::string> m_project_colors;
    sigc::signal<void()> m_changed;

    // Shared by mark_repeating() (so marking something gives immediate
    // feedback instead of waiting for the next scan) and run_spawn_scan()
    // (so every generator gets the same check, in a loop).
    void spawn_if_due(int id, RepeatedTaskRow& row);
};
#endif
