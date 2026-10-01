#ifndef PROJECTSPAGE_HPP
#define PROJECTSPAGE_HPP

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include "view/Refresh.hpp"

class TreeController;
class Priority;
class Requirements;

// Which projects address which requirements. The ranked requirements on
// the left, worked down from the top; the selected one's projects on the
// right.
class ProjectsPage : public Gtk::Box {
public:
    ProjectsPage(TreeController& life, TreeController& projects, Priority& priority,
                 Requirements& requirements);
    ~ProjectsPage() override = default;

private:
    TreeController& m_life;
    TreeController& m_projects;
    Priority& m_priority;
    Requirements& m_requirements;

    // -1 only when there are no requirements.
    int m_requirement_id = -1;

    Gtk::Box m_ranked_section{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_covered;
    Gtk::ScrolledWindow m_ranked_scroll;
    Gtk::Box m_ranked{Gtk::Orientation::VERTICAL, 2};

    Gtk::Box m_projects_section{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_selected_title;
    Gtk::ScrolledWindow m_projects_scroll;
    Gtk::Box m_project_checks{Gtk::Orientation::VERTICAL, 2};

    // Set while the checks are filled, so their handlers don't write back
    // the values just put in front of them.
    bool m_populating = false;

    void rebuild();
    void rebuild_projects();

    Refresh m_refresh{sigc::mem_fun(*this, &ProjectsPage::rebuild)};
};

#endif
