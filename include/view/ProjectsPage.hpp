#ifndef PROJECTSPAGE_HPP
#define PROJECTSPAGE_HPP

#include <string>

#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/scrolledwindow.h>

#include "view/Refresh.hpp"

class TreeController;
class Priority;

// Which projects serve which life leaves, in what proportion. Two halves,
// one per share axis: pick a leaf on the left to set each project's
// goal_share of it; pick a project on the right to set its project_share
// across leaves.
class ProjectsPage : public Gtk::Box {
public:
    ProjectsPage(TreeController& life, TreeController& projects, Priority& priority);
    ~ProjectsPage() override = default;

private:
    TreeController& m_life;
    TreeController& m_projects;
    Priority& m_priority;

    // Which share a half's spin buttons write.
    enum class Axis { GOAL, PROJECT };

    // Current selections; -1 only when there's nothing to select.
    int m_goal_id = -1;
    int m_project_id = -1;

    Gtk::Box m_goal_columns{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Box m_project_columns{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Box m_goal_section{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box m_project_section{Gtk::Orientation::VERTICAL, 6};
    Gtk::ScrolledWindow m_goal_list_scroll;
    Gtk::ScrolledWindow m_goal_links_scroll;
    Gtk::ScrolledWindow m_project_list_scroll;
    Gtk::ScrolledWindow m_project_links_scroll;
    Gtk::Grid m_goal_list;      // every life leaf
    Gtk::Grid m_goal_links;     // projects, for the picked leaf
    Gtk::Grid m_project_list;   // every top-level project
    Gtk::Grid m_project_links;  // leaves, for the picked project

    // Set while the grids are being filled, so handlers don't write back
    // the values just put in front of them.
    bool m_populating = false;

    void rebuild();
    void validate_selection();

    void build_section(Gtk::Box& section, Gtk::Box& columns, Gtk::ScrolledWindow& list_scroll,
                       Gtk::ScrolledWindow& links_scroll, Gtk::Grid& list, Gtk::Grid& links,
                       const std::string& title);
    void append_selector_row(Gtk::Grid& grid, int row, Axis axis, int id, const std::string& title);
    void append_link_row(Gtk::Grid& grid, int row, Axis axis, int project_id, int leaf_id,
                         const std::string& title);
    void append_empty_note(Gtk::Grid& grid, const std::string& text);

    Refresh m_refresh{sigc::mem_fun(*this, &ProjectsPage::rebuild)};
};

#endif
