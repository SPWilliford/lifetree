#include "view/ProjectTreePanel.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glibmm/datetime.h>
#include <glibmm/main.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/calendar.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/togglebutton.h>
#include <pangomm/layout.h>

#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "view/CardRow.hpp"
#include "view/Style.hpp"

namespace {

// The trailing "+" row. It lives in the store beside real nodes so it can
// sit exactly where the next project will appear, which a button below the
// list can't do once the list scrolls.
constexpr int NEW_PROJECT_ROW = -1;

bool is_new_project_row(int id) {
    return id == NEW_PROJECT_ROW;
}

// Minutes-since-midnight is stored; HH:MM is typed. Lenient on input:
// blank or unparseable means "no time", since that's the common answer.
int hhmm_to_minutes(const Glib::ustring& text) {
    int hours = 0, minutes = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &hours, &minutes) != 2) return TaskDateRow::NO_TIME;
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return TaskDateRow::NO_TIME;
    return hours * 60 + minutes;
}

std::string minutes_to_hhmm(int minutes) {
    if (minutes < 0 || minutes >= 24 * 60) return "";
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

}  // namespace

ProjectTreePanel::ProjectTreePanel(TreeController& projects, TaskAttributes& task_attributes)
    : TreePanel(projects), m_task_attributes(task_attributes) {
    build();
}

void ProjectTreePanel::seed_root_store() {
    for (int id : m_tree.children_of(0)) {
        m_root_store->append(NodeItem::create(id));
    }
    m_root_store->append(NodeItem::create(NEW_PROJECT_ROW));
}

void ProjectTreePanel::connect_sources() {
    // Colors, repeat marks and completions change how a row looks without
    // touching the tree, so its own signal never fires for them.
    m_task_attributes.connect_changed([this]() { m_refresh.request(); });
}

void ProjectTreePanel::decorate_row(CardRow& card, int id) {
    if (is_new_project_row(id)) {
        // The icon alone. The tooltip says the rest, and the whole row is the
        // hit target — .card-row-action brightens it on hover, which is the
        // same gesture that raises the tooltip.
        card.set_icon("list-add-symbolic");
        card.set_tooltip_text("Add a new project");
        return;
    }

    // A node can be several of these at once, and Dishes — a routine done in
    // order — is all it takes. These used to be one if/else picking a
    // winner, so an ordered routine showed only that it repeated. Built up
    // instead, in a fixed order so a row's marks don't reshuffle as its
    // attributes change.
    //
    // All of them TRAIL, and so does the life tree's weight figure. A marker
    // that leads is a variable-width element in front of the title — present
    // on some rows, absent on most — so no two titles start at the same x and
    // the list loses the left edge you scan down. Same defect the ancestor
    // path had in the backlog, and the weight figure had here.
    //
    // Text symbols, not emoji. An emoji carries its own colour from the
    // font, so it can't be toned down and it can't follow the theme — the
    // orange of 🔁 shouted next to a plain title. These are ordinary
    // characters that take the label's colour, which is what made ↓ the
    // only marker that ever looked right.
    std::vector<std::string> marks;

    // Dates first: a dated task is missing from the backlog entirely, and
    // this row is the only place that can say why. Clock alone for later
    // today, grid alone for another day, both for a time on another day.
    //
    // Dropped once the date has passed, because by then it explains
    // nothing: the task is an ordinary available one, and a mark that
    // outlives what it was warning about is a mark you learn to ignore.
    // A recurring node keeps it — its time comes round again tomorrow.
    if (m_task_attributes.has_date(id) &&
        (!m_task_attributes.date_has_passed(id) || m_task_attributes.recurs(id))) {
        const bool timed = m_task_attributes.date_settings(id).time_start != TaskDateRow::NO_TIME;
        const bool is_today = m_task_attributes.date_is_today(id);
        marks.push_back(timed ? (is_today ? "◷" : "▦◷") : "▦");
    }

    // On the routine's top row only. The rows below it carry override
    // schedules, not routines of their own, and a glyph on each would read
    // as several separate repeats.
    if (m_task_attributes.is_repeat_root(id)) marks.push_back("↻");

    if (m_task_attributes.is_sequential(id)) marks.push_back("↓");

    // Spaced, because two marks butted together read as one wider glyph.
    std::string text;
    for (const auto& mark : marks) {
        if (!text.empty()) text += " ";
        text += mark;
    }
    card.set_marker(text, CardRow::MarkerSide::AFTER);

    card.set_color(m_task_attributes.get_color(id));
}

void ProjectTreePanel::on_row_activated(int id) {
    if (is_new_project_row(id)) add_project();
}

void ProjectTreePanel::add_project() {
    int new_id = m_tree.add(0, "");
    if (new_id == -1) return;

    // Inserted before the trailing "+" so that row stays last.
    const unsigned int n = m_root_store->get_n_items();
    m_root_store->insert(n > 0 ? n - 1 : 0, NodeItem::create(new_id));

    begin_edit_on(new_id);
}

// ---------------------------------------------------------------------
// Row menu
// ---------------------------------------------------------------------

void ProjectTreePanel::extend_row_menu(Gtk::Box& menu, Gtk::Popover* popover, int id) {
    if (id <= 0) return;  // the root is hidden here, so this is belt-and-braces

    // Both directions of container/action cascade, so the
    // containers-above-actions invariant repairs itself either way and
    // there's nothing to gray out.
    const bool container = m_task_attributes.is_container(id);
    auto* kind_button =
        Gtk::make_managed<Gtk::Button>(container ? "Make action" : "Make container");
    kind_button->signal_clicked().connect([this, id, container, popover]() {
        popover->popdown();
        Glib::signal_idle().connect_once([this, id, container]() {
            if (container)
                m_task_attributes.mark_action(id);
            else
                m_task_attributes.mark_container(id);
        });
    });
    menu.append(*kind_button);

    auto* date_button = Gtk::make_managed<Gtk::Button>(
        m_task_attributes.has_date(id) ? "Change date/time…" : "Set date/time…");
    date_button->signal_clicked().connect(
        [this, id, popover]() { popover->set_child(*build_date_editor(id, popover)); });
    menu.append(*date_button);

    if (m_task_attributes.has_date(id)) {
        auto* clear_button = Gtk::make_managed<Gtk::Button>("Clear date/time");
        clear_button->signal_clicked().connect([this, id, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once([this, id]() { m_task_attributes.clear_date(id); });
        });
        menu.append(*clear_button);
    }

    // Marks the parent: its children happen in order. Any depth — a chapter
    // broken into sections is the same shape as a routine broken into steps.
    if (!m_tree.children_of(id).empty()) {
        const bool sequential = m_task_attributes.is_sequential(id);
        auto* seq_button = Gtk::make_managed<Gtk::Button>(sequential ? "Unordered" : "Do in order");
        seq_button->signal_clicked().connect([this, id, sequential, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once([this, id, sequential]() {
                if (sequential)
                    m_task_attributes.unmark_sequential(id);
                else
                    m_task_attributes.mark_sequential(id);
            });
        });
        menu.append(*seq_button);
    }

    // Three cases, and the middle one is why this isn't a simple toggle: a
    // node inside someone else's routine has a schedule but doesn't own it,
    // so it opens the routine's grid rather than starting a nested repeat.
    const int repeat_root = m_task_attributes.repeat_root_of(id);

    if (repeat_root == id) {
        auto* edit_button = Gtk::make_managed<Gtk::Button>("Edit schedule…");
        edit_button->signal_clicked().connect(
            [this, id, popover]() { popover->set_child(*build_repeat_config(id, popover)); });
        menu.append(*edit_button);

        auto* stop_button = Gtk::make_managed<Gtk::Button>("Stop repeating");
        stop_button->signal_clicked().connect([this, id, popover]() {
            popover->popdown();
            Glib::signal_idle().connect_once(
                [this, id]() { m_task_attributes.unmark_repeating(id); });
        });
        menu.append(*stop_button);
    } else if (repeat_root > 0) {
        auto* edit_button = Gtk::make_managed<Gtk::Button>("Edit schedule…");
        edit_button->signal_clicked().connect([this, repeat_root, popover]() {
            popover->set_child(*build_repeat_config(repeat_root, popover));
        });
        menu.append(*edit_button);
    } else {
        auto* repeat_button = Gtk::make_managed<Gtk::Button>("Make repeating…");
        repeat_button->signal_clicked().connect(
            [this, id, popover]() { popover->set_child(*build_repeat_config(id, popover)); });
        menu.append(*repeat_button);
    }

    // Colors apply to a whole project, so only on a top-level one.
    if (m_tree.parent_of(id) == 0) {
        auto* color_button = Gtk::make_managed<Gtk::Button>("Set color…");
        color_button->signal_clicked().connect(
            [this, id, popover]() { popover->set_child(*build_color_picker(id, popover)); });
        menu.append(*color_button);
    }
}

namespace {

// Sun..Sat, matching weekday_mask's bit order (tm_wday: Sunday = 0), so a
// button's index IS its bit and building a mask needs no reordering.
const char* const DAY_LABELS[7] = {"S", "M", "T", "W", "T", "F", "S"};

// One editable line in the schedule grid.
struct ScheduleRow {
    int node_id = 0;
    int depth = 0;
    int parent_index = -1;  // -1 for the root line
    int mask_before = 0;    // what it resolved to on open
    bool expanded = false;  // whether its children show
    Gtk::Widget* line = nullptr;
    Gtk::Button* expander = nullptr;  // null when it has no children
    std::array<Gtk::ToggleButton*, 7> toggles{};
};

}  // namespace

Gtk::Widget* ProjectTreePanel::build_repeat_config(int root_id, Gtk::Popover* popover) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    // Opens showing what's set, so this doubles as the editor. For a node
    // not yet marked, repeat_settings gives a zeroed row and the defaults
    // below apply instead — all seven days, so marking something repeating
    // is one click and it starts working without visiting this grid at all.
    const bool editing = m_task_attributes.has_own_repeat(root_id);
    const RepeatedTaskRow current = m_task_attributes.repeat_settings(root_id);
    const int root_mask = editing ? current.weekday_mask : 0x7F;

    // How many times a day it falls due. One figure for the whole routine
    // rather than one per step: three sets of pushups is a count, and a
    // routine whose steps each ran a different number of times a day isn't
    // a shape worth the column it would cost.
    auto* count_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    count_box->append(*Gtk::make_managed<Gtk::Label>("Times per day"));
    auto* count_spin = Gtk::make_managed<Gtk::SpinButton>(
        Gtk::Adjustment::create(editing ? current.count_per_day : 1, 1, 20, 1));
    count_box->append(*count_spin);
    box->append(*count_box);

    // Pre-order, which is both the tree's reading order and the order the
    // writes have to happen in — see the apply handler.
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

    // Collapsed by default — someone who breaks work down finely can have
    // twenty rows here, and the routine's own steps are what they came to
    // see. Expanded only where the subtree holds a schedule set by hand.
    //
    // That second half isn't a nicety. Writing a node's mask clears every
    // override beneath it, so a hidden one could be wiped by a gesture two
    // levels up with nothing on screen having mentioned it. Anything
    // deliberately set stays visible without being asked for.
    //
    // Reverse pre-order, so every child is visited before its parent and
    // one pass carries the answer up.
    for (int i = static_cast<int>(rows->size()) - 1; i > 0; --i) {
        auto& row = (*rows)[i];
        if (m_task_attributes.has_own_repeat(row.node_id) || row.expanded) {
            (*rows)[row.parent_index].expanded = true;
        }
    }
    (*rows)[0].expanded = true;  // the routine's own steps always show

    auto* grid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    for (auto& row : *rows) {
        // What this node resolves to right now: its own row if it has one,
        // the nearest ancestor's otherwise. The root's own value is the
        // default above when it isn't marked yet.
        if (row.node_id == root_id) {
            row.mask_before = root_mask;
        } else {
            const RepeatedTaskRow governing = m_task_attributes.governing_repeat(row.node_id);
            row.mask_before = (governing.node_id != -1) ? governing.weekday_mask : root_mask;
        }

        auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        line->set_margin_start(row.depth * 14);  // the tree's shape, kept
        row.line = line;

        // A branch gets a disclosure control; a leaf gets a spacer the same
        // width, so every title on a level still starts at the same x.
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

        // Dimmed where the schedule is inherited rather than set here, so a
        // course you've decided about and one merely following the routine
        // can be told apart at a glance.
        if (row.node_id != root_id && !m_task_attributes.has_own_repeat(row.node_id)) {
            label->add_css_class("dim-label");
        }
        line->append(*label);

        for (int i = 0; i < 7; ++i) {
            auto* btn = Gtk::make_managed<Gtk::ToggleButton>(DAY_LABELS[i]);
            btn->set_active((row.mask_before & (1 << i)) != 0);

            // A checked ToggleButton is only a slightly different surface
            // tint, which is hard to read across seven of them in a row.
            // The accent is the platform's own, so it tracks the theme
            // rather than being a fixed value that has to work on both —
            // and it isn't green, which already means "a project serves
            // this goal" everywhere else in the app.
            btn->signal_toggled().connect([btn]() {
                if (btn->get_active())
                    btn->add_css_class("suggested-action");
                else
                    btn->remove_css_class("suggested-action");
            });
            if (btn->get_active()) btn->add_css_class("suggested-action");

            line->append(*btn);
            row.toggles[i] = btn;
        }
        grid->append(*line);
    }

    // A line shows only if every ancestor between it and the root is open.
    // Recomputed wholesale rather than incrementally: the list is short, and
    // one rule in one place can't disagree with itself.
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

    // A routine with one step is one line and needs no scroller; sixteen
    // problems marked repeating would otherwise run off the screen.
    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(*grid);
    scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller->set_propagate_natural_height(true);
    scroller->set_propagate_natural_width(true);
    scroller->set_max_content_height(320);
    box->append(*scroller);

    auto* apply_button = Gtk::make_managed<Gtk::Button>(editing ? "Update" : "Repeat");
    apply_button->signal_clicked().connect([this, root_id, rows, count_spin, popover]() {
        // Read before deferring — these widgets live inside the popover
        // and mustn't be touched once it's closing.
        std::vector<std::pair<int, int>> edits;  // node id, new mask
        for (const auto& row : *rows) {
            int mask = 0;
            for (int i = 0; i < 7; ++i) {
                if (row.toggles[i]->get_active()) mask |= (1 << i);
            }
            // The root always writes: its count may have moved even
            // when its days haven't. Everything else writes only when
            // it changed, so untouched rows stay inherited.
            if (row.node_id == root_id || mask != row.mask_before) {
                edits.push_back({row.node_id, mask});
            }
        }
        const int count = count_spin->get_value_as_int();
        popover->popdown();

        Glib::signal_idle().connect_once([this, edits, count]() {
            // TRAP: pre-order, and it has to stay that way. Writing a
            // node's mask clears every override beneath it, so an
            // ancestor applied after its descendant would wipe the edit
            // just made. Parents first means the narrowed course
            // survives the sweep from the branch above it.
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
    // Glib::DateTime months are 1-based and match the stored text, so the
    // substrings go in unadjusted. A new entry gets Gtk::Calendar's default.
    if (editing && current.date.size() == 10) {
        calendar->select_day(Glib::DateTime::create_local(
            std::stoi(current.date.substr(0, 4)), std::stoi(current.date.substr(5, 2)),
            std::stoi(current.date.substr(8, 2)), 0, 0, 0));
    }
    box->append(*calendar);

    auto* times_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* start_entry = Gtk::make_managed<Gtk::Entry>();
    auto* end_entry = Gtk::make_managed<Gtk::Entry>();

    // "--:--" rather than a sample time: a plausible placeholder reads as a
    // value already filled in, and blank is a meaningful answer here.
    start_entry->set_placeholder_text("--:--");
    end_entry->set_placeholder_text("--:--");
    start_entry->set_max_width_chars(6);
    end_entry->set_max_width_chars(6);

    if (editing) {
        // Exactly what's stored: prefilling would quietly add a time to a
        // date deliberately left without one.
        start_entry->set_text(minutes_to_hhmm(current.time_start));
        end_entry->set_text(minutes_to_hhmm(current.time_end));
    } else {
        auto now = Glib::DateTime::create_now_local();
        start_entry->set_text(minutes_to_hhmm(now.get_hour() * 60 + now.get_minute()));
    }

    times_box->append(*start_entry);
    times_box->append(*Gtk::make_managed<Gtk::Label>("to"));
    times_box->append(*end_entry);
    box->append(*times_box);

    auto* apply_button = Gtk::make_managed<Gtk::Button>("Set");
    apply_button->signal_clicked().connect([this, id, calendar, start_entry, end_entry, popover]() {
        std::string date = calendar->get_date().format("%Y-%m-%d");

        int start = hhmm_to_minutes(start_entry->get_text());
        int end = hhmm_to_minutes(end_entry->get_text());
        // An end without a start has nothing to anchor it.
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

    // A dot colored by Pango markup rather than an emoji. Emoji carry their
    // own color and needed no styling, which is why they were used — but
    // Unicode has only nine colored circles, so the picker was capped at the
    // glyphs rather than at anything about the colors.
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
