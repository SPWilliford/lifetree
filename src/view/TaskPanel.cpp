#include "view/TaskPanel.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <gdkmm/display.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/popover.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/Scheduler.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "view/NodeItem.hpp"

namespace {
// Ours first, then the theme's names as a safety net. The bundled one
// is compiled into the binary and always resolves, so the fallbacks
// only matter if the resource is ever unlinked by a build change —
// which is exactly when a broken-image glyph would be baffling.
//
// Worth keeping the list even so: symbolic names come and go between
// adwaita-icon-theme releases (Fedora 43 has neither funnel-symbolic
// nor view-filter-symbolic, legacy package installed or not), and a
// name the theme lacks draws as a broken image rather than falling
// back on its own.
std::string first_available_icon(const std::vector<std::string>& names) {
    auto display = Gdk::Display::get_default();
    if (display) {
        auto theme = Gtk::IconTheme::get_for_display(display);
        if (theme) {
            for (const auto& name : names) {
                if (theme->has_icon(name)) return name;
            }
        }
    }
    return names.back();
}

// The project's color is what says which project a row belongs to, now
// that the path itself is gone from the row.
void set_colored_text(Gtk::Label& label, const std::string& text, const std::string& color) {
    if (!color.empty() && !text.empty()) {
        label.set_markup("<span foreground='" + color + "'>" + Glib::Markup::escape_text(text) +
                         "</span>");
    } else {
        label.set_text(text);
    }
}
}  // namespace

TaskPanel::TaskPanel(TreeController& projects, TaskAttributes& task_attributes, Priority& priority)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12),
      m_projects(projects),
      m_task_attributes(task_attributes),
      m_priority(priority) {
    initialize_layout();

    // Deferred to idle rather than run synchronously — avoids rebuilding
    // the store while GTK is still partway through delivering whatever
    // event triggered the change.
    m_projects.connect_changed([this]() { m_refresh.request(); });

    // A full rebuild, not the in-place restyle a tree does for this signal:
    // repeat marks and completions change this list's MEMBERSHIP without
    // touching the tree, so no tree signal fires and this is the only
    // notice. Weights change the order rather than the membership, but a
    // re-sort still rebuilds the store. Cheap either way — a flat list has
    // no expand state to lose.
    m_priority.connect_changed([this]() { m_refresh.request(); });

    m_task_attributes.connect_changed([this]() { m_refresh.request(); });

    refresh();
}

void TaskPanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    m_store = Gio::ListStore<Glib::Object>::create();

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &TaskPanel::on_setup));
    factory->signal_bind().connect(sigc::mem_fun(*this, &TaskPanel::on_bind));
    m_list_view.set_factory(factory);
    m_list_view.set_model(Gtk::SingleSelection::create(m_store));
    m_list_view.set_hexpand(true);
    m_list_view.set_vexpand(true);

    // The panel's first header: the narrowed-to project on the left, the
    // control that narrowed it on the right. Two jobs, so two positions —
    // the title names what this list IS, which is a fact about the list
    // rather than part of the button.
    //
    // Left for the title because that's where the rows start, so it reads
    // as the column's heading. Right for the button for the same reason
    // the life tree's weights toggle is there: a control on the left edge
    // competes with the titles the eye scans down.
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    header->set_margin_start(6);
    header->set_margin_end(6);

    // Empty when unfiltered. Every project IS the list — saying so would be
    // a word on screen that earns nothing, and the row holds its height
    // either way, so nothing shifts when the text appears.
    //
    // Bold and uncoloured: the rows below already carry the project's
    // colour, and colouring the heading too would repeat a signal instead
    // of adding one. Weight alone reads as structure rather than as one
    // more coloured item.
    m_filter_label.set_ellipsize(Pango::EllipsizeMode::END);
    m_filter_label.set_hexpand(true);
    m_filter_label.set_xalign(0.0);
    m_filter_label.add_css_class("heading");
    header->append(m_filter_label);

    m_filter_icon.set_from_icon_name(first_available_icon(
        {"lifetree-filter-symbolic", "funnel-symbolic", "view-filter-symbolic"}));

    m_filter_button.set_child(m_filter_icon);
    m_filter_button.set_has_frame(false);
    m_filter_button.set_popover(m_filter_popover);
    m_filter_button.set_tooltip_text("Filter by project");

    header->append(m_filter_button);
    append(*header);

    auto* backlog_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    backlog_scroll->set_child(m_list_view);
    backlog_scroll->set_hexpand(true);
    backlog_scroll->set_vexpand(true);
    // NEVER horizontally, or the title's ellipsize never fires: an
    // AUTOMATIC scroller offers to scroll instead, which lets the list take
    // its natural width and a long title run past the panel's edge.
    backlog_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    backlog_scroll->set_min_content_width(420);
    append(*backlog_scroll);
}

void TaskPanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->set_margin(6);

    // The title alone. The ancestor path used to lead the row, and being
    // variable-width it meant no two titles began at the same place — the
    // list had no left edge to scan down. It's the row's tooltip now; which
    // project a row belongs to is already carried by its color.
    //
    // Plain, read-only label — this panel is a mirror of the tree, not
    // an editing surface. Double-click sends the task to the schedule
    // instead of starting an edit.
    auto* title_label = Gtk::make_managed<Gtk::Label>();
    title_label->set_hexpand(true);

    // FILL, with xalign doing the left-aligning: ellipsizing needs an
    // allocation to measure against, and halign START would shrink the label
    // to its text so there'd be nothing to truncate. Without this one long
    // task makes the whole list demand more width than the pane has.
    title_label->set_halign(Gtk::Align::FILL);
    title_label->set_xalign(0.0);
    title_label->set_ellipsize(Pango::EllipsizeMode::END);

    box->append(*title_label);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this, item](int n_press, double, double) {
        if (n_press == 2) {
            auto obj = std::dynamic_pointer_cast<NodeItem>(item->get_item());
            if (obj) m_task_chosen.emit(obj->node_id());
        }
    });
    box->add_controller(click);

    item->set_child(*box);
}

void TaskPanel::on_bind(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto obj = std::dynamic_pointer_cast<NodeItem>(item->get_item());
    if (!obj) return;

    auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
    if (!box) return;

    auto* title_label = dynamic_cast<Gtk::Label*>(box->get_first_child());
    if (!title_label) return;

    const int id = obj->node_id();
    TaskSnapshot snap = m_task_attributes.snapshot(id);

    // "Pushups  1/3" rather than three identical rows. The old model made
    // three nodes and they competed for slots against three other projects;
    // one counted row says more and costs the rotation nothing.
    std::string text = snap.title;
    const int target = m_task_attributes.target_count(id);
    if (target > 1) {
        text += "  " + std::to_string(m_task_attributes.completions_today(id)) + "/" +
                std::to_string(target);
    }

    set_colored_text(*title_label, text, snap.color);

    // On the box, not the label: the box fills the row, so the whole row
    // answers. Carries the title too, which is what makes it readable when
    // a long one has been ellipsized. Same "path - title" shape the staged
    // dock shows — that one keeps it on screen, since a single row with the
    // panel's whole width is a statement of what you're working on rather
    // than something to scan.
    box->set_tooltip_text(snap.path.empty() ? snap.title : snap.path + " - " + snap.title);
}

void TaskPanel::set_filter(int root_id) {
    m_filter_root_id = root_id;
    update_filter_button();
    refresh();
}

void TaskPanel::update_filter_button() {
    const bool filtering = m_filter_root_id > 0 && m_projects.contains(m_filter_root_id);

    // set_text, not set_visible: the label keeps its place in the layout so
    // the button can't drift sideways as the title comes and goes.
    m_filter_label.set_text(filtering ? m_projects.display_title(m_filter_root_id) : "");
}

void TaskPanel::rebuild_filter_menu() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);

    // A Button centres its label, which makes a column of them read as
    // ragged; giving it the label directly is what lets it be left-aligned.
    auto add_entry = [this, box](int root_id, const std::string& text, const std::string& color) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        set_colored_text(*label, text, color);
        label->set_xalign(0.0);
        label->set_hexpand(true);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_max_width_chars(24);

        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*label);
        button->set_has_frame(false);
        if (root_id == m_filter_root_id) button->add_css_class("suggested-action");

        button->signal_clicked().connect([this, root_id]() {
            m_filter_popover.popdown();
            // Deferred: applying the filter rebuilds this menu, and these
            // buttons mustn't be destroyed while one of them is mid-click.
            Glib::signal_idle().connect_once([this, root_id]() { set_filter(root_id); });
        });
        box->append(*button);
    };

    add_entry(-1, "All projects", "");

    // Tree order, and every project whether or not it has anything up
    // today. A menu that only listed projects with work available would
    // reorder itself as the day went on, and "nothing in here right now" is
    // a real answer worth being able to reach.
    for (int root_id : m_projects.children_of(0)) {
        add_entry(root_id, m_projects.display_title(root_id), m_task_attributes.get_color(root_id));
    }

    m_filter_popover.set_child(*box);
}

void TaskPanel::refresh() {
    // The filtered project may have been deleted since it was chosen, which
    // would otherwise leave the list permanently empty with a button naming
    // something that no longer exists.
    if (m_filter_root_id > 0 && !m_projects.contains(m_filter_root_id)) {
        m_filter_root_id = -1;
    }
    rebuild_filter_menu();
    update_filter_button();

    m_store->remove_all();

    std::vector<scheduler::Candidate> candidates;
    for (int id : m_task_attributes.eligible_today()) {
        // project_root_of, not the task's parent: filtering to a project
        // means its whole subtree, however deeply the work is broken down.
        const int root_id = m_task_attributes.project_root_of(id);
        if (m_filter_root_id > 0 && root_id != m_filter_root_id) continue;
        candidates.push_back({id, root_id});
    }

    // eligible_today walks an unordered set. The scheduler is deterministic
    // only as far as its input is, so sort before handing it over.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.project_id != b.project_id) return a.project_id < b.project_id;
        return a.task_id < b.task_id;
    });

    // Interleaved rather than sorted: sorting groups every project's tasks
    // into one block, so the top of the list is always the same project and
    // a fragmented goal is never reached at all.
    for (int id : scheduler::interleave(candidates, m_priority.project_priorities())) {
        m_store->append(NodeItem::create(id));
    }
}
