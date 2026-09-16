#include "core/TaskAttributes.hpp"

#include <algorithm>
#include <ctime>

#include "core/Clock.hpp"
#include "core/TreeController.hpp"

TaskAttributes::TaskAttributes(std::shared_ptr<Database> db, TreeController& projects)
    : m_db(std::move(db)), m_projects(projects) {}

void TaskAttributes::load() {
    m_repeats.clear();
    for (auto& row : m_db->load_repeated_tasks()) m_repeats[row.node_id] = row;

    m_sequential.clear();
    for (int id : m_db->load_sequential()) m_sequential.insert(id);

    m_containers.clear();
    for (int id : m_db->load_containers()) m_containers.insert(id);

    m_dates.clear();
    for (auto& row : m_db->load_task_dates()) m_dates[row.node_id] = row;

    m_project_colors.clear();
    for (auto& row : m_db->load_project_colors()) m_project_colors[row.project_root_id] = row.color;

    // Blanked, not filled: the first read does the load.
    m_completions_date.clear();
    m_completions.clear();
}

// ---------------------------------------------------------------------
// Repeating
// ---------------------------------------------------------------------

bool TaskAttributes::has_own_repeat(int id) const {
    return m_repeats.find(id) != m_repeats.end();
}

RepeatedTaskRow TaskAttributes::repeat_settings(int id) const {
    auto it = m_repeats.find(id);
    return (it != m_repeats.end()) ? it->second : RepeatedTaskRow{};
}

RepeatedTaskRow TaskAttributes::governing_repeat(int id) const {
    if (m_repeats.empty()) return {};
    for (int current = id; current > 0; current = m_projects.parent_of(current)) {
        auto it = m_repeats.find(current);
        if (it != m_repeats.end()) return it->second;
    }
    return {};
}

bool TaskAttributes::recurs(int id) const {
    return governing_repeat(id).node_id != -1;
}

int TaskAttributes::repeat_root_of(int id) const {
    if (m_repeats.empty()) return -1;
    int root = -1;
    for (int current = id; current > 0; current = m_projects.parent_of(current)) {
        if (has_own_repeat(current)) root = current;
    }
    return root;
}

bool TaskAttributes::is_repeat_root(int id) const {
    return has_own_repeat(id) && repeat_root_of(id) == id;
}

void TaskAttributes::apply_repeat(int id, int weekday_mask, int count_per_day) {
    const bool had_own = has_own_repeat(id);
    const RepeatedTaskRow own = repeat_settings(id);

    // Only a changed mask cascades; editing the count alone must not
    // discard the overrides below.
    const bool mask_changed = !had_own || own.weekday_mask != weekday_mask;

    const int parent = m_projects.parent_of(id);
    const RepeatedTaskRow inherited = (parent > 0) ? governing_repeat(parent) : RepeatedTaskRow{};

    if (inherited.node_id != -1 && inherited.weekday_mask == weekday_mask) {
        if (had_own) {
            if (!m_db->remove_repeated_task(id)) return;
            m_repeats.erase(id);
        }
    } else {
        if (!m_db->insert_repeated_task(id, weekday_mask, count_per_day)) return;
        m_repeats[id] = RepeatedTaskRow{id, weekday_mask, count_per_day};
    }

    if (mask_changed) clear_repeats_below(id);
    m_changed.emit();
}

void TaskAttributes::clear_repeats_below(int id) {
    std::vector<int> pending = m_projects.children_of(id);
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();

        if (has_own_repeat(current) && m_db->remove_repeated_task(current)) {
            m_repeats.erase(current);
        }
        for (int child : m_projects.children_of(current)) pending.push_back(child);
    }
}

void TaskAttributes::unmark_repeating(int id) {
    if (!m_db->remove_repeated_task(id)) return;
    m_repeats.erase(id);

    // Overrides go too, or the highest survivor becomes a repeat root.
    clear_repeats_below(id);
    m_changed.emit();
}

// ---------------------------------------------------------------------
// Completions
// ---------------------------------------------------------------------

void TaskAttributes::ensure_today() const {
    const std::string now_date = clock_util::today();
    if (m_completions_date == now_date) return;

    m_completions.clear();
    for (const auto& row : m_db->load_completions_for_date(now_date)) {
        m_completions[row.node_id]++;
    }
    m_completions_date = now_date;
}

void TaskAttributes::record_completion(int id) {
    const TaskSnapshot snap = snapshot(id);
    if (snap.title.empty() && !m_projects.contains(id)) return;

    ensure_today();

    TaskCompletionRow row;
    row.node_id = id;
    row.date = m_completions_date;
    row.title = snap.title;
    row.path = snap.path;
    row.color = snap.color;
    row.project_root_id = snap.project_root_id;
    row.completed_at = std::time(nullptr);

    if (!m_db->insert_completion(row)) return;
    m_completions[id]++;
    m_changed.emit();
}

int TaskAttributes::completions_today(int id) const {
    ensure_today();
    auto it = m_completions.find(id);
    return (it != m_completions.end()) ? it->second : 0;
}

int TaskAttributes::target_count(int id) const {
    const RepeatedTaskRow rep = governing_repeat(id);
    if (rep.node_id == -1) return 1;
    // A stored 0 would make the task permanently done.
    return std::max(1, rep.count_per_day);
}

bool TaskAttributes::satisfied_today(int id) const {
    if (!recurs(id)) return false;
    return completions_today(id) >= target_count(id);
}

// ---------------------------------------------------------------------
// Ordering, containers
// ---------------------------------------------------------------------

void TaskAttributes::mark_sequential(int id) {
    if (!m_db->insert_sequential(id)) return;
    m_sequential.insert(id);
    m_changed.emit();
}

void TaskAttributes::unmark_sequential(int id) {
    if (!m_db->remove_sequential(id)) return;
    m_sequential.erase(id);
    m_changed.emit();
}

bool TaskAttributes::is_sequential(int id) const {
    return m_sequential.count(id) > 0;
}

bool TaskAttributes::is_container(int id) const {
    return m_containers.count(id) > 0;
}

void TaskAttributes::mark_container(int id) {
    bool touched = false;
    for (int current = id; current > 0; current = m_projects.parent_of(current)) {
        if (is_container(current)) continue;
        if (!m_db->insert_container(current)) continue;
        m_containers.insert(current);
        touched = true;
    }
    if (touched) m_changed.emit();
}

void TaskAttributes::mark_action(int id) {
    // Whole subtree rather than stopping at the first action, so a
    // container that got under one is cleared.
    bool touched = false;
    std::vector<int> pending{id};
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();

        if (is_container(current) && m_db->remove_container(current)) {
            m_containers.erase(current);
            touched = true;
        }
        for (int child : m_projects.children_of(current)) pending.push_back(child);
    }
    if (touched) m_changed.emit();
}

// ---------------------------------------------------------------------
// Dates
// ---------------------------------------------------------------------

bool TaskAttributes::has_date(int id) const {
    return m_dates.count(id) > 0;
}

bool TaskAttributes::date_is_today(int id) const {
    auto it = m_dates.find(id);
    return it != m_dates.end() && it->second.date == clock_util::today();
}

bool TaskAttributes::date_has_passed(int id) const {
    auto it = m_dates.find(id);
    if (it == m_dates.end()) return false;
    const TaskDateRow& row = it->second;

    const std::string now_date = clock_util::today();
    if (row.date < now_date) return true;
    if (row.date > now_date) return false;

    if (row.time_start == TaskDateRow::NO_TIME) return false;
    return clock_util::minutes_now() >= row.time_start;
}

std::vector<TaskDateRow> TaskAttributes::scheduled_today() const {
    std::vector<TaskDateRow> out;
    const std::string now_date = clock_util::today();

    for (const auto& [node_id, row] : m_dates) {
        if (row.date != now_date) continue;
        if (row.time_start == TaskDateRow::NO_TIME) continue;
        if (!m_projects.contains(node_id)) continue;
        out.push_back(row);
    }

    std::sort(out.begin(), out.end(), [](const TaskDateRow& a, const TaskDateRow& b) {
        return a.time_start < b.time_start;
    });
    return out;
}

TaskDateRow TaskAttributes::date_settings(int id) const {
    auto it = m_dates.find(id);
    return (it == m_dates.end()) ? TaskDateRow{} : it->second;
}

bool TaskAttributes::is_available(int id) const {
    if (m_dates.empty()) return true;

    const std::string now_date = clock_util::today();
    const int now_minutes = clock_util::minutes_now();

    for (int current = id; current > 0; current = m_projects.parent_of(current)) {
        auto it = m_dates.find(current);
        if (it == m_dates.end()) continue;
        const TaskDateRow& row = it->second;

        if (row.date > now_date) return false;
        if (row.date == now_date && row.time_start != TaskDateRow::NO_TIME &&
            now_minutes < row.time_start) {
            return false;
        }
    }
    return true;
}

void TaskAttributes::set_date(int id, const std::string& date, int time_start, int time_end) {
    if (!m_db->set_task_date(id, date, time_start, time_end)) return;

    TaskDateRow row;
    row.node_id = id;
    row.date = date;
    row.time_start = time_start;
    row.time_end = time_end;
    m_dates[id] = row;
    m_changed.emit();
}

void TaskAttributes::clear_date(int id) {
    if (!m_db->clear_task_date(id)) return;
    m_dates.erase(id);
    m_changed.emit();
}

// ---------------------------------------------------------------------
// Derived views
// ---------------------------------------------------------------------

std::unordered_set<int> TaskAttributes::eligible_today() const {
    const auto blocked = blocked_tasks();
    const int day_bit = clock_util::today_weekday_bit();

    std::unordered_set<int> out;
    for (int id : m_projects.leaves()) {
        // Leaf and action differ: a container becomes a leaf the moment its
        // last child is completed.
        if (is_container(id)) continue;
        if (!is_available(id)) continue;

        const RepeatedTaskRow rep = governing_repeat(id);
        if (rep.node_id != -1) {
            if ((rep.weekday_mask & day_bit) == 0) continue;
            if (completions_today(id) >= std::max(1, rep.count_per_day)) continue;
        }

        if (blocked.count(id) > 0) continue;
        out.insert(id);
    }
    return out;
}

std::unordered_set<int> TaskAttributes::blocked_tasks() const {
    std::unordered_set<int> blocked;
    for (int node_id : m_sequential) {
        if (!m_projects.contains(node_id)) continue;
        bool found_first = false;
        collect_blocked(node_id, found_first, blocked);
    }
    return blocked;
}

void TaskAttributes::collect_blocked(int node_id, bool& found_first,
                                     std::unordered_set<int>& out) const {
    // Depth-first in position order, so "first" is first as the tree reads.
    for (int child : m_projects.children_of(node_id)) {
        const bool child_is_leaf = m_projects.children_of(child).empty();

        if (child_is_leaf && !is_container(child)) {
            // TRAP: this skip is what advances a recurring routine. A
            // completed recurring step stays in the tree; without it the
            // first step holds the lock all day.
            if (satisfied_today(child)) continue;

            if (found_first) {
                out.insert(child);
            } else {
                found_first = true;
            }
            continue;
        }
        collect_blocked(child, found_first, out);
    }
}

int TaskAttributes::project_root_of(int id) const {
    if (!m_projects.contains(id)) return -1;

    int current = id;
    int parent = m_projects.parent_of(current);
    while (parent > 0) {
        current = parent;
        parent = m_projects.parent_of(current);
    }
    return (parent == 0) ? current : -1;
}

std::string TaskAttributes::get_color(int id) const {
    auto it = m_project_colors.find(project_root_of(id));
    return (it != m_project_colors.end()) ? it->second : "";
}

TaskSnapshot TaskAttributes::snapshot(int id) const {
    if (!m_projects.contains(id)) return {};
    return {m_projects.display_title(id), m_projects.ancestor_path(id), get_color(id),
            project_root_of(id)};
}

void TaskAttributes::set_project_color(int project_root_id, const std::string& color) {
    if (!m_db->set_project_color(project_root_id, color)) return;
    m_project_colors[project_root_id] = color;
    m_changed.emit();
}

void TaskAttributes::clear_project_color(int project_root_id) {
    if (!m_db->clear_project_color(project_root_id)) return;
    m_project_colors.erase(project_root_id);
    m_changed.emit();
}
