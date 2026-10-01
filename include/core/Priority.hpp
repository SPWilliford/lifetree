#ifndef PRIORITY_HPP
#define PRIORITY_HPP

#include <memory>
#include <unordered_map>

#include <sigc++/signal.h>

#include "core/Database.hpp"

class TreeController;

// Weights on life tree nodes, cascaded root to leaf. Every child of a node
// has one, and a node's children always sum to TOTAL. Shares are whole
// numbers.
class Priority {
public:
    static constexpr double TOTAL = 100.0;

    Priority(std::shared_ptr<Database> db, TreeController& life);

    // Call once at startup, after the life tree has loaded.
    void load();

    // Sets one node's share of its parent and scales its siblings into the
    // rest. An only child is always TOTAL.
    void set_weight(int node_id, double weight);

    // The node's share of its parent; the root is TOTAL.
    double weight_of(int node_id) const;

    // Restores the invariant: each child set present and summing to TOTAL.
    // Runs at load and on every tree change. Idempotent.
    //
    // A node without a weight joins its siblings evenly if they are still
    // an even split, and at zero if they have been weighted by hand.
    void normalize();

    // Every node's share of the whole tree. One walk for all of them; hold
    // the result rather than calling per node.
    std::unordered_map<int, double> priorities() const;

    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    // Write-through to the database and cache; no rebalancing, no signal.
    void write_weight(int node_id, double weight);

    std::shared_ptr<Database> m_db;
    TreeController& m_life;

    // Every life node except the root.
    std::unordered_map<int, double> m_weights;

    sigc::signal<void()> m_changed;
};

#endif
