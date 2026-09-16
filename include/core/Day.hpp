#ifndef DAY_HPP
#define DAY_HPP
#include <memory>
#include <string>

#include <sigc++/signal.h>

#include "core/Database.hpp"

// The working day: when it starts, when it ends.
//
// Small on purpose, and its own component rather than another concern on
// TaskAttributes — which is about nodes, and this is about dates. It's also
// the place the morning check-in, the evening review and the day's notes go
// when they arrive, so it earns the file before it needs it.
//
// Why this exists at all: logged time has no meaning without a denominator.
// Four hours is either most of a day or a fraction of one, and until the day
// has edges the app can only measure against twenty-four hours or a guess.
class Day {
public:
    Day(std::shared_ptr<Database> db);

    // Call once at startup.
    void load();

    // Today's hours — its own if set, otherwise inherited from the most
    // recent day that set any. Check defined() before using the figures.
    DayHoursRow hours() const;

    // Any day's, by 'YYYY-MM-DD'. Review reads this per day; nothing else
    // needs it yet.
    DayHoursRow hours_for(const std::string& date) const;

    // Writes today's, which every later day then inherits until one of them
    // sets its own. Both in minutes since midnight.
    void set_hours(int start_minutes, int end_minutes);

    // Drops today's row, so today falls back to whatever it would have
    // inherited. Not the same as setting no hours — there is no way to say
    // "today has no working day" and no need for one yet.
    void clear_hours();

    // How far through the working day it is now, 0.0 to 1.0. Exactly 0
    // before it starts and 1 after it ends, and 0 when no day is defined —
    // the caller checks defined() to tell "not started" from "not set".
    double elapsed_fraction() const;

    sigc::connection connect_changed(sigc::slot<void()> slot) { return m_changed.connect(slot); }

private:
    std::shared_ptr<Database> m_db;

    // Today's answer, cached because the panel reads it every draw. Mutable
    // for the same reason TaskAttributes' completion counts are: the civil
    // day can turn while the app is open, and every read checks the date
    // first so the answer is right afterwards rather than stuck on
    // yesterday's.
    mutable std::string m_cached_date;
    mutable DayHoursRow m_cached;

    void ensure_today() const;

    sigc::signal<void()> m_changed;
};
#endif
