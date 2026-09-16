#include "view/MainWindow.hpp"

#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include "core/App.hpp"
#include "view/Style.hpp"

namespace {
constexpr int WINDOW_WIDTH = 1400;
constexpr int WINDOW_HEIGHT = 1000;
constexpr int PAGE_MARGIN = 16;
constexpr int PANEL_GAP = 8;
constexpr int TREE_START = 440;
constexpr int TASK_START = 420;
}  // namespace

MainWindow::MainWindow(App& app)
    : m_projects_panel(app.projects(), app.task_attributes()),
      m_life_tree_page(app.life(), app.priority()),
      m_projects_page(app.life(), app.projects(), app.priority()),
      m_review_page(app.life(), app.priority(), app.work()),
      m_schedule_panel(app.projects(), app.work(), app.task_attributes(), app.day()),
      m_task_panel(app.projects(), app.task_attributes(), app.priority()) {
    set_title("LifeTree");
    set_default_size(WINDOW_WIDTH, WINDOW_HEIGHT);

    style::install();

    // --- header ---
    m_header_title.add_css_class("title");
    m_header_bar.set_title_widget(m_header_title);
    m_header_bar.set_show_title_buttons(true);
    set_titlebar(m_header_bar);

    m_nav_actions = Gio::SimpleActionGroup::create();
    m_nav_actions->add_action("lifetree", [this]() { show_mode("priority", "Life Tree", "life"); });
    m_nav_actions->add_action("projects", [this]() { show_mode("priority", "Projects", "links"); });
    m_nav_actions->add_action("review", [this]() { show_mode("review", "Review", ""); });
    insert_action_group("go", m_nav_actions);

    auto menu = Gio::Menu::create();
    menu->append("Life Tree", "go.lifetree");
    menu->append("Projects", "go.projects");
    menu->append("Review", "go.review");

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(menu);
    m_menu_button.set_tooltip_text("Menu");

    m_back_button.set_icon_name("go-previous-symbolic");
    m_back_button.set_tooltip_text("Back to today");
    m_back_button.signal_clicked().connect([this]() { show_mode("daily", "", ""); });

    m_header_bar.pack_start(m_menu_button);
    m_header_bar.pack_start(m_back_button);

    // --- daily ---
    m_projects_panel.add_css_class("panel-left");
    m_task_panel.add_css_class("panel-right");

    m_projects_panel.set_hexpand(true);
    m_schedule_panel.set_hexpand(true);
    m_schedule_panel.set_vexpand(true);
    m_task_panel.set_hexpand(true);

    m_projects_panel.set_margin_end(PANEL_GAP);
    m_schedule_panel.set_margin_start(PANEL_GAP);
    m_schedule_panel.set_margin_end(PANEL_GAP);
    m_task_panel.set_margin_start(PANEL_GAP);

    // The schedule absorbs resizing; the tree and task list keep whatever
    // width they were last dragged to.
    m_inner_paned.set_start_child(m_schedule_panel);
    m_inner_paned.set_end_child(m_task_panel);
    m_inner_paned.set_resize_start_child(true);
    m_inner_paned.set_resize_end_child(false);

    m_outer_paned.set_start_child(m_projects_panel);
    m_outer_paned.set_end_child(m_inner_paned);
    m_outer_paned.set_resize_start_child(false);
    m_outer_paned.set_resize_end_child(true);

    m_outer_paned.set_position(TREE_START);
    m_inner_paned.set_position(WINDOW_WIDTH - 2 * PAGE_MARGIN - TREE_START - TASK_START);
    m_outer_paned.set_margin(PAGE_MARGIN);
    m_outer_paned.set_vexpand(true);

    m_daily_page.append(m_outer_paned);

    m_task_panel.signal_task_chosen().connect(
        sigc::mem_fun(m_schedule_panel, &SchedulePanel::stage_task));

    // --- priority ---
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

    m_life_tree_column.append(m_life_tree_page);
    m_life_tree_column.append(m_to_projects_button);

    m_projects_page.set_vexpand(true);
    m_projects_column.append(m_to_life_tree_button);
    m_projects_column.append(m_projects_page);

    m_priority_stack.add(m_life_tree_column, "life");
    m_priority_stack.add(m_projects_column, "links");
    m_priority_stack.set_transition_type(Gtk::StackTransitionType::SLIDE_UP_DOWN);
    m_priority_stack.set_transition_duration(220);
    m_priority_stack.set_vexpand(true);

    m_priority_page.set_margin(PAGE_MARGIN);
    m_priority_page.append(m_priority_stack);

    // --- modes ---
    m_mode_stack.add(m_daily_page, "daily");
    m_mode_stack.add(m_priority_page, "priority");
    m_mode_stack.add(m_review_page, "review");
    m_mode_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_mode_stack.set_transition_duration(150);
    m_mode_stack.set_vexpand(true);

    show_mode("daily", "", "");
    set_child(m_mode_stack);
}

void MainWindow::show_mode(const std::string& name, const std::string& title,
                           const std::string& sub_page) {
    if (!sub_page.empty()) m_priority_stack.set_visible_child(sub_page);
    m_mode_stack.set_visible_child(name);
    m_header_title.set_text(title);

    const bool away = (name != "daily");
    m_menu_button.set_visible(!away);
    m_back_button.set_visible(away);
}
