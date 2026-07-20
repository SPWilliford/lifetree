#include "view/SchedulePanel.hpp"
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
#include "engine/TaskAttributes.hpp"
#include <pangomm/layout.h>
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

    // tm_hour is 24-hour — this is the repeated bit of converting it for
    // display, shared by the tick labels and the clock box.
    struct Hour12 { int hour; const char* am_pm; };
    Hour12 to_12_hour(const std::tm& tm_buf) {
        int hour = tm_buf.tm_hour % 12;
        if (hour == 0) hour = 12;
        return { hour, (tm_buf.tm_hour < 12) ? "AM" : "PM" };
    }
}

SchedulePanel::SchedulePanel(ITreeController& projects, WorkLog& worklog, TaskAttributes& task_attributes)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_projects(projects), m_worklog(worklog), m_task_attributes(task_attributes)
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

    // Recomputes tick and band positions from real elapsed time each
    // tick, rather than nudging them — so motion stays exactly correct
    // even if a tick is delayed, instead of drifting further off over time.
    Glib::signal_timeout().connect(sigc::mem_fun(*this, &SchedulePanel::on_timer_tick), 100);
}

void SchedulePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    // --- header: staged-task row, confined to the right half to mirror
    //     the timeline split below. The clock used to live here too, but
    //     it's moved into the now-line itself (see draw_now_line) ---
    m_header.set_margin(12);
    m_header.set_halign(Gtk::Align::FILL);
    m_header.set_hexpand(true);

    m_slot_label.add_css_class("dim-label");
    // Caps how wide this is allowed to *want* to be — without this, a
    // long "path - title" string has no upper bound and forces the whole
    // panel wider than the Paned actually allocated it, which is exactly
    // what was overflowing behind TreePanel. Ellipsize means it happily
    // accepts less than max_width_chars too, truncating with "…" rather
    // than ever demanding more room than it's given.
    m_slot_label.set_ellipsize(Pango::EllipsizeMode::END);
    m_slot_label.set_max_width_chars(40);
    m_slot_row.set_halign(Gtk::Align::CENTER);
    m_slot_row.append(m_slot_label);

    m_slot_buttons_row.set_halign(Gtk::Align::CENTER);
    m_slot_buttons_row.append(m_activate_button);
    m_slot_buttons_row.append(m_complete_button);
    m_slot_row.append(m_slot_buttons_row);

    // Homogeneous specifically so this is an exact 50/50 width split —
    // matching draw_timeline()'s width/2.0 — regardless of how wide
    // m_slot_row's own content happens to be.
    m_split_row.set_homogeneous(true);
    m_split_row.set_hexpand(true);
    m_left_spacer.set_hexpand(true);
    m_slot_row.set_hexpand(true);
    m_split_row.append(m_left_spacer);
    m_split_row.append(m_slot_row);
    m_header.append(m_split_row);

    m_activate_button.set_sensitive(false); // nothing staged yet
    m_activate_button.set_visible(false);
    m_activate_button.signal_clicked().connect(sigc::mem_fun(*this, &SchedulePanel::on_activate_clicked));

    m_complete_button.set_sensitive(false); // nothing staged yet
    m_complete_button.set_visible(false);
    m_complete_button.signal_clicked().connect(sigc::mem_fun(*this, &SchedulePanel::on_complete_clicked));

    // --- body: scrolling timeline, with a fixed now-line overlaid on top ---
    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));
    m_body.set_child(m_timeline);

    // Tall enough for the clock box (see draw_now_line) rather than just
    // a thin strip — still vertically centered on the timeline, same
    // position the plain line used to mark.
    m_now_line.set_size_request(-1, 48);
    m_now_line.set_halign(Gtk::Align::FILL);
    m_now_line.set_valign(Gtk::Align::CENTER);
    m_now_line.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_now_line));
    m_body.add_overlay(m_now_line);

    m_body.set_hexpand(true);
    m_body.set_vexpand(true);
    append(m_body);

    // Appended after the body, not before — the staged-task slot sits
    // below the timeline now, matching the future-at-top/past-at-bottom
    // flip: this row is where "the present" effectively lives, so it
    // belongs at the boundary closest to where new time keeps entering
    // the view from beneath.
    append(m_header);
}

// ---------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------

void SchedulePanel::draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    // Explicit background so the timeline reads clearly regardless of
    // whatever's behind it — this is the "measuring tape" backdrop.
    cr->set_source_rgb(0.13, 0.13, 0.15);
    cr->paint();

    double center_y = height / 2.0;
    double right_half_x = width / 2.0;
    time_t now = std::time(nullptr);

    // Completed sessions from earlier today — drawn first, in a cooler,
    // more settled color than the current session's amber, so "already
    // logged" reads differently from "still in progress." Confined to
    // the right half — the left half is reserved for pure time markers,
    // nothing task-related drawn over it.
    cr->set_source_rgba(0.35, 0.55, 0.75, 0.25);
    for (const auto& entry : m_completed_bands) {
        // Minus, not plus — future is above center, past is below.
        // start_time is always <= end_time chronologically, but that
        // means the *larger* time value maps to the *smaller* y (higher
        // up the screen) under this sign, so min/max rather than
        // assuming which one comes out on top.
        double y_a = center_y - static_cast<double>(entry.start_time - now) * PIXELS_PER_SECOND;
        double y_b = center_y - static_cast<double>(entry.end_time - now) * PIXELS_PER_SECOND;
        double y_top = std::min(y_a, y_b);
        double y_bottom = std::max(y_a, y_b);
        cr->rectangle(right_half_x, y_top, width - right_half_x, y_bottom - y_top);
        cr->fill();
    }

    // The current-session band, if any — drawn before the tick marks so
    // they stay legible on top of it. Reads straight from WorkLog rather
    // than tracking its own copy of session start/end. Same right-half
    // confinement, and same min/max reasoning, as the completed bands
    // above.
    if (m_worklog.session_start() != 0) {
        bool live = m_worklog.has_active_session();
        time_t band_end = live ? now : m_worklog.session_end();
        double y_a = center_y - static_cast<double>(m_worklog.session_start() - now) * PIXELS_PER_SECOND;
        double y_b = center_y - static_cast<double>(band_end - now) * PIXELS_PER_SECOND;
        double y_top = std::min(y_a, y_b);
        double y_bottom = std::max(y_a, y_b);

        cr->set_source_rgba(0.95, 0.75, 0.2, live ? 0.35 : 0.2);
        cr->rectangle(right_half_x, y_top, width - right_half_x, y_bottom - y_top);
        cr->fill();
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
        double y = center_y - static_cast<double>(tick_time - now) * PIXELS_PER_SECOND; // minus — future above, past below
        if (y < -20 || y > height + 20) continue;

        std::tm tm_buf{};
        localtime_r(&tick_time, &tm_buf);

        double tick_length;
        bool draw_label = false;
        if (tm_buf.tm_min == 0) {
            tick_length = 36.0;
            draw_label = true;
            cr->set_source_rgb(0.85, 0.85, 0.9);
        } else if (tm_buf.tm_min == 30) {
            tick_length = 24.0;
            cr->set_source_rgb(0.6, 0.6, 0.65);
        } else {
            tick_length = 14.0;
            cr->set_source_rgb(0.4, 0.4, 0.45);
        }

        cr->set_line_width(2.0);
        cr->move_to(0, y);
        cr->line_to(tick_length, y);
        cr->stroke();

        if (draw_label) {
            char buf[16];
            Hour12 h12 = to_12_hour(tm_buf);
            std::snprintf(buf, sizeof(buf), "%d %s", h12.hour, h12.am_pm);
            cr->set_source_rgb(0.85, 0.85, 0.9);
            cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cr->set_font_size(13);
            cr->move_to(tick_length + 8, y + 4);
            cr->show_text(buf);
        }
    }
}

void SchedulePanel::draw_now_line(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    double center_y = height / 2.0;

    // The gold "now" line, spanning the full width — same role it always
    // had, just thinner than this widget's height now that there's room
    // reserved here for the clock box below.
    cr->set_source_rgba(0.95, 0.75, 0.2, 0.9);
    cr->rectangle(0, center_y - 1.5, width, 3);
    cr->fill();

    // The clock, boxed in gold, centered in the left half — sitting
    // right on the line. Recomputed fresh from real time on every draw,
    // same as everything else here, rather than tracked as separate
    // state that could drift out of sync with it.
    time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    Hour12 h12 = to_12_hour(tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", h12.hour, tm_buf.tm_min, h12.am_pm);

    cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cr->set_font_size(20);
    Cairo::TextExtents extents;
    cr->get_text_extents(buf, extents);

    constexpr double PAD_X = 16.0, PAD_Y = 10.0;
    double box_w = extents.width + PAD_X * 2;
    double box_h = extents.height + PAD_Y * 2;
    double left_half_center = width / 4.0; // center of the left half specifically, not the whole panel
    double box_x = left_half_center - box_w / 2.0;
    double box_y = center_y - box_h / 2.0;

    // Dark fill matching the timeline's own background, so the box reads
    // as cut into the line rather than sitting flatly on top of it.
    cr->set_source_rgb(0.13, 0.13, 0.15);
    cr->rectangle(box_x, box_y, box_w, box_h);
    cr->fill();

    cr->set_source_rgba(0.95, 0.75, 0.2, 0.9);
    cr->set_line_width(2.0);
    cr->rectangle(box_x, box_y, box_w, box_h);
    cr->stroke();

    cr->set_source_rgb(0.95, 0.95, 0.95);
    cr->move_to(
        left_half_center - extents.width / 2.0 - extents.x_bearing,
        center_y - extents.height / 2.0 - extents.y_bearing
    );
    cr->show_text(buf);
}

// ---------------------------------------------------------------------
// Live updates
// ---------------------------------------------------------------------

bool SchedulePanel::on_timer_tick() {
    m_timeline.queue_draw();
    m_now_line.queue_draw(); // its clock box needs redrawing now, not just a static-looking bar
    return true; // keep repeating
}

void SchedulePanel::on_activate_clicked() {
    if (m_worklog.has_active_session()) {
        m_worklog.end_active_session();
        m_activate_button.set_label("Activate");
    } else {
        if (m_staged_id == -1) return; // button should be disabled anyway; just a safety net
        m_worklog.start_session(m_staged_id);
        m_activate_button.set_label("Deactivate");
    }
}

void SchedulePanel::on_complete_clicked() {
    if (m_staged_id == -1) return;

    if (m_worklog.has_active_session()) {
        // Freeze the worked-time band rather than discard it — same as
        // a normal deactivate, just happening as part of completing.
        m_worklog.end_active_session();
        m_activate_button.set_label("Activate");
    }

    // Only log real, matching work time. If the last recorded session
    // belongs to a *different* task — e.g. it was deactivated but never
    // completed, then something else got staged and worked instead —
    // don't attribute that time to whatever's being completed now.
    if (m_worklog.active_task_id() == m_staged_id && m_worklog.session_start() != 0) {
        std::string title = m_projects.get_title(m_staged_id);

        // Same generator-skip as refresh_staged_label() — must happen
        // before remove() below, since the node (and its parent_of())
        // won't be walkable once it's gone.
        int parent_id = m_projects.parent_of(m_staged_id);
        std::string path = m_task_attributes.is_generator(parent_id)
            ? m_projects.ancestor_path(parent_id)
            : m_projects.ancestor_path(m_staged_id);
        m_worklog.record_completion(title, path, m_worklog.session_start(), m_worklog.session_end());

        // The session that band represented is now permanent history —
        // retire the "current" slot so draw_timeline() doesn't keep
        // drawing it as if it were still live, redundant with the band
        // refresh_completed_bands() is about to add.
        m_worklog.clear_session();
    }

    m_projects.remove(m_staged_id); // the only place a task actually gets deleted now
    m_staged_id = -1;
    refresh_staged_label();
    refresh_completed_bands();
}

void SchedulePanel::stage_task(int task_id) {
    if (m_worklog.has_active_session()) return; // can't swap out a task that's actively being worked —
                                                 // deliberately silent for now; revisit if this needs
                                                 // a visible cue (message, shake, etc.) later
    m_staged_id = task_id;
    refresh_staged_label();
}

void SchedulePanel::refresh_completed_bands() {
    m_completed_bands = m_worklog.entries_for_day(std::time(nullptr));
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
        m_activate_button.set_visible(false);
        m_complete_button.set_sensitive(false);
        m_complete_button.set_visible(false);
        return;
    }

    // Same "path - title" shape as Backlog and Completed Today. If the
    // immediate parent is a generator, its title duplicates this
    // instance's own title (that's how spawning works) — skip straight
    // to its ancestors instead.
    int parent_id = m_projects.parent_of(m_staged_id);
    std::string path = m_task_attributes.is_generator(parent_id)
        ? m_projects.ancestor_path(parent_id)
        : m_projects.ancestor_path(m_staged_id);
    m_slot_label.set_text((path.empty() ? "" : path + " - ") + m_projects.get_title(m_staged_id));
    m_activate_button.set_sensitive(true);
    m_activate_button.set_visible(true);
    m_complete_button.set_sensitive(true);
    m_complete_button.set_visible(true);
}
