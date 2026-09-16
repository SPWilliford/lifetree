#ifndef TREE_HPP
#define TREE_HPP
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

// Which of the two trees. Same structure; the distinction is what they mean
// and which table they persist to.
enum class TreeType { LIFE, PROJECTS };

struct Node {
    std::string title;
    int parent_id = -1;
    int position = 0;
    std::vector<int> children;
};

class Tree {
private:
    std::unordered_map<int, Node> m_nodes;

public:
    Tree() = default;
    const Node& get(int id) const { return m_nodes.at(id); }
    Node& get_mut(int id) { return m_nodes.at(id); }
    bool contains(int id) const { return m_nodes.find(id) != m_nodes.end(); }

    // One greater than the highest position among the current children —
    // NOT children.size(). Positions aren't renumbered on removal, so
    // size() can hand out a position a surviving sibling still holds.
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

    // Templated on the container rather than including Database.hpp, so
    // this stays a plain data structure with no dependency on storage.
    // Rows need id, parent_id, position, title.
    void load(const auto& rows) {
        m_nodes.clear();

        // 1: allocate
        for (const auto& r : rows) {
            m_nodes[r.id] = Node{r.title, r.parent_id, r.position, {}};
        }
        // 2: link parents to children
        for (const auto& [id, node] : m_nodes) {
            if (node.parent_id != -1 && contains(node.parent_id)) {
                m_nodes[node.parent_id].children.push_back(id);
            }
        }
        // 3: sort each child list into stored position order
        for (auto& [id, node] : m_nodes) {
            std::sort(node.children.begin(), node.children.end(),
                      [this](int a, int b) { return m_nodes[a].position < m_nodes[b].position; });
        }
    }

    void add(int id, int parent_id, int position, std::string title) {
        m_nodes[id] = Node{std::move(title), parent_id, position, {}};
        if (parent_id != -1 && contains(parent_id)) {
            m_nodes[parent_id].children.push_back(id);
        }
    }

    // Puts a parent's child list back into stored position order. load()
    // does this at startup; anything that inserts between siblings, rather
    // than appending after them, has to do it again.
    void sort_children(int parent_id) {
        if (!contains(parent_id)) return;
        auto& children = m_nodes[parent_id].children;
        std::sort(children.begin(), children.end(),
                  [this](int a, int b) { return m_nodes[a].position < m_nodes[b].position; });
    }

    void remove(int id) {
        if (m_nodes.find(id) == m_nodes.end()) return;
        int p_id = m_nodes[id].parent_id;
        if (p_id != -1 && contains(p_id)) {
            auto& s_children = m_nodes[p_id].children;
            s_children.erase(std::remove(s_children.begin(), s_children.end(), id),
                             s_children.end());
        }
        std::vector<int> children_to_clean = m_nodes[id].children;
        m_nodes.erase(id);
        for (int child_id : children_to_clean) {
            remove(child_id);
        }
    }
};
#endif
