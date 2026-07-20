#ifndef WORKLOG_HPP
#define WORKLOG_HPP
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <ctime>
#include "engine/Database.hpp"

// Owns both the *current* work session (start/stop, the one thing that
// can be active at a time) and the *permanent* history of completed
// sessions. These turned out to be the same responsibility once
// SchedulePanel's local session bookkeeping was pulled out of the view —
// not two components glued together.
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

    // Permanently records a finished session. title/path are a snapshot,
    // taken by the caller — meant to be called right before the
    // underlying tree node is deleted, since that's the last moment its
    // title and ancestry are still knowable.
    void record_completion(std::string_view title, std::string_view path, time_t start, time_t end);
    std::vector<WorkLogRow> entries_for_day(time_t day);

    // Empties the current-session slot without recording anything. For
    // after record_completion() — the session is now permanently in the
    // history, so nothing should still be reading it back out of here as
    // if it were still "current."
    void clear_session();

private:
    std::shared_ptr<Database> m_db;

    bool m_active = false;
    int m_task_id = -1;
    time_t m_start = 0;
    time_t m_end = 0;
};
#endif
