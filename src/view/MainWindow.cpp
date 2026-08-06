#include "view/MainWindow.hpp"
#include "core/App.hpp"
#include "core/Work.hpp"

#include <gtkmm/cssprovider.h>
#include <gtkmm/stylecontext.h>
#include <gdkmm/display.h>
#include <glibmm/main.h>

#include <ctime>
#include <cstdio>

MainWindow::MainWindow(App& app)
    : m_work(app.work()),
      m_tree_panel(app.life(), app.projects(), app.task_attributes(), app.priority()),
      m_schedule_panel(app.projects(), app.work(), app.task_attributes()),
      m_task_panel(app.projects(), app.work(), app.task_attributes(), app.priority())
{
    // Deliberately empty: the header bar keeps the window controls but
    // shows no text. Set explicitly rather than left unset, since an unset
    // title falls back to the application name.
    set_title("");
    set_default_size(1400, 1000);

    apply_styles();

    m_header_bar.set_show_title_buttons(true); // standard minimize/maximize/close
    set_titlebar(m_header_bar);

    m_tree_panel.add_css_class("panel-left");
    m_task_panel.add_css_class("panel-right");

    m_tree_panel.set_hexpand(true);
    m_schedule_panel.set_hexpand(true);
    m_schedule_panel.set_vexpand(true);
    m_task_panel.set_hexpand(true);

    // A small gap on each side facing a divider — otherwise the panels
    // sit flush against the Paned's thin drag handle with nothing but
    // that handle between them.
    constexpr int GAP = 8;
    m_tree_panel.set_margin_end(GAP);
    m_schedule_panel.set_margin_start(GAP);
    m_schedule_panel.set_margin_end(GAP);
    m_task_panel.set_margin_start(GAP);

    m_inner_paned.set_start_child(m_schedule_panel);
    m_inner_paned.set_end_child(m_task_panel);
    m_inner_paned.set_resize_start_child(true); // schedule absorbs space as the divider moves,
    m_inner_paned.set_resize_end_child(false);  // task keeps whatever width you last dragged it to

    m_outer_paned.set_start_child(m_tree_panel);
    m_outer_paned.set_end_child(m_inner_paned);
    m_outer_paned.set_resize_start_child(false); // tree keeps whatever width you last dragged it to
    m_outer_paned.set_resize_end_child(true);    // the schedule+task pair absorbs the rest

    // Same starting proportions the old fixed widths gave — 340 for
    // tree, 420 for task, schedule getting whatever's left of the
    // default 1400 width. Purely a starting point now, not a floor —
    // every divider is draggable from here.
    m_outer_paned.set_position(340);
    m_inner_paned.set_position(1400 - 32 /* margin */ - 340 /* tree */ - 420 /* task */);

    m_outer_paned.set_margin(16);
    m_outer_paned.set_vexpand(true);

    m_footer.add_css_class("app-footer");
    m_footer.set_margin_start(16);
    m_footer.set_margin_end(16);
    m_footer.set_margin_bottom(8);

    m_footer_date.set_halign(Gtk::Align::START);
    m_footer_date.add_css_class("dim-label");

    m_footer_tracked.set_hexpand(true);
    m_footer_tracked.set_halign(Gtk::Align::END);
    m_footer_tracked.add_css_class("dim-label");

    m_footer.append(m_footer_date);
    m_footer.append(m_footer_tracked);

    m_root.append(m_outer_paned);
    m_root.append(m_footer);
    set_child(m_root);

    // The footer's figure comes from banked segments, so it moves when a
    // session is paused or a task completed — exactly what Work signals.
    m_work.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh_footer(); });
    });
    refresh_footer();

    // Double-clicking a backlog row stages it in the schedule.
    m_task_panel.signal_task_chosen().connect(
        sigc::mem_fun(m_schedule_panel, &SchedulePanel::stage_task));
}

void MainWindow::apply_styles() {
    auto provider = Gtk::CssProvider::create();

    // Padding so panel content doesn't sit against its own border. Square
    // corners and a 2px stroke, matching the schedule panel's Cairo frame
    // (see STROKE_WIDTH) so the three read as one set rather than three
    // unrelated treatments.
    provider->load_from_data(
        ".panel-left, .panel-right { padding: 8px; }\n"
        ".panel-left  { border: 2px solid #000000; }\n"
        ".panel-right { border: 2px solid #ffffff; }\n"
        ".app-footer  { padding: 2px 4px; }\n"
        // The schedule panel's staged-task dock. Gold to match the
        // timeline's own Cairo border below it, and the same dark fill the
        // timeline paints, so the two read as one panel split in two rather
        // than as unrelated boxes. The timeline's frame stays in Cairo —
        // it's drawn among the bands and ticks, not around a widget.
        ".staged-dock {\n"
        "  background-color: #212126;\n"
        "  border: 2px solid #f2bf33;\n"
        "  padding: 10px 14px;\n"
        "}\n"
    );

    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void MainWindow::refresh_footer() {
    const time_t now = std::time(nullptr);

    std::tm tm_buf{};
    localtime_r(&now, &tm_buf);
    char date[64];
    std::strftime(date, sizeof(date), "%A, %B %e", &tm_buf);
    m_footer_date.set_text(date);

    // Banked segments only. A session still running hasn't been written to
    // the work log yet, so this catches up when you pause rather than
    // ticking live — live would need its own timer, and the schedule
    // panel's clock is already the thing on screen that moves.
    long seconds = 0;
    for (const auto& row : m_work.entries_for_day(now)) {
        seconds += static_cast<long>(row.end_time - row.start_time);
    }

    char tracked[64];
    std::snprintf(tracked, sizeof(tracked), "%ldh %02ldm tracked today",
                  seconds / 3600, (seconds % 3600) / 60);
    m_footer_tracked.set_text(tracked);
}
