#ifndef CARDROW_HPP
#define CARDROW_HPP
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/editablelabel.h>
#include <sigc++/sigc++.h>
#include <string_view>
class CardRow : public Gtk::Box {
private:
    // Stack-allocate your internal elements so they are perfectly memory-safe!
    Gtk::Label         m_marker; // e.g. the generator icon — decoration only, never part of the editable text
    Gtk::Label         m_label;
    Gtk::EditableLabel m_editor;
    
    bool m_is_editing = false;
    sigc::slot<void(std::string_view)> m_on_changed;
    sigc::signal<void()> m_secondary_clicked;
public:
    CardRow(std::string_view initial_text, sigc::slot<void(std::string_view)> on_changed);
    ~CardRow() override = default;
    // Explicit public accessor so factories can pass text down cleanly!
    void set_text(std::string_view text);

    // A small decoration shown before the title — empty hides it. Kept
    // entirely separate from m_label/m_editor on purpose: the editor
    // seeds itself from whatever the label currently shows, so anything
    // baked into the title text risks getting saved back as if it were
    // really part of the title. This never touches that text at all.
    void set_marker(std::string_view emoji);

    // Fired on right-click. Deliberately generic — CardRow doesn't know
    // what a right-click should *do* (that's the repeating-task menu,
    // today, but this shouldn't need to change if that grows or a
    // second thing wants a context menu later); the owner decides.
    sigc::signal<void()> signal_secondary_clicked() { return m_secondary_clicked; }

    // Toggles the glowing-border/recolored-text look. Not wired to
    // anything yet — the shared "which task is active" state that will
    // call this from TreePanel/TaskPanel/SchedulePanel is separate,
    // later work. This just adds the visual capability on its own.
    void set_active(bool active);
};
#endif
