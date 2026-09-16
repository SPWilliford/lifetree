#ifndef LINKSPANEL_HPP
#define LINKSPANEL_HPP
#include <string>
#include <vector>

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>

#include "view/Refresh.hpp"

class TreeController;
class Priority;

// Which projects serve which goals, and in what proportion.
//
// A link's two weights are different questions (see Priority), so each gets
// its own half of the page: pick a goal on top to set what each project
// delivers of it, pick a project below to set how much of it is about each
// goal. Same shape both times — list on the left, that item's links on the
// right — so a spin button means one fixed thing rather than depending on
// what you last clicked.
class LinksPanel : public Gtk::Box {
private:
    TreeController& m_life;
    TreeController& m_projects;
    Priority& m_priority;

    // Which number a spin writes. Fixed per half, not per click.
    enum class Axis { GOAL, PROJECT };

    // Held separately, so picking a project below doesn't disturb the goal
    // above. -1 only when there's nothing to select — rebuild() otherwise
    // keeps a live selection.
    int m_goal_id = -1;
    int m_project_id = -1;

    Gtk::Label m_goal_hint;
    Gtk::Label m_project_hint;

    Gtk::Box m_goal_columns{Gtk::Orientation::HORIZONTAL, 12};
    Gtk::Box m_project_columns{Gtk::Orientation::HORIZONTAL, 12};

    // One per direction, side by side. Stacked, each half got less than a
    // screen of height and the pair still read as one long form; abreast,
    // each is a column of its own and the page is two questions rather than
    // four stacked lists.
    Gtk::Box m_goal_section{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box m_project_section{Gtk::Orientation::VERTICAL, 6};

    Gtk::ScrolledWindow m_goal_list_scroll;
    Gtk::ScrolledWindow m_goal_links_scroll;
    Gtk::ScrolledWindow m_project_list_scroll;
    Gtk::ScrolledWindow m_project_links_scroll;

    Gtk::Grid m_goal_list;      // top left:    every life tree leaf
    Gtk::Grid m_goal_links;     // top right:   projects, for the picked goal
    Gtk::Grid m_project_list;   // bottom left: every top-level project
    Gtk::Grid m_project_links;  // bottom right: goals, for the picked project

    bool m_populating = false;

    void rebuild();

    // Falls back to the first available when a selected node is deleted.
    void validate_selection();

    // Heading, then list beside links.
    void build_section(Gtk::Box& section, Gtk::Label& hint, Gtk::Box& columns,
                       Gtk::ScrolledWindow& list_scroll, Gtk::ScrolledWindow& links_scroll,
                       Gtk::Grid& list, Gtk::Grid& links, const std::string& title);

    // A left-hand row: selects, and is marked when it's the current one.
    void append_selector_row(Gtk::Grid& grid, int row, Axis axis, int id, const std::string& title);

    // A right-hand row: whether the link exists, and its share on this
    // half's axis.
    void append_link_row(Gtk::Grid& grid, int row, Axis axis, int project_id, int leaf_id,
                         const std::string& title);

    void append_empty_note(Gtk::Grid& grid, const std::string& text);

public:
    LinksPanel(TreeController& life, TreeController& projects, Priority& priority);
    ~LinksPanel() override = default;

private:
    Refresh m_refresh{sigc::mem_fun(*this, &LinksPanel::rebuild)};
};

#endif
