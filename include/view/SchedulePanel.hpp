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
#include "engine/Database.hpp"

class ITreeController;
class WorkLog;
class TaskAttributes;

// The main schedule view.
//
// Layout is deliberately inverted from a conventional top-down calendar:
// the timeline sits above the staged-task row, future time is above
// center and past time is below, and the whole thing drifts downward
// rather than up. The idea is that "future rises, past sinks" — matching
// the app's own tree metaphor (things grow upward from a base) — reads
// more intuitively than the usual top-to-bottom reading-order convention
// once you're used to it.
//
// Header (own area, doesn't overlap the timeline, sits *below* it): the
// staged-task slot, an activate/deactivate button, and a Complete
// button, confined to the right half — mirroring the timeline's own
// split above, so the staged task visually lines up with the half of
// the timeline where its band will actually be drawn. Sitting at the
// boundary closest to where new time keeps entering the view (from
// beneath) is deliberate — this is effectively "the present," positioned
// where the present actually is. A task gets staged here via
// double-click in the backlog (TaskPanel) — this panel doesn't reach
// into the backlog itself, it just exposes stage_task() for whoever's
// coordinating that. Complete is the *only* place a task actually gets
// removed from the tree — TreePanel and TaskPanel no longer do this.
//
// Body: a passive timeline of hour/half-hour/quarter-hour notches that
// drifts downward at a slow, constant, real-time pace — future entering
// from the top, sinking toward and past center as it becomes present,
// then past — with a gold "now" line overlaid at its vertical center,
// spanning the full width — and, centered within just the left half, a
// boxed digital clock sitting right on that line. The clock used to be a
// separate, static label in the header; merging it into the line itself
// means "what time is it" and "where is now on this timeline" are the
// same visual element instead of two things you have to mentally connect
// yourself. The left half is otherwise reserved for time markers alone —
// nothing task-related is drawn over it. The right half is where task
// bands live: while a task is active, the span of wall-clock time you've
// worked it is drawn there as a highlighted band — it keeps growing
// while active, and stays put as a visual record of that work once
// deactivated. Once a task is completed, its band is drawn from the
// day's permanent history instead (see m_completed_bands) — so the
// timeline keeps showing everything worked today, not just whatever's
// currently staged.
//
// This panel doesn't do its own time bookkeeping — WorkLog owns the
// current session (start/stop) and the permanent history. SchedulePanel
// just asks it questions and draws the answers.
class SchedulePanel : public Gtk::Box {
private:
    ITreeController& m_projects;
    WorkLog& m_worklog;
    TaskAttributes& m_task_attributes;

    // -1 = nothing staged. This is the *only* task that can be activated
    // or completed from this panel. This stays local rather than moving
    // into WorkLog — it's "which slot is populated," a UI selection
    // concept, not a domain fact about time worked.
    int m_staged_id = -1;

    // Today's completed sessions, drawn on the timeline as fixed bands
    // alongside the current one — a static snapshot re-pulled whenever a
    // task completes, not re-queried on every draw (draw_timeline runs
    // on every 100ms tick).
    std::vector<WorkLogRow> m_completed_bands;

    // header
    Gtk::Box    m_header{Gtk::Orientation::VERTICAL, 8};
    // Splits the staged-task row into the right half only, mirroring the
    // timeline's own left(markers)/right(tasks) split below — m_split_row
    // is homogeneous specifically so the divide is an exact 50/50 of the
    // width, matching draw_timeline()'s width/2.0, regardless of how wide
    // m_slot_row's actual content happens to be.
    Gtk::Box    m_split_row{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Box    m_left_spacer{Gtk::Orientation::HORIZONTAL, 0}; // empty — just claims the left half's width
    // Vertical, not horizontal — label above, buttons below. Gives the
    // label the full half-width to itself before anything needs to
    // ellipsize, rather than competing with two buttons for the same row.
    Gtk::Box    m_slot_row{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label  m_slot_label;
    Gtk::Box    m_slot_buttons_row{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_activate_button{"Activate"};
    Gtk::Button m_complete_button{"Complete"};

    // body
    Gtk::Overlay     m_body;
    Gtk::DrawingArea m_timeline;
    Gtk::DrawingArea m_now_line;

    void initialize_layout();
    void refresh_staged_label();
    void refresh_completed_bands();

    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void draw_now_line(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    bool on_timer_tick();
    void on_activate_clicked();
    void on_complete_clicked();

public:
    SchedulePanel(ITreeController& projects, WorkLog& worklog, TaskAttributes& task_attributes);
    ~SchedulePanel() override = default;

    // Loads a task into the slot. Whatever was staged (and however far
    // along it was) is simply replaced — no confirmation, at least for now.
    void stage_task(int task_id);
};
#endif
