#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <gtkmm/window.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/paned.h>
#include "view/TreePanel.hpp"
#include "view/TaskPanel.hpp"
#include "view/SchedulePanel.hpp"
class App;
class MainWindow : public Gtk::Window {
private:
    Gtk::HeaderBar m_header_bar;

    // Nested rather than one three-way split — Gtk::Paned is inherently
    // two children, so three resizable columns means one Paned holding
    // the tree panel and a second, inner Paned (holding schedule + task)
    // as its other child.
    Gtk::Paned     m_outer_paned{Gtk::Orientation::HORIZONTAL};
    Gtk::Paned     m_inner_paned{Gtk::Orientation::HORIZONTAL};

    TreePanel      m_tree_panel;
    SchedulePanel  m_schedule_panel;
    TaskPanel      m_task_panel;
public:
    explicit MainWindow(App& app);
    ~MainWindow() override = default;
};
#endif
