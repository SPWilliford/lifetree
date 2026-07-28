#ifndef TREECONTROLLER_HPP
#define TREECONTROLLER_HPP

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <sigc++/signal.h>
#include "core/Tree.hpp"
#include "core/Database.hpp"

// Owns one tree's in-memory cache and its mirror in the database. One
// instance per tree — there's no LIFE/PROJECTS branching inside, because
// each instance only ever knows about its own.
//
// Every mutation writes to the database first and only touches the cache
// if that succeeded, so the two can't disagree.
class TreeController {
public:
    TreeController(std::shared_ptr<Database> db, TreeType type)
        : m_db(std::move(db)), m_type(type) {}

    // Pulls every row for this tree's table and rebuilds the cache. Call
    // once at startup, after the root row exists.
    void load() {
        m_tree.load(m_db->load(m_type));
    }

    int add(int parent_id, std::string_view text) {
        int position = m_tree.next_child_position(parent_id);

        int new_id = m_db->insert(m_type, parent_id, position, text);
        if (new_id == -1) return -1; // DB write failed — don't touch the cache

        m_tree.add(new_id, parent_id, position, std::string(text));
        m_changed.emit();
        return new_id;
    }

    void edit(int id, std::string_view new_text) {
        if (!m_tree.contains(id)) return;
        if (!m_db->write_title(m_type, id, new_text)) return; // leave the cache as-is
        m_tree.get_mut(id).title = new_text;
        m_changed.emit();
    }

    void remove(int id) {
        if (id <= 0) return; // protect the synthetic root
        if (!m_db->remove(m_type, id)) return; // leave the cache as-is
        m_tree.remove(id);
        m_changed.emit();
    }

    std::string get_title(int id) const {
        return m_tree.contains(id) ? m_tree.get(id).title : "";
    }

    bool contains(int id) const {
        return m_tree.contains(id);
    }

    std::vector<int> children_of(int id) const {
        return m_tree.contains(id) ? m_tree.get(id).children : std::vector<int>{};
    }

    // -1 if the node doesn't exist or has no parent (the synthetic root).
    int parent_of(int id) const {
        return m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
    }

    // Ancestor chain as "Grandparent > Parent", excluding both the
    // synthetic root and the node itself — just where it lives, not what
    // it is. Meant to be captured as a snapshot right before a node is
    // deleted; once it's gone, this can't be reconstructed.
    std::string ancestor_path(int id) const {
        std::vector<std::string> parts;
        int current = m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
        while (current > 0 && m_tree.contains(current)) {
            parts.push_back(m_tree.get(current).title);
            current = m_tree.get(current).parent_id;
        }

        std::string path;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!path.empty()) path += " > ";
            path += *it;
        }
        return path;
    }

    // Every leaf in the whole tree, excluding the synthetic root anchor
    // (id 0) — that's never a real item, even if the tree is empty.
    std::vector<int> leaves() const {
        std::vector<int> result;
        for (int id : m_tree.leaf_ids()) {
            if (id != 0) result.push_back(id);
        }
        return result;
    }

    // Fires after any successful add/edit/remove. Coarse on purpose: it
    // doesn't say what changed, just that something did. A subscriber
    // should treat it as "go re-read what you need." Whoever made the
    // change already knows what happened and has updated their own view —
    // this signal is for everyone else.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    std::shared_ptr<Database> m_db;
    TreeType m_type;
    Tree m_tree;
    sigc::signal<void()> m_changed;
};

#endif
