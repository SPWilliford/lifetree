#include "view/LifeTreePanel.hpp"

#include <cstdio>

#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/TreeController.hpp"
#include "view/CardRow.hpp"
#include "view/Style.hpp"

LifeTreePanel::LifeTreePanel(TreeController& life, Priority& priority)
    : TreePanel(life), m_priority(priority) {
    build();

    // prepend, not append: build() has already put the scrolled tree in, and
    // this belongs above it.
    m_show_weights.set_halign(Gtk::Align::END);
    m_show_weights.set_active(false);
    m_show_weights.signal_toggled().connect([this]() { m_refresh.request(); });
    prepend(m_show_weights);
}

void LifeTreePanel::seed_root_store() {
    m_root_store->append(NodeItem::create(0));
}

void LifeTreePanel::connect_sources() {
    // A weight moving changes every figure below it, and the tree's own
    // signal doesn't fire for that.
    m_priority.connect_changed([this]() { m_refresh.request(); });
}

void LifeTreePanel::decorate_row(CardRow& card, int id) {
    // Leaves only. A branch's priority flows to its children, so nothing
    // links to one and "is it served" isn't a question you can ask about it.
    // Independent of the weights toggle: this says whether the tree is
    // covered, which is worth seeing while shaping it, not only while tuning
    // the numbers.
    if (id != 0 && m_tree.children_of(id).empty() && !m_priority.projects_for(id).empty()) {
        card.set_color(style::served_hex());
    }

    // Everything below is a weight figure, so hiding them means doing none
    // of it — the base has already reset the row to its title.
    if (!m_show_weights.get_active()) return;

    // priorities() costs one walk whatever you ask it for; the tree is a
    // handful of nodes, so per-row is cheaper than a cache that can go stale.
    // After the title, not before it. Leading, the figure indents the text
    // by its own width, so no two titles start at the same place and the
    // tree's shape stops being readable at a glance.
    const auto values = m_priority.priorities();
    auto it = values.find(id);
    if (it != values.end()) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f%%", it->second);
        card.set_marker(buf, CardRow::MarkerSide::AFTER);
    }

    if (id == 0) return;  // the root is the whole 100; nothing to trade against

    // Depth by walking up rather than off the TreeListRow — a restyle has
    // only the id.
    int depth = 0;
    for (int p = m_tree.parent_of(id); p > 0; p = m_tree.parent_of(p)) ++depth;
    card.set_weight(m_priority.weight_of(id), depth);
}

void LifeTreePanel::on_weight_edited(int id, double value) {
    m_priority.set_weight(id, value);
}

void LifeTreePanel::extend_row_menu(Gtk::Box& menu, Gtk::Popover* popover, int id) {
    if (id == 0) return;  // the synthetic root isn't a goal

    // "Define" rather than "Edit" when there's nothing there yet: the first
    // time is writing down what a node is for, which is a different act from
    // adjusting wording later.
    const bool defined = m_tree.has_seed(id);
    auto* seed_button = Gtk::make_managed<Gtk::Button>(defined ? "Edit seed…" : "Define seed…");
    seed_button->signal_clicked().connect(
        [this, id, popover]() { popover->set_child(*build_seed_editor(id, popover)); });
    menu.append(*seed_button);
}

Gtk::Widget* LifeTreePanel::build_seed_editor(int id, Gtk::Popover* popover) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    box->set_margin(10);

    auto* heading = Gtk::make_managed<Gtk::Label>(m_tree.display_title(id));
    heading->set_xalign(0.0);
    heading->set_ellipsize(Pango::EllipsizeMode::END);
    heading->set_max_width_chars(28);
    heading->add_css_class("heading");
    box->append(*heading);

    // A TextView, not an Entry: a seed is a sentence or several, and the
    // whole reason it isn't the title is that it needs the room.
    auto* view = Gtk::make_managed<Gtk::TextView>();
    view->set_wrap_mode(Gtk::WrapMode::WORD);
    view->get_buffer()->set_text(m_tree.get_seed(id));
    view->set_size_request(320, 120);

    auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(*view);
    scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller->set_has_frame(true);
    box->append(*scroller);

    auto* apply_button = Gtk::make_managed<Gtk::Button>("Save");
    apply_button->signal_clicked().connect([this, id, view, popover]() {
        // Read before deferring — the view lives inside the popover and
        // mustn't be touched once it's closing.
        const std::string seed = view->get_buffer()->get_text();
        popover->popdown();

        // Saving an empty seed clears it, which is the only way to take one
        // back. No separate control, and no state where a node has a seed
        // that says nothing.
        Glib::signal_idle().connect_once([this, id, seed]() { m_tree.set_seed(id, seed); });
    });
    box->append(*apply_button);

    return box;
}
