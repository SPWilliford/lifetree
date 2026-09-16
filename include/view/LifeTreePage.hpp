#ifndef LIFETREEPAGE_HPP
#define LIFETREEPAGE_HPP

#include <gtkmm/box.h>
#include <gtkmm/editablelabel.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>

#include "view/LifeTreeView.hpp"
#include "view/Refresh.hpp"

class TreeController;
class Priority;

// Where the life tree is defined and weighted: the drawn tree, and beside
// it the selected node's title and seed over every leaf ranked by its
// share of the whole.
class LifeTreePage : public Gtk::Box {
public:
    LifeTreePage(TreeController& life, Priority& priority);
    ~LifeTreePage() override = default;

private:
    TreeController& m_life;
    Priority& m_priority;

    LifeTreeView m_view;

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

    // --- every leaf, ranked ---
    // A stepper shows a node's share of its PARENT; this shows what that
    // came to by the time it reached the leaf.
    Gtk::Box m_ranked_panel{Gtk::Orientation::VERTICAL, 6};
    Gtk::ToggleButton m_weights_toggle;
    Gtk::ScrolledWindow m_ranked_scroll;
    Gtk::Box m_ranked{Gtk::Orientation::VERTICAL, 2};

    void rebuild_ranking();

    Refresh m_ranking_refresh{sigc::mem_fun(*this, &LifeTreePage::rebuild_ranking)};
};

#endif
