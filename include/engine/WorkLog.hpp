#ifndef WORKLOG_HPP
#define WORKLOG_HPP
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <ctime>
#include "engine/Database.hpp"

// Owns both the *current* work session (start/stop, the one thing that
// can be active at a time) and the *permanent* history of segments —
// every pause, and the final stretch right before Complete, gets
// recorded as its own row the moment it ends. A task's segments stay
// "open" (completed_at == 0 — see Database.hpp) until the task is
// actually completed, at which point they're all stamped together and
// collapse into a single summary for anything reviewing completed work.
class WorkLog {
public:
    explicit WorkLog(std::shared_ptr<Database> db) : m_db(std::move(db)) {}

    // Returns false (no-op) if a session is already active — this is
    // where "can't swap the active task mid-session" actually lives now.
    bool start_session(int task_id);
    void end_active_session();

    bool has_active_session() const { return m_active; }
    int active_task_id() const { return m_task_id; }
    time_t session_start() const { return m_start; }
    // Only meaningful once the session has ended — while still active,
    // callers wanting a live end point should use the current time
    // themselves rather than read this.
    time_t session_end() const { return m_end; }

    // Permanently records one segment of worked time — every time a
    // segment ends, whether that's a pause or the final one right before
    // Complete. title/path/color are a snapshot, taken by the caller —
    // for the final segment, that must happen right before the
    // underlying tree node is deleted, since that's the last moment its
    // title, ancestry, and project color are still knowable.
    // source_task_id ties this segment back to the task it belongs to
    // (title/path alone can't do that — a generator's spawned instances
    // can share an identical title and path).
    void record_segment(std::string_view title, std::string_view path, std::string_view color, int source_task_id, time_t start, time_t end);

    // Called once, when a task is actually completed — stamps every one
    // of its not-yet-completed segments (see record_segment above) with
    // completed_at, so they collapse into a single row when
    // entries_for_completed_day groups them back together.
    bool mark_completed(int source_task_id, time_t completed_at);

    // Every segment recorded today, completed or not — what the
    // timeline draws as bands. "Was this worked today" doesn't care
    // whether the task it belongs to is finished yet.
    std::vector<WorkLogRow> entries_for_day(time_t day);

    // One row per task actually completed on the given day, each a sum
    // across all of that task's segments — what Completed Today
    // displays. Grouped by completed_at, not by any segment's own
    // start_time, since a task's segments can span multiple days if it
    // was paused overnight.
    std::vector<CompletedTaskSummary> entries_for_completed_day(time_t day);

    // Empties the current-session slot without recording anything. For
    // after record_segment() — that segment is now permanently in the
    // history, so nothing should still be reading it back out of here as
    // if it were still "current." Called after both a pause and a
    // Complete, since both write a durable segment now.
    void clear_session();

private:
    std::shared_ptr<Database> m_db;

    bool m_active = false;
    int m_task_id = -1;
    time_t m_start = 0;
    time_t m_end = 0;
};
#endif
