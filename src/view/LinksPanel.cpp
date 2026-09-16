#include "view/LinksPanel.hpp"

#include <algorithm>

#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <gtkmm/button.h>
#include <gtkmm/separator.h>

#include "core/Priority.hpp"
#include "core/TreeController.hpp"

namespace {
constexpr int ROOT_ID = 0;

// A floor per half: left to its content, a sparse top half collapses to
// two rows and the page reads as one section and some leftovers.
// Taller for the same reason: a section has the page's full height now
// instead of half of it.
constexpr int SECTION_MIN_HEIGHT = 320;

// Narrower than when the sections were stacked and each had the whole
// window: two of these now sit abreast, and a selector column is a list
// of names rather than something that needs reading width.
constexpr int LIST_MIN_WIDTH = 170;

Gtk::Label* dim_label(const std::string& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_halign(Gtk::Align::START);
    label->set_wrap(true);
    return label;
}
}  // namespace

LinksPanel::LinksPanel(TreeController& life, TreeController& projects, Priority& priority)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 12),
      m_life(life),
      m_projects(projects),
      m_priority(priority) {
    set_hexpand(true);
    set_vexpand(true);

    // Titled by what you pick on each side rather than by the question the
    // side answers. The questions were a sentence each and the hint beneath
    // already says which way the shares run.
    build_section(m_goal_section, m_goal_hint, m_goal_columns, m_goal_list_scroll,
                  m_goal_links_scroll, m_goal_list, m_goal_links, "By goal");

    auto* divider = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    divider->set_margin_start(4);
    divider->set_margin_end(4);
    append(*divider);

    build_section(m_project_section, m_project_hint, m_project_columns, m_project_list_scroll,
                  m_project_links_scroll, m_project_list, m_project_links, "By project");

    m_life.connect_changed([this]() { m_refresh.request(); });
    m_projects.connect_changed([this]() { m_refresh.request(); });
    m_priority.connect_changed([this]() { m_refresh.request(); });

    rebuild();
}

void LinksPanel::build_section(Gtk::Box& section, Gtk::Label& hint, Gtk::Box& columns,
                               Gtk::ScrolledWindow& list_scroll, Gtk::ScrolledWindow& links_scroll,
                               Gtk::Grid& list, Gtk::Grid& links, const std::string& title) {
    auto* heading = Gtk::make_managed<Gtk::Label>();
    heading->set_text(title);
    heading->add_css_class("heading");
    heading->set_halign(Gtk::Align::START);
    section.append(*heading);

    hint.set_halign(Gtk::Align::START);
    hint.add_css_class("dim-label");
    hint.set_ellipsize(Pango::EllipsizeMode::END);

    for (auto* grid : {&list, &links}) {
        grid->set_row_spacing(2);
        grid->set_column_spacing(12);
    }

    list_scroll.set_child(list);
    links_scroll.set_child(links);

    // The list is a fixed set of names; the links side carries a checkbox
    // and a spin per title, so it takes the rest.
    list_scroll.set_hexpand(false);
    list_scroll.set_min_content_width(LIST_MIN_WIDTH);
    list_scroll.set_size_request(LIST_MIN_WIDTH, -1);
    links_scroll.set_hexpand(true);

    for (auto* scroll : {&list_scroll, &links_scroll}) {
        scroll->set_vexpand(true);
        scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
        scroll->set_min_content_height(SECTION_MIN_HEIGHT);
    }

    section.append(hint);

    columns.set_hexpand(true);
    columns.set_vexpand(true);
    columns.append(list_scroll);
    columns.append(links_scroll);
    section.append(columns);

    section.set_hexpand(true);
    section.set_vexpand(true);
    append(section);
}

void LinksPanel::validate_selection() {
    // Ranked, not tree order, so a fallback selection lands on the goal that
    // matters most rather than whichever one was created first.
    const auto leaves = m_priority.ranked_leaves();
    if (std::find(leaves.begin(), leaves.end(), m_goal_id) == leaves.end()) {
        m_goal_id = leaves.empty() ? -1 : leaves.front();
    }

    const auto projects = m_projects.children_of(ROOT_ID);
    if (std::find(projects.begin(), projects.end(), m_project_id) == projects.end()) {
        m_project_id = projects.empty() ? -1 : projects.front();
    }
}

void LinksPanel::rebuild() {
    m_populating = true;

    for (auto* grid : {&m_goal_list, &m_goal_links, &m_project_list, &m_project_links}) {
        while (auto* child = grid->get_first_child()) grid->remove(*child);
    }

    validate_selection();

    // --- Top half: a goal, and what each project delivers of it ---

    // Weightiest goal first, in both halves — the top half's list of goals
    // and the bottom half's list of the goals a project is about are the
    // same set, so they read as one order.
    const auto leaves = m_priority.ranked_leaves();
    int row = 0;
    for (int leaf : leaves) {
        append_selector_row(m_goal_list, row++, Axis::GOAL, leaf, m_life.display_title(leaf));
    }

    const auto projects = m_projects.children_of(ROOT_ID);

    if (m_goal_id < 0) {
        m_goal_hint.set_text("No goals yet");
        append_empty_note(m_goal_links, "Add a leaf to the life tree");
    } else {
        m_goal_hint.set_text(m_life.display_title(m_goal_id) + " — 100 across its projects");
        if (projects.empty()) {
            append_empty_note(m_goal_links, "No projects yet");
        } else {
            row = 0;
            for (int project : projects) {
                append_link_row(m_goal_links, row++, Axis::GOAL, project, m_goal_id,
                                m_projects.display_title(project));
            }
        }
    }

    // --- Bottom half: a project, and how much of it is about each goal ---

    row = 0;
    for (int project : projects) {
        append_selector_row(m_project_list, row++, Axis::PROJECT, project,
                            m_projects.display_title(project));
    }

    if (m_project_id < 0) {
        m_project_hint.set_text("No projects yet");
        append_empty_note(m_project_links, "Add a project");
    } else {
        m_project_hint.set_text(m_projects.display_title(m_project_id) + " — 100 across its goals");
        if (leaves.empty()) {
            append_empty_note(m_project_links, "Add a leaf to the life tree");
        } else {
            row = 0;
            for (int leaf : leaves) {
                append_link_row(m_project_links, row++, Axis::PROJECT, m_project_id, leaf,
                                m_life.display_title(leaf));
            }
        }
    }

    m_populating = false;
}

void LinksPanel::append_empty_note(Gtk::Grid& grid, const std::string& text) {
    grid.attach(*dim_label(text), 0, 0);
}

void LinksPanel::append_selector_row(Gtk::Grid& grid, int row, Axis axis, int id,
                                     const std::string& title) {
    auto* button = Gtk::make_managed<Gtk::Button>(title);
    button->set_halign(Gtk::Align::FILL);
    button->set_hexpand(true);
    button->set_has_frame(false);

    // The button's own label centers itself; left-aligning it makes the
    // column read as a list of names rather than a row of controls.
    if (auto* label = dynamic_cast<Gtk::Label*>(button->get_child())) {
        label->set_halign(Gtk::Align::START);
        label->set_ellipsize(Pango::EllipsizeMode::END);
    }

    // Framed for the selection: suggested-action alone only recolors text,
    // too quiet in a list of plain labels to find at a glance — and this is
    // what the whole right-hand column depends on.
    const int selected = (axis == Axis::GOAL) ? m_goal_id : m_project_id;
    if (selected == id) {
        button->set_has_frame(true);
        button->add_css_class("suggested-action");
    } else if (axis == Axis::GOAL && !m_priority.projects_for(id).empty()) {
        // Goals only — "served" is a fact about a goal, not a project. Never
        // on the selected row: suggested-action already owns its text color,
        // and that row needs the mark least, since what serves it is listed
        // right beside it.
        button->add_css_class("leaf-served");
    }

    // No toggle-off: releasing would leave an empty right-hand column, and
    // a click that sometimes clears and sometimes moves is the double
    // meaning this panel exists to remove.
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

void LinksPanel::append_link_row(Gtk::Grid& grid, int row, Axis axis, int project_id, int leaf_id,
                                 const std::string& title) {
    const bool linked = m_priority.has_link(project_id, leaf_id);

    auto* check = Gtk::make_managed<Gtk::CheckButton>();
    check->set_active(linked);
    grid.attach(*check, 0, row);

    // Not hexpanding: expanded, the title column strands the spin against
    // the page edge. At natural width the grid lines every spin up just past
    // the longest title. Capped so one long title can't undo that.
    auto* label = Gtk::make_managed<Gtk::Label>(title);
    label->set_halign(Gtk::Align::START);
    label->set_max_width_chars(34);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    grid.attach(*label, 1, row);

    // Fixed by which half of the page this is, not by anything the user
    // last clicked.
    const bool goal_axis = (axis == Axis::GOAL);
    const double value = goal_axis ? m_priority.goal_share(project_id, leaf_id)
                                   : m_priority.project_share(project_id, leaf_id);

    auto* spin =
        Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(value, 0.0, 100.0, 1.0, 5.0));
    spin->set_digits(0);
    spin->set_width_chars(3);
    spin->set_sensitive(linked);
    if (!linked) spin->set_text("");  // an unlinked row holds no share
    grid.attach(*spin, 2, row);

    // The same link is editable from both halves, since it's one link seen
    // two ways -- unchecking it here removes it from the other half too.
    // Deferred to an idle for the same reason as the spin below.
    check->signal_toggled().connect([this, check, project_id, leaf_id]() {
        if (m_populating) return;
        const bool now_linked = check->get_active();
        Glib::signal_idle().connect_once([this, now_linked, project_id, leaf_id]() {
            if (now_linked)
                m_priority.set_link(project_id, leaf_id);
            else
                m_priority.clear_link(project_id, leaf_id);
        });
    });

    spin->signal_value_changed().connect([this, spin, project_id, leaf_id, goal_axis]() {
        if (m_populating) return;
        const double v = spin->get_value();
        // Deferred: setting a share rebalances the others on that axis and
        // emits, which rebuilds these grids — including the widget
        // currently inside its own handler.
        Glib::signal_idle().connect_once([this, v, project_id, leaf_id, goal_axis]() {
            if (goal_axis)
                m_priority.set_goal_share(project_id, leaf_id, v);
            else
                m_priority.set_project_share(project_id, leaf_id, v);
        });
    });
}
