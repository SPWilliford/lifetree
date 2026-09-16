#ifndef TASKPANEL_HPP
#define TASKPANEL_HPP

#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <sigc++/sigc++.h>

#include "view/Refresh.hpp"

class TreeController;
class TaskAttributes;
class Priority;

// Every task workable right now, interleaved by project
// priority. Read-only; double-click sends a task to the schedule.
// Optionally narrowed to one project, for this session only.
class TaskPanel : public Gtk::Box {
public:
    TaskPanel(TreeController& projects, TaskAttributes& task_attributes, Priority& priority);
    ~TaskPanel() override = default;

    void refresh();

    sigc::signal<void(int)> signal_task_chosen() { return m_task_chosen; }

private:
    TreeController& m_projects;
    TaskAttributes& m_task_attributes;
    Priority& m_priority;

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_store;
    Gtk::ListView m_list_view;

    // A project root id, or -1 for no filter. The whole subtree passes.
    int m_filter_root_id = -1;

    Gtk::MenuButton m_filter_button;
    Gtk::Popover m_filter_popover;
    Gtk::Image m_filter_icon;
    Gtk::Label m_filter_label;  // the narrowed-to project; empty when unfiltered

    sigc::signal<void(int)> m_task_chosen;

    void initialize_layout();
    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item);

    void rebuild_filter_menu();
    void set_filter(int root_id);
    void update_filter_button();

    Refresh m_refresh{sigc::mem_fun(*this, &TaskPanel::refresh)};
};

#endif
