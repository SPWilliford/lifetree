#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <gtkmm/window.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/paned.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
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

    // The window's whole content: the three panels above, footer below.
    Gtk::Box       m_root{Gtk::Orientation::VERTICAL, 0};

    Work&          m_work;

    TreePanel      m_tree_panel;
    SchedulePanel  m_schedule_panel;
    TaskPanel      m_task_panel;

    Gtk::Box       m_footer{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Label     m_footer_date;
    Gtk::Label     m_footer_tracked;

    // Panel borders live in CSS rather than being drawn, unlike the
    // schedule panel's gold frame — that one is Cairo because it has to
    // break around the staged-task dock, which a CSS border can't do.
    // Installed for the whole display, so the classes are usable from any
    // widget (card-row-active included, once something calls set_active).
    void apply_styles();

    void refresh_footer();
public:
    explicit MainWindow(App& app);
    ~MainWindow() override = default;
};
#endif
