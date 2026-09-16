#include "view/ReviewPage.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <glibmm/markup.h>

#include "core/Clock.hpp"
#include "core/Priority.hpp"
#include "core/Tree.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"
#include "view/Style.hpp"

namespace {

// "2h 05m", or "12m" under the hour.
std::string duration_text(double seconds) {
    const long total = static_cast<long>(seconds + 0.5);
    char buf[32];
    if (total >= 3600) {
        std::snprintf(buf, sizeof(buf), "%ldh %02ldm", total / 3600, (total % 3600) / 60);
    } else {
        std::snprintf(buf, sizeof(buf), "%ldm", total / 60);
    }
    return buf;
}

std::string percent_text(double value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f%%", value);
    return buf;
}

Gtk::Label* dim_label(const std::string& text, Gtk::Align align) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_halign(align);
    return label;
}

// Days are stepped from noon so a 23- or 25-hour DST day can't land back in
// the same day or skip one.
constexpr int NOON_HOUR = 12;

constexpr int MEASURE = 820;

}  // namespace

ReviewPage::ReviewPage(TreeController& life, Priority& priority, Work& work)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12),
      m_life(life),
      m_priority(priority),
      m_work(work),
      m_day(std::time(nullptr)),
      m_prev_day("◀"),
      m_next_day("▶"),
      m_today("Today") {
    set_hexpand(true);
    set_vexpand(true);

    for (auto* button : {&m_prev_day, &m_next_day, &m_today}) button->set_has_frame(false);

    m_day_label.set_width_chars(22);
    m_day_label.set_halign(Gtk::Align::CENTER);

    m_day_bar.set_halign(Gtk::Align::CENTER);
    m_day_bar.append(m_prev_day);
    m_day_bar.append(m_day_label);
    m_day_bar.append(m_next_day);
    m_day_bar.append(m_today);
    append(m_day_bar);

    m_prev_day.signal_clicked().connect([this]() { step_day(-1); });
    m_next_day.signal_clicked().connect([this]() { step_day(1); });
    m_today.signal_clicked().connect([this]() {
        m_day = std::time(nullptr);
        m_refresh.request();
    });

    m_content.set_halign(Gtk::Align::CENTER);
    m_content.set_size_request(MEASURE, -1);
    m_content.set_margin_top(8);
    m_scroll.set_child(m_content);
    m_scroll.set_hexpand(true);
    m_scroll.set_vexpand(true);
    m_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    append(m_scroll);

    m_work.connect_changed([this]() { m_refresh.request(); });
    m_priority.connect_changed([this]() { m_refresh.request(); });
    m_life.connect_changed([this]() { m_refresh.request(); });

    rebuild();
}

void ReviewPage::step_day(int days) {
    std::tm tm_buf = clock_util::local_tm(m_day);
    tm_buf.tm_hour = NOON_HOUR;
    tm_buf.tm_min = 0;
    tm_buf.tm_sec = 0;
    tm_buf.tm_mday += days;
    tm_buf.tm_isdst = -1;
    m_day = std::mktime(&tm_buf);
    m_refresh.request();
}

void ReviewPage::rebuild() {
    const std::tm tm_buf = clock_util::local_tm(m_day);
    char date[64];
    std::strftime(date, sizeof(date), "%A, %B %e", &tm_buf);
    m_day_label.set_text(date);

    const std::tm now_tm = clock_util::local_tm(std::time(nullptr));
    const bool on_today = (tm_buf.tm_year == now_tm.tm_year && tm_buf.tm_yday == now_tm.tm_yday);
    m_next_day.set_sensitive(!on_today);
    m_today.set_sensitive(!on_today);

    while (auto* child = m_content.get_first_child()) m_content.remove(*child);

    build_finished();
    build_attribution();
    build_unserved();
}

Gtk::Box& ReviewPage::append_section(const std::string& title, const std::string& subtitle) {
    auto* section = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    section->set_hexpand(true);

    auto* heading = Gtk::make_managed<Gtk::Label>();
    heading->set_markup("<b>" + Glib::Markup::escape_text(title) + "</b>");
    heading->set_halign(Gtk::Align::START);
    section->append(*heading);

    if (!subtitle.empty()) {
        auto* sub = dim_label(subtitle, Gtk::Align::START);
        sub->set_wrap(true);
        sub->set_margin_bottom(6);
        section->append(*sub);
    }

    m_content.append(*section);
    return *section;
}

Gtk::Widget& ReviewPage::make_path_title(const std::string& path, const std::string& title,
                                         const std::string& color) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    box->set_hexpand(true);
    box->set_halign(Gtk::Align::START);

    if (!path.empty()) {
        auto* path_label = Gtk::make_managed<Gtk::Label>();
        path_label->set_halign(Gtk::Align::START);
        style::set_colored_text(*path_label, path + " - ", color);
        if (color.empty()) path_label->add_css_class("dim-label");
        box->append(*path_label);
    }

    auto* title_label = Gtk::make_managed<Gtk::Label>();
    title_label->set_halign(Gtk::Align::START);
    style::set_colored_text(*title_label, title, color);
    box->append(*title_label);

    return *box;
}

Gtk::Grid& ReviewPage::make_rows(Gtk::Box& section) {
    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(2);
    grid->set_column_spacing(16);
    grid->set_hexpand(true);
    section.append(*grid);
    return *grid;
}

void ReviewPage::build_finished() {
    auto& section = append_section(
        "Finished", "Tasks completed on this day, with the time banked against each.");

    const auto entries = m_work.entries_for_completed_day(m_day);
    if (entries.empty()) {
        section.append(*dim_label("Nothing completed.", Gtk::Align::START));
        return;
    }

    auto& grid = make_rows(section);

    int row = 0;
    long total = 0;
    for (const auto& entry : entries) {
        grid.attach(make_path_title(entry.path, entry.title, entry.color), 0, row);
        grid.attach(
            *dim_label(duration_text(static_cast<double>(entry.total_seconds)), Gtk::Align::END), 1,
            row);
        total += entry.total_seconds;
        ++row;
    }

    char summary[96];
    std::snprintf(summary, sizeof(summary), "%d finished, %s banked.",
                  static_cast<int>(entries.size()),
                  duration_text(static_cast<double>(total)).c_str());
    auto* footer = dim_label(summary, Gtk::Align::END);
    footer->set_margin_top(6);
    section.append(*footer);
}

std::unordered_map<int, double> ReviewPage::seconds_by_leaf(double& unattributed) const {
    std::unordered_map<int, double> out;
    unattributed = 0.0;

    for (const auto& entry : m_work.entries_for_day(m_day)) {
        const double seconds = static_cast<double>(entry.end_time - entry.start_time);
        if (seconds <= 0.0) continue;

        if (entry.project_root_id < 0) {
            unattributed += seconds;
            continue;
        }

        // Normalized against the shares present, not TOTAL: a link whose
        // leaf gained children is skipped by Priority, and the whole segment
        // should still be attributed.
        const auto leaves = m_priority.leaves_for(entry.project_root_id);
        double present = 0.0;
        for (int leaf : leaves) present += m_priority.project_share(entry.project_root_id, leaf);

        if (leaves.empty() || present <= 0.0) {
            unattributed += seconds;
            continue;
        }

        for (int leaf : leaves) {
            const double share = m_priority.project_share(entry.project_root_id, leaf);
            if (share > 0.0) out[leaf] += seconds * share / present;
        }
    }

    return out;
}

int ReviewPage::top_branch_of(int leaf_id) const {
    if (!m_life.contains(leaf_id)) return -1;

    int current = leaf_id;
    for (int parent = m_life.parent_of(current); parent > Tree::ROOT_ID;
         parent = m_life.parent_of(current)) {
        current = parent;
    }
    return current;
}

void ReviewPage::build_attribution() {
    auto& section = append_section("Where the time went",
                                   "Worked time pushed back through each project's link shares, "
                                   "against the share of the whole you gave that branch.");

    double unattributed = 0.0;
    const auto by_leaf = seconds_by_leaf(unattributed);

    std::unordered_map<int, double> by_branch;
    double tracked = unattributed;
    for (const auto& [leaf, seconds] : by_leaf) {
        tracked += seconds;
        const int branch = top_branch_of(leaf);
        if (branch < 0) {
            unattributed += seconds;  // leaf deleted since the work was logged
            continue;
        }
        by_branch[branch] += seconds;
    }

    if (tracked <= 0.0) {
        section.append(*dim_label("No time tracked on this day.", Gtk::Align::START));
        return;
    }

    const auto intended = m_priority.priorities();

    // Every top-level branch, worked or not: a branch at zero is the finding.
    std::vector<std::pair<int, double>> rows;
    for (int id : m_life.children_of(Tree::ROOT_ID)) {
        auto it = by_branch.find(id);
        rows.emplace_back(id, it != by_branch.end() ? it->second : 0.0);
    }
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    auto& grid = make_rows(section);

    int row = 0;
    for (const auto& [id, seconds] : rows) {
        auto* title = Gtk::make_managed<Gtk::Label>(m_life.display_title(id));
        title->set_halign(Gtk::Align::START);
        title->set_hexpand(true);
        grid.attach(*title, 0, row);

        grid.attach(*dim_label(duration_text(seconds), Gtk::Align::END), 1, row);

        auto* got = Gtk::make_managed<Gtk::Label>(percent_text(100.0 * seconds / tracked));
        got->set_halign(Gtk::Align::END);
        grid.attach(*got, 2, row);

        auto it = intended.find(id);
        const double want = (it != intended.end()) ? it->second : 0.0;
        grid.attach(*dim_label("of " + percent_text(want) + " intended", Gtk::Align::END), 3, row);
        ++row;
    }

    if (unattributed > 0.0) {
        auto* title = Gtk::make_managed<Gtk::Label>("Not attached to any goal");
        title->set_halign(Gtk::Align::START);
        title->set_hexpand(true);
        title->add_css_class("dim-label");
        grid.attach(*title, 0, row);

        grid.attach(*dim_label(duration_text(unattributed), Gtk::Align::END), 1, row);
        grid.attach(*dim_label(percent_text(100.0 * unattributed / tracked), Gtk::Align::END), 2,
                    row);
    }
}

void ReviewPage::build_unserved() {
    auto& section = append_section("Nothing is serving these",
                                   "Goals carrying priority that no project claims.");

    if (m_life.leaves().empty()) {
        section.append(*dim_label("The life tree has no goals yet.", Gtk::Align::START));
        return;
    }

    const auto unserved = m_priority.unserved_leaves();
    if (unserved.empty()) {
        section.append(*dim_label("Every goal has at least one project.", Gtk::Align::START));
        return;
    }

    auto& grid = make_rows(section);

    int row = 0;
    double total = 0.0;
    for (const auto& [id, share] : unserved) {
        grid.attach(make_path_title(m_life.ancestor_path(id), m_life.display_title(id), ""), 0,
                    row);

        auto* value = Gtk::make_managed<Gtk::Label>(percent_text(share));
        value->set_halign(Gtk::Align::END);
        grid.attach(*value, 1, row);

        total += share;
        ++row;
    }

    auto* footer =
        dim_label(percent_text(total) + " of what you said matters is unserved.", Gtk::Align::END);
    footer->set_margin_top(6);
    section.append(*footer);
}
