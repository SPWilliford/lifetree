#ifndef REVIEWPAGE_HPP
#define REVIEWPAGE_HPP

#include <ctime>
#include <string>
#include <unordered_map>

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include "view/Refresh.hpp"

class TreeController;
class Priority;
class Work;

// One day against what you said mattered. Everything is derived per
// refresh; nothing is stored.
class ReviewPage : public Gtk::Box {
public:
    ReviewPage(TreeController& life, Priority& priority, Work& work);
    ~ReviewPage() override = default;

private:
    TreeController& m_life;
    Priority& m_priority;
    Work& m_work;

    // Any instant inside the day being reviewed.
    time_t m_day;

    Gtk::Box m_day_bar{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_prev_day;
    Gtk::Button m_next_day;
    Gtk::Button m_today;
    Gtk::Label m_day_label;
    Gtk::ScrolledWindow m_scroll;
    Gtk::Box m_content{Gtk::Orientation::VERTICAL, 28};

    // Empties m_content and builds every section again with managed
    // widgets, so nothing below it is a member.
    void rebuild();

    Gtk::Box& append_section(const std::string& title);
    Gtk::Grid& make_rows(Gtk::Box& section);
    Gtk::Widget& make_path_title(const std::string& path, const std::string& title,
                                 const std::string& color);

    void build_finished();
    void build_attribution();

    // The day's worked seconds split across leaves by project_share. Time
    // that can't be attributed lands in the out-parameter.
    std::unordered_map<int, double> seconds_by_leaf(double& unattributed) const;

    void step_day(int days);

    Refresh m_refresh{sigc::mem_fun(*this, &ReviewPage::rebuild)};
};

#endif
