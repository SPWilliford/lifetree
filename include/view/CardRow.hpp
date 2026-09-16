#ifndef CARDROW_HPP
#define CARDROW_HPP

#include <string>
#include <string_view>

#include <gtkmm/box.h>
#include <gtkmm/editablelabel.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <sigc++/sigc++.h>

// One tree row: an optional leading icon, an editable title, and an optional
// trailing marker. Double-click edits; right-click fires
// signal_secondary_clicked. In action mode the row is a button instead.
class CardRow : public Gtk::Box {
public:
    explicit CardRow(sigc::slot<void(std::string_view)> on_changed);
    ~CardRow() override = default;

    void set_text(std::string_view text);

    // Tints the title; "" clears it.
    void set_color(const std::string& hex_color);

    // Trailing decoration, never part of the editable text; "" hides it.
    void set_marker(std::string_view text);

    // Icon theme name; "" hides it.
    void set_icon(std::string_view icon_name);

    // A button rather than data: single click fires signal_activated and
    // double-click no longer edits.
    void set_action(bool action);

    // Opens the editor as a double-click would.
    void begin_edit();

    sigc::signal<void()> signal_secondary_clicked() { return m_secondary_clicked; }
    sigc::signal<void()> signal_activated() { return m_activated; }

private:
    Gtk::Image m_icon;
    Gtk::Label m_label;
    Gtk::EditableLabel m_editor;
    Gtk::Label m_marker;

    // hexpand spacer so the marker stays beside the title while the row
    // still fills its width for the click gestures.
    Gtk::Label m_spacer;

    // Text and color kept apart from the label: coloring goes through
    // set_markup, so both are needed to rebuild it when either changes.
    std::string m_current_text;
    std::string m_current_color;
    void render_label();

    bool m_is_editing = false;
    bool m_is_action = false;

    sigc::slot<void(std::string_view)> m_on_changed;
    sigc::signal<void()> m_secondary_clicked;
    sigc::signal<void()> m_activated;
};

#endif
