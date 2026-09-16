#include "core/Work.hpp"

#include "core/TreeController.hpp"

Work::Work(std::shared_ptr<Database> db, TreeController& projects, TaskAttributes& attributes)
    : m_db(std::move(db)), m_projects(projects), m_attributes(attributes) {
    // The worked task vanished — deleted in the tree. Bank its time and end
    // the session.
    //
    // Here rather than in a panel: a stranded session isn't a display
    // problem. It blocks every future start(), and the controls that could
    // end it were disabled by the same deletion.
    //
    // The snapshot from start() is what makes this recoverable — the signal
    // arrives after the node is gone.
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

    // Bank first, so the final segment exists before the stamp sweeps up
    // every segment for this task. Only if the session is actually this
    // task's — completing something else mustn't attribute that time.
    if (m_active && m_task_id == task_id) {
        bank_active_segment();
    }

    // A no-op when the task was never worked: nothing matches, so
    // nothing appears in Completed Today. Independent of the tree, so
    // it's safe either side of the removal below.
    m_db->mark_work_log_completed(task_id, std::time(nullptr));

    // Asked BEFORE the record, and the record written BEFORE any removal:
    // both read the live node. record_completion takes the snapshot that
    // outlives it, and recurs() walks ancestors that a delete would take.
    const bool recurring = m_attributes.recurs(task_id);
    m_attributes.record_completion(task_id);

    // A recurring task is finished for today, not finished with. Leaving
    // the node is the whole point: the routine's structure — its order, its
    // colour, its steps — is authored once and stands there tomorrow.
    if (!recurring) m_projects.remove(task_id);

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

    m_db->insert_work_log(WorkLogRow{.title = m_snapshot.title,
                                     .path = m_snapshot.path,
                                     .color = m_snapshot.color,
                                     .source_task_id = m_task_id,
                                     .project_root_id = m_snapshot.project_root_id,
                                     .start_time = m_start,
                                     .end_time = std::time(nullptr),
                                     .completed_at = 0});
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

long Work::recorded_seconds(int task_id) {
    return m_db->open_work_seconds(task_id);
}

long Work::active_seconds() const {
    if (!m_active) return 0;
    return static_cast<long>(std::time(nullptr) - m_start);
}
