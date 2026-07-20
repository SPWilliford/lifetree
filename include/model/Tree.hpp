#ifndef TREE_HPP
#define TREE_HPP
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <algorithm>
template <typename T>
struct Node {
    T data;
    int parent_id = -1;
    int position = 0;
    std::vector<int> children;
};
template <typename T>
class Tree {
private:
    std::unordered_map<int, Node<T>> m_nodes;
public:
    Tree() = default;
    const Node<T>& get(int id) const { return m_nodes.at(id); }
    Node<T>& get_mut(int id) { return m_nodes.at(id); }
    bool contains(int id) const { return m_nodes.find(id) != m_nodes.end(); }

    // One greater than the highest position among parent_id's current
    // children — NOT children.size(). Positions aren't renumbered when a
    // sibling is removed, so size() can hand out a position that's still
    // in use by a surviving sibling (e.g. children at positions 0/1/2,
    // remove the middle one, size() drops to 2 but position 2 is taken).
    // Falls back to 0 when the parent doesn't exist or has no children.
    int next_child_position(int parent_id) const {
        if (!contains(parent_id)) return 0;
        int max_pos = -1;
        for (int child_id : m_nodes.at(parent_id).children) {
            max_pos = std::max(max_pos, m_nodes.at(child_id).position);
        }
        return max_pos + 1;
    }

    // Every node with no children, anywhere in the tree.
    std::vector<int> leaf_ids() const {
        std::vector<int> leaves;
        for (const auto& [id, node] : m_nodes) {
            if (node.children.empty()) leaves.push_back(id);
        }
        return leaves;
    }

    // Rebuilds the memory tree effortlessly from relational database rows
    void load(const auto& rows, auto&& mapper) {
        m_nodes.clear();
        
        // Pass 1: Allocate nodes into the map lookup cache
        for (const auto& r : rows) {
            m_nodes[r.id] = Node<T>{ mapper(r), r.parent_id, r.position, {} };
        }
        // Pass 2: Establish the parent-to-child relationship links
        for (const auto& [id, node] : m_nodes) {
            if (node.parent_id != -1 && contains(node.parent_id)) {
                m_nodes[node.parent_id].children.push_back(id);
            }
        }
        // Pass 3: Sort child vectors so they perfectly match custom positions
        for (auto& [id, node] : m_nodes) {
            std::sort(node.children.begin(), node.children.end(), [this](int a, int b) {
                return m_nodes[a].position < m_nodes[b].position;
            });
        }
    }
    void add(int id, int parent_id, int position, T data) {
        m_nodes[id] = Node<T>{ std::move(data), parent_id, position, {} };
        if (parent_id != -1 && contains(parent_id)) {
            m_nodes[parent_id].children.push_back(id);
        }
    }
    void remove(int id) {
        if (m_nodes.find(id) == m_nodes.end()) return;
        int p_id = m_nodes[id].parent_id;
        if (p_id != -1 && contains(p_id)) {
            auto& s_children = m_nodes[p_id].children;
            s_children.erase(std::remove(s_children.begin(), s_children.end(), id), s_children.end());
        }
        std::vector<int> children_to_clean = m_nodes[id].children;
        m_nodes.erase(id);
        for (int child_id : children_to_clean) {
            remove(child_id);
        }
    }
};
#endif
