#ifndef DAY_HPP
#define DAY_HPP

#include <memory>
#include <string>

#include <sigc++/signal.h>

#include "core/Database.hpp"

// The working day: when it starts and ends, in minutes since midnight.
//
// Rows are sparse and inherit: a day with no row of its own takes the most
// recent earlier day's, so setting the hours once covers every day after.
class Day {
public:
    explicit Day(std::shared_ptr<Database> db);

    // Call once at startup.
    void load();

    // Today's hours, own or inherited. Check defined() before using them.
    DayHoursRow hours() const;

    // Writes today's row.
    void set_hours(int start_minutes, int end_minutes);

    // Drops today's row, so today inherits again.
    void clear_hours();

    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;

    // Mutable cache keyed on the civil day, which can turn while the app is
    // open; every read checks the date first.
    mutable std::string m_cached_date;
    mutable DayHoursRow m_cached;

    void ensure_today() const;

    sigc::signal<void()> m_changed;
};

#endif
