#include "view/TreePanel.hpp"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/treeexpander.h>

#include "core/TreeController.hpp"
#include "view/CardRow.hpp"

TreePanel::TreePanel(TreeController& tree)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_tree(tree) {}

void TreePanel::build() {
    initialize_layout();

    m_tree.connect_changed([this]() { m_refresh.request(); });
    connect_sources();
}

void TreePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    m_model = create_model();
    setup_factory();
    m_view.set_model(Gtk::SingleSelection::create(m_model));
    m_view.set_hexpand(true);
    m_view.set_vexpand(true);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_child(m_view);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    // A ScrolledWindow asks for ~0 width, leaving the paned nothing to size
    // against. A floor, not a starting width — that's MainWindow's.
    scroll->set_min_content_width(240);
    append(*scroll);
}

// ---------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------

Glib::RefPtr<Gtk::TreeListModel> TreePanel::create_model() {
    m_root_store = Gio::ListStore<Glib::Object>::create();
    seed_root_store();

    // passthrough=false: rows arrive as TreeListRow wrappers, which is what
    // every cast below expects.
    return Gtk::TreeListModel::create(
        m_root_store,
        [this](const Glib::RefPtr<Glib::ObjectBase>& item) { return expand_node(item); },
        /*passthrough=*/false, autoexpand());
}

Glib::RefPtr<Gio::ListModel> TreePanel::expand_node(const Glib::RefPtr<Glib::ObjectBase>& item) {
    auto obj = std::dynamic_pointer_cast<NodeItem>(item);
    if (!obj || is_synthetic_row(obj->node_id())) return Glib::RefPtr<Gio::ListModel>();

    auto store = Gio::ListStore<Glib::Object>::create();
    for (int child_id : m_tree.children_of(obj->node_id())) {
        store->append(NodeItem::create(child_id));
    }
    return store;
}

void TreePanel::setup_factory() {
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &TreePanel::on_setup));
    factory->signal_bind().connect(sigc::mem_fun(*this, &TreePanel::on_bind));
    factory->signal_unbind().connect(sigc::mem_fun(*this, &TreePanel::on_unbind));
    m_view.set_factory(factory);
}

// ---------------------------------------------------------------------
// Factory callbacks
// ---------------------------------------------------------------------

namespace {
// The node behind a list item. Empty means the item carries no node at
// all — which is NOT the same as a negative id, since a synthetic row
// (the trailing "+") has one and callers do act on it. Returning -1 for
// both is how activation on the "+" row silently stopped working.
std::optional<int> node_id_of(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    if (!row) return std::nullopt;
    auto obj = std::dynamic_pointer_cast<NodeItem>(row->get_item());
    if (!obj) return std::nullopt;
    return obj->node_id();
}
}  // namespace

void TreePanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* expander = Gtk::make_managed<Gtk::TreeExpander>();

    auto* card = Gtk::make_managed<CardRow>("", [this, item](std::string_view new_text) {
        const auto id = node_id_of(item);
        if (id && *id >= 0) m_tree.edit(*id, new_text);
    });

    // Connected once here, not per bind: ListView recycles this widget, so
    // binding per bind stacks one extra call per recycle. Which row it
    // currently shows is read at fire time.
    card->signal_secondary_clicked().connect([this, item, card]() {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
        const auto id = node_id_of(item);
        if (row && id && *id >= 0) show_row_menu(*card, row, *id);
    });

    card->signal_activated().connect([this, item]() {
        const auto id = node_id_of(item);
        if (id) on_row_activated(*id);
    });

    card->signal_weight_changed().connect([this, item](double value) {
        const auto id = node_id_of(item);
        if (!id || *id <= 0) return;
        // Deferred: the handler rewrites siblings and emits, which restyles
        // every bound row including this one, mid-handler.
        const int node = *id;
        Glib::signal_idle().connect_once([this, node, value]() { on_weight_edited(node, value); });
    });

    expander->set_child(*card);
    item->set_child(*expander);
}

void TreePanel::on_bind(const Glib::RefPtr<Gtk::ListItem>& item) {
    if (!item) return;
    auto* expander = dynamic_cast<Gtk::TreeExpander*>(item->get_child());
    if (!expander) return;

    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    if (!row) return;
    expander->set_list_row(row);

    auto* card = dynamic_cast<CardRow*>(expander->get_child());
    auto obj = std::dynamic_pointer_cast<NodeItem>(row->get_item());
    if (!card || !obj) return;

    m_cards[obj->node_id()] = card;
    card->set_action(is_synthetic_row(obj->node_id()));
    apply_row_visuals(*card, obj->node_id());
}

void TreePanel::on_unbind(const Glib::RefPtr<Gtk::ListItem>& item) {
    if (!item) return;
    auto* expander = dynamic_cast<Gtk::TreeExpander*>(item->get_child());
    if (!expander) return;
    auto* card = dynamic_cast<CardRow*>(expander->get_child());
    if (!card) return;

    const auto id = node_id_of(item);
    if (!id) return;

    // Only drop the entry if it still points at this widget: row widgets are
    // recycled, so a late unbind must not evict a mapping a newer bind has
    // already installed for the same id.
    auto it = m_cards.find(*id);
    if (it != m_cards.end() && it->second == card) m_cards.erase(it);
}

void TreePanel::apply_row_visuals(CardRow& card, int id) {
    // An arrow on a node with nothing under it invites a click that opens
    // an empty level, which reads as "there is more here, collapsed".
    //
    // Hidden rather than made non-expandable, which is GTK's own advice for
    // this: expand_node has to keep returning a store even for a childless
    // node, because add_child expands the row and appends straight into
    // that store. Return null and adding the first child to a leaf — the
    // most common edit there is — silently does nothing.
    //
    // Called from apply_row_visuals rather than on_bind alone so the arrow
    // appears the moment a leaf gains a child, without waiting for a
    // rebind. The C function because gtkmm 4.10 doesn't wrap this one.
    // An arrow on a node with nothing under it invites a click that opens an
    // empty level, which reads as "there is more here, collapsed".
    //
    // Marked for the stylesheet rather than hidden here. hide-expander takes
    // the icon's WIDTH away with it, so a leaf's title slides left until it
    // sits under its parent's and the indentation stops reading as depth —
    // and padding it back means naming the arrow's footprint as a number,
    // which is a guess that has to be re-tuned whenever the icon size moves.
    // Style.cpp makes it transparent instead, so GTK reserves exactly the
    // width it always did and nothing has to be measured.
    //
    // Set both ways because ListView recycles these widgets.
    if (auto* expander = dynamic_cast<Gtk::TreeExpander*>(card.get_parent())) {
        const bool childless = !is_synthetic_row(id) && m_tree.children_of(id).empty();
        if (childless)
            expander->add_css_class("leaf-row");
        else
            expander->remove_css_class("leaf-row");
    }

    // Reset first. ListView recycles row widgets, so anything a subclass
    // doesn't set would leak in from whatever this row showed last — which
    // is what lets decorate_row set only what it cares about.
    card.set_text("");
    card.set_marker("");
    card.set_icon("");
    card.set_color("");
    card.hide_weight();
    card.set_tooltip_text("");

    if (!is_synthetic_row(id)) {
        auto title = m_tree.get_title(id);
        if (title.empty() && id == 0) title = root_title();
        card.set_text(title);
    }

    decorate_row(card, id);
}

// ---------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------

void TreePanel::refresh_tree() {
    prune_missing();
    restyle_bound_rows();
}

void TreePanel::restyle_bound_rows() {
    for (auto& [id, card] : m_cards) {
        if (is_synthetic_row(id)) continue;
        // Its node is already gone and prune_missing is about to drop the
        // row — don't blank the text in the meantime.
        if (!m_tree.contains(id)) continue;
        apply_row_visuals(*card, id);
    }
}

void TreePanel::prune_missing() {
    if (!m_model) return;

    // Collect (store, index) in one forward pass: removing while iterating
    // the flattened model shifts indices out from under later matches.
    std::vector<std::pair<Glib::RefPtr<Gio::ListStore<Glib::Object>>, unsigned int>> stale;

    const unsigned int n = m_model->get_n_items();
    for (unsigned int i = 0; i < n; ++i) {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(m_model->get_object(i));
        if (!row) continue;
        auto obj = std::dynamic_pointer_cast<NodeItem>(row->get_item());
        if (!obj) continue;
        if (is_synthetic_row(obj->node_id())) continue;
        if (m_tree.contains(obj->node_id())) continue;

        auto parent_row = row->get_parent();
        Glib::RefPtr<Gio::ListStore<Glib::Object>> store =
            parent_row ? std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(
                             parent_row->get_children())
                       : m_root_store;
        if (!store) continue;

        for (unsigned int j = 0; j < store->get_n_items(); ++j) {
            auto candidate = std::dynamic_pointer_cast<NodeItem>(store->get_item(j));
            if (candidate && candidate->node_id() == obj->node_id()) {
                stale.push_back({store, j});
                break;
            }
        }
    }

    // Highest index first within each store, so removing one match doesn't
    // shift another still waiting.
    std::sort(stale.begin(), stale.end(), [](const auto& a, const auto& b) {
        return a.first.get() == b.first.get() ? a.second > b.second : a.first.get() > b.first.get();
    });
    for (auto& entry : stale) entry.first->remove(entry.second);
}

// ---------------------------------------------------------------------
// Row menu
// ---------------------------------------------------------------------

void TreePanel::extend_row_menu(Gtk::Box&, Gtk::Popover*, int) {}
void TreePanel::on_row_activated(int) {}
void TreePanel::on_weight_edited(int, double) {}

void TreePanel::show_row_menu(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id) {
    auto* popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_parent(card);

    auto* menu_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    menu_box->set_margin(6);

    // Every mutation here is deferred to an idle: the callback is still
    // inside the popover's own click handling, and add_child/delete_node
    // mutate — or destroy — the row this popover is parented to.
    auto* add_button = Gtk::make_managed<Gtk::Button>("Add child");
    add_button->signal_clicked().connect([this, row, id, popover]() {
        popover->popdown();
        Glib::signal_idle().connect_once([this, row, id]() { add_child(row, id); });
    });
    menu_box->append(*add_button);

    if (id > 0) {  // the root is protected
        auto* delete_button = Gtk::make_managed<Gtk::Button>("Delete");
        delete_button->signal_clicked().connect([this, row, id, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once([this, row, id]() { delete_node(row, id); });
        });
        menu_box->append(*delete_button);
    }

    extend_row_menu(*menu_box, popover, id);

    popover->set_child(*menu_box);
    popover->signal_closed().connect(
        [popover]() { Glib::signal_idle().connect_once([popover]() { popover->unparent(); }); });
    popover->popup();
}

void TreePanel::add_child(const Glib::RefPtr<Gtk::TreeListRow>& row, int parent_id) {
    if (!row) return;

    // Grab the child store BEFORE add() writes: if this is the row's first
    // expansion, expand_node then builds it from the old child list, so the
    // append below is correct exactly once either way.
    row->set_expanded(true);
    auto store = std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(row->get_children());

    // Untitled, not "New Sub-Item": the editor opens on it immediately, so a
    // placeholder would only be text to select and delete before typing.
    // Abandoning it leaves a blank row rather than removing the node —
    // deleting something you just made, because you clicked elsewhere, is
    // the worse surprise.
    int new_id = m_tree.add(parent_id, "");
    if (new_id == -1) return;
    if (store) store->append(NodeItem::create(new_id));

    begin_edit_on(new_id);
}

// A row only has a widget while it's on screen, so this quietly does nothing
// for a node created below the fold. The node still exists and is renamable;
// it just isn't already waiting for the cursor.
void TreePanel::begin_edit_on(int id) {
    Glib::signal_idle().connect_once([this, id]() {
        auto it = m_cards.find(id);
        if (it != m_cards.end() && it->second) it->second->begin_edit();
    });
}

void TreePanel::delete_node(const Glib::RefPtr<Gtk::TreeListRow>& row, int node_id) {
    if (node_id <= 0) return;

    m_tree.remove(node_id);

    if (!row) return;
    auto parent_row = row->get_parent();
    if (!parent_row) return;  // top-level: prune_missing clears it from m_root_store

    auto store =
        std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(parent_row->get_children());
    if (!store) return;

    for (unsigned int i = 0; i < store->get_n_items(); ++i) {
        auto obj = std::dynamic_pointer_cast<NodeItem>(store->get_item(i));
        if (obj && obj->node_id() == node_id) {
            store->remove(i);
            break;
        }
    }
}
