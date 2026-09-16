#ifndef CARDROW_HPP
#define CARDROW_HPP
#include <string>
#include <string_view>

#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/editablelabel.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <sigc++/sigc++.h>
// One row of a tree: an optional icon, a marker either side of an editable
// title, and an optional trailing weight spin.
class CardRow : public Gtk::Box {
private:
    // Ahead of everything including the leading marker: an icon stands for
    // the row as a whole, not for its title.
    Gtk::Image m_icon;

    // Decoration only, never part of the editable text. Two so a marker can
    // sit either side without reordering the box; only one is ever shown.
    Gtk::Label m_marker;        // before the title
    Gtk::Label m_marker_after;  // after it

    // Empty, hexpand — soaks up spare width so the trailing marker stays
    // next to the title while the row still spans its full width.
    Gtk::Label m_spacer;
    Gtk::Label m_label;
    Gtk::EditableLabel m_editor;

    // Kept separately from what m_label displays: coloring goes through
    // set_markup(), so text and color must both be known to rebuild it,
    // whichever one changed.
    std::string m_current_text;
    std::string m_current_color;  // "" = no color, plain text
    void render_label();

    bool m_is_editing = false;
    bool m_is_action = false;
    sigc::slot<void(std::string_view)> m_on_changed;
    sigc::signal<void()> m_secondary_clicked;
    sigc::signal<void()> m_activated;

    // Always constructed, shown only on rows with a share to set.
    Gtk::SpinButton m_weight_spin;

    // Set while set_weight moves the control, so the value-changed handler
    // doesn't report a refresh as a user edit — which would write the
    // displayed value back and, with rounding, drift the tree.
    bool m_setting_weight = false;

    sigc::signal<void(double)> m_weight_changed;

public:
    CardRow(std::string_view initial_text, sigc::slot<void(std::string_view)> on_changed);
    ~CardRow() override = default;
    void set_text(std::string_view text);

    // Leading reads as a property of the row and indents the text; trailing
    // reads as a note about the title and doesn't.
    enum class MarkerSide { BEFORE, AFTER };

    // Empty hides it. Separate from m_label/m_editor because the editor
    // seeds from what the label shows — anything baked into the title text
    // would get saved back as part of the title.
    void set_marker(std::string_view emoji, MarkerSide side = MarkerSide::BEFORE);

    // Tints the title; "" clears it. set_markup is safe here where it isn't
    // for the marker: get_text() strips markup, so the edit flow that seeds
    // from it is unaffected.
    void set_color(const std::string& hex_color);

    // Fired on right-click. Generic: the owner decides what it means.
    sigc::signal<void()> signal_secondary_clicked() { return m_secondary_clicked; }

    // On the row rather than in a parallel panel because a weight only
    // means anything against its siblings, and those are the rows above and
    // below.
    //
    // set_weight moves the control without emitting, so a refresh can't be
    // mistaken for the user turning the dial. depth indents it to match the
    // title's nesting; siblings share a depth and so still line up.
    void set_weight(double weight, int depth);
    void hide_weight();
    sigc::signal<void(double)> signal_weight_changed() { return m_weight_changed; }

    // Looked up from the icon theme by name; "" hides it. Separate from
    // set_marker, which is a character in the title's font — symbolic icons
    // take their color from the surrounding text.
    void set_icon(std::string_view icon_name);

    // As a double-click would. Public so a just-created row can open ready
    // to be named.
    void begin_edit();

    // Left-click, action mode only — see set_action.
    sigc::signal<void()> signal_activated() { return m_activated; }

    // Turns the row into a button rather than data: click fires
    // signal_activated, double-click no longer edits. For rows that aren't
    // nodes — the trailing "+" — where renaming means nothing.
    void set_action(bool action);

    // The cross-panel active-task highlight. Not wired to anything yet:
    // the shared "which task is active" state is still to come.
    void set_active(bool active);
};
#endif
