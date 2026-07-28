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

    // Space kept clear on the left for the tick labels and the clock flag.
    // The dock is never allowed to reach back into it, so the two can't
    // collide however the panel is resized.
    constexpr double LEFT_GUTTER = 300.0;

    // The dock grows with the panel instead of sitting at one fixed width,
    // so a wide window gets wide bands and a readable label rather than an
    // ellipsis with empty space beside it. Bounded at both ends: too narrow
    // and the label is useless, too wide and the bands dominate a timeline
    // that's mostly meant to be read as time.
    constexpr double DOCK_WIDTH_FRACTION = 0.5;
    constexpr double DOCK_MIN_WIDTH = 260.0;
    constexpr double DOCK_MAX_WIDTH = 720.0;

    // Shared between the Cairo geometry below and m_staged_box's GTK
    // alignment (see initialize_layout), so the drawn band and the real
    // widgets anchor to the same reference point.
    constexpr double BAND_RIGHT_MARGIN = 20.0;

    // How far above the now-line the staged-task box (buttons + label)
    // rests, rather than sitting flush against it. Lowering this by one
    // drops the entire dock — widgets, frame, and label border alike — by
    // one pixel, since all of them derive from the paired
    // dock_margin_bottom / dock_content_bottom_y below.
    constexpr int STAGED_BOX_GAP_ABOVE_LINE = 10;

    // Padding around the staged-task box's content (buttons + label),
    // inside its drawn border. Top and bottom are separate because the
    // frame's BOTTOM edge is aligned to the now-line and must stay put:
    // only the top pad may change to give the content more room. Shared
    // between draw_dock_frame and initialize_layout, which sizes
    // m_now_layer tall enough not to clip the frame.
    constexpr double TASK_BOX_PAD_X = 18.0;
    constexpr double TASK_BOX_PAD_TOP = 14.0;
    constexpr double TASK_BOX_PAD_BOTTOM = 8.0; // pinned — sets where the frame meets the line

    // Padding around the label's own text, inside its recess. Same
    // top/bottom split and the same reason. PAD_X must stay below
    // TASK_BOX_PAD_X or the recess would escape the dock frame around it.
    constexpr double LABEL_BORDER_PAD_X = 12.0;
    constexpr double LABEL_BORDER_PAD_TOP = 8.0;
    constexpr double LABEL_BORDER_PAD_BOTTOM = 4.0; // pinned, as above

    // Space between the button row and the label below it. Must clear the
    // label border's top stroke, which reaches LABEL_BORDER_PAD_TOP plus
    // half a stroke above the label — otherwise the two overlap.
    constexpr int BUTTONS_LABEL_GAP = 12;

    constexpr double BG_R = 0.13, BG_G = 0.13, BG_B = 0.15;             // dark background — timeline, clock box, task dock
    constexpr double GOLD_R = 0.95, GOLD_G = 0.75, GOLD_B = 0.2;        // the "now" accent — line, both borders
    constexpr double CLOCK_TEXT_R = 0.95, CLOCK_TEXT_G = 0.95, CLOCK_TEXT_B = 0.95;

    // The tick marks' three tiers, brightest on the hour. TICK_HOUR is also
    // the staged label's border, so that border reads as part of the same
    // neutral furniture as the time markers rather than as another accent
    // competing with the gold.
    // The dock's interior. Deliberately lighter than BG so the dock reads
    // as a solid panel resting ON the timeline rather than a window cut
    // through it — closer to the window grey behind the whole schedule
    // than to the timeline's own dark. Sampled by eye, not from the theme
    // (GTK4 has no clean public API for a widget's background color), so
    // nudge it until it sits right against your actual theme.
    constexpr double DOCK_FILL_R = 0.18, DOCK_FILL_G = 0.18, DOCK_FILL_B = 0.19;

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

    // The vertical strip shared by every band and by the staged-task dock,
    // right-anchored against the panel's edge. dock_width comes from the
    // dock widget's actual allocation — see SchedulePanel::dock_width() —
    // never from the policy below, so the drawn band is by construction the
    // same width as the label that appears to be printing it.
    struct BandColumn {
        double x;
        double width;
        double center() const { return x + width / 2.0; }
    };

    BandColumn band_column(int panel_width, double dock_width) {
        return { panel_width - BAND_RIGHT_MARGIN - dock_width, dock_width };
    }

    // How wide the dock *should* be at a given panel width. The single home
    // for that policy — and deliberately the only thing that consults it is
    // the margin that drives the widget. Nothing drawn is derived from this;
    // drawing follows the resulting allocation instead. That's what keeps
    // the band and the real widgets in agreement even during a drag, when a
    // freshly computed width and an already-allocated one would differ.
    double dock_width_for(int panel_width) {
        const double wanted = panel_width * DOCK_WIDTH_FRACTION;
        const double room = panel_width - BAND_RIGHT_MARGIN - LEFT_GUTTER;
        // min() keeps the dock out of the clock's gutter; max() stops it
        // collapsing when the panel is too narrow to honour both.
        return std::max(DOCK_MIN_WIDTH, std::min({ wanted, room, DOCK_MAX_WIDTH }));
    }

    // Left margin that produces that width, given the dock is FILL-aligned
    // and pinned to the right edge by BAND_RIGHT_MARGIN.
    int dock_left_margin(int panel_width) {
        return static_cast<int>(panel_width - BAND_RIGHT_MARGIN - dock_width_for(panel_width));
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

    // --- The staged-task dock's vertical anchoring ---
    //
    // The dock is bottom-anchored a fixed gap above the now-line by a
    // margin trick rather than by explicit positioning: m_staged_box
    // gets valign(CENTER) plus a bottom margin, so the *padded* box
    // centers on the line and its *content* therefore sits entirely
    // above it. (Different from the clock box, which centers its content
    // ON the line.)
    //
    // Both halves of that trick live here, adjacent, because they have
    // to agree exactly and nothing in GTK enforces it: initialize_layout
    // applies the margin, draw_dock_frame draws a border around wherever
    // the content consequently lands. Previously each computed its own
    // half independently and they were correct only by inspection.
    //
    // The derivation, once: centering a padded box of height
    // (content_h + margin) puts its top edge at
    // center_y - (content_h + margin)/2, and the content occupies the
    // top content_h of that, so the content's bottom edge lands at
    // center_y + (content_h - margin)/2. Substituting the margin below
    // makes that exactly center_y - STAGED_BOX_GAP_ABOVE_LINE — which is
    // why the margin needs *twice* the gap and not once: the margin
    // grows the padded box, so only half of any added margin shows up as
    // visible lift.
    int dock_margin_bottom(int content_height) {
        return content_height + 2 * STAGED_BOX_GAP_ABOVE_LINE;
    }

    double dock_content_bottom_y(double center_y) {
        return center_y - STAGED_BOX_GAP_ABOVE_LINE;
    }

    // The dock frame's top and bottom edges. Needed by two different
    // canvases: draw_dock_frame draws the frame, and draw_panel_border has
    // to break the panel's own right edge over exactly the same span so the
    // two meet rather than overlap. m_timeline and m_now_layer are both
    // centred on the same line in m_body, so each can call this from its own
    // centre and get the same answer.
    struct DockSpan { double top, bottom; };

    DockSpan dock_span(double content_bottom, int content_height) {
        return { content_bottom - content_height - TASK_BOX_PAD_TOP,
                 content_bottom + TASK_BOX_PAD_BOTTOM };
    }

    // The two reference points the dock's chrome is built from, derived
    // once so the outer frame and the label border inside it can't end up
    // measuring from different places.
    struct DockGeometry {
        BandColumn col;        // where the dock sits horizontally
        double content_bottom; // where the real widgets' bottom edge lands

        DockGeometry(int panel_width, double center_y, double dock_width)
            : col(band_column(panel_width, dock_width)),
              content_bottom(dock_content_bottom_y(center_y)) {}
    };

    // The present-moment marker: a gold line running in from the left
    // edge into a flag-shaped clock whose triangular point IS the line's
    // endpoint. Nothing continues past it into the dock's region.
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

    // The dock's backing panel — same fill and gold border as the clock
    // flag, so the two read as parts of one structure rather than two
    // boxes floating separately. Drawn even with nothing staged: it's
    // permanent furniture, not something that appears when a task
    // arrives. Its right edge runs to the panel's own edge; only the left
    // is derived from the content width.
    void draw_dock_frame(const Cairo::RefPtr<Cairo::Context>& cr, int panel_width,
                         const DockGeometry& dock, int content_height) {
        const DockSpan span = dock_span(dock.content_bottom, content_height);
        const double content_w = dock.col.width + TASK_BOX_PAD_X * 2;
        const double x = dock.col.center() - content_w / 2.0;

        // Fill and strokes both run right out to the panel's edge. Nothing
        // to avoid there any more: draw_panel_border leaves this exact span
        // of its right edge undrawn, so the dock occupies the boundary
        // rather than sitting just inside it.
        cr->set_source_rgb(DOCK_FILL_R, DOCK_FILL_G, DOCK_FILL_B);
        cr->rectangle(x, span.top, panel_width - x, span.bottom - span.top);
        cr->fill();

        // Top, left and bottom only. The panel's perimeter turns in at
        // span.top and resumes at span.bottom, so these three sides *are*
        // the perimeter across the dock — not a second frame inside it.
        // Same trick the clock flag uses where it meets its gold point.
        cr->move_to(panel_width, span.top);
        cr->line_to(x, span.top);
        cr->line_to(x, span.bottom);
        cr->line_to(panel_width, span.bottom);
        cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
        cr->set_line_width(STROKE_WIDTH);
        cr->stroke();
    }

    // The panel's gold perimeter, interrupted on the right across the
    // dock's span. Instead of running straight down past the dock, the
    // frame turns in at its top edge and picks up again at its bottom —
    // draw_dock_frame supplies the three sides that carry it across. Drawn
    // as one open path rather than a rectangle, since a rectangle can't
    // have a bite taken out of it.
    void draw_panel_border(const Cairo::RefPtr<Cairo::Context>& cr,
                           int width, int height, const DockSpan& dock) {
        const double l = BORDER_HALF_WIDTH;
        const double t = BORDER_HALF_WIDTH;
        const double r = width - BORDER_HALF_WIDTH;
        const double b = height - BORDER_HALF_WIDTH;

        // Clamped so a panel too short to contain the dock still produces a
        // sane path rather than segments doubling back on themselves.
        const double gap_top = std::clamp(dock.top, t, b);
        const double gap_bottom = std::clamp(dock.bottom, t, b);

        cr->move_to(r, gap_bottom);  // resume below the dock
        cr->line_to(r, b);           // down the right
        cr->line_to(l, b);           // along the bottom
        cr->line_to(l, t);           // up the left
        cr->line_to(r, t);           // along the top
        cr->line_to(r, gap_top);     // back down, stopping at the dock

        cr->set_source_rgba(GOLD_R, GOLD_G, GOLD_B, 0.9);
        cr->set_line_width(STROKE_WIDTH);
        cr->stroke();
    }

    // The recess the staged task's label sits in: filled back down to the
    // timeline's own dark, then outlined. Cut into the dock's lighter fill
    // rather than sitting on it, so the label reads as inset and the dock
    // as the raised surface around it.
    //
    // Drawn whether or not anything is staged — an empty recess reads as a
    // place a task goes, which is the point of the dock being permanent
    // furniture. The outline is neutral rather than tinted to the project
    // color: it marks where the label sits, and the label text already
    // carries which project it belongs to.
    void draw_label_slot(const Cairo::RefPtr<Cairo::Context>& cr,
                         const DockGeometry& dock, int label_height) {
        const double w = dock.col.width + LABEL_BORDER_PAD_X * 2;
        const double h = label_height + LABEL_BORDER_PAD_TOP + LABEL_BORDER_PAD_BOTTOM;
        const double x = dock.col.center() - w / 2.0;
        // Grows upward only: the bottom edge lands at content_bottom plus
        // the pinned bottom pad regardless of what the top pad becomes.
        const double y = dock.content_bottom - label_height - LABEL_BORDER_PAD_TOP;

        cr->set_source_rgb(BG_R, BG_G, BG_B);
        cr->rectangle(x, y, w, h);
        cr->fill();

        cr->set_source_rgba(TICK_HOUR_R, TICK_HOUR_G, TICK_HOUR_B, 0.9);
        cr->set_line_width(STROKE_WIDTH);
        cr->rectangle(x, y, w, h);
        cr->stroke();
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

    // --- staged-task dock: buttons row on top, label below, overlaid
    //     directly on m_body (the timeline) ---
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
    // START rather than FILL: the dock is as wide as the panel allows, and
    // two buttons stretched across all of it would look absurd.
    m_staged_buttons_row.set_halign(Gtk::Align::START);
    // Clearance for the label's own drawn border, whose top stroke
    // reaches LABEL_BORDER_PAD_Y plus half a stroke above the label.
    // Set BEFORE the get_preferred_size() below, so the cached content
    // height folds this gap in.
    m_staged_buttons_row.set_margin_bottom(BUTTONS_LABEL_GAP);

    // No character cap and no size request. m_staged_box is FILL-aligned,
    // so its allocation is fixed by its parent and the label can't inflate
    // it however long the text is — which is what a max_width_chars guess
    // was previously there to prevent. The label simply ellipsizes against
    // whatever width the dock actually got.
    //
    // Deliberately no dim-label class: this is the dock's main content,
    // not secondary text.
    m_slot_label.set_ellipsize(Pango::EllipsizeMode::END);
    m_slot_label.set_xalign(0.5);
    m_slot_label.set_halign(Gtk::Align::FILL);

    m_staged_box.append(m_staged_buttons_row);
    m_staged_box.append(m_slot_label);

    // FILL, not END: with FILL the parent's allocation decides this box's
    // width outright, so no child can widen it and no size request is
    // needed. Width is then purely a matter of the two margins — the right
    // one fixed, the left one recomputed on resize (see below).
    m_staged_box.set_halign(Gtk::Align::FILL);
    m_staged_box.set_margin_end(static_cast<int>(BAND_RIGHT_MARGIN));
    m_staged_box.set_margin_start(dock_left_margin(1400)); // plausible start; the first resize corrects it

    // Measured and cached BEFORE the bottom-anchor margin gets applied
    // below — get_preferred_size() on a widget that already has a margin
    // set folds that margin back into the reported height, so
    // re-measuring m_staged_box after this point would get back
    // something like double the real content height. Everything needing
    // the content's true height reads this cached value instead.
    Gtk::Requisition min_req, nat_req;
    m_staged_box.get_preferred_size(min_req, nat_req);
    m_staged_box_content_height = nat_req.get_height();

    // m_slot_label's own height alone, not the whole box — sizes the
    // recess it sits in (see draw_label_slot). No margin concern here;
    // this one's unaffected by the margin about to be applied below,
    // since that margin is set on m_staged_box, not on the label itself.
    Gtk::Requisition label_min, label_nat;
    m_slot_label.get_preferred_size(label_min, label_nat);
    m_slot_label_height = label_nat.get_height();

    // valign + margin together are the bottom-anchor trick — see
    // dock_margin_bottom, which is paired with the dock_content_bottom_y
    // that draw_dock_frame borders against.
    m_staged_box.set_valign(Gtk::Align::CENTER);
    m_staged_box.set_margin_bottom(dock_margin_bottom(m_staged_box_content_height));

    // --- body: scrolling timeline, with the now-line and the staged-task
    //     controls both overlaid on top of it ---
    m_timeline.set_hexpand(true);
    m_timeline.set_vexpand(true);
    m_timeline.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_timeline));

    // The dock's width follows the panel's. Deferred to idle because this
    // fires from inside the allocation pass that just reported the width,
    // and changing a margin there would mutate the layout mid-traversal.
    //
    // The one-frame delay that introduces is harmless precisely because
    // nothing drawn is computed from dock_width_for(): the band reads the
    // dock's real allocation, so for that frame both are simply still the
    // old width together, rather than disagreeing.
    m_timeline.signal_resize().connect([this](int width, int) {
        Glib::signal_idle().connect_once([this, width]() {
            m_staged_box.set_margin_start(dock_left_margin(width));
        });
    });

    m_body.set_child(m_timeline);

    // Tall enough for whichever is taller, the clock flag or the dock
    // frame. The dock extends upward from (center_y - GAP) by its content
    // height plus padding on both sides, and this canvas is centered on
    // the line, so it needs twice that distance in total or its own
    // drawing is clipped at the top. +20 is slack, not load-bearing.
    int now_layer_height = static_cast<int>(2 * (STAGED_BOX_GAP_ABOVE_LINE + m_staged_box_content_height + TASK_BOX_PAD_TOP)) + 20;
    m_now_layer.set_size_request(-1, now_layer_height);
    m_now_layer.set_halign(Gtk::Align::FILL);
    m_now_layer.set_valign(Gtk::Align::CENTER);
    m_now_layer.set_draw_func(sigc::mem_fun(*this, &SchedulePanel::draw_now_layer));
    m_body.add_overlay(m_now_layer);
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

    // Every band shares this column, which is exactly the staged-task
    // dock's own width — that's the "the dock prints the bands" reading.
    // Not tied to any individual task's label length, so completed bands
    // don't shift when something else is staged later.
    const BandColumn col = band_column(width, dock_width());

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

    // The panel's perimeter, drawn last so no tick or band interrupts it.
    // m_timeline fills the panel with no margin, so a border at this
    // canvas's edges is the panel's border. The dock's span is computed
    // from this canvas's own centre — the same value m_now_layer arrives at
    // from its centre, which is what lets the two halves of the frame meet.
    draw_panel_border(cr, width, height,
                      dock_span(dock_content_bottom_y(geo.center_y), m_staged_box_content_height));
}

void SchedulePanel::draw_now_layer(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    const double center_y = height / 2.0;
    const DockGeometry dock{width, center_y, dock_width()};

    draw_now_marker(cr, center_y, std::time(nullptr));
    draw_dock_frame(cr, width, dock, m_staged_box_content_height);
    draw_label_slot(cr, dock, m_slot_label_height);
}

// ---------------------------------------------------------------------
// Live updates
// ---------------------------------------------------------------------

bool SchedulePanel::on_timer_tick() {
    m_timeline.queue_draw();
    m_now_layer.queue_draw(); // the clock reads live, so this can't be drawn once and left
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

double SchedulePanel::dock_width() const {
    // Read, not recomputed. get_width() is the width GTK actually allocated
    // for the frame being drawn — unlike get_preferred_size(), which is a
    // request and can lag a cycle behind what's on screen. Deriving the
    // band from this rather than from dock_width_for() is what makes the
    // drawn band and the real widgets incapable of disagreeing.
    const int allocated = m_staged_box.get_width();
    return allocated > 0 ? static_cast<double>(allocated) : DOCK_MIN_WIDTH;
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

    // Always explicit markup, never plain set_text(): the label sits on a
    // dark recess this file paints itself, so a light theme's default text
    // color would come out dark-on-dark. DEFAULT_TEXT_COLOR covers the
    // "no project color" case.
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
