#include "core/Day.hpp"

#include "core/Clock.hpp"

Day::Day(std::shared_ptr<Database> db) : m_db(std::move(db)) {}

void Day::load() {
    // Blanked, not filled: the first read does the load.
    m_cached_date.clear();
    m_cached = DayHoursRow{};
}

void Day::ensure_today() const {
    const std::string now_date = clock_util::today();
    if (m_cached_date == now_date) return;

    m_cached = m_db->load_day_hours(now_date);
    m_cached_date = now_date;
}

DayHoursRow Day::hours() const {
    ensure_today();
    return m_cached;
}

void Day::set_hours(int start_minutes, int end_minutes) {
    const std::string now_date = clock_util::today();
    if (!m_db->set_day_hours(now_date, start_minutes, end_minutes)) return;

    m_cached_date = now_date;
    m_cached.date = now_date;
    m_cached.start_minutes = start_minutes;
    m_cached.end_minutes = end_minutes;
    m_changed.emit();
}

void Day::clear_hours() {
    if (!m_db->clear_day_hours(clock_util::today())) return;

    // Re-read rather than blanked: today inherits again.
    m_cached_date.clear();
    m_changed.emit();
}
