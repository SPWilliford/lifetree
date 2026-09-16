#ifndef TREE_HPP
#define TREE_HPP

#include <string>
#include <unordered_map>
#include <vector>

// Which of the two trees. Same structure, different table and meaning.
enum class TreeType { LIFE, PROJECTS };

// One node as stored. parent_id is -1 for the root.
struct Row {
    int id;
    int parent_id;
    int position;
    std::string title;
};

struct Node {
    std::string title;
    int parent_id = -1;
    int position = 0;
    std::vector<int> children;  // in position order
};

// In-memory tree with no knowledge of storage.
class Tree {
public:
    // The synthetic root of either tree. Never a real item.
    static constexpr int ROOT_ID = 0;

    Tree() = default;

    const Node& get(int id) const { return m_nodes.at(id); }
    Node& get_mut(int id) { return m_nodes.at(id); }
    bool contains(int id) const { return m_nodes.find(id) != m_nodes.end(); }

    // One past the highest position among the children — not
    // children.size(), since positions aren't renumbered on removal.
    int next_child_position(int parent_id) const;

    std::vector<int> leaf_ids() const;

    void load(const std::vector<Row>& rows);
    void add(int id, int parent_id, int position, std::string title);

    // Restores position order after an insert between siblings.
    void sort_children(int parent_id);

    // Removes the node and its whole subtree.
    void remove(int id);

private:
    std::unordered_map<int, Node> m_nodes;
};

#endif
