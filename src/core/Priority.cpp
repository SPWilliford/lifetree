#include "core/Priority.hpp"
#include "core/TreeController.hpp"
#include <algorithm>
#include <vector>

namespace {
    // The life tree's synthetic root. Not a goal the user wrote — it's the
    // anchor everything hangs from, and it holds the whole budget. The same
    // 0 that TreeController::leaves() excludes and remove() refuses.
    constexpr int ROOT_ID = 0;
}

Priority::Priority(std::shared_ptr<Database> db, TreeController& life, TreeController& projects)
    : m_db(std::move(db)), m_life(life), m_projects(projects) {}

void Priority::load() {
    m_weights.clear();
    for (const auto& row : m_db->load_life_weights()) {
        m_weights[row.node_id] = row.weight;
    }

    m_links.clear();
    for (const auto& row : m_db->load_project_links()) {
        m_links[row.project_root_id][row.leaf_id] = row.weight;
    }
}

void Priority::set_weight(int node_id, double weight) {
    weight = std::clamp(weight, 0.0, TOTAL);
    if (!m_db->set_life_weight(node_id, weight)) return; // leave the cache as-is
    m_weights[node_id] = weight;
    m_changed.emit();
}

void Priority::clear_weight(int node_id) {
    if (!m_db->clear_life_weight(node_id)) return;
    m_weights.erase(node_id);
    m_changed.emit();
}

bool Priority::has_weight(int node_id) const {
    return m_weights.find(node_id) != m_weights.end();
}

double Priority::weight_of(int node_id) const {
    auto it = m_weights.find(node_id);
    return (it != m_weights.end()) ? it->second : 0.0;
}

std::unordered_map<int, double> Priority::priorities() const {
    std::unordered_map<int, double> out;
    if (!m_life.contains(ROOT_ID)) return out;

    out[ROOT_ID] = TOTAL;

    // Breadth-first, because a node's children can only be resolved once
    // the node itself has a priority to divide — which visiting strictly
    // top-down guarantees. Index-based so the queue can grow while it's
    // being walked.
    std::vector<int> queue{ ROOT_ID };
    for (std::size_t i = 0; i < queue.size(); ++i) {
        int node = queue[i];
        distribute(node, out[node], out);
        for (int child : m_life.children_of(node)) {
            queue.push_back(child);
        }
    }
    return out;
}

void Priority::set_link(int project_root_id, int leaf_id, double weight) {
    weight = std::max(0.0, weight);
    if (!m_db->set_project_link(project_root_id, leaf_id, weight)) return;
    m_links[project_root_id][leaf_id] = weight;
    m_changed.emit();
}

void Priority::clear_link(int project_root_id, int leaf_id) {
    if (!m_db->clear_project_link(project_root_id, leaf_id)) return;
    auto it = m_links.find(project_root_id);
    if (it != m_links.end()) {
        it->second.erase(leaf_id);
        if (it->second.empty()) m_links.erase(it);
    }
    m_changed.emit();
}

bool Priority::has_link(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    return it != m_links.end() && it->second.count(leaf_id) > 0;
}

double Priority::link_weight(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return 0.0;
    auto jt = it->second.find(leaf_id);
    return (jt != it->second.end()) ? jt->second : 0.0;
}

std::vector<int> Priority::leaves_for(int project_root_id) const {
    std::vector<int> out;
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return out;
    for (const auto& [leaf_id, weight] : it->second) out.push_back(leaf_id);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> Priority::projects_for(int leaf_id) const {
    std::vector<int> out;
    for (const auto& [project_id, leaves] : m_links) {
        if (leaves.count(leaf_id) > 0) out.push_back(project_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool Priority::is_leaf_target(int node_id) const {
    // ROOT_ID is excluded for the same reason TreeController::leaves() does:
    // the synthetic anchor is never a real goal, even on an empty tree.
    return node_id != ROOT_ID && m_life.contains(node_id) && m_life.children_of(node_id).empty();
}

std::unordered_map<int, double> Priority::project_priorities() const {
    const auto node_priorities = priorities();

    // Regroup the links by leaf. The division has to happen from the leaf's
    // side: a leaf owns a fixed share and hands it out, so that share is
    // spent exactly once however many projects are attached to it.
    std::unordered_map<int, std::vector<std::pair<int, double>>> by_leaf;
    for (const auto& [project_id, leaves] : m_links) {
        for (const auto& [leaf_id, weight] : leaves) {
            if (!is_leaf_target(leaf_id)) continue; // stale — see is_leaf_target
            by_leaf[leaf_id].push_back({ project_id, weight });
        }
    }

    // Seed every top-level project at zero so unlinked ones still appear.
    std::unordered_map<int, double> out;
    for (int project_id : m_projects.children_of(ROOT_ID)) out[project_id] = 0.0;

    for (const auto& [leaf_id, entries] : by_leaf) {
        auto it = node_priorities.find(leaf_id);
        if (it == node_priorities.end()) continue;
        const double share = it->second;

        double total_weight = 0.0;
        for (const auto& [project_id, weight] : entries) total_weight += weight;

        for (const auto& [project_id, weight] : entries) {
            // All weights zero means the user attached projects but never
            // said how much each matters. An even split is the honest
            // reading, and keeps the leaf's share from vanishing.
            const double portion = (total_weight > 0.0)
                ? weight / total_weight
                : 1.0 / static_cast<double>(entries.size());
            out[project_id] += share * portion;
        }
    }
    return out;
}

std::vector<int> Priority::ranked_projects() const {
    const auto values = project_priorities();

    std::vector<int> ids;
    ids.reserve(values.size());
    for (const auto& [project_id, value] : values) ids.push_back(project_id);

    std::sort(ids.begin(), ids.end(), [&values](int a, int b) {
        if (values.at(a) != values.at(b)) return values.at(a) > values.at(b);
        return a < b; // stable order for equal priorities
    });
    return ids;
}

std::vector<std::pair<int, double>> Priority::unserved_leaves() const {
    const auto node_priorities = priorities();

    std::vector<std::pair<int, double>> out;
    for (int leaf_id : m_life.leaves()) {
        if (!projects_for(leaf_id).empty()) continue;
        auto it = node_priorities.find(leaf_id);
        if (it != node_priorities.end() && it->second > 0.0) {
            out.push_back({ leaf_id, it->second });
        }
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

void Priority::distribute(int node_id, double budget, std::unordered_map<int, double>& out) const {
    std::vector<int> children = m_life.children_of(node_id);
    if (children.empty()) return; // a leaf keeps its whole share

    double claimed = 0.0;
    int unweighted = 0;
    for (int child : children) {
        auto it = m_weights.find(child);
        if (it != m_weights.end()) claimed += it->second;
        else ++unweighted;
    }

    if (unweighted == 0) {
        // Every child is weighted. Their weights are treated as relative to
        // each other and scaled to fill the budget exactly, whether they
        // sum to less than TOTAL or more. Leaving a shortfall unspent would
        // demote this entire subtree against its siblings for a reason the
        // user never expressed — they said how to divide this branch, not
        // that the branch deserves less than it was given.
        if (claimed <= 0.0) {
            // Every child explicitly zero. There's no ratio to honour, and
            // discarding the budget would break the invariant, so fall back
            // to an even split.
            for (int child : children) out[child] = budget / static_cast<double>(children.size());
            return;
        }
        for (int child : children) out[child] = budget * (m_weights.at(child) / claimed);
        return;
    }

    if (claimed >= TOTAL) {
        // Over-allocated: the weighted children already claim everything,
        // so there's nothing left for the unweighted ones. A planning UI
        // should stop this happening, but the invariant has to hold anyway.
        for (int child : children) {
            auto it = m_weights.find(child);
            out[child] = (it != m_weights.end()) ? budget * (it->second / claimed) : 0.0;
        }
        return;
    }

    // The ordinary case: weighted children take exactly what they asked
    // for, and the remainder splits evenly among the unweighted ones —
    // rather than being spread over all children, which would push the
    // weighted ones above the share the user explicitly chose for them.
    const double each = (TOTAL - claimed) / static_cast<double>(unweighted);
    for (int child : children) {
        auto it = m_weights.find(child);
        const double share = (it != m_weights.end()) ? it->second : each;
        out[child] = budget * (share / TOTAL);
    }
}
