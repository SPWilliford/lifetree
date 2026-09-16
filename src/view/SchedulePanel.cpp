#include "view/SchedulePanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairomm/surface.h>
#include <glibmm/main.h>
#include <gtkmm/entry.h>
#include <pangomm/layout.h>

#include "core/Clock.hpp"
#include "core/Day.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"
#include "view/Style.hpp"

namespace {
// Drift speed of the timeline.
constexpr double PIXELS_PER_HOUR = 180.0;
constexpr double PIXELS_PER_SECOND = PIXELS_PER_HOUR / 3600.0;
constexpr long QUARTER_HOUR_SECONDS = 15 * 60;

// Must clear the hour labels: tick_length(36) + gap(8) + widest label.
constexpr double CLOCK_LEFT_X = 88.0;

constexpr double CLOCK_RULE_GAP = 20.0;

constexpr double BAND_MIN_WIDTH = 200.0;

constexpr int DOCK_TIMELINE_GAP = 8;

constexpr int DOCK_SPLIT_GAP = 8;

constexpr int ELAPSED_WIDTH_CHARS = 7;

constexpr int TRANSPORT_TOP_GAP = 8;

constexpr double STROKE_WIDTH = 2.0;
// Cairo centers a stroke on its path; a shape meeting a border's outer
// edge must offset by half of it.
constexpr double BORDER_HALF_WIDTH = STROKE_WIDTH / 2.0;

constexpr double CLOCK_PAD_X = 16.0;
constexpr double CLOCK_PAD_Y = 10.0;

// The clock box is sized from this so it holds still all day.
constexpr const char* CLOCK_WIDEST_TIME = "12:00 AM";

// sqrt(3)/2: an equilateral triangle's height over its base.
constexpr double TRIANGLE_ASPECT = 0.8660254;

constexpr double ACCENT_ALPHA = 0.9;

// Half-height of a dashed line's hover target.
constexpr double HOVER_SLACK = 7.0;

constexpr double SCHEDULED_BLOCK_ALPHA = 0.18;

// tm_hour is 24-hour; labels want 12.
struct Hour12 {
    int hour;
    const char* am_pm;
};
Hour12 to_12_hour(const std::tm& tm_buf) {
    int hour = tm_buf.tm_hour % 12;
    if (hour == 0) hour = 12;
    return {hour, (tm_buf.tm_hour < 12) ? "AM" : "PM"};
}

// Opacity rather than visible(false), so the dock doesn't resize. TRAP: an
// opacity-0 widget is still hit-tested; set_has_tooltip is what suppresses
// the tooltip, and it must come after set_tooltip_text, which re-enables it.
void set_control_shown(Gtk::Button& button, bool shown, const char* tooltip) {
    button.set_opacity(shown ? 1.0 : 0.0);
    button.set_sensitive(shown);
    button.set_tooltip_text(tooltip);
    button.set_has_tooltip(shown);
}

std::string elapsed_text(long seconds) {
    if (seconds < 0) seconds = 0;
    const long hours = seconds / 3600;
    const long minutes = (seconds % 3600) / 60;
    const long secs = seconds % 60;

    char buf[24];
    if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", hours, minutes, secs);
    } else {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld", minutes, secs);
    }
    return buf;
}

std::string clock_time(time_t t) {
    std::tm tm_buf = clock_util::local_tm(t);
    const Hour12 h12 = to_12_hour(tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", h12.hour, tm_buf.tm_min, h12.am_pm);
    return buf;
}

// Real time to y. The inverted layout (future above, past below) lives
// entirely in y_for's sign. Built fresh per draw from real elapsed time,
// so a late tick doesn't accumulate drift.
struct TimelineGeometry {
    double center_y;
    time_t now;

    TimelineGeometry(int height, time_t now_) : center_y(height / 2.0), now(now_) {}

    // Minus: a later time sits higher.
    double y_for(time_t t) const {
        return center_y - static_cast<double>(t - now) * PIXELS_PER_SECOND;
    }

    struct Span {
        double y, height;
    };
    // y_for inverts, so end is unconditionally the top edge.
    Span span_for(time_t start, time_t end) const {
        double top = y_for(end);
        return {top, y_for(start) - top};
    }
};

// The clock's rectangle; the rule, the flag and the band columns all hang
// off it.
struct ClockBox {
    double x, y, w, h;

    double center_y() const { return y + h / 2.0; }

    // The planned/actual boundary the whole panel is split on.
    double rule_x() const { return x + w + CLOCK_RULE_GAP; }

    double tip_x() const { return rule_x() + h * TRIANGLE_ASPECT; }
};

// Where the mirrored arrowhead points: the far edge of the band column.
double right_tip_x(const ClockBox& box, int panel_width) {
    return panel_width - BORDER_HALF_WIDTH - box.h * TRIANGLE_ASPECT;
}

struct BandColumn {
    double x;
    double width;
    double center() const { return x + width / 2.0; }
};

// Worked bands: clock tip to arrow tip, with a floor for narrow panels.
BandColumn band_column(int panel_width, const ClockBox& clock) {
    const double x = clock.tip_x();
    const double width = std::max(BAND_MIN_WIDTH, right_tip_x(clock, panel_width) - x);
    return {x, width};
}

// Planned bands: leading edge to the rule, over the ticks and clock.
BandColumn scheduled_column(const ClockBox& clock) {
    return {0.0, clock.rule_x()};
}

// Set before measuring and again before drawing: the tick labels in between
// change the font.
void select_clock_font(const Cairo::RefPtr<Cairo::Context>& cr) {
    cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cr->set_font_size(20);
}

ClockBox clock_box_for(const Cairo::RefPtr<Cairo::Context>& cr, double center_y) {
    select_clock_font(cr);

    Cairo::TextExtents widest;
    cr->get_text_extents(CLOCK_WIDEST_TIME, widest);

    ClockBox box;
    // TRAP: both edges must land on whole pixels; each carries a 2px stroke
    // centred on it, and a fractional x covers three columns at half strength.
    box.w = std::ceil(widest.width + CLOCK_PAD_X * 2);
    box.h = widest.height + CLOCK_PAD_Y * 2;
    box.x = CLOCK_LEFT_X;
    box.y = center_y - box.h / 2.0;
    return box;
}

// The rule's x without a window: it depends only on font metrics. Resizing
// a widget from inside a draw function is how you get a layout loop.
double measure_rule_x() {
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 1, 1);
    auto cr = Cairo::Context::create(surface);
    const ClockBox box = clock_box_for(cr, 0.0);
    return box.rule_x();
}

// The flag's point mirrored against the far edge; sized from the clock box
// so the two stay identical.
void draw_now_arrow(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                    const ClockBox& box, int width, double center_y) {
    const double base_x = width - BORDER_HALF_WIDTH;
    const double point_x = right_tip_x(box, width);

    cr->move_to(base_x, box.y - BORDER_HALF_WIDTH);
    cr->line_to(point_x, center_y);
    cr->line_to(base_x, box.y + box.h + BORDER_HALF_WIDTH);
    cr->close_path();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->fill();
}

// Drawn over bands and ticks, before the flag.
void draw_clock_rule(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                     const ClockBox& box, int height) {
    const double x = box.rule_x();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->move_to(x, 0);
    cr->line_to(x, height);
    cr->stroke();
}

// Opaque, never the accent: the accent's one job is marking now.
void draw_band(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
               const TimelineGeometry& geo, const BandColumn& col, time_t start, time_t end,
               const std::string& color) {
    style::set_source(cr, style::parse_hex(color, palette.band_fallback));

    auto span = geo.span_for(start, end);
    cr->rectangle(col.x, span.y, col.width, span.height);
    cr->fill();
}

// A wash for a span, a dashed rule for a bare time. Dashed rather than
// solid because the ticks share this column. No label: the tooltip has it.
void draw_scheduled_band(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                         const TimelineGeometry& geo, const BandColumn& col, time_t start,
                         time_t end, bool is_block, const std::string& color) {
    const Gdk::RGBA rgba = style::parse_hex(color, palette.band_fallback);

    if (is_block) {
        auto span = geo.span_for(start, end);
        style::set_source(cr, rgba, SCHEDULED_BLOCK_ALPHA);
        cr->rectangle(col.x, span.y, col.width, span.height);
        cr->fill();
        return;
    }

    style::set_source(cr, rgba);
    cr->set_line_width(STROKE_WIDTH);
    const double y = geo.y_for(start);
    std::vector<double> dashes{4.0, 4.0};
    cr->set_dash(dashes, 0.0);
    cr->move_to(col.x, y);
    cr->line_to(col.x + col.width, y);
    cr->stroke();
    cr->unset_dash();
}

// Inset by BORDER_HALF_WIDTH so the stroke isn't clipped.
void draw_panel_border(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                       int width, int height) {
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->rectangle(BORDER_HALF_WIDTH, BORDER_HALF_WIDTH, width - STROKE_WIDTH,
                  height - STROKE_WIDTH);
    cr->stroke();
}

// The now-line, the clock box threaded onto it, and the flag's point standing
// on the rule. Deliberately NOT pixel-snapped, unlike the rule: this line
// drifts continuously and antialiasing is what conveys that.
void draw_now_marker(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                     const ClockBox& box, time_t now) {
    const double center_y = box.center_y();

    std::tm tm_buf = clock_util::local_tm(now);
    Hour12 h12 = to_12_hour(tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", h12.hour, tm_buf.tm_min, h12.am_pm);

    select_clock_font(cr);
    Cairo::TextExtents extents;
    cr->get_text_extents(buf, extents);

    const double rule = box.rule_x();

    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->move_to(0, center_y);
    cr->line_to(box.x, center_y);
    cr->move_to(box.x + box.w, center_y);
    cr->line_to(rule, center_y);
    cr->stroke();

    style::set_source(cr, palette.background);
    cr->rectangle(box.x, box.y, box.w, box.h);
    cr->fill_preserve();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->stroke();

    cr->move_to(rule, box.y - BORDER_HALF_WIDTH);
    cr->line_to(box.tip_x(), center_y);
    cr->line_to(rule, box.y + box.h + BORDER_HALF_WIDTH);
    cr->close_path();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->fill();

    style::set_source(cr, palette.text);
    cr->move_to(box.x + (box.w - extents.width) / 2.0 - extents.x_bearing,
                center_y - extents.height / 2.0 - extents.y_bearing);
    cr->show_text(buf);
}

}  // namespace

SchedulePanel::SchedulePanel(TreeController& projects, Work& work, TaskAttributes& task_attributes,
                             Day& day)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      m_projects(projects),
      m_work(work),
      m_task_attributes(task_attributes),
      m_day(day) {
    initialize_layout();
    refresh_completed_bands();
    refresh_scheduled_bands();
    refresh_date_label();

    m_projects.connect_changed([this]() {
        m_label_refresh.request();
        m_scheduled_refresh.request();
    });

    // A color or a date doesn't touch the tree.

    m_task_attributes.connect_changed([this]() {
        m_label_refresh.request();
        m_scheduled_refresh.request();
    });

    // Work also banks time on its own when a worked task is deleted, which
    // no button handler here sees.
    m_work.connect_changed([this]() {
        m_bands_refresh.request();
        m_label_refresh.request();
    });

    Glib::signal_timeout().connect(sigc::mem_fun(*this, &SchedulePanel::on_timer_tick), 100);
}

void SchedulePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    m_activate_button.add_css_class("dock-activate");
    set_control_shown(m_activate_button, false, "Activate");
    m_activate_button.signal_clicked().connect(
        sigc::mem_fun(*this, &SchedulePanel::on_activate_clicked));

    m_complete_button.add_css_class("dock-complete");
    set_control_shown(m_complete_button, false, "Mark complete");
    m_complete_button.signal_clicked().connect(
        sigc::mem_fun(*this, &SchedulePanel::on_complete_clicked));

    m_elapsed_label.add_css_class("dock-elapsed");
    m_elapsed_label.set_width_chars(ELAPSED_WIDTH_CHARS);
    m_elapsed_label.set_xalign(1.0);

    m_staged_buttons_row.append(m_elapsed_label);
    m_staged_buttons_row.append(m_activate_button);
    m_staged_buttons_row.append(m_complete_button);

    m_dock_date.set_halign(Gtk::Align::START);
    m_dock_date.set_xalign(0.0);

    // Insurance: set_size_request is a minimum, and a wide label would push
    // the dock's split out of line with the rule beneath it.
    m_dock_date.set_ellipsize(Pango::EllipsizeMode::END);

    m_day_button.set_child(m_dock_date);
    m_day_button.set_has_frame(false);
    m_day_button.set_popover(m_day_menu);

    // Rebuilt on every open, so inherited hours show correctly.
    m_day_menu.signal_show().connect(sigc::mem_fun(*this, &SchedulePanel::build_day_menu));
    m_day_button.set_halign(Gtk::Align::FILL);
    m_day_button.set_valign(Gtk::Align::FILL);
    m_day_button.set_vexpand(true);
    m_day_button.add_css_class("day-button");
    m_day_button.set_tooltip_text("The day's start, end and review");

    m_timeline_dock.append(m_day_button);
    m_timeline_dock.append(m_timeline_controls);
    m_timeline_controls.set_vexpand(true);

    // TRAP: explicitly false. A widget with no expand flag inherits one from
    // any child that has it, and the dock would take height off the timeline.
    m_timeline_dock.set_vexpand(false);

    m_timeline_dock.add_css_class("staged-dock");

    m_timeline_dock.set_hexpand(false);
    m_timeline_dock.set_size_request(static_cast<int>(measure_rule_x() - DOCK_SPLIT_GAP / 2.0), -1);

    m_staged_buttons_row.set_halign(Gtk::Align::CENTER);
    m_staged_buttons_row.set_margin_top(TRANSPORT_TOP_GAP);

    for (Gtk::Label* label : {&m_slot_path, &m_slot_title}) {
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_halign(Gtk::Align::FILL);
        label->set_xalign(0.5);
    }

    m_slot_path.add_css_class("dock-path");
    m_slot_title.add_css_class("dock-title");

    m_staged_box.append(m_slot_path);
    m_staged_box.append(m_slot_title);
    m_staged_box.append(m_staged_buttons_row);
    m_staged_box.add_css_class("staged-dock");
    m_staged_box.set_hexpand(true);

    m_dock_row.set_spacing(DOCK_SPLIT_GAP);
    m_dock_row.append(m_timeline_dock);
    m_dock_row.append(m_staged_box);
    m_dock_row.set_margin_bottom(DOCK_TIMELINE_GAP);

    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));

    // after=false, so this runs before the default handler declines.
    m_timeline.set_has_tooltip(true);
    m_timeline.signal_query_tooltip().connect(
        sigc::mem_fun(*this, &SchedulePanel::on_timeline_tooltip), false);

    append(m_dock_row);
    append(m_timeline);
}

void SchedulePanel::draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    const style::Palette palette = style::palette_for(m_timeline);

    style::set_source(cr, palette.background);
    cr->paint();

    time_t now = std::time(nullptr);
    TimelineGeometry geo{height, now};

    // First: everything positions against it.
    const ClockBox clock = clock_box_for(cr, geo.center_y);

    const BandColumn col = band_column(width, clock);

    for (const auto& entry : m_completed_bands) {
        draw_band(cr, palette, geo, col, entry.start_time, entry.end_time, entry.color);
    }

    if (m_work.has_active_session()) {
        draw_band(cr, palette, geo, col, m_work.session_start(), now,
                  m_task_attributes.get_color(m_work.active_task_id()));
    }

    const BandColumn planned = scheduled_column(clock);
    for (const auto& band : m_scheduled_bands) {
        draw_scheduled_band(cr, palette, geo, planned, band.start, band.end, band.is_block,
                            band.color);
    }

    // Every UTC offset in use is a multiple of 15 minutes, so epoch-aligned
    // quarter-hours are wall-clock ones too.
    time_t aligned = now - (now % QUARTER_HOUR_SECONDS);

    int half_span_ticks = static_cast<int>((height / 2.0) / (PIXELS_PER_HOUR / 4.0)) + 2;

    for (int k = -half_span_ticks; k <= half_span_ticks; ++k) {
        time_t tick_time = aligned + k * QUARTER_HOUR_SECONDS;
        double y = geo.y_for(tick_time);
        if (y < -20 || y > height + 20) continue;

        std::tm tm_buf = clock_util::local_tm(tick_time);

        double tick_length;
        bool draw_label = false;
        if (tm_buf.tm_min == 0) {
            tick_length = 36.0;
            draw_label = true;
            style::set_source(cr, palette.tick_major);
        } else if (tm_buf.tm_min == 30) {
            tick_length = 24.0;
            style::set_source(cr, palette.tick_minor);
        } else {
            tick_length = 14.0;
            style::set_source(cr, palette.tick_faint);
        }

        cr->set_line_width(STROKE_WIDTH);
        cr->move_to(0, y);
        cr->line_to(tick_length, y);
        cr->stroke();

        if (draw_label) {
            char buf[16];
            Hour12 h12 = to_12_hour(tm_buf);
            std::snprintf(buf, sizeof(buf), "%d %s", h12.hour, h12.am_pm);
            style::set_source(cr, palette.tick_major);
            cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cr->set_font_size(13);
            cr->move_to(tick_length + 8, y + 4);
            cr->show_text(buf);
        }
    }

    draw_clock_rule(cr, palette, clock, height);
    draw_now_marker(cr, palette, clock, now);
    draw_now_arrow(cr, palette, clock, width, geo.center_y);

    // Last, so nothing interrupts it.
    draw_panel_border(cr, palette, width, height);
}

bool SchedulePanel::on_timeline_tooltip(int x, int y, bool keyboard,
                                        const Glib::RefPtr<Gtk::Tooltip>& tooltip) {
    if (keyboard) return false;
    if (m_scheduled_bands.empty()) return false;
    static const double rule_x = measure_rule_x();
    if (x > rule_x) return false;

    const TimelineGeometry geo{m_timeline.get_height(), std::time(nullptr)};

    for (const auto& band : m_scheduled_bands) {
        double top, bottom;
        if (band.is_block) {
            const auto span = geo.span_for(band.start, band.end);
            top = span.y;
            bottom = span.y + span.height;
        } else {
            top = bottom = geo.y_for(band.start);
        }

        const double center = (top + bottom) / 2.0;
        top = std::min(top, center - HOVER_SLACK);
        bottom = std::max(bottom, center + HOVER_SLACK);

        if (y < top || y > bottom) continue;

        const std::string when = band.is_block
                                     ? clock_time(band.start) + " – " + clock_time(band.end)
                                     : clock_time(band.start);
        tooltip->set_text(when + "  " + band.title);
        return true;
    }

    return false;
}

// Nothing signals midnight or the passing seconds; the tick is what moves
// the date and elapsed labels.
bool SchedulePanel::on_timer_tick() {
    m_timeline.queue_draw();

    refresh_date_label();

    refresh_elapsed_label();

    return true;
}

void SchedulePanel::on_activate_clicked() {
    if (m_work.has_active_session()) {
        m_work.pause();
    } else {
        if (m_staged_id == -1) return;
        m_work.start(m_staged_id);
    }
    refresh_staged_label();
}

void SchedulePanel::on_complete_clicked() {
    if (m_staged_id == -1) return;

    m_work.complete(m_staged_id);
    m_staged_id = -1;
    refresh_staged_label();
}

void SchedulePanel::stage_task(int task_id) {
    if (m_work.has_active_session()) return;  // can't swap out a task being worked
    m_staged_id = task_id;
    refresh_staged_label();
}

void SchedulePanel::build_day_menu() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    auto* heading = Gtk::make_managed<Gtk::Label>("Working day");
    heading->set_xalign(0.0);
    heading->add_css_class("heading");
    box->append(*heading);

    const DayHoursRow hours = m_day.hours();

    auto* times_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* start_entry = Gtk::make_managed<Gtk::Entry>();
    auto* end_entry = Gtk::make_managed<Gtk::Entry>();

    start_entry->set_placeholder_text("--:--");
    end_entry->set_placeholder_text("--:--");
    start_entry->set_max_width_chars(6);
    end_entry->set_max_width_chars(6);

    if (hours.defined()) {
        start_entry->set_text(clock_util::format_hhmm(hours.start_minutes));
        end_entry->set_text(clock_util::format_hhmm(hours.end_minutes));
    }

    times_box->append(*start_entry);
    times_box->append(*Gtk::make_managed<Gtk::Label>("to"));
    times_box->append(*end_entry);
    box->append(*times_box);

    auto* apply_button = Gtk::make_managed<Gtk::Button>("Set");
    apply_button->signal_clicked().connect([this, start_entry, end_entry]() {
        const int start = clock_util::parse_hhmm(start_entry->get_text());
        const int end = clock_util::parse_hhmm(end_entry->get_text());

        m_day_menu.popdown();

        // Both or neither: a start with no end has no denominator.
        if (start == DayHoursRow::NO_HOURS || end == DayHoursRow::NO_HOURS) return;

        Glib::signal_idle().connect_once([this, start, end]() { m_day.set_hours(start, end); });
    });
    box->append(*apply_button);

    m_day_menu.set_child(*box);
}

void SchedulePanel::refresh_date_label() {
    const time_t now = std::time(nullptr);

    std::tm tm_buf = clock_util::local_tm(now);
    if (tm_buf.tm_yday == m_shown_yday) return;

    // Day appended by hand: %e space-pads single digits.
    char date[64];
    std::strftime(date, sizeof(date), "%A, %B ", &tm_buf);
    m_dock_date.set_text(std::string(date) + std::to_string(tm_buf.tm_mday));
    m_shown_yday = tm_buf.tm_yday;
}

void SchedulePanel::refresh_completed_bands() {
    m_completed_bands = m_work.entries_for_day(std::time(nullptr));
}

void SchedulePanel::refresh_scheduled_bands() {
    m_scheduled_bands.clear();

    // Stored minutes are wall-clock, so they're added to local midnight.
    const time_t day_start = clock_util::local_midnight(std::time(nullptr));

    for (const auto& row : m_task_attributes.scheduled_today()) {
        ScheduledBand band;
        band.node_id = row.node_id;
        band.start = day_start + row.time_start * 60;
        band.is_block = (row.time_end != TaskDateRow::NO_TIME && row.time_end > row.time_start);
        band.end = band.is_block ? day_start + row.time_end * 60 : band.start;
        band.title = m_projects.display_title(row.node_id);
        band.color = m_task_attributes.get_color(row.node_id);
        m_scheduled_bands.push_back(band);
    }
}

void SchedulePanel::refresh_staged_label() {
    if (m_staged_id != -1 && !m_projects.contains(m_staged_id)) {
        m_staged_id = -1;
    }

    if (m_staged_id == -1) {
        m_slot_path.set_text("");
        m_slot_title.set_text("");
        m_elapsed_label.set_text("");
        m_staged_banked = 0;
        m_shown_elapsed = -1;
        set_control_shown(m_activate_button, false, "Activate");
        set_control_shown(m_complete_button, false, "Mark complete");
        return;
    }

    TaskSnapshot snap = m_task_attributes.snapshot(m_staged_id);

    m_slot_path.set_text(snap.path);

    style::set_colored_text(m_slot_title, snap.title, snap.color);

    m_staged_banked = m_work.recorded_seconds(m_staged_id);
    m_shown_elapsed = -1;
    refresh_elapsed_label();

    const bool active = m_work.has_active_session();

    m_activate_button.set_label(active ? "⏸" : "▶");
    set_control_shown(m_activate_button, true, active ? "Deactivate" : "Activate");
    set_control_shown(m_complete_button, active, "Mark complete");
}

void SchedulePanel::refresh_elapsed_label() {
    if (m_staged_id == -1) return;

    const long live = (m_work.active_task_id() == m_staged_id) ? m_work.active_seconds() : 0;
    const long total = m_staged_banked + live;

    if (total == m_shown_elapsed) return;  // the tick is ten times this label's rate
    m_shown_elapsed = total;
    m_elapsed_label.set_text(elapsed_text(total));
}
