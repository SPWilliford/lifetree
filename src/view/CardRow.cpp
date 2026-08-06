#include "view/CardRow.hpp"
#include <gtkmm/gestureclick.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>

CardRow::CardRow(std::string_view initial_text, sigc::slot<void(std::string_view)> on_changed)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 8), m_on_changed(on_changed)
{
    // Matches TaskPanel's row spacing/margin (Backlog, Completed Today) —
    // no reason for the two panels' rows to have drifted to different
    // arbitrary numbers independently.
    set_margin(6);

    m_marker.set_visible(false);
    append(m_marker);

    m_current_text = std::string(initial_text);
    m_label.set_text(m_current_text);
    m_label.set_halign(Gtk::Align::START);
    // Not hexpand: the label taking all the slack would push a trailing
    // marker out to the row's far right, where it reads as a separate
    // column rather than as a note on the title. The spacer below absorbs
    // the slack instead, so the marker stays beside the text it belongs to
    // while the row still fills its width (which the double-click gesture
    // needs — a narrow row would only be clickable over the text).
    append(m_label);

    m_marker_after.set_visible(false);
    append(m_marker_after);

    m_spacer.set_hexpand(true);
    append(m_spacer);

    // Connected once, for the lifetime of this widget — NOT inside the
    // double-click handler. Editing can start and finish many times for
    // the same CardRow (this widget gets recycled by the ListView), so
    // reconnecting on every double-click would stack up duplicate
    // callbacks: one extra per edit session, all firing together.
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
        if (n_press == 2 && !m_is_editing) {
            m_is_editing = true;
            remove(m_label);
            m_editor.set_text(m_current_text); // the authoritative plain text,
                                                // not m_label.get_text() — same
                                                // value either way (get_text()
                                                // already strips markup), but
                                                // this doesn't lean on that
            m_editor.set_hexpand(true);
            insert_child_after(m_editor, m_marker); // where the label just was

            Glib::signal_timeout().connect_once([this]() {
                m_editor.start_editing();
            }, 10);
        }
    });
    add_controller(click);

    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(GDK_BUTTON_SECONDARY);
    right_click->signal_pressed().connect([this](int, double, double) {
        m_secondary_clicked.emit();
    });
    add_controller(right_click);
}

void CardRow::render_label() {
    if (m_current_color.empty()) {
        m_label.set_text(m_current_text);
    } else {
        m_label.set_markup("<span foreground='" + m_current_color + "'>"
            + Glib::Markup::escape_text(m_current_text) + "</span>");
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
