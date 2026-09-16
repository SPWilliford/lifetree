#include "view/LifeTreePage.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/TreeController.hpp"

namespace {
constexpr int SIDE_WIDTH = 340;
}

LifeTreePage::LifeTreePage(TreeController& life, Priority& priority)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0),
      m_life(life),
      m_priority(priority),
      m_view(life, priority) {
    set_hexpand(true);
    set_vexpand(true);

    m_view.set_hexpand(true);
    append(m_view);

    // --- detail ---
    m_hint.set_text("Select a node");
    m_hint.add_css_class("dim-label");
    m_hint.set_valign(Gtk::Align::CENTER);
    m_hint.set_vexpand(true);

    m_title.add_css_class("node-title");
    m_title.set_alignment(0.5f);
    m_title.set_margin_top(6);
    m_title.property_editing().signal_changed().connect([this]() {
        if (m_title.get_editing()) return;
        if (m_populating || m_detail_id < 0) return;
        m_life.edit(m_detail_id, m_title.get_text().raw());
    });

    m_seed.set_wrap_mode(Gtk::WrapMode::WORD);
    m_seed.set_justification(Gtk::Justification::CENTER);
    m_seed.add_css_class("seed-view");
    m_seed.set_left_margin(14);
    m_seed.set_right_margin(14);
    m_seed.set_top_margin(10);
    m_seed.set_bottom_margin(10);
    m_seed.set_pixels_above_lines(2);
    m_seed.set_pixels_below_lines(2);

    auto seed_focus = Gtk::EventControllerFocus::create();
    seed_focus->signal_leave().connect(sigc::mem_fun(*this, &LifeTreePage::commit_seed));
    m_seed.add_controller(seed_focus);

    m_seed_scroll.set_child(m_seed);
    m_seed_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_seed_scroll.set_vexpand(true);
    m_seed_scroll.set_margin_top(10);

    m_detail.add_css_class("panel-right");
    m_detail.set_vexpand(true);
    m_detail.append(m_hint);
    m_detail.append(m_title);
    m_detail.append(m_seed_scroll);

    m_view.signal_selected().connect(sigc::mem_fun(*this, &LifeTreePage::show_node));

    // A node the menu just created: open its title for typing, once the
    // selection has landed on it.
    m_view.signal_node_added().connect([this](int id) {
        Glib::signal_idle().connect_once([this, id]() {
            if (m_detail_id == id) m_title.start_editing();
        });
    });

    // Not while the seed is being typed into: the refill would replace it.
    m_life.connect_changed([this]() {
        if (!m_seed.has_focus()) show_node(m_detail_id);
    });

    show_node(-1);

    // --- ranked leaves ---
    m_weights_toggle.set_label("Weights");
    m_weights_toggle.set_has_frame(false);
    m_weights_toggle.set_tooltip_text(
        "Size the tree by weight, and show a stepper on the selected node");
    m_weights_toggle.signal_toggled().connect(
        [this]() { m_view.set_show_weights(m_weights_toggle.get_active()); });

    m_ranked_scroll.set_child(m_ranked);
    m_ranked_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_ranked_scroll.set_vexpand(true);

    m_ranked_panel.add_css_class("panel-right");
    m_ranked_panel.set_vexpand(true);
    m_ranked_panel.append(m_weights_toggle);
    m_ranked_panel.append(m_ranked_scroll);

    m_life.connect_changed([this]() { m_ranking_refresh.request(); });
    m_priority.connect_changed([this]() { m_ranking_refresh.request(); });
    rebuild_ranking();

    // Half each, both expanding, so the split stays a half.
    m_side.set_hexpand(false);
    m_side.set_size_request(SIDE_WIDTH, -1);
    m_side.append(m_detail);
    m_side.append(m_ranked_panel);
    append(m_side);
}

void LifeTreePage::show_node(int id) {
    if (id != m_detail_id) commit_seed();

    const bool has_node = id >= 0 && m_life.contains(id);
    m_detail_id = has_node ? id : -1;

    m_hint.set_visible(!has_node);
    m_title.set_visible(has_node);
    m_seed_scroll.set_visible(has_node);
    if (!has_node) return;

    m_populating = true;
    m_title.set_text(m_life.get_title(id));
    m_seed.get_buffer()->set_text(m_life.get_seed(id));
    m_populating = false;
}

void LifeTreePage::commit_seed() {
    if (m_populating || m_detail_id < 0) return;
    if (!m_life.contains(m_detail_id)) return;

    const std::string typed = m_seed.get_buffer()->get_text();
    if (typed == m_life.get_seed(m_detail_id)) return;
    m_life.set_seed(m_detail_id, typed);
}

void LifeTreePage::rebuild_ranking() {
    while (auto* child = m_ranked.get_first_child()) m_ranked.remove(*child);

    std::vector<std::pair<double, int>> leaves;
    for (const auto& [id, share] : m_priority.priorities()) {
        if (!m_life.contains(id)) continue;
        if (!m_life.children_of(id).empty()) continue;
        leaves.push_back({share, id});
    }
    std::sort(leaves.begin(), leaves.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });

    for (const auto& [share, id] : leaves) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

        auto* name = Gtk::make_managed<Gtk::Label>(m_life.display_title(id));
        name->set_xalign(0.0);
        name->set_hexpand(true);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        row->append(*name);

        char figure[8];
        std::snprintf(figure, sizeof(figure), "%d%%", static_cast<int>(std::lround(share)));
        auto* value = Gtk::make_managed<Gtk::Label>(figure);
        value->add_css_class("dim-label");
        row->append(*value);

        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*row);
        button->set_has_frame(false);
        button->signal_clicked().connect([this, id]() { m_view.select(id); });
        m_ranked.append(*button);
    }
}
