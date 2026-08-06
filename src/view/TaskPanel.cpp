#include "view/TaskPanel.hpp"
#include "view/TreePanel.hpp"   // reuses TreeObject, the id-wrapper GObject
#include "core/TreeController.hpp"
#include "core/Work.hpp"
#include "core/TaskAttributes.hpp"
#include "core/Priority.hpp"
#include "core/Scheduler.hpp"
#include <algorithm>
#include <vector>
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

TaskPanel::TaskPanel(TreeController& projects, Work& worklog, TaskAttributes& task_attributes, Priority& priority)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_projects(projects), m_work(worklog), m_task_attributes(task_attributes), m_priority(priority)
{
    initialize_layout();

    // Deferred to idle rather than run synchronously — avoids rebuilding
    // the store while GTK is still partway through delivering whatever
    // event triggered the change.
    m_projects.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh(); });
    });

    // A full rebuild rather than the in-place restyle TreePanel does for
    // this same signal, because generator status changes this list's
    // *membership* and not just its appearance — refresh() excludes
    // generators from the backlog. Marking something repeating on a day
    // it isn't due spawns no children, so no tree signal fires, and this
    // is the only notice that the node should leave the list. Cheap here
    // regardless: a flat list has no expand state to lose.
    // Associations and weights change the ORDER of this list, not its
    // membership, but a re-sort still means rebuilding the store.
    m_priority.connect_changed([this]() {
        Glib::signal_idle().connect_once([this]() { refresh(); });
    });

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

    // Full path rather than just the immediate parent, matching how
    // Completed Today shows it. ancestor_path() already excludes the
    // hidden root, so an empty result just means no prefix.
    TaskSnapshot snap = m_task_attributes.snapshot(obj->node_id());

    set_colored_text(*path_label, snap.path.empty() ? "" : snap.path + " - ", snap.color);
    set_colored_text(*title_label, snap.title, snap.color);
}

void TaskPanel::refresh() {
    m_store->remove_all();

    const auto blocked = m_task_attributes.blocked_tasks();

    std::vector<scheduler::Candidate> candidates;
    for (int id : m_projects.leaves()) {
        if (m_task_attributes.is_generator(id)) continue; // the generator itself isn't a real task

        // A top-level node is a project, not a task — that's what the rest
        // of the system already assumes, since colors, life goal links and
        // work_log.project_root_id all hang off exactly these ids.
        //
        // It matters here because "leaf" and "task" aren't the same thing.
        // A project becomes a leaf the moment its last task is completed,
        // and an ongoing category like "Clean" would then reappear in the
        // backlog as if it were work — every time you finished clearing it.
        if (m_projects.parent_of(id) == 0) continue;

        // Held back behind an earlier sibling in a sequential project —
        // real work, deliberately out of sight until its turn.
        if (blocked.count(id) > 0) continue;

        candidates.push_back({ id, m_task_attributes.project_root_of(id) });
    }

    // leaves() walks an unordered map, so the order it hands back isn't
    // defined between runs. Sorting here gives the scheduler a stable
    // input, which is what lets an unchanged backlog produce an unchanged
    // list — the scheduler itself is deterministic, but only as far as
    // what it's given is.
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.project_id != b.project_id) return a.project_id < b.project_id;
        return a.task_id < b.task_id;
    });

    // Interleaved rather than sorted: sorting groups every project's tasks
    // into one block, so the top of the list is always the same project and
    // a fragmented goal is never reached at all.
    for (int id : scheduler::interleave(candidates, m_priority.project_priorities())) {
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

    for (const auto& entry : m_work.entries_for_completed_day(std::time(nullptr))) {
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
