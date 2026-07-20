#include "engine/TaskAttributes.hpp"
#include "engine/TreeController.hpp"
#include <ctime>

namespace {
    std::string format_date(const std::tm& tm_buf) {
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
        return buf;
    }
}

TaskAttributes::TaskAttributes(std::shared_ptr<Database> db, ITreeController& projects)
    : m_db(std::move(db)), m_projects(projects) {}

void TaskAttributes::load() {
    m_generators.clear();
    for (auto& row : m_db->load_repeated_tasks()) {
        m_generators[row.generator_id] = row;
    }
}

bool TaskAttributes::is_generator(int id) const {
    return m_generators.find(id) != m_generators.end();
}

void TaskAttributes::mark_repeating(int id, int weekday_mask, int count_per_day) {
    if (!m_db->insert_repeated_task(id, weekday_mask, count_per_day)) return;
    m_generators[id] = RepeatedTaskRow{ id, weekday_mask, count_per_day, "" };

    // Immediate feedback rather than making the user wait for the next
    // scan (today, that's app restart) to see anything happen.
    spawn_if_due(id, m_generators[id]);
}

void TaskAttributes::unmark_repeating(int id) {
    if (!m_db->remove_repeated_task(id)) return;
    m_generators.erase(id);
}

void TaskAttributes::spawn_if_due(int id, RepeatedTaskRow& row) {
    if (!m_projects.contains(id)) return; // generator itself is gone

    time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    std::string today = format_date(tm_buf);
    int today_bit = 1 << tm_buf.tm_wday; // tm_wday: Sunday=0 .. Saturday=6

    if (row.last_spawned_date == today) return;      // already spawned today
    if ((row.weekday_mask & today_bit) == 0) return;  // not due today

    // A generator's children are never anything but its own spawned
    // instances — safe to clear all of them before respawning.
    for (int child_id : m_projects.children_of(id)) {
        m_projects.remove(child_id);
    }

    std::string title = m_projects.get_title(id);
    for (int i = 0; i < row.count_per_day; ++i) {
        m_projects.add(id, title);
    }

    if (m_db->update_last_spawned(id, today)) {
        row.last_spawned_date = today;
    }
}

void TaskAttributes::run_spawn_scan() {
    for (auto& [id, row] : m_generators) {
        spawn_if_due(id, row);
    }
}
