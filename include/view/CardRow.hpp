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
    Gtk::Label         m_label;
    Gtk::EditableLabel m_editor;
    
    bool m_is_editing = false;
    sigc::slot<void(std::string_view)> m_on_changed;
public:
    CardRow(std::string_view initial_text, sigc::slot<void(std::string_view)> on_changed);
    ~CardRow() override = default;
    // Explicit public accessor so factories can pass text down cleanly!
    void set_text(std::string_view text);

    // Toggles the glowing-border/recolored-text look. Not wired to
    // anything yet — the shared "which task is active" state that will
    // call this from TreePanel/TaskPanel/SchedulePanel is separate,
    // later work. This just adds the visual capability on its own.
    void set_active(bool active);
};
#endif
