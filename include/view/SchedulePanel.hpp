#ifndef SCHEDULEPANEL_HPP
#define SCHEDULEPANEL_HPP

#include <ctime>
#include <string>
#include <vector>

#include <cairomm/context.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/tooltip.h>

#include "core/Database.hpp"
#include "view/Refresh.hpp"

class TreeController;
class Work;
class TaskAttributes;
class Day;

// Two docks across the top (the day; the staged task and its transport)
// and the timeline below. Time runs inverted: future above, past below.
//
// A task is staged from the task list via stage_task(). The Complete button
// is the only route to Work::complete().
class SchedulePanel : public Gtk::Box {
public:
    SchedulePanel(TreeController& projects, Work& work, TaskAttributes& task_attributes, Day& day);
    ~SchedulePanel() override = default;

    // Replaces whatever was staged, unless a session is active.
    void stage_task(int task_id);

private:
    TreeController& m_projects;
    Work& m_work;
    TaskAttributes& m_task_attributes;
    Day& m_day;

    // The only task this panel can act on; -1 for none. View state, not
    // Work's: which slot is filled is a selection, not a fact about time.
    int m_staged_id = -1;

    // Cached on Work's signal, not queried per draw (draw runs at 10 Hz).
    std::vector<WorkLogRow> m_completed_bands;

    // Today's timed tasks. Stale across midnight until something refreshes.
    struct ScheduledBand {
        int node_id;
        time_t start;
        time_t end;     // == start when the task has no end time
        bool is_block;  // an end time was set
        std::string title;
        std::string color;
    };
    std::vector<ScheduledBand> m_scheduled_bands;

    Gtk::Box m_dock_row{Gtk::Orientation::HORIZONTAL, 0};

    // Left dock: the day. Its width is measured to the timeline's rule.
    Gtk::Box m_timeline_dock{Gtk::Orientation::VERTICAL, 4};
    Gtk::MenuButton m_day_button;  // the date label doubles as the button
    Gtk::Popover m_day_menu;
    Gtk::Label m_dock_date;
    Gtk::Box m_timeline_controls{Gtk::Orientation::HORIZONTAL, 4};  // reserved

    // tm_yday the date label shows; the tick compares against this.
    int m_shown_yday = -1;

    // Right dock: the staged task. Buttons toggle via opacity, not
    // visibility, so the dock never resizes.
    Gtk::Box m_staged_box{Gtk::Orientation::VERTICAL, 2};
    Gtk::Label m_slot_path;
    Gtk::Label m_slot_title;
    Gtk::Box m_staged_buttons_row{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Label m_elapsed_label;
    Gtk::Button m_activate_button;  // ▶ / ⏸
    Gtk::Button m_complete_button{"✓"};

    // Banked seconds for the staged task; the tick adds the live session.
    long m_staged_banked = 0;

    // What the elapsed label reads, so the 10 Hz tick writes it once a second.
    long m_shown_elapsed = -1;

    Gtk::DrawingArea m_timeline;

    void initialize_layout();
    void build_day_menu();
    void refresh_staged_label();
    void refresh_elapsed_label();
    void refresh_date_label();
    void refresh_completed_bands();
    void refresh_scheduled_bands();

    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    bool on_timeline_tooltip(int x, int y, bool keyboard,
                             const Glib::RefPtr<Gtk::Tooltip>& tooltip);

    bool on_timer_tick();
    void on_activate_clicked();
    void on_complete_clicked();

    Refresh m_label_refresh{sigc::mem_fun(*this, &SchedulePanel::refresh_staged_label)};
    Refresh m_bands_refresh{sigc::mem_fun(*this, &SchedulePanel::refresh_completed_bands)};
    Refresh m_scheduled_refresh{sigc::mem_fun(*this, &SchedulePanel::refresh_scheduled_bands)};
};

#endif
