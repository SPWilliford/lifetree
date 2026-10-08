#include "view/DayPage.hpp"

#include <ctime>
#include <string>

#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerfocus.h>

#include "core/Clock.hpp"
#include "core/Day.hpp"

namespace {
constexpr int COLUMN_WIDTH = 720;
constexpr int SAVE_DELAY_MS = 600;

Gtk::Label* heading(const char* text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("heading");
    label->set_xalign(0.0);
    return label;
}
}  // namespace

DayPage::DayPage(Day& day) : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_day(day) {
    set_hexpand(true);
    set_vexpand(true);

    m_date.add_css_class("title-2");
    m_date.set_xalign(0.0);
    m_column.append(m_date);

    // --- working day ---
    m_column.append(*heading("Working day"));

    auto* hours = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    for (auto* entry : {&m_start_entry, &m_end_entry}) {
        entry->set_placeholder_text("--:--");
        entry->set_max_width_chars(6);
        entry->set_width_chars(6);
        entry->signal_activate().connect(sigc::mem_fun(*this, &DayPage::apply_hours));
    }
    hours->append(m_start_entry);
    hours->append(*Gtk::make_managed<Gtk::Label>("to"));
    hours->append(m_end_entry);

    auto* set_button = Gtk::make_managed<Gtk::Button>("Set");
    set_button->signal_clicked().connect(sigc::mem_fun(*this, &DayPage::apply_hours));
    hours->append(*set_button);
    m_column.append(*hours);

    // --- notes ---
    auto* notes_heading = heading("Notes");
    notes_heading->set_margin_top(12);
    m_column.append(*notes_heading);

    m_notes.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_notes.set_left_margin(10);
    m_notes.set_right_margin(10);
    m_notes.set_top_margin(8);
    m_notes.set_bottom_margin(8);
    m_notes.get_buffer()->set_text(m_day.notes());
    m_notes.get_buffer()->signal_changed().connect(sigc::mem_fun(*this, &DayPage::schedule_save));

    auto focus = Gtk::EventControllerFocus::create();
    focus->signal_leave().connect(sigc::mem_fun(*this, &DayPage::save_notes));
    m_notes.add_controller(focus);

    m_notes_scroll.set_child(m_notes);
    m_notes_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_notes_scroll.set_vexpand(true);
    m_notes_scroll.add_css_class("panel-right");
    m_column.append(m_notes_scroll);

    m_column.set_size_request(COLUMN_WIDTH, -1);
    m_column.set_halign(Gtk::Align::CENTER);
    m_column.set_vexpand(true);
    m_column.set_margin(16);
    append(m_column);

    // Focus lands in the notes, not the first entry: a stray key would
    // otherwise replace the start time.
    signal_map().connect([this]() {
        refresh();
        Glib::signal_idle().connect_once([this]() {
            m_notes.grab_focus();
            m_start_entry.select_region(0, 0);
        });
    });
    signal_unmap().connect(sigc::mem_fun(*this, &DayPage::save_notes));
    refresh();
}

DayPage::~DayPage() {
    save_notes();
}

void DayPage::refresh() {
    std::tm tm_buf = clock_util::local_tm(std::time(nullptr));
    char date[64];
    std::strftime(date, sizeof(date), "%A, %B ", &tm_buf);
    m_date.set_text(std::string(date) + std::to_string(tm_buf.tm_mday));

    const DayHoursRow hours = m_day.hours();
    m_start_entry.set_text(hours.defined() ? clock_util::format_hhmm(hours.start_minutes) : "");
    m_end_entry.set_text(hours.defined() ? clock_util::format_hhmm(hours.end_minutes) : "");
}

void DayPage::apply_hours() {
    const int start = clock_util::parse_hhmm(m_start_entry.get_text());
    const int end = clock_util::parse_hhmm(m_end_entry.get_text());

    // Both or neither: a start with no end has no denominator.
    if (start != DayHoursRow::NO_HOURS && end != DayHoursRow::NO_HOURS) {
        m_day.set_hours(start, end);
    }
    refresh();
}

void DayPage::schedule_save() {
    m_save_pending.disconnect();
    m_save_pending = Glib::signal_timeout().connect(
        [this]() {
            save_notes();
            return false;
        },
        SAVE_DELAY_MS);
}

void DayPage::save_notes() {
    m_save_pending.disconnect();
    m_day.set_notes(m_notes.get_buffer()->get_text().raw());
}
