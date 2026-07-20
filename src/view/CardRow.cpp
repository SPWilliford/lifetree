#include "view/CardRow.hpp"
#include <gtkmm/gestureclick.h>
#include <glibmm/main.h>

CardRow::CardRow(std::string_view initial_text, sigc::slot<void(std::string_view)> on_changed)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 12), m_on_changed(on_changed)
{
    m_label.set_text(std::string(initial_text));
    m_label.set_halign(Gtk::Align::START);
    m_label.set_hexpand(true);
    append(m_label);

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

            m_label.set_text(updated_text);
            remove(m_editor);
            m_is_editing = false;
            append(m_label);
        }
    });

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);

    click->signal_pressed().connect([this](int n_press, double, double) {
        if (n_press == 2 && !m_is_editing) {
            m_is_editing = true;
            remove(m_label);
            m_editor.set_text(m_label.get_text());
            m_editor.set_hexpand(true);
            append(m_editor);

            Glib::signal_timeout().connect_once([this]() {
                m_editor.start_editing();
            }, 10);
        }
    });
    add_controller(click);
}

void CardRow::set_text(std::string_view text) {
    m_label.set_text(std::string(text));
    m_label.queue_resize();
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
