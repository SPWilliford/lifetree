#ifndef TASKATTRIBUTES_HPP
#define TASKATTRIBUTES_HPP
#include <memory>
#include <unordered_map>
#include "engine/Database.hpp"

class ITreeController;

// Home for optional things a task can have beyond its title — starting
// with repeating, with room to grow a second small table alongside
// repeated_tasks (e.g. a date/time attribute) rather than needing a new,
// differently-named component every time another attribute type shows up.
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

private:
    std::shared_ptr<Database> m_db;
    ITreeController& m_projects;
    std::unordered_map<int, RepeatedTaskRow> m_generators;

    // Shared by mark_repeating() (so marking something gives immediate
    // feedback instead of waiting for the next scan) and run_spawn_scan()
    // (so every generator gets the same check, in a loop).
    void spawn_if_due(int id, RepeatedTaskRow& row);
};
#endif
