#include "view/TaskPanel.hpp"
#include "view/TreePanel.hpp"   // reuses TreeObject, the id-wrapper GObject
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
#include "engine/TaskAttributes.hpp"
#include <pangomm/layout.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/signallistitemfactory.h>
#include <gtkmm/gestureclick.h>
#include <glibmm/main.h>
#include <cstdio>

TaskPanel::TaskPanel(ITreeController& projects, WorkLog& worklog, TaskAttributes& task_attributes)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12), m_projects(projects), m_worklog(worklog), m_task_attributes(task_attributes)
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

void TaskPanel::bind_actions() {
    // Nothing to bind right now — completion moved to SchedulePanel,
    // and the Refresh button was removed once connect_changed made it
    // redundant. Kept as a hook for whatever comes next.
}

void TaskPanel::on_setup(const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    // hexpand+default(FILL) here, NOT halign(END) — the row needs to
    // always claim the full, actual viewport width so it shrinks and
    // grows correctly as the pane is resized. halign(END) alone doesn't
    // do that: it only repositions the row within its own natural width,
    // so a narrower viewport doesn't shrink it — it just scrolls
    // horizontally instead, defaulting to showing the (now mostly blank)
    // left edge while the real content sits off-screen to the right.
    box->set_hexpand(true);

    // Absorbs the extra space, pushing title+path to sit snug against
    // the right edge — the actual "right-justified" mechanism, applied
    // inside a row that itself always spans the real width available.
    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);

    // Plain, read-only label — this panel is a mirror of the tree, not
    // an editing surface. Double-click sends the task to the schedule
    // instead of starting an edit.
    auto* title_label = Gtk::make_managed<Gtk::Label>();
    // Capped the same way path_label is below — without it, an unusually
    // long title alone (with the spacer already squeezed to nothing)
    // could still force the row wider than the available space.
    title_label->set_ellipsize(Pango::EllipsizeMode::END);
    title_label->set_max_width_chars(40);

    auto* path_label = Gtk::make_managed<Gtk::Label>();
    path_label->add_css_class("dim-label");
    // Caps how wide this is allowed to want to be, same reason as
    // SchedulePanel's staged-task label — a long path shouldn't force
    // the row (and the panel) wider than intended. Ellipsizing from the
    // START rather than the end, since the part closest to the actual
    // task (the immediate parent, at the end of a root-first path) is
    // probably more useful to keep visible than the far-off root name.
    path_label->set_ellipsize(Pango::EllipsizeMode::START);
    path_label->set_max_width_chars(30);

    box->append(*spacer);
    box->append(*title_label);
    box->append(*path_label);

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

    // First child is now the spacer, not title_label — get_next_sibling()
    // steps past it. path_label is still the last child either way.
    auto* first = box->get_first_child();
    auto* title_label = dynamic_cast<Gtk::Label*>(first ? first->get_next_sibling() : nullptr);
    auto* path_label = dynamic_cast<Gtk::Label*>(box->get_last_child());
    if (!title_label || !path_label) return;

    int id = obj->node_id();

    // Full path rather than just the immediate parent, matching how
    // Completed Today shows it. ancestor_path() already excludes the
    // hidden root, so an empty result just means nothing trails the
    // title. If the immediate parent is a generator, its title is
    // identical to this instance's own title (that's how spawning
    // works) — showing it would just duplicate the title, so skip
    // straight to its ancestors. Segments themselves stay in normal
    // root-first reading order — only the label's position moved.
    int parent_id = m_projects.parent_of(id);
    std::string path = m_task_attributes.is_generator(parent_id)
        ? m_projects.ancestor_path(parent_id)
        : m_projects.ancestor_path(id);
    title_label->set_text(m_projects.get_title(id));
    path_label->set_text(path.empty() ? "" : " - " + path);
}

void TaskPanel::refresh() {
    m_store->remove_all();
    for (int id : m_projects.leaves()) {
        if (m_task_attributes.is_generator(id)) continue; // the generator itself isn't a real task
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
