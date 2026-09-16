#include "view/TaskPanel.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <gdkmm/display.h>
#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/singleselection.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/Scheduler.hpp"
#include "core/TaskAttributes.hpp"
#include "core/Tree.hpp"
#include "core/TreeController.hpp"
#include "view/NodeItem.hpp"
#include "view/Style.hpp"

namespace {

// The bundled icon first, then theme names in case the resource is ever
// unlinked. A name the theme lacks draws as a broken image.
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

}  // namespace

TaskPanel::TaskPanel(TreeController& projects, TaskAttributes& task_attributes, Priority& priority)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12),
      m_projects(projects),
      m_task_attributes(task_attributes),
      m_priority(priority) {
    initialize_layout();

    // All three change membership or order; a flat list has no state to
    // lose, so every one is a full rebuild.
    m_projects.connect_changed([this]() { m_refresh.request(); });
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

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    header->set_margin_start(6);
    header->set_margin_end(6);

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

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_child(m_list_view);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);
    // TRAP: NEVER horizontally, or the title's ellipsize never fires — an
    // AUTOMATIC scroller offers to scroll instead.
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_width(420);
    append(*scroll);
}

void TaskPanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->set_margin(6);

    auto* title_label = Gtk::make_managed<Gtk::Label>();
    title_label->set_hexpand(true);
    // TRAP: FILL with xalign, not halign START — START shrinks the label to
    // its text and leaves nothing to ellipsize.
    title_label->set_halign(Gtk::Align::FILL);
    title_label->set_xalign(0.0);
    title_label->set_ellipsize(Pango::EllipsizeMode::END);
    box->append(*title_label);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this, item](int n_press, double, double) {
        if (n_press != 2) return;
        auto obj = std::dynamic_pointer_cast<NodeItem>(item->get_item());
        if (obj) m_task_chosen.emit(obj->node_id());
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
    const TaskSnapshot snap = m_task_attributes.snapshot(id);

    // "Pushups  1/3" for a task due more than once a day.
    std::string text = snap.title;
    const int target = m_task_attributes.target_count(id);
    if (target > 1) {
        text += "  " + std::to_string(m_task_attributes.completions_today(id)) + "/" +
                std::to_string(target);
    }
    style::set_colored_text(*title_label, text, snap.color);

    // The path is the tooltip; project color carries it in the row.
    box->set_tooltip_text(snap.path.empty() ? snap.title : snap.path + " - " + snap.title);
}

void TaskPanel::set_filter(int root_id) {
    m_filter_root_id = root_id;
    update_filter_button();
    refresh();
}

void TaskPanel::update_filter_button() {
    const bool filtering = m_filter_root_id > 0 && m_projects.contains(m_filter_root_id);
    m_filter_label.set_text(filtering ? m_projects.display_title(m_filter_root_id) : "");
}

void TaskPanel::rebuild_filter_menu() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);

    auto add_entry = [this, box](int root_id, const std::string& text, const std::string& color) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        style::set_colored_text(*label, text, color);
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
            // Deferred: applying the filter rebuilds this menu.
            Glib::signal_idle().connect_once([this, root_id]() { set_filter(root_id); });
        });
        box->append(*button);
    };

    add_entry(-1, "All projects", "");

    // Tree order, every project, so the menu doesn't reorder itself.
    for (int root_id : m_projects.children_of(Tree::ROOT_ID)) {
        add_entry(root_id, m_projects.display_title(root_id), m_task_attributes.get_color(root_id));
    }

    m_filter_popover.set_child(*box);
}

void TaskPanel::refresh() {
    if (m_filter_root_id > 0 && !m_projects.contains(m_filter_root_id)) {
        m_filter_root_id = -1;
    }
    rebuild_filter_menu();
    update_filter_button();

    m_store->remove_all();

    std::vector<scheduler::Candidate> candidates;
    for (int id : m_task_attributes.eligible_today()) {
        const int root_id = m_task_attributes.project_root_of(id);
        if (m_filter_root_id > 0 && root_id != m_filter_root_id) continue;
        candidates.push_back({id, root_id});
    }

    // eligible_today is an unordered set; the scheduler is only as
    // deterministic as its input.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.project_id != b.project_id) return a.project_id < b.project_id;
        return a.task_id < b.task_id;
    });

    for (int id : scheduler::interleave(candidates, m_priority.project_priorities())) {
        m_store->append(NodeItem::create(id));
    }
}
