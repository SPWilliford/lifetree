#include "view/MainWindow.hpp"
#include "engine/AppEngine.hpp"

MainWindow::MainWindow(AppEngine& engine)
    : m_tree_panel(engine.life(), engine.projects()),
      m_schedule_panel(engine.projects(), engine.work_log()),
      m_task_panel(engine.projects(), engine.work_log())
{
    set_title("Life Tree");
    set_default_size(1400, 1000);

    m_header_bar.set_show_title_buttons(true); // standard minimize/maximize/close
    set_titlebar(m_header_bar);                // Gtk::Window's title (set above) shows
                                                // as the centered header bar text automatically

    m_root_box.set_margin(16);
    m_tree_panel.set_hexpand(false); // sized by its ScrolledWindow's min-content-width
    m_task_panel.set_hexpand(false); // sized by its ScrolledWindow's min-content-width
    m_schedule_panel.set_hexpand(true); // absorbs whatever space is left
    m_schedule_panel.set_vexpand(true);
    m_root_box.append(m_tree_panel);
    m_root_box.append(m_schedule_panel);
    m_root_box.append(m_task_panel);
    set_child(m_root_box);

    // Double-clicking a backlog row stages it in the schedule.
    m_task_panel.signal_task_chosen().connect(
        sigc::mem_fun(m_schedule_panel, &SchedulePanel::stage_task));
}
