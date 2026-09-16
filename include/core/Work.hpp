#ifndef WORK_HPP
#define WORK_HPP

#include <ctime>
#include <memory>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"
#include "core/TaskAttributes.hpp"

class TreeController;

// Starting, pausing and completing a task, and the segments those record.
// Each verb is a whole operation: the session slot and the database are
// consistent when it returns.
//
// Segments stay open (completed_at == 0) until the task is completed.
class Work {
public:
    Work(std::shared_ptr<Database> db, TreeController& projects, TaskAttributes& task_attributes);

    // False if a session is already active or task_id isn't a node.
    bool start(int task_id);

    // Banks the active session as a segment. False if nothing was active.
    bool pause();

    // Banks open time, stamps every segment completed, records the
    // completion, and removes the node unless it recurs. False if task_id
    // isn't a node.
    bool complete(int task_id);

    bool has_active_session() const { return m_active; }
    int active_task_id() const { return m_task_id; }
    time_t session_start() const { return m_start; }

    // Every segment recorded on the day containing `day`, completed or not.
    std::vector<WorkLogRow> entries_for_day(time_t day);

    // One row per task completed on that day, summed across its segments.
    std::vector<CompletedTaskSummary> entries_for_completed_day(time_t day);

    // Banked seconds since this task was last completed, across days.
    // Excludes the live session — see active_seconds.
    long recorded_seconds(int task_id);

    // The running session's elapsed seconds; 0 when nothing is active.
    long active_seconds() const;

    // Recorded history changed. Not emitted by start(), which records nothing.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    std::shared_ptr<Database> m_db;
    TreeController& m_projects;
    TaskAttributes& m_task_attributes;

    bool m_active = false;
    int m_task_id = -1;
    time_t m_start = 0;

    // Taken at start() and refreshed while the node is readable, so a
    // segment can be labelled after the node is deleted.
    TaskSnapshot m_snapshot;

    // Writes the active session and empties the slot. Emits nothing.
    bool bank_active_segment();
    void clear_session();

    sigc::signal<void()> m_changed;
};

#endif
