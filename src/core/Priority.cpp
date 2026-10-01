#include "core/Priority.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/TreeController.hpp"

namespace {

constexpr double EPSILON = 1e-6;

// Whole numbers summing to exactly `target`, by largest remainder.
void quantize(std::vector<double>& values, double target) {
    if (values.empty()) return;

    double assigned = 0.0;
    std::vector<std::pair<double, size_t>> remainders;
    for (size_t i = 0; i < values.size(); ++i) {
        const double floored = std::floor(values[i]);
        remainders.push_back({values[i] - floored, i});
        values[i] = floored;
        assigned += floored;
    }
    std::sort(remainders.begin(), remainders.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    const int leftover = static_cast<int>(std::llround(target - assigned));
    for (int i = 0; i < leftover && i < static_cast<int>(remainders.size()); ++i) {
        values[remainders[i].second] += 1.0;
    }
}

// Scales `values` to sum to `target`, evenly if they sum to zero, then
// quantizes. The one rule every share set follows.
std::vector<double> rebalance(std::vector<double> values, double target) {
    if (values.empty()) return values;

    double total = 0.0;
    for (double v : values) total += v;

    for (double& v : values) {
        v = total > 0.0 ? v * (target / total) : target / static_cast<double>(values.size());
    }
    quantize(values, target);
    return values;
}

// Holds values[index] at `fixed` and rebalances the others into the rest.
// A lone value is always TOTAL.
std::vector<double> rebalance_around(std::vector<double> values, size_t index, double fixed) {
    if (values.size() == 1) return {Priority::TOTAL};

    std::vector<double> others;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != index) others.push_back(values[i]);
    }
    others = rebalance(others, Priority::TOTAL - fixed);

    size_t j = 0;
    for (size_t i = 0; i < values.size(); ++i) values[i] = (i == index) ? fixed : others[j++];
    return values;
}

// Whole numbers that are an even split with its rounding: they differ by
// at most one.
bool is_even_split(const std::vector<double>& values) {
    if (values.empty()) return true;
    const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    return *hi - *lo <= 1.0 + EPSILON;
}

double clamp_share(double share) {
    return std::round(std::clamp(share, 0.0, Priority::TOTAL));
}

}  // namespace

Priority::Priority(std::shared_ptr<Database> db, TreeController& life)
    : m_db(std::move(db)), m_life(life) {}

void Priority::load() {
    m_weights.clear();
    for (const auto& row : m_db->load_life_weights()) m_weights[row.node_id] = row.weight;
}

// ---------------------------------------------------------------------
// Weights
// ---------------------------------------------------------------------

double Priority::weight_of(int node_id) const {
    if (node_id == Tree::ROOT_ID) return TOTAL;
    auto it = m_weights.find(node_id);
    return (it != m_weights.end()) ? it->second : 0.0;
}

void Priority::write_weight(int node_id, double weight) {
    if (!m_db->set_life_weight(node_id, weight)) return;
    m_weights[node_id] = weight;
}

void Priority::set_weight(int node_id, double weight) {
    const int parent = m_life.parent_of(node_id);
    if (parent < 0) return;

    const std::vector<int> siblings = m_life.children_of(parent);
    std::vector<double> current;
    size_t index = 0;
    for (size_t i = 0; i < siblings.size(); ++i) {
        current.push_back(weight_of(siblings[i]));
        if (siblings[i] == node_id) index = i;
    }

    const auto updated = rebalance_around(current, index, clamp_share(weight));
    for (size_t i = 0; i < siblings.size(); ++i) write_weight(siblings[i], updated[i]);
    m_changed.emit();
}

void Priority::normalize() {
    bool changed = false;

    for (auto it = m_weights.begin(); it != m_weights.end();) {
        it = m_life.contains(it->first) ? std::next(it) : m_weights.erase(it);
    }

    std::vector<int> pending{Tree::ROOT_ID};
    while (!pending.empty()) {
        const int node = pending.back();
        pending.pop_back();

        const std::vector<int> children = m_life.children_of(node);
        for (int child : children) pending.push_back(child);
        if (children.empty()) continue;

        std::vector<double> current;
        std::vector<double> stored;
        bool any_missing = false;
        for (int child : children) {
            auto it = m_weights.find(child);
            if (it == m_weights.end()) {
                any_missing = true;
                current.push_back(0.0);
            } else {
                current.push_back(it->second);
                stored.push_back(it->second);
            }
        }

        // A newcomer joins an untouched set evenly. Where the siblings have
        // been weighted by hand it arrives at zero, so nothing you chose
        // moves without you.
        if (any_missing && is_even_split(stored)) {
            std::fill(current.begin(), current.end(), 1.0);
        }

        const auto updated = rebalance(current, TOTAL);
        for (size_t i = 0; i < children.size(); ++i) {
            if (std::abs(weight_of(children[i]) - updated[i]) > EPSILON ||
                m_weights.find(children[i]) == m_weights.end()) {
                write_weight(children[i], updated[i]);
                changed = true;
            }
        }
    }

    if (changed) m_changed.emit();
}

std::unordered_map<int, double> Priority::priorities() const {
    std::unordered_map<int, double> out;
    if (!m_life.contains(Tree::ROOT_ID)) return out;

    out[Tree::ROOT_ID] = TOTAL;
    std::vector<int> queue{Tree::ROOT_ID};
    for (size_t i = 0; i < queue.size(); ++i) {
        const int node = queue[i];
        for (int child : m_life.children_of(node)) {
            out[child] = out[node] * (weight_of(child) / TOTAL);
            queue.push_back(child);
        }
    }
    return out;
}
