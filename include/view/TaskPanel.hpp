#ifndef TASKPANEL_HPP
#define TASKPANEL_HPP
#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <sigc++/sigc++.h>

#include "view/Refresh.hpp"

class TreeController;
class TaskAttributes;
class Priority;

// The backlog: every task workable right now, interleaved by project
// priority. A read-only mirror of the projects tree.
//
// Optionally narrowed to a single project. The filter is view state and
// lives only as long as the panel — a narrowing that survived a restart
// would look exactly like most of the backlog having vanished.
class TaskPanel : public Gtk::Box {
private:
    TreeController& m_projects;
    TaskAttributes& m_task_attributes;
    Priority& m_priority;

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_store;

    Gtk::ListView m_list_view;

    // -1 for no filter. Otherwise a project root id: the whole subtree
    // under it passes, which is what "only this project" means.
    int m_filter_root_id = -1;

    Gtk::MenuButton m_filter_button;
    Gtk::Popover m_filter_popover;
    Gtk::Image m_filter_icon;

    // The list's heading: the project it's narrowed to, or empty when it
    // isn't narrowed. Named rather than left to the icon alone, because a
    // filter you can't see is indistinguishable from having lost most of
    // your tasks.
    Gtk::Label m_filter_label;

    // Double-click, with that task's id. Everything past "send it to the
    // schedule" is SchedulePanel's.
    sigc::signal<void(int)> m_task_chosen;

    void initialize_layout();
    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item);

    // Every top-level project, in tree order — the same order as the panel
    // above it, so an entry stays where it was last time. Ranking by
    // priority would reshuffle the menu as weights move.
    void rebuild_filter_menu();
    void set_filter(int root_id);
    void update_filter_button();

public:
    TaskPanel(TreeController& projects, TaskAttributes& task_attributes, Priority& priority);
    ~TaskPanel() override = default;

    // Public mainly for the initial population; otherwise driven by the
    // signals wired in the .cpp.
    void refresh();

    sigc::signal<void(int)> signal_task_chosen() { return m_task_chosen; }

private:
    // All three signals want the same response, and a change tripping two
    // at once still rebuilds only once.
    Refresh m_refresh{sigc::mem_fun(*this, &TaskPanel::refresh)};
};
#endif
