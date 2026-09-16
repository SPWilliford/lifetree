#include "core/Work.hpp"

#include "core/TreeController.hpp"

Work::Work(std::shared_ptr<Database> db, TreeController& projects, TaskAttributes& task_attributes)
    : m_db(std::move(db)), m_projects(projects), m_task_attributes(task_attributes) {
    // The worked task was deleted from the tree: bank its time and end the
    // session, or every later start() is blocked with no control to end it.
    m_projects.connect_changed([this]() {
        if (m_active && !m_projects.contains(m_task_id)) pause();
    });
}

bool Work::start(int task_id) {
    if (m_active) return false;
    if (!m_projects.contains(task_id)) return false;

    m_active = true;
    m_task_id = task_id;
    m_start = std::time(nullptr);
    m_snapshot = m_task_attributes.snapshot(task_id);
    return true;
}

bool Work::pause() {
    if (!bank_active_segment()) return false;
    m_changed.emit();
    return true;
}

bool Work::complete(int task_id) {
    if (!m_projects.contains(task_id)) return false;

    // Bank first, so the final segment exists before the stamp sweeps every
    // segment for this task. Only if the session is this task's.
    if (m_active && m_task_id == task_id) bank_active_segment();

    m_db->mark_work_log_completed(task_id, std::time(nullptr));

    // Both read the live node, so both before any removal.
    const bool recurring = m_task_attributes.recurs(task_id);
    m_task_attributes.record_completion(task_id);

    if (!recurring) m_projects.remove(task_id);

    m_changed.emit();
    return true;
}

bool Work::bank_active_segment() {
    if (!m_active) return false;

    if (m_projects.contains(m_task_id)) {
        m_snapshot = m_task_attributes.snapshot(m_task_id);
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
