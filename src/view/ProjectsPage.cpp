#include "view/ProjectsPage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/separator.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/Requirements.hpp"
#include "core/Tree.hpp"
#include "core/TreeController.hpp"

namespace {

std::string percent(double share) {
    char figure[8];
    std::snprintf(figure, sizeof(figure), "%d%%", static_cast<int>(std::lround(share)));
    return figure;
}

Gtk::Label* dim_label(const std::string& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_halign(Gtk::Align::START);
    label->set_wrap(true);
    return label;
}

}  // namespace

ProjectsPage::ProjectsPage(TreeController& life, TreeController& projects, Priority& priority,
                           Requirements& requirements)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0),
      m_life(life),
      m_projects(projects),
      m_priority(priority),
      m_requirements(requirements) {
    set_hexpand(true);
    set_vexpand(true);

    // Two equal halves. The divider sits inside the right one, so it can't
    // take a third share.
    set_homogeneous(true);

    // --- ranked requirements ---
    auto* heading_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* heading = Gtk::make_managed<Gtk::Label>("Requirements");
    heading->add_css_class("heading");
    heading->set_halign(Gtk::Align::START);
    heading->set_hexpand(true);
    heading_row->append(*heading);

    // The share of everything that has a project behind it.
    m_covered.add_css_class("dim-label");
    m_covered.set_tooltip_text("How much of the total has a project behind it");
    heading_row->append(m_covered);

    m_ranked_scroll.set_child(m_ranked);
    m_ranked_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_ranked_scroll.set_vexpand(true);

    m_ranked_section.set_margin_end(12);
    m_ranked_section.append(*heading_row);
    m_ranked_section.append(m_ranked_scroll);
    append(m_ranked_section);

    auto* right_half = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    right_half->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    right_half->append(m_projects_section);
    append(*right_half);

    // --- the selected requirement's projects ---
    m_selected_title.add_css_class("heading");
    m_selected_title.set_halign(Gtk::Align::START);
    m_selected_title.set_wrap(true);
    m_selected_title.set_xalign(0.0);

    m_projects_scroll.set_child(m_project_checks);
    m_projects_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_projects_scroll.set_vexpand(true);

    m_projects_section.set_hexpand(true);
    m_projects_section.append(m_selected_title);
    m_projects_section.append(m_projects_scroll);

    m_life.connect_changed([this]() { m_refresh.request(); });
    m_projects.connect_changed([this]() { m_refresh.request(); });
    m_priority.connect_changed([this]() { m_refresh.request(); });
    m_requirements.connect_changed([this]() { m_refresh.request(); });

    rebuild();
}

void ProjectsPage::rebuild() {
    while (auto* child = m_ranked.get_first_child()) m_ranked.remove(*child);

    const auto ranked = m_requirements.ranked(m_priority.priorities());

    // Falls back to the highest-ranked requirement.
    bool selection_valid = false;
    int first = -1;
    double covered = 0.0;
    for (const auto& line : ranked) {
        if (line.requirement_id == -1) continue;
        if (first == -1) first = line.requirement_id;
        if (line.requirement_id == m_requirement_id) selection_valid = true;
        if (m_requirements.is_served(line.requirement_id)) covered += line.share;
    }
    if (!selection_valid) m_requirement_id = first;

    m_covered.set_text(percent(covered) + " covered");
    m_covered.set_visible(first != -1);

    for (const auto& line : ranked) {
        const bool empty_leaf = line.requirement_id == -1;
        const std::string text = empty_leaf ? m_life.display_title(line.leaf_id) + ": none yet"
                                            : m_requirements.title(line.requirement_id);

        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* name = Gtk::make_managed<Gtk::Label>(text);
        name->set_xalign(0.0);
        name->set_hexpand(true);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        name->set_max_width_chars(20);
        name->set_tooltip_text(text);
        row->append(*name);

        auto* value = Gtk::make_managed<Gtk::Label>(percent(line.share));
        value->add_css_class("dim-label");
        row->append(*value);

        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*row);
        button->set_has_frame(false);

        // A leaf with none yet has nothing to link here.
        if (empty_leaf) {
            button->set_sensitive(false);
        } else if (line.requirement_id == m_requirement_id) {
            button->set_has_frame(true);
            button->add_css_class("suggested-action");
        } else if (m_requirements.is_served(line.requirement_id)) {
            // Not on the selected row: suggested-action owns its text color.
            button->add_css_class("leaf-served");
        }

        const int id = line.requirement_id;
        button->signal_clicked().connect([this, id]() {
            if (m_requirement_id == id) return;
            m_requirement_id = id;
            m_refresh.request();
        });
        m_ranked.append(*button);
    }

    if (ranked.empty()) m_ranked.append(*dim_label("Add a leaf to the life tree"));

    rebuild_projects();
}

void ProjectsPage::rebuild_projects() {
    m_populating = true;
    while (auto* child = m_project_checks.get_first_child()) m_project_checks.remove(*child);

    m_selected_title.set_visible(m_requirement_id != -1);
    if (m_requirement_id == -1) {
        m_project_checks.append(*dim_label("Requirements are added on the Life Tree page"));
        m_populating = false;
        return;
    }
    m_selected_title.set_text(m_requirements.title(m_requirement_id));

    const auto projects = m_projects.children_of(Tree::ROOT_ID);
    if (projects.empty()) m_project_checks.append(*dim_label("No projects yet"));

    const auto linked = m_requirements.projects_of(m_requirement_id);
    for (int project : projects) {
        auto* check = Gtk::make_managed<Gtk::CheckButton>(m_projects.display_title(project));
        check->set_active(std::find(linked.begin(), linked.end(), project) != linked.end());

        // Deferred: the change emits, which rebuilds this list, check included.
        const int requirement = m_requirement_id;
        check->signal_toggled().connect([this, check, requirement, project]() {
            if (m_populating) return;
            const bool now_linked = check->get_active();
            Glib::signal_idle().connect_once([this, requirement, project, now_linked]() {
                if (now_linked) {
                    m_requirements.link_project(requirement, project);
                } else {
                    m_requirements.unlink_project(requirement, project);
                }
            });
        });
        m_project_checks.append(*check);
    }

    m_populating = false;
}
