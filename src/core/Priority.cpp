#include "core/Priority.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/TreeController.hpp"

namespace {
// The life tree's synthetic root. Not a goal the user wrote — it's the
// anchor everything hangs from, and it holds the whole budget. The same
// 0 that TreeController::leaves() excludes and remove() refuses.
constexpr int ROOT_ID = 0;

// Whole numbers summing to exactly `target`, by largest remainder.
// Shares are entered and shown as whole percents, so quantizing here
// means the stored numbers are the shown ones — otherwise three
// siblings read 33, 33, 33 and visibly fail to make 100.
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
}  // namespace

Priority::Priority(std::shared_ptr<Database> db, TreeController& life, TreeController& projects)
    : m_db(std::move(db)), m_life(life), m_projects(projects) {}

void Priority::load() {
    m_weights.clear();
    for (const auto& row : m_db->load_life_weights()) {
        m_weights[row.node_id] = row.weight;
    }

    m_links.clear();
    for (const auto& row : m_db->load_project_links()) {
        m_links[row.project_root_id][row.leaf_id] = {row.weight, row.goal_share};
    }
}

void Priority::set_weight(int node_id, double weight) {
    const int parent = m_life.parent_of(node_id);
    if (parent < 0) return;  // not in the tree, or the root, which has no share to set

    std::vector<int> siblings;
    for (int child : m_life.children_of(parent)) {
        if (child != node_id) siblings.push_back(child);
    }

    weight = std::round(std::clamp(weight, 0.0, TOTAL));

    // An only child always holds the whole of its parent — there's nothing
    // to trade against, so the requested value is ignored rather than
    // stored and silently overridden by the invariant.
    if (siblings.empty()) {
        write_weight(node_id, TOTAL);
        m_changed.emit();
        return;
    }

    const double remaining = TOTAL - weight;

    double sibling_total = 0.0;
    for (int sibling : siblings) sibling_total += weight_of(sibling);

    write_weight(node_id, weight);

    // The siblings' new values, quantized so the whole set lands on exactly
    // TOTAL as whole numbers. The node the user just set keeps precisely
    // what they typed — the rounding is absorbed by the others.
    std::vector<double> shares;
    shares.reserve(siblings.size());
    for (int sibling : siblings) {
        shares.push_back(sibling_total > 0.0 ? weight_of(sibling) * (remaining / sibling_total)
                                             : remaining / static_cast<double>(siblings.size()));
    }
    quantize(shares, remaining);

    for (size_t i = 0; i < siblings.size(); ++i) write_weight(siblings[i], shares[i]);

    m_changed.emit();
}

void Priority::write_weight(int node_id, double weight) {
    if (!m_db->set_life_weight(node_id, weight)) return;  // leave the cache as-is
    m_weights[node_id] = weight;
}

void Priority::normalize() {
    // A link survives only while both its ends are things a link can be made
    // between: a top-level project, and a node that is currently a leaf.
    // Anything else is deleted outright, from the cache and the database.
    //
    // Three ways a link stops qualifying, and all three are purged:
    //
    //   - its project or its leaf was deleted. The database cascades these
    //     already; the cache didn't hear about it, which is what made a
    //     project's live shares sum to seventy-five with nothing on screen
    //     accounting for the rest.
    //   - its leaf gained children, so it is a branch now and the link
    //     should belong to one of the children instead.
    //   - its project was demoted under another, so it stopped being a root.
    //     Links belong to roots; this is that invariant enforced rather than
    //     merely guarded.
    //
    // Kept instead of purged, these were invisible: nothing lists them, so a
    // goal subdivided months ago and later collapsed back would silently
    // restore shares nobody remembers setting. What the Links page shows is
    // now exactly what the database holds.
    for (auto project = m_links.begin(); project != m_links.end();) {
        const bool project_ok = is_live_project(project->first);
        auto& links = project->second;

        for (auto link = links.begin(); link != links.end();) {
            if (project_ok && is_leaf_target(link->first)) {
                ++link;
                continue;
            }
            m_db->clear_project_link(project->first, link->first);
            link = links.erase(link);
        }

        project = links.empty() ? m_links.erase(project) : std::next(project);
    }

    // Links come along for the ride: both are "bring stored values in line
    // with the invariant", both run at load, and a caller that wanted one
    // without the other would be asking for a half-consistent model.
    normalize_links();

    // Weights whose node is gone. The database cascades them away, but this
    // cache doesn't hear about a deletion — and a stale entry is exactly
    // what makes a node look weighted when nothing in the tree carries it.
    // Harmless today, since every read walks the tree; cheap insurance
    // against the first read that doesn't.
    for (auto it = m_weights.begin(); it != m_weights.end();) {
        it = m_life.contains(it->first) ? std::next(it) : m_weights.erase(it);
    }

    bool changed = false;

    // Breadth-first from the root: only the set of children matters, and
    // each set is independent of every other.
    std::vector<int> pending{ROOT_ID};
    while (!pending.empty()) {
        const int node = pending.back();
        pending.pop_back();

        std::vector<int> children = m_life.children_of(node);
        for (int child : children) pending.push_back(child);
        if (children.empty()) continue;

        double stored_total = 0.0;
        int missing = 0;
        for (int child : children) {
            auto it = m_weights.find(child);
            if (it == m_weights.end())
                ++missing;
            else
                stored_total += it->second;
        }

        // A child with no stored weight takes an EVEN share, and the scaling
        // below brings the whole set back to TOTAL — so adding one dilutes
        // its siblings rather than arriving with nothing.
        //
        // It used to get whatever was left over, which on a set already
        // summing to TOTAL — which every set does, that being the invariant
        // — is exactly zero. And zero is absorbing: scaling it by any factor
        // leaves it at zero, so the node was stuck there forever and every
        // later redistribution went round it. Deleting a sibling is where
        // that becomes visible, because the survivors share out the freed
        // weight and the stuck node still doesn't move.
        if (missing > 0) {
            const double each = TOTAL / static_cast<double>(children.size());
            for (int child : children) {
                if (m_weights.find(child) == m_weights.end()) {
                    write_weight(child, each);
                    changed = true;
                }
            }
            stored_total += each * missing;
        }

        // Scaled, not nudged: converts weights stored when sums over TOTAL
        // were harmless into ones that mean what they say.
        std::vector<double> shares;
        shares.reserve(children.size());
        for (int child : children) {
            shares.push_back(stored_total > 0.0 ? weight_of(child) * (TOTAL / stored_total)
                                                : TOTAL / static_cast<double>(children.size()));
        }
        quantize(shares, TOTAL);

        for (size_t i = 0; i < children.size(); ++i) {
            if (std::abs(weight_of(children[i]) - shares[i]) > 1e-6) {
                write_weight(children[i], shares[i]);
                changed = true;
            }
        }
    }

    if (changed) m_changed.emit();
}

double Priority::weight_of(int node_id) const {
    if (node_id == ROOT_ID) return TOTAL;  // the root is the whole of itself
    auto it = m_weights.find(node_id);
    return (it != m_weights.end()) ? it->second : 0.0;
}

std::unordered_map<int, double> Priority::priorities() const {
    std::unordered_map<int, double> out;
    if (!m_life.contains(ROOT_ID)) return out;

    out[ROOT_ID] = TOTAL;

    // Breadth-first: a node's children can only be resolved once the node
    // has a priority to divide. Index-based so the queue can grow as it's
    // walked.
    std::vector<int> queue{ROOT_ID};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        int node = queue[i];
        distribute(node, out[node], out);
        for (int child : m_life.children_of(node)) {
            queue.push_back(child);
        }
    }
    return out;
}

void Priority::write_link(int project_root_id, int leaf_id, const LinkWeights& weights) {
    if (!m_db->set_project_link(project_root_id, leaf_id, weights.project_share,
                                weights.goal_share))
        return;
    m_links[project_root_id][leaf_id] = weights;
}

void Priority::set_link(int project_root_id, int leaf_id) {
    if (has_link(project_root_id, leaf_id)) return;

    // Counted BEFORE the link exists, so the new one takes an even share.
    // Seeded explicitly rather than left to normalize_links: an axis whose
    // other links already sum to TOTAL looks consistent to the normalizer,
    // which would leave the newcomer on zero.
    const size_t project_links =
        m_links.count(project_root_id) ? m_links[project_root_id].size() : 0;
    const size_t leaf_claimants = projects_for(leaf_id).size();

    write_link(project_root_id, leaf_id, LinkWeights{});
    set_project_share(project_root_id, leaf_id, TOTAL / static_cast<double>(project_links + 1));
    set_goal_share(project_root_id, leaf_id, TOTAL / static_cast<double>(leaf_claimants + 1));
    m_changed.emit();
}

void Priority::clear_link(int project_root_id, int leaf_id) {
    if (!m_db->clear_project_link(project_root_id, leaf_id)) return;
    auto it = m_links.find(project_root_id);
    if (it != m_links.end()) {
        it->second.erase(leaf_id);
        if (it->second.empty()) m_links.erase(it);
    }
    // The shares the departing link held are now unclaimed on both axes.
    normalize_links();
    m_changed.emit();
}

bool Priority::has_link(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    return it != m_links.end() && it->second.count(leaf_id) > 0;
}

double Priority::project_share(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return 0.0;
    auto jt = it->second.find(leaf_id);
    return (jt != it->second.end()) ? jt->second.project_share : 0.0;
}

double Priority::goal_share(int project_root_id, int leaf_id) const {
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return 0.0;
    auto jt = it->second.find(leaf_id);
    return (jt != it->second.end()) ? jt->second.goal_share : 0.0;
}

void Priority::set_project_share(int project_root_id, int leaf_id, double share) {
    if (!has_link(project_root_id, leaf_id)) return;
    share = std::round(std::clamp(share, 0.0, TOTAL));

    // Rebalances along this PROJECT's other links — its shares describe how
    // it divides, so its own set is what has to stay summing to TOTAL. The
    // goal axis is untouched: nothing was said about any leaf's division.
    std::vector<int> others;
    for (const auto& [other_leaf, unused] : m_links[project_root_id]) {
        if (other_leaf != leaf_id) others.push_back(other_leaf);
    }

    auto weights = m_links[project_root_id][leaf_id];
    if (others.empty()) {
        weights.project_share = TOTAL;  // sole link: it is the whole project
        write_link(project_root_id, leaf_id, weights);
        m_changed.emit();
        return;
    }

    weights.project_share = share;
    write_link(project_root_id, leaf_id, weights);

    double other_total = 0.0;
    for (int other : others) other_total += project_share(project_root_id, other);

    const double remaining = TOTAL - share;
    std::vector<double> shares;
    shares.reserve(others.size());
    for (int other : others) {
        shares.push_back(other_total > 0.0
                             ? project_share(project_root_id, other) * (remaining / other_total)
                             : remaining / static_cast<double>(others.size()));
    }
    quantize(shares, remaining);

    for (size_t i = 0; i < others.size(); ++i) {
        auto w = m_links[project_root_id][others[i]];
        w.project_share = shares[i];
        write_link(project_root_id, others[i], w);
    }
    m_changed.emit();
}

void Priority::set_goal_share(int project_root_id, int leaf_id, double share) {
    if (!has_link(project_root_id, leaf_id)) return;
    share = std::round(std::clamp(share, 0.0, TOTAL));

    // Rebalances along this LEAF's other projects — the opposite axis to
    // set_project_share, which is why the two never disturb each other.
    std::vector<int> others;
    for (int project : projects_for(leaf_id)) {
        if (project != project_root_id) others.push_back(project);
    }

    auto weights = m_links[project_root_id][leaf_id];
    if (others.empty()) {
        weights.goal_share = TOTAL;  // sole claimant: it delivers the whole goal
        write_link(project_root_id, leaf_id, weights);
        m_changed.emit();
        return;
    }

    weights.goal_share = share;
    write_link(project_root_id, leaf_id, weights);

    double other_total = 0.0;
    for (int other : others) other_total += goal_share(other, leaf_id);

    const double remaining = TOTAL - share;
    std::vector<double> shares;
    shares.reserve(others.size());
    for (int other : others) {
        shares.push_back(other_total > 0.0 ? goal_share(other, leaf_id) * (remaining / other_total)
                                           : remaining / static_cast<double>(others.size()));
    }
    quantize(shares, remaining);

    for (size_t i = 0; i < others.size(); ++i) {
        auto w = m_links[others[i]][leaf_id];
        w.goal_share = shares[i];
        write_link(others[i], leaf_id, w);
    }
    m_changed.emit();
}

void Priority::normalize_links() {
    // The liveness tests below are belt and braces: normalize() purges
    // anything that fails them before calling this, so by the time it runs
    // every link is live. They stay because this must not depend on the
    // order of two steps in its caller — counting a link the Links page
    // doesn't show is exactly the bug that produced shares summing to
    // seventy-five.

    // Project axis: each project's shares across its own live links.
    for (auto& [project, links] : m_links) {
        if (links.empty()) continue;
        if (!is_live_project(project)) continue;

        double total = 0.0;
        std::vector<int> leaves;
        for (const auto& [leaf, w] : links) {
            if (!is_leaf_target(leaf)) continue;
            leaves.push_back(leaf);
            total += w.project_share;
        }
        if (leaves.empty()) continue;

        std::vector<double> shares;
        shares.reserve(leaves.size());
        for (int leaf : leaves) {
            shares.push_back(total > 0.0 ? links[leaf].project_share * (TOTAL / total)
                                         : TOTAL / static_cast<double>(leaves.size()));
        }
        quantize(shares, TOTAL);

        bool differs = false;
        for (size_t i = 0; i < leaves.size(); ++i) {
            if (std::abs(links[leaves[i]].project_share - shares[i]) > 1e-6) differs = true;
        }
        if (!differs) continue;

        for (size_t i = 0; i < leaves.size(); ++i) {
            links[leaves[i]].project_share = shares[i];
            const auto& w = links[leaves[i]];
            m_db->set_project_link(project, leaves[i], w.project_share, w.goal_share);
        }
    }

    // Goal axis: each leaf's shares across the projects claiming it. Built
    // by scanning, since m_links is keyed the other way round.
    std::unordered_map<int, std::vector<int>> claimants;
    for (const auto& [project, links] : m_links) {
        if (!is_live_project(project)) continue;
        for (const auto& [leaf, w] : links) {
            if (!is_leaf_target(leaf)) continue;
            claimants[leaf].push_back(project);
        }
    }

    for (const auto& [leaf, projects] : claimants) {
        double total = 0.0;
        for (int project : projects) total += m_links[project][leaf].goal_share;

        std::vector<double> shares;
        for (int project : projects) {
            shares.push_back(total > 0.0 ? m_links[project][leaf].goal_share * (TOTAL / total)
                                         : TOTAL / static_cast<double>(projects.size()));
        }
        quantize(shares, TOTAL);

        bool differs = false;
        for (size_t i = 0; i < projects.size(); ++i) {
            if (std::abs(m_links[projects[i]][leaf].goal_share - shares[i]) > 1e-6) differs = true;
        }
        if (!differs) continue;

        for (size_t i = 0; i < projects.size(); ++i) {
            m_links[projects[i]][leaf].goal_share = shares[i];
            const auto& w = m_links[projects[i]][leaf];
            m_db->set_project_link(projects[i], leaf, w.project_share, w.goal_share);
        }
    }
}

std::vector<int> Priority::leaves_for(int project_root_id) const {
    std::vector<int> out;
    auto it = m_links.find(project_root_id);
    if (it == m_links.end()) return out;
    for (const auto& [leaf_id, weight] : it->second) {
        // Stale links stay in the table but are ignored wherever they'd
        // have an effect — see is_leaf_target. Reporting them here made
        // callers sum weights project_priorities never counted.
        if (!is_leaf_target(leaf_id)) continue;
        out.push_back(leaf_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> Priority::projects_for(int leaf_id) const {
    std::vector<int> out;
    for (const auto& [project_id, leaves] : m_links) {
        if (!is_live_project(project_id)) continue;  // stale — see is_live_project
        if (leaves.count(leaf_id) > 0) out.push_back(project_id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool Priority::is_live_project(int project_root_id) const {
    // Top-level only: a project is a child of the hidden root, and a link
    // is only ever set against one of those.
    return m_projects.contains(project_root_id) && m_projects.parent_of(project_root_id) == ROOT_ID;
}

bool Priority::is_leaf_target(int node_id) const {
    // ROOT_ID is excluded for the same reason TreeController::leaves() does:
    // the synthetic anchor is never a real goal, even on an empty tree.
    return node_id != ROOT_ID && m_life.contains(node_id) && m_life.children_of(node_id).empty();
}

std::unordered_map<int, double> Priority::project_priorities() const {
    const auto node_priorities = priorities();

    // From the leaf's side: a leaf owns a fixed share and hands it out, so
    // it's spent exactly once however many projects attach to it.
    //
    // goal_share, not project_share — a project being 90% about a leaf says
    // nothing about how much of that leaf it covers.
    std::unordered_map<int, std::vector<std::pair<int, double>>> by_leaf;
    for (const auto& [project_id, leaves] : m_links) {
        if (!is_live_project(project_id)) continue;  // stale — see is_live_project
        for (const auto& [leaf_id, weights] : leaves) {
            if (!is_leaf_target(leaf_id)) continue;  // stale — see is_leaf_target
            by_leaf[leaf_id].push_back({project_id, weights.goal_share});
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
            const double portion = (total_weight > 0.0) ? weight / total_weight
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
        return a < b;  // stable order for equal priorities
    });
    return ids;
}

std::vector<int> Priority::ranked_leaves() const {
    const auto values = priorities();

    std::vector<int> ids = m_life.leaves();
    std::sort(ids.begin(), ids.end(), [&values](int a, int b) {
        // Looked up rather than indexed: priorities() is built by walking
        // down from the root, so a node the walk can't reach simply isn't in
        // it, and ranking last is the right answer for one.
        auto ia = values.find(a);
        auto ib = values.find(b);
        const double va = (ia == values.end()) ? 0.0 : ia->second;
        const double vb = (ib == values.end()) ? 0.0 : ib->second;
        if (va != vb) return va > vb;
        return a < b;  // stable order for equal priorities
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
            out.push_back({leaf_id, it->second});
        }
    }

    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

void Priority::distribute(int node_id, double budget, std::unordered_map<int, double>& out) const {
    // One rule, no branches: normalize() guarantees the children are all
    // present and sum to TOTAL, so there's nothing to reconcile.
    for (int child : m_life.children_of(node_id)) {
        out[child] = budget * (weight_of(child) / TOTAL);
    }
}
