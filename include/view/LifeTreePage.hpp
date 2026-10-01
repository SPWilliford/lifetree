#ifndef LIFETREEPAGE_HPP
#define LIFETREEPAGE_HPP

#include <gtkmm/box.h>
#include <gtkmm/editablelabel.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/overlay.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>

#include "view/LifeTreeView.hpp"
#include "view/Refresh.hpp"

class TreeController;
class Priority;
class Requirements;

// Where the life tree is defined and weighted: the drawn tree, and beside
// it the selected node's title, seed and requirements over every
// requirement ranked by its share of the whole.
class LifeTreePage : public Gtk::Box {
public:
    LifeTreePage(TreeController& life, Priority& priority, Requirements& requirements);
    ~LifeTreePage() override = default;

private:
    TreeController& m_life;
    Priority& m_priority;
    Requirements& m_requirements;

    LifeTreeView m_view;
    Gtk::Overlay m_tree_overlay;
    Gtk::ToggleButton m_weights_toggle;

    Gtk::Box m_side{Gtk::Orientation::VERTICAL, 8};

    // --- the selected node ---
    // Exactly one of the hint and the two editors is visible.
    Gtk::Box m_detail{Gtk::Orientation::VERTICAL, 8};
    Gtk::Label m_hint;
    Gtk::EditableLabel m_title;
    Gtk::TextView m_seed;
    Gtk::ScrolledWindow m_seed_scroll;

    // What the detail column shows. Not the view's selection: a seed commit
    // must reach the node the text was typed against, which is the previous
    // one by the time a new selection arrives.
    int m_detail_id = -1;

    // Set while the editors are filled from the model, so their handlers
    // don't write back what was just put in front of them.
    bool m_populating = false;

    void show_node(int id);

    // No Save button: called when the seed loses focus and before the
    // column is pointed at another node.
    void commit_seed();

    // --- the selected leaf's requirements ---
    // A branch shows the note instead.
    Gtk::Box m_requirements_box{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_requirements_heading;
    Gtk::ScrolledWindow m_requirements_scroll;
    Gtk::Box m_requirement_rows{Gtk::Orientation::VERTICAL, 2};
    Gtk::Entry m_requirement_entry;
    Gtk::Box m_suggestions{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label m_branch_note;

    void rebuild_requirements();
    void rebuild_suggestions();
    Gtk::Widget& make_requirement_row(int leaf_id, int requirement_id, int index, int count);

    Refresh m_requirements_refresh{sigc::mem_fun(*this, &LifeTreePage::rebuild_requirements)};

    // --- every requirement, ranked ---
    // The page's output. A leaf with none yet is listed at its own share.
    Gtk::Box m_ranked_panel{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label m_ranked_heading;
    Gtk::ScrolledWindow m_ranked_scroll;
    Gtk::Box m_ranked{Gtk::Orientation::VERTICAL, 2};

    void rebuild_ranking();

    Refresh m_ranking_refresh{sigc::mem_fun(*this, &LifeTreePage::rebuild_ranking)};
};

#endif
