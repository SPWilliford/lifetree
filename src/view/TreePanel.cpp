#include "view/TreePanel.hpp"
#include "view/CardRow.hpp"
#include "engine/TreeController.hpp"
#include <gtkmm/singleselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/treeexpander.h>
#include <glibmm/main.h>
#include <algorithm>
#include <vector>
#include <utility>
#include <iostream>

// Flip to 1 while debugging factory binds; 0 for normal use.
#define TREEPANEL_DEBUG 0
#if TREEPANEL_DEBUG
#define TP_LOG(x) std::cout << x << std::endl
#else
#define TP_LOG(x)
#endif

TreePanel::TreePanel(ITreeController& life, ITreeController& projects)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_life(life), m_projects(projects)
{
    initialize_layout();
    bind_actions();

    // Catches changes this panel didn't make itself (e.g. SchedulePanel's
    // Complete button) — self-made changes already update the relevant
    // store directly, so this is mostly a no-op in that case, just a
    // cheap extra scan.
    m_life.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { prune_missing(TreeType::LIFE); });
    });
    m_projects.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { prune_missing(TreeType::PROJECTS); });
    });
}

void TreePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    m_switcher.set_stack(m_stack);
    m_switcher.set_hexpand(true);
    append(m_switcher);

    m_life_model = create_model(TreeType::LIFE);
    m_project_model = create_model(TreeType::PROJECTS);

    setup_factory(m_life_view, TreeType::LIFE);
    m_life_view.set_model(Gtk::SingleSelection::create(m_life_model));
    m_life_view.set_hexpand(true);
    m_life_view.set_vexpand(true);

    setup_factory(m_project_view, TreeType::PROJECTS);
    m_project_view.set_model(Gtk::SingleSelection::create(m_project_model));
    m_project_view.set_hexpand(true);
    m_project_view.set_vexpand(true);

    auto* life_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    life_scroll->set_child(m_life_view);
    life_scroll->set_hexpand(true);
    life_scroll->set_vexpand(true);
    life_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    life_scroll->set_min_content_width(300); // ScrolledWindow doesn't size to its content by
                                              // default — without this it'd ask for ~0 width

    auto* project_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    project_scroll->set_child(m_project_view);
    project_scroll->set_hexpand(true);
    project_scroll->set_vexpand(true);
    project_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    project_scroll->set_min_content_width(300);

    m_stack.add(*life_scroll, "life_page", "Life Tree");
    m_stack.add(*project_scroll, "projects_page", "Projects");
    m_stack.set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);
    m_stack.set_transition_duration(250);
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    append(m_stack);

    auto* toolbar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    toolbar->set_halign(Gtk::Align::CENTER);
    toolbar->append(m_add_button);
    toolbar->append(m_remove_button);
    toolbar->append(m_new_project_button);
    append(*toolbar);

    // Only meaningful on the Projects tab — hide it otherwise.
    auto update_new_project_visibility = [this]() {
        m_new_project_button.set_visible(active_type() == TreeType::PROJECTS);
    };
    m_stack.property_visible_child_name().signal_changed().connect(update_new_project_visibility);
    update_new_project_visibility(); // Life tab is shown first, so this hides it immediately
}

// ---------------------------------------------------------------------
// Small lookup helpers
// ---------------------------------------------------------------------

TreeType TreePanel::active_type() const {
    return (m_stack.get_visible_child_name() == "projects_page") ? TreeType::PROJECTS : TreeType::LIFE;
}

Gtk::ListView& TreePanel::active_view() {
    return (active_type() == TreeType::LIFE) ? m_life_view : m_project_view;
}

Glib::RefPtr<Gio::ListStore<Glib::Object>>& TreePanel::root_store(TreeType type) {
    return (type == TreeType::LIFE) ? m_life_root_store : m_project_root_store;
}

ITreeController& TreePanel::controller_for(TreeType type) {
    return (type == TreeType::LIFE) ? m_life : m_projects;
}

// ---------------------------------------------------------------------
// Model plumbing
// ---------------------------------------------------------------------

Glib::RefPtr<Gtk::TreeListModel> TreePanel::create_model(TreeType type) {
    auto& store = root_store(type);
    store = Gio::ListStore<Glib::Object>::create();

    if (type == TreeType::LIFE) {
        // Life still shows its single root visibly, as "Live a Good Life".
        store->append(TreeObject::create(0));
    } else {
        // Projects hides its synthetic root entirely — the display model
        // starts one level down, so each real project is a top-level row.
        for (int id : controller_for(type).children_of(0)) {
            store->append(TreeObject::create(id));
        }
    }

    return Gtk::TreeListModel::create(store, [this, type](const Glib::RefPtr<Glib::ObjectBase>& item) {
        return expand_node(item, type);
    }, /*passthrough=*/false, /*autoexpand=*/true);
}

Glib::RefPtr<Gio::ListModel> TreePanel::expand_node(const Glib::RefPtr<Glib::ObjectBase>& item, TreeType type) {
    auto obj = std::dynamic_pointer_cast<TreeObject>(item);
    if (!obj) return Glib::RefPtr<Gio::ListModel>();

    auto store = Gio::ListStore<Glib::Object>::create();

    for (int child_id : controller_for(type).children_of(obj->node_id())) {
        store->append(TreeObject::create(child_id));
    }

    return store;
}

void TreePanel::setup_factory(Gtk::ListView& view, TreeType type) {
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &TreePanel::on_setup));
    factory->signal_bind().connect(sigc::bind(sigc::mem_fun(*this, &TreePanel::on_bind), type));
    view.set_factory(factory);
}

// ---------------------------------------------------------------------
// Factory callbacks
// ---------------------------------------------------------------------

void TreePanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* expander = Gtk::make_managed<Gtk::TreeExpander>();

    auto* card = Gtk::make_managed<CardRow>("", [this, item](std::string_view new_text) {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
        if (!row) return;
        auto obj = std::dynamic_pointer_cast<TreeObject>(row->get_item());
        if (obj) {
            controller_for(active_type()).edit(obj->node_id(), new_text);
        }
    });

    expander->set_child(*card);
    item->set_child(*expander);
}

void TreePanel::on_bind(const Glib::RefPtr<Gtk::ListItem>& item, TreeType type) {
    if (!item) return;

    auto* expander = dynamic_cast<Gtk::TreeExpander*>(item->get_child());
    if (!expander) {
        TP_LOG("[TreePanel] on_bind: expander widget missing from row");
        return;
    }

    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    if (!row) {
        TP_LOG("[TreePanel] on_bind: item was not a TreeListRow (unexpected)");
        return;
    }

    expander->set_list_row(row);
    auto obj = std::dynamic_pointer_cast<TreeObject>(row->get_item());
    if (!obj) {
        TP_LOG("[TreePanel] on_bind: TreeListRow payload was not a TreeObject");
        return;
    }

    auto* card = dynamic_cast<CardRow*>(expander->get_child());
    if (!card) return;

    auto title = controller_for(type).get_title(obj->node_id());
    if (title.empty() && obj->node_id() == 0) {
        title = (type == TreeType::LIFE) ? "Live a Good Life" : "Master Project Root";
    }
    card->set_text(title);
}

int TreePanel::resolve_id(Gtk::ListView& view, int position) {
    auto selection = std::dynamic_pointer_cast<Gtk::SingleSelection>(view.get_model());
    if (!selection) return -1;

    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(selection->get_object(position));
    if (!row) return -1;

    auto obj = std::dynamic_pointer_cast<TreeObject>(row->get_item());
    return obj ? obj->node_id() : -1;
}

// ---------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------

void TreePanel::bind_actions() {
    m_add_button.signal_clicked().connect(sigc::mem_fun(*this, &TreePanel::on_add_clicked));
    m_remove_button.signal_clicked().connect(sigc::mem_fun(*this, &TreePanel::on_remove_clicked));
    m_new_project_button.signal_clicked().connect(sigc::mem_fun(*this, &TreePanel::on_new_project_clicked));
}

void TreePanel::on_add_clicked() {
    auto type = active_type();
    auto& view = active_view();
    auto selection = std::dynamic_pointer_cast<Gtk::SingleSelection>(view.get_model());
    if (!selection) return;

    unsigned int pos = selection->get_selected();
    int parent_id = (pos != GTK_INVALID_LIST_POSITION) ? resolve_id(view, pos) : 0;
    if (parent_id == -1) return;

    int new_id = controller_for(type).add(parent_id, "New Sub-Item");
    if (new_id == -1) return; // DB write failed — nothing to add to the view

    if (pos == GTK_INVALID_LIST_POSITION) {
        root_store(type)->append(TreeObject::create(new_id));
        return;
    }

    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(selection->get_object(pos));
    if (!row) return;

    row->set_expanded(true);
    auto store = std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(row->get_children());
    if (store) {
        store->append(TreeObject::create(new_id));
    }
}

void TreePanel::on_new_project_clicked() {
    int new_id = m_projects.add(0, "New Project");
    if (new_id == -1) return; // DB write failed — nothing to add to the view

    root_store(TreeType::PROJECTS)->append(TreeObject::create(new_id));
    m_stack.set_visible_child("projects_page");
}

void TreePanel::prune_missing(TreeType type) {
    auto model = (type == TreeType::LIFE) ? m_life_model : m_project_model;
    if (!model) return;

    auto& controller = controller_for(type);

    // Collect (store, index) pairs in one forward pass first — removing
    // while iterating the flattened model would shift indices out from
    // under later matches.
    std::vector<std::pair<Glib::RefPtr<Gio::ListStore<Glib::Object>>, unsigned int>> stale;

    unsigned int n = model->get_n_items();
    for (unsigned int i = 0; i < n; ++i) {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(model->get_object(i));
        if (!row) continue;
        auto obj = std::dynamic_pointer_cast<TreeObject>(row->get_item());
        if (!obj) continue;
        if (controller.contains(obj->node_id())) continue; // still real, leave it

        auto parent_row = row->get_parent();
        Glib::RefPtr<Gio::ListStore<Glib::Object>> store = parent_row
            ? std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(parent_row->get_children())
            : root_store(type);
        if (!store) continue;

        for (unsigned int j = 0; j < store->get_n_items(); ++j) {
            auto candidate = std::dynamic_pointer_cast<TreeObject>(store->get_item(j));
            if (candidate && candidate->node_id() == obj->node_id()) {
                stale.push_back({store, j});
                break;
            }
        }
    }

    // Remove highest indices first, and within the same store, so
    // removing one match doesn't shift the position of another still
    // waiting to be removed from that same store.
    std::sort(stale.begin(), stale.end(), [](const auto& a, const auto& b) {
        return a.first.get() == b.first.get() ? a.second > b.second : a.first.get() > b.first.get();
    });
    for (auto& entry : stale) {
        entry.first->remove(entry.second);
    }
}

void TreePanel::on_remove_clicked() {
    auto type = active_type();
    auto& view = active_view();
    auto selection = std::dynamic_pointer_cast<Gtk::SingleSelection>(view.get_model());
    if (!selection) return;

    unsigned int pos = selection->get_selected();
    if (pos == GTK_INVALID_LIST_POSITION) return;

    int node_id = resolve_id(view, pos);
    if (node_id <= 0) return;

    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(selection->get_object(pos));

    controller_for(type).remove(node_id);

    if (!row) return;
    auto parent_row = row->get_parent();
    if (!parent_row) return;

    auto store = std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(parent_row->get_children());
    if (!store) return;

    for (unsigned int i = 0; i < store->get_n_items(); ++i) {
        auto obj = std::dynamic_pointer_cast<TreeObject>(store->get_item(i));
        if (obj && obj->node_id() == node_id) {
            store->remove(i);
            break;
        }
    }
}
