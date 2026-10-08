#ifndef DAY_HPP
#define DAY_HPP

#include <memory>
#include <string>
#include <string_view>

#include <sigc++/signal.h>

#include "core/Database.hpp"

// The working day: when it starts and ends, in minutes since midnight, and
// the notes kept alongside it.
//
// Hours rows are sparse and inherit: a day with no row of its own takes the
// most recent earlier day's, so setting the hours once covers every day
// after.
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

    // General notes, kept across days and not tied to any date. Writing
    // them doesn't emit: the page that edits them is their only reader.
    const std::string& notes() const { return m_notes; }
    void set_notes(std::string_view text);

    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;

    // Mutable cache keyed on the civil day, which can turn while the app is
    // open; every read checks the date first.
    mutable std::string m_cached_date;
    mutable DayHoursRow m_cached;

    std::string m_notes;

    void ensure_today() const;

    sigc::signal<void()> m_changed;
};

#endif
