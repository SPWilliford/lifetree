#ifndef TREECONTROLLER_HPP
#define TREECONTROLLER_HPP

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"
#include "core/Tree.hpp"

// One tree's in-memory cache and its mirror in the database. Every mutation
// writes to the database first and touches the cache only on success.
class TreeController {
public:
    TreeController(std::shared_ptr<Database> db, TreeType type);

    // Call once at startup, after the root row exists.
    void load();

    // Returns the new id, or -1 if the write failed.
    int add(int parent_id, std::string_view text);

    // Renames. A brace range ("Problem {1-16}") expands into siblings: this
    // node keeps its identity and becomes the first; the rest are inserted
    // after it.
    void edit(int id, std::string_view new_text);

    // Removes the node and its subtree. Refuses the root.
    void remove(int id);

    // The most nodes one title may expand into.
    static constexpr int EXPANSION_CAP = 200;

    // "Problem {1-48}" or "{1..48}" into a title per number; counts down if
    // the range does. Anything unparseable, two brace groups, or a range
    // past EXPANSION_CAP comes back as the single literal title.
    static std::vector<std::string> expand(std::string_view text);

    // The stored title, which may be blank.
    std::string get_title(int id) const;

    // "Untitled" in place of a blank title, for panels that can't rename.
    std::string display_title(int id) const;

    // The node's long-form description. Empty when none has been written.
    std::string get_seed(int id) const;
    bool has_seed(int id) const;

    // An empty seed removes it.
    void set_seed(int id, std::string_view seed);

    bool contains(int id) const;
    std::vector<int> children_of(int id) const;

    // -1 for the root or an unknown id.
    int parent_of(int id) const;

    // "Grandparent > Parent", excluding the root and the node itself.
    std::string ancestor_path(int id) const;

    // Every leaf except the root.
    std::vector<int> leaves() const;

    // Coarse: something changed, re-read what you need.
    sigc::connection connect_changed(const sigc::slot<void()>& slot);

private:
    // Inserts titles[1..] as siblings directly after sibling_id, pushing
    // later siblings' positions down to make room.
    void insert_after(int sibling_id, const std::vector<std::string>& titles);

    std::shared_ptr<Database> m_db;
    TreeType m_type;
    Tree m_tree;
    std::unordered_map<int, std::string> m_seeds;  // sparse
    sigc::signal<void()> m_changed;
};

#endif
