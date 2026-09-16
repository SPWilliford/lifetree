#include "core/Clock.hpp"

#include <cstdio>

namespace clock_util {

std::tm local_tm(time_t t) {
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    return tm_buf;
}

std::string today() {
    const std::tm tm_buf = local_tm(std::time(nullptr));
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
    return buf;
}

int minutes_now() {
    const std::tm tm_buf = local_tm(std::time(nullptr));
    return tm_buf.tm_hour * 60 + tm_buf.tm_min;
}

int today_weekday_bit() {
    return 1 << local_tm(std::time(nullptr)).tm_wday;
}

time_t local_midnight(time_t t) {
    std::tm tm_buf = local_tm(t);
    tm_buf.tm_hour = 0;
    tm_buf.tm_min = 0;
    tm_buf.tm_sec = 0;
    // TRAP: -1, not the tm_isdst localtime_r filled in for `t`. On the two
    // DST transition days they disagree at midnight and the result is an
    // hour off.
    tm_buf.tm_isdst = -1;
    return std::mktime(&tm_buf);
}

int parse_hhmm(const std::string& text) {
    int hours = 0, minutes = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &hours, &minutes) != 2) return -1;
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return -1;
    return hours * 60 + minutes;
}

std::string format_hhmm(int minutes) {
    if (minutes < 0 || minutes >= 24 * 60) return "";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

}  // namespace clock_util
