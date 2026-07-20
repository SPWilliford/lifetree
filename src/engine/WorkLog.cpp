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

void WorkLog::record_completion(std::string_view title, std::string_view path, time_t start, time_t end) {
    m_db->insert_work_log(title, path, start, end);
}

std::vector<WorkLogRow> WorkLog::entries_for_day(time_t day) {
    return m_db->load_work_log_for_day(day);
}
