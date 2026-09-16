#ifndef WORK_HPP
#define WORK_HPP
#include <ctime>
#include <memory>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"
#include "core/TaskAttributes.hpp"

class TreeController;

// Starting, pausing and completing a task, plus the history of segments
// those produce. The three verbs are whole operations: each leaves the
// session slot and the database consistent on its own.
//
// Segments stay open (completed_at == 0) until the task is completed. A
// task worked then deleted rather than completed keeps its segments open
// forever — the time was spent, but nothing was finished.
class Work {
public:
    Work(std::shared_ptr<Database> db, TreeController& projects, TaskAttributes& attributes);

    // Snapshots task_id immediately, so a rename or deletion mid-session
    // can't leave a segment with nothing to label it. False if a session is
    // already active, or task_id isn't real.
    bool start(int task_id);

    // Banks the active session's elapsed time as a durable segment, so it
    // survives the app closing or the task sitting paused overnight. False
    // if nothing was active.
    bool pause();

    // Banks open time, stamps every segment completed, and records the
    // completion. Removes the node only if it isn't inside a recurring
    // subtree — one of those is done for today, not done with.
    //
    // The only place a task is deleted as a *completion* rather than
    // discarded, and the only place a completion is recorded. Safe on a
    // task never worked. False if not a real task.
    bool complete(int task_id);

    bool has_active_session() const { return m_active; }
    int active_task_id() const { return m_task_id; }
    time_t session_start() const { return m_start; }

    // Every segment recorded on the given day, completed or not.
    std::vector<WorkLogRow> entries_for_day(time_t day);

    // One row per task completed on the given day, summed across all its
    // segments.
    std::vector<CompletedTaskSummary> entries_for_completed_day(time_t day);

    // Banked seconds since this task was last completed, across as many
    // days as that spans. A one-off has never been completed, so that's its
    // whole history; a recurring one starts over at each completion, so
    // three sets of pushups are timed separately.
    //
    // Excludes whatever the live session has run up so far — that's
    // active_seconds, and the two are separate because this one reads the
    // database and the caller wants a live figure every tick.
    long recorded_seconds(int task_id);

    // The running session's elapsed time, 0 when nothing is active. Check
    // active_task_id() first if you care which task it belongs to.
    long active_seconds() const;

    // Recorded history changed; re-read what you show. Not emitted by
    // start(), which records nothing.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    std::shared_ptr<Database> m_db;
    TreeController& m_projects;
    TaskAttributes& m_attributes;

    bool m_active = false;
    int m_task_id = -1;
    time_t m_start = 0;

    // Captured at start(), refreshed on the way out if the node is still
    // readable. The fallback copy is the point: once the task is gone it's
    // the only thing left to label a segment with.
    TaskSnapshot m_snapshot;

    // Writes the active session's elapsed time and empties the slot. Emits
    // nothing — pause() and complete() each signal once, after their whole
    // operation, rather than midway.
    bool bank_active_segment();

    void clear_session();

    sigc::signal<void()> m_changed;
};
#endif
