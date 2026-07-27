#include "engine/WorkLog.hpp"

bool WorkLog::start_session(int task_id) {
    if (m_active) return false;
    m_active = true;
    m_task_id = task_id;
    m_start = std::time(nullptr);
    m_end = 0;
    return true;
}

void WorkLog::end_active_session() {
    if (!m_active) return;
    m_active = false;
    m_end = std::time(nullptr);
}

void WorkLog::record_segment(std::string_view title, std::string_view path, std::string_view color, int source_task_id, time_t start, time_t end) {
    m_db->insert_work_log(title, path, color, source_task_id, start, end);
}

bool WorkLog::mark_completed(int source_task_id, time_t completed_at) {
    return m_db->mark_work_log_completed(source_task_id, completed_at);
}

void WorkLog::clear_session() {
    m_active = false;
    m_task_id = -1;
    m_start = 0;
    m_end = 0;
}

std::vector<WorkLogRow> WorkLog::entries_for_day(time_t day) {
    return m_db->load_work_log_for_day(day);
}

std::vector<CompletedTaskSummary> WorkLog::entries_for_completed_day(time_t day) {
    return m_db->load_completed_tasks_for_day(day);
}
