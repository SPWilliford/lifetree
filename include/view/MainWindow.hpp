#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <string>

#include <giomm/simpleactiongroup.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/paned.h>
#include <gtkmm/stack.h>
#include <gtkmm/window.h>

#include "view/LifeTreePage.hpp"
#include "view/ProjectTreePanel.hpp"
#include "view/ProjectsPage.hpp"
#include "view/ReviewPage.hpp"
#include "view/SchedulePanel.hpp"
#include "view/TaskPanel.hpp"

class App;

// Assembles the panels and owns navigation between the daily screen and
// the places you step out of it to. Holds no domain references itself;
// every panel takes the components it reads.
class MainWindow : public Gtk::Window {
public:
    explicit MainWindow(App& app);
    ~MainWindow() override = default;

private:
    // --- header ---
    Gtk::HeaderBar m_header_bar;

    // Where you are inside the app, blank on the daily page. The window
    // title itself stays "LifeTree" for the desktop.
    Gtk::Label m_header_title;

    // Share the header's leading slot; exactly one is shown.
    Gtk::MenuButton m_menu_button;
    Gtk::Button m_back_button;
    Glib::RefPtr<Gio::SimpleActionGroup> m_nav_actions;

    // The one place a destination is decided: switches the stack, names it
    // in the header, swaps menu for back. sub_page is the priority stack's
    // page, or empty for a destination outside it.
    void show_mode(const std::string& name, const std::string& title, const std::string& sub_page);

    // --- daily ---
    // Paned takes two children, so three columns is a paned inside a paned.
    Gtk::Paned m_outer_paned{Gtk::Orientation::HORIZONTAL};
    Gtk::Paned m_inner_paned{Gtk::Orientation::HORIZONTAL};
    Gtk::Box m_daily_page{Gtk::Orientation::VERTICAL, 0};

    // --- priority ---
    // Life tree above, projects below; the two join at the leaves. Each is
    // wrapped with the button that leads across the join.
    Gtk::Box m_priority_page{Gtk::Orientation::VERTICAL, 0};
    Gtk::Stack m_priority_stack;
    Gtk::Box m_life_tree_column{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box m_projects_column{Gtk::Orientation::VERTICAL, 0};
    Gtk::Button m_to_projects_button;
    Gtk::Button m_to_life_tree_button;

    Gtk::Stack m_mode_stack;  // daily | priority | review

    ProjectTreePanel m_projects_panel;
    LifeTreePage m_life_tree_page;
    ProjectsPage m_projects_page;
    ReviewPage m_review_page;
    SchedulePanel m_schedule_panel;
    TaskPanel m_task_panel;
};

#endif
