#include "core/Day.hpp"

#include <algorithm>
#include <ctime>

namespace {
std::string today() {
    time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return buf;
}

// Minutes since midnight, the unit day_hours and TaskDateRow both use.
int minutes_now() {
    time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    return tm_buf.tm_hour * 60 + tm_buf.tm_min;
}
}  // namespace

Day::Day(std::shared_ptr<Database> db) : m_db(std::move(db)) {}

void Day::load() {
    // Blanked rather than filled: the first read does the load, so there is
    // one path for "the cache is stale" instead of two.
    m_cached_date.clear();
    m_cached = DayHoursRow{};
}

void Day::ensure_today() const {
    const std::string now_date = today();
    if (m_cached_date == now_date) return;

    m_cached = m_db->load_day_hours(now_date);
    m_cached_date = now_date;
}

DayHoursRow Day::hours() const {
    ensure_today();
    return m_cached;
}

DayHoursRow Day::hours_for(const std::string& date) const {
    return m_db->load_day_hours(date);
}

void Day::set_hours(int start_minutes, int end_minutes) {
    const std::string now_date = today();
    if (!m_db->set_day_hours(now_date, start_minutes, end_minutes)) return;

    m_cached_date = now_date;
    m_cached.date = now_date;
    m_cached.start_minutes = start_minutes;
    m_cached.end_minutes = end_minutes;
    m_changed.emit();
}

void Day::clear_hours() {
    if (!m_db->clear_day_hours(today())) return;

    // Forced to re-read rather than blanked: dropping today's row doesn't
    // mean today has no hours, it means today inherits again.
    m_cached_date.clear();
    m_changed.emit();
}

double Day::elapsed_fraction() const {
    ensure_today();
    if (!m_cached.defined()) return 0.0;

    const int span = m_cached.end_minutes - m_cached.start_minutes;
    if (span <= 0) return 0.0;  // an end at or before the start divides by nothing

    const double through = static_cast<double>(minutes_now() - m_cached.start_minutes) / span;
    return std::clamp(through, 0.0, 1.0);
}
