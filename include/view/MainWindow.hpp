#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP
#include <string>

#include <giomm/simpleactiongroup.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/editablelabel.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>

#include "core/Priority.hpp"
#include "core/TreeController.hpp"
#include "view/LifeTreeView.hpp"
#include "view/LinksPanel.hpp"
#include "view/ProjectTreePanel.hpp"
#include "view/ReviewPanel.hpp"
#include "view/SchedulePanel.hpp"
#include "view/TaskPanel.hpp"
class App;
class MainWindow : public Gtk::Window {
private:
    Gtk::HeaderBar m_header_bar;

    // What the header shows, which is NOT the window title. The window is
    // always "LifeTree" so the desktop's window list has something to call
    // it; this says where you are inside it, and is blank on the daily page.
    Gtk::Label m_header_title;

    // The day, and the places you step out of it to. Destinations go in the
    // menu rather than the header, so adding one costs a menu item instead
    // of a word on the screen you look at all day.
    Gtk::Stack m_mode_stack;

    // Both live in the header's leading slot and exactly one is ever shown,
    // so it reads as a single control that changes with context.
    Gtk::MenuButton m_menu_button;
    Gtk::Button m_back_button;
    Glib::RefPtr<Gio::SimpleActionGroup> m_nav_actions;

    // Switches the stack, names the destination in the header, and swaps
    // menu for back. The one place any of that is decided.
    // One call for every destination. sub_page is the priority stack's
    // page — empty when the destination isn't inside it, so nothing has to
    // remember which mode has halves and which doesn't.
    void show_mode(const std::string& name, const std::string& title, const std::string& sub_page);

    // Gtk::Paned takes two children, so three resizable columns means one
    // holding the tree and a second, inner one holding schedule + task.
    Gtk::Paned m_outer_paned{Gtk::Orientation::HORIZONTAL};
    Gtk::Paned m_inner_paned{Gtk::Orientation::HORIZONTAL};

    Gtk::Box m_daily_page{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box m_priority_page{Gtk::Orientation::VERTICAL, 0};

    // Not peers: a column. The life tree is above, the projects that serve
    // its leaves below, and moving between them slides rather than fades —
    // the same view panning down the structure rather than two tabs swapped.
    //
    // A StackSwitcher used to sit over this and said the opposite. Whatever
    // orientation it were given, a switcher means "here are some options,
    // pick one"; the relationship here has a direction.
    Gtk::Stack m_priority_stack;

    // Each sub-page is its content plus the control that leads onward, so
    // the columns are wrapped rather than added to the stack directly.
    Gtk::Box m_life_tree_column{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box m_projects_column{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box m_life_tree_page{Gtk::Orientation::HORIZONTAL, 0};

    // The right-hand column of the Life Tree page: the selected node's
    // details on top, the indented list below it for now.
    //
    // Half each, both expanding, so the split stays a half whatever either
    // one ends up holding — the same arrangement as the schedule's two
    // docks, and for the same reason.
    Gtk::Box m_life_side{Gtk::Orientation::VERTICAL, 8};
    Gtk::Box m_life_detail{Gtk::Orientation::VERTICAL, 8};

    // Structure or proportion — the two things this page is for, and the
    // toggle between them. Off by default: a tree is defined before it is
    // weighted, and weights on an unfinished tree are noise.
    Gtk::ToggleButton m_weights_toggle;

    // Every leaf, ranked by its share of the WHOLE tree — which is the
    // number the node steppers can't show. A node's stepper says how it
    // divides its parent; these say what that came to by the time it reached
    // the bottom, and they sum to a hundred across the list.
    //
    // The two answer different questions and both are needed: a leaf at 50%
    // of a branch worth 4% is a small slice of your life, and only this
    // says so.
    Gtk::Box m_life_ranked{Gtk::Orientation::VERTICAL, 2};
    Gtk::ScrolledWindow m_life_ranked_scroll;

    // The toggle belongs to this list, not to the page: the list IS the
    // weights, and the switch that turns the tree into a weighted one sits
    // at its head rather than floating above the column.
    Gtk::Box m_life_ranked_panel{Gtk::Orientation::VERTICAL, 6};

    void rebuild_life_ranking();

    // The selected node, or the hint when there isn't one. Exactly one of
    // the hint and the editors below is ever visible.
    Gtk::Label m_life_detail_hint;

    // Editable in place, both of them: this panel replaces the list as the
    // way a node is defined, so reading and writing happen in the same spot
    // rather than through a menu that opens somewhere else.
    Gtk::EditableLabel m_life_detail_title;
    Gtk::TextView m_life_detail_seed;
    Gtk::ScrolledWindow m_life_detail_seed_scroll;

    void show_life_node(int id);

    // Writes the seed field back to whichever node it was opened on. Called
    // on focus leaving the field and before the panel is pointed at another
    // node — there is no Save button, so those are the only two moments the
    // text is finished being edited.
    void commit_life_seed();

    // What the panel is showing. Not the view's selection: a commit has to
    // reach the node the text was typed against, which is the previous one
    // by the time a new selection arrives.
    int m_life_detail_id = -1;

    // Set while the fields are being filled from the model, so the handlers
    // that write them back don't fire on text they just put there.
    bool m_life_detail_populating = false;

    // The one component this window reads itself rather than handing to a
    // panel: the detail column shows a node's title and seed, and there is
    // no panel between it and the tree.
    TreeController& m_life;

    // Read directly for the same reason: the ranked column is this window's
    // own, with no panel between it and the numbers.
    Priority& m_priority;
    Gtk::Box m_links_page{Gtk::Orientation::HORIZONTAL, 0};

    // Down from the life tree, up from projects. Flat and centred on the
    // edge they lead across, so each reads as the way onward rather than as
    // a control belonging to the content above it.
    Gtk::Button m_to_projects_button;
    Gtk::Button m_to_life_tree_button;

    // The window's whole content: the mode stack above, footer below.
    Gtk::Box m_root{Gtk::Orientation::VERTICAL, 0};

    // The life tree lives under Priority: weighting it is a question you
    // step out of the day to answer, not something to stare at while
    // working.
    ProjectTreePanel m_projects_panel;
    LifeTreeView m_life_view;
    LinksPanel m_links_panel;
    ReviewPanel m_review_panel;
    SchedulePanel m_schedule_panel;
    TaskPanel m_task_panel;

    // Empty for now. The date and the day's total moved into the schedule's
    // own left dock, where they sit beside the timeline they describe. Kept
    // rather than deleted: this strip is where periodic reminders go.
    Gtk::Box m_footer{Gtk::Orientation::HORIZONTAL, 12};

public:
    explicit MainWindow(App& app);
    ~MainWindow() override = default;
};
#endif
