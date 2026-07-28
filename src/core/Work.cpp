#include "core/Work.hpp"
#include "core/TreeController.hpp"

Work::Work(std::shared_ptr<Database> db, TreeController& projects, TaskAttributes& attributes)
    : m_db(std::move(db)), m_projects(projects), m_attributes(attributes)
{
    // If the task being worked disappears from the tree — deleted
    // directly in TreePanel, or cleared as a stale generator instance —
    // bank its time and end the session rather than leaving a session
    // pointing at a node that no longer exists.
    //
    // This has to live here rather than in a panel: a stranded active
    // session isn't a display problem. It blocks every future start()
    // and, before this, kept a phantom band growing on the timeline with
    // no way left to stop it, since the controls that could have ended
    // it had already been disabled by the same deletion.
    //
    // The snapshot taken at start() is what makes this recoverable at
    // all. This signal arrives *after* the node is already gone, so
    // there is nothing left to read a title or path from by now.
    m_projects.connect_changed([this]() {
        if (m_active && !m_projects.contains(m_task_id)) {
            pause();
        }
    });
}

bool Work::start(int task_id) {
    if (m_active) return false;
    if (!m_projects.contains(task_id)) return false;

    m_active = true;
    m_task_id = task_id;
    m_start = std::time(nullptr);
    m_snapshot = m_attributes.snapshot(task_id);
    return true;
}

bool Work::pause() {
    if (!bank_active_segment()) return false;
    m_changed.emit();
    return true;
}

bool Work::complete(int task_id) {
    if (!m_projects.contains(task_id)) return false;

    // Bank first, so the final segment exists before the stamp below
    // sweeps up every segment belonging to this task. Only if the active
    // session is actually this task's — completing something else while
    // a session runs must not attribute that time to it.
    if (m_active && m_task_id == task_id) {
        bank_active_segment();
    }

    // A no-op when the task was never worked: nothing matches, so
    // nothing appears in Completed Today. Independent of the tree, so
    // it's safe either side of the removal below.
    m_db->mark_work_log_completed(task_id, std::time(nullptr));

    m_projects.remove(task_id);
    m_changed.emit();
    return true;
}

bool Work::bank_active_segment() {
    if (!m_active) return false;

    // Refresh the snapshot while the node is still readable, so a rename
    // mid-session is reflected. Falls back to what start() captured when
    // it isn't — which is the case this whole mechanism exists for.
    if (m_projects.contains(m_task_id)) {
        m_snapshot = m_attributes.snapshot(m_task_id);
    }

    m_db->insert_work_log(WorkLogRow{
        .title = m_snapshot.title,
        .path = m_snapshot.path,
        .color = m_snapshot.color,
        .source_task_id = m_task_id,
        .project_root_id = m_snapshot.project_root_id,
        .start_time = m_start,
        .end_time = std::time(nullptr),
        .completed_at = 0
    });
    clear_session();
    return true;
}

void Work::clear_session() {
    m_active = false;
    m_task_id = -1;
    m_start = 0;
    m_snapshot = {};
}

std::vector<WorkLogRow> Work::entries_for_day(time_t day) {
    return m_db->load_work_log_for_day(day);
}

std::vector<CompletedTaskSummary> Work::entries_for_completed_day(time_t day) {
    return m_db->load_completed_tasks_for_day(day);
}
