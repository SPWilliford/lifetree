#ifndef CLOCK_HPP
#define CLOCK_HPP

#include <ctime>
#include <string>

// Local civil time, in the units the rest of core stores: dates as
// 'YYYY-MM-DD' text and times as minutes since midnight.
namespace clock_util {

// Today as 'YYYY-MM-DD'.
std::string today();

// Minutes since local midnight, now.
int minutes_now();

// 1 << tm_wday for today (Sunday = bit 0), matching RepeatedTaskRow's mask.
int today_weekday_bit();

// Local midnight at the start of the day containing `t`. Correct across DST
// transitions.
time_t local_midnight(time_t t);

// Local time components of `t`.
std::tm local_tm(time_t t);

// "HH:MM" <-> minutes since midnight. Blank or unparseable text gives -1
// (the value both TaskDateRow::NO_TIME and DayHoursRow::NO_HOURS use);
// minutes out of range give "".
int parse_hhmm(const std::string& text);
std::string format_hhmm(int minutes);

}  // namespace clock_util

#endif
