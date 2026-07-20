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

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_life_root_store;
    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_project_root_store;

    Glib::RefPtr<Gtk::TreeListModel> m_life_model;
    Glib::RefPtr<Gtk::TreeListModel> m_project_model;

    Gtk::Stack          m_stack;
    Gtk::StackSwitcher   m_switcher;
    Gtk::ListView       m_life_view;
    Gtk::ListView       m_project_view;
    Gtk::Button         m_add_button{"[+]"};
    Gtk::Button         m_remove_button{"[\u2212]"};
    Gtk::Button         m_new_project_button{"New Project"};

    void initialize_layout();
    void bind_actions();

    Glib::RefPtr<Gio::ListModel> expand_node(const Glib::RefPtr<Glib::ObjectBase>& item, TreeType type);
    Glib::RefPtr<Gtk::TreeListModel> create_model(TreeType type);
    void setup_factory(Gtk::ListView& view, TreeType type);

    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item, TreeType type);

    void on_add_clicked();
    void on_remove_clicked();
    void on_new_project_clicked();

    // Removes any currently-materialized row whose id no longer exists in
    // the real tree — handles changes made from elsewhere (e.g. Complete
    // in SchedulePanel) that this panel didn't make itself and so never
    // updated its own stores for.
    void prune_missing(TreeType type);

    TreeType active_type() const;
    Gtk::ListView& active_view();
    Glib::RefPtr<Gio::ListStore<Glib::Object>>& root_store(TreeType type);
    ITreeController& controller_for(TreeType type);

public:
    TreePanel(ITreeController& life, ITreeController& projects);
    ~TreePanel() override = default;

    int resolve_id(Gtk::ListView& view, int position);
};
#endif
