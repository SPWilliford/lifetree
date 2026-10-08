#ifndef DAYPAGE_HPP
#define DAYPAGE_HPP

#include <gtkmm/box.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>

#include <sigc++/connection.h>

class Day;

// Today, stepped out of the daily screen: the date, the working hours, and
// notes kept across days.
class DayPage : public Gtk::Box {
public:
    explicit DayPage(Day& day);
    ~DayPage() override;

private:
    Day& m_day;

    Gtk::Box m_column{Gtk::Orientation::VERTICAL, 12};
    Gtk::Label m_date;

    Gtk::Entry m_start_entry;
    Gtk::Entry m_end_entry;

    Gtk::TextView m_notes;
    Gtk::ScrolledWindow m_notes_scroll;

    // Notes save after a pause in typing, on leaving the field, and on close.
    sigc::connection m_save_pending;
    void schedule_save();
    void save_notes();

    // Rereads the date and hours; the page can stay open across midnight.
    void refresh();
    void apply_hours();
};

#endif
