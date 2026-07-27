#include "view/SchedulePanel.hpp"
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
#include "engine/TaskAttributes.hpp"
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

    // The clock's horizontal center, and the band/dock's width — both
    // fixed pixel values now, not fractions of the panel's current
    // width. That's deliberate: with fixed values, resizing the panel
    // only ever changes the *empty* middle space between them — the
    // clock and the task dock themselves stay genuinely solid,
    // completely unaffected by how the side panels get dragged. This
    // also removes an entire class of resize-lag bugs that fractional
    // sizing had: the band was being recomputed fresh on every draw
    // call, but the real GTK widgets (the label, the button row) could
    // only catch up to a new width one idle-cycle later, so during an
    // active drag the drawn band and the actual widgets could
    // momentarily disagree — visibly, as the label sticking outside its
    // own drawn border. Fixed values need no re-syncing at all, so
    // there's nothing left that can drift out of sync.
    //
    // CLOCK_CENTER_X specifically needs to clear the hour-tick labels,
    // which extend out to roughly tick_length(36) + gap(8) + their own
    // text width — an estimate, not a measured value (no way to measure
    // real font metrics without an actual display), so treat this as a
    // starting point to nudge rather than an exact number.
    constexpr double CLOCK_CENTER_X = 170.0;
    constexpr double BAND_WIDTH = 280.0;

    // How far the band/label/buttons sit from the panel's actual right
    // edge — a little breathing room, not a lot. Shared between the
    // Cairo geometry below and m_staged_box's own GTK alignment (see
    // initialize_layout), so both anchor to the same reference point
    // (the panel's right edge) with the same margin, rather than each
    // independently computing "centered in the task region" and only
    // agreeing by coincidence.
    constexpr double BAND_RIGHT_MARGIN = 20.0;

    // How far above the now-line the staged-task box (buttons + label)
    // rests, rather than sitting flush against it.
    constexpr int STAGED_BOX_GAP_ABOVE_LINE = 11;

    // Padding around the staged-task box's content (buttons + label),
    // inside its drawn border — shared between draw_now_line (which
    // draws the border) and initialize_layout (which needs to know how
    // tall m_now_line has to be to avoid clipping that border).
    constexpr double TASK_BOX_PAD_X = 14.0;
    constexpr double TASK_BOX_PAD_Y = 8.0;

    // Padding around just the label's own text, inside its own smaller
    // border (separate from the whole dock's outer one — see
    // draw_now_line).
    constexpr double LABEL_BORDER_PAD_X = 10.0;
    constexpr double LABEL_BORDER_PAD_Y = 4.0;

    // Space between the button row and the label below it — needs to
    // clear the label's own drawn border, which reaches
    // LABEL_BORDER_PAD_Y + half a stroke width above the label's top.
    constexpr int BUTTONS_LABEL_GAP = 8;

    // Colors — named once so the same RGB(A) tuples can't quietly drift
    // out of sync with each other across the several places each one is
    // used, and so intent reads at the call site instead of three bare
    // decimals.
    constexpr double BG_R = 0.13, BG_G = 0.13, BG_B = 0.15;             // dark background — timeline, clock box, task dock
    constexpr double GOLD_R = 0.95, GOLD_G = 0.75, GOLD_B = 0.2;        // the "now" accent — line, both borders
    constexpr double CLOCK_TEXT_R = 0.95, CLOCK_TEXT_G = 0.95, CLOCK_TEXT_B = 0.95;

    // The staged label's default text color when its task's project has
    // no color set — explicit (matching CLOCK_TEXT_R/G/B) rather than
    // just leaving it to GTK's theme default, specifically so there's a
    // known value to also draw the label's own border in (see
    // draw_now_line) — a theme-default color isn't something this file
    // has an RGB value for at all. Kept as a hex string, not RGB
    // doubles, since it's used directly in Pango markup.
    constexpr const char* DEFAULT_TEXT_COLOR = "#f2f2f2";

    // A single neutral fallback for any band whose project has no color
    // set — same for a completed band or the currently-elapsing one, no
    // distinction by status. Grey rather than gold specifically so it
    // doesn't compete with gold's existing role as the "now" accent (the
    // line, the clock, the dock border) — this is deliberately just
    // "time that was worked," nothing more. Close to the tick marks'
    // own grey palette, so it reads as part of the same family rather
    // than a fourth, unrelated color.
    constexpr double BAND_DEFAULT_R = 0.65, BAND_DEFAULT_G = 0.65, BAND_DEFAULT_B = 0.7;

    // One line width for every border/stroke this file draws (the line,
    // both boxes' borders, tick marks) — was previously the bare number
    // 2.0 repeated independently in ~4 places, with a separately
    // hardcoded "half of it" (1.0, used to align the clock's triangle
    // point against the border's outer edge) that only stayed correct
    // because nobody had changed one without the other yet.
    constexpr double STROKE_WIDTH = 2.0;
    constexpr double BORDER_HALF_WIDTH = STROKE_WIDTH / 2.0;

    // Padding around the clock's own text, inside its drawn box — kept
    // at file scope (not local to draw_now_line, like it was before) to
    // match TASK_BOX_PAD_X/Y's scope, since both play the same
    // conceptual role for their respective boxes.
    constexpr double CLOCK_PAD_X = 16.0;
    constexpr double CLOCK_PAD_Y = 10.0;

    // One band transparency for everything — completed history, and the
    // currently-elapsing one. Deliberately no distinction by status
    // (live vs. paused vs. completed) or by whether a color is set: a
    // band is meant to feel "printed" onto the timeline the moment it
    // exists, not something whose appearance keeps signaling its current
    // state.
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

    // Where the band (and the label, and the task box) sit, given the
    // panel's current width. band_width is now a fixed constant
    // (BAND_WIDTH) — only band_x still depends on the panel's width, to
    // keep the band anchored against the actual right edge as the panel
    // resizes. Shared so draw_timeline and draw_now_line can't drift out
    // of sync with each other; both used to inline this same formula
    // independently before.
    void compute_band_geometry(int width, double& band_x, double& band_width) {
        band_width = BAND_WIDTH;
        band_x = width - BAND_RIGHT_MARGIN - band_width;
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

    // Same reason — a color change doesn't touch the tree structure, so
    // the signal above wouldn't fire for it, but the slot's tinting still
    // needs to update if you color the project of whatever's staged.
    m_task_attributes.connect_changed([this]() {
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

    // --- staged-task dock: buttons row on top, label below — overlaid
    //     directly on m_body (the timeline), not a separate header
    //     strip anymore ---
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
    // A fixed width now (BAND_WIDTH), set once here rather than synced
    // continuously from draw_timeline — see BAND_WIDTH's own comment for
    // why fixed sizing replaced the old fraction-of-panel-width version.
    // Gives the row somewhere to actually have slack in the first place;
    // without an explicit width it's exactly as wide as its two buttons,
    // which is what makes Gtk::Box's default packing (children start
    // from the left, unused space trails after them) show up as
    // left-aligned rather than just looking identically tight either way.
    m_staged_buttons_row.set_size_request(static_cast<int>(BAND_WIDTH), -1);
    // CENTER here is a no-op visually — m_staged_box (the parent) is
    // exactly BAND_WIDTH wide too, same as this row, so there's no extra
    // space within it to center against. The row's overall position
    // comes from m_staged_box's own END alignment instead (see below).
    m_staged_buttons_row.set_halign(Gtk::Align::CENTER);
    // Clearance between the buttons and the label's own drawn border
    // below them — that border's top stroke reaches LABEL_BORDER_PAD_Y
    // plus half a stroke width above the label's top edge, which the
    // buttons used to sit flush against. Set here, BEFORE the
    // get_preferred_size() measurement further down, so the cached
    // content height (and everything derived from it — m_now_line's
    // height, the dock border in draw_now_line) folds this gap in
    // automatically.
    m_staged_buttons_row.set_margin_bottom(BUTTONS_LABEL_GAP);

    // max_width_chars is back, but recalibrated — removing it entirely
    // last time was the wrong direction. set_size_request() only sets a
    // *minimum* width; it was never going to stop the label from
    // requesting its full natural size when nothing else capped it, and
    // Gtk::Box (m_staged_box) sizes itself to fit its widest child's
    // natural width regardless of that child's own size_request. Without
    // *something* bounding the label's natural width, ellipsize never
    // gets a reason to activate — that's why removing max_width_chars
    // made the label just show the full path uncapped. The old value
    // (40) was itself the real bug: it computes to roughly 320-400px for
    // typical text, well past BAND_WIDTH's 240px — so it did cap
    // *something*, just not tightly enough. 24 is a conservative
    // estimate meant to stay comfortably under 240px even for wider
    // characters — character-based sizing is inherently approximate
    // (font-dependent), so this is a starting point to nudge, not an
    // exact value.
    m_slot_label.set_max_width_chars(24);
    // No dim-label class here — this used to be secondary text in a
    // small header strip, but now it's the main content sitting right on
    // the timeline, so it shouldn't read as de-emphasized.
    m_slot_label.set_ellipsize(Pango::EllipsizeMode::END);
    m_slot_label.set_xalign(0.5); // centered text within its (fixed) width
    m_slot_label.set_halign(Gtk::Align::CENTER);
    m_slot_label.set_size_request(static_cast<int>(BAND_WIDTH), -1); // same fixed width as the button row above

    m_staged_box.append(m_staged_buttons_row);
    m_staged_box.append(m_slot_label);

    // END + margin_end, not CENTER — anchors m_staged_box to the same
    // reference point compute_band_geometry() now uses (the panel's
    // right edge, with the same BAND_RIGHT_MARGIN gap), rather than each
    // independently computing "centered in the task region" and only
    // agreeing by coincidence. m_staged_box's own natural width is
    // always exactly BAND_WIDTH now (both children are explicitly that
    // width), so END-aligning it lands its right edge at exactly the
    // same point the band's right edge is drawn at.
    m_staged_box.set_halign(Gtk::Align::END);
    m_staged_box.set_margin_end(static_cast<int>(BAND_RIGHT_MARGIN));

    // vexpand here was originally load-bearing for a Grid-specific quirk
    // (a Grid row only claims extra height if something inside it is
    // also vexpand) — now that m_staged_box is added directly as an
    // overlay child below instead of nested in a Grid, Overlay children
    // are positioned by their own align/margin against its full size
    // regardless, so this is likely no longer strictly necessary. Kept
    // anyway, defensively, since I can't fully verify Overlay's exact
    // child-sizing behavior without compiling — harmless either way.
    m_staged_box.set_vexpand(true);

    // Bottom-anchors the box's content a small gap above the line,
    // rather than resting flush on it: valign(CENTER) centers the
    // *padded* box (content + margins) on m_body's own vertical center,
    // and content sits entirely above that centerline (all the margin is
    // on the bottom) — different from m_now_line's clock box, which
    // centers its content ON the line rather than resting above it.
    // Raising the content by STAGED_BOX_GAP_ABOVE_LINE pixels means
    // adding *twice* that to the margin, not just that amount — the
    // margin grows the padded box, and centering it means only half of
    // any added margin actually shows up as visible lift.
    //
    // Measured and cached BEFORE the margin gets applied below —
    // get_preferred_size() on a widget that already has a margin set
    // folds that margin back into the reported height, so anything that
    // re-measures m_staged_box after this point (draw_now_line) would
    // get back something like double the real content height instead.
    Gtk::Requisition min_req, nat_req;
    m_staged_box.get_preferred_size(min_req, nat_req);
    m_staged_box_content_height = nat_req.get_height();

    // m_slot_label's own height alone, not the whole box — for the
    // label's own border (see draw_now_line). No margin concern here;
    // this one's unaffected by the margin about to be applied below,
    // since that margin is set on m_staged_box, not on the label itself.
    Gtk::Requisition label_min, label_nat;
    m_slot_label.get_preferred_size(label_min, label_nat);
    m_slot_label_height = label_nat.get_height();

    m_staged_box.set_valign(Gtk::Align::CENTER);
    m_staged_box.set_margin_bottom(m_staged_box_content_height + 2 * STAGED_BOX_GAP_ABOVE_LINE);

    // --- body: scrolling timeline, with the now-line and the staged-task
    //     controls both overlaid on top of it ---
    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));
    m_body.set_child(m_timeline);

    // Tall enough for whichever is taller — the clock box or the
    // staged-task border box — not just a thin strip. The staged-task
    // box extends upward from (center_y - GAP) by its content height
    // plus padding on both sides; m_now_line needs at least twice that
    // distance in total height, or its own drawing gets clipped at the
    // top (which is exactly what a hardcoded 48px was doing once the
    // box became taller than the clock alone needed). +20 is a small
    // safety buffer, not load-bearing.
    int now_line_height = static_cast<int>(2 * (STAGED_BOX_GAP_ABOVE_LINE + m_staged_box_content_height + TASK_BOX_PAD_Y)) + 20;
    m_now_line.set_size_request(-1, now_line_height);
    m_now_line.set_halign(Gtk::Align::FILL);
    m_now_line.set_valign(Gtk::Align::CENTER);
    m_now_line.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_now_line));
    m_body.add_overlay(m_now_line);
    m_body.add_overlay(m_staged_box);

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
    cr->set_source_rgb(BG_R, BG_G, BG_B);
    cr->paint();

    double center_y = height / 2.0;

    // Fixed width and right-aligned position for every band today — same
    // fixed BAND_WIDTH and right-anchoring m_staged_box itself uses (see
    // initialize_layout), not tied to any particular task's own label
    // width.
    double band_x, band_width;
    compute_band_geometry(width, band_x, band_width);
    time_t now = std::time(nullptr);

    // Completed sessions from earlier today — drawn first. Uses each
    // one's own project color if it had one set, falling back to
    // BAND_DEFAULT_R/G/B otherwise — same neutral fallback and same
    // alpha the live band below uses, no distinction by status. The
    // left third is reserved for pure time markers, nothing
    // task-related drawn over it.
    for (const auto& entry : m_completed_bands) {
        double r, g, b;
        if (parse_hex_color(entry.color, r, g, b)) {
            cr->set_source_rgba(r, g, b, BAND_ALPHA);
        } else {
            cr->set_source_rgba(BAND_DEFAULT_R, BAND_DEFAULT_G, BAND_DEFAULT_B, BAND_ALPHA);
        }

        // Minus, not plus — future is above center, past is below.
        // start_time is always <= end_time chronologically, but that
        // means the *larger* time value maps to the *smaller* y (higher
        // up the screen) under this sign, so min/max rather than
        // assuming which one comes out on top.
        double y_a = center_y - static_cast<double>(entry.start_time - now) * PIXELS_PER_SECOND;
        double y_b = center_y - static_cast<double>(entry.end_time - now) * PIXELS_PER_SECOND;
        double y_top = std::min(y_a, y_b);
        double y_bottom = std::max(y_a, y_b);
        cr->rectangle(band_x, y_top, band_width, y_bottom - y_top);
        cr->fill();
    }

    // The current-session band, if any — drawn before the tick marks so
    // they stay legible on top of it. Reads straight from WorkLog rather
    // than tracking its own copy of session start/end. Same fixed
    // position/width, same min/max reasoning, same BAND_ALPHA, and same
    // neutral fallback color as the completed bands above — no "still in
    // progress" look distinct from a completed one, in any respect.
    //
    // No live-vs-paused branching here either, deliberately: pausing
    // (see SchedulePanel::on_activate_clicked) writes the segment to the
    // database and clears this slot immediately and synchronously, with
    // no draw call able to run in between. So by the time this code can
    // ever observe session_start() != 0, the session is always still
    // genuinely active — band_end is always "now," there's no other case
    // left to handle.
    if (m_worklog.session_start() != 0) {
        double y_a = center_y - static_cast<double>(m_worklog.session_start() - now) * PIXELS_PER_SECOND;
        double y_b = center_y - static_cast<double>(now - now) * PIXELS_PER_SECOND; // always center_y — band's far edge is always "now"
        double y_top = std::min(y_a, y_b);
        double y_bottom = std::max(y_a, y_b);

        double r, g, b;
        std::string color = m_task_attributes.get_color(m_worklog.active_task_id());
        if (parse_hex_color(color, r, g, b)) {
            cr->set_source_rgba(r, g, b, BAND_ALPHA);
        } else {
            cr->set_source_rgba(BAND_DEFAULT_R, BAND_DEFAULT_G, BAND_DEFAULT_B, BAND_ALPHA);
        }
        cr->rectangle(band_x, y_top, band_width, y_bottom - y_top);
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

        cr->set_line_width(STROKE_WIDTH);
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

    // A gold border around the whole panel — ties the clock and the dock
    // together as part of one structure, rather than two separate boxes
    // floating over an unbordered timeline. m_timeline fills the entire
    // panel with no margin around it (see initialize_layout), so a
    // border drawn at this canvas's own edges is effectively a border
    // around the whole panel. Inset by BORDER_HALF_WIDTH on every side
    // so the stroke — which Cairo centers on the path — doesn't get
    // clipped at the true edge; same reasoning already used for the
    // clock's triangle alignment. Drawn last, after everything else, so
    // it stays a crisp, complete frame rather than risking being
    // interrupted by a tick mark or band that happens to reach the edge.
    cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
    cr->set_line_width(STROKE_WIDTH);
    cr->rectangle(BORDER_HALF_WIDTH, BORDER_HALF_WIDTH, width - STROKE_WIDTH, height - STROKE_WIDTH);
    cr->stroke();
}

void SchedulePanel::draw_now_line(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    double center_y = height / 2.0;

    // The clock, boxed in gold, centered in the left third — sitting
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

    double box_w = extents.width + CLOCK_PAD_X * 2;
    double box_h = extents.height + CLOCK_PAD_Y * 2;
    // Fixed distance from the left edge now (CLOCK_CENTER_X) — solid,
    // unaffected by how the panel gets resized, rather than a fraction
    // of its current width. This centers just the rectangular body — the
    // triangular point below is a pure extension off its right edge, not
    // something the whole flag shape (rectangle + point) needs to be
    // centered around.
    double box_x = CLOCK_CENTER_X - box_w / 2.0;
    double box_y = center_y - box_h / 2.0;

    // Sized so the point forms a true equilateral triangle with the
    // box's own height as its base — the two slanted edges and that
    // base are all the same length (sqrt(3)/2 times the base).
    double triangle_len = box_h * 0.8660254;

    // The gold "now" line — shortened to lead into the clock flag rather
    // than spanning the whole width. The flag's point is the line's real
    // endpoint now; nothing continues past it into the task region.
    cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
    cr->rectangle(0, center_y - 1.5, box_x, 3);
    cr->fill();

    // The rectangle body: filled as a normal closed rectangle, but only
    // bordered on three sides (top, left, bottom) — no right edge, so
    // there's no stroke line sitting between the dark fill and the solid
    // gold triangle. That seam was what made the two shapes read as
    // separate objects even once they were precisely aligned; leaving
    // the border open on that one side lets it read as one continuous
    // outline wrapping the whole flag silhouette instead.
    cr->set_source_rgb(BG_R, BG_G, BG_B);
    cr->rectangle(box_x, box_y, box_w, box_h);
    cr->fill();

    cr->move_to(box_x + box_w, box_y);       // top-right corner (unstroked start)
    cr->line_to(box_x, box_y);               // top edge, right-to-left
    cr->line_to(box_x, box_y + box_h);       // left edge, top-to-bottom
    cr->line_to(box_x + box_w, box_y + box_h); // bottom edge, left-to-right
    cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
    cr->set_line_width(STROKE_WIDTH);
    cr->stroke();

    // The triangular point — a separate shape now, solid gold rather
    // than outlined, attached flush to the rectangle's right edge. No
    // horizontal offset here — that would leave a 1px unfilled sliver
    // between the rectangle's fill (ending at box_x+box_w) and the
    // triangle's fill, invisible in the middle (same dark color as the
    // background there) but showing as a visible notch at the top/
    // bottom, right where gold border sits on both sides of that gap.
    // A vertical offset is still needed, though: the top/bottom border
    // strokes still bleed BORDER_HALF_WIDTH outside box_y/box_y+box_h
    // (stroke() centers its line on the path), so the triangle's
    // top/bottom vertices need to reach that same outer edge, or there'd
    // be a slight step where the two meet.
    cr->move_to(box_x + box_w, box_y - BORDER_HALF_WIDTH);
    cr->line_to(box_x + box_w + triangle_len, center_y);
    cr->line_to(box_x + box_w, box_y + box_h + BORDER_HALF_WIDTH);
    cr->close_path();
    cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
    cr->fill();

    cr->set_source_rgb(CLOCK_TEXT_R, CLOCK_TEXT_G, CLOCK_TEXT_B);
    cr->move_to(
        CLOCK_CENTER_X - extents.width / 2.0 - extents.x_bearing,
        center_y - extents.height / 2.0 - extents.y_bearing
    );
    cr->show_text(buf);

    // The staged-task dock's background + border — same treatment as
    // the clock box above, for visual consistency between the two
    // things sitting on the line. Drawn here (not in draw_timeline) so
    // it renders on top of the bands/ticks but still underneath the
    // real widgets (buttons, label) in m_staged_box, which composites
    // after this drawing area. Always drawn now, even with nothing
    // staged — it reads as a permanent dock attached to the panel's
    // right edge, not something that only appears once a task exists.
    // Its right edge extends all the way to the panel's own right edge,
    // like part of the panel's own frame — only the left edge is
    // derived from the content's actual (stable, opacity-based) width.
    double band_x, band_width;
    compute_band_geometry(width, band_x, band_width);

    Gtk::Requisition staged_min, staged_nat;
    m_staged_box.get_preferred_size(staged_min, staged_nat);

    // Width is fine to read live here — only margin_bottom was set on
    // m_staged_box (see initialize_layout), which doesn't affect its
    // reported width. Height would be corrupted by that margin though —
    // get_preferred_size() on a widget that already has a margin folds
    // it back into the reported size — so the cached, pre-margin value
    // is used for height instead of re-measuring it here.
    double content_height = m_staged_box_content_height;
    double task_box_content_w = staged_nat.get_width() + TASK_BOX_PAD_X * 2;
    double task_box_h = content_height + TASK_BOX_PAD_Y * 2;
    double task_box_center_x = band_x + band_width / 2.0;
    double task_box_x = task_box_center_x - task_box_content_w / 2.0;
    double task_box_w = width - task_box_x; // extends all the way to the panel's right edge
    // Content's bottom edge sits at (center_y - GAP) — see the margin
    // trick in initialize_layout. Padding needs to split evenly above
    // and below that, not stack entirely above it, or the border reads
    // as bottom-heavy instead of evenly wrapping the content.
    double content_bottom = center_y - STAGED_BOX_GAP_ABOVE_LINE;
    double task_box_y = content_bottom - content_height - TASK_BOX_PAD_Y;

    cr->set_source_rgb(BG_R, BG_G, BG_B);
    cr->rectangle(task_box_x, task_box_y, task_box_w, task_box_h);
    cr->fill();

    cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
    cr->set_line_width(STROKE_WIDTH);
    cr->rectangle(task_box_x, task_box_y, task_box_w, task_box_h);
    cr->stroke();

    // The label's own border — a tighter box around just the task text,
    // matching its color exactly (project color if set, else the same
    // near-white default used elsewhere — see DEFAULT_TEXT_COLOR).
    // Only drawn when something's actually staged, unlike the outer dock
    // frame above — an empty bordered box around no text would just be
    // clutter, not a useful "this is where a task would go" cue.
    if (m_staged_id != -1) {
        std::string label_color = m_task_attributes.get_color(m_staged_id);
        if (label_color.empty()) label_color = DEFAULT_TEXT_COLOR;

        double lr, lg, lb;
        if (parse_hex_color(label_color, lr, lg, lb)) {
            double label_border_w = band_width + LABEL_BORDER_PAD_X * 2;
            double label_border_h = m_slot_label_height + LABEL_BORDER_PAD_Y * 2;
            double label_border_x = task_box_center_x - label_border_w / 2.0;
            // Padding split evenly above and below the label's text —
            // extending upward from content_bottom by the label height
            // plus *both* pads (the old version) stacked all the
            // vertical padding on top of the text, pushing the border's
            // top stroke up into the button row above.
            double label_border_y = content_bottom - m_slot_label_height - LABEL_BORDER_PAD_Y;

            cr->set_source_rgba(lr, lg, lb, 0.9);
            cr->set_line_width(STROKE_WIDTH);
            cr->rectangle(label_border_x, label_border_y, label_border_w, label_border_h);
            cr->stroke();
        }
    }
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
        m_worklog.end_active_session(); // sets session_end() = now

        // Pausing writes a durable segment immediately, same as
        // completing does — so the time survives even if the app closes,
        // or the task sits paused overnight, before it's ever finished.
        // Same defensive check on_complete_clicked uses below: only
        // record if the session genuinely belongs to what's staged.
        if (m_worklog.active_task_id() == m_staged_id && m_worklog.session_start() != 0) {
            TaskSnapshot snap = snapshot_task(m_staged_id);
            m_worklog.record_segment(snap.title, snap.path, snap.color, m_staged_id, m_worklog.session_start(), m_worklog.session_end());

            // That segment is now permanent history — retire the
            // "current" slot so draw_timeline() doesn't keep drawing it
            // as if it were still live, redundant with the band
            // refresh_completed_bands() is about to add.
            m_worklog.clear_session();
            refresh_completed_bands();
        }
    } else {
        if (m_staged_id == -1) return; // button should be disabled anyway; just a safety net
        m_worklog.start_session(m_staged_id);
    }
    refresh_staged_label(); // updates the play/pause icon and Complete's visibility together
}

void SchedulePanel::on_complete_clicked() {
    if (m_staged_id == -1) return;

    if (m_worklog.has_active_session()) {
        // Freeze the worked-time band rather than discard it — same as
        // a normal deactivate, just happening as part of completing.
        m_worklog.end_active_session();
    }

    // Only log real, matching work time. If the last recorded session
    // belongs to a *different* task — e.g. it was deactivated but never
    // completed, then something else got staged and worked instead —
    // don't attribute that time to whatever's being completed now.
    if (m_worklog.active_task_id() == m_staged_id && m_worklog.session_start() != 0) {
        // Must happen before remove() below, since the node (and its
        // parent_of()) won't be walkable once it's gone.
        TaskSnapshot snap = snapshot_task(m_staged_id);
        m_worklog.record_segment(snap.title, snap.path, snap.color, m_staged_id, m_worklog.session_start(), m_worklog.session_end());
        m_worklog.clear_session();
    }

    // Stamps completed_at on every one of this task's segments — not
    // just the one just recorded above (if any), but any earlier ones
    // from previous pauses too, so they all collapse into one entry in
    // Completed Today instead of showing up as separate rows. Safe to
    // call even if the task was never actually worked at all (a no-op —
    // nothing matches, nothing shows in Completed Today, same as today).
    m_worklog.mark_completed(m_staged_id, std::time(nullptr));

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

SchedulePanel::TaskSnapshot SchedulePanel::snapshot_task(int id) const {
    // If the immediate parent is a generator, its title duplicates this
    // instance's own title (that's how spawning works) — skip straight
    // to its ancestors instead.
    int parent_id = m_projects.parent_of(id);
    std::string path = m_task_attributes.is_generator(parent_id)
        ? m_projects.ancestor_path(parent_id)
        : m_projects.ancestor_path(id);
    return { m_projects.get_title(id), path, m_task_attributes.get_color(id) };
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
    TaskSnapshot snap = snapshot_task(m_staged_id);
    std::string text = (snap.path.empty() ? "" : snap.path + " - ") + snap.title;

    // Always explicit markup now, never plain set_text() — even the
    // "no project color" case needs a known color value, not just
    // whatever GTK's theme default happens to be, so draw_now_line can
    // draw a matching border around the label in the same color.
    std::string effective_color = snap.color.empty() ? DEFAULT_TEXT_COLOR : snap.color;
    m_slot_label.set_markup("<span foreground='" + effective_color + "'>" + Glib::Markup::escape_text(text) + "</span>");

    m_activate_button.set_sensitive(true);
    m_activate_button.set_opacity(1.0);

    // Only two states are ever meaningful: staged-but-idle is just play;
    // active is pause plus the checkmark. Complete never applies to a
    // task that hasn't been worked at all.
    bool active = m_worklog.has_active_session();
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
