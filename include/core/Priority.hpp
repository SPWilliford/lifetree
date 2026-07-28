#ifndef PRIORITY_HPP
#define PRIORITY_HPP

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sigc++/signal.h>
#include "core/Database.hpp"

class TreeController;

// Turns what the user says matters into numbers that can be compared.
//
// The user weights life tree nodes relative to their siblings, and this
// cascades those weights from the root down to the leaves. The rule that
// makes it work: a node passes its ENTIRE priority to its children, so
// siblings always sum to exactly their parent. Nothing is created or lost
// on the way down, which is what makes a leaf under "finances" comparable
// to a leaf under "health" — both are shares of the same 100.
//
// Weights are sparse. The user isn't expected to weight every level: they
// might split the top carefully and leave a whole branch alone. A node
// with no weight of its own takes an even share of whatever its weighted
// siblings left unclaimed, so an unfinished tree still produces a complete,
// correctly-summing set of priorities rather than holes.
//
// One consequence worth knowing: because a node's share divides among its
// children, the SHAPE of the tree is itself a priority signal. A branch
// split into eight fine-grained leaves spreads its share thin; one with a
// single leaf concentrates it. That's defensible — more sub-goals really
// does mean each is a smaller piece — but it means someone who breaks down
// the areas they've thought hardest about will quietly downweight them.
// Worth surfacing effective priorities in any planning view so that's
// visible rather than a surprise.
//
// Nothing derived is stored. Priorities are recomputed from the weights on
// demand, because a stored derived value is a value that drifts.
class Priority {
public:
    // What the root holds, and the scale weights are entered on. Both are
    // 100 so a leaf's priority reads directly as a percentage of the whole
    // — "this leaf is 12.5" means 12.5% of everything the user cares about.
    static constexpr double TOTAL = 100.0;

    Priority(std::shared_ptr<Database> db, TreeController& life, TreeController& projects);

    // Pulls stored weights into the cache. Call once at startup, after the
    // life tree itself has loaded.
    void load();

    // How much of its parent's priority this node claims, on a 0..TOTAL
    // scale. Clamped to that range — a negative weight would let a subtree
    // take priority away from its siblings, which isn't a thing the model
    // has any meaning for.
    void set_weight(int node_id, double weight);

    // Back to "unweighted", which is not the same as a weight of zero: an
    // unweighted node shares out the remainder with its unweighted
    // siblings, whereas an explicit zero genuinely claims nothing.
    void clear_weight(int node_id);

    bool has_weight(int node_id) const;

    // The stored weight, or 0 if there isn't one. Check has_weight() to
    // tell "unweighted" from "explicitly zero".
    double weight_of(int node_id) const;

    // Priority for every node in the life tree, computed top-down in a
    // single pass. Includes intermediate nodes, not just leaves — a
    // planning view wants to show a branch's total as well as its parts.
    //
    // Returns a whole map rather than answering per node because resolving
    // any one node costs the same walk as resolving all of them. Hold the
    // result; don't call this in a loop.
    std::unordered_map<int, double> priorities() const;

    // --- Associations between projects and the leaves they serve ---
    //
    // weight is a rough "how much of this project is really about this
    // leaf", entered from the project's side because that's the judgement
    // a person can actually make. It is NOT normalised per project — see
    // project_priorities() for why the division has to happen per leaf.
    void set_link(int project_root_id, int leaf_id, double weight);
    void clear_link(int project_root_id, int leaf_id);
    bool has_link(int project_root_id, int leaf_id) const;
    double link_weight(int project_root_id, int leaf_id) const;

    std::vector<int> leaves_for(int project_root_id) const;
    std::vector<int> projects_for(int leaf_id) const;

    // Priority per top-level project. Each leaf divides ITS OWN priority
    // among the projects serving it, in proportion to their weights —
    // rather than each project keeping the full priority of everything it
    // touches, which would count a leaf once per project and inflate the
    // total past 100.
    //
    // The consequence, which will feel wrong the first time: attaching a
    // second project to a leaf HALVES the first one's contribution from it.
    // That's correct — wanting to sleep well didn't become twice as
    // important because you thought of another way to pursue it — but it's
    // worth saying out loud in any UI that shows these numbers.
    //
    // Every top-level project appears, including ones with no links at all.
    // A project at zero isn't serving anything you said you cared about,
    // and surfacing that is the point rather than an oversight.
    std::unordered_map<int, double> project_priorities() const;

    // Projects highest-priority first. Ties break by id so the order is
    // stable across calls rather than shifting with hash iteration.
    std::vector<int> ranked_projects() const;

    // Leaves carrying priority that no project serves, highest first.
    // The payoff of the whole mechanism: "this is 25% of what you said
    // matters, and nothing you're working on touches it."
    //
    // Note for whoever builds the review around this: an empty result means
    // "everything is served" ONLY if the life tree actually has leaves. A
    // tree that is still just the root also returns nothing here, and
    // rendering that as full coverage would be exactly backwards for
    // someone who hasn't defined anything yet.
    std::vector<std::pair<int, double>> unserved_leaves() const;

    // Fires when a weight or an association changes. Coarse, like the
    // tree's own signal: something moved, re-read what you show.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) { return m_changed.connect(slot); }

private:
    // Splits one node's priority across its children, writing each child's
    // result into out. Every branch of this preserves the total exactly.
    void distribute(int node_id, double budget, std::unordered_map<int, double>& out) const;

    // A node counts as a leaf target only if it currently has no children.
    // A link to a node that has since gained children is stale: that node's
    // priority now flows to those children, so honouring the link as well
    // would count the same share twice.
    bool is_leaf_target(int node_id) const;

    std::shared_ptr<Database> m_db;
    TreeController& m_life;
    TreeController& m_projects;

    // Only nodes the user has explicitly weighted. Absence is meaningful.
    std::unordered_map<int, double> m_weights;

    // project_root_id -> (leaf_id -> weight). Finding a leaf's projects
    // means scanning, which is fine at this size and beats keeping a second
    // index that can fall out of step with this one.
    std::unordered_map<int, std::unordered_map<int, double>> m_links;

    sigc::signal<void()> m_changed;
};

#endif
