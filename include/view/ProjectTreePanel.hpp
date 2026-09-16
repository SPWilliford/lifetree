#ifndef PROJECTTREEPANEL_HPP
#define PROJECTTREEPANEL_HPP
#include "view/TreePanel.hpp"

class TaskAttributes;
namespace Gtk {
class Widget;
}

// The projects tree. Rows carry a project color and a marker for whatever
// makes them not-plain — a date, a repeat, an ordering. The repeat marker
// shows on a routine's top row only; the rows beneath it may carry their
// own days, but they are overrides within that routine, not routines.
//
// The synthetic root is hidden, so each project is a top-level row, and a
// trailing "+" row sits where the next one will appear. Starts collapsed:
// project trees grow, and expanding by default means scrolling past
// everything to see the top-level list.
class ProjectTreePanel : public TreePanel {
public:
    ProjectTreePanel(TreeController& projects, TaskAttributes& task_attributes);

protected:
    void seed_root_store() override;
    bool autoexpand() const override { return false; }
    std::string root_title() const override { return "Master Project Root"; }
    void decorate_row(CardRow& card, int id) override;
    void connect_sources() override;
    void extend_row_menu(Gtk::Box& menu, Gtk::Popover* popover, int id) override;
    void on_row_activated(int id) override;

private:
    TaskAttributes& m_task_attributes;

    void add_project();

    // Second levels of the row menu, swapped into the still-open popover.

    // The schedule for a whole routine: one line per node in the subtree
    // under root_id, indented, each with its own seven day toggles. Only
    // the routine's top node opens this — a node inside one edits its days
    // here rather than starting a routine of its own.
    Gtk::Widget* build_repeat_config(int root_id, Gtk::Popover* popover);
    Gtk::Widget* build_date_editor(int id, Gtk::Popover* popover);
    Gtk::Widget* build_color_picker(int id, Gtk::Popover* popover);
};
#endif
