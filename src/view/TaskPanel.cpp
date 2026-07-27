#include "view/TaskPanel.hpp"
#include "view/TreePanel.hpp"   // reuses TreeObject, the id-wrapper GObject
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
#include "engine/TaskAttributes.hpp"
#include <pangomm/layout.h>
#include <glibmm/markup.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/gestureclick.h>
#include <glibmm/main.h>
#include <cstdio>

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

    // Shared by both the path and title labels, in both Backlog and
    // Completed Today — colors the text if a color is set, plain
    // otherwise. Path labels keep their "dim-label" CSS class regardless
    // (that's set once, at construction, not here) — dim-label reduces
    // the whole widget's opacity independent of whatever color the text
    // actually is, so a colored path still reads as a softened, washed-
    // out version of the same hue the title shows at full strength —
    // that's what visually distinguishes path from title now that both
    // carry the same color, without needing bold or a separately
    // computed darker shade.
    void set_colored_text(Gtk::Label& label, const std::string& text, const std::string& color) {
        if (!color.empty() && !text.empty()) {
            label.set_markup("<span foreground='" + color + "'>" + Glib::Markup::escape_text(text) + "</span>");
        } else {
            label.set_text(text);
        }
    }
}

TaskPanel::TaskPanel(ITreeController& projects, WorkLog& worklog, TaskAttributes& task_attributes)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_projects(projects), m_worklog(worklog), m_task_attributes(task_attributes)
{
    initialize_layout();

    // Deferred to idle rather than run synchronously — avoids rebuilding
    // the store while GTK is still partway through delivering whatever
    // event triggered the change.
    m_projects.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh(); });
    });

    // Same reason — a color or repeat-status change doesn't touch the
    // tree structure itself, so m_projects' signal above wouldn't fire
    // for it, but an already-bound row still needs to pick up the change.
    m_task_attributes.connect_changed([this]() {
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
    backlog_scroll->set_min_content_width(420);

    // --- Completed Today page — plain ListBox, not a full ListView/
    // factory setup. This list is small (a day's worth of completions)
    // and purely for viewing, so the extra machinery isn't worth it.
    m_completed_list.set_selection_mode(Gtk::SelectionMode::NONE);
    auto* completed_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    completed_scroll->set_child(m_completed_list);
    completed_scroll->set_hexpand(true);
    completed_scroll->set_vexpand(true);
    completed_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    completed_scroll->set_min_content_width(420);

    m_stack.add(*backlog_scroll, "backlog_page", "Backlog");
    m_stack.add(*completed_scroll, "completed_page", "Complete");
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    append(m_stack);

    // Rebuild the completed list whenever that tab is switched to —
    // lazy refresh is enough for a look-back view, no need to keep it
    // live the instant something completes elsewhere.
    m_stack.property_visible_child_name().signal_changed().connect(
        sigc::mem_fun(*this, &TaskPanel::refresh_completed));
}

void TaskPanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->set_margin(6);

    auto* path_label = Gtk::make_managed<Gtk::Label>();
    path_label->add_css_class("dim-label");

    // Plain, read-only label — this panel is a mirror of the tree, not
    // an editing surface. Double-click sends the task to the schedule
    // instead of starting an edit.
    auto* title_label = Gtk::make_managed<Gtk::Label>();
    title_label->set_halign(Gtk::Align::START);
    title_label->set_hexpand(true);

    box->append(*path_label);
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

    auto* path_label = dynamic_cast<Gtk::Label*>(box->get_first_child());
    auto* title_label = dynamic_cast<Gtk::Label*>(box->get_last_child());
    if (!path_label || !title_label) return;

    int id = obj->node_id();

    // Full path rather than just the immediate parent, matching how
    // Completed Today shows it. ancestor_path() already excludes the
    // hidden root, so an empty result just means no prefix. If the
    // immediate parent is a generator, its title is identical to this
    // instance's own title (that's how spawning works) — showing it
    // would just duplicate the title, so skip straight to its ancestors.
    int parent_id = m_projects.parent_of(id);
    std::string path = m_task_attributes.is_generator(parent_id)
        ? m_projects.ancestor_path(parent_id)
        : m_projects.ancestor_path(id);

    std::string title = m_projects.get_title(id);
    std::string color = m_task_attributes.get_color(id);

    set_colored_text(*path_label, path.empty() ? "" : path + " - ", color);
    set_colored_text(*title_label, title, color);
}

void TaskPanel::refresh() {
    m_store->remove_all();
    for (int id : m_projects.leaves()) {
        if (m_task_attributes.is_generator(id)) continue; // the generator itself isn't a real task
        m_store->append(TreeObject::create(id));
    }
}

void TaskPanel::refresh_completed() {
    // Only rebuild when this is actually the visible page — switching
    // away from it fires the same signal, no need to redo the work.
    if (m_stack.get_visible_child_name() != "completed_page") return;

    while (auto* row = m_completed_list.get_row_at_index(0)) {
        m_completed_list.remove(*row);
    }

    for (const auto& entry : m_worklog.entries_for_completed_day(std::time(nullptr))) {
        auto* row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row_box->set_margin(6);

        auto* path_label = Gtk::make_managed<Gtk::Label>();
        path_label->add_css_class("dim-label");
        set_colored_text(*path_label, entry.path.empty() ? "" : entry.path + " - ", entry.color);

        auto* title_label = Gtk::make_managed<Gtk::Label>();
        set_colored_text(*title_label, entry.title, entry.color);
        title_label->set_halign(Gtk::Align::START);
        title_label->set_hexpand(true);

        auto* duration_label = Gtk::make_managed<Gtk::Label>(format_duration(entry.total_seconds));
        duration_label->add_css_class("dim-label");

        row_box->append(*path_label);
        row_box->append(*title_label);
        row_box->append(*duration_label);

        m_completed_list.append(*row_box);
    }
}
