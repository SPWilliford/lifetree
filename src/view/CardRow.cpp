#include "view/CardRow.hpp"

#include <glibmm/main.h>
#include <gtkmm/gestureclick.h>

#include "view/Style.hpp"

CardRow::CardRow(sigc::slot<void(std::string_view)> on_changed)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 8), m_on_changed(std::move(on_changed)) {
    set_margin(6);

    m_icon.set_visible(false);
    append(m_icon);

    m_label.set_halign(Gtk::Align::START);
    append(m_label);

    m_marker.set_visible(false);
    append(m_marker);

    m_spacer.set_hexpand(true);
    append(m_spacer);

    // Connected once for the widget's lifetime: ListView recycles rows, so
    // connecting per edit would stack duplicate callbacks.
    m_editor.property_editing().signal_changed().connect([this]() {
        if (!m_is_editing || m_editor.get_editing()) return;

        const std::string updated = m_editor.get_text().raw();
        if (m_on_changed) m_on_changed(updated);

        m_current_text = updated;
        render_label();
        remove(m_editor);
        m_is_editing = false;
        insert_child_after(m_label, m_icon);
    });

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this](int n_press, double, double) {
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
    style::set_colored_text(m_label, m_current_text, m_current_color);
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

void CardRow::set_marker(std::string_view text) {
    m_marker.set_text(std::string(text));
    m_marker.set_visible(!text.empty());
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
    if (action) {
        add_css_class("card-row-action");
    } else {
        remove_css_class("card-row-action");
    }
}

void CardRow::begin_edit() {
    if (m_is_editing || m_is_action) return;

    m_is_editing = true;
    remove(m_label);
    m_editor.set_text(m_current_text);
    m_editor.set_hexpand(true);
    insert_child_after(m_editor, m_icon);

    // The editor can't take focus until it has been realized.
    Glib::signal_timeout().connect_once([this]() { m_editor.start_editing(); }, 10);
}
