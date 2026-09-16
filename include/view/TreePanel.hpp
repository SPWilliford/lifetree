#ifndef TREEPANEL_HPP
#define TREEPANEL_HPP
#include <string>
#include <unordered_map>

#include <giomm/liststore.h>
#include <gtkmm/box.h>
#include <gtkmm/listitem.h>
#include <gtkmm/listview.h>
#include <gtkmm/treelistmodel.h>

#include "view/NodeItem.hpp"
#include "view/Refresh.hpp"

class TreeController;
class CardRow;
namespace Gtk {
class Popover;
}

// One tree rendered as CardRow rows in a ListView. Abstract: the machinery
// is shared, the differences between the life tree and a projects tree are
// the virtuals below.
//
// Derived classes must call build() from their own constructor, not rely on
// this one — build() reaches the virtuals, which don't dispatch until the
// derived object exists.
class TreePanel : public Gtk::Box {
public:
    ~TreePanel() override = default;

protected:
    explicit TreePanel(TreeController& tree);

    // Layout, model, factory, signals. Call once, from the derived ctor.
    void build();

    // Real node ids are positive and 0 is the root, so anything negative is
    // a row the display invented: not editable, not deletable, not in any
    // controller.
    static bool is_synthetic_row(int id) { return id < 0; }

    TreeController& m_tree;
    Glib::RefPtr<Gio::ListStore<Glib::Object>> m_root_store;
    std::unordered_map<int, CardRow*> m_cards;
    Refresh m_refresh{sigc::mem_fun(*this, &TreePanel::refresh_tree)};

    // Opens a freshly created node's editor. Deferred internally: ListView
    // builds the row widget in response to the insert, so m_cards has no
    // entry for it until the current signal has been delivered.
    void begin_edit_on(int id);

    // --- what a subclass must supply ---

    // Fill m_root_store with the top-level rows.
    virtual void seed_root_store() = 0;
    virtual bool autoexpand() const = 0;

    // Shown on node 0 when the stored title is empty.
    virtual std::string root_title() const = 0;

    // Everything on a row past its title. Called with every visual already
    // reset, so a subclass only sets what it wants and a recycled row can't
    // carry anything over.
    virtual void decorate_row(CardRow& card, int id) = 0;

    // --- optional ---

    virtual void connect_sources() {}
    virtual void extend_row_menu(Gtk::Box& menu, Gtk::Popover* popover, int id);
    virtual void on_row_activated(int id);
    virtual void on_weight_edited(int id, double value);

private:
    Glib::RefPtr<Gtk::TreeListModel> m_model;
    Gtk::ListView m_view;

    void initialize_layout();
    Glib::RefPtr<Gtk::TreeListModel> create_model();
    Glib::RefPtr<Gio::ListModel> expand_node(const Glib::RefPtr<Glib::ObjectBase>& item);
    void setup_factory();

    void on_setup(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_bind(const Glib::RefPtr<Gtk::ListItem>& item);
    void on_unbind(const Glib::RefPtr<Gtk::ListItem>& item);

    void apply_row_visuals(CardRow& card, int id);

    // Prune before restyle: restyle skips ids the controller no longer has,
    // so the other order works on rows about to disappear.
    void refresh_tree();
    void restyle_bound_rows();

    // Drops rows whose node is gone — changes made elsewhere (Complete in
    // SchedulePanel) never touched this panel's stores.
    void prune_missing();

    void show_row_menu(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id);
    void add_child(const Glib::RefPtr<Gtk::TreeListRow>& row, int parent_id);
    void delete_node(const Glib::RefPtr<Gtk::TreeListRow>& row, int node_id);
};
#endif
