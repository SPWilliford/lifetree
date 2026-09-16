#include "core/Priority.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/TreeController.hpp"

namespace {

constexpr double EPSILON = 1e-6;

// Whole numbers summing to exactly `target`, by largest remainder.
void quantize(std::vector<double>& values, double target) {
    if (values.empty()) return;

    double assigned = 0.0;
    std::vector<std::pair<double, size_t>> remainders;
    for (size_t i = 0; i < values.size(); ++i) {
        const double floored = std::floor(values[i]);
        remainders.push_back({values[i] - floored, i});
        values[i] = floored;
        assigned += floored;
    }
    std::sort(remainders.begin(), remainders.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    const int leftover = static_cast<int>(std::llround(target - assigned));
    for (int i = 0; i < leftover && i < static_cast<int>(remainders.size()); ++i) {
        values[remainders[i].second] += 1.0;
    }
}

// Scales `values` to sum to `target`, evenly if they sum to zero, then
// quantizes. The one rule every share set follows.
std::vector<double> rebalance(std::vector<double> values, double target) {
    if (values.empty()) return values;

    double total = 0.0;
    for (double v : values) total += v;

    for (double& v : values) {
        v = total > 0.0 ? v * (target / total) : target / static_cast<double>(values.size());
    }
    quantize(values, target);
    return values;
}

// Holds values[index] at `fixed` and rebalances the others into the rest.
// A lone value is always TOTAL.
std::vector<double> rebalance_around(std::vector<double> values, size_t index, double fixed) {
    if (values.size() == 1) return {Priority::TOTAL};

    std::vector<double> others;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != index) others.push_back(values[i]);
    }
    others = rebalance(others, Priority::TOTAL - fixed);

    size_t j = 0;
    for (size_t i = 0; i < values.size(); ++i) values[i] = (i == index) ? fixed : others[j++];
    return values;
}

bool differs(const std::vector<double>& a, const std::vector<double>& b) {
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > EPSILON) return true;
    }
    return false;
}

// Whole numbers that are an even split with its rounding: they differ by
// at most one.
bool is_even_split(const std::vector<double>& values) {
    if (values.empty()) return true;
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    return *hi - *lo <= 1.0 + EPSILON;
}

double clamp_share(double share) {
    return std::round(std::clamp(share, 0.0, Priority::TOTAL));
}

}  // namespace

Priority::Priority(std::shared_ptr<Database> db, TreeController& life, TreeController& projects)
    : m_db(std::move(db)), m_life(life), m_projects(projects) {}

void Priority::load() {
    m_weights.clear();
    for (const auto& row : m_db->load_life_weights()) m_weights[row.node_id] = row.weight;

    m_links.clear();
    for (const auto& row : m_db->load_project_links()) {
        m_links[row.project_root_id][row.leaf_id] = {row.project_share, row.goal_share};
    }
}

// ---------------------------------------------------------------------
// Weights
// ---------------------------------------------------------------------

double Priority::weight_of(int node_id) const {
    if (node_id == Tree::ROOT_ID) return TOTAL;
    auto it = m_weights.find(node_id);
    return (it != m_weights.end()) ? it->second : 0.0;
}

void Priority::write_weight(int node_id, double weight) {
    if (!m_db->set_life_weight(node_id, weight)) return;
    m_weights[node_id] = weight;
}

void Priority::set_weight(int node_id, double weight) {
    const int parent = m_life.parent_of(node_id);
    if (parent < 0) return;

    const std::vector<int> siblings = m_life.children_of(parent);
    std::vector<double> current;
    size_t index = 0;
    for (size_t i = 0; i < siblings.size(); ++i) {
        current.push_back(weight_of(siblings[i]));
        if (siblings[i] == node_id) index = i;
    }

    const auto updated = rebalance_around(current, index, clamp_share(weight));
    for (size_t i = 0; i < siblings.size(); ++i) write_weight(siblings[i], updated[i]);
    m_changed.emit();
}

void Priority::normalize() {
    bool changed = false;

    for (auto it = m_weights.begin(); it != m_weights.end();) {
        it = m_life.contains(it->first) ? std::next(it) : m_weights.erase(it);
    }

    std::vector<int> pending{Tree::ROOT_ID};
    while (!pending.empty()) {
        const int node = pending.back();
        pending.pop_back();

        const std::vector<int> children = m_life.children_of(node);
        for (int child : children) pending.push_back(child);
        if (children.empty()) continue;

        std::vector<double> current;
        std::vector<double> stored;
        bool any_missing = false;
        for (int child : children) {
            auto it = m_weights.find(child);
            if (it == m_weights.end()) {
                any_missing = true;
                current.push_back(0.0);
            } else {
                current.push_back(it->second);
                stored.push_back(it->second);
            }
        }

        // A newcomer joins an untouched set evenly. Where the siblings have
        // been weighted by hand it arrives at zero, so nothing you chose
        // moves without you.
        if (any_missing && is_even_split(stored)) {
            std::fill(current.begin(), current.end(), 1.0);
        }

        const auto updated = rebalance(current, TOTAL);
        for (size_t i = 0; i < children.size(); ++i) {
            if (std::abs(weight_of(children[i]) - updated[i]) > EPSILON ||
                m_weights.find(children[i]) == m_weights.end()) {
                write_weight(children[i], updated[i]);
                changed = true;
            }
        }
    }

    if (purge_stale_links()) changed = true;
    if (normalize_links()) changed = true;

    if (changed) m_changed.emit();
}

std::unordered_map<int, double> Priority::priorities() const {
    std::unordered_map<int, double> out;
    if (!m_life.contains(Tree::ROOT_ID)) return out;

    out[Tree::ROOT_ID] = TOTAL;
    std::vector<int> queue{Tree::ROOT_ID};
    for (size_t i = 0; i < queue.size(); ++i) {
        const int node = queue[i];
        for (int child : m_life.children_of(node)) {
            out[child] = out[node] * (weight_of(child) / TOTAL);
            queue.push_back(child);
        }
    }
    return out;
}

std::vector<int> Priority::ranked_leaves() const {
    const auto values = priorities();
    std::vector<int> ids = m_life.leaves();
    std::sort(ids.begin(), ids.end(), [&values](int a, int b) {
        const double va = values.count(a) ? values.at(a) : 0.0;
        const double vb = values.count(b) ? values.at(b) : 0.0;
        if (va != vb) return va > vb;
        return a < b;
    });
    return ids;
}

// ---------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------

bool Priority::is_live_project(int project_root_id) const {
    return m_projects.contains(project_root_id) &&
           m_projects.parent_of(project_root_id) == Tree::ROOT_ID;
}

bool Priority::is_leaf_target(int node_id) const {
    return node_id != Tree::ROOT_ID && m_life.contains(node_id) &&
           m_life.children_of(node_id).empty();
}

bool Priority::has_link(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    return it != m_links.end() && it->second.count(leaf_id) > 0;
}

Priority::LinkWeights Priority::link(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return {};
    auto jt = it->second.find(leaf_id);
    return (jt != it->second.end()) ? jt->second : LinkWeights{};
}

double Priority::project_share(int project_root_id, int leaf_id) const {
    return link(project_root_id, leaf_id).project_share;
}

double Priority::goal_share(int project_root_id, int leaf_id) const {
    return link(project_root_id, leaf_id).goal_share;
}

void Priority::write_link(int project_root_id, int leaf_id, const LinkWeights& weights) {
    if (!m_db->set_project_link(project_root_id, leaf_id, weights.project_share,
                                weights.goal_share)) {
        return;
    }
    m_links[project_root_id][leaf_id] = weights;
}

std::vector<int> Priority::leaves_for(int project_root_id) const {
    std::vector<int> out;
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return out;
    for (const auto& [leaf_id, unused] : it->second) {
        if (is_leaf_target(leaf_id)) out.push_back(leaf_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> Priority::projects_for(int leaf_id) const {
    std::vector<int> out;
    for (const auto& [project_id, leaves] : m_links) {
        if (!is_live_project(project_id)) continue;
        if (leaves.count(leaf_id) > 0) out.push_back(project_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A new link takes an even share on both axes and the existing links make
// room: a project that starts at zero would rank as though unlinked.
void Priority::set_link(int project_root_id, int leaf_id) {
    if (has_link(project_root_id, leaf_id)) return;

    const auto leaves = leaves_for(project_root_id);
    const auto projects = projects_for(leaf_id);

    write_link(project_root_id, leaf_id, LinkWeights{});
    assign_project_share(project_root_id, leaf_id, TOTAL / static_cast<double>(leaves.size() + 1));
    assign_goal_share(project_root_id, leaf_id, TOTAL / static_cast<double>(projects.size() + 1));
    m_changed.emit();
}

void Priority::clear_link(int project_root_id, int leaf_id) {
    if (!m_db->clear_project_link(project_root_id, leaf_id)) return;
    auto it = m_links.find(project_root_id);
    if (it != m_links.end()) {
        it->second.erase(leaf_id);
        if (it->second.empty()) m_links.erase(it);
    }
    normalize_links();
    m_changed.emit();
}

void Priority::set_project_share(int project_root_id, int leaf_id, double share) {
    if (!has_link(project_root_id, leaf_id)) return;
    assign_project_share(project_root_id, leaf_id, share);
    m_changed.emit();
}

void Priority::set_goal_share(int project_root_id, int leaf_id, double share) {
    if (!has_link(project_root_id, leaf_id)) return;
    assign_goal_share(project_root_id, leaf_id, share);
    m_changed.emit();
}

void Priority::assign_project_share(int project_root_id, int leaf_id, double share) {
    const std::vector<int> leaves = leaves_for(project_root_id);
    std::vector<double> current;
    size_t index = 0;
    for (size_t i = 0; i < leaves.size(); ++i) {
        current.push_back(project_share(project_root_id, leaves[i]));
        if (leaves[i] == leaf_id) index = i;
    }

    const auto updated = rebalance_around(current, index, clamp_share(share));
    for (size_t i = 0; i < leaves.size(); ++i) {
        LinkWeights w = link(project_root_id, leaves[i]);
        w.project_share = updated[i];
        write_link(project_root_id, leaves[i], w);
    }
}

void Priority::assign_goal_share(int project_root_id, int leaf_id, double share) {
    const std::vector<int> projects = projects_for(leaf_id);
    std::vector<double> current;
    size_t index = 0;
    for (size_t i = 0; i < projects.size(); ++i) {
        current.push_back(goal_share(projects[i], leaf_id));
        if (projects[i] == project_root_id) index = i;
    }

    const auto updated = rebalance_around(current, index, clamp_share(share));
    for (size_t i = 0; i < projects.size(); ++i) {
        LinkWeights w = link(projects[i], leaf_id);
        w.goal_share = updated[i];
        write_link(projects[i], leaf_id, w);
    }
}

bool Priority::purge_stale_links() {
    bool changed = false;
    for (auto project = m_links.begin(); project != m_links.end();) {
        const bool project_ok = is_live_project(project->first);
        auto& links = project->second;

        for (auto entry = links.begin(); entry != links.end();) {
            if (project_ok && is_leaf_target(entry->first)) {
                ++entry;
                continue;
            }
            m_db->clear_project_link(project->first, entry->first);
            entry = links.erase(entry);
            changed = true;
        }
        project = links.empty() ? m_links.erase(project) : std::next(project);
    }
    return changed;
}

bool Priority::normalize_links() {
    bool changed = false;

    // Project axis: a project's links sum to TOTAL.
    for (const auto& [project, unused] : m_links) {
        if (!is_live_project(project)) continue;
        const std::vector<int> leaves = leaves_for(project);
        if (leaves.empty()) continue;

        std::vector<double> current;
        for (int leaf : leaves) current.push_back(project_share(project, leaf));
        const auto updated = rebalance(current, TOTAL);
        if (!differs(current, updated)) continue;

        for (size_t i = 0; i < leaves.size(); ++i) {
            LinkWeights w = link(project, leaves[i]);
            w.project_share = updated[i];
            write_link(project, leaves[i], w);
        }
        changed = true;
    }

    // Goal axis: a leaf's projects sum to TOTAL.
    std::vector<int> leaves;
    for (const auto& [project, links] : m_links) {
        if (!is_live_project(project)) continue;
        for (const auto& [leaf, unused] : links) {
            if (is_leaf_target(leaf)) leaves.push_back(leaf);
        }
    }
    std::sort(leaves.begin(), leaves.end());
    leaves.erase(std::unique(leaves.begin(), leaves.end()), leaves.end());

    for (int leaf : leaves) {
        const std::vector<int> projects = projects_for(leaf);
        std::vector<double> current;
        for (int project : projects) current.push_back(goal_share(project, leaf));
        const auto updated = rebalance(current, TOTAL);
        if (!differs(current, updated)) continue;

        for (size_t i = 0; i < projects.size(); ++i) {
            LinkWeights w = link(projects[i], leaf);
            w.goal_share = updated[i];
            write_link(projects[i], leaf, w);
        }
        changed = true;
    }

    return changed;
}

// ---------------------------------------------------------------------
// Derived
// ---------------------------------------------------------------------

std::unordered_map<int, double> Priority::project_priorities() const {
    const auto node_priorities = priorities();

    std::unordered_map<int, double> out;
    for (int project_id : m_projects.children_of(Tree::ROOT_ID)) out[project_id] = 0.0;

    for (int leaf : m_life.leaves()) {
        auto it = node_priorities.find(leaf);
        if (it == node_priorities.end()) continue;

        const std::vector<int> projects = projects_for(leaf);
        if (projects.empty()) continue;

        double total = 0.0;
        for (int project : projects) total += goal_share(project, leaf);

        for (int project : projects) {
            const double portion =
                total > 0.0 ? goal_share(project, leaf) / total : 1.0 / projects.size();
            out[project] += it->second * portion;
        }
    }
    return out;
}

std::vector<std::pair<int, double>> Priority::unserved_leaves() const {
    const auto node_priorities = priorities();

    std::vector<std::pair<int, double>> out;
    for (int leaf_id : m_life.leaves()) {
        if (!projects_for(leaf_id).empty()) continue;
        auto it = node_priorities.find(leaf_id);
        if (it != node_priorities.end() && it->second > 0.0) out.push_back({leaf_id, it->second});
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}
