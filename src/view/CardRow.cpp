#include "view/CardRow.hpp"

#include <algorithm>

#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <gtkmm/gestureclick.h>

CardRow::CardRow(std::string_view initial_text, sigc::slot<void(std::string_view)> on_changed)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 8), m_on_changed(on_changed) {
    // Matches TaskPanel's row spacing/margin (Backlog, Completed Today) —
    // no reason for the two panels' rows to have drifted to different
    // arbitrary numbers independently.
    set_margin(6);
    add_css_class("card-row");  // see Style.cpp — keeps the weight spin from
                                // making a row taller than a plain one

    m_icon.set_visible(false);
    append(m_icon);

    m_marker.set_visible(false);
    append(m_marker);

    m_current_text = std::string(initial_text);
    m_label.set_text(m_current_text);
    m_label.set_halign(Gtk::Align::START);
    // Not hexpand: the spacer absorbs the slack instead, so a trailing
    // marker stays beside its title rather than at the row's far edge. The
    // row still fills its width, which the double-click gesture needs.
    append(m_label);

    m_marker_after.set_visible(false);
    append(m_marker_after);

    m_spacer.set_hexpand(true);
    append(m_spacer);

    // After the spacer, so it forms a column down the tree. That column is
    // the point — weights are read against each other.
    m_weight_spin.set_adjustment(Gtk::Adjustment::create(0.0, 0.0, 100.0, 1.0, 5.0));
    m_weight_spin.set_digits(0);
    m_weight_spin.set_width_chars(3);
    m_weight_spin.set_visible(false);
    m_weight_spin.set_valign(Gtk::Align::CENTER);
    m_weight_spin.signal_value_changed().connect([this]() {
        if (m_setting_weight) return;
        m_weight_changed.emit(m_weight_spin.get_value());
    });
    append(m_weight_spin);

    // Once, for this widget's lifetime — NOT inside the double-click
    // handler. ListView recycles the widget, so reconnecting per edit would
    // stack duplicate callbacks that all fire together.
    m_editor.property_editing().signal_changed().connect([this]() {
        if (m_is_editing && !m_editor.get_editing()) {
            auto updated_text = m_editor.get_text().raw();

            if (m_on_changed) {
                m_on_changed(updated_text);
            }

            m_current_text = updated_text;
            render_label();
            remove(m_editor);
            m_is_editing = false;
            // insert_child_after, not append — append would put the label
            // past the trailing marker, so a row edited once would show its
            // marker on the wrong side from then on.
            insert_child_after(m_label, m_marker);
        }
    });

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);

    click->signal_pressed().connect([this](int n_press, double, double) {
        // Checked at fire time rather than by swapping controllers: a
        // recycled row can be an action on one bind and a node on the next.
        if (m_is_action) {
            if (n_press == 1) m_activated.emit();
            return;
        }

        if (n_press == 2) begin_edit();
    });
    add_controller(click);

    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(GDK_BUTTON_SECONDARY);
    right_click->signal_pressed().connect(
        [this](int, double, double) { m_secondary_clicked.emit(); });
    add_controller(right_click);
}

void CardRow::render_label() {
    if (m_current_color.empty()) {
        m_label.set_text(m_current_text);
    } else {
        m_label.set_markup("<span foreground='" + m_current_color + "'>" +
                           Glib::Markup::escape_text(m_current_text) + "</span>");
    }
}

void CardRow::set_text(std::string_view text) {
    m_current_text = std::string(text);
    render_label();
    m_label.queue_resize();
}

void CardRow::set_color(const std::string& hex_color) {
    m_current_color = hex_color;
    render_label();
}

void CardRow::set_marker(std::string_view emoji, MarkerSide side) {
    // Always clear both — a row whose marker moves sides would otherwise
    // keep showing the old one.
    m_marker.set_visible(false);
    m_marker_after.set_visible(false);
    if (emoji.empty()) return;

    Gtk::Label& target = (side == MarkerSide::BEFORE) ? m_marker : m_marker_after;
    target.set_text(std::string(emoji));
    target.set_visible(true);
}

void CardRow::begin_edit() {
    if (m_is_editing || m_is_action) return;

    m_is_editing = true;
    remove(m_label);
    m_editor.set_text(m_current_text);  // the authoritative plain text,
                                        // not m_label.get_text(): same
                                        // value, but doesn't lean on
                                        // get_text() stripping markup
    m_editor.set_hexpand(true);
    insert_child_after(m_editor, m_marker);  // where the label just was

    // Deferred: the editor has to be realized before it can take focus,
    // and it was added to the box a moment ago.
    Glib::signal_timeout().connect_once([this]() { m_editor.start_editing(); }, 10);
}

void CardRow::set_weight(double weight, int depth) {
    m_setting_weight = true;
    m_weight_spin.set_value(weight);
    m_setting_weight = false;

    // From the trailing edge, since that's what it's anchored to: a deeper
    // row gets a smaller margin and sits further right, mirroring its title.
    // Clamped so a deep tree stops indenting rather than running off.
    constexpr int STEP = 14;
    constexpr int LEVELS = 4;
    const int level = std::min(std::max(depth, 0), LEVELS);
    m_weight_spin.set_margin_end((LEVELS - level) * STEP);

    m_weight_spin.set_visible(true);
}

void CardRow::hide_weight() {
    m_weight_spin.set_visible(false);
}

void CardRow::set_icon(std::string_view icon_name) {
    if (icon_name.empty()) {
        m_icon.set_visible(false);
        return;
    }
    m_icon.set_from_icon_name(std::string(icon_name));
    m_icon.set_visible(true);
}

void CardRow::set_action(bool action) {
    m_is_action = action;

    // Styled as an affordance rather than as data — see .card-row-action in
    // Style.cpp. Toggled here rather than by the caller so the two halves of
    // "this row is a button" can't get out of step on a recycled widget.
    if (action) {
        add_css_class("card-row-action");
    } else {
        remove_css_class("card-row-action");
    }
}

void CardRow::set_active(bool active) {
    // Hook for later — no CSS rule defined for this class right now, so
    // toggling it currently has no visible effect. Revisit once the
    // cross-panel "which task is active" glow is actually wired up.
    if (active) {
        add_css_class("card-row-active");
    } else {
        remove_css_class("card-row-active");
    }
}
