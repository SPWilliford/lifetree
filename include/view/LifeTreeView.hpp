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

// The life tree drawn as discs hanging from the root. Click selects,
// right-click opens the node menu. In weights mode each disc's AREA is its
// share of the whole tree, and the selected node gets a +/- stepper.
//
// Placement (place) and drawing (on_draw) are separate steps.
class LifeTreeView : public Gtk::DrawingArea {
public:
    LifeTreeView(TreeController& life, Priority& priority);
    ~LifeTreeView() override;

    // The last-clicked node, or -1.
    sigc::signal<void(int)> signal_selected() { return m_selected_signal; }
    int selected() const { return m_selected; }

    // A node was just created from the menu; the owner may want to open
    // its title for typing.
    sigc::signal<void(int)> signal_node_added() { return m_added_signal; }

    void select(int id);

    void set_show_weights(bool show);
    bool show_weights() const { return m_show_weights; }

private:
    TreeController& m_life;
    Priority& m_priority;

    struct PlacedNode {
        int id = 0;
        double x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        int parent_id = -1;
        double share = 0.0;      // of the whole tree
        bool steppable = false;  // false for an only child, which is always TOTAL
    };

    // The stepper's two halves, as circles. Computed rather than stored so
    // the draw and the click test can't disagree.
    struct Stepper {
        double minus_x = 0.0;
        double plus_x = 0.0;
        double y = 0.0;
        double radius = 0.0;
        bool visible = false;
    };
    Stepper stepper_for(const PlacedNode& node) const;

    std::vector<PlacedNode> place(int width, int height) const;

    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void on_pressed(int n_press, double x, double y);
    void on_secondary(int n_press, double x, double y);

    // The node under a point, or -1.
    int node_at(double x, double y) const;

    void show_node_menu(int id, double x, double y);

    int m_selected = -1;
    bool m_show_weights = false;

    // The last frame's placement, so a click is tested against what was
    // drawn.
    std::vector<PlacedNode> m_placed;

    Glib::RefPtr<Gtk::GestureClick> m_click;
    Glib::RefPtr<Gtk::GestureClick> m_secondary_click;
    Gtk::Popover m_node_menu;

    sigc::signal<void(int)> m_selected_signal;
    sigc::signal<void(int)> m_added_signal;
};

#endif
