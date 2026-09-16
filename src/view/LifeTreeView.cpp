#include "view/LifeTreeView.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include "core/Priority.hpp"
#include "core/Tree.hpp"
#include "core/TreeController.hpp"
#include "view/Style.hpp"

namespace {
// Clear space around the outermost discs, on top of a radius.
constexpr double EDGE_GAP = 14.0;

// Ceiling for a disc in structure mode; the spacing caps usually win.
constexpr double NODE_RADIUS = 58.0;

constexpr double EDGE_WIDTH = 2.0;

constexpr double SELECT_RING_GAP = 5.0;
constexpr double SELECT_RING_WIDTH = 2.0;

constexpr double EDGE_ALPHA = 0.35;

// Fraction each node is drawn toward its parent, 0 for a strict grid.
constexpr double PARENT_PULL = 0.24;

// Below 1: level gaps shrink with depth.
constexpr double DEPTH_EASING = 0.86;

constexpr double LABEL_ALPHA = 0.72;
constexpr double LABEL_FIT = 0.82;
constexpr double LABEL_MIN_SIZE = 8.0;
constexpr double LABEL_MAX_SIZE = 15.0;

// Per stepper press.
constexpr double WEIGHT_STEP = 5.0;

constexpr double STEPPER_RADIUS = 11.0;
constexpr double STEPPER_SPREAD = 36.0;
constexpr double STEPPER_GAP = 15.0;
constexpr double STEPPER_FONT = 12.0;

// Pixel floor for a weighted disc, so a tiny share stays clickable. The
// one place the area mapping is broken.
constexpr double WEIGHTED_FLOOR = 7.0;

// Of the gap between neighbours' centres, so discs are separated rather
// than merely not overlapping.
constexpr double NEIGHBOUR_CLEARANCE = 0.86;

// Ceiling for the root in weights mode.
constexpr double WEIGHTED_ROOT_RADIUS = 96.0;

// Cairo's toy text API has no ellipsizing; trims a character at a time.
std::string fit_label(const Cairo::RefPtr<Cairo::Context>& cr, const std::string& text,
                      double max_width) {
    Cairo::TextExtents extents;
    cr->get_text_extents(text, extents);
    if (extents.width <= max_width) return text;

    std::string trimmed = text;
    while (trimmed.size() > 1) {
        // Back off whole UTF-8 sequences: a cut mid-glyph is unrenderable.
        do {
            trimmed.pop_back();
        } while (!trimmed.empty() && (static_cast<unsigned char>(trimmed.back()) & 0xC0) == 0x80);

        const std::string candidate = trimmed + "…";
        cr->get_text_extents(candidate, extents);
        if (extents.width <= max_width) return candidate;
    }
    return "";
}

constexpr double NODE_ALPHA = 1.0;
}  // namespace

// Below the disc at a fixed size, on the selected node only.
LifeTreeView::Stepper LifeTreeView::stepper_for(const PlacedNode& node) const {
    Stepper stepper;
    stepper.visible = m_show_weights && node.steppable && node.id == m_selected;
    stepper.radius = STEPPER_RADIUS;
    stepper.y = node.y + node.radius + STEPPER_GAP + STEPPER_RADIUS;
    stepper.minus_x = node.x - STEPPER_SPREAD;
    stepper.plus_x = node.x + STEPPER_SPREAD;
    return stepper;
}

void LifeTreeView::select(int id) {
    if (id == m_selected) return;
    m_selected = id;
    queue_draw();
    m_selected_signal.emit(m_selected);
}

void LifeTreeView::set_show_weights(bool show) {
    if (m_show_weights == show) return;
    m_show_weights = show;
    queue_draw();
}

LifeTreeView::~LifeTreeView() {
    m_node_menu.unparent();
}

LifeTreeView::LifeTreeView(TreeController& life, Priority& priority)
    : m_life(life), m_priority(priority) {
    set_draw_func(sigc::mem_fun(*this, &LifeTreeView::on_draw));
    set_hexpand(true);
    set_vexpand(true);

    m_click = Gtk::GestureClick::create();
    m_click->signal_pressed().connect(sigc::mem_fun(*this, &LifeTreeView::on_pressed));
    add_controller(m_click);

    // Its own gesture: GestureClick delivers only the button it was told to.
    m_secondary_click = Gtk::GestureClick::create();
    m_secondary_click->set_button(GDK_BUTTON_SECONDARY);
    m_secondary_click->signal_pressed().connect(sigc::mem_fun(*this, &LifeTreeView::on_secondary));
    add_controller(m_secondary_click);

    m_node_menu.set_parent(*this);
    m_node_menu.set_has_arrow(false);
    m_node_menu.set_position(Gtk::PositionType::BOTTOM);

    m_life.connect_changed([this]() { queue_draw(); });

    m_priority.connect_changed([this]() { queue_draw(); });
}

// A tidy tree: leaves take consecutive slots in tree order and a parent
// centres over its first and last child. Deterministic, so the tree is in
// the same place every time.
std::vector<LifeTreeView::PlacedNode> LifeTreeView::place(int width, int height) const {
    std::vector<PlacedNode> placed;
    if (!m_life.contains(Tree::ROOT_ID)) return placed;

    std::unordered_map<int, double> slot;
    std::unordered_map<int, int> depth;
    double next_slot = 0.0;
    int max_depth = 0;

    // Explicit stack, two phases per node: descend, then centre on the way
    // back up once the children are placed.
    struct Frame {
        int id;
        int node_depth;
        bool descended;
    };
    std::vector<Frame> stack{{0, 0, false}};

    while (!stack.empty()) {
        Frame& frame = stack.back();
        const auto children = m_life.children_of(frame.id);

        if (!frame.descended) {
            frame.descended = true;
            depth[frame.id] = frame.node_depth;
            max_depth = std::max(max_depth, frame.node_depth);

            if (children.empty()) {
                slot[frame.id] = next_slot;
                next_slot += 1.0;
                stack.pop_back();
                continue;
            }
            const int child_depth = frame.node_depth + 1;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back({*it, child_depth, false});
            }
            continue;
        }

        // First and last child, not the mean: the mean drags a parent toward
        // whichever side has more children.
        slot[frame.id] = (slot[children.front()] + slot[children.back()]) / 2.0;
        stack.pop_back();
    }

    const double slots = std::max(1.0, next_slot - 1.0);

    // Two passes: the margin and the radius each need the other. The first
    // assumes the largest possible disc; the second uses the radius that
    // came out of it as the margin.
    const auto shares = m_priority.priorities();

    const double ceiling = m_show_weights ? WEIGHTED_ROOT_RADIUS : NODE_RADIUS;
    const double probe = ceiling + EDGE_GAP;
    const double probe_w = std::max(1.0, width - 2 * probe);
    const double probe_h = std::max(1.0, height - 2 * probe);

    const double slot_width = (probe_w / std::max(1.0, next_slot)) * (1.0 - PARENT_PULL);
    const double level_height = probe_h / std::max(1.0, static_cast<double>(max_depth));

    // Capped by both the slot width and the level gap; only the smaller is safe.
    const double radius = std::min({NODE_RADIUS, slot_width * 0.42, level_height * 0.38});

    // Weighted sizing conserves AREA down the tree: radius = R * sqrt(share),
    // so children's discs sum to their parent's. Anything else draws a tree
    // whose parts don't add up to their whole.
    double root_radius = radius;
    if (m_show_weights) {
        auto share_of = [&shares](int id) {
            if (id == Tree::ROOT_ID) return Priority::TOTAL;
            auto it = shares.find(id);
            return (it != shares.end()) ? it->second : 0.0;
        };

        std::vector<int> ids;
        ids.reserve(depth.size());
        for (const auto& [id, node_depth] : depth) ids.push_back(id);
        std::sort(ids.begin(), ids.end(),
                  [&depth](int a, int b) { return depth.at(a) < depth.at(b); });

        std::unordered_map<int, double> probe_x;
        for (int id : ids) {
            double x = (slots <= 0.0) ? probe_w / 2.0 : (slot.at(id) / slots) * probe_w;
            auto parent = probe_x.find(m_life.parent_of(id));
            if (parent != probe_x.end()) x += (parent->second - x) * PARENT_PULL;
            probe_x[id] = x;
        }

        // The largest R that keeps every adjacent pair on every row clear:
        // two neighbours need R*(sqrt(a) + sqrt(b)) between their centres.
        std::unordered_map<int, std::vector<int>> rows;
        for (int id : ids) rows[depth.at(id)].push_back(id);

        root_radius = std::min(WEIGHTED_ROOT_RADIUS, level_height * 0.42);
        for (auto& [row_depth, row] : rows) {
            std::sort(row.begin(), row.end(),
                      [&probe_x](int a, int b) { return probe_x[a] < probe_x[b]; });
            for (std::size_t i = 1; i < row.size(); ++i) {
                const double gap = probe_x[row[i]] - probe_x[row[i - 1]];
                const double units = std::sqrt(share_of(row[i - 1]) / Priority::TOTAL) +
                                     std::sqrt(share_of(row[i]) / Priority::TOTAL);
                if (units <= 0.0) continue;
                root_radius = std::min(root_radius, gap * NEIGHBOUR_CLEARANCE / units);
            }
        }
        root_radius = std::max(root_radius, WEIGHTED_FLOOR * 2.0);
    }

    // A bottom-row leaf carries its stepper below it; reserved rather than
    // clamped so the tree doesn't jump when a leaf is selected.
    const double stepper_reserve =
        m_show_weights ? STEPPER_GAP + 2 * STEPPER_RADIUS + EDGE_GAP : 0.0;

    const double margin = (m_show_weights ? root_radius : radius) + EDGE_GAP;
    const double usable_w = std::max(1.0, width - 2 * margin);
    const double usable_h = std::max(1.0, height - 2 * margin - stepper_reserve);

    for (const auto& [id, node_depth] : depth) {
        PlacedNode node;
        node.id = id;
        node.parent_id = m_life.parent_of(id);

        if (id == Tree::ROOT_ID) {
            node.share = Priority::TOTAL;
        } else {
            auto share = shares.find(id);
            node.share = (share != shares.end()) ? share->second : 0.0;

            node.steppable =
                m_life.children_of(node.parent_id).size() > 1;  // needs a sibling to trade with
        }

        node.radius = radius;
        if (m_show_weights) {
            const double area = std::sqrt(std::max(0.0, node.share) / Priority::TOTAL);
            node.radius = std::max(WEIGHTED_FLOOR, root_radius * area);
        }
        node.x = margin + (slots <= 0.0 ? usable_w / 2.0 : (slot.at(id) / slots) * usable_w);

        const double even = (max_depth == 0) ? 0.0 : static_cast<double>(node_depth) / max_depth;
        // Root at the top, depth growing downward, eased so the first split
        // gets the most room.
        node.y = margin + std::pow(even, DEPTH_EASING) * usable_h;

        placed.push_back(node);
    }

    // Shallowest first: edges draw under children, and the pull below
    // cascades from an already-pulled parent.
    std::sort(placed.begin(), placed.end(), [&depth](const PlacedNode& a, const PlacedNode& b) {
        return depth.at(a.id) < depth.at(b.id);
    });

    std::unordered_map<int, double> pulled;
    for (auto& node : placed) {
        auto parent = pulled.find(node.parent_id);
        if (parent != pulled.end()) {
            node.x += (parent->second - node.x) * PARENT_PULL;
        }
        pulled[node.id] = node.x;
    }
    return placed;
}

void LifeTreeView::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height) {
    const style::Palette palette = style::palette_for(*this);

    // Kept, so a click is tested against exactly what was drawn.
    m_placed = place(width, height);
    const auto& placed = m_placed;
    if (placed.empty()) return;

    std::unordered_map<int, const PlacedNode*> by_id;
    for (const auto& node : placed) by_id[node.id] = &node;

    // Edges first, all of them, so no disc is crossed by a later line.
    style::set_source(cr, palette.text, EDGE_ALPHA);
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    cr->set_line_width(EDGE_WIDTH);
    for (const auto& node : placed) {
        auto parent = by_id.find(node.parent_id);
        if (parent == by_id.end()) continue;

        cr->move_to(parent->second->x, parent->second->y);
        cr->line_to(node.x, node.y);
        cr->stroke();
    }

    for (const auto& node : placed) {
        style::set_source(cr, palette.text, NODE_ALPHA);
        cr->begin_new_path();
        cr->arc(node.x, node.y, node.radius, 0.0, 2 * M_PI);
        cr->fill();

        if (node.id == m_selected) {
            cr->set_line_width(SELECT_RING_WIDTH);
            cr->begin_new_path();
            cr->arc(node.x, node.y, node.radius + SELECT_RING_GAP, 0.0, 2 * M_PI);
            cr->stroke();
        }

        cr->select_font_face("Sans", Cairo::ToyFontFace::Slant::NORMAL,
                             Cairo::ToyFontFace::Weight::NORMAL);
        cr->set_font_size(std::clamp(node.radius * 0.34, LABEL_MIN_SIZE, LABEL_MAX_SIZE));

        const std::string title = m_life.get_title(node.id);
        if (!title.empty()) {
            const std::string label = fit_label(cr, title, node.radius * 2.0 * LABEL_FIT);
            if (!label.empty()) {
                Cairo::TextExtents extents;
                cr->get_text_extents(label, extents);
                style::set_source(cr, palette.background, LABEL_ALPHA);
                cr->move_to(node.x - extents.width / 2.0 - extents.x_bearing,
                            node.y - extents.height / 2.0 - extents.y_bearing);
                cr->show_text(label);
            }
        }

        const Stepper stepper = stepper_for(node);
        if (!stepper.visible) continue;

        // The figure is the node's share of its PARENT; the disc's size is
        // its share of the whole.
        const double own = m_priority.weight_of(node.id);
        char figure[8];
        std::snprintf(figure, sizeof(figure), "%d%%", static_cast<int>(std::lround(own)));

        cr->set_font_size(STEPPER_FONT);
        Cairo::TextExtents extents;
        cr->get_text_extents(figure, extents);
        style::set_source(cr, palette.text);
        cr->move_to(node.x - extents.width / 2.0 - extents.x_bearing,
                    stepper.y - extents.height / 2.0 - extents.y_bearing);
        cr->show_text(figure);

        // TRAP: begin_new_path before each arc. show_text leaves a current
        // point and arc() appends, so the circle would be joined to the text.
        cr->set_line_width(1.4);
        for (const double cx : {stepper.minus_x, stepper.plus_x}) {
            cr->begin_new_path();
            cr->arc(cx, stepper.y, stepper.radius, 0.0, 2 * M_PI);
            cr->stroke();
        }
        cr->begin_new_path();

        const double arm = stepper.radius * 0.5;
        cr->move_to(stepper.minus_x - arm, stepper.y);
        cr->line_to(stepper.minus_x + arm, stepper.y);
        cr->move_to(stepper.plus_x - arm, stepper.y);
        cr->line_to(stepper.plus_x + arm, stepper.y);
        cr->move_to(stepper.plus_x, stepper.y - arm);
        cr->line_to(stepper.plus_x, stepper.y + arm);
        cr->stroke();
    }
}

// Nearest centre wins where discs overlap.
int LifeTreeView::node_at(double x, double y) const {
    int hit = -1;
    double best = 0.0;
    for (const auto& node : m_placed) {
        const double dx = x - node.x;
        const double dy = y - node.y;
        const double distance = std::sqrt(dx * dx + dy * dy);
        if (distance > node.radius) continue;
        if (hit == -1 || distance < best) {
            hit = node.id;
            best = distance;
        }
    }
    return hit;
}

void LifeTreeView::on_pressed(int, double x, double y) {
    // The stepper is tested first and swallows the click.
    if (m_show_weights) {
        for (const auto& node : m_placed) {
            const Stepper stepper = stepper_for(node);
            if (!stepper.visible) continue;

            const double dy = y - stepper.y;
            const double from_minus = std::hypot(x - stepper.minus_x, dy);
            const double from_plus = std::hypot(x - stepper.plus_x, dy);
            if (from_minus > stepper.radius && from_plus > stepper.radius) continue;

            const double step = (from_minus <= from_plus) ? -WEIGHT_STEP : WEIGHT_STEP;

            const double target =
                std::clamp(m_priority.weight_of(node.id) + step, 0.0, Priority::TOTAL);

            // Deferred: set_weight emits, which redraws this widget while the
            // gesture is still running.
            const int id = node.id;
            Glib::signal_idle().connect_once(
                [this, id, target]() { m_priority.set_weight(id, target); });
            return;
        }
    }

    const int hit = node_at(x, y);

    // Clicking empty space clears the selection.
    if (hit == m_selected) return;
    m_selected = hit;
    queue_draw();
    m_selected_signal.emit(m_selected);
}

void LifeTreeView::on_secondary(int, double x, double y) {
    const int hit = node_at(x, y);
    if (hit == -1) return;

    // Selected as well as menued, so the detail panel shows the node the
    // menu is about.
    if (hit != m_selected) {
        m_selected = hit;
        queue_draw();
        m_selected_signal.emit(m_selected);
    }
    show_node_menu(hit, x, y);
}

// Every mutation from the menu is deferred to an idle: it emits the tree's
// signal, which redraws this widget under the button handling its click.
void LifeTreeView::show_node_menu(int id, double x, double y) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    box->set_margin(4);

    auto add_entry = [box](const std::string& label, std::function<void()> action) {
        auto* button = Gtk::make_managed<Gtk::Button>(label);
        button->set_has_frame(false);
        if (auto* child = dynamic_cast<Gtk::Label*>(button->get_child())) {
            child->set_xalign(0.0);
        }
        button->signal_clicked().connect(action);
        box->append(*button);
    };

    add_entry("Add child", [this, id]() {
        m_node_menu.popdown();
        Glib::signal_idle().connect_once([this, id]() {
            const int new_id = m_life.add(id, "");
            if (new_id == -1) return;

            m_selected = new_id;
            queue_draw();
            m_selected_signal.emit(new_id);

            m_added_signal.emit(new_id);
        });
    });

    if (id != Tree::ROOT_ID) {
        add_entry("Delete", [this, id]() {
            m_node_menu.popdown();
            Glib::signal_idle().connect_once([this, id]() {
                m_life.remove(id);
                if (m_selected == id) {
                    m_selected = -1;
                    m_selected_signal.emit(-1);
                }
                queue_draw();
            });
        });
    }

    m_node_menu.set_child(*box);
    m_node_menu.set_pointing_to(Gdk::Rectangle(static_cast<int>(x), static_cast<int>(y), 1, 1));
    m_node_menu.popup();
}
