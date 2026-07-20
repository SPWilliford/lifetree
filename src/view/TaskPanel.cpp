#include "view/TaskPanel.hpp"
#include "view/TreePanel.hpp"   // reuses TreeObject, the id-wrapper GObject
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
#include <gtkmm/singleselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/gestureclick.h>
#include <glibmm/main.h>
#include <cstdio>

TaskPanel::TaskPanel(ITreeController& projects, WorkLog& worklog)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_projects(projects), m_worklog(worklog)
{
    initialize_layout();
    bind_actions();

    // Deferred to idle rather than run synchronously — avoids rebuilding
    // the store while GTK is still partway through delivering whatever
    // event triggered the change.
    m_projects.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh(); });
    });

    refresh();
}

void TaskPanel::initialize_layout() {
    set_hexpand(true);
    set_vexpand(true);

    m_switcher.set_stack(m_stack);
    m_switcher.set_hexpand(true);
    m_header.append(m_switcher);
    append(m_header);

    // --- Backlog page ---
    m_store = Gio::ListStore<Glib::Object>::create();

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &TaskPanel::on_setup));
    factory->signal_bind().connect(sigc::mem_fun(*this, &TaskPanel::on_bind));
    m_list_view.set_factory(factory);
    m_list_view.set_model(Gtk::SingleSelection::create(m_store));
    m_list_view.set_hexpand(true);
    m_list_view.set_vexpand(true);

    auto* backlog_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    backlog_scroll->set_child(m_list_view);
    backlog_scroll->set_hexpand(true);
    backlog_scroll->set_vexpand(true);
    backlog_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    backlog_scroll->set_min_content_width(300);

    // --- Completed Today page — plain ListBox, not a full ListView/
    // factory setup. This list is small (a day's worth of completions)
    // and purely for viewing, so the extra machinery isn't worth it.
    m_completed_list.set_selection_mode(Gtk::SelectionMode::NONE);
    auto* completed_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    completed_scroll->set_child(m_completed_list);
    completed_scroll->set_hexpand(true);
    completed_scroll->set_vexpand(true);
    completed_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    completed_scroll->set_min_content_width(300);

    m_stack.add(*backlog_scroll, "backlog_page", "Backlog");
    m_stack.add(*completed_scroll, "completed_page", "Completed Today");
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    append(m_stack);

    // Rebuild the completed list whenever that tab is switched to —
    // lazy refresh is enough for a look-back view, no need to keep it
    // live the instant something completes elsewhere.
    m_stack.property_visible_child_name().signal_changed().connect(
        sigc::mem_fun(*this, &TaskPanel::refresh_completed));
}

void TaskPanel::bind_actions() {
    // Nothing to bind right now — completion moved to SchedulePanel,
    // and the Refresh button was removed once connect_changed made it
    // redundant. Kept as a hook for whatever comes next.
}

void TaskPanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);

    auto* prefix_label = Gtk::make_managed<Gtk::Label>();
    prefix_label->add_css_class("dim-label");

    // Plain, read-only label — this panel is a mirror of the tree, not
    // an editing surface. Double-click sends the task to the schedule
    // instead of starting an edit.
    auto* title_label = Gtk::make_managed<Gtk::Label>();
    title_label->set_halign(Gtk::Align::START);
    title_label->set_hexpand(true);

    box->append(*prefix_label);
    box->append(*title_label);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this, item](int n_press, double, double) {
        if (n_press == 2) {
            auto obj = std::dynamic_pointer_cast<TreeObject>(item->get_item());
            if (obj) m_task_chosen.emit(obj->node_id());
        }
    });
    box->add_controller(click);

    item->set_child(*box);
}

void TaskPanel::on_bind(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto obj = std::dynamic_pointer_cast<TreeObject>(item->get_item());
    if (!obj) return;

    auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
    if (!box) return;

    auto* prefix_label = dynamic_cast<Gtk::Label*>(box->get_first_child());
    auto* title_label = dynamic_cast<Gtk::Label*>(box->get_last_child());
    if (!prefix_label || !title_label) return;

    int id = obj->node_id();
    int parent_id = m_projects.parent_of(id);

    // 0 is the hidden root — not a real category, so no prefix in that case.
    prefix_label->set_text(parent_id > 0 ? (m_projects.get_title(parent_id) + " - ") : "");
    title_label->set_text(m_projects.get_title(id));
}

void TaskPanel::refresh() {
    m_store->remove_all();
    for (int id : m_projects.leaves()) {
        m_store->append(TreeObject::create(id));
    }
}

namespace {
    std::string format_duration(time_t seconds) {
        int total_minutes = static_cast<int>(seconds / 60);
        int hours = total_minutes / 60;
        int minutes = total_minutes % 60;
        char buf[16];
        if (hours > 0) {
            std::snprintf(buf, sizeof(buf), "%dh %dm", hours, minutes);
        } else {
            std::snprintf(buf, sizeof(buf), "%dm", minutes);
        }
        return buf;
    }
}

void TaskPanel::refresh_completed() {
    // Only rebuild when this is actually the visible page — switching
    // away from it fires the same signal, no need to redo the work.
    if (m_stack.get_visible_child_name() != "completed_page") return;

    while (auto* row = m_completed_list.get_row_at_index(0)) {
        m_completed_list.remove(*row);
    }

    for (const auto& entry : m_worklog.entries_for_day(std::time(nullptr))) {
        auto* row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row_box->set_margin(6);

        auto* path_label = Gtk::make_managed<Gtk::Label>(entry.path.empty() ? "" : entry.path + " - ");
        path_label->add_css_class("dim-label");

        auto* title_label = Gtk::make_managed<Gtk::Label>(entry.title);
        title_label->set_halign(Gtk::Align::START);
        title_label->set_hexpand(true);

        auto* duration_label = Gtk::make_managed<Gtk::Label>(format_duration(entry.end_time - entry.start_time));
        duration_label->add_css_class("dim-label");

        row_box->append(*path_label);
        row_box->append(*title_label);
        row_box->append(*duration_label);

        m_completed_list.append(*row_box);
    }
}
