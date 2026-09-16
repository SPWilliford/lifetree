#ifndef PRIORITY_HPP
#define PRIORITY_HPP

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"

class TreeController;

// Weights on life tree nodes, cascaded root to leaf, and the links from
// projects to the leaves they serve.
//
// Weights: every child of a node has one, and a node's children always sum
// to TOTAL. Shares are whole numbers.
//
// Links: each carries two shares running opposite ways, and neither is
// derivable from the other.
//   project_share  how much of this project is about this leaf. Sums to
//                  TOTAL across a project's links. Attributes logged time
//                  back to life branches (Review).
//   goal_share     how much of this leaf this project delivers. Sums to
//                  TOTAL across a leaf's projects. Divides the leaf's
//                  priority among them (scheduling).
// The two axes normalize independently.
class Priority {
public:
    static constexpr double TOTAL = 100.0;

    Priority(std::shared_ptr<Database> db, TreeController& life, TreeController& projects);

    // Call once at startup, after both trees have loaded.
    void load();

    // Sets one node's share of its parent and scales its siblings into the
    // rest. An only child is always TOTAL.
    void set_weight(int node_id, double weight);

    // The node's share of its parent; the root is TOTAL.
    double weight_of(int node_id) const;

    // Restores every invariant: each child set present and summing to
    // TOTAL, stale links purged, link shares summing on each axis. Runs at
    // load and on every tree change. Idempotent.
    //
    // A node without a weight joins its siblings evenly if they are still
    // an even split, and at zero if they have been weighted by hand.
    void normalize();

    // Every node's share of the whole tree. One walk for all of them; hold
    // the result rather than calling per node.
    std::unordered_map<int, double> priorities() const;

    // --- links ---
    void set_link(int project_root_id, int leaf_id);
    void set_project_share(int project_root_id, int leaf_id, double share);
    void set_goal_share(int project_root_id, int leaf_id, double share);
    void clear_link(int project_root_id, int leaf_id);
    bool has_link(int project_root_id, int leaf_id) const;
    double project_share(int project_root_id, int leaf_id) const;
    double goal_share(int project_root_id, int leaf_id) const;

    std::vector<int> leaves_for(int project_root_id) const;
    std::vector<int> projects_for(int leaf_id) const;

    // Each leaf's priority divided among its projects by goal_share.
    // Unlinked projects appear at zero.
    std::unordered_map<int, double> project_priorities() const;

    // Highest priority first, ties by id.
    std::vector<int> ranked_leaves() const;

    // Leaves carrying priority that no project serves, highest first.
    // Empty on a tree that has no leaves at all.
    std::vector<std::pair<int, double>> unserved_leaves() const;

    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    struct LinkWeights {
        double project_share = 0.0;
        double goal_share = 0.0;
    };

    // Write-through to the database and cache; no rebalancing, no signal.
    void write_weight(int node_id, double weight);
    void write_link(int project_root_id, int leaf_id, const LinkWeights& weights);

    // Stored weights, or zeros for a missing link.
    LinkWeights link(int project_root_id, int leaf_id) const;

    // The set_* bodies, without the signal.
    void assign_project_share(int project_root_id, int leaf_id, double share);
    void assign_goal_share(int project_root_id, int leaf_id, double share);

    // Both return whether anything was written.
    bool purge_stale_links();
    bool normalize_links();

    // A link's ends must be a top-level project and a current leaf. Checked
    // at point of use, since the caches don't hear deletions.
    bool is_leaf_target(int node_id) const;
    bool is_live_project(int project_root_id) const;

    std::shared_ptr<Database> m_db;
    TreeController& m_life;
    TreeController& m_projects;

    // Every life node except the root.
    std::unordered_map<int, double> m_weights;

    // project_root_id -> (leaf_id -> weights).
    std::unordered_map<int, std::unordered_map<int, LinkWeights>> m_links;

    sigc::signal<void()> m_changed;
};

#endif
