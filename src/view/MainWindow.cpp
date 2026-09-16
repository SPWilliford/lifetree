#include "view/MainWindow.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <glibmm/main.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <pangomm/layout.h>

#include "core/App.hpp"
#include "view/Style.hpp"

MainWindow::MainWindow(App& app)
    // Order matches the declaration order in the header, which is the order
    // members are actually constructed in — the list is only a request.
    : m_life(app.life()),
      m_priority(app.priority()),
      m_projects_panel(app.projects(), app.task_attributes()),
      m_life_view(app.life(), app.priority()),
      m_links_panel(app.life(), app.projects(), app.priority()),
      m_review_panel(app.life(), app.priority(), app.work()),
      m_schedule_panel(app.projects(), app.work(), app.task_attributes(), app.day()),
      m_task_panel(app.projects(), app.task_attributes(), app.priority()) {
    // The window's name, which is what the desktop's window list, alt-tab
    // and the taskbar read. It was "" so the header would show nothing, but
    // the two are separate things and blanking this made the app show up
    // outside itself as "untitled window".
    //
    // Where you are inside the app is m_header_title, below.
    set_title("LifeTree");
    set_default_size(1400, 1000);

    style::install();

    // Replaces what the header would otherwise draw — the window title —
    // with a label the app controls. The css class is what Adwaita styles a
    // header's title with; without it this reads as ordinary body text.
    m_header_title.add_css_class("title");
    m_header_bar.set_title_widget(m_header_title);

    m_header_bar.set_show_title_buttons(true);  // standard minimize/maximize/close
    set_titlebar(m_header_bar);

    m_projects_panel.add_css_class("panel-left");
    m_task_panel.add_css_class("panel-right");

    m_projects_panel.set_hexpand(true);
    m_schedule_panel.set_hexpand(true);
    m_schedule_panel.set_vexpand(true);
    m_task_panel.set_hexpand(true);

    // Otherwise the panels sit flush against the Paned's thin drag handle.
    constexpr int GAP = 8;
    m_projects_panel.set_margin_end(GAP);
    m_schedule_panel.set_margin_start(GAP);
    m_schedule_panel.set_margin_end(GAP);
    m_task_panel.set_margin_start(GAP);

    m_inner_paned.set_start_child(m_schedule_panel);
    m_inner_paned.set_end_child(m_task_panel);
    m_inner_paned.set_resize_start_child(true);  // schedule absorbs space as the divider moves,
    m_inner_paned.set_resize_end_child(false);   // task keeps whatever width you last dragged it to

    m_outer_paned.set_start_child(m_projects_panel);
    m_outer_paned.set_end_child(m_inner_paned);
    m_outer_paned.set_resize_start_child(
        false);                                // tree keeps whatever width you last dragged it to
    m_outer_paned.set_resize_end_child(true);  // the schedule+task pair absorbs the rest

    // Starting positions, not floors — every divider stays draggable.
    constexpr int TREE_START = 440;
    constexpr int TASK_START = 420;
    constexpr int OUTER_MARGIN = 32;  // set_margin(16), both sides

    m_outer_paned.set_position(TREE_START);
    m_inner_paned.set_position(1400 - OUTER_MARGIN - TREE_START - TASK_START);

    m_outer_paned.set_margin(16);
    m_outer_paned.set_vexpand(true);

    m_footer.add_css_class("app-footer");
    m_footer.set_margin_start(16);
    m_footer.set_margin_end(16);
    m_footer.set_margin_bottom(8);

    m_daily_page.append(m_outer_paned);

    // --- Priority's sub-pages ---
    //
    // Each is held to a measure and centered rather than filling. Planning
    // content is rows of a name against figures; edge to edge on a
    // maximized window the two ends stop reading as the same row.
    // Narrower than the list it replaced: a title and a seed need reading
    // width, not the room a tree of indented rows and weight spins did.
    constexpr int DETAIL_MEASURE = 340;

    // The drawing IS the page now, and takes the whole of it minus the
    // column beside it. The indented list used to hold this space and be the
    // way the tree was edited; the structure is read from the drawing
    // instead, and editing follows it there.
    m_life_view.set_hexpand(true);
    m_life_tree_page.set_hexpand(true);
    m_life_tree_page.append(m_life_view);

    // Top right: what the selected node IS. Empty until clicking a node
    // means something — the frame is here so the page's proportions can be
    // judged before there's anything to put in it.
    m_life_detail_hint.set_text("Select a node");
    m_life_detail_hint.add_css_class("dim-label");
    m_life_detail_hint.set_valign(Gtk::Align::CENTER);
    m_life_detail_hint.set_vexpand(true);

    // Centred. A node's detail is a statement about one thing rather than a
    // row in a list, so there is no left edge for the eye to run down.
    m_life_detail_title.add_css_class("node-title");
    m_life_detail_title.set_alignment(0.5f);
    m_life_detail_title.set_margin_top(6);

    // Once, for the widget's lifetime. The property fires on both entering
    // and leaving edit mode, so the write happens only on the way out.
    m_life_detail_title.property_editing().signal_changed().connect([this]() {
        if (m_life_detail_title.get_editing()) return;
        if (m_life_detail_populating || m_life_detail_id < 0) return;
        m_life.edit(m_life_detail_id, m_life_detail_title.get_text().raw());
    });

    // The seed is the long form and runs to a paragraph, so it's a TextView
    // rather than a field. Centred to match the title above it.
    m_life_detail_seed.set_wrap_mode(Gtk::WrapMode::WORD);
    m_life_detail_seed.set_justification(Gtk::Justification::CENTER);
    m_life_detail_seed.add_css_class("seed-view");

    // Room around the text, and room between its lines. A statement of
    // intent set tight against its own edges reads as a form field; given
    // air it reads as something written on purpose.
    m_life_detail_seed.set_left_margin(14);
    m_life_detail_seed.set_right_margin(14);
    m_life_detail_seed.set_top_margin(10);
    m_life_detail_seed.set_bottom_margin(10);
    m_life_detail_seed.set_pixels_above_lines(2);
    m_life_detail_seed.set_pixels_below_lines(2);

    // No Save button: the text is committed when focus leaves, which is the
    // moment you have finished with it. A button would be one more thing to
    // remember for a field whose whole point is being quick to write in.
    auto seed_focus = Gtk::EventControllerFocus::create();
    seed_focus->signal_leave().connect(sigc::mem_fun(*this, &MainWindow::commit_life_seed));
    m_life_detail_seed.add_controller(seed_focus);

    m_life_detail_seed_scroll.set_child(m_life_detail_seed);
    m_life_detail_seed_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_life_detail_seed_scroll.set_vexpand(true);

    // The gap that makes the title read as a caption ABOVE the seed rather
    // than the first line of it.
    m_life_detail_seed_scroll.set_margin_top(10);

    m_life_detail.add_css_class("panel-right");
    m_life_detail.set_vexpand(true);
    m_life_detail.append(m_life_detail_hint);
    m_life_detail.append(m_life_detail_title);
    m_life_detail.append(m_life_detail_seed_scroll);

    m_life_view.signal_selected().connect(sigc::mem_fun(*this, &MainWindow::show_life_node));

    // A new node arrives untitled, so the cursor goes straight into the
    // title rather than leaving a blank disc for you to work out how to
    // name. Deferred by one turn: the panel is being repointed at it in the
    // same breath, and start_editing on a field that is still being filled
    // gets undone by the fill.
    m_life_view.signal_node_added().connect([this](int id) {
        Glib::signal_idle().connect_once([this, id]() {
            if (m_life_detail_id != id) return;
            m_life_detail_title.start_editing();
        });
    });

    // Renaming or reseeding from the list beside it has to reach here too,
    // or the two views disagree about the node they are both showing.
    m_life.connect_changed([this]() {
        if (!m_life_detail_seed.has_focus()) show_life_node(m_life_detail_id);
    });

    show_life_node(-1);

    // The indented list is gone. It was the way the life tree was edited and
    // the way its weights were set; the drawing does both now, and a list of
    // the same nodes beside it was a second answer to a question already
    // answered better.
    m_weights_toggle.set_label("Weights");
    m_weights_toggle.set_has_frame(false);
    m_weights_toggle.set_tooltip_text(
        "Size the tree by weight, and show a stepper on the selected node");
    m_weights_toggle.signal_toggled().connect(
        [this]() { m_life_view.set_show_weights(m_weights_toggle.get_active()); });

    m_life_ranked_scroll.set_child(m_life_ranked);
    m_life_ranked_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_life_ranked_scroll.set_vexpand(true);

    m_life_ranked_panel.add_css_class("panel-right");
    m_life_ranked_panel.set_vexpand(true);
    m_life_ranked_panel.append(m_weights_toggle);
    m_life_ranked_panel.append(m_life_ranked_scroll);

    // Rebuilt on both signals: the structure decides which nodes are leaves,
    // and the weights decide what each one came to.
    m_life.connect_changed([this]() { rebuild_life_ranking(); });
    m_priority.connect_changed([this]() { rebuild_life_ranking(); });
    rebuild_life_ranking();

    m_life_side.set_hexpand(false);
    m_life_side.set_size_request(DETAIL_MEASURE, -1);
    m_life_side.append(m_life_detail);
    m_life_side.append(m_life_ranked_panel);
    m_life_tree_page.append(m_life_side);

    // Unbounded now that the two directions sit abreast rather than stacked.
    // The measure was there because a full-window row of pick-here-edit-there
    // drifted to opposite edges and stopped reading as a pair; four columns
    // across the page have no slack to drift into.
    m_links_panel.set_hexpand(true);
    m_links_page.set_hexpand(true);
    m_links_page.append(m_links_panel);

    // The way onward, on the edge it leads across: down at the foot of the
    // life tree, up at the head of projects. Flat and centred so it reads as
    // an edge of the page rather than a control belonging to the content.
    m_to_projects_button.set_icon_name("go-down-symbolic");
    m_to_projects_button.set_has_frame(false);
    m_to_projects_button.set_halign(Gtk::Align::CENTER);
    m_to_projects_button.set_tooltip_text("Projects");
    m_to_projects_button.signal_clicked().connect(
        [this]() { show_mode("priority", "Projects", "links"); });

    m_to_life_tree_button.set_icon_name("go-up-symbolic");
    m_to_life_tree_button.set_has_frame(false);
    m_to_life_tree_button.set_halign(Gtk::Align::CENTER);
    m_to_life_tree_button.set_tooltip_text("Life Tree");
    m_to_life_tree_button.signal_clicked().connect(
        [this]() { show_mode("priority", "Life Tree", "life"); });

    m_life_tree_page.set_vexpand(true);
    m_life_tree_column.append(m_life_tree_page);
    m_life_tree_column.append(m_to_projects_button);

    m_links_page.set_vexpand(true);
    m_projects_column.append(m_to_life_tree_button);
    m_projects_column.append(m_links_page);

    // Order is the direction: "life" is added first, so moving to "links"
    // slides the content up and the next page in from below — the view
    // panning DOWN the structure, which is the gesture the arrows describe.
    // SLIDE_UP_DOWN reads that order itself, so neither button has to say
    // which way it is going.
    m_priority_stack.add(m_life_tree_column, "life");
    m_priority_stack.add(m_projects_column, "links");
    m_priority_stack.set_transition_type(Gtk::StackTransitionType::SLIDE_UP_DOWN);
    m_priority_stack.set_transition_duration(220);
    m_priority_stack.set_vexpand(true);

    m_priority_page.set_margin(16);
    m_priority_page.append(m_priority_stack);

    m_mode_stack.add(m_daily_page, "daily");
    m_mode_stack.add(m_priority_page, "priority");
    m_mode_stack.add(m_review_panel, "review");
    m_mode_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_mode_stack.set_transition_duration(150);
    m_mode_stack.set_vexpand(true);

    // --- navigation ---
    //
    // Destinations are actions behind a menu, so a new one is a line here
    // and a line in the model rather than another control in the header.
    m_nav_actions = Gio::SimpleActionGroup::create();
    // The stack page keeps its old id: the destination was renamed, not
    // replaced, and the id is internal.
    m_nav_actions->add_action("lifetree", [this]() { show_mode("priority", "Life Tree", "life"); });
    m_nav_actions->add_action("projects", [this]() { show_mode("priority", "Projects", "links"); });
    m_nav_actions->add_action("review", [this]() { show_mode("review", "Review", ""); });
    insert_action_group("go", m_nav_actions);

    auto menu = Gio::Menu::create();
    // Listed in the order they flow, which is the order the stack holds
    // them: the tree gives leaves, the leaves give projects.
    menu->append("Life Tree", "go.lifetree");
    menu->append("Projects", "go.projects");
    menu->append("Review", "go.review");

    // The platform's own hamburger. Named rather than bundled: GTK depends
    // on adwaita-icon-theme, and a user running a different icon theme gets
    // that theme's drawing of the same name.
    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(menu);
    m_menu_button.set_tooltip_text("Menu");

    m_back_button.set_icon_name("go-previous-symbolic");
    m_back_button.set_tooltip_text("Back to today");
    m_back_button.signal_clicked().connect([this]() { show_mode("daily", "", ""); });

    // Both in the leading slot, one shown at a time — the panel layout runs
    // reflective-to-active left to right, and stepping out of the day is one
    // more move that way.
    m_header_bar.pack_start(m_menu_button);
    m_header_bar.pack_start(m_back_button);

    show_mode("daily", "", "");

    m_root.append(m_mode_stack);
    m_root.append(m_footer);
    set_child(m_root);

    // Double-clicking a backlog row stages it in the schedule.
    m_task_panel.signal_task_chosen().connect(
        sigc::mem_fun(m_schedule_panel, &SchedulePanel::stage_task));
}

void MainWindow::commit_life_seed() {
    if (m_life_detail_populating || m_life_detail_id < 0) return;
    if (!m_life.contains(m_life_detail_id)) return;

    const std::string typed = m_life_detail_seed.get_buffer()->get_text();
    if (typed == m_life.get_seed(m_life_detail_id)) return;  // nothing to write

    m_life.set_seed(m_life_detail_id, typed);
}

void MainWindow::rebuild_life_ranking() {
    while (auto* child = m_life_ranked.get_first_child()) {
        m_life_ranked.remove(*child);
    }

    // Leaves only. An internal node's share is already accounted for by the
    // leaves beneath it, so listing both would double every branch and the
    // column would no longer sum to anything.
    std::vector<std::pair<double, int>> leaves;
    for (const auto& [id, share] : m_priority.priorities()) {
        if (!m_life.contains(id)) continue;
        if (!m_life.children_of(id).empty()) continue;
        leaves.push_back({share, id});
    }

    // Heaviest first, and by id when two are equal, so a redraw can't
    // reshuffle rows that are worth the same.
    std::sort(leaves.begin(), leaves.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second < b.second;
    });

    for (const auto& [share, id] : leaves) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

        auto* name = Gtk::make_managed<Gtk::Label>(m_life.display_title(id));
        name->set_xalign(0.0);
        name->set_hexpand(true);
        name->set_ellipsize(Pango::EllipsizeMode::END);
        row->append(*name);

        char figure[8];
        std::snprintf(figure, sizeof(figure), "%d%%", static_cast<int>(std::lround(share)));
        auto* value = Gtk::make_managed<Gtk::Label>(figure);
        value->add_css_class("dim-label");
        row->append(*value);

        // A row is a way into the tree, not just a readout: the list is
        // where you notice something is mis-weighted, and the node is where
        // you fix it.
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*row);
        button->set_has_frame(false);
        button->signal_clicked().connect([this, id]() { m_life_view.select(id); });
        m_life_ranked.append(*button);
    }
}

void MainWindow::show_life_node(int id) {
    // Whatever was being typed belongs to the node it was typed against, so
    // it goes to the database before the panel points anywhere else.
    if (id != m_life_detail_id) commit_life_seed();

    // The root shows too: it isn't a category above the goals, it's the
    // whole of what the tree is for, and naming it is the first thing
    // someone should be able to do.
    const bool has_node = id >= 0 && m_life.contains(id);
    m_life_detail_id = has_node ? id : -1;

    m_life_detail_hint.set_visible(!has_node);
    m_life_detail_title.set_visible(has_node);
    m_life_detail_seed_scroll.set_visible(has_node);
    if (!has_node) return;

    // Guarded: setting these fires the same handlers that write them back,
    // which would rewrite each node with its own value on every selection.
    m_life_detail_populating = true;
    m_life_detail_title.set_text(m_life.get_title(id));
    m_life_detail_seed.get_buffer()->set_text(m_life.get_seed(id));
    m_life_detail_populating = false;
}

void MainWindow::show_mode(const std::string& name, const std::string& title,
                           const std::string& sub_page) {
    // The sub-page first, so a jump straight to Projects from the menu has
    // already switched underneath before the mode stack reveals it —
    // otherwise the life tree flashes and then slides away.
    if (!sub_page.empty()) m_priority_stack.set_visible_child(sub_page);

    m_mode_stack.set_visible_child(name);

    // The header's title slot is otherwise unused, so it costs nothing to
    // let it answer "where am I" — which the old single button never did.
    m_header_title.set_text(title);

    const bool away = (name != "daily");
    m_menu_button.set_visible(!away);
    m_back_button.set_visible(away);
}
