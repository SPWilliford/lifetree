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

class ITreeController;
class WorkLog;

// The main schedule view.
//
// Header (own area, doesn't overlap the timeline): a digital clock on
// top, with the staged-task slot, an activate/deactivate button, and a
// Complete button in a row directly beneath it. A task gets staged here
// via double-click in the backlog (TaskPanel) — this panel doesn't reach
// into the backlog itself, it just exposes stage_task() for whoever's
// coordinating that. Complete is the *only* place a task actually gets
// removed from the tree — TreePanel and TaskPanel no longer do this.
//
// Body: a passive timeline of hour/half-hour/quarter-hour notches that
// drifts upward at a slow, constant, real-time pace, with a fixed "now"
// line overlaid at its vertical center. While a task is active, the span
// of wall-clock time you've worked it is drawn directly on the timeline
// as a highlighted band — it keeps growing while active, and stays put
// as a visual record of that work once deactivated.
//
// This panel doesn't do its own time bookkeeping — WorkLog owns the
// current session (start/stop) and the permanent history. SchedulePanel
// just asks it questions and draws the answers.
class SchedulePanel : public Gtk::Box {
private:
    ITreeController& m_projects;
    WorkLog& m_worklog;

    // -1 = nothing staged. This is the *only* task that can be activated
    // or completed from this panel. This stays local rather than moving
    // into WorkLog — it's "which slot is populated," a UI selection
    // concept, not a domain fact about time worked.
    int m_staged_id = -1;

    // header
    Gtk::Box    m_header{Gtk::Orientation::VERTICAL, 8};
    Gtk::Label  m_clock_label;
    Gtk::Box    m_slot_row{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Label  m_slot_label;
    Gtk::Button m_activate_button{"Activate"};
    Gtk::Button m_complete_button{"Complete"};

    // body
    Gtk::Overlay     m_body;
    Gtk::DrawingArea m_timeline;
    Gtk::DrawingArea m_now_line;

    void initialize_layout();
    void refresh_staged_label();

    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void draw_now_line(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    bool on_timer_tick();
    void update_clock_label();
    void on_activate_clicked();
    void on_complete_clicked();

public:
    SchedulePanel(ITreeController& projects, WorkLog& worklog);
    ~SchedulePanel() override = default;

    // Loads a task into the slot. Whatever was staged (and however far
    // along it was) is simply replaced — no confirmation, at least for now.
    void stage_task(int task_id);
};
#endif
