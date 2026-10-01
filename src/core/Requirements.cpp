#include "core/Requirements.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

#include "core/TreeController.hpp"

namespace {

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trimmed(std::string_view text) {
    constexpr std::string_view SPACE = " \t\r\n";
    const auto first = text.find_first_not_of(SPACE);
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(SPACE);
    return text.substr(first, last - first + 1);
}

// The part of a leaf's priority its index-th requirement of count gets:
// count, count - 1, ... 1, scaled to sum to one.
double rank_weight(int index, int count) {
    const double total = count * (count + 1) / 2.0;
    return (count - index) / total;
}

}  // namespace

Requirements::Requirements(std::shared_ptr<Database> db, const TreeController& life,
                           const TreeController& projects)
    : m_db(std::move(db)), m_life(life), m_projects(projects) {}

void Requirements::load() {
    m_titles.clear();
    for (const auto& row : m_db->load_requirements()) m_titles[row.id] = row.title;

    m_by_leaf.clear();
    for (const auto& link : m_db->load_leaf_requirement_links()) {
        m_by_leaf[link.leaf_id].push_back(link.requirement_id);
    }

    m_projects_of.clear();
    for (const auto& link : m_db->load_requirement_project_links()) {
        m_projects_of[link.requirement_id].push_back(link.project_root_id);
    }
}

void Requirements::reload() {
    const auto titles = m_titles;
    const auto by_leaf = m_by_leaf;
    const auto projects_of = m_projects_of;
    load();
    if (titles != m_titles || by_leaf != m_by_leaf || projects_of != m_projects_of) {
        m_changed.emit();
    }
}

void Requirements::normalize() {
    std::set<int> branches;
    for (const auto& link : m_db->load_leaf_requirement_links()) {
        if (!m_life.children_of(link.leaf_id).empty()) branches.insert(link.leaf_id);
    }

    for (int branch : branches) {
        int leaf = branch;
        while (!m_life.children_of(leaf).empty()) leaf = m_life.children_of(leaf).front();
        m_db->move_leaf_requirement_links(branch, leaf);
    }

    m_db->remove_orphan_requirements();

    for (const auto& link : m_db->load_requirement_project_links()) {
        if (!is_top_level_project(link.project_root_id)) {
            m_db->clear_requirement_project_link(link.requirement_id, link.project_root_id);
        }
    }
    reload();
}

bool Requirements::is_leaf(int node_id) const {
    return node_id != Tree::ROOT_ID && m_life.contains(node_id) &&
           m_life.children_of(node_id).empty();
}

std::vector<int> Requirements::requirements_of(int leaf_id) const {
    auto it = m_by_leaf.find(leaf_id);
    return it != m_by_leaf.end() ? it->second : std::vector<int>{};
}

std::string Requirements::title(int requirement_id) const {
    auto it = m_titles.find(requirement_id);
    return it != m_titles.end() ? it->second : "";
}

int Requirements::add(int leaf_id, std::string_view title) {
    const std::string_view clean = trimmed(title);
    if (clean.empty() || !is_leaf(leaf_id)) return -1;

    const std::string key = lowered(clean);
    int existing = -1;
    for (const auto& [id, other] : m_titles) {
        if (lowered(other) != key) continue;
        if (existing == -1 || id < existing) existing = id;
    }
    if (existing != -1) {
        attach(leaf_id, existing);
        return existing;
    }

    const int id = m_db->insert_requirement(clean);
    if (id == -1) return -1;

    std::vector<int> order = requirements_of(leaf_id);
    order.push_back(id);
    write_order(leaf_id, order);
    reload();
    return id;
}

void Requirements::attach(int leaf_id, int requirement_id) {
    if (!is_leaf(leaf_id) || m_titles.count(requirement_id) == 0) return;

    std::vector<int> order = requirements_of(leaf_id);
    if (std::find(order.begin(), order.end(), requirement_id) != order.end()) return;
    order.push_back(requirement_id);
    write_order(leaf_id, order);
    reload();
}

void Requirements::detach(int leaf_id, int requirement_id) {
    if (!m_db->clear_leaf_requirement_link(leaf_id, requirement_id)) return;
    m_db->remove_orphan_requirements();
    reload();
}

void Requirements::rename(int requirement_id, std::string_view title) {
    const std::string_view clean = trimmed(title);
    if (clean.empty() || m_titles.count(requirement_id) == 0) return;
    if (!m_db->write_requirement_title(requirement_id, clean)) return;
    reload();
}

void Requirements::move(int leaf_id, int requirement_id, int index) {
    std::vector<int> order = requirements_of(leaf_id);
    auto it = std::find(order.begin(), order.end(), requirement_id);
    if (it == order.end()) return;

    order.erase(it);
    index = std::clamp(index, 0, static_cast<int>(order.size()));
    order.insert(order.begin() + index, requirement_id);
    write_order(leaf_id, order);
    reload();
}

std::vector<int> Requirements::suggestions(int leaf_id, std::string_view text) const {
    const std::string key = lowered(trimmed(text));
    if (key.empty()) return {};

    const std::vector<int> mine = requirements_of(leaf_id);

    // (doesn't start with key, lowered title, id)
    std::vector<std::tuple<bool, std::string, int>> matches;
    for (const auto& [id, title] : m_titles) {
        if (std::find(mine.begin(), mine.end(), id) != mine.end()) continue;
        std::string lower = lowered(title);
        const auto at = lower.find(key);
        if (at == std::string::npos) continue;
        matches.emplace_back(at != 0, std::move(lower), id);
    }
    std::sort(matches.begin(), matches.end());

    std::vector<int> out;
    out.reserve(matches.size());
    for (const auto& match : matches) out.push_back(std::get<2>(match));
    return out;
}

std::unordered_map<int, Requirements::Parts> Requirements::contributions(
    const std::unordered_map<int, double>& priorities) const {
    std::vector<int> leaves = m_life.leaves();
    std::sort(leaves.begin(), leaves.end());

    std::unordered_map<int, Parts> out;
    for (int leaf : leaves) {
        auto it = priorities.find(leaf);
        const double share = it != priorities.end() ? it->second : 0.0;

        const std::vector<int> ids = requirements_of(leaf);
        const int count = static_cast<int>(ids.size());
        for (int i = 0; i < count; ++i) {
            out[ids[i]].push_back({leaf, share * rank_weight(i, count)});
        }
    }
    return out;
}

std::vector<RankedRequirement> Requirements::ranked(
    const std::unordered_map<int, double>& priorities) const {
    std::vector<RankedRequirement> out;

    for (int leaf : m_life.leaves()) {
        if (!requirements_of(leaf).empty()) continue;
        auto it = priorities.find(leaf);
        out.push_back({-1, leaf, it != priorities.end() ? it->second : 0.0});
    }

    for (const auto& [id, parts] : contributions(priorities)) {
        RankedRequirement entry{id, -1, 0.0};
        double largest = 0.0;
        for (const auto& [leaf, part] : parts) {
            entry.share += part;
            if (entry.leaf_id == -1 || part > largest) {
                entry.leaf_id = leaf;
                largest = part;
            }
        }
        out.push_back(entry);
    }

    std::sort(out.begin(), out.end(), [](const RankedRequirement& a, const RankedRequirement& b) {
        if (a.share != b.share) return a.share > b.share;
        if ((a.requirement_id == -1) != (b.requirement_id == -1)) return a.requirement_id != -1;
        if (a.requirement_id != b.requirement_id) return a.requirement_id < b.requirement_id;
        return a.leaf_id < b.leaf_id;
    });
    return out;
}

std::vector<int> Requirements::projects_of(int requirement_id) const {
    auto it = m_projects_of.find(requirement_id);
    return it != m_projects_of.end() ? it->second : std::vector<int>{};
}

bool Requirements::is_served(int requirement_id) const {
    return !projects_of(requirement_id).empty();
}

void Requirements::link_project(int requirement_id, int project_root_id) {
    if (m_titles.count(requirement_id) == 0 || !is_top_level_project(project_root_id)) return;
    if (!m_db->set_requirement_project_link(requirement_id, project_root_id)) return;
    reload();
}

void Requirements::unlink_project(int requirement_id, int project_root_id) {
    if (!m_db->clear_requirement_project_link(requirement_id, project_root_id)) return;
    reload();
}

std::unordered_map<int, double> Requirements::project_priorities(
    const std::unordered_map<int, double>& priorities) const {
    std::unordered_map<int, double> out;
    for (int project : m_projects.children_of(Tree::ROOT_ID)) out[project] = 0.0;

    for (const auto& [id, parts] : contributions(priorities)) {
        const std::vector<int> projects = projects_of(id);
        if (projects.empty()) continue;

        double share = 0.0;
        for (const auto& [leaf, part] : parts) share += part;
        for (int project : projects) out[project] += share / static_cast<double>(projects.size());
    }
    return out;
}

std::unordered_map<int, double> Requirements::time_split(
    int project_root_id, const std::unordered_map<int, double>& priorities) const {
    std::vector<int> served;
    for (const auto& [id, projects] : m_projects_of) {
        if (std::find(projects.begin(), projects.end(), project_root_id) != projects.end()) {
            served.push_back(id);
        }
    }

    std::unordered_map<int, double> out;
    if (served.empty()) return out;

    const auto all_parts = contributions(priorities);
    const double per_requirement = 1.0 / static_cast<double>(served.size());
    for (int id : served) {
        auto it = all_parts.find(id);
        if (it == all_parts.end() || it->second.empty()) continue;
        const Parts& parts = it->second;

        double total = 0.0;
        for (const auto& [leaf, part] : parts) total += part;

        // Leaves weighted to zero still get the time, evenly.
        for (const auto& [leaf, part] : parts) {
            const double fraction =
                total > 0.0 ? part / total : 1.0 / static_cast<double>(parts.size());
            out[leaf] += per_requirement * fraction;
        }
    }
    return out;
}

bool Requirements::is_top_level_project(int project_id) const {
    return m_projects.contains(project_id) && m_projects.parent_of(project_id) == Tree::ROOT_ID;
}

sigc::connection Requirements::connect_changed(const sigc::slot<void()>& slot) {
    return m_changed.connect(slot);
}

void Requirements::write_order(int leaf_id, const std::vector<int>& order) {
    for (std::size_t i = 0; i < order.size(); ++i) {
        m_db->set_leaf_requirement_link(leaf_id, order[i], static_cast<int>(i));
    }
}
