#ifndef PROJECTTREEPANEL_HPP
#define PROJECTTREEPANEL_HPP

#include <unordered_map>

#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/popover.h>
#include <gtkmm/treelistmodel.h>

#include "view/Refresh.hpp"

class CardRow;
class TaskAttributes;
class TreeController;

// The projects tree as a ListView of CardRows. The synthetic root is hidden,
// so each project is a top-level row, and a trailing "+" row adds the next.
class ProjectTreePanel : public Gtk::Box {
public:
    ProjectTreePanel(TreeController& projects, TaskAttributes& task_attributes);
    ~ProjectTreePanel() override = default;

private:
    TreeController& m_tree;
    TaskAttributes& m_task_attributes;

    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_root_store;
    Glib::RefPtr<Gtk::TreeListModel> m_model;
    Gtk::ListView m_view;

    // Row widgets currently bound, by node id. ListView recycles widgets, so
    // an entry exists only while its row is on screen.
    std::unordered_map<int, CardRow*> m_cards;

    Refresh m_refresh{sigc::mem_fun(*this, &ProjectTreePanel::refresh_tree)};

    // Negative ids are rows the display invented (the trailing "+"): not
    // editable, not deletable, not in the controller.
    static bool is_synthetic_row(int id) { return id < 0; }

    void initialize_layout();
    Glib::RefPtr<Gio::ListModel> expand_node(const Glib::RefPtr<Glib::ObjectBase>& item);

    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_unbind(const Glib::RefPtr<Gtk::ListItem>& item);

    // Everything a row shows. Resets every visual first, so a recycled row
    // carries nothing over from what it showed last.
    void apply_row_visuals(CardRow& card, int id);

    void refresh_tree();
    void restyle_bound_rows();

    // Drops rows whose node is gone: changes made elsewhere (Complete in
    // SchedulePanel) never touch this panel's stores.
    void prune_missing();

    void add_project();
    void add_child(const Glib::RefPtr<Gtk::TreeListRow>& row, int parent_id);
    void delete_node(const Glib::RefPtr<Gtk::TreeListRow>& row, int node_id);

    // Opens a just-created node's editor. Deferred: ListView builds the row
    // widget in response to the insert, so m_cards has no entry for it yet.
    void begin_edit_on(int id);

    void show_row_menu(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id);

    // Second levels of the row menu, swapped into the still-open popover.
    Gtk::Widget* build_repeat_config(int root_id, Gtk::Popover* popover);
    Gtk::Widget* build_date_editor(int id, Gtk::Popover* popover);
    Gtk::Widget* build_color_picker(int id, Gtk::Popover* popover);
};

#endif
