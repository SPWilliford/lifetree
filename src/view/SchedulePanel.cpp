#include "view/SchedulePanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <cairomm/surface.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <gtkmm/entry.h>
#include <pangomm/layout.h>

#include "core/Day.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"
#include "view/Style.hpp"

namespace {
// Smaller = faster drift. 180px/hour is a barely-there creep — you
// notice it over minutes, not seconds.
constexpr double PIXELS_PER_HOUR = 180.0;
constexpr double PIXELS_PER_SECOND = PIXELS_PER_HOUR / 3600.0;
constexpr long QUARTER_HOUR_SECONDS = 15 * 60;

// The clock box's leading edge. Fixed pixels, not a fraction of the
// panel, so the Cairo geometry never has to re-sync with the real
// widgets.
//
// An anchor on the LEFT rather than a centre, because the constraint is
// a clearance and not a position: this has to clear the hour labels,
// which reach tick_length(36) + gap(8) + their own width — about 73px
// for the widest of them, "12 AM". Real font metrics aren't measurable
// here, so it's a value to nudge, and nudging it against a centre moved
// the gap by only half as much as you asked for.
constexpr double CLOCK_LEFT_X = 88.0;

// Between the clock's trailing border and the vertical rule.
//
// The rule used to sit ON that border, with the point growing out of the
// box. That crowded the clock and pinned the dock's split to the clock's
// own width, so a date label wider than the clock pushed the split out
// of line with the rule underneath it.
//
// The point now stands on the rule instead, mirroring the arrowhead at
// the far edge, so this gap is free: nothing spans it but the now-line,
// and widening it moves only the rule.
constexpr double CLOCK_RULE_GAP = 20.0;

// A floor, for a panel too narrow to fit clock and band both.
constexpr double BAND_MIN_WIDTH = 200.0;

// Enough that the two framed areas don't read as one box with a line
// across it.
constexpr int DOCK_TIMELINE_GAP = 8;

// Between the two docks. The vertical rule runs up the middle of it, so
// the left dock is half this narrower than the rule's own x.
constexpr int DOCK_SPLIT_GAP = 8;

// Width reserved for the elapsed readout, in characters. "0:00:00"
// fits; a single stretch past ten hours would push wider, which is long
// enough that reserving for it isn't worth the extra gap all day. The
// label is right-aligned inside this, so the distance to the button
// beside it holds constant as the digits grow.
constexpr int ELAPSED_WIDTH_CHARS = 7;

// Above the transport, which sits closer to the title than the box's
// own 2px spacing would put it.
constexpr int TRANSPORT_TOP_GAP = 8;

// BORDER_HALF_WIDTH exists because Cairo centers a stroke on its path:
// a shape meeting a border's OUTER edge must offset by half of it.
constexpr double STROKE_WIDTH = 2.0;
constexpr double BORDER_HALF_WIDTH = STROKE_WIDTH / 2.0;

// Inside the clock's drawn box.
constexpr double CLOCK_PAD_X = 16.0;
constexpr double CLOCK_PAD_Y = 10.0;

// The box is sized from this, not from the time it's showing, so it
// holds still all day. Structural, not cosmetic: the vertical rule hangs
// off this box's trailing edge, and a rule that shifted every time the
// hour rolled over would be worse than a box a few pixels roomy.
constexpr const char* CLOCK_WIDEST_TIME = "12:00 AM";

// An equilateral triangle on a base of b stands sqrt(3)/2 * b tall.
// Both arrowheads derive their length from this, so they stay identical.
constexpr double TRIANGLE_ASPECT = 0.8660254;

// Just short of opaque: softens against the background without letting
// anything show through.
constexpr double ACCENT_ALPHA = 0.9;

// Half-height of a dashed line's hover target. The line itself is 2px;
// pointing at something 2px tall is not a reasonable ask.
constexpr double HOVER_SLACK = 7.0;

// Light enough that the tick marks and hour labels drawn over it stay
// legible — this column is shared, unlike the open band column.
constexpr double SCHEDULED_BLOCK_ALPHA = 0.18;

// 'HH:MM' both ways, matching how a task's times are typed in the tree's
// date editor. Duplicated from ProjectTreePanel rather than shared — two
// small functions in two panels, and a header holding only these would
// be a file that exists to avoid ten lines. Worth folding together if a
// third caller ever appears.
int hhmm_to_minutes(const Glib::ustring& text) {
    int hours = 0, minutes = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &hours, &minutes) != 2) return DayHoursRow::NO_HOURS;
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return DayHoursRow::NO_HOURS;
    return hours * 60 + minutes;
}

std::string minutes_to_hhmm(int minutes) {
    if (minutes < 0 || minutes >= 24 * 60) return "";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

// tm_hour is 24-hour; the tick labels and the clock box both need 12.
struct Hour12 {
    int hour;
    const char* am_pm;
};
Hour12 to_12_hour(const std::tm& tm_buf) {
    int hour = tm_buf.tm_hour % 12;
    if (hour == 0) hour = 12;
    return {hour, (tm_buf.tm_hour < 12) ? "AM" : "PM"};
}

// Shown-ness for a dock control: opacity, sensitivity and tooltip move
// together because they're one state, and setting them apart is how a
// control ends up invisible but still explaining itself.
//
// Opacity rather than visible(false) so the dock doesn't resize on every
// activate — but an opacity-0 widget is still hit-tested, which is what
// made the tooltip leak. set_has_tooltip is what actually suppresses it,
// and it goes after the text: setting text turns has-tooltip back on.
void set_control_shown(Gtk::Button& button, bool shown, const char* tooltip) {
    button.set_opacity(shown ? 1.0 : 0.0);
    button.set_sensitive(shown);
    button.set_tooltip_text(tooltip);
    button.set_has_tooltip(shown);
}

// Elapsed time for the dock. Hours only once there are any — a task
// twelve minutes in reads "12:34", not "0:12:34".
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
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    const Hour12 h12 = to_12_hour(tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", h12.hour, tm_buf.tm_min, h12.am_pm);
    return buf;
}

// Real time to vertical position. The inverted layout lives entirely in
// y_for's sign; every tick and band derives from it rather than
// flipping the sign for itself.
//
// Built fresh per draw: deriving position from real elapsed time every
// frame keeps motion correct when a timer tick arrives late, instead of
// accumulating drift.
struct TimelineGeometry {
    double center_y;
    time_t now;

    TimelineGeometry(int height, time_t now_) : center_y(height / 2.0), now(now_) {}

    // Minus, not plus — a later time sits higher up the screen.
    double y_for(time_t t) const {
        return center_y - static_cast<double>(t - now) * PIXELS_PER_SECOND;
    }

    // The {y, height} pair Cairo's rectangle() wants. start <= end
    // chronologically and y_for inverts, so end is unconditionally the
    // top edge — no min/max needed.
    struct Span {
        double y, height;
    };
    Span span_for(time_t start, time_t end) const {
        double top = y_for(end);
        return {top, y_for(start) - top};
    }
};

// The clock's rectangular body, worked out before anything is drawn:
// the flag, the vertical rule on its trailing edge, and the bands
// anchored past its point all need it.
struct ClockBox {
    double x, y, w, h;

    double center_y() const { return y + h / 2.0; }

    // Where the vertical rule sits. One definition, because the rule is
    // drawn from it, the dock's width is measured from it, and it's the
    // planned/actual boundary the whole panel is split on.
    double rule_x() const { return x + w + CLOCK_RULE_GAP; }

    // The single definition of where the clock's zone ends and the
    // timeline begins — the bands anchor to it. Measured from the rule,
    // because that's where the point now stands.
    double tip_x() const { return rule_x() + h * TRIANGLE_ASPECT; }
};

// Where the mirrored arrowhead points. The counterpart of
// ClockBox::tip_x, and the single definition of the timeline's far edge
// — both the arrow and the bands read it, so neither can drift.
double right_tip_x(const ClockBox& box, int panel_width) {
    return panel_width - BORDER_HALF_WIDTH - box.h * TRIANGLE_ASPECT;
}

struct BandColumn {
    double x;
    double width;
    double center() const { return x + width / 2.0; }
};

// Tip to tip. A band starts where the clock's point ends and stops where
// the far arrowhead's begins, so the pair reads as printing the band
// between them rather than as decoration alongside it.
//
// The floor still wins on a panel too narrow to hold both: bands then
// run under the arrowheads, which is the lesser of the two evils
// against a band too thin to carry a color.
BandColumn band_column(int panel_width, const ClockBox& clock) {
    const double x = clock.tip_x();
    const double width = std::max(BAND_MIN_WIDTH, right_tip_x(clock, panel_width) - x);
    return {x, width};
}

// The planned side: leading edge to the clock rule, over the ticks and
// the clock's own zone. That gives the rule one meaning the whole way
// down the panel — intended on the left, actual on the right — and costs
// no width, since that region held only ticks and the clock.
BandColumn scheduled_column(const ClockBox& clock) {
    return {0.0, clock.rule_x()};
}

// Set before measuring and again before drawing: the tick labels in
// between select their own font, and one function is what stops the box
// being measured in a font other than the one it's drawn in.
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
    // Both edges land on whole pixels: x is a whole constant and w is
    // ceiled, so x + w is whole too. The left edge carries the box's own
    // border, the right edge the vertical rule — both 2px strokes
    // centred on their x, and a fractional value covers three columns at
    // half strength instead of two at full.
    box.w = std::ceil(widest.width + CLOCK_PAD_X * 2);
    box.h = widest.height + CLOCK_PAD_Y * 2;
    box.x = CLOCK_LEFT_X;
    box.y = center_y - box.h / 2.0;
    return box;
}

// The x the timeline's vertical rule sits on, measured without a window.
//
// The layout needs this before anything is drawn, and it depends only on
// font metrics, which don't change while the app runs — so it's measured
// once against a scratch surface rather than plumbed out of a draw call.
// Resizing a widget from inside a draw function is how you get a
// layout/redraw loop.
//
// Goes through clock_box_for so there is exactly one copy of the
// arithmetic; a second one here would be free to drift.
double measure_rule_x() {
    auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, 1, 1);
    auto cr = Cairo::Context::create(surface);
    const ClockBox box = clock_box_for(cr, 0.0);
    return box.rule_x();
}

// The flag's point mirrored against the far edge, so the two bracket the
// present moment: the clock names it on the left, this aims back at it
// from the right. Its base sits on the panel border's centreline, which
// is what makes it read as part of the frame rather than as a mark
// floating in the timeline.
//
// Sized from the clock box rather than from literals of its own — if the
// clock's font or padding changes, both arrowheads move together instead
// of one quietly drifting out of step. The same BORDER_HALF_WIDTH bleed
// top and bottom, for the same reason: it keeps the two identical.
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

// In the panel border's color and weight, so the timeline scrolls
// beneath a fixed structure rather than carrying a line with it. Drawn
// late, over bands and ticks, but before the flag — whose opaque body
// interrupts it so the two read as one assembly.
//
// On the box's TRAILING edge, so the rule is the boundary of the
// schedule and only the flag's point crosses it. Anchored to the leading
// edge, the whole clock body sat on the schedule side and read as the
// clock intruding rather than pointing.
//
// Nothing here has to avoid the flag: the box fill and then the point
// are both painted over this afterwards, so the segment crossing the
// flag is covered rather than blended — which is what keeps the accent
// from doubling up along the point's base.
void draw_clock_rule(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                     const ClockBox& box, int height) {
    const double x = box.rule_x();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->move_to(x, 0);
    cr->line_to(x, height);
    cr->stroke();
}

// Opaque, and never the accent: a band records time actually spent, and
// the accent's one job is marking now. An empty or unparseable color
// falls back to the palette's neutral band tone.
void draw_band(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
               const TimelineGeometry& geo, const BandColumn& col, time_t start, time_t end,
               const std::string& color) {
    style::set_source(cr, style::parse_hex(color, palette.band_fallback));

    auto span = geo.span_for(start, end);
    cr->rectangle(col.x, span.y, col.width, span.height);
    cr->fill();
}

// Never solid at full strength: a worked band is, and
// happened-versus-going-to-happen is the one distinction that has to
// survive at a glance. A span washes the column; no end time means no
// span, so it collapses to a single dashed rule.
//
// Dashed and not solid even though the side already says "planned": the
// tick marks share this column and are solid, so a solid line here would
// read as an unusually long tick.
//
// A wash rather than an outline, which is what this was while it lived in
// the open band column. Here the left edge is clipped by the panel border
// and the right edge is covered by the clock rule, so an outlined block
// showed only its top and bottom and read as two stray ticks.
//
// No label. The title is the tooltip — see on_timeline_tooltip. Text sat
// on top of the line it belonged to, and over the hour marks it's worse
// here than it was out in the open column.
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

// Inset by BORDER_HALF_WIDTH so the centred stroke isn't clipped at the
// true edge.
void draw_panel_border(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                       int width, int height) {
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->rectangle(BORDER_HALF_WIDTH, BORDER_HALF_WIDTH, width - STROKE_WIDTH,
                  height - STROKE_WIDTH);
    cr->stroke();
}

// A line in from the left edge into a flag-shaped clock whose
// triangular point IS the line's endpoint.
//
// In the accent like the rest of this assembly, and it stays that way
// whatever is being worked. The tick zone, the clock and the rule are
// the FRAME — fixed structure the timeline scrolls beneath — and a frame
// that took the active task's colour would stop being the fixed
// reference it exists to be. What's running is said by the band growing
// in the column to the right, which is content, and saying it twice
// costs the frame the one property it has.
void draw_now_marker(const Cairo::RefPtr<Cairo::Context>& cr, const style::Palette& palette,
                     const ClockBox& box, time_t now) {
    const double center_y = box.center_y();

    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    Hour12 h12 = to_12_hour(tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", h12.hour, tm_buf.tm_min, h12.am_pm);

    // The tick labels left a smaller, lighter font on the context.
    select_clock_font(cr);
    Cairo::TextExtents extents;
    cr->get_text_extents(buf, extents);

    const double rule = box.rule_x();

    // Two segments, and the second one is what makes the clock read as
    // threaded onto the line rather than parked beside it: the line runs
    // in from the panel edge, the box interrupts it, and it picks up
    // again in the gap and carries on to the rule.
    //
    // STROKE_WIDTH, like the rule and the flag's border. It was 1px as
    // an exact-minute hairline, but half the ink spread by antialiasing
    // across two rows reads as translucent next to a 2px rule — and the
    // precision it was buying is not needed with a clock face printed
    // beside it saying the same thing.
    //
    // Still deliberately NOT pixel-snapped, unlike the vertical rule:
    // this line drifts continuously, and antialiasing is what conveys
    // that. Snapped, it would advance in visible jumps.
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->move_to(0, center_y);
    cr->line_to(box.x, center_y);
    cr->move_to(box.x + box.w, center_y);
    cr->line_to(rule, center_y);
    cr->stroke();

    // All four sides now. The point used to grow out of the right edge,
    // so a stroke there would have put a seam through one shape; the two
    // are separate objects now and the box closes.
    style::set_source(cr, palette.background);
    cr->rectangle(box.x, box.y, box.w, box.h);
    cr->fill_preserve();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->set_line_width(STROKE_WIDTH);
    cr->stroke();

    // Standing on the RULE, not on the box: the exact mirror of the
    // arrowhead at the far edge, so the two bracket the present moment
    // symmetrically instead of one being a flag and the other a marker.
    // Same vertical extent as that one, deliberately — see
    // draw_now_arrow, whose geometry this has to match.
    cr->move_to(rule, box.y - BORDER_HALF_WIDTH);
    cr->line_to(box.tip_x(), center_y);
    cr->line_to(rule, box.y + box.h + BORDER_HALF_WIDTH);
    cr->close_path();
    style::set_source(cr, palette.accent, ACCENT_ALPHA);
    cr->fill();

    // Centered within the box, which is sized for the longest time
    // string — so a shorter one has slack on both sides.
    style::set_source(cr, palette.text);
    cr->move_to(box.x + (box.w - extents.width) / 2.0 - extents.x_bearing,
                center_y - extents.height / 2.0 - extents.y_bearing);
    cr->show_text(buf);
}

}  // namespace

SchedulePanel::SchedulePanel(TreeController& projects, Work& worklog,
                             TaskAttributes& task_attributes, Day& day)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0),
      m_projects(projects),
      m_work(worklog),
      m_task_attributes(task_attributes),
      m_day(day) {
    initialize_layout();
    refresh_completed_bands();
    refresh_scheduled_bands();
    refresh_date_label();

    // Keeps the slot honest when the staged task is renamed or deleted
    // elsewhere. A title or a deletion also changes what's drawn ahead.
    m_projects.connect_changed([this]() {
        m_label_refresh.request();
        m_scheduled_refresh.request();
    });

    // A color or a date doesn't touch the tree, so the signal above never
    // fires for it. This is what puts a new appointment on the timeline.
    m_task_attributes.connect_changed([this]() {
        m_label_refresh.request();
        m_scheduled_refresh.request();
    });

    // Not done in the button handlers: Work also banks time on its own when
    // a task being worked is deleted elsewhere, which no handler here sees.
    // The label too, not just the bands — banking a segment moves the
    // elapsed total the dock is holding.
    m_work.connect_changed([this]() {
        m_bands_refresh.request();
        m_label_refresh.request();
    });

    // Positions come from real elapsed time each tick rather than being
    // nudged, so a delayed tick doesn't accumulate drift.
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &SchedulePanel::on_timer_tick), 100);
}

void SchedulePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    // --- the staged-task dock, across the top ---
    //
    // Activate is the only primary action here, and it's the only one drawn
    // as one: round and larger, with Complete beside it as a plain
    // secondary. See .dock-activate in Style.cpp.
    m_activate_button.add_css_class("dock-activate");
    set_control_shown(m_activate_button, false, "Activate");  // nothing staged yet
    m_activate_button.signal_clicked().connect(
        sigc::mem_fun(*this, &SchedulePanel::on_activate_clicked));

    m_complete_button.add_css_class("dock-complete");
    set_control_shown(m_complete_button, false,
                      "Mark complete");  // nothing staged, or staged but idle
    m_complete_button.signal_clicked().connect(
        sigc::mem_fun(*this, &SchedulePanel::on_complete_clicked));

    // Reserved width rather than a natural one, so the transport doesn't
    // shuffle sideways the moment the readout gains a digit.
    m_elapsed_label.add_css_class("dock-elapsed");
    m_elapsed_label.set_width_chars(ELAPSED_WIDTH_CHARS);
    m_elapsed_label.set_xalign(1.0);

    m_staged_buttons_row.append(m_elapsed_label);
    m_staged_buttons_row.append(m_activate_button);
    m_staged_buttons_row.append(m_complete_button);

    // --- the day dock, over the timeline's tick and clock zone ---
    m_dock_date.set_halign(Gtk::Align::START);
    m_dock_date.set_xalign(0.0);

    // Insurance, not the fix. set_size_request below is a MINIMUM, so a
    // label wider than the rule pushes this box past it and the dock's
    // split stops lining up with the gold rule underneath — which is the
    // one alignment in the panel you can't help noticing. Ellipsizing drops
    // the label's own minimum to almost nothing, so the box can never be
    // widened from the inside whatever the date, locale or font turns out
    // to be.
    m_dock_date.set_ellipsize(Pango::EllipsizeMode::END);

    // The date IS the button, rather than a button beside it. It already
    // names the thing being configured, and the alternative — a control
    // added next to it — would spend width the dock doesn't have.
    //
    // Flat, so an unclicked dock reads as a label. The hover state is the
    // whole affordance, which is why it gets its own rule in Style.cpp
    // instead of relying on a flat button's default.
    m_day_button.set_child(m_dock_date);
    m_day_button.set_has_frame(false);
    m_day_button.set_popover(m_day_menu);

    // Rebuilt on every open, not filled once: today may have inherited its
    // hours from a day set weeks ago, and the fields have to show that
    // rather than whatever was last typed into them.
    m_day_menu.signal_show().connect(sigc::mem_fun(*this, &SchedulePanel::build_day_menu));
    m_day_button.set_halign(Gtk::Align::FILL);
    m_day_button.set_valign(Gtk::Align::FILL);
    m_day_button.set_vexpand(true);
    m_day_button.add_css_class("day-button");
    m_day_button.set_tooltip_text("The day's start, end and review");

    // Half the dock each, both expanding. The button takes the top; the
    // spacer holds the bottom open for the timeline's controls, which land
    // opposite the staged task's transport across the gap. Without it the
    // button would centre itself in the whole box and the two docks would
    // stop lining up.
    m_timeline_dock.append(m_day_button);
    m_timeline_dock.append(m_timeline_controls);
    m_timeline_controls.set_vexpand(true);

    // TRAP: explicitly false, and it has to be. A widget with no expand flag
    // of its own inherits one from any child that has it, so the two halves
    // above would have made this dock expand, then the dock row, and the row
    // would have taken its height off the timeline — the panel's whole point.
    // Setting the flag here, either way, ends that chain: the dock takes its
    // natural height and its children divide only what it was given.
    m_timeline_dock.set_vexpand(false);

    m_timeline_dock.add_css_class("staged-dock");

    // Fixed, and half the gap short of the rule so the gap straddles it.
    // Not hexpand: the right dock takes whatever is left.
    m_timeline_dock.set_hexpand(false);
    m_timeline_dock.set_size_request(static_cast<int>(measure_rule_x() - DOCK_SPLIT_GAP / 2.0), -1);

    // Centred, so path, title and transport share one vertical axis and the
    // dock reads as a single assembly. Safe here in a way it wouldn't be in
    // a list: this is one item, and nothing scans down it looking for a
    // left edge to follow.
    m_staged_buttons_row.set_halign(Gtk::Align::CENTER);
    m_staged_buttons_row.set_margin_top(TRANSPORT_TOP_GAP);

    // No character cap needed: the dock spans the panel, so a long title
    // can't stretch it.
    //
    // halign stays FILL so ellipsizing has a width to measure against;
    // xalign is what moves the text inside that allocation.
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

    // --- the timeline, filling everything below it ---
    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));

    // What the drawn lines no longer say. false so this runs before the
    // default handler rather than after it has already declined.
    m_timeline.set_has_tooltip(true);
    m_timeline.signal_query_tooltip().connect(
        sigc::mem_fun(*this, &SchedulePanel::on_timeline_tooltip), false);

    append(m_dock_row);
    append(m_timeline);
}

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------

void SchedulePanel::draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    // Once per draw, never cached — see style::palette_for.
    const style::Palette palette = style::palette_for(m_timeline);

    // Explicit, so the timeline reads clearly whatever is behind it.
    style::set_source(cr, palette.background);
    cr->paint();

    time_t now = std::time(nullptr);
    TimelineGeometry geo{height, now};

    // First: everything else positions against it, and measuring it sets
    // the clock font before the tick labels replace it.
    const ClockBox clock = clock_box_for(cr, geo.center_y);

    // Shared by every band, so a task's label length never determines its
    // width and earlier bands don't shift when something else is staged.
    const BandColumn col = band_column(width, clock);

    // Before the tick marks, so those stay legible on top.
    for (const auto& entry : m_completed_bands) {
        draw_band(cr, palette, geo, col, entry.start_time, entry.end_time, entry.color);
    }

    // No live-vs-paused branching: every path out of Work's one active
    // slot banks the segment and empties it in the same call, so an active
    // session is always genuinely running and its far edge is always now.
    if (m_work.has_active_session()) {
        // y_for(geo.now) is center_y by definition, so passing now
        // terminates the band exactly on the line.
        draw_band(cr, palette, geo, col, m_work.session_start(), now,
                  m_task_attributes.get_color(m_work.active_task_id()));
    }

    // In their own column left of the rule, and before the ticks — which
    // then draw on top, so a dashed line reads as a wash behind the time
    // scale rather than striking through "3 PM".
    const BandColumn planned = scheduled_column(clock);
    for (const auto& band : m_scheduled_bands) {
        draw_scheduled_band(cr, palette, geo, planned, band.start, band.end, band.is_block,
                            band.color);
    }

    // Safe for real time zones: every UTC offset in use is a multiple of
    // 15 minutes, so epoch-aligned quarter-hours are also wall-clock ones.
    time_t aligned = now - (now % QUARTER_HOUR_SECONDS);

    // Plus a couple extra so ticks don't pop in at the boundary.
    int half_span_ticks = static_cast<int>((height / 2.0) / (PIXELS_PER_HOUR / 4.0)) + 2;

    for (int k = -half_span_ticks; k <= half_span_ticks; ++k) {
        time_t tick_time = aligned + k * QUARTER_HOUR_SECONDS;
        double y = geo.y_for(tick_time);
        if (y < -20 || y > height + 20) continue;

        std::tm tm_buf{};
        localtime_r(&tick_time, &tm_buf);

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

    // Over the ticks it sits among.
    draw_clock_rule(cr, palette, clock, height);
    draw_now_marker(cr, palette, clock, now);
    draw_now_arrow(cr, palette, clock, width, geo.center_y);

    // Last, so no tick or band interrupts it.
    draw_panel_border(cr, palette, width, height);
}

// ---------------------------------------------------------------------
// Live updates
// ---------------------------------------------------------------------

// Hit-tests the scheduled column, since that's the only thing on the
// timeline whose label was taken away. Geometry is rebuilt here rather than
// cached from the draw: it depends on the clock, which moves every tick, and
// rebuilding is a subtraction.
bool SchedulePanel::on_timeline_tooltip(int x, int y, bool keyboard,
                                        const Glib::RefPtr<Gtk::Tooltip>& tooltip) {
    if (keyboard) return false;  // a pointer position is the whole query
    if (m_scheduled_bands.empty()) return false;
    if (x > measure_rule_x()) return false;

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

        // A short block is as hard to point at as a line, so the slack
        // applies to both — it only ever grows the target.
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

bool SchedulePanel::on_timer_tick() {
    m_timeline.queue_draw();  // the clock reads live, so this can't be drawn once and left

    // Nothing signals midnight, and this panel is meant to be left running
    // overnight — so the tick is what rolls the date over.
    refresh_date_label();

    // The only other thing on screen that moves without a signal.
    refresh_elapsed_label();

    return true;  // keep repeating
}

void SchedulePanel::on_activate_clicked() {
    if (m_work.has_active_session()) {
        m_work.pause();
    } else {
        if (m_staged_id == -1) return;  // button should be insensitive anyway; safety net
        m_work.start(m_staged_id);
    }
    refresh_staged_label();  // updates the play/pause icon and Complete's visibility together
}

void SchedulePanel::on_complete_clicked() {
    if (m_staged_id == -1) return;

    m_work.complete(m_staged_id);
    m_staged_id = -1;
    refresh_staged_label();
}

void SchedulePanel::stage_task(int task_id) {
    if (m_work.has_active_session())
        return;  // can't swap out a task that's actively being worked —
                 // deliberately silent for now; revisit if this needs
                 // a visible cue (message, shake, etc.) later
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

    // "--:--" rather than a sample time, for the same reason the task date
    // editor uses it: a plausible placeholder reads as a value already set.
    start_entry->set_placeholder_text("--:--");
    end_entry->set_placeholder_text("--:--");
    start_entry->set_max_width_chars(6);
    end_entry->set_max_width_chars(6);

    // Prefilled with what today resolves to, inherited or not. Showing an
    // inherited figure is the point: it's what today's measurements are
    // being taken against, whether or not today is the day it was typed.
    if (hours.defined()) {
        start_entry->set_text(minutes_to_hhmm(hours.start_minutes));
        end_entry->set_text(minutes_to_hhmm(hours.end_minutes));
    }

    times_box->append(*start_entry);
    times_box->append(*Gtk::make_managed<Gtk::Label>("to"));
    times_box->append(*end_entry);
    box->append(*times_box);

    auto* apply_button = Gtk::make_managed<Gtk::Button>("Set");
    apply_button->signal_clicked().connect([this, start_entry, end_entry]() {
        const int start = hhmm_to_minutes(start_entry->get_text());
        const int end = hhmm_to_minutes(end_entry->get_text());

        m_day_menu.popdown();

        // Both or neither: an end with no start has nothing to measure
        // from, and a start with no end has no denominator, which is the
        // whole reason for defining a day.
        if (start == DayHoursRow::NO_HOURS || end == DayHoursRow::NO_HOURS) return;

        // Values read before deferring — these widgets live inside the
        // popover and mustn't be touched once it's closing.
        Glib::signal_idle().connect_once([this, start, end]() { m_day.set_hours(start, end); });
    });
    box->append(*apply_button);

    m_day_menu.set_child(*box);
}

void SchedulePanel::refresh_date_label() {
    const time_t now = std::time(nullptr);

    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    if (tm_buf.tm_yday == m_shown_yday) return;

    // The day appended rather than left to %e, which space-pads a single
    // digit and would read as "September  2" for nine days of every month.
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

    // Stored minutes are wall-clock, so they're added to a local midnight
    // rather than an epoch offset — an hour that survives a DST change.
    time_t now = std::time(nullptr);
    std::tm midnight{};
    localtime_r(&now, &midnight);
    midnight.tm_hour = 0;
    midnight.tm_min = 0;
    midnight.tm_sec = 0;
    midnight.tm_isdst = -1;
    const time_t day_start = std::mktime(&midnight);

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
        // Vanished out from under us — clear rather than show stale text.
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

    // Two channels doing separate work: colour on the title says which
    // project, dimming on the path says that line is context. Inline they
    // were one string in one colour doing both.
    m_slot_path.set_text(snap.path);

    // Markup only when there's a color: the dock is a themed surface, so
    // the theme's own text color is guaranteed to contrast with it.
    if (snap.color.empty()) {
        m_slot_title.set_text(snap.title);
    } else {
        m_slot_title.set_markup("<span foreground='" + snap.color + "'>" +
                                Glib::Markup::escape_text(snap.title) + "</span>");
    }

    // Every day's segments, not just today's: a task paused overnight and
    // picked up again carries time on both sides of midnight.
    m_staged_banked = m_work.recorded_seconds(m_staged_id);
    m_shown_elapsed = -1;  // the total moved; make the next write unconditional
    refresh_elapsed_label();

    // Two states: staged-but-idle is play; active is pause plus the check.
    // Complete never applies to a task that was never worked.
    const bool active = m_work.has_active_session();

    m_activate_button.set_label(active ? "⏸" : "▶");
    set_control_shown(m_activate_button, true, active ? "Deactivate" : "Activate");
    set_control_shown(m_complete_button, active, "Mark complete");
}

void SchedulePanel::refresh_elapsed_label() {
    if (m_staged_id == -1) return;

    // Only a session belonging to THIS task counts. Work runs one session
    // at a time but the dock's staged slot is a separate choice, so the two
    // can name different tasks.
    const long live = (m_work.active_task_id() == m_staged_id) ? m_work.active_seconds() : 0;
    const long total = m_staged_banked + live;

    if (total == m_shown_elapsed) return;  // the tick is ten times this label's rate
    m_shown_elapsed = total;
    m_elapsed_label.set_text(elapsed_text(total));
}
