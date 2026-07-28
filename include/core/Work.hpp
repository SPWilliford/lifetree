#ifndef WORK_HPP
#define WORK_HPP
#include <memory>
#include <vector>
#include <ctime>
#include <sigc++/signal.h>
#include "core/Database.hpp"
#include "core/TaskAttributes.hpp"

class TreeController;

// Owns the act of working on a task: starting, pausing, and completing,
// plus the permanent history of segments those produce.
//
// The three public verbs below are whole operations, not steps. Each one
// leaves the session slot and the database consistent with each other on
// its own, so no caller has to know the correct order to do anything in.
// That ordering used to live in SchedulePanel — snapshot before remove,
// record before stamp, clear after record — where getting it wrong was
// both easy and silent.
//
// A task's segments stay "open" (completed_at == 0 — see Database.hpp)
// until the task is actually completed, at which point they're all
// stamped together and collapse into one summary for anything reviewing
// completed work. A task that's worked and then deleted rather than
// completed keeps its banked segments open forever, which is right: the
// time was really spent, but nothing was ever finished.
class Work {
public:
    Work(std::shared_ptr<Database> db, TreeController& projects, TaskAttributes& attributes);

    // --- The working lifecycle ---

    // Begins working task_id, snapshotting it immediately. That snapshot
    // is what makes the rest of this class safe: the title, path, and
    // color needed to record worked time are captured while the node is
    // definitely still there, so a rename or a deletion mid-session
    // can't leave a segment with nothing to label it.
    //
    // Returns false without starting anything if a session is already
    // active, or if task_id isn't a real task.
    bool start(int task_id);

    // Ends the active session, banking its elapsed time as a durable
    // segment so it survives the app closing or the task sitting paused
    // overnight. Returns false if nothing was active.
    bool pause();

    // Banks any time currently being worked on task_id, stamps every one
    // of its segments as completed so they collapse into a single
    // Completed Today entry, and removes it from the tree. The only
    // place a task is actually deleted as a *completion*, as opposed to
    // being discarded from the tree directly.
    //
    // Safe on a task that was never worked at all — it just completes
    // with no recorded time. Returns false if task_id isn't a real task.
    bool complete(int task_id);

    // --- Queries ---

    bool has_active_session() const { return m_active; }
    int active_task_id() const { return m_task_id; }
    time_t session_start() const { return m_start; }

    // Every segment recorded today, completed or not — what the timeline
    // draws as bands. "Was this worked today" doesn't care whether the
    // task it belongs to is finished yet.
    std::vector<WorkLogRow> entries_for_day(time_t day);

    // One row per task actually completed on the given day, each a sum
    // across all of that task's segments — what Completed Today
    // displays. Grouped by completed_at rather than by any segment's own
    // start_time, since a task's segments can span multiple days if it
    // was paused overnight.
    std::vector<CompletedTaskSummary> entries_for_completed_day(time_t day);

    // Fires whenever recorded history changes — a segment banked, or a
    // task completed. Subscribers should re-read whatever they show;
    // like the tree's own signal, it deliberately doesn't say what
    // changed. Not emitted by start(), which records nothing.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;
    TreeController& m_projects;
    TaskAttributes& m_attributes;

    bool m_active = false;
    int m_task_id = -1;
    time_t m_start = 0;

    // Captured at start(), refreshed on the way out if the node is still
    // readable. The fallback copy is the point: it's the only thing left
    // to label a segment with once the task is gone.
    TaskSnapshot m_snapshot;

    // Writes the active session's elapsed time as a segment and empties
    // the slot. Emits nothing — pause() and complete() each signal once,
    // after their whole operation is done, rather than mid-way through.
    bool bank_active_segment();

    void clear_session();

    sigc::signal<void()> m_changed;
};
#endif
