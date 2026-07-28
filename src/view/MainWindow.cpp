#include "view/MainWindow.hpp"
#include "core/App.hpp"

MainWindow::MainWindow(App& app)
    : m_tree_panel(app.life(), app.projects(), app.task_attributes(), app.priority()),
      m_schedule_panel(app.projects(), app.work(), app.task_attributes()),
      m_task_panel(app.projects(), app.work(), app.task_attributes(), app.priority())
{
    set_title("LifeTree");
    set_default_size(1400, 1000);

    m_header_bar.set_show_title_buttons(true); // standard minimize/maximize/close
    set_titlebar(m_header_bar);                // Gtk::Window's title (set above) shows
                                                // as the centered header bar text automatically

    m_tree_panel.set_hexpand(true);
    m_schedule_panel.set_hexpand(true);
    m_schedule_panel.set_vexpand(true);
    m_task_panel.set_hexpand(true);

    // A small gap on each side facing a divider — otherwise the panels
    // sit flush against the Paned's thin drag handle with nothing but
    // that handle between them.
    constexpr int GAP = 8;
    m_tree_panel.set_margin_end(GAP);
    m_schedule_panel.set_margin_start(GAP);
    m_schedule_panel.set_margin_end(GAP);
    m_task_panel.set_margin_start(GAP);

    m_inner_paned.set_start_child(m_schedule_panel);
    m_inner_paned.set_end_child(m_task_panel);
    m_inner_paned.set_resize_start_child(true); // schedule absorbs space as the divider moves,
    m_inner_paned.set_resize_end_child(false);  // task keeps whatever width you last dragged it to

    m_outer_paned.set_start_child(m_tree_panel);
    m_outer_paned.set_end_child(m_inner_paned);
    m_outer_paned.set_resize_start_child(false); // tree keeps whatever width you last dragged it to
    m_outer_paned.set_resize_end_child(true);    // the schedule+task pair absorbs the rest

    // Same starting proportions the old fixed widths gave — 340 for
    // tree, 420 for task, schedule getting whatever's left of the
    // default 1400 width. Purely a starting point now, not a floor —
    // every divider is draggable from here.
    m_outer_paned.set_position(340);
    m_inner_paned.set_position(1400 - 32 /* margin */ - 340 /* tree */ - 420 /* task */);

    m_outer_paned.set_margin(16);
    set_child(m_outer_paned);

    // Double-clicking a backlog row stages it in the schedule.
    m_task_panel.signal_task_chosen().connect(
        sigc::mem_fun(m_schedule_panel, &SchedulePanel::stage_task));
}
