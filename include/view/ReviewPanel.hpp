#ifndef REVIEWPANEL_HPP
#define REVIEWPANEL_HPP
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

// What happened on a given day, against what you said mattered: what got
// finished, where the time went, and what nothing is serving.
//
// The middle section is the only reader of project_share — it pushes logged
// time back through each project's links onto life branches.
//
// Everything is derived per refresh; no summary row exists to drift out of
// step with the work log.
class ReviewPanel : public Gtk::Box {
private:
    TreeController& m_life;
    Priority& m_priority;
    Work& m_work;

    // Any instant inside the day. Both Work queries resolve the civil day
    // themselves, so this never needs normalizing to midnight.
    time_t m_day;

    Gtk::Box m_day_bar{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_prev_day;
    Gtk::Button m_next_day;
    Gtk::Button m_today;
    Gtk::Label m_day_label;

    Gtk::ScrolledWindow m_scroll;

    // A fixed measure, centered: across a maximized window the two ends of
    // a row stop reading as the same row.
    Gtk::Box m_content{Gtk::Orientation::VERTICAL, 28};

    // Nothing below m_content is a member: a refresh empties it and builds
    // the sections again, all make_managed, so no widget's lifetime spans
    // two rebuilds. The sets are small, and a rebuild can't leave a row
    // pointing at a node that's gone.
    void rebuild();

    // A grid ready to take a section's rows, already appended to it.
    Gtk::Grid& make_rows(Gtk::Box& section);

    void build_finished();
    void build_attribution();
    void build_unserved();

    // Split across leaves by each project's link shares. Time that can't be
    // attributed lands in the out-parameter rather than being dropped:
    // unattributed time is a finding, not an error.
    std::unordered_map<int, double> seconds_by_leaf(double& unattributed) const;

    // The top-level branch a leaf sits under, or -1 if it's gone. Time
    // rolls up to branches because a leaf's slice of a day is usually
    // minutes.
    int top_branch_of(int leaf_id) const;

    // Returns the box the section's rows go into, so its parts sit tightly
    // together and the wide gap falls only between sections.
    Gtk::Box& append_section(const std::string& title, const std::string& subtitle);

    // One cell, so the gap between them is the "- " and not a grid column
    // stretched to the widest path.
    Gtk::Widget& make_path_title(const std::string& path, const std::string& title,
                                 const std::string& color);

    void step_day(int days);

public:
    ReviewPanel(TreeController& life, Priority& priority, Work& work);
    ~ReviewPanel() override = default;

private:
    // Declared last so the mem_fun above names something already visible.
    Refresh m_refresh{sigc::mem_fun(*this, &ReviewPanel::rebuild)};
};

#endif
