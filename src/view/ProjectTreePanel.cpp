#include "view/ProjectTreePanel.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glibmm/datetime.h>
#include <glibmm/main.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/calendar.h>
#include <gtkmm/entry.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/treeexpander.h>
#include <pangomm/layout.h>

#include "core/Clock.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "view/CardRow.hpp"
#include "view/NodeItem.hpp"
#include "view/Style.hpp"

namespace {

// The trailing "+" row lives in the store beside real nodes so it sits where
// the next project will appear even after the list scrolls.
constexpr int NEW_PROJECT_ROW = -1;

bool is_new_project_row(int id) {
    return id == NEW_PROJECT_ROW;
}

// The node behind a list item, or nothing if the item carries no node. A
// synthetic row has an id (negative) and callers act on it, so this is not
// the same as returning -1.
std::optional<int> node_id_of(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
    if (!row) return std::nullopt;
    auto obj = std::dynamic_pointer_cast<NodeItem>(row->get_item());
    if (!obj) return std::nullopt;
    return obj->node_id();
}

}  // namespace

ProjectTreePanel::ProjectTreePanel(TreeController& projects, TaskAttributes& task_attributes)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12),
      m_tree(projects),
      m_task_attributes(task_attributes) {
    initialize_layout();

    m_tree.connect_changed([this]() { m_refresh.request(); });

    // Colors, repeat marks and completions change how a row looks without
    // touching the tree.
    m_task_attributes.connect_changed([this]() { m_refresh.request(); });
}

void ProjectTreePanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    m_root_store = Gio::ListStore<Glib::Object>::create();
    for (int id : m_tree.children_of(0)) {
        m_root_store->append(NodeItem::create(id));
    }
    m_root_store->append(NodeItem::create(NEW_PROJECT_ROW));

    // passthrough=false: rows arrive as TreeListRow wrappers, which every
    // cast below expects. Not autoexpanded: project trees grow.
    m_model = Gtk::TreeListModel::create(
        m_root_store,
        [this](const Glib::RefPtr<Glib::ObjectBase>& item) { return expand_node(item); },
        /*passthrough=*/false, /*autoexpand=*/false);

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &ProjectTreePanel::on_setup));
    factory->signal_bind().connect(sigc::mem_fun(*this, &ProjectTreePanel::on_bind));
    factory->signal_unbind().connect(sigc::mem_fun(*this, &ProjectTreePanel::on_unbind));

    m_view.set_factory(factory);
    m_view.set_model(Gtk::SingleSelection::create(m_model));
    m_view.set_hexpand(true);
    m_view.set_vexpand(true);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_child(m_view);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);
    scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_width(240);
    append(*scroll);
}

// Returns a store even for a childless node: add_child expands the row and
// appends into this store, so returning null would make adding the first
// child to a leaf silently do nothing.
Glib::RefPtr<Gio::ListModel> ProjectTreePanel::expand_node(
    const Glib::RefPtr<Glib::ObjectBase>& item) {
    auto obj = std::dynamic_pointer_cast<NodeItem>(item);
    if (!obj || is_synthetic_row(obj->node_id())) return Glib::RefPtr<Gio::ListModel>();

    auto store = Gio::ListStore<Glib::Object>::create();
    for (int child_id : m_tree.children_of(obj->node_id())) {
        store->append(NodeItem::create(child_id));
    }
    return store;
}

// ---------------------------------------------------------------------
// Factory callbacks
// ---------------------------------------------------------------------

void ProjectTreePanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* expander = Gtk::make_managed<Gtk::TreeExpander>();

    auto* card = Gtk::make_managed<CardRow>([this, item](std::string_view new_text) {
        const auto id = node_id_of(item);
        if (id && *id >= 0) m_tree.edit(*id, new_text);
    });

    // Connected here, not per bind: the widget is recycled, and which row it
    // currently shows is read at fire time.
    card->signal_secondary_clicked().connect([this, item, card]() {
        auto row = std::dynamic_pointer_cast<Gtk::TreeListRow>(item->get_item());
        const auto id = node_id_of(item);
        if (row && id && *id >= 0) show_row_menu(*card, row, *id);
    });

    card->signal_activated().connect([this, item]() {
        const auto id = node_id_of(item);
        if (id && is_new_project_row(*id)) add_project();
    });

    expander->set_child(*card);
    item->set_child(*expander);
}

void ProjectTreePanel::on_bind(const Glib::RefPtr<Gtk::ListItem>& item) {
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

void ProjectTreePanel::on_unbind(const Glib::RefPtr<Gtk::ListItem>& item) {
    if (!item) return;
    auto* expander = dynamic_cast<Gtk::TreeExpander*>(item->get_child());
    if (!expander) return;
    auto* card = dynamic_cast<CardRow*>(expander->get_child());
    if (!card) return;

    const auto id = node_id_of(item);
    if (!id) return;

    // Only if the entry still points at this widget: a late unbind must not
    // evict a mapping a newer bind has installed for the same id.
    auto it = m_cards.find(*id);
    if (it != m_cards.end() && it->second == card) m_cards.erase(it);
}

void ProjectTreePanel::apply_row_visuals(CardRow& card, int id) {
    // A leaf's expander arrow is made transparent by the stylesheet rather
    // than hidden: hiding takes its width with it and the title slides left.
    if (auto* expander = dynamic_cast<Gtk::TreeExpander*>(card.get_parent())) {
        const bool childless = !is_synthetic_row(id) && m_tree.children_of(id).empty();
        if (childless) {
            expander->add_css_class("leaf-row");
        } else {
            expander->remove_css_class("leaf-row");
        }
    }

    card.set_text("");
    card.set_marker("");
    card.set_icon("");
    card.set_color("");
    card.set_tooltip_text("");

    if (is_new_project_row(id)) {
        card.set_icon("list-add-symbolic");
        card.set_tooltip_text("Add a new project");
        return;
    }

    card.set_text(m_tree.get_title(id));
    card.set_color(m_task_attributes.get_color(id));

    // Trailing marks, in a fixed order so they don't reshuffle as attributes
    // change. Plain text symbols rather than emoji, so they take the label's
    // color.
    std::vector<std::string> marks;

    // A dated task is absent from the task list, and this is the only place
    // that says why. Dropped once the date has passed, unless it recurs.
    if (m_task_attributes.has_date(id) &&
        (!m_task_attributes.date_has_passed(id) || m_task_attributes.recurs(id))) {
        const bool timed = m_task_attributes.date_settings(id).time_start != TaskDateRow::NO_TIME;
        const bool is_today = m_task_attributes.date_is_today(id);
        marks.push_back(timed ? (is_today ? "◷" : "▦◷") : "▦");
    }

    // Only on a routine's top row; the rows beneath carry overrides, not
    // routines of their own.
    if (m_task_attributes.is_repeat_root(id)) marks.push_back("↻");
    if (m_task_attributes.is_sequential(id)) marks.push_back("↓");

    std::string text;
    for (const auto& mark : marks) {
        if (!text.empty()) text += " ";
        text += mark;
    }
    card.set_marker(text);
}

// ---------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------

void ProjectTreePanel::refresh_tree() {
    // Prune first: restyle skips ids the controller no longer has.
    prune_missing();
    restyle_bound_rows();
}

void ProjectTreePanel::restyle_bound_rows() {
    for (auto& [id, card] : m_cards) {
        if (is_synthetic_row(id)) continue;
        if (!m_tree.contains(id)) continue;
        apply_row_visuals(*card, id);
    }
}

void ProjectTreePanel::prune_missing() {
    if (!m_model) return;

    // Collect (store, index) first: removing while iterating the flattened
    // model shifts indices out from under later matches.
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

    // Highest index first within each store, so one removal can't shift
    // another still waiting.
    std::sort(stale.begin(), stale.end(), [](const auto& a, const auto& b) {
        return a.first.get() == b.first.get() ? a.second > b.second : a.first.get() > b.first.get();
    });
    for (auto& entry : stale) entry.first->remove(entry.second);
}

// ---------------------------------------------------------------------
// Adding and removing nodes
// ---------------------------------------------------------------------

void ProjectTreePanel::add_project() {
    const int new_id = m_tree.add(0, "");
    if (new_id == -1) return;

    // Before the trailing "+", so that row stays last.
    const unsigned int n = m_root_store->get_n_items();
    m_root_store->insert(n > 0 ? n - 1 : 0, NodeItem::create(new_id));

    begin_edit_on(new_id);
}

void ProjectTreePanel::add_child(const Glib::RefPtr<Gtk::TreeListRow>& row, int parent_id) {
    if (!row) return;

    // Grab the child store BEFORE add() writes: on a first expansion,
    // expand_node builds it from the old child list, so the append below is
    // correct exactly once either way.
    row->set_expanded(true);
    auto store = std::dynamic_pointer_cast<Gio::ListStore<Glib::Object>>(row->get_children());

    // Untitled, with the editor opening on it. An abandoned blank node stays
    // rather than being deleted on empty commit.
    const int new_id = m_tree.add(parent_id, "");
    if (new_id == -1) return;
    if (store) store->append(NodeItem::create(new_id));

    begin_edit_on(new_id);
}

// Quietly does nothing for a node created below the fold: a row has no
// widget until it's on screen.
void ProjectTreePanel::begin_edit_on(int id) {
    Glib::signal_idle().connect_once([this, id]() {
        auto it = m_cards.find(id);
        if (it != m_cards.end() && it->second) it->second->begin_edit();
    });
}

void ProjectTreePanel::delete_node(const Glib::RefPtr<Gtk::TreeListRow>& row, int node_id) {
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

// ---------------------------------------------------------------------
// Row menu
// ---------------------------------------------------------------------

// Every mutation from the menu is deferred to an idle: the handler is still
// inside the popover's own click dispatch, and the mutation may destroy the
// row the popover is parented to.
void ProjectTreePanel::show_row_menu(CardRow& card, const Glib::RefPtr<Gtk::TreeListRow>& row,
                                     int id) {
    if (id <= 0) return;

    auto* popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_parent(card);

    auto* menu = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    menu->set_margin(6);

    auto* add_button = Gtk::make_managed<Gtk::Button>("Add child");
    add_button->signal_clicked().connect([this, row, id, popover]() {
        popover->popdown();
        Glib::signal_idle().connect_once([this, row, id]() { add_child(row, id); });
    });
    menu->append(*add_button);

    auto* delete_button = Gtk::make_managed<Gtk::Button>("Delete");
    delete_button->signal_clicked().connect([this, row, id, popover]() {
        popover->popdown();
        Glib::signal_idle().connect_once([this, row, id]() { delete_node(row, id); });
    });
    menu->append(*delete_button);

    const bool container = m_task_attributes.is_container(id);
    auto* kind_button =
        Gtk::make_managed<Gtk::Button>(container ? "Make action" : "Make container");
    kind_button->signal_clicked().connect([this, id, container, popover]() {
        popover->popdown();
        Glib::signal_idle().connect_once([this, id, container]() {
            if (container) {
                m_task_attributes.mark_action(id);
            } else {
                m_task_attributes.mark_container(id);
            }
        });
    });
    menu->append(*kind_button);

    auto* date_button = Gtk::make_managed<Gtk::Button>(
        m_task_attributes.has_date(id) ? "Change date/time…" : "Set date/time…");
    date_button->signal_clicked().connect(
        [this, id, popover]() { popover->set_child(*build_date_editor(id, popover)); });
    menu->append(*date_button);

    if (m_task_attributes.has_date(id)) {
        auto* clear_button = Gtk::make_managed<Gtk::Button>("Clear date/time");
        clear_button->signal_clicked().connect([this, id, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once([this, id]() { m_task_attributes.clear_date(id); });
        });
        menu->append(*clear_button);
    }

    // Marks the parent: its children happen in order.
    if (!m_tree.children_of(id).empty()) {
        const bool sequential = m_task_attributes.is_sequential(id);
        auto* seq_button = Gtk::make_managed<Gtk::Button>(sequential ? "Unordered" : "Do in order");
        seq_button->signal_clicked().connect([this, id, sequential, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once([this, id, sequential]() {
                if (sequential) {
                    m_task_attributes.unmark_sequential(id);
                } else {
                    m_task_attributes.mark_sequential(id);
                }
            });
        });
        menu->append(*seq_button);
    }

    // A node inside someone else's routine has a schedule but doesn't own
    // it: it opens the routine's grid rather than starting a nested repeat.
    const int repeat_root = m_task_attributes.repeat_root_of(id);
    if (repeat_root == id) {
        auto* edit_button = Gtk::make_managed<Gtk::Button>("Edit schedule…");
        edit_button->signal_clicked().connect(
            [this, id, popover]() { popover->set_child(*build_repeat_config(id, popover)); });
        menu->append(*edit_button);

        auto* stop_button = Gtk::make_managed<Gtk::Button>("Stop repeating");
        stop_button->signal_clicked().connect([this, id, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once(
                [this, id]() { m_task_attributes.unmark_repeating(id); });
        });
        menu->append(*stop_button);
    } else if (repeat_root > 0) {
        auto* edit_button = Gtk::make_managed<Gtk::Button>("Edit schedule…");
        edit_button->signal_clicked().connect([this, repeat_root, popover]() {
            popover->set_child(*build_repeat_config(repeat_root, popover));
        });
        menu->append(*edit_button);
    } else {
        auto* repeat_button = Gtk::make_managed<Gtk::Button>("Make repeating…");
        repeat_button->signal_clicked().connect(
            [this, id, popover]() { popover->set_child(*build_repeat_config(id, popover)); });
        menu->append(*repeat_button);
    }

    // Colors apply to a whole project, so only on a top-level one.
    if (m_tree.parent_of(id) == 0) {
        auto* color_button = Gtk::make_managed<Gtk::Button>("Set color…");
        color_button->signal_clicked().connect(
            [this, id, popover]() { popover->set_child(*build_color_picker(id, popover)); });
        menu->append(*color_button);
    }

    popover->set_child(*menu);
    popover->signal_closed().connect(
        [popover]() { Glib::signal_idle().connect_once([popover]() { popover->unparent(); }); });
    popover->popup();
}

namespace {

// Sun..Sat, matching weekday_mask's bit order (tm_wday: Sunday = 0).
const char* const DAY_LABELS[7] = {"S", "M", "T", "W", "T", "F", "S"};

// One editable line in the schedule grid.
struct ScheduleRow {
    int node_id = 0;
    int depth = 0;
    int parent_index = -1;  // -1 for the root line
    int mask_before = 0;    // what it resolved to on open
    bool expanded = false;
    Gtk::Widget* line = nullptr;
    Gtk::Button* expander = nullptr;  // null when it has no children
    std::array<Gtk::ToggleButton*, 7> toggles{};
};

}  // namespace

Gtk::Widget* ProjectTreePanel::build_repeat_config(int root_id, Gtk::Popover* popover) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    // For an unmarked node the default is every day, so marking something
    // repeating is one click.
    const bool editing = m_task_attributes.has_own_repeat(root_id);
    const RepeatedTaskRow current = m_task_attributes.repeat_settings(root_id);
    const int root_mask = editing ? current.weekday_mask : 0x7F;

    // One count for the whole routine.
    auto* count_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    count_box->append(*Gtk::make_managed<Gtk::Label>("Times per day"));
    auto* count_spin = Gtk::make_managed<Gtk::SpinButton>(
        Gtk::Adjustment::create(editing ? current.count_per_day : 1, 1, 20, 1));
    count_box->append(*count_spin);
    box->append(*count_box);

    // Pre-order: the tree's reading order, and the order the writes must
    // happen in (see the apply handler).
    auto rows = std::make_shared<std::vector<ScheduleRow>>();
    std::vector<std::array<int, 3>> pending{{root_id, 0, -1}};  // node, depth, parent
    while (!pending.empty()) {
        const auto [node_id, depth, parent_index] = pending.back();
        pending.pop_back();

        const int self_index = static_cast<int>(rows->size());
        ScheduleRow row;
        row.node_id = node_id;
        row.depth = depth;
        row.parent_index = parent_index;
        rows->push_back(row);

        const auto children = m_tree.children_of(node_id);
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            pending.push_back({*it, depth + 1, self_index});
        }
    }

    // Collapsed by default, except where a subtree holds a schedule set by
    // hand: writing a mask clears every override beneath it, so a hidden
    // override could be wiped by a gesture two levels up. Reverse pre-order
    // carries the answer up in one pass.
    for (int i = static_cast<int>(rows->size()) - 1; i > 0; --i) {
        auto& row = (*rows)[i];
        if (m_task_attributes.has_own_repeat(row.node_id) || row.expanded) {
            (*rows)[row.parent_index].expanded = true;
        }
    }
    (*rows)[0].expanded = true;

    auto* grid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    for (auto& row : *rows) {
        if (row.node_id == root_id) {
            row.mask_before = root_mask;
        } else {
            const RepeatedTaskRow governing = m_task_attributes.governing_repeat(row.node_id);
            row.mask_before = (governing.node_id != -1) ? governing.weekday_mask : root_mask;
        }

        auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        line->set_margin_start(row.depth * 14);
        row.line = line;

        // A leaf gets a spacer the expander's width, so titles on a level
        // still start at the same x.
        if (!m_tree.children_of(row.node_id).empty()) {
            auto* expander = Gtk::make_managed<Gtk::Button>();
            expander->set_icon_name(row.expanded ? "pan-down-symbolic" : "pan-end-symbolic");
            expander->add_css_class("flat");
            expander->set_has_frame(false);
            line->append(*expander);
            row.expander = expander;
        } else {
            auto* spacer = Gtk::make_managed<Gtk::Box>();
            spacer->set_size_request(24, -1);
            line->append(*spacer);
        }

        auto* label = Gtk::make_managed<Gtk::Label>(m_tree.display_title(row.node_id));
        label->set_xalign(0.0);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_margin_end(8);

        // Dimmed where the schedule is inherited rather than set here.
        if (row.node_id != root_id && !m_task_attributes.has_own_repeat(row.node_id)) {
            label->add_css_class("dim-label");
        }
        line->append(*label);

        for (int i = 0; i < 7; ++i) {
            auto* btn = Gtk::make_managed<Gtk::ToggleButton>(DAY_LABELS[i]);
            btn->set_active((row.mask_before & (1 << i)) != 0);

            // A checked ToggleButton is only a slightly different tint,
            // hard to read across seven in a row.
            btn->signal_toggled().connect([btn]() {
                if (btn->get_active()) {
                    btn->add_css_class("suggested-action");
                } else {
                    btn->remove_css_class("suggested-action");
                }
            });
            if (btn->get_active()) btn->add_css_class("suggested-action");

            line->append(*btn);
            row.toggles[i] = btn;
        }
        grid->append(*line);
    }

    // A line shows only if every ancestor between it and the root is open.
    auto update_visibility = std::make_shared<std::function<void()>>();
    *update_visibility = [rows]() {
        std::vector<bool> visible(rows->size(), false);
        for (size_t i = 0; i < rows->size(); ++i) {
            auto& row = (*rows)[i];
            visible[i] = (row.parent_index < 0) ||
                         (visible[row.parent_index] && (*rows)[row.parent_index].expanded);
            row.line->set_visible(visible[i]);
            if (row.expander) {
                row.expander->set_icon_name(row.expanded ? "pan-down-symbolic"
                                                         : "pan-end-symbolic");
            }
        }
    };

    for (size_t i = 0; i < rows->size(); ++i) {
        auto* expander = (*rows)[i].expander;
        if (!expander) continue;
        expander->signal_clicked().connect([rows, i, update_visibility]() {
            (*rows)[i].expanded = !(*rows)[i].expanded;
            (*update_visibility)();
        });
    }
    (*update_visibility)();

    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(*grid);
    scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller->set_propagate_natural_height(true);
    scroller->set_propagate_natural_width(true);
    scroller->set_max_content_height(320);
    box->append(*scroller);

    auto* apply_button = Gtk::make_managed<Gtk::Button>(editing ? "Update" : "Repeat");
    apply_button->signal_clicked().connect([this, root_id, rows, count_spin, popover]() {
        // Read before deferring: these widgets live inside the popover.
        std::vector<std::pair<int, int>> edits;  // node id, new mask
        for (const auto& row : *rows) {
            int mask = 0;
            for (int i = 0; i < 7; ++i) {
                if (row.toggles[i]->get_active()) mask |= (1 << i);
            }
            // The root always writes (its count may have moved); everything
            // else only when changed, so untouched rows stay inherited.
            if (row.node_id == root_id || mask != row.mask_before) {
                edits.push_back({row.node_id, mask});
            }
        }
        const int count = count_spin->get_value_as_int();
        popover->popdown();

        // TRAP: edits are in pre-order and must be applied in that order.
        // Writing a node's mask clears every override beneath it, so an
        // ancestor applied after its descendant would wipe that edit.
        Glib::signal_idle().connect_once([this, edits, count]() {
            for (const auto& [node_id, mask] : edits) {
                m_task_attributes.apply_repeat(node_id, mask, count);
            }
        });
    });
    box->append(*apply_button);

    return box;
}

Gtk::Widget* ProjectTreePanel::build_date_editor(int id, Gtk::Popover* popover) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    const bool editing = m_task_attributes.has_date(id);
    const TaskDateRow current = m_task_attributes.date_settings(id);

    auto* calendar = Gtk::make_managed<Gtk::Calendar>();
    if (editing && current.date.size() == 10) {
        calendar->select_day(Glib::DateTime::create_local(
            std::stoi(current.date.substr(0, 4)), std::stoi(current.date.substr(5, 2)),
            std::stoi(current.date.substr(8, 2)), 0, 0, 0));
    }
    box->append(*calendar);

    auto* times_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* start_entry = Gtk::make_managed<Gtk::Entry>();
    auto* end_entry = Gtk::make_managed<Gtk::Entry>();

    start_entry->set_placeholder_text("--:--");
    end_entry->set_placeholder_text("--:--");
    start_entry->set_max_width_chars(6);
    end_entry->set_max_width_chars(6);

    if (editing) {
        start_entry->set_text(clock_util::format_hhmm(current.time_start));
        end_entry->set_text(clock_util::format_hhmm(current.time_end));
    } else {
        auto now = Glib::DateTime::create_now_local();
        start_entry->set_text(clock_util::format_hhmm(now.get_hour() * 60 + now.get_minute()));
    }

    times_box->append(*start_entry);
    times_box->append(*Gtk::make_managed<Gtk::Label>("to"));
    times_box->append(*end_entry);
    box->append(*times_box);

    auto* apply_button = Gtk::make_managed<Gtk::Button>("Set");
    apply_button->signal_clicked().connect([this, id, calendar, start_entry, end_entry, popover]() {
        std::string date = calendar->get_date().format("%Y-%m-%d");

        int start = clock_util::parse_hhmm(start_entry->get_text());
        int end = clock_util::parse_hhmm(end_entry->get_text());
        if (start == TaskDateRow::NO_TIME) end = TaskDateRow::NO_TIME;

        popover->popdown();
        Glib::signal_idle().connect_once(
            [this, id, date, start, end]() { m_task_attributes.set_date(id, date, start, end); });
    });
    box->append(*apply_button);

    return box;
}

Gtk::Widget* ProjectTreePanel::build_color_picker(int id, Gtk::Popover* popover) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    constexpr int SWATCHES_PER_ROW = 4;
    auto* swatch_grid = Gtk::make_managed<Gtk::Grid>();
    swatch_grid->set_row_spacing(4);
    swatch_grid->set_column_spacing(4);

    int index = 0;
    for (const char* hex : style::project_swatches()) {
        auto* dot = Gtk::make_managed<Gtk::Label>();
        dot->set_markup(std::string("<span foreground='") + hex + "'>●</span>");

        auto* swatch_button = Gtk::make_managed<Gtk::Button>();
        swatch_button->set_child(*dot);

        std::string hex_str = hex;
        swatch_button->signal_clicked().connect([this, id, hex_str, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once(
                [this, id, hex_str]() { m_task_attributes.set_project_color(id, hex_str); });
        });

        swatch_grid->attach(*swatch_button, index % SWATCHES_PER_ROW, index / SWATCHES_PER_ROW);
        ++index;
    }
    box->append(*swatch_grid);

    auto* clear_button = Gtk::make_managed<Gtk::Button>("Clear color");
    clear_button->signal_clicked().connect([this, id, popover]() {
        popover->popdown();
        Glib::signal_idle().connect_once(
            [this, id]() { m_task_attributes.clear_project_color(id); });
    });
    box->append(*clear_button);

    return box;
}
