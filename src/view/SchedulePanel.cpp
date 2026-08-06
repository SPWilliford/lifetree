#include "view/SchedulePanel.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"
#include "core/TaskAttributes.hpp"
#include <pangomm/layout.h>
#include <glibmm/markup.h>
#include <glibmm/main.h>
#include <cstdio>
#include <algorithm>

namespace {
    // How much vertical space represents one hour of real time. Smaller
    // = faster drift, larger = slower. 180px/hour is a deliberately slow,
    // barely-there creep — you notice it over minutes, not seconds.
    constexpr double PIXELS_PER_HOUR = 180.0;
    constexpr double PIXELS_PER_SECOND = PIXELS_PER_HOUR / 3600.0;
    constexpr long QUARTER_HOUR_SECONDS = 15 * 60;

    // Fixed pixel values, not fractions of the panel's width. Resizing
    // then only changes the empty space between the clock and the dock,
    // and the Cairo geometry never has to re-sync with the real widgets
    // (which can only catch up an idle cycle later).
    //
    // CLOCK_CENTER_X has to clear the hour-tick labels, which reach out
    // to about tick_length(36) + gap(8) + their own text width. That's
    // an estimate — real font metrics aren't measurable here — so it's a
    // value to nudge rather than an exact one.
    constexpr double CLOCK_CENTER_X = 170.0;

    // Space kept clear on the left for the tick marks and their hour
    // labels. Bands never reach back into it, so the two can't collide
    // however the panel is resized.
    constexpr double LEFT_GUTTER = 300.0;

    // Bands grow with the panel rather than sitting at one fixed width,
    // bounded at both ends: too narrow and they're hard to read, too wide
    // and they dominate a timeline that's mostly meant to be read as time.
    constexpr double BAND_WIDTH_FRACTION = 0.45;
    constexpr double BAND_MIN_WIDTH = 200.0;
    constexpr double BAND_MAX_WIDTH = 600.0;

    // Shared between the Cairo geometry below and m_staged_box's GTK
    // alignment (see initialize_layout), so the drawn band and the real
    // widgets anchor to the same reference point.
    constexpr double BAND_RIGHT_MARGIN = 20.0;

    // Between the dock's bottom edge and the timeline's top. Enough to read
    // as two separate framed areas rather than one box with a line across
    // it, which is what adjacent borders would look like.
    constexpr int DOCK_TIMELINE_GAP = 8;

    constexpr double BG_R = 0.13, BG_G = 0.13, BG_B = 0.15;             // dark background — timeline, clock box, task dock
    constexpr double GOLD_R = 0.95, GOLD_G = 0.75, GOLD_B = 0.2;        // the "now" accent — line, both borders
    constexpr double CLOCK_TEXT_R = 0.95, CLOCK_TEXT_G = 0.95, CLOCK_TEXT_B = 0.95;

    // The tick marks' three tiers, brightest on the hour. TICK_HOUR is also
    // the staged label's border, so that border reads as part of the same
    // neutral furniture as the time markers rather than as another accent
    // competing with the gold.
    constexpr double TICK_HOUR_R = 0.85, TICK_HOUR_G = 0.85, TICK_HOUR_B = 0.9;
    constexpr double TICK_HALF_R = 0.6, TICK_HALF_G = 0.6, TICK_HALF_B = 0.65;
    constexpr double TICK_QUARTER_R = 0.4, TICK_QUARTER_G = 0.4, TICK_QUARTER_B = 0.45;

    // The staged label's color when its project has none set. Explicit
    // rather than left to GTK's theme default because the label is a real
    // widget sitting on a dark background this file draws itself — a light
    // theme's default text color would be dark-on-dark and unreadable.
    // Hex string because it goes straight into Pango markup.
    constexpr const char* DEFAULT_TEXT_COLOR = "#f2f2f2";

    // Fallback for a band whose project has no color set. Grey rather
    // than gold so it never competes with gold's role as the "now"
    // accent, and close to the tick marks' greys so it reads as family.
    constexpr double BAND_DEFAULT_R = 0.65, BAND_DEFAULT_G = 0.65, BAND_DEFAULT_B = 0.7;

    // One line width for every stroke this file draws. BORDER_HALF_WIDTH
    // exists because Cairo centers a stroke on its path: a shape that
    // has to meet a border's *outer* edge must offset by half of it.
    constexpr double STROKE_WIDTH = 2.0;
    constexpr double BORDER_HALF_WIDTH = STROKE_WIDTH / 2.0;

    // Padding around the clock's text, inside its drawn box.
    constexpr double CLOCK_PAD_X = 16.0;
    constexpr double CLOCK_PAD_Y = 10.0;

    // One transparency for every band, with no distinction by status: a
    // band reads as "printed" onto the timeline the moment it exists,
    // rather than as something still signalling its own state.
    constexpr double BAND_ALPHA = 0.3;

    // tm_hour is 24-hour — this is the repeated bit of converting it for
    // display, shared by the tick labels and the clock box.
    struct Hour12 { int hour; const char* am_pm; };
    Hour12 to_12_hour(const std::tm& tm_buf) {
        int hour = tm_buf.tm_hour % 12;
        if (hour == 0) hour = 12;
        return { hour, (tm_buf.tm_hour < 12) ? "AM" : "PM" };
    }

    // "#RRGGBB" -> 0..1 doubles. Returns false (leaving r/g/b untouched)
    // on anything malformed, so callers can fall back to a default color
    // rather than drawing garbage.
    bool parse_hex_color(const std::string& hex, double& r, double& g, double& b) {
        if (hex.size() != 7 || hex[0] != '#') return false;
        try {
            r = std::stoi(hex.substr(1, 2), nullptr, 16) / 255.0;
            g = std::stoi(hex.substr(3, 2), nullptr, 16) / 255.0;
            b = std::stoi(hex.substr(5, 2), nullptr, 16) / 255.0;
        } catch (...) {
            return false;
        }
        return true;
    }

    // Maps real time onto vertical position within the timeline canvas.
    // The panel's inverted layout — future above center, past below —
    // lives entirely in y_for's sign, and every tick and band position
    // is derived from it rather than each site flipping the sign for
    // itself (which is what five separate copies of this arithmetic
    // used to mean).
    //
    // Built fresh per draw call rather than stored as state: height and
    // now both change constantly, and deriving position from real
    // elapsed time every frame is what keeps motion correct when a
    // timer tick arrives late, instead of accumulating drift.
    struct TimelineGeometry {
        double center_y;
        time_t now;

        TimelineGeometry(int height, time_t now_) : center_y(height / 2.0), now(now_) {}

        // Minus, not plus — a later time sits higher up the screen.
        double y_for(time_t t) const {
            return center_y - static_cast<double>(t - now) * PIXELS_PER_SECOND;
        }

        // A time interval's vertical extent, as the {y, height} pair
        // Cairo's rectangle() wants. start <= end chronologically, and
        // y_for inverts, so end is unconditionally the top edge — the
        // min/max each band site used to need existed only because each
        // one applied the sign flip itself and so couldn't assume which
        // of its two values came out on top.
        struct Span { double y, height; };
        Span span_for(time_t start, time_t end) const {
            double top = y_for(end);
            return { top, y_for(start) - top };
        }
    };

    // The vertical strip every band is drawn in, right-anchored against
    // the panel's edge.
    //
    // A pure function of the panel's width now. It used to read the dock
    // widget's actual allocation, because the dock sat directly above the
    // bands and any disagreement between the two was visible; with the dock
    // moved out of the timeline there is nothing left to agree with, so the
    // policy can simply be applied where it's needed.
    struct BandColumn {
        double x;
        double width;
        double center() const { return x + width / 2.0; }
    };

    BandColumn band_column(int panel_width) {
        const double wanted = panel_width * BAND_WIDTH_FRACTION;
        const double room = panel_width - BAND_RIGHT_MARGIN - LEFT_GUTTER;
        // min() keeps bands clear of the tick labels; max() stops them
        // collapsing when the panel is too narrow to honour both.
        const double width = std::max(BAND_MIN_WIDTH, std::min({ wanted, room, BAND_MAX_WIDTH }));
        return { panel_width - BAND_RIGHT_MARGIN - width, width };
    }

    // One band of worked time. Every band the panel draws goes through
    // here — today's completed history and the currently-elapsing one
    // alike — so "a band looks the same regardless of status" is
    // structural rather than a convention two separate blocks had to
    // keep agreeing on.
    //
    // color is a project color or "" — anything unparseable falls back
    // to BAND_DEFAULT_*, deliberately grey rather than gold so it never
    // competes with gold's role as the "now" accent.
    void draw_band(const Cairo::RefPtr<Cairo::Context>& cr, const TimelineGeometry& geo,
                   const BandColumn& col, time_t start, time_t end, const std::string& color) {
        double r, g, b;
        if (!parse_hex_color(color, r, g, b)) {
            r = BAND_DEFAULT_R;
            g = BAND_DEFAULT_G;
            b = BAND_DEFAULT_B;
        }
        cr->set_source_rgba(r, g, b, BAND_ALPHA);

        auto span = geo.span_for(start, end);
        cr->rectangle(col.x, span.y, col.width, span.height);
        cr->fill();
    }

    // The panel's gold perimeter. A plain rectangle again: with the dock
    // lifted out of the timeline there is nothing for it to break around.
    // Inset by BORDER_HALF_WIDTH so the centred stroke isn't clipped at the
    // true edge.
    void draw_panel_border(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
        cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
        cr->set_line_width(STROKE_WIDTH);
        cr->rectangle(BORDER_HALF_WIDTH, BORDER_HALF_WIDTH,
                      width - STROKE_WIDTH, height - STROKE_WIDTH);
        cr->stroke();
    }

    // The present-moment marker: a gold line running in from the left
    // edge into a flag-shaped clock whose triangular point IS the line's
    // endpoint.
    void draw_now_marker(const Cairo::RefPtr<Cairo::Context>& cr, double center_y, time_t now) {
        std::tm tm_buf{};
        localtime_r(&now, &tm_buf);
        Hour12 h12 = to_12_hour(tm_buf);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d %s", h12.hour, tm_buf.tm_min, h12.am_pm);

        cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cr->set_font_size(20);
        Cairo::TextExtents extents;
        cr->get_text_extents(buf, extents);

        // CLOCK_CENTER_X centers the rectangular body only — the point is
        // an extension off its right edge, not part of what's centered.
        const double box_w = extents.width + CLOCK_PAD_X * 2;
        const double box_h = extents.height + CLOCK_PAD_Y * 2;
        const double box_x = CLOCK_CENTER_X - box_w / 2.0;
        const double box_y = center_y - box_h / 2.0;

        // An equilateral point: with box_h as its base, the slanted edges
        // match that base at sqrt(3)/2 times its length.
        const double point_len = box_h * 0.8660254;

        // The line runs only as far as the flag it leads into.
        cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
        cr->rectangle(0, center_y - 1.5, box_x, 3);
        cr->fill();

        // Filled as a closed rectangle but bordered on three sides only —
        // no right edge, so no stroke sits between the dark fill and the
        // gold point. That seam is what made the two read as separate
        // shapes rather than one flag silhouette.
        cr->set_source_rgb(BG_R, BG_G, BG_B);
        cr->rectangle(box_x, box_y, box_w, box_h);
        cr->fill();

        cr->move_to(box_x + box_w, box_y);         // top-right (unstroked start)
        cr->line_to(box_x, box_y);                 // top
        cr->line_to(box_x, box_y + box_h);         // left
        cr->line_to(box_x + box_w, box_y + box_h); // bottom
        cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
        cr->set_line_width(STROKE_WIDTH);
        cr->stroke();

        // No horizontal offset — any gap leaves a 1px sliver that's
        // invisible mid-height but shows as a notch top and bottom where
        // gold border flanks it. A vertical offset IS needed: the top and
        // bottom strokes bleed BORDER_HALF_WIDTH past box_y and
        // box_y + box_h, so the point's vertices must reach that same
        // outer edge or the two shapes meet with a step.
        cr->move_to(box_x + box_w, box_y - BORDER_HALF_WIDTH);
        cr->line_to(box_x + box_w + point_len, center_y);
        cr->line_to(box_x + box_w, box_y + box_h + BORDER_HALF_WIDTH);
        cr->close_path();
        cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
        cr->fill();

        cr->set_source_rgb(CLOCK_TEXT_R, CLOCK_TEXT_G, CLOCK_TEXT_B);
        cr->move_to(CLOCK_CENTER_X - extents.width / 2.0 - extents.x_bearing,
                    center_y - extents.height / 2.0 - extents.y_bearing);
        cr->show_text(buf);
    }

}

SchedulePanel::SchedulePanel(TreeController& projects, Work& worklog, TaskAttributes& task_attributes)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_projects(projects), m_work(worklog), m_task_attributes(task_attributes)
{
    initialize_layout();
    refresh_completed_bands();

    // If the staged task's title changes (or it gets removed) elsewhere —
    // e.g. edited or deleted directly in TreePanel — keep the slot honest.
    // Deferred to idle for the same reason TaskPanel defers its refresh:
    // avoids rebuilding state while GTK's still mid-delivery of whatever
    // triggered the change.
    m_projects.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh_staged_label(); });
    });

    // Same reason — a color change doesn't touch the tree structure, so
    // the signal above wouldn't fire for it, but the slot's tinting still
    // needs to update if you color the project of whatever's staged.
    m_task_attributes.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh_staged_label(); });
    });

    // Recorded history changed — a segment was banked by a pause, or a
    // task completed. Re-pull the day's bands rather than each button
    // handler remembering to do it: Work also banks time on its own
    // when a task being worked is deleted elsewhere, which no handler
    // here is involved in at all.
    m_work.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh_completed_bands(); });
    });

    // Recomputes tick and band positions from real elapsed time each
    // tick, rather than nudging them — so motion stays exactly correct
    // even if a tick is delayed, instead of drifting further off over time.
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &SchedulePanel::on_timer_tick), 100);
}

void SchedulePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    // --- the staged-task dock, across the top ---
    m_activate_button.set_tooltip_text("Activate");
    m_activate_button.set_sensitive(false); // nothing staged yet
    m_activate_button.set_opacity(0.0); // not visible(false) — see class doc comment for why
    m_activate_button.signal_clicked().connect(sigc::mem_fun(*this, &SchedulePanel::on_activate_clicked));

    m_complete_button.set_tooltip_text("Mark complete");
    m_complete_button.set_sensitive(false); // nothing staged yet, or staged but not active
    m_complete_button.set_opacity(0.0); // not visible(false) — see class doc comment for why
    m_complete_button.signal_clicked().connect(sigc::mem_fun(*this, &SchedulePanel::on_complete_clicked));

    m_staged_buttons_row.append(m_activate_button);
    m_staged_buttons_row.append(m_complete_button);
    m_staged_buttons_row.set_halign(Gtk::Align::START);

    // Ellipsizes against whatever width the dock gets. No character cap
    // needed: the dock spans the panel, so its width is settled by the
    // parent and a long title can't stretch it.
    m_slot_label.set_ellipsize(Pango::EllipsizeMode::END);
    m_slot_label.set_xalign(0.0);
    m_slot_label.set_halign(Gtk::Align::FILL);

    m_staged_box.append(m_staged_buttons_row);
    m_staged_box.append(m_slot_label);
    m_staged_box.add_css_class("staged-dock");
    m_staged_box.set_hexpand(true);
    m_staged_box.set_margin_bottom(DOCK_TIMELINE_GAP);

    // --- the timeline, filling everything below it ---
    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));

    append(m_staged_box);
    append(m_timeline);
}

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------

void SchedulePanel::draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    // Explicit background so the timeline reads clearly regardless of
    // whatever's behind it — this is the "measuring tape" backdrop.
    cr->set_source_rgb(BG_R, BG_G, BG_B);
    cr->paint();

    // Every band shares this column, so a task's own label length never
    // determines how wide its band is and completed bands don't shift when
    // something else is staged later.
    const BandColumn col = band_column(width);

    time_t now = std::time(nullptr);
    TimelineGeometry geo{height, now};

    // Completed sessions from earlier today — drawn first, before the
    // tick marks, so those stay legible on top. The left third of the
    // panel is reserved for pure time markers; nothing task-related is
    // drawn over it.
    for (const auto& entry : m_completed_bands) {
        draw_band(cr, geo, col, entry.start_time, entry.end_time, entry.color);
    }

    // The current-session band, if any. Reads straight from Work
    // rather than tracking its own copy of session start/end, and goes
    // through the same draw_band as the completed ones above.
    //
    // No live-vs-paused branching needed: Work has exactly one
    // active-session slot, and every path out of it — pause, complete,
    // and the task being deleted from under it — banks the segment and
    // empties the slot within the same call. So an active session is
    // always one that's genuinely still running, and this band's far
    // edge is always "now."
    if (m_work.has_active_session()) {
        // Passing now as the interval's end makes the band terminate
        // exactly on the line, since y_for(geo.now) is center_y by
        // definition — no separate special case for "the far edge is
        // always now."
        draw_band(cr, geo, col, m_work.session_start(), now,
                  m_task_attributes.get_color(m_work.active_task_id()));
    }

    // Align to the nearest quarter-hour boundary at or before "now".
    // Safe for real-world time zones: every UTC offset in use today is a
    // multiple of 15 minutes, so epoch-aligned quarter-hours line up with
    // local wall-clock quarter-hours too.
    time_t aligned = now - (now % QUARTER_HOUR_SECONDS);

    // How many quarter-hour ticks fit from center to each edge, plus a
    // couple extra so ticks don't visibly pop in at the boundary.
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
            cr->set_source_rgb(TICK_HOUR_R, TICK_HOUR_G, TICK_HOUR_B);
        } else if (tm_buf.tm_min == 30) {
            tick_length = 24.0;
            cr->set_source_rgb(TICK_HALF_R, TICK_HALF_G, TICK_HALF_B);
        } else {
            tick_length = 14.0;
            cr->set_source_rgb(TICK_QUARTER_R, TICK_QUARTER_G, TICK_QUARTER_B);
        }

        cr->set_line_width(STROKE_WIDTH);
        cr->move_to(0, y);
        cr->line_to(tick_length, y);
        cr->stroke();

        if (draw_label) {
            char buf[16];
            Hour12 h12 = to_12_hour(tm_buf);
            std::snprintf(buf, sizeof(buf), "%d %s", h12.hour, h12.am_pm);
            cr->set_source_rgb(TICK_HOUR_R, TICK_HOUR_G, TICK_HOUR_B);
            cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cr->set_font_size(13);
            cr->move_to(tick_length + 8, y + 4);
            cr->show_text(buf);
        }
    }

    // The present moment, over the ticks it sits among.
    draw_now_marker(cr, geo.center_y, now);

    // The perimeter last, so no tick or band interrupts it.
    draw_panel_border(cr, width, height);
}

// ---------------------------------------------------------------------
// Live updates
// ---------------------------------------------------------------------

bool SchedulePanel::on_timer_tick() {
    m_timeline.queue_draw(); // the clock reads live, so this can't be drawn once and left
    return true; // keep repeating
}

void SchedulePanel::on_activate_clicked() {
    if (m_work.has_active_session()) {
        m_work.pause();
    } else {
        if (m_staged_id == -1) return; // button should be insensitive anyway; safety net
        m_work.start(m_staged_id);
    }
    refresh_staged_label(); // updates the play/pause icon and Complete's visibility together
}

void SchedulePanel::on_complete_clicked() {
    if (m_staged_id == -1) return;

    m_work.complete(m_staged_id);
    m_staged_id = -1;
    refresh_staged_label();
}

void SchedulePanel::stage_task(int task_id) {
    if (m_work.has_active_session()) return; // can't swap out a task that's actively being worked —
                                                 // deliberately silent for now; revisit if this needs
                                                 // a visible cue (message, shake, etc.) later
    m_staged_id = task_id;
    refresh_staged_label();
}

void SchedulePanel::refresh_completed_bands() {
    m_completed_bands = m_work.entries_for_day(std::time(nullptr));
}

void SchedulePanel::refresh_staged_label() {
    if (m_staged_id != -1 && !m_projects.contains(m_staged_id)) {
        // Staged task vanished out from under us (e.g. removed directly
        // in TreePanel) — clear the slot rather than show stale text.
        m_staged_id = -1;
    }

    if (m_staged_id == -1) {
        m_slot_label.set_text("");
        m_activate_button.set_sensitive(false);
        m_activate_button.set_opacity(0.0);
        m_complete_button.set_sensitive(false);
        m_complete_button.set_opacity(0.0);
        return;
    }

    // Same "path - title" shape as Backlog and Completed Today.
    TaskSnapshot snap = m_task_attributes.snapshot(m_staged_id);
    std::string text = (snap.path.empty() ? "" : snap.path + " - ") + snap.title;

    // Always explicit markup, never plain set_text(): the dock has a dark
    // background of its own (see MainWindow::apply_styles), so a light
    // theme's default text color would come out dark-on-dark.
    // DEFAULT_TEXT_COLOR covers the "no project color" case.
    std::string effective_color = snap.color.empty() ? DEFAULT_TEXT_COLOR : snap.color;
    m_slot_label.set_markup("<span foreground='" + effective_color + "'>" + Glib::Markup::escape_text(text) + "</span>");

    m_activate_button.set_sensitive(true);
    m_activate_button.set_opacity(1.0);

    // Only two states are ever meaningful: staged-but-idle is just play;
    // active is pause plus the checkmark. Complete never applies to a
    // task that hasn't been worked at all.
    bool active = m_work.has_active_session();
    m_activate_button.set_label(active ? "⏸" : "▶");
    m_activate_button.set_tooltip_text(active ? "Deactivate" : "Activate");

    // Opacity, not visible(false) — an invisible widget is removed from
    // layout entirely, which would change m_staged_box's own natural
    // size depending on whether the checkmark happens to be there,
    // resizing the whole dock every time you activate/deactivate.
    // Staying in the layout at all times, just fully transparent and
    // unclickable when it doesn't apply, keeps the dock's size constant
    // regardless of state.
    m_complete_button.set_sensitive(active);
    m_complete_button.set_opacity(active ? 1.0 : 0.0);
}
