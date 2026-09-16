#ifndef TREECONTROLLER_HPP
#define TREECONTROLLER_HPP

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sigc++/signal.h>

#include "core/Database.hpp"
#include "core/Tree.hpp"

// One tree's in-memory cache and its mirror in the database. One instance
// per tree; no LIFE/PROJECTS branching inside.
//
// Every mutation writes to the database first and touches the cache only if
// that succeeded, so the two can't disagree.
class TreeController {
public:
    TreeController(std::shared_ptr<Database> db, TreeType type)
        : m_db(std::move(db)), m_type(type) {}

    // Call once at startup, after the root row exists.
    void load() {
        m_tree.load(m_db->load(m_type));

        m_seeds.clear();
        for (const auto& row : m_db->load_seeds(m_type)) {
            m_seeds[row.node_id] = row.seed;
        }
    }

    int add(int parent_id, std::string_view text) {
        int position = m_tree.next_child_position(parent_id);

        int new_id = m_db->insert(m_type, parent_id, position, text);
        if (new_id == -1) return -1;  // DB write failed — don't touch the cache

        m_tree.add(new_id, parent_id, position, std::string(text));
        m_changed.emit();
        return new_id;
    }

    // The most nodes one title may expand into. A textbook section is
    // dozens; four figures is a typo, and creating them is far cheaper than
    // deleting them again one at a time.
    static constexpr int EXPANSION_CAP = 200;

    // "Problem {1-48}" into forty-eight titles. Also accepts {1..48}, and
    // counts down when the range does.
    //
    // Anything it can't read comes back as the title itself, unchanged and
    // without complaint: {1..}, {a..z}, two brace groups, a range past the
    // cap. The feature is invisible when unused and silent when misused,
    // which is what lets a title contain a brace without needing an escape.
    static std::vector<std::string> expand(std::string_view text) {
        const std::string title(text);
        const std::vector<std::string> literal{title};

        const auto open = title.find('{');
        if (open == std::string::npos) return literal;
        const auto close = title.find('}', open + 1);
        if (close == std::string::npos) return literal;

        // One group only. A cartesian product is where this stops being
        // predictable, so a second group means the whole title is literal.
        if (title.find('{', open + 1) != std::string::npos) return literal;
        if (title.find('}', close + 1) != std::string::npos) return literal;

        const std::string inside = title.substr(open + 1, close - open - 1);

        // ".." first: a hyphen also appears inside "1..48" would-be ranges
        // only as a minus sign, and leading position 0 is never a separator.
        std::size_t sep = inside.find("..");
        std::size_t sep_len = 2;
        if (sep == std::string::npos) {
            sep = inside.find('-', 1);
            sep_len = 1;
        }
        if (sep == std::string::npos || sep == 0) return literal;

        const std::string first_text = inside.substr(0, sep);
        const std::string last_text = inside.substr(sep + sep_len);
        if (!all_digits(first_text) || !all_digits(last_text)) return literal;

        const long first = std::strtol(first_text.c_str(), nullptr, 10);
        const long last = std::strtol(last_text.c_str(), nullptr, 10);

        const long count = std::labs(last - first) + 1;
        if (count > EXPANSION_CAP) return literal;

        const std::string prefix = title.substr(0, open);
        const std::string suffix = title.substr(close + 1);
        const long step = (last >= first) ? 1 : -1;

        std::vector<std::string> out;
        out.reserve(static_cast<std::size_t>(count));
        for (long n = first;; n += step) {
            out.push_back(prefix + std::to_string(n) + suffix);
            if (n == last) break;
        }
        return out;
    }

    // Renames, expanding a brace range into siblings.
    //
    // The node KEEPS ITS IDENTITY and becomes the first of the series; the
    // rest are inserted after it. Not "replaced by forty-eight", which would
    // silently destroy its children, its colour and its repeat schedule —
    // a rename must never be destructive.
    void edit(int id, std::string_view new_text) {
        if (!m_tree.contains(id)) return;

        const std::vector<std::string> titles = expand(new_text);

        if (!m_db->write_title(m_type, id, titles.front())) return;  // leave the cache as-is
        m_tree.get_mut(id).title = titles.front();

        // One signal for the whole batch. Forty-eight adds would otherwise
        // emit forty-eight times; Refresh coalesces the redraws, but the
        // writes and cache updates would still be interleaved with them.
        if (titles.size() > 1) insert_after(id, titles);

        m_changed.emit();
    }

    void remove(int id) {
        if (id <= 0) return;                    // protect the synthetic root
        if (!m_db->remove(m_type, id)) return;  // leave the cache as-is
        m_tree.remove(id);
        m_changed.emit();
    }

    // The stored title, blank included. The tree panels use this: a node you
    // left unnamed should show as empty in the place you can rename it.
    std::string get_title(int id) const { return m_tree.contains(id) ? m_tree.get(id).title : ""; }

    // For every other panel, which lists nodes without offering a rename.
    // A blank row there is just an unreadable one.
    std::string display_title(int id) const {
        std::string title = get_title(id);
        return title.empty() ? "Untitled" : title;
    }

    // What this node MEANS, at whatever length it takes — as against the
    // title, which is the handle you scan for in a list.
    //
    // Empty for a node nobody has written one for, which is most of them.
    // That emptiness is the point: it's what makes "which parts of the tree
    // are still undefined" answerable.
    std::string get_seed(int id) const {
        auto it = m_seeds.find(id);
        return (it != m_seeds.end()) ? it->second : "";
    }

    bool has_seed(int id) const { return m_seeds.find(id) != m_seeds.end(); }

    void set_seed(int id, std::string_view seed) {
        if (!m_tree.contains(id)) return;
        if (!m_db->set_seed(m_type, id, seed)) return;  // leave the cache as-is

        if (seed.empty())
            m_seeds.erase(id);
        else
            m_seeds[id] = std::string(seed);
        m_changed.emit();
    }

    bool contains(int id) const { return m_tree.contains(id); }

    std::vector<int> children_of(int id) const {
        return m_tree.contains(id) ? m_tree.get(id).children : std::vector<int>{};
    }

    // -1 if the node doesn't exist or has no parent (the synthetic root).
    int parent_of(int id) const { return m_tree.contains(id) ? m_tree.get(id).parent_id : -1; }

    // "Grandparent > Parent" — where a node lives, not what it is.
    // Excludes the synthetic root and the node itself. Capture it before
    // deleting a node; afterwards it can't be reconstructed.
    std::string ancestor_path(int id) const {
        std::vector<std::string> parts;
        int current = m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
        while (current > 0 && m_tree.contains(current)) {
            parts.push_back(display_title(current));
            current = m_tree.get(current).parent_id;
        }

        std::string path;
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            if (!path.empty()) path += " > ";
            path += *it;
        }
        return path;
    }

    // Every leaf except the root anchor, which is never a real item.
    std::vector<int> leaves() const {
        std::vector<int> result;
        for (int id : m_tree.leaf_ids()) {
            if (id != 0) result.push_back(id);
        }
        return result;
    }

    // Something changed; re-read what you need. Coarse on purpose — whoever
    // made the change already updated their own view, so this is for
    // everyone else.
    sigc::connection connect_changed(const sigc::slot<void()>& slot) {
        return m_changed.connect(slot);
    }

private:
    static bool all_digits(const std::string& text) {
        if (text.empty()) return false;
        return std::all_of(text.begin(), text.end(),
                           [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    // Inserts titles[1..] as siblings directly after sibling_id.
    //
    // Positions aren't dense — removal doesn't renumber — so the later
    // siblings are pushed down by the number being inserted rather than
    // renumbered from scratch. Cheaper, and it leaves every other position
    // exactly as it was.
    void insert_after(int sibling_id, const std::vector<std::string>& titles) {
        const int parent_id = m_tree.get(sibling_id).parent_id;
        if (parent_id < 0) return;  // the synthetic root has no sibling list

        const int base = m_tree.get(sibling_id).position;
        const int inserted = static_cast<int>(titles.size()) - 1;

        for (int child_id : m_tree.get(parent_id).children) {
            if (child_id == sibling_id) continue;
            const int position = m_tree.get(child_id).position;
            if (position <= base) continue;
            if (!m_db->set_position(m_type, child_id, position + inserted)) continue;
            m_tree.get_mut(child_id).position = position + inserted;
        }

        for (std::size_t i = 1; i < titles.size(); ++i) {
            const int position = base + static_cast<int>(i);
            const int new_id = m_db->insert(m_type, parent_id, position, titles[i]);
            if (new_id == -1) continue;  // DB write failed — don't touch the cache
            m_tree.add(new_id, parent_id, position, titles[i]);
        }

        // Tree::add appends, so the child list is out of order until this.
        m_tree.sort_children(parent_id);
    }

    std::shared_ptr<Database> m_db;
    TreeType m_type;
    Tree m_tree;

    // Sparse, mirroring the table: an entry exists only where someone wrote
    // a seed. Cached because the row decoration asks per node per draw.
    std::unordered_map<int, std::string> m_seeds;
    sigc::signal<void()> m_changed;
};

#endif
