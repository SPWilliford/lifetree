#include "view/SchedulePanel.hpp"
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
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
}

SchedulePanel::SchedulePanel(ITreeController& projects, WorkLog& worklog)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_projects(projects), m_worklog(worklog)
{
    initialize_layout();
    update_clock_label();

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

    // --- header: clock on top, staged-task row directly beneath it ---
    m_header.set_margin(12);
    m_header.set_halign(Gtk::Align::CENTER);
    m_clock_label.set_markup("<span size='xx-large' weight='bold'>--:--</span>");
    m_header.append(m_clock_label);

    m_slot_label.add_css_class("dim-label");
    m_slot_row.set_halign(Gtk::Align::CENTER);
    m_slot_row.append(m_slot_label);
    m_slot_row.append(m_activate_button);
    m_slot_row.append(m_complete_button);
    m_header.append(m_slot_row);

    append(m_header);

    m_activate_button.set_sensitive(false); // nothing staged yet
    m_activate_button.signal_clicked().connect(sigc::mem_fun(*this, &SchedulePanel::on_activate_clicked));

    m_complete_button.set_sensitive(false); // nothing staged yet
    m_complete_button.signal_clicked().connect(sigc::mem_fun(*this, &SchedulePanel::on_complete_clicked));

    // --- body: scrolling timeline, with a fixed now-line overlaid on top ---
    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));
    m_body.set_child(m_timeline);

    m_now_line.set_size_request(-1, 3);
    m_now_line.set_halign(Gtk::Align::FILL);
    m_now_line.set_valign(Gtk::Align::CENTER);
    m_now_line.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_now_line));
    m_body.add_overlay(m_now_line);

    m_body.set_hexpand(true);
    m_body.set_vexpand(true);
    append(m_body);
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
    time_t now = std::time(nullptr);

    // The worked-time band, if any — drawn first so tick marks stay
    // legible on top of it. Reads straight from WorkLog rather than
    // tracking its own copy of session start/end.
    if (m_worklog.session_start() != 0) {
        bool live = m_worklog.has_active_session();
        time_t band_end = live ? now : m_worklog.session_end();
        double y_start = center_y + static_cast<double>(m_worklog.session_start() - now) * PIXELS_PER_SECOND;
        double y_end = center_y + static_cast<double>(band_end - now) * PIXELS_PER_SECOND;

        cr->set_source_rgba(0.95, 0.75, 0.2, live ? 0.35 : 0.2);
        cr->rectangle(0, y_start, width, y_end - y_start);
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
        double y = center_y + static_cast<double>(tick_time - now) * PIXELS_PER_SECOND;
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
            int hour12 = tm_buf.tm_hour % 12;
            if (hour12 == 0) hour12 = 12;
            const char* am_pm = (tm_buf.tm_hour < 12) ? "AM" : "PM";
            std::snprintf(buf, sizeof(buf), "%d %s", hour12, am_pm);
            cr->set_source_rgb(0.85, 0.85, 0.9);
            cairo_select_font_face(cr->cobj(), "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
            cr->set_font_size(13);
            cr->move_to(tick_length + 8, y + 4);
            cr->show_text(buf);
        }
    }
}

void SchedulePanel::draw_now_line(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    (void)width;
    (void)height;
    cr->set_source_rgba(0.95, 0.75, 0.2, 0.9);
    cr->paint();
}

// ---------------------------------------------------------------------
// Live updates
// ---------------------------------------------------------------------

bool SchedulePanel::on_timer_tick() {
    m_timeline.queue_draw();
    update_clock_label();
    return true; // keep repeating
}

void SchedulePanel::update_clock_label() {
    time_t now = std::time(nullptr);
    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);

    // Display drops seconds and uses 12-hour format — the underlying
    // time_t (used for all the scrolling/band math) still has full
    // second-level precision, this only affects what's shown here.
    int hour12 = tm_buf.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;
    const char* am_pm = (tm_buf.tm_hour < 12) ? "AM" : "PM";

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, tm_buf.tm_min, am_pm);

    std::string markup = "<span size='xx-large' weight='bold'>" + std::string(buf) + "</span>";
    m_clock_label.set_markup(markup);
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
        std::string path = m_projects.ancestor_path(m_staged_id); // must happen before remove() — the node won't exist to walk afterward
        m_worklog.record_completion(title, path, m_worklog.session_start(), m_worklog.session_end());
    }

    m_projects.remove(m_staged_id); // the only place a task actually gets deleted now
    m_staged_id = -1;
    refresh_staged_label();
}

void SchedulePanel::stage_task(int task_id) {
    if (m_worklog.has_active_session()) return; // can't swap out a task that's actively being worked —
                                                 // deliberately silent for now; revisit if this needs
                                                 // a visible cue (message, shake, etc.) later
    m_staged_id = task_id;
    refresh_staged_label();
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
        m_complete_button.set_sensitive(false);
        return;
    }

    m_slot_label.set_text(m_projects.get_title(m_staged_id));
    m_activate_button.set_sensitive(true);
    m_complete_button.set_sensitive(true);
}
