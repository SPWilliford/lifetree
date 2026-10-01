#include "view/LifeTreePage.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <pangomm/layout.h>

#include "core/Priority.hpp"
#include "core/Requirements.hpp"
#include "core/TreeController.hpp"

namespace {
constexpr int SIDE_WIDTH = 340;
constexpr int SUGGESTION_LIMIT = 5;
}  // namespace

LifeTreePage::LifeTreePage(TreeController& life, Priority& priority, Requirements& requirements)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0),
      m_life(life),
      m_priority(priority),
      m_requirements(requirements),
      m_view(life, priority) {
    set_hexpand(true);
    set_vexpand(true);

    m_weights_toggle.set_label("Weights");
    m_weights_toggle.set_tooltip_text(
        "Size the tree by weight, and show a stepper on the selected node");
    m_weights_toggle.set_halign(Gtk::Align::END);
    m_weights_toggle.set_valign(Gtk::Align::START);
    m_weights_toggle.set_margin_end(8);
    m_weights_toggle.signal_toggled().connect(
        [this]() { m_view.set_show_weights(m_weights_toggle.get_active()); });

    m_tree_overlay.set_child(m_view);
    m_tree_overlay.add_overlay(m_weights_toggle);
    m_tree_overlay.set_hexpand(true);
    append(m_tree_overlay);

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
    m_seed_scroll.set_propagate_natural_height(true);
    m_seed_scroll.set_min_content_height(60);
    m_seed_scroll.set_max_content_height(140);
    m_seed_scroll.set_margin_top(10);

    // --- requirements ---
    m_requirements_heading.set_text("What must be true?");
    m_requirements_heading.add_css_class("heading");
    m_requirements_heading.set_xalign(0.0);

    m_requirements_scroll.set_child(m_requirement_rows);
    m_requirements_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_requirements_scroll.set_propagate_natural_height(true);

    m_requirement_entry.set_placeholder_text("Add requirement");
    m_requirement_entry.signal_activate().connect([this]() {
        if (m_requirements.add(m_detail_id, m_requirement_entry.get_text().raw()) == -1) return;
        m_requirement_entry.set_text("");
    });
    m_requirement_entry.signal_changed().connect(
        sigc::mem_fun(*this, &LifeTreePage::rebuild_suggestions));

    m_requirements_box.set_vexpand(true);
    m_requirements_box.set_margin_top(10);
    m_requirements_box.append(m_requirements_heading);
    m_requirements_box.append(m_requirements_scroll);
    m_requirements_box.append(m_requirement_entry);
    m_requirements_box.append(m_suggestions);

    m_branch_note.set_text("Requirements live on leaves");
    m_branch_note.add_css_class("dim-label");

    m_requirements.connect_changed([this]() { m_requirements_refresh.request(); });

    m_detail.add_css_class("panel-right");
    m_detail.set_vexpand(true);
    m_detail.append(m_hint);
    m_detail.append(m_title);
    m_detail.append(m_seed_scroll);
    m_detail.append(m_requirements_box);
    m_detail.append(m_branch_note);

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

    // --- ranked requirements ---
    m_ranked_heading.set_text("All requirements");
    m_ranked_heading.add_css_class("heading");
    m_ranked_heading.set_xalign(0.0);

    m_ranked_scroll.set_child(m_ranked);
    m_ranked_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_ranked_scroll.set_vexpand(true);

    m_ranked_panel.add_css_class("panel-right");
    m_ranked_panel.set_vexpand(true);
    m_ranked_panel.append(m_ranked_heading);
    m_ranked_panel.append(m_ranked_scroll);

    m_life.connect_changed([this]() { m_ranking_refresh.request(); });
    m_priority.connect_changed([this]() { m_ranking_refresh.request(); });
    m_requirements.connect_changed([this]() { m_ranking_refresh.request(); });
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
    if (id != m_detail_id) m_requirement_entry.set_text("");
    m_detail_id = has_node ? id : -1;

    const bool leaf = has_node && m_requirements.is_leaf(id);
    m_hint.set_visible(!has_node);
    m_title.set_visible(has_node);
    m_seed_scroll.set_visible(has_node);
    m_seed_scroll.set_vexpand(has_node && !leaf);
    m_requirements_box.set_visible(leaf);
    m_branch_note.set_visible(has_node && !leaf);
    m_requirements_refresh.request();
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

void LifeTreePage::rebuild_requirements() {
    while (auto* child = m_requirement_rows.get_first_child()) m_requirement_rows.remove(*child);
    rebuild_suggestions();
    if (!m_requirements.is_leaf(m_detail_id)) return;

    const std::vector<int> ids = m_requirements.requirements_of(m_detail_id);
    const int count = static_cast<int>(ids.size());
    m_requirements_scroll.set_visible(count > 0);
    for (int i = 0; i < count; ++i) {
        m_requirement_rows.append(make_requirement_row(m_detail_id, ids[i], i, count));
    }
}

void LifeTreePage::rebuild_suggestions() {
    while (auto* child = m_suggestions.get_first_child()) m_suggestions.remove(*child);

    std::vector<int> ids =
        m_requirements.suggestions(m_detail_id, m_requirement_entry.get_text().raw());
    if (ids.size() > SUGGESTION_LIMIT) ids.resize(SUGGESTION_LIMIT);
    m_suggestions.set_visible(!ids.empty());

    for (int id : ids) {
        auto* label = Gtk::make_managed<Gtk::Label>(m_requirements.title(id));
        label->set_xalign(0.0);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->add_css_class("dim-label");

        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*label);
        button->set_has_frame(false);

        // Deferred: clearing the entry rebuilds this list, button included.
        const int leaf = m_detail_id;
        button->signal_clicked().connect([this, leaf, id]() {
            Glib::signal_idle().connect_once([this, leaf, id]() {
                m_requirements.attach(leaf, id);
                m_requirement_entry.set_text("");
                m_requirement_entry.grab_focus();
            });
        });
        m_suggestions.append(*button);
    }
}

Gtk::Widget& LifeTreePage::make_requirement_row(int leaf_id, int requirement_id, int index,
                                                int count) {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);

    auto* number = Gtk::make_managed<Gtk::Label>(std::to_string(index + 1));
    number->add_css_class("dim-label");
    number->add_css_class("numeric");
    number->set_width_chars(2);
    number->set_xalign(1.0);
    row->append(*number);

    // Wraps rather than widening the column.
    auto* title = Gtk::make_managed<Gtk::Label>(m_requirements.title(requirement_id));
    title->set_xalign(0.0);
    title->set_hexpand(true);
    title->set_wrap(true);
    title->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
    title->set_max_width_chars(20);
    row->append(*title);

    auto* menu = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    menu->set_margin(4);
    auto* popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_has_arrow(false);
    popover->set_child(*menu);

    // Renaming changes it under every leaf, so it sits behind the menu
    // rather than a click on the title.
    auto* name_entry = Gtk::make_managed<Gtk::Entry>();
    name_entry->set_text(m_requirements.title(requirement_id));
    name_entry->signal_activate().connect([this, popover, name_entry, requirement_id]() {
        const std::string text = name_entry->get_text();
        popover->popdown();
        Glib::signal_idle().connect_once(
            [this, requirement_id, text]() { m_requirements.rename(requirement_id, text); });
    });
    // An edit abandoned without Enter doesn't linger into the next opening.
    popover->signal_show().connect([this, name_entry, requirement_id]() {
        name_entry->set_text(m_requirements.title(requirement_id));
    });
    menu->append(*name_entry);

    auto add_item = [menu, popover](const std::string& label, sigc::slot<void()> action) {
        auto* button = Gtk::make_managed<Gtk::Button>(label);
        button->set_has_frame(false);
        if (auto* child = dynamic_cast<Gtk::Label*>(button->get_child())) {
            child->set_xalign(0.0);
        }
        button->signal_clicked().connect([popover, action]() {
            popover->popdown();
            Glib::signal_idle().connect_once(action);
        });
        menu->append(*button);
    };

    if (index > 0) {
        add_item("Move up", [this, leaf_id, requirement_id, index]() {
            m_requirements.move(leaf_id, requirement_id, index - 1);
        });
    }
    if (index + 1 < count) {
        add_item("Move down", [this, leaf_id, requirement_id, index]() {
            m_requirements.move(leaf_id, requirement_id, index + 1);
        });
    }
    add_item("Remove",
             [this, leaf_id, requirement_id]() { m_requirements.detach(leaf_id, requirement_id); });

    auto* menu_button = Gtk::make_managed<Gtk::MenuButton>();
    menu_button->set_icon_name("view-more-symbolic");
    menu_button->set_has_frame(false);
    menu_button->set_valign(Gtk::Align::CENTER);
    menu_button->set_popover(*popover);
    row->append(*menu_button);

    return *row;
}

void LifeTreePage::rebuild_ranking() {
    while (auto* child = m_ranked.get_first_child()) m_ranked.remove(*child);

    for (const auto& line : m_requirements.ranked(m_priority.priorities())) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

        const bool empty_leaf = line.requirement_id == -1;
        const std::string text = empty_leaf ? m_life.display_title(line.leaf_id) + ": none yet"
                                            : m_requirements.title(line.requirement_id);
        auto* name = Gtk::make_managed<Gtk::Label>(text);
        name->set_xalign(0.0);
        name->set_hexpand(true);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        name->set_max_width_chars(20);
        name->set_tooltip_text(text);
        if (empty_leaf) name->add_css_class("dim-label");
        row->append(*name);

        char figure[8];
        std::snprintf(figure, sizeof(figure), "%d%%", static_cast<int>(std::lround(line.share)));
        auto* value = Gtk::make_managed<Gtk::Label>(figure);
        value->add_css_class("dim-label");
        row->append(*value);

        // Opens the leaf it comes from; for a shared one, the leaf giving
        // it the most.
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*row);
        button->set_has_frame(false);
        const int leaf = line.leaf_id;
        button->signal_clicked().connect([this, leaf]() { m_view.select(leaf); });
        m_ranked.append(*button);
    }
}
