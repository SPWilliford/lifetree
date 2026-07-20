#ifndef TREECONTROLLER_HPP
#define TREECONTROLLER_HPP

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <concepts>
#include <sigc++/signal.h>
#include "model/Tree.hpp"
#include "engine/Database.hpp"

// Non-template interface so a widget that needs to hold "whichever tree
// is active right now" (e.g. TreePanel switching between life/projects)
// can depend on one reference type instead of two different template
// instantiations.
class ITreeController {
public:
    virtual ~ITreeController() = default;

    virtual int add(int parent_id, std::string_view text) = 0;
    virtual void edit(int id, std::string_view new_text) = 0;
    virtual void remove(int id) = 0;
    virtual std::string get_title(int id) const = 0;
    virtual bool contains(int id) const = 0;
    virtual std::vector<int> children_of(int id) const = 0;

    // -1 if the node doesn't exist or has no parent (the synthetic root).
    virtual int parent_of(int id) const = 0;

    // Ancestor chain as "Grandparent > Parent", excluding both the
    // synthetic root and the node itself — just where it lives, not what
    // it is. Meant to be captured as a snapshot right before a node is
    // deleted — once it's gone, this can't be reconstructed.
    virtual std::string ancestor_path(int id) const = 0;

    // Every leaf node in the whole tree, excluding the synthetic root
    // anchor (id 0) — that's never a real item, even if the tree is empty.
    virtual std::vector<int> leaves() const = 0;

    // Fires after any successful add/edit/remove. Coarse on purpose: it
    // doesn't say what changed, just that something did. A subscriber
    // (e.g. TaskPanel) should treat this as "go re-read what you need."
    // Whoever made the change already knows what happened and updated
    // their own view directly — this signal is for everyone else.
    virtual sigc::connection connect_changed(const sigc::slot<void()>& slot) = 0;
};

// edit(), get_title(), and ancestor_path() below reach past the mapper
// and touch T::title directly — cheaper than threading a getter/setter
// through every TreeController for a field every T happens to share.
// Constraining the template here means a future T that breaks this
// assumption fails loudly at the instantiation site, not silently at
// whichever call turns out to touch .title.
template <typename T>
concept HasTitle = requires(T t) {
    { t.title } -> std::convertible_to<std::string>;
};

// Owns one tree's cache + its mirror in the database. One instance per
// node type (LifeNode, TaskNode, ...) — no LIFE/PROJECTS branching inside,
// because each instance only ever knows about its own tree.
template <typename T>
    requires HasTitle<T>
class TreeController : public ITreeController {
public:
    TreeController(std::shared_ptr<Database> db, TreeType type,
                   std::function<T(const Row&)> mapper)
        : m_db(std::move(db)), m_type(type), m_mapper(std::move(mapper)) {}

    // Pulls every row for this tree's table and rebuilds the in-memory
    // cache. Call once at startup, after the root row exists.
    void load() {
        auto rows = m_db->load(m_type);
        m_tree.load(rows, m_mapper);
    }

    int add(int parent_id, std::string_view text) override {
        int position = m_tree.next_child_position(parent_id);

        int new_id = m_db->insert(m_type, parent_id, position, text);
        if (new_id == -1) return -1; // DB write failed — don't touch the cache

        m_tree.add(new_id, parent_id, position, m_mapper(Row{new_id, parent_id, position, std::string(text)}));
        m_changed.emit();
        return new_id;
    }

    void edit(int id, std::string_view new_text) override {
        if (!m_tree.contains(id)) return;
        if (!m_db->write_title(m_type, id, new_text)) return; // DB write failed — leave the cache as-is
        m_tree.get_mut(id).data.title = new_text;
        m_changed.emit();
    }

    void remove(int id) override {
        if (id <= 0) return; // protect the synthetic root
        if (!m_db->remove(m_type, id)) return; // DB write failed — leave the cache as-is
        m_tree.remove(id);
        m_changed.emit();
    }

    std::string get_title(int id) const override {
        return m_tree.contains(id) ? m_tree.get(id).data.title : "";
    }

    bool contains(int id) const override {
        return m_tree.contains(id);
    }

    std::vector<int> children_of(int id) const override {
        return m_tree.contains(id) ? m_tree.get(id).children : std::vector<int>{};
    }

    int parent_of(int id) const override {
        return m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
    }

    std::string ancestor_path(int id) const override {
        std::vector<std::string> parts;
        int current = m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
        while (current > 0 && m_tree.contains(current)) {
            parts.push_back(m_tree.get(current).data.title);
            current = m_tree.get(current).parent_id;
        }

        std::string path;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!path.empty()) path += " > ";
            path += *it;
        }
        return path;
    }

    std::vector<int> leaves() const override {
        std::vector<int> result;
        for (int id : m_tree.leaf_ids()) {
            if (id != 0) result.push_back(id);
        }
        return result;
    }

    sigc::connection connect_changed(const sigc::slot<void()>& slot) override {
        return m_changed.connect(slot);
    }

    // Typed access, for anything that needs T directly rather than just ids.
    const Tree<T>& tree() const { return m_tree; }

private:
    std::shared_ptr<Database> m_db;
    TreeType m_type;
    std::function<T(const Row&)> m_mapper;
    Tree<T> m_tree;
    sigc::signal<void()> m_changed;
};

#endif
