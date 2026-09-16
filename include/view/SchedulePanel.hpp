#ifndef SCHEDULEPANEL_HPP
#define SCHEDULEPANEL_HPP
#include <ctime>
#include <vector>

#include <cairo.h>
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

// The schedule: a staged-task dock across the top, the timeline below.
//
// Time runs inverted from a conventional calendar — future above centre,
// past below, drifting downward — to match the app's tree metaphor. That
// sign convention is expressed once, in TimelineGeometry::y_for.
//
// A task is staged from the backlog via stage_task(); this panel never
// reaches into the backlog. The Complete button is the only route to
// Work::complete(). No time bookkeeping is sequenced here: Work's verbs are
// whole operations, so each button handler is one call plus a redraw.
class SchedulePanel : public Gtk::Box {
private:
    TreeController& m_projects;
    Work& m_work;
    TaskAttributes& m_task_attributes;
    Day& m_day;

    // -1 = nothing staged; the only task this panel can act on. Local
    // rather than in Work: which slot is populated is a UI selection, not a
    // domain fact about time worked.
    int m_staged_id = -1;

    // Re-pulled on Work's changed signal, not queried per draw —
    // draw_timeline runs every 100ms.
    std::vector<WorkLogRow> m_completed_bands;

    // Today's timed tasks, cached the same way. Goes stale across midnight
    // until something triggers a refresh — the same rollover gap the
    // backlog has.
    struct ScheduledBand {
        int node_id;
        time_t start;
        time_t end;     // == start when the task carries no end time
        bool is_block;  // an end time was set, so it occupies a span
        std::string title;
        std::string color;
    };
    std::vector<ScheduledBand> m_scheduled_bands;

    // Two docks across the top, not one. The gap between them is centred on
    // the timeline's vertical rule, so the panel reads as a single division
    // running top to bottom: the day on the left, the task on the right.
    Gtk::Box m_dock_row{Gtk::Orientation::HORIZONTAL, 0};

    // Left dock: the day itself. Sized to the rule — see measure_rule_x in
    // the .cpp for why that number is measured once.
    //
    // Top-aligned rather than centred in the taller box: the date lines up
    // with the staged task's path line across the gap, and the space below
    // it is where the timeline controls will sit, opposite the task's
    // transport.
    Gtk::Box m_timeline_dock{Gtk::Orientation::VERTICAL, 4};

    // The date is the way into the day's own settings — its start and end,
    // and whatever the check-in and review turn into. It sits over the
    // timeline and names the thing being configured, which is why the
    // controls hang off it rather than off a button of their own.
    //
    // Half the dock's height, top-aligned. The other half is left for the
    // timeline's controls, opposite the staged task's transport across the
    // gap — so each dock reads as a label over its own controls.
    Gtk::MenuButton m_day_button;
    Gtk::Popover m_day_menu;

    // Rebuilt each time the popover opens rather than filled once, so the
    // fields always show what's stored — including on a day that has just
    // inherited yesterday's hours.
    void build_day_menu();
    Gtk::Label m_dock_date;

    // Empty for now, and holding the bottom half open on purpose: zoom and
    // whatever else the timeline needs go here, opposite the staged task's
    // transport. Both halves expand, so the split is a half regardless of
    // what either one ends up containing.
    Gtk::Box m_timeline_controls{Gtk::Orientation::HORIZONTAL, 4};

    // Which day the label is currently showing, as tm_yday. The date is the
    // one thing on screen that changes without any user action and without
    // any signal firing, so the tick compares against this rather than
    // rewriting the label sixty times a second.
    int m_shown_yday = -1;

    // The dock's bottom row: elapsed, then the transport. Both buttons stay
    // in the layout and toggle via opacity: dropping one out would resize
    // the dock on every activate.
    Gtk::Box m_staged_buttons_row{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Label m_elapsed_label;
    Gtk::Button m_activate_button;       // label toggles ▶ / ⏸ — see refresh_staged_label
    Gtk::Button m_complete_button{"✓"};  // only ever active while a task is actually active

    // A now-playing slot: where the task came from, then the task, then the
    // transport, all on one centred axis. Sized by its content rather than
    // a fraction of the panel. Its frame is the .staged-dock CSS border in
    // Style.cpp, not drawn.
    //
    // Path and title are two labels rather than one "path - title" string.
    // Inline, the path is a variable-width leading element in front of the
    // thing you actually read — the same defect the backlog row had, and
    // fixed the same way.
    Gtk::Box m_staged_box{Gtk::Orientation::VERTICAL, 2};
    Gtk::Label m_slot_path;
    Gtk::Label m_slot_title;

    // Segments already banked for the staged task, summed across every day.
    // Re-read when the staged task or the log changes, never per tick: the
    // tick adds the live session's seconds to this instead of re-querying.
    long m_staged_banked = 0;

    // What the elapsed label currently reads, in seconds. The tick runs at
    // 100ms and this changes once a second, so comparing against it is what
    // stops ten relayouts a second — same trick as m_shown_yday.
    long m_shown_elapsed = -1;

    // One canvas: the dock is real widgets in their own strip, so nothing
    // has to composite between the bands and them.
    Gtk::DrawingArea m_timeline;

    void initialize_layout();
    void refresh_staged_label();

    // Banked plus whatever the live session has run up. Cheap enough for
    // the tick because it does no arithmetic the tick doesn't already do.
    void refresh_elapsed_label();

    void refresh_date_label();
    void refresh_completed_bands();
    void refresh_scheduled_bands();

    // Bands, ticks, the now marker, the panel border. Resolves state and
    // hands off to the free functions in the .cpp, which take everything as
    // parameters.
    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    Refresh m_label_refresh{sigc::mem_fun(*this, &SchedulePanel::refresh_staged_label)};
    Refresh m_bands_refresh{sigc::mem_fun(*this, &SchedulePanel::refresh_completed_bands)};
    Refresh m_scheduled_refresh{sigc::mem_fun(*this, &SchedulePanel::refresh_scheduled_bands)};

    // Scheduled items are drawn without labels, so this is where they say
    // what they are.
    bool on_timeline_tooltip(int x, int y, bool keyboard,
                             const Glib::RefPtr<Gtk::Tooltip>& tooltip);

    bool on_timer_tick();
    void on_activate_clicked();
    void on_complete_clicked();

public:
    SchedulePanel(TreeController& projects, Work& worklog, TaskAttributes& task_attributes,
                  Day& day);
    ~SchedulePanel() override = default;

    // Whatever was staged is replaced, no confirmation.
    void stage_task(int task_id);
};
#endif
