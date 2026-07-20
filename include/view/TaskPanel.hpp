#ifndef TASKPANEL_HPP
#define TASKPANEL_HPP
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/button.h>
#include <gtkmm/listview.h>
#include <gtkmm/listitem.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>
#include <gtkmm/listbox.h>
#include <giomm/liststore.h>
#include <sigc++/sigc++.h>

class ITreeController;
class WorkLog;
class TaskAttributes;

class TaskPanel : public Gtk::Box {
private:
    ITreeController& m_projects;
    WorkLog& m_worklog;
    TaskAttributes& m_task_attributes;

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_store;

    // header slot
    Gtk::Box           m_header{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::StackSwitcher  m_switcher;

    // "Backlog" page
    Gtk::Stack    m_stack;
    Gtk::ListView m_list_view;

    // "Completed Today" page — read-only review, no interaction beyond
    // viewing. Rebuilt lazily whenever this tab becomes visible, rather
    // than kept live — it's a look back at the past, not something that
    // needs to update the instant a completion happens elsewhere.
    Gtk::ListBox m_completed_list;

    // Fired when a row is double-clicked, with that task's id. Completion
    // and everything past "send it to the schedule" happens in
    // SchedulePanel now — this panel is a read-only mirror of the tree.
    sigc::signal<void(int)> m_task_chosen;

    void initialize_layout();
    void bind_actions();
    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item);
    void refresh_completed();

public:
    TaskPanel(ITreeController& projects, WorkLog& worklog, TaskAttributes& task_attributes);
    ~TaskPanel() override = default;

    // Rebuilds the list from the current set of leaf nodes. Called
    // automatically whenever the projects controller reports a change
    // (see connect_changed in the .cpp) — public mainly for the initial
    // population at construction.
    void refresh();

    sigc::signal<void(int)> signal_task_chosen() { return m_task_chosen; }
};
#endif
