#include "core/TreeController.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {
bool all_digits(const std::string& text) {
    if (text.empty()) return false;
    return std::all_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}
}  // namespace

TreeController::TreeController(std::shared_ptr<Database> db, TreeType type)
    : m_db(std::move(db)), m_type(type) {}

void TreeController::load() {
    m_tree.load(m_db->load(m_type));

    m_seeds.clear();
    for (const auto& row : m_db->load_seeds(m_type)) {
        m_seeds[row.node_id] = row.seed;
    }
}

int TreeController::add(int parent_id, std::string_view text) {
    const int position = m_tree.next_child_position(parent_id);

    const int new_id = m_db->insert(m_type, parent_id, position, text);
    if (new_id == -1) return -1;

    m_tree.add(new_id, parent_id, position, std::string(text));
    m_changed.emit();
    return new_id;
}

std::vector<std::string> TreeController::expand(std::string_view text) {
    const std::string title(text);
    const std::vector<std::string> literal{title};

    const auto open = title.find('{');
    if (open == std::string::npos) return literal;
    const auto close = title.find('}', open + 1);
    if (close == std::string::npos) return literal;
    if (title.find('{', open + 1) != std::string::npos) return literal;
    if (title.find('}', close + 1) != std::string::npos) return literal;

    const std::string inside = title.substr(open + 1, close - open - 1);

    // ".." first; a hyphen at position 0 is a sign, not a separator.
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

void TreeController::edit(int id, std::string_view new_text) {
    if (!m_tree.contains(id)) return;

    const std::vector<std::string> titles = expand(new_text);

    if (!m_db->write_title(m_type, id, titles.front())) return;
    m_tree.get_mut(id).title = titles.front();

    // One signal for the whole batch.
    if (titles.size() > 1) insert_after(id, titles);

    m_changed.emit();
}

void TreeController::insert_after(int sibling_id, const std::vector<std::string>& titles) {
    const int parent_id = m_tree.get(sibling_id).parent_id;
    if (parent_id < 0) return;

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
        if (new_id == -1) continue;
        m_tree.add(new_id, parent_id, position, titles[i]);
    }

    m_tree.sort_children(parent_id);
}

void TreeController::remove(int id) {
    if (id <= Tree::ROOT_ID) return;
    if (!m_db->remove(m_type, id)) return;
    m_tree.remove(id);
    m_changed.emit();
}

std::string TreeController::get_title(int id) const {
    return m_tree.contains(id) ? m_tree.get(id).title : "";
}

std::string TreeController::display_title(int id) const {
    const std::string title = get_title(id);
    return title.empty() ? "Untitled" : title;
}

std::string TreeController::get_seed(int id) const {
    auto it = m_seeds.find(id);
    return (it != m_seeds.end()) ? it->second : "";
}

bool TreeController::has_seed(int id) const {
    return m_seeds.find(id) != m_seeds.end();
}

void TreeController::set_seed(int id, std::string_view seed) {
    if (!m_tree.contains(id)) return;
    if (!m_db->set_seed(m_type, id, seed)) return;

    if (seed.empty()) {
        m_seeds.erase(id);
    } else {
        m_seeds[id] = std::string(seed);
    }
    m_changed.emit();
}

bool TreeController::contains(int id) const {
    return m_tree.contains(id);
}

std::vector<int> TreeController::children_of(int id) const {
    return m_tree.contains(id) ? m_tree.get(id).children : std::vector<int>{};
}

int TreeController::parent_of(int id) const {
    return m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
}

std::string TreeController::ancestor_path(int id) const {
    std::vector<std::string> parts;
    int current = m_tree.contains(id) ? m_tree.get(id).parent_id : -1;
    while (current > Tree::ROOT_ID && m_tree.contains(current)) {
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

std::vector<int> TreeController::leaves() const {
    std::vector<int> result;
    for (int id : m_tree.leaf_ids()) {
        if (id != Tree::ROOT_ID) result.push_back(id);
    }
    return result;
}

sigc::connection TreeController::connect_changed(const sigc::slot<void()>& slot) {
    return m_changed.connect(slot);
}
