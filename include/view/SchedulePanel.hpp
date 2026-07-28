#ifndef SCHEDULEPANEL_HPP
#define SCHEDULEPANEL_HPP
#include <gtkmm/box.h>
#include <gtkmm/overlay.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/button.h>
#include <cairomm/context.h>
#include <cairo.h>
#include <ctime>
#include <vector>
#include "core/Database.hpp"

class TreeController;
class Work;
class TaskAttributes;

// The main schedule view.
//
// Layout is deliberately inverted from a conventional calendar: future
// time is above center, past below, and the whole thing drifts downward.
// "Future rises, past sinks" matches the app's own tree metaphor —
// things grow upward from a base. Everything vertical follows from
// TimelineGeometry::y_for in the .cpp, which is the only place that sign
// convention is expressed.
//
// The clock keeps a fixed position on the left, inside LEFT_GUTTER. The
// staged-task dock takes a share of what's left, growing with the panel
// so a wide window gets a readable label rather than an ellipsis beside
// empty space.
//
// The dock's width has exactly one source of truth: its own allocation.
// dock_width_for() decides the policy and drives a margin; everything
// drawn reads dock_width(), which reports what GTK actually allocated.
// Nothing recomputes the policy at draw time, so a resize can't leave the
// drawn band and the real widgets disagreeing the way it once did — for a
// frame they're simply both still the old width. The staged task's band
// grows downward from where the dock's bottom edge meets the line, at
// exactly the dock's width: the band emerging from the task producing it,
// the way roots grow from a trunk.
//
// The dock (m_staged_box) is two rows: play/pause and a checkmark
// "complete" button on top, the task label below, bottom edge resting on
// the line. Its drawn frame extends to the panel's right edge and stays
// visible even with nothing staged, so it reads as part of the panel's
// own structure; only the contents change. Complete applies only while a
// task is genuinely active, so both buttons stay in the layout at all
// times and toggle via opacity rather than visibility — dropping one out
// of the layout would resize the whole dock on every activate.
//
// Every band shares one fixed width and right-aligned position, so a
// task's own label width never determines its band's width and completed
// bands don't shift when something else is staged later.
//
// A task is staged from the backlog (TaskPanel) via stage_task(); this
// panel doesn't reach into the backlog itself. The Complete button is
// the only route to Work::complete(), which is where a task gets
// removed from the tree as a completion.
//
// No time bookkeeping happens here and none of it is sequenced here.
// Work owns the session and the history, and its start/pause/complete
// are whole operations — each button handler below is one call plus a
// redraw.
class SchedulePanel : public Gtk::Box {
private:
    TreeController& m_projects;
    Work& m_work;
    TaskAttributes& m_task_attributes;

    // -1 = nothing staged. This is the *only* task that can be activated
    // or completed from this panel. This stays local rather than moving
    // into Work — it's "which slot is populated," a UI selection
    // concept, not a domain fact about time worked.
    int m_staged_id = -1;

    // Today's segments, drawn as bands. A snapshot re-pulled on
    // Work's changed signal rather than queried per draw —
    // draw_timeline runs every 100ms.
    std::vector<WorkLogRow> m_completed_bands;

    // Play/pause and checkmark, side by side — the top row of the
    // staged-task dock.
    Gtk::Box    m_staged_buttons_row{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_activate_button; // label toggles ▶ / ⏸ — see refresh_staged_label
    Gtk::Button m_complete_button{"✓"}; // only ever active while a task is actually active — see below

    // The dock's content: buttons row on top, task label below. Bottom-
    // anchored a fixed gap above the line by a margin trick — see
    // dock_margin_bottom in the .cpp for the derivation.
    Gtk::Box    m_staged_box{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label  m_slot_label;

    // Cached at construction, BEFORE the bottom-anchor margin is
    // applied. get_preferred_size() on a widget that already has a
    // margin folds it into the reported size, so re-measuring later
    // would report roughly double the real content height. Anything
    // needing the true height must read this, not re-measure.
    int m_staged_box_content_height = 0;

    // The label's height alone, for drawing a border around just the
    // label rather than the whole dock. Stable whatever text it shows,
    // being single-line and non-wrapping.
    int m_slot_label_height = 0;

    // Two canvases, not one, because of z-order. m_timeline scrolls with
    // time and sits at the bottom; m_now_layer holds everything anchored
    // to "now" — the marker and the dock's chrome — and composites above
    // the bands but still below the real widgets in m_staged_box.
    Gtk::Overlay     m_body;
    Gtk::DrawingArea m_timeline;
    Gtk::DrawingArea m_now_layer;

    void initialize_layout();
    void refresh_staged_label();
    void refresh_completed_bands();

    // The dock's current on-screen width, read from the widget's own
    // allocation rather than recomputed from a policy. Every band is drawn
    // this wide, which is what ties the bands to the label producing them.
    double dock_width() const;

    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    // Resolves the panel's own state and hands off to the drawing
    // functions in the .cpp's anonymous namespace, which take everything
    // they need as parameters.
    void draw_now_layer(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    bool on_timer_tick();
    void on_activate_clicked();
    void on_complete_clicked();

public:
    SchedulePanel(TreeController& projects, Work& worklog, TaskAttributes& task_attributes);
    ~SchedulePanel() override = default;

    // Loads a task into the slot. Whatever was staged (and however far
    // along it was) is simply replaced — no confirmation, at least for now.
    void stage_task(int task_id);
};
#endif
