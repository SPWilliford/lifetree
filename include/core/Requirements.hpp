#ifndef REQUIREMENTS_HPP
#define REQUIREMENTS_HPP

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"

class TreeController;

// One line of the ranked list: a requirement, or a leaf with none yet.
struct RankedRequirement {
    int requirement_id = -1;  // -1 for a leaf with none yet
    int leaf_id = -1;         // for a requirement, the leaf giving it the most
    double share = 0.0;       // of the whole; every line together sums to it
};

// What must be true for each life tree leaf, and the projects addressing
// it. Only leaves have requirements, and one requirement can be shared by
// several leaves. Projects link at their root.
class Requirements {
public:
    Requirements(std::shared_ptr<Database> db, const TreeController& life,
                 const TreeController& projects);

    // Call once at startup, after the life tree has loaded.
    void load();

    // Moves requirements off any node that has gained children down to its
    // first leaf, deletes requirements no leaf uses, and drops project links
    // from anything that isn't a top-level project. Runs at startup and on
    // every change to either tree.
    void normalize();

    bool is_leaf(int node_id) const;

    // In the leaf's order.
    std::vector<int> requirements_of(int leaf_id) const;
    std::string title(int requirement_id) const;

    // Appends to the leaf's list. A title matching an existing requirement,
    // ignoring case, shares that one instead of making a duplicate.
    // Returns its id, or -1.
    int add(int leaf_id, std::string_view title);

    // Appends an existing requirement to this leaf's list.
    void attach(int leaf_id, int requirement_id);

    // A requirement taken off its last leaf is deleted.
    void detach(int leaf_id, int requirement_id);

    // Renames it under every leaf. A blank title is ignored.
    void rename(int requirement_id, std::string_view title);

    void move(int leaf_id, int requirement_id, int index);

    // Requirements this leaf doesn't have whose titles contain text,
    // ignoring case. Titles starting with it come first.
    std::vector<int> suggestions(int leaf_id, std::string_view text) const;

    // Each leaf's priority passed down to its requirements, more to the
    // ones higher in its list. A shared requirement collects from every
    // leaf it's under. Highest first.
    std::vector<RankedRequirement> ranked(const std::unordered_map<int, double>& priorities) const;

    // --- projects ---
    // In project id order.
    std::vector<int> projects_of(int requirement_id) const;
    bool is_served(int requirement_id) const;
    void link_project(int requirement_id, int project_root_id);
    void unlink_project(int requirement_id, int project_root_id);

    // Each top-level project's priority for the task list: the shares of the
    // requirements it serves, each split evenly among its projects. A
    // project serving nothing is at zero.
    std::unordered_map<int, double> project_priorities(
        const std::unordered_map<int, double>& priorities) const;

    // How a project's logged time divides among leaves: evenly across the
    // requirements it serves, then across each requirement's leaves by what
    // they give it. Sums to one; empty for a project serving nothing.
    std::unordered_map<int, double> time_split(
        int project_root_id, const std::unordered_map<int, double>& priorities) const;

    sigc::connection connect_changed(const sigc::slot<void()>& slot);

private:
    void write_order(int leaf_id, const std::vector<int>& order);

    // Rereads the database, and signals if anything differs.
    void reload();

    bool is_top_level_project(int project_id) const;

    // The ranking formula, in one place: requirement id -> (leaf id, part of
    // that leaf's priority it receives), leaves in id order.
    using Parts = std::vector<std::pair<int, double>>;
    std::unordered_map<int, Parts> contributions(
        const std::unordered_map<int, double>& priorities) const;

    std::shared_ptr<Database> m_db;
    const TreeController& m_life;
    const TreeController& m_projects;

    std::unordered_map<int, std::string> m_titles;
    std::unordered_map<int, std::vector<int>> m_by_leaf;
    std::unordered_map<int, std::vector<int>> m_projects_of;

    sigc::signal<void()> m_changed;
};

#endif
