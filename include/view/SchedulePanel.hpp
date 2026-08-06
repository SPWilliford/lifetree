#ifndef SCHEDULEPANEL_HPP
#define SCHEDULEPANEL_HPP
#include <gtkmm/box.h>
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

// The main schedule view: a staged-task dock across the top, the timeline
// below it.
//
// Layout is deliberately inverted from a conventional calendar: future
// time is above centre, past below, and the whole thing drifts downward.
// "Future rises, past sinks" matches the app's own tree metaphor — things
// grow upward from a base. Everything vertical follows from
// TimelineGeometry::y_for in the .cpp, which is the only place that sign
// convention is expressed.
//
// The dock is ordinary widgets in their own strip, not something drawn.
// It used to be overlaid on the middle of the timeline, which meant every
// part of its appearance — frame, label recess, the gap above the now-line
// — had to be drawn in Cairo and kept in agreement with where GTK had
// actually put the widgets. Moving it out of the timeline deleted all of
// that: two canvases became one, and the panel border went back to being
// a plain rectangle instead of one that had to break around the dock.
//
// The timeline keeps the clock flag on the left, with its gold line
// running in from the panel edge to the flag's point. Bands of worked time
// are drawn in a column on the right, at a width derived from the panel
// alone — no longer tied to the dock, which no longer sits above them.
//
// A task is staged from the backlog (TaskPanel) via stage_task(); this
// panel doesn't reach into the backlog itself. The Complete button is the
// only route to Work::complete(), which is where a task gets removed from
// the tree as a completion.
//
// Both buttons stay in the layout at all times and toggle via opacity
// rather than visibility — dropping one out of the layout would resize the
// dock on every activate.
//
// No time bookkeeping happens here and none of it is sequenced here. Work
// owns the session and the history, and its start/pause/complete are whole
// operations — each button handler below is one call plus a redraw.
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

    // Play/pause and checkmark, side by side — the dock's top row.
    Gtk::Box    m_staged_buttons_row{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_activate_button; // label toggles ▶ / ⏸ — see refresh_staged_label
    Gtk::Button m_complete_button{"✓"}; // only ever active while a task is actually active

    // The dock: buttons row above, task label below, in a strip across the
    // top of the panel. Sized by its own content rather than to a fixed
    // fraction of the panel — two rows of widgets need what they need, and
    // a proportional height would leave a tall window mostly empty space.
    // Its frame is a CSS border (see MainWindow::apply_styles), not drawn.
    Gtk::Box    m_staged_box{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label  m_slot_label;

    // One canvas now. The dock no longer overlaps the timeline, so there's
    // nothing needing to composite between the bands and the real widgets.
    Gtk::DrawingArea m_timeline;

    void initialize_layout();
    void refresh_staged_label();
    void refresh_completed_bands();

    // Draws the whole timeline: bands, ticks, the now marker, and the
    // panel's own border. Resolves this panel's state and hands off to the
    // free functions in the .cpp's anonymous namespace, which take
    // everything they need as parameters.
    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

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
