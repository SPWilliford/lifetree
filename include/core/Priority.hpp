#ifndef PRIORITY_HPP
#define PRIORITY_HPP

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"

class TreeController;

// Weights on life tree nodes, cascaded root-to-leaf. A node passes its
// ENTIRE priority to its children, so siblings always sum to exactly their
// parent. Nothing derived is stored.
class Priority {
public:
    // What the root holds and the scale weights are entered on. 100 so a
    // leaf's priority reads directly as a percentage of the whole.
    static constexpr double TOTAL = 100.0;

    Priority(std::shared_ptr<Database> db, TreeController& life, TreeController& projects);

    // Call once at startup, after the life tree has loaded.
    void load();

    // Moves siblings to keep the set summing to TOTAL — proportionally, or
    // evenly when they're all at zero. The only way weights change.
    void set_weight(int node_id, double weight);

    // The node's share of its parent. Every node has one; the root is TOTAL.
    double weight_of(int node_id) const;

    // Every node's children present and summing to TOTAL, written through.
    // Runs at load and on every shape change, so a newly added node is never
    // the one child without a weight. Idempotent.
    void normalize();

    // Priority for every node, computed top-down in one pass. Returns the
    // whole map because resolving one node costs the same walk as resolving
    // all of them — hold the result, don't call this in a loop.
    std::unordered_map<int, double> priorities() const;

    // --- Links between projects and the goals they serve ---
    //
    // A link carries two weights running opposite ways round the loop, and
    // neither can be computed from the other:
    //
    //   project_share  how much of this project is about this goal.
    //                  Sums to TOTAL across a project's links. Attributes
    //                  logged time back to life branches, so it feeds review.
    //
    //   goal_share     how much of this goal this project delivers.
    //                  Sums to TOTAL across a leaf's projects. Divides the
    //                  leaf's priority, so it feeds scheduling.
    //
    // The axes are independent — a project's shares normalize against its
    // own other links, a leaf's against other projects — so setting one
    // direction never disturbs the other.
    void set_link(int project_root_id, int leaf_id);
    void set_project_share(int project_root_id, int leaf_id, double share);
    void set_goal_share(int project_root_id, int leaf_id, double share);
    void clear_link(int project_root_id, int leaf_id);
    bool has_link(int project_root_id, int leaf_id) const;
    double project_share(int project_root_id, int leaf_id) const;
    double goal_share(int project_root_id, int leaf_id) const;

    std::vector<int> leaves_for(int project_root_id) const;
    std::vector<int> projects_for(int leaf_id) const;

    // Each leaf divides ITS OWN priority among the projects serving it, so
    // the total can't exceed TOTAL. Unlinked projects appear at zero — that
    // one serves nothing you cared about is the finding.
    std::unordered_map<int, double> project_priorities() const;

    // Highest priority first, ties by id so the order is stable across
    // calls rather than shifting with hash iteration.
    std::vector<int> ranked_projects() const;

    // The life tree's leaves in the same order, by the same rule. Tree order
    // is the order you happened to build the tree in; this is the order the
    // tree says they matter in.
    std::vector<int> ranked_leaves() const;

    // Leaves carrying priority no project serves, highest first.
    //
    // Empty means full coverage ONLY if the life tree has leaves — a tree
    // that's still just a root also returns nothing here, and rendering
    // that as full coverage is backwards.
    std::vector<std::pair<int, double>> unserved_leaves() const;

    // Something moved; re-read what you show. Coarse, like the tree's.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    struct LinkWeights {
        double project_share = 0.0;
        double goal_share = 0.0;
    };

    // Both write through to database and cache with no rebalancing and no
    // signal: every caller is mid-way through restoring an invariant, and
    // emitting here would publish a half-adjusted set.
    void write_weight(int node_id, double weight);
    void write_link(int project_root_id, int leaf_id, const LinkWeights& weights);

    // Restores the summing-to-TOTAL invariant on each axis independently.
    // Also the migration: links written before goal_share existed arrive at
    // 0 and come out an even split.
    void normalize_links();

    // Splits one node's priority across its children into out. Every branch
    // preserves the total exactly.
    void distribute(int node_id, double budget, std::unordered_map<int, double>& out) const;

    // A link to a node that has since gained children is stale — that
    // node's priority now flows to its children, so honouring the link too
    // would count the same share twice.
    bool is_leaf_target(int node_id) const;

    // The same guard from the project end. Deleting a project cascades
    // project_links away on disk but not from m_links, and a dead project
    // keeps taking a portion of every leaf it served. At point of use rather
    // than purged on a signal, which can't be reentered mid-mutation.
    bool is_live_project(int project_root_id) const;

    std::shared_ptr<Database> m_db;
    TreeController& m_life;
    TreeController& m_projects;

    // Every life node except the root. Complete, not sparse — normalize()
    // guarantees it, so absence means the node isn't in the tree.
    std::unordered_map<int, double> m_weights;

    // project_root_id -> (leaf_id -> weights). Finding a leaf's projects
    // means scanning, which beats a second index that can fall out of step.
    std::unordered_map<int, std::unordered_map<int, LinkWeights>> m_links;

    sigc::signal<void()> m_changed;
};

#endif
