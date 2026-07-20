#ifndef TREEPANEL_HPP
#define TREEPANEL_HPP
#include <gtkmm/box.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/stack.h>
#include <gtkmm/stackswitcher.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treelistmodel.h>
#include <gtkmm/button.h>
#include <giomm/liststore.h>
#include "model/Entities.hpp"

class ITreeController;
class TaskAttributes;
class CardRow;
namespace Gtk { class Popover; }

// Lightweight GObject wrapper to store node IDs inside Gio::ListStore
class TreeObject : public Glib::Object {
private:
    int m_node_id;
protected:
    explicit TreeObject(int node_id) : m_node_id(node_id) {}
public:
    static Glib::RefPtr<TreeObject> create(int node_id) {
        return Glib::make_refptr_for_instance<TreeObject>(new TreeObject(node_id));
    }
    int node_id() const { return m_node_id; }
};

class TreePanel : public Gtk::Box {
private:
    ITreeController& m_life;
    ITreeController& m_projects;
    TaskAttributes& m_task_attributes;

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_life_root_store;
    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_project_root_store;

    Glib::RefPtr<Gtk::TreeListModel> m_life_model;
    Glib::RefPtr<Gtk::TreeListModel> m_project_model;

    Gtk::Stack          m_stack;
    Gtk::StackSwitcher   m_switcher;
    Gtk::ListView       m_life_view;
    Gtk::ListView       m_project_view;
    Gtk::Button         m_new_project_button{"New Project"};

    void initialize_layout();
    void bind_actions();

    Glib::RefPtr<Gio::ListModel> expand_node(const Glib::RefPtr<Glib::ObjectBase>& item, TreeType type);
    Glib::RefPtr<Gtk::TreeListModel> create_model(TreeType type);
    void setup_factory(Gtk::ListView& view, TreeType type);

    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item, TreeType type);

    // Right-click on any row — a small menu (Add child, Delete, and, on
    // the Projects tab only, Make repeating / Stop repeating). "Make
    // repeating" is a second step within the same popover, not its own
    // top-level item — see show_row_menu.
    void on_row_right_clicked(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id);
    void show_row_menu(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id);

    // Both take the row explicitly (from the right-click that triggered
    // them) rather than reading it back off the current selection — the
    // context menu operates on whatever was clicked, not whatever
    // happens to be selected.
    void add_child(TreeType type, const Glib::RefPtr<Gtk::TreeListRow>& row, int parent_id);
    void delete_node(TreeType type, const Glib::RefPtr<Gtk::TreeListRow>& row, int node_id);

    // The weekday/count picker — "second level" of the row menu, reached
    // via "Make repeating…". Returns the widget so show_row_menu can
    // swap it into the still-open popover.
    Gtk::Widget* build_repeat_config(int id, Gtk::Popover* popover);

    void on_new_project_clicked();

    // Removes any currently-materialized row whose id no longer exists in
    // the real tree — handles changes made from elsewhere (e.g. Complete
    // in SchedulePanel) that this panel didn't make itself and so never
    // updated its own stores for.
    void prune_missing(TreeType type);

    TreeType active_type() const;
    Glib::RefPtr<Gio::ListStore<Glib::Object>>& root_store(TreeType type);
    ITreeController& controller_for(TreeType type);

public:
    TreePanel(ITreeController& life, ITreeController& projects, TaskAttributes& task_attributes);
    ~TreePanel() override = default;
};
#endif
