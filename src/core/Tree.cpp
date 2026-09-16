#include "core/Tree.hpp"

#include <algorithm>

int Tree::next_child_position(int parent_id) const {
    if (!contains(parent_id)) return 0;
    int max_pos = -1;
    for (int child_id : m_nodes.at(parent_id).children) {
        max_pos = std::max(max_pos, m_nodes.at(child_id).position);
    }
    return max_pos + 1;
}

std::vector<int> Tree::leaf_ids() const {
    std::vector<int> leaves;
    for (const auto& [id, node] : m_nodes) {
        if (node.children.empty()) leaves.push_back(id);
    }
    return leaves;
}

void Tree::load(const std::vector<Row>& rows) {
    m_nodes.clear();
    for (const auto& r : rows) {
        m_nodes[r.id] = Node{r.title, r.parent_id, r.position, {}};
    }
    for (const auto& [id, node] : m_nodes) {
        if (node.parent_id != -1 && contains(node.parent_id)) {
            m_nodes[node.parent_id].children.push_back(id);
        }
    }
    for (auto& [id, node] : m_nodes) sort_children(id);
}

void Tree::add(int id, int parent_id, int position, std::string title) {
    m_nodes[id] = Node{std::move(title), parent_id, position, {}};
    if (parent_id != -1 && contains(parent_id)) {
        m_nodes[parent_id].children.push_back(id);
    }
}

void Tree::sort_children(int parent_id) {
    if (!contains(parent_id)) return;
    auto& children = m_nodes[parent_id].children;
    std::sort(children.begin(), children.end(),
              [this](int a, int b) { return m_nodes[a].position < m_nodes[b].position; });
}

void Tree::remove(int id) {
    if (!contains(id)) return;
    const int parent_id = m_nodes[id].parent_id;
    if (parent_id != -1 && contains(parent_id)) {
        auto& siblings = m_nodes[parent_id].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    }
    const std::vector<int> children = m_nodes[id].children;
    m_nodes.erase(id);
    for (int child_id : children) remove(child_id);
}
