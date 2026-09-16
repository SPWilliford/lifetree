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
#include "core/TreeController.hpp"
#include "view/Style.hpp"

namespace {
// Clear space around the outermost discs. Not the margin itself — the
// margin has to be at least a radius or the top and bottom rows are cut
// in half by the panel edge, and the radius isn't known until the
// usable area is. See place().
constexpr double EDGE_GAP = 14.0;

// One size for every node, weight included. The drawing has one job for
// now — showing the tree's shape — and a second channel arguing with it
// makes both harder to read while the layout is still being settled.
//
// Big enough to have mass and to be an easy click target. A dot reads as
// a joint between lines; a disc reads as a thing the lines connect.
//
// A ceiling rather than the size actually drawn — the caps below almost
// always win, and what they leave is the largest disc the tree's own
// spacing allows.
constexpr double NODE_RADIUS = 58.0;

constexpr double EDGE_WIDTH = 2.0;

// The selected node keeps its fill and gains a ring outside it, rather
// than changing colour or size. Size would misread as weight once weight
// is a size, and a second colour would be the first thing in this
// drawing that means something other than "a node".
constexpr double SELECT_RING_GAP = 5.0;
constexpr double SELECT_RING_WIDTH = 2.0;

// Fainter than the nodes. An edge says two nodes are related, which is
// structure rather than content — at full strength a wide tree reads as
// a web of lines with discs caught in it.
constexpr double EDGE_ALPHA = 0.35;

// How far each node is drawn toward its parent, 0 for the strict grid a
// tidy layout produces and 1 for every child sitting on top of its
// parent. Children hugging their parent is what makes a branch read as a
// branch rather than as a row of equally spaced dots.
constexpr double PARENT_PULL = 0.24;

// Below 1, so the gaps between levels shrink as they descend and the
// whole tree settles downward instead of dividing the height evenly.
constexpr double DEPTH_EASING = 0.86;

// The title inside the disc. Faint on purpose: the point of a title
// being separate from the seed was that the short form could be shown
// without a sentence cluttering the tree, and the same reasoning says
// it shouldn't shout here either.
constexpr double LABEL_ALPHA = 0.72;
constexpr double LABEL_FIT = 0.82;  // of the disc's diameter
constexpr double LABEL_MIN_SIZE = 8.0;
constexpr double LABEL_MAX_SIZE = 15.0;

// How much one press moves a weight. Moving by 1 meant twenty presses to
// shift anything, and the scale was never the point — these are shares
// of a hundred, entered and read as whole percents.
constexpr double WEIGHT_STEP = 5.0;

// Fixed, not proportional: a control has to be the same target whatever
// the node it belongs to is worth. See the header for why it sits below
// the disc rather than in it.
constexpr double STEPPER_RADIUS = 11.0;
constexpr double STEPPER_SPREAD = 36.0;  // from the node's centre, each way
constexpr double STEPPER_GAP = 15.0;     // below the disc's edge
constexpr double STEPPER_FONT = 12.0;

// Sized by area, not radius: a radius-proportional circle exaggerates by
// the square, so a quarter of the tree would look four times a
// twelfth rather than twice it.
//
// A floor, because a deeply divided branch puts leaves at fractions of a
// percent and a disc has to stay clickable however small its share.
// Pixels, not a ratio: below this a disc is neither visible nor
// clickable, whatever the tree around it is doing. The only place the
// area mapping is broken, and deliberately.
constexpr double WEIGHTED_FLOOR = 7.0;

// Of the gap between two neighbours' centres, so touching discs are
// separated rather than merely not overlapping.
constexpr double NEIGHBOUR_CLEARANCE = 0.86;

// What a node worth the whole tree is allowed to be. Higher than the
// uniform ceiling on purpose: the root has no siblings to crowd, so in
// weights mode the only thing limiting it is the level below, and
// holding it to the size a LEAF may be makes the whole tree small.
constexpr double WEIGHTED_ROOT_RADIUS = 96.0;

// Cairo's toy text API has no ellipsizing, so a label too wide for its
// disc is trimmed a character at a time until it fits. Short titles are
// the norm here — the seed carries the long form — so this rarely runs
// more than a few times.
std::string fit_label(const Cairo::RefPtr<Cairo::Context>& cr, const std::string& text,
                      double max_width) {
    Cairo::TextExtents extents;
    cr->get_text_extents(text, extents);
    if (extents.width <= max_width) return text;

    std::string trimmed = text;
    while (trimmed.size() > 1) {
        // Back off whole UTF-8 sequences, not bytes: cutting mid-glyph
        // produces a byte Cairo can't render.
        do {
            trimmed.pop_back();
        } while (!trimmed.empty() && (static_cast<unsigned char>(trimmed.back()) & 0xC0) == 0x80);

        const std::string candidate = trimmed + "…";
        cr->get_text_extents(candidate, extents);
        if (extents.width <= max_width) return candidate;
    }
    return "";
}

// No labels. A name on every node turns the drawing into a diagram of
// text, and the list beside it already answers "which one is that"
// better than a truncated caption could. Hover can say it later.
constexpr double NODE_ALPHA = 1.0;
}  // namespace

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

LifeTreeView::LifeTreeView(TreeController& life, Priority& priority)
    : m_life(life), m_priority(priority) {
    set_draw_func(sigc::mem_fun(*this, &LifeTreeView::on_draw));
    set_hexpand(true);
    set_vexpand(true);

    m_click = Gtk::GestureClick::create();
    m_click->signal_pressed().connect(sigc::mem_fun(*this, &LifeTreeView::on_pressed));
    add_controller(m_click);

    // Button 3 on its own gesture rather than a branch inside the first:
    // GestureClick delivers only the button it was told to watch, so the
    // two never have to agree about which one is handling an event.
    m_secondary_click = Gtk::GestureClick::create();
    m_secondary_click->set_button(GDK_BUTTON_SECONDARY);
    m_secondary_click->signal_pressed().connect(sigc::mem_fun(*this, &LifeTreeView::on_secondary));
    add_controller(m_secondary_click);

    m_node_menu.set_parent(*this);
    m_node_menu.set_has_arrow(false);
    m_node_menu.set_position(Gtk::PositionType::BOTTOM);

    m_life.connect_changed([this]() { queue_draw(); });

    // A weight moving resizes every disc under the same parent, so this has
    // to redraw as much as a structural change does.
    m_priority.connect_changed([this]() { queue_draw(); });
}

std::vector<LifeTreeView::PlacedNode> LifeTreeView::place(int width, int height) const {
    std::vector<PlacedNode> placed;
    if (!m_life.contains(0)) return placed;

    // A tidy tree, laid out in two passes over one depth-first walk: leaves
    // take consecutive slots in the order they appear, and a parent centres
    // over its first and last child. Deterministic, so the tree is in the
    // same place every time the page opens — which a force layout would not
    // be without saved positions.
    std::unordered_map<int, double> slot;
    std::unordered_map<int, int> depth;
    double next_slot = 0.0;
    int max_depth = 0;

    // Explicit stack rather than recursion, in two phases per node: descend
    // first, then centre on the way back up, which needs the children placed.
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
            // Reversed, so the stack pops them in tree order and the slots
            // run left to right the way the list reads top to bottom.
            const int child_depth = frame.node_depth + 1;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back({*it, child_depth, false});
            }
            continue;
        }

        // First and last child, not the mean of all: centring on the mean
        // drags a parent toward whichever side has more children, and the
        // trunk visibly leans away from the bigger branch.
        slot[frame.id] = (slot[children.front()] + slot[children.back()]) / 2.0;
        stack.pop_back();
    }

    const double slots = std::max(1.0, next_slot - 1.0);

    // Two passes, because the margin and the radius each need the other.
    //
    // The first assumes the largest disc that could be drawn and measures
    // what's left; the second uses the radius that came out of it as the
    // margin, so the top and bottom rows sit a clear gap inside the panel
    // instead of being sliced by it. Conservative by a few pixels when the
    // radius ends up small, which is invisible and cheaper than iterating.
    const auto shares = m_priority.priorities();

    const double ceiling = m_show_weights ? WEIGHTED_ROOT_RADIUS : NODE_RADIUS;
    const double probe = ceiling + EDGE_GAP;
    const double probe_w = std::max(1.0, width - 2 * probe);
    const double probe_h = std::max(1.0, height - 2 * probe);

    // Discs must not touch, in either direction. Capped at a node's own
    // horizontal slot and at the gap between depths, because a tree can
    // crowd either way: many siblings squeeze it sideways, many levels
    // squeeze it vertically, and only the smaller of the two is safe.
    // Reduced by the pull, because pulling children toward their parent
    // brings siblings closer together — a radius sized to the unpulled slot
    // would then overlap.
    const double slot_width = (probe_w / std::max(1.0, next_slot)) * (1.0 - PARENT_PULL);
    const double level_height = probe_h / std::max(1.0, static_cast<double>(max_depth));

    const double radius = std::min({NODE_RADIUS, slot_width * 0.42, level_height * 0.38});

    // Weighted sizing conserves AREA down the tree: a node's children's
    // discs sum to exactly their parent's disc. radius = R * sqrt(share/100)
    // is the whole of it, and it falls out of the cascade — siblings sum to
    // their parent, so their areas do too.
    //
    // That makes the picture the same claim the numbers make. Anything else
    // — normalising per level, giving the root its own size — draws a tree
    // whose parts don't add up to their whole, which is exactly what the
    // weight model promises they do.
    //
    // The floor is the one concession: a leaf worth a fraction of a percent
    // would be sub-pixel, so it's held at something clickable and stops
    // being proportional down there.
    double root_radius = radius;
    if (m_show_weights) {
        auto share_of = [&shares](int id) {
            if (id == 0) return Priority::TOTAL;
            auto it = shares.find(id);
            return (it != shares.end()) ? it->second : 0.0;
        };

        // Provisional positions, at the probe margin and without offsetting
        // by it — only the GAPS between neighbours matter here, and those
        // are the same wherever the row starts. Conservative, because the
        // probe margin is the largest one possible and so the gaps it gives
        // are the tightest.
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

        // The largest R that leaves every adjacent pair on every row clear
        // of each other. Two neighbours need R*(sqrt(a) + sqrt(b)) of room
        // between their centres, so each pair puts a ceiling on R and the
        // tightest one wins. Solved rather than guessed at, because a
        // per-node cap would break the area sum this exists to preserve.
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

    // A leaf on the bottom row carries its stepper BELOW it, which the
    // margin would otherwise know nothing about — so the buttons hang off
    // the page into the arrow bar beneath. Reserved rather than clamped,
    // because clamping would shift the row up only when a leaf happened to
    // be selected and the tree would jump.
    const double stepper_reserve =
        m_show_weights ? STEPPER_GAP + 2 * STEPPER_RADIUS + EDGE_GAP : 0.0;

    const double margin = (m_show_weights ? root_radius : radius) + EDGE_GAP;
    const double usable_w = std::max(1.0, width - 2 * margin);
    const double usable_h = std::max(1.0, height - 2 * margin - stepper_reserve);

    for (const auto& [id, node_depth] : depth) {
        PlacedNode node;
        node.id = id;
        node.parent_id = m_life.parent_of(id);

        if (id == 0) {
            node.share = Priority::TOTAL;
        } else {
            auto share = shares.find(id);
            node.share = (share != shares.end()) ? share->second : 0.0;

            // Its weight can only move if there is a sibling to take it
            // from. The pool is the parent's share and it is always full.
            node.steppable = m_life.children_of(node.parent_id).size() > 1;
        }

        // Uniform unless weights are showing, in which case AREA carries the
        // share — sqrt, because a radius-proportional circle exaggerates by
        // the square. The floor keeps a leaf worth a fraction of a percent
        // big enough to see and to click.
        node.radius = radius;
        if (m_show_weights) {
            const double area = std::sqrt(std::max(0.0, node.share) / Priority::TOTAL);
            node.radius = std::max(WEIGHTED_FLOOR, root_radius * area);
        }
        node.x = margin + (slots <= 0.0 ? usable_w / 2.0 : (slot.at(id) / slots) * usable_w);

        // Root at the TOP, depth growing downward. The tree hangs rather
        // than stands, which reads the way the indented list beside it does
        // — first row at the top, children below — so moving between the
        // two views doesn't mean re-reading the same structure upside down.
        //
        // Eased rather than even: the first split gets the most room and
        // each one after it less, so the tree opens out at the top and
        // settles toward the bottom the way weight on a branch would.
        const double even = (max_depth == 0) ? 0.0 : static_cast<double>(node_depth) / max_depth;
        node.y = margin + std::pow(even, DEPTH_EASING) * usable_h;

        placed.push_back(node);
    }

    // Shallowest first, so a parent's edges are drawn before the children
    // that sit on top of them — and so the pull below can cascade, each
    // node moving toward a parent that has already moved.
    std::sort(placed.begin(), placed.end(), [&depth](const PlacedNode& a, const PlacedNode& b) {
        return depth.at(a.id) < depth.at(b.id);
    });

    // Gravity, after placement rather than during it. A tidy tree spreads
    // its leaves evenly across the whole width, which is legible and looks
    // like a chart; drawing each node partway toward its parent gathers a
    // branch into a shape instead. Cascading, so a deep node is pulled by
    // its parent's already-pulled position and the whole limb leans.
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

    // Kept, so a click is tested against what was actually drawn. Recomputing
    // in the handler would be the same numbers today and a bug the moment
    // placement depends on anything the draw pass knows.
    m_placed = place(width, height);
    const auto& placed = m_placed;
    if (placed.empty()) return;

    std::unordered_map<int, const PlacedNode*> by_id;
    for (const auto& node : placed) by_id[node.id] = &node;

    // One colour for the whole drawing: the theme's text colour, which is
    // near-white on dark and near-black on light, so it tracks the theme
    // without meaning anything. Nothing here is trying to say which node is
    // which yet — only what the tree is shaped like.

    // Edges first, all of them, so no circle is crossed by a line drawn
    // afterwards.
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

        // Everything inside a disc is drawn in the background colour — the
        // disc is filled with the theme's text colour, so a marking on it
        // has to be what the page behind it is.
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

        // The figure between the two halves is the node's share of its
        // PARENT, not of the tree. That's the number the presses move and
        // the one that sums to a hundred across the siblings — while the
        // disc's size shows what the cascade makes of it further down. Local
        // number, global size, and both true.
        const double own = m_priority.weight_of(node.id);
        char figure[8];
        std::snprintf(figure, sizeof(figure), "%d%%", static_cast<int>(std::lround(own)));

        // Out on the page now, not on the disc, so these take the page's own
        // colour rather than the background the disc is drawn against.
        cr->set_font_size(STEPPER_FONT);
        Cairo::TextExtents extents;
        cr->get_text_extents(figure, extents);
        style::set_source(cr, palette.text);
        cr->move_to(node.x - extents.width / 2.0 - extents.x_bearing,
                    stepper.y - extents.height / 2.0 - extents.y_bearing);
        cr->show_text(figure);

        // Outlined rather than filled: a solid blob either side of the
        // figure would read as two more nodes.
        //
        // TRAP: begin_new_path before each arc. show_text leaves a current
        // point where the text ended, and arc() APPENDS — so without this
        // the first circle is joined to the figure by a straight line drawn
        // from wherever the last glyph finished.
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

int LifeTreeView::node_at(double x, double y) const {
    // Nearest hit rather than first, so overlapping discs resolve to the one
    // whose centre the click is closest to instead of whichever the layout
    // happened to place first.
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
    // The stepper is tested first and swallows the click. Its halves sit
    // inside the disc, so without this every press on one would also count
    // as a press on the node underneath.
    if (m_show_weights) {
        for (const auto& node : m_placed) {
            const Stepper stepper = stepper_for(node);
            if (!stepper.visible) continue;

            const double dy = y - stepper.y;
            const double from_minus = std::hypot(x - stepper.minus_x, dy);
            const double from_plus = std::hypot(x - stepper.plus_x, dy);
            if (from_minus > stepper.radius && from_plus > stepper.radius) continue;

            const double step = (from_minus <= from_plus) ? -WEIGHT_STEP : WEIGHT_STEP;

            // Clamped here rather than trusted to the engine: a weight below
            // zero or above the whole pool isn't a state the invariant can
            // repair into anything meaningful.
            const double target =
                std::clamp(m_priority.weight_of(node.id) + step, 0.0, Priority::TOTAL);

            // Deferred, as everywhere else in this widget: set_weight moves
            // the siblings and emits, which redraws this widget while the
            // gesture that called it is still running.
            const int id = node.id;
            Glib::signal_idle().connect_once(
                [this, id, target]() { m_priority.set_weight(id, target); });
            return;
        }
    }

    const int hit = node_at(x, y);

    // Clicking empty space clears the selection. Deliberate: it's the way
    // back to "nothing chosen" without a control for it, and it matches
    // what clicking away does everywhere else.
    if (hit == m_selected) return;
    m_selected = hit;
    queue_draw();
    m_selected_signal.emit(m_selected);
}

void LifeTreeView::on_secondary(int, double x, double y) {
    const int hit = node_at(x, y);
    if (hit == -1) return;  // nothing here to act on

    // Selected as well as menued, so the panel is showing the node the menu
    // is about. A menu acting on something other than what's on screen is
    // how the wrong node gets deleted.
    if (hit != m_selected) {
        m_selected = hit;
        queue_draw();
        m_selected_signal.emit(m_selected);
    }
    show_node_menu(hit, x, y);
}

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
        // Deferred: adding emits the tree's changed signal, which redraws
        // this widget out from under the button that is still handling its
        // own click.
        Glib::signal_idle().connect_once([this, id]() {
            const int new_id = m_life.add(id, "");
            if (new_id == -1) return;

            m_selected = new_id;
            queue_draw();
            m_selected_signal.emit(new_id);

            // An untitled node with the cursor already in its title, which
            // is what the list does on Add child — the node exists, and
            // naming it is the next thing you were going to do anyway.
            m_added_signal.emit(new_id);
        });
    });

    // The root is the tree. Removing it would take everything with it and
    // leave nothing to hang the next node from, so it isn't offered.
    if (id != 0) {
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
