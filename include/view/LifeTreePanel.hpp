#ifndef LIFETREEPANEL_HPP
#define LIFETREEPANEL_HPP
#include <gtkmm/checkbutton.h>

#include "view/TreePanel.hpp"

class Priority;

// The life tree. With weights shown a row carries two figures: the spin is
// its share of its parent (what you set), the marker its share of everything
// (what the cascade produces). With them hidden it's titles and structure.
//
// Hidden by default — the numbers are a tuning tool, not something to read
// past while you're shaping the tree.
//
// The root is shown and the tree auto-expands: it's small, and a weight only
// means anything with its siblings on screen.
class LifeTreePanel : public TreePanel {
public:
    LifeTreePanel(TreeController& life, Priority& priority);

protected:
    void seed_root_store() override;
    bool autoexpand() const override { return true; }
    std::string root_title() const override { return "Live a Good Life"; }
    void decorate_row(CardRow& card, int id) override;
    void connect_sources() override;
    void on_weight_edited(int id, double value) override;

    // "Edit seed…" on the row menu, and the editor it opens.
    void extend_row_menu(Gtk::Box& menu, Gtk::Popover* popover, int id) override;
    Gtk::Widget* build_seed_editor(int id, Gtk::Popover* popover);

private:
    Priority& m_priority;

    // Above the tree and trailing-aligned, so it sits over the column of
    // spins it governs. Session-only; there's nowhere to persist a view
    // preference yet.
    Gtk::CheckButton m_show_weights{"Weights"};
};
#endif
