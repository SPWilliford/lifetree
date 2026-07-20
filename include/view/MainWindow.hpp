#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <gtkmm/window.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/box.h>
#include "view/TreePanel.hpp"
#include "view/TaskPanel.hpp"
#include "view/SchedulePanel.hpp"
class AppEngine;
class MainWindow : public Gtk::Window {
private:
    Gtk::HeaderBar m_header_bar;
    Gtk::Box       m_root_box{Gtk::Orientation::HORIZONTAL, 16};
    TreePanel      m_tree_panel;
    SchedulePanel  m_schedule_panel;
    TaskPanel      m_task_panel;
public:
    explicit MainWindow(AppEngine& engine);
    ~MainWindow() override = default;
};
#endif
