#include "view/TreePanel.hpp"
#include "view/CardRow.hpp"
#include "engine/TreeController.hpp"
#include "engine/TaskAttributes.hpp"
#include <gtkmm/singleselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/treeexpander.h>
#include <gtkmm/popover.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/adjustment.h>
#include <glibmm/main.h>
#include <algorithm>
#include <vector>
#include <utility>
#include <array>
#include <iostream>

// Flip to 1 while debugging factory binds; 0 for normal use.
#define TREEPANEL_DEBUG 0
#if TREEPANEL_DEBUG
#define TP_LOG(x) std::cout << x << std::endl
#else
#define TP_LOG(x)
#endif

TreePanel::TreePanel(ITreeController& life, ITreeController& projects, TaskAttributes& task_attributes)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_life(life), m_projects(projects), m_task_attributes(task_attributes)
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
    life_scroll->set_min_content_width(340); // ScrolledWindow doesn't size to its content by
                                              // default — without this it'd ask for ~0 width

    auto* project_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    project_scroll->set_child(m_project_view);
    project_scroll->set_hexpand(true);
    project_scroll->set_vexpand(true);
    project_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    project_scroll->set_min_content_width(340);

    m_stack.add(*life_scroll, "life_page", "Life Tree");
    m_stack.add(*project_scroll, "projects_page", "Projects");
    m_stack.set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);
    m_stack.set_transition_duration(250);
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    append(m_stack);

    m_new_project_button.set_halign(Gtk::Align::CENTER);
    append(m_new_project_button);

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

    // Life stays auto-expanded — it's small and meant to always be
    // visible at a glance. Projects starts collapsed — as project trees
    // grow, defaulting to expanded would mean scrolling past everything
    // just to see the top-level list.
    bool autoexpand = (type == TreeType::LIFE);

    return Gtk::TreeListModel::create(store, [this, type](const Glib::RefPtr<Glib::ObjectBase>& item) {
        return expand_node(item, type);
    }, /*passthrough=*/false, autoexpand);
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

    card->signal_secondary_clicked().connect([this, item, card]() {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
        if (!row) return;
        auto obj = std::dynamic_pointer_cast<TreeObject>(row->get_item());
        if (obj) {
            on_row_right_clicked(*card, row, obj->node_id());
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

    // is_generator() is only meaningful for Projects-tree ids — Life and
    // Projects each have their own independent id space, so checking it
    // against a Life node would be a coincidental, meaningless lookup.
    bool is_generator = (type == TreeType::PROJECTS) && m_task_attributes.is_generator(obj->node_id());
    card->set_marker(is_generator ? "🔁" : "");
}

// ---------------------------------------------------------------------
// Repeat menu
// ---------------------------------------------------------------------

void TreePanel::on_row_right_clicked(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id) {
    show_row_menu(card, row, id);
}

void TreePanel::show_row_menu(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row, int id) {
    TreeType type = active_type();

    auto* popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_parent(card);

    auto* menu_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    menu_box->set_margin(6);

    auto* add_button = Gtk::make_managed<Gtk::Button>("Add child");
    add_button->signal_clicked().connect([this, type, row, id, popover]() {
        popover->popdown();
        // Deferred to idle — add_child() mutates the very row this
        // popover is parented to, and this callback is still technically
        // "inside" the popover's own click handling at this point. Same
        // reentrancy concern every other GTK list-store mutation in this
        // file already defers for.
        Glib::signal_idle().connect_once([this, type, row, id]() {
            add_child(type, row, id);
        });
    });
    menu_box->append(*add_button);

    if (id > 0) { // the root is protected — never offer to delete it
        auto* delete_button = Gtk::make_managed<Gtk::Button>("Delete");
        delete_button->signal_clicked().connect([this, type, row, id, popover]() {
            popover->popdown();
            // Deferred to idle — delete_node() can destroy the very row
            // (and CardRow) this popover is parented to. Doing that
            // synchronously, before the popover has finished closing,
            // risks tearing down a widget still in use mid-callback —
            // this is the one that was actually crashing.
            Glib::signal_idle().connect_once([this, type, row, id]() {
                delete_node(type, row, id);
            });
        });
        menu_box->append(*delete_button);
    }

    // Repeating is a task concept — Projects tab only, and never on the
    // root (which is hidden there anyway, so id is never 0 in practice).
    if (type == TreeType::PROJECTS && id > 0) {
        if (m_task_attributes.is_generator(id)) {
            auto* stop_button = Gtk::make_managed<Gtk::Button>("Stop repeating");
            stop_button->signal_clicked().connect([this, id, popover]() {
                popover->popdown();
                Glib::signal_idle().connect_once([this, id]() {
                    m_task_attributes.unmark_repeating(id);
                });
            });
            menu_box->append(*stop_button);
        } else {
            auto* repeat_button = Gtk::make_managed<Gtk::Button>("Make repeating…");
            repeat_button->signal_clicked().connect([this, id, popover]() {
                // Swaps the popover's content in place — this is the
                // "second level" of the menu, reached only from here,
                // rather than being the first thing you see on right-click.
                // No tree mutation happens here, just a widget swap, so
                // no deferral needed for this one.
                popover->set_child(*build_repeat_config(id, popover));
            });
            menu_box->append(*repeat_button);
        }
    }

    popover->set_child(*menu_box);

    // Deferred to idle rather than unparented directly in the closed
    // handler — same reentrancy concern as everywhere else GTK list-store
    // mutations get deferred: this callback is still technically "inside"
    // the popover's own signal at that point.
    popover->signal_closed().connect([popover]() {
        Glib::signal_idle().connect_once([popover]() { popover->unparent(); });
    });

    popover->popup();
}

Gtk::Widget* TreePanel::build_repeat_config(int id, Gtk::Popover* popover) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    // One toggle per weekday, ordered Sun..Sat to match
    // RepeatedTaskRow::weekday_mask's bit order (tm_wday: Sunday=0),
    // so building the mask below needs no reordering.
    auto* days_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    static const char* day_labels[7] = { "S", "M", "T", "W", "T", "F", "S" };
    auto day_buttons = std::make_shared<std::array<Gtk::ToggleButton*, 7>>();
    for (int i = 0; i < 7; ++i) {
        auto* btn = Gtk::make_managed<Gtk::ToggleButton>(day_labels[i]);
        btn->set_active(true); // default: every day
        days_box->append(*btn);
        (*day_buttons)[i] = btn;
    }
    box->append(*days_box);

    auto count_adjustment = Gtk::Adjustment::create(1, 1, 20, 1);
    auto* count_spin = Gtk::make_managed<Gtk::SpinButton>(count_adjustment);
    box->append(*count_spin);

    auto* apply_button = Gtk::make_managed<Gtk::Button>("Repeat");
    apply_button->signal_clicked().connect([this, id, day_buttons, count_spin, popover]() {
        int mask = 0;
        for (int i = 0; i < 7; ++i) {
            if ((*day_buttons)[i]->get_active()) mask |= (1 << i);
        }
        int count = count_spin->get_value_as_int();
        popover->popdown();
        // mask/count are plain values, read before deferring — day_buttons
        // and count_spin themselves live inside this popover and shouldn't
        // be touched from the deferred callback below, after it's closing.
        Glib::signal_idle().connect_once([this, id, mask, count]() {
            m_task_attributes.mark_repeating(id, mask, count);
        });
    });
    box->append(*apply_button);

    return box;
}

// ---------------------------------------------------------------------
// Button handlers
// ---------------------------------------------------------------------

void TreePanel::bind_actions() {
    m_new_project_button.signal_clicked().connect(sigc::mem_fun(*this, &TreePanel::on_new_project_clicked));
}

void TreePanel::add_child(TreeType type, const Glib::RefPtr<Gtk::TreeListRow>& row, int parent_id) {
    if (!row) return;

    // Grab the child store BEFORE add() touches the database below —
    // that's what guarantees expand_node(), if this is the row's
    // first-ever expansion, builds its store from the *old* child list
    // (not yet including the new one). That makes the manual append
    // below always correct exactly once, whether this row had been
    // expanded before or not — rather than depending on which case
    // we're in.
    row->set_expanded(true);
    auto store = std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(row->get_children());

    int new_id = controller_for(type).add(parent_id, "New Sub-Item");
    if (new_id == -1) return; // DB write failed — nothing to add to the view

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

void TreePanel::delete_node(TreeType type, const Glib::RefPtr<Gtk::TreeListRow>& row, int node_id) {
    if (node_id <= 0) return; // protect the root

    controller_for(type).remove(node_id);

    if (!row) return;
    auto parent_row = row->get_parent();
    if (!parent_row) return; // top-level row — prune_missing() (already wired to
                              // connect_changed) cleans this up from root_store instead

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
