#include "view/ProjectsPage.hpp"

#include <algorithm>

#include <glibmm/main.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinbutton.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/Tree.hpp"
#include "core/TreeController.hpp"

namespace {

constexpr int SECTION_MIN_HEIGHT = 320;
constexpr int LIST_MIN_WIDTH = 170;

Gtk::Label* dim_label(const std::string& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_halign(Gtk::Align::START);
    label->set_wrap(true);
    return label;
}

}  // namespace

ProjectsPage::ProjectsPage(TreeController& life, TreeController& projects, Priority& priority)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 12),
      m_life(life),
      m_projects(projects),
      m_priority(priority) {
    set_hexpand(true);
    set_vexpand(true);

    build_section(m_goal_section, m_goal_columns, m_goal_list_scroll, m_goal_links_scroll,
                  m_goal_list, m_goal_links, "By goal");

    auto* divider = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    divider->set_margin_start(4);
    divider->set_margin_end(4);
    append(*divider);

    build_section(m_project_section, m_project_columns, m_project_list_scroll,
                  m_project_links_scroll, m_project_list, m_project_links, "By project");

    m_life.connect_changed([this]() { m_refresh.request(); });
    m_projects.connect_changed([this]() { m_refresh.request(); });
    m_priority.connect_changed([this]() { m_refresh.request(); });

    rebuild();
}

void ProjectsPage::build_section(Gtk::Box& section, Gtk::Box& columns,
                                 Gtk::ScrolledWindow& list_scroll,
                                 Gtk::ScrolledWindow& links_scroll, Gtk::Grid& list,
                                 Gtk::Grid& links, const std::string& title) {
    auto* heading = Gtk::make_managed<Gtk::Label>();
    heading->set_text(title);
    heading->add_css_class("heading");
    heading->set_halign(Gtk::Align::START);
    section.append(*heading);

    for (auto* grid : {&list, &links}) {
        grid->set_row_spacing(2);
        grid->set_column_spacing(12);
    }

    list_scroll.set_child(list);
    links_scroll.set_child(links);

    list_scroll.set_hexpand(false);
    list_scroll.set_min_content_width(LIST_MIN_WIDTH);
    list_scroll.set_size_request(LIST_MIN_WIDTH, -1);
    links_scroll.set_hexpand(true);

    for (auto* scroll : {&list_scroll, &links_scroll}) {
        scroll->set_vexpand(true);
        scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        scroll->set_min_content_height(SECTION_MIN_HEIGHT);
    }

    columns.set_hexpand(true);
    columns.set_vexpand(true);
    columns.append(list_scroll);
    columns.append(links_scroll);
    section.append(columns);

    section.set_hexpand(true);
    section.set_vexpand(true);
    append(section);
}

void ProjectsPage::validate_selection() {
    // Falls back to the highest-ranked item, not the first created.
    const auto leaves = m_priority.ranked_leaves();
    if (std::find(leaves.begin(), leaves.end(), m_goal_id) == leaves.end()) {
        m_goal_id = leaves.empty() ? -1 : leaves.front();
    }

    const auto projects = m_projects.children_of(Tree::ROOT_ID);
    if (std::find(projects.begin(), projects.end(), m_project_id) == projects.end()) {
        m_project_id = projects.empty() ? -1 : projects.front();
    }
}

void ProjectsPage::rebuild() {
    m_populating = true;

    for (auto* grid : {&m_goal_list, &m_goal_links, &m_project_list, &m_project_links}) {
        while (auto* child = grid->get_first_child()) grid->remove(*child);
    }

    validate_selection();

    const auto leaves = m_priority.ranked_leaves();
    const auto projects = m_projects.children_of(Tree::ROOT_ID);

    // Left half: a leaf, and each project's share of delivering it.
    int row = 0;
    for (int leaf : leaves) {
        append_selector_row(m_goal_list, row++, Axis::GOAL, leaf, m_life.display_title(leaf));
    }

    if (m_goal_id < 0) {
        append_empty_note(m_goal_links, "Add a leaf to the life tree");
    } else if (projects.empty()) {
        append_empty_note(m_goal_links, "No projects yet");
    } else {
        row = 0;
        for (int project : projects) {
            append_link_row(m_goal_links, row++, Axis::GOAL, project, m_goal_id,
                            m_projects.display_title(project));
        }
    }

    // Right half: a project, and how much of it is about each leaf.
    row = 0;
    for (int project : projects) {
        append_selector_row(m_project_list, row++, Axis::PROJECT, project,
                            m_projects.display_title(project));
    }

    if (m_project_id < 0) {
        append_empty_note(m_project_links, "Add a project");
    } else if (leaves.empty()) {
        append_empty_note(m_project_links, "Add a leaf to the life tree");
    } else {
        row = 0;
        for (int leaf : leaves) {
            append_link_row(m_project_links, row++, Axis::PROJECT, m_project_id, leaf,
                            m_life.display_title(leaf));
        }
    }

    m_populating = false;
}

void ProjectsPage::append_empty_note(Gtk::Grid& grid, const std::string& text) {
    grid.attach(*dim_label(text), 0, 0);
}

void ProjectsPage::append_selector_row(Gtk::Grid& grid, int row, Axis axis, int id,
                                       const std::string& title) {
    auto* button = Gtk::make_managed<Gtk::Button>(title);
    button->set_halign(Gtk::Align::FILL);
    button->set_hexpand(true);
    button->set_has_frame(false);

    if (auto* label = dynamic_cast<Gtk::Label*>(button->get_child())) {
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::END);
    }

    const int selected = (axis == Axis::GOAL) ? m_goal_id : m_project_id;
    if (selected == id) {
        button->set_has_frame(true);
        button->add_css_class("suggested-action");
    } else if (axis == Axis::GOAL && !m_priority.projects_for(id).empty()) {
        // Not on the selected row: suggested-action owns its text color.
        button->add_css_class("leaf-served");
    }

    // No toggle-off: a click always moves the selection.
    button->signal_clicked().connect([this, axis, id]() {
        if (axis == Axis::GOAL) {
            if (m_goal_id == id) return;
            m_goal_id = id;
        } else {
            if (m_project_id == id) return;
            m_project_id = id;
        }
        m_refresh.request();
    });

    grid.attach(*button, 0, row);
}

void ProjectsPage::append_link_row(Gtk::Grid& grid, int row, Axis axis, int project_id, int leaf_id,
                                   const std::string& title) {
    const bool linked = m_priority.has_link(project_id, leaf_id);

    auto* check = Gtk::make_managed<Gtk::CheckButton>();
    check->set_active(linked);
    grid.attach(*check, 0, row);

    auto* label = Gtk::make_managed<Gtk::Label>(title);
    label->set_halign(Gtk::Align::START);
    label->set_max_width_chars(34);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    grid.attach(*label, 1, row);

    const bool goal_axis = (axis == Axis::GOAL);
    const double value = goal_axis ? m_priority.goal_share(project_id, leaf_id)
                                   : m_priority.project_share(project_id, leaf_id);

    auto* spin =
        Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(value, 0.0, 100.0, 1.0, 5.0));
    spin->set_digits(0);
    spin->set_width_chars(3);
    spin->set_sensitive(linked);
    if (!linked) spin->set_text("");
    grid.attach(*spin, 2, row);

    // Both handlers defer to an idle: the change emits, which rebuilds these
    // grids, including the widget currently inside its own handler.
    check->signal_toggled().connect([this, check, project_id, leaf_id]() {
        if (m_populating) return;
        const bool now_linked = check->get_active();
        Glib::signal_idle().connect_once([this, now_linked, project_id, leaf_id]() {
            if (now_linked) {
                m_priority.set_link(project_id, leaf_id);
            } else {
                m_priority.clear_link(project_id, leaf_id);
            }
        });
    });

    spin->signal_value_changed().connect([this, spin, project_id, leaf_id, goal_axis]() {
        if (m_populating) return;
        const double v = spin->get_value();
        Glib::signal_idle().connect_once([this, v, project_id, leaf_id, goal_axis]() {
            if (goal_axis) {
                m_priority.set_goal_share(project_id, leaf_id, v);
            } else {
                m_priority.set_project_share(project_id, leaf_id, v);
            }
        });
    });
}
