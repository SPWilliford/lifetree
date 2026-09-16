#ifndef LIFETREEVIEW_HPP
#define LIFETREEVIEW_HPP
#include <vector>

#include <cairomm/context.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/popover.h>
#include <sigc++/signal.h>

class TreeController;
class Priority;

// The life tree as a drawing: the structure itself, hanging from its root.
//
// The SEEING view. The list beside it stays the editing view — a weight is
// only meaningful against its siblings, and siblings sharing a baseline in
// an indented list is what makes them comparable. This says something the
// list can't: how the whole thing is shaped, and where the weight sits.
//
// Placement and drawing are deliberately separate steps. Where a node goes
// and how it's drawn are different questions, and keeping them apart is what
// makes a second layout — force-directed, radial, a naturalistic tree —
// cheap to add later. It also decides what would ever be worth extracting:
// the placement. The drawing is where this app's character lives.
class LifeTreeView : public Gtk::DrawingArea {
public:
    LifeTreeView(TreeController& life, Priority& priority);

    // The node the user last clicked, or -1. Emitted rather than polled so
    // the panel showing its details doesn't have to know this widget exists
    // beyond one connection.
    sigc::signal<void(int)> signal_selected() { return m_selected_signal; }
    int selected() const { return m_selected; }

    // A node was just created, so whoever owns the detail panel can point
    // it there and open the title for typing. Separate from selection
    // because only a brand-new node wants the cursor put in it.
    sigc::signal<void(int)> signal_node_added() { return m_added_signal; }

    // Weights mode: every node sized by the share of your life it carries,
    // and a stepper inside each disc for changing it. Off, the tree is pure
    // structure — which is the other thing you come to this page to think
    // about, and thinking about both at once is what made the old
    // one-weight-at-a-time popover unusable.
    // Selects from outside — the ranked list beside this one points at a
    // node by clicking it, and the drawing is where a selection lives.
    void select(int id);

    void set_show_weights(bool show);
    bool show_weights() const { return m_show_weights; }

private:
    TreeController& m_life;
    Priority& m_priority;

    // One node, placed. radius is uniform for now — sizing by share is the
    // obvious next channel — but it lives here rather than as a constant
    // because the click test and the drawing must agree on it exactly.
    struct PlacedNode {
        int id = 0;
        double x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        int parent_id = -1;

        // The node's share of the WHOLE tree, which is what the disc's area
        // shows. Distinct from the figure printed inside it — see draw.
        double share = 0.0;

        // Whether this node's weight can move at all. An only child is
        // always its parent's whole share, so a stepper on it would be a
        // control that does nothing.
        bool steppable = false;
    };

    // The two halves of a node's stepper, as circles. Returned rather than
    // stored so the draw and the click test can't disagree about where they
    // are: both ask this, neither owns it.
    struct Stepper {
        double minus_x = 0.0;
        double plus_x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        bool visible = false;
    };

    // Below the node rather than inside it, at a fixed size, and on the
    // SELECTED node only.
    //
    // Inside was the obvious place and it doesn't work: once a disc is sized
    // by its share, most of them are small, and a title plus a figure plus
    // two buttons does not fit in twenty pixels. Sized proportionally the
    // buttons came out at two or three pixels across on everything but the
    // root. Outside and fixed, they are the same target whatever the node is
    // worth — which is what a control has to be.
    //
    // One node at a time keeps the tree readable: the whole page would
    // otherwise be steppers, and you only ever adjust one.
    Stepper stepper_for(const PlacedNode& node) const;

    // Everything the drawing needs, and nothing about how it looks.
    std::vector<PlacedNode> place(int width, int height) const;

    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void on_pressed(int n_press, double x, double y);
    void on_secondary(int n_press, double x, double y);

    // The node under a point, or -1. One definition, because both the click
    // that selects and the one that opens the menu ask it.
    int node_at(double x, double y) const;

    void show_node_menu(int id, double x, double y);

    int m_selected = -1;
    bool m_show_weights = false;

    // The last frame's placement, kept so a click is tested against exactly
    // what was drawn rather than a layout recomputed at a different size.
    std::vector<PlacedNode> m_placed;

    Glib::RefPtr<Gtk::GestureClick> m_click;
    Glib::RefPtr<Gtk::GestureClick> m_secondary_click;

    // Parented to this widget and pointed at the click, so it opens where
    // the pointer is rather than somewhere on the node it belongs to.
    Gtk::Popover m_node_menu;

    sigc::signal<void(int)> m_selected_signal;
    sigc::signal<void(int)> m_added_signal;
};
#endif
