#ifndef SCHEDULEPANEL_HPP
#define SCHEDULEPANEL_HPP
#include <gtkmm/box.h>
#include <gtkmm/overlay.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/button.h>
#include <cairomm/context.h>
#include <cairo.h>
#include <ctime>
#include <vector>
#include "engine/Database.hpp"

class ITreeController;
class WorkLog;
class TaskAttributes;

// The main schedule view.
//
// Layout is deliberately inverted from a conventional top-down calendar:
// future time is above center and past time is below, and the whole
// thing drifts downward rather than up. The idea is that "future rises,
// past sinks" — matching the app's own tree metaphor (things grow upward
// from a base) — reads more intuitively than the usual top-to-bottom
// reading-order convention once you're used to it.
//
// There's no separate header anymore — the staged-task controls sit in
// a dock attached to the right edge of the panel, overlaid on the
// timeline rather than in a strip below it. Both the clock (on the
// left) and the dock (on the right) sit at fixed pixel positions/widths
// — genuinely solid, unaffected by how wide the panel itself ends up
// being as the side panels get dragged. Resizing the panel only ever
// changes the empty space *between* them; nothing about either end
// needs recomputing or re-syncing when that happens, which is also what
// keeps them from ever drifting out of sync with what they draw (see
// CLOCK_CENTER_X, BAND_WIDTH). The staged task's band of worked time is
// drawn growing downward from the exact point the dock's bottom edge
// touches the line — the idea being that the band visually emerges from
// the task producing it, the way roots grow down from a trunk.
//
// The dock (m_staged_box) is two rows: play/pause and a checkmark
// "complete" button side by side on top, the task label below — bottom
// edge resting on the line, right-aligned against the panel's edge with
// a small margin (see BAND_RIGHT_MARGIN, initialize_layout). Its drawn
// frame (in draw_now_line) always stays visible, extending all the way
// to the panel's right edge like part of the panel's own frame, even
// with nothing staged — only the content inside (label text, which of
// the two buttons apply) changes. Complete only ever applies once a
// task is actually active — never to a task that hasn't been worked at
// all — so both buttons stay in the layout at all times (keeping the
// dock's size stable) but use opacity, not visibility, to show only
// what currently applies; removing a button from layout instead would
// resize the whole dock every time you activate or deactivate. A task
// gets staged here via double-click in the backlog (TaskPanel) — this
// panel doesn't reach into the backlog itself, it just exposes
// stage_task() for whoever's coordinating that. Complete is the *only*
// place a task actually gets removed from the tree — TreePanel and
// TaskPanel no longer do this.
//
// Every band today — the live one and all of today's completed history
// — shares the same fixed width and the same right-aligned position,
// under the staged-task box. Deliberately uniform: a task's own
// name/label width isn't what determines how wide its band is, so
// completed bands don't shift around based on whatever happens to be
// staged later.
//
// This panel doesn't do its own time bookkeeping — WorkLog owns the
// current session (start/stop) and the permanent history. SchedulePanel
// just asks it questions and draws the answers.
class SchedulePanel : public Gtk::Box {
private:
    ITreeController& m_projects;
    WorkLog& m_worklog;
    TaskAttributes& m_task_attributes;

    // -1 = nothing staged. This is the *only* task that can be activated
    // or completed from this panel. This stays local rather than moving
    // into WorkLog — it's "which slot is populated," a UI selection
    // concept, not a domain fact about time worked.
    int m_staged_id = -1;

    // Today's completed sessions, drawn on the timeline as fixed bands
    // alongside the current one — a static snapshot re-pulled whenever a
    // task completes, not re-queried on every draw (draw_timeline runs
    // on every 100ms tick).
    std::vector<WorkLogRow> m_completed_bands;

    // Play/pause and checkmark, side by side — the top row of the
    // staged-task dock.
    Gtk::Box    m_staged_buttons_row{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_activate_button; // label toggles ▶ / ⏸ — see refresh_staged_label
    Gtk::Button m_complete_button{"✓"}; // only ever active while a task is actually active — see below

    // The dock's content: the buttons row on top, the task label below.
    // Bottom-anchored to the line via a margin trick (see
    // initialize_layout): valign(CENTER) plus a bottom margin equal to
    // the box's own natural height means the *padded* box centers on the
    // line, which puts the *content*'s bottom edge exactly on it, rather
    // than centering the content itself on the line the way m_now_line's
    // clock box does.
    Gtk::Box    m_staged_box{Gtk::Orientation::VERTICAL, 4};
    Gtk::Label  m_slot_label;

    // m_staged_box's true content height, cached once at construction
    // time — *before* the bottom-anchor margin gets applied to it (see
    // initialize_layout). get_preferred_size() on a widget that already
    // has a margin set folds that margin back into the reported size, so
    // re-querying m_staged_box directly later would report something
    // like double the real content height. Reused everywhere that needs
    // to know the content's actual height instead of re-measuring it.
    int m_staged_box_content_height = 0;

    // m_slot_label's own height alone (not the whole m_staged_box) —
    // needed to draw a border around just the label, not the whole dock.
    // Cached once, same reasoning as m_staged_box_content_height above;
    // stable regardless of what text the label ends up showing, since
    // it's a single-line, non-wrapping label.
    int m_slot_label_height = 0;

    // body
    Gtk::Overlay     m_body;
    Gtk::DrawingArea m_timeline;
    Gtk::DrawingArea m_now_line;

    void initialize_layout();
    void refresh_staged_label();
    void refresh_completed_bands();

    // title/path/color for a given task id — shared by refresh_staged_label
    // (for display), and by on_activate_clicked/on_complete_clicked
    // (for recording a permanent segment). Must be called while the node
    // still exists — ancestor_path()/parent_of() need it walkable, and
    // for on_complete_clicked specifically, that means calling this
    // *before* removing the node.
    struct TaskSnapshot { std::string title, path, color; };
    TaskSnapshot snapshot_task(int id) const;

    void draw_timeline(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);
    void draw_now_line(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height);

    bool on_timer_tick();
    void on_activate_clicked();
    void on_complete_clicked();

public:
    SchedulePanel(ITreeController& projects, WorkLog& worklog, TaskAttributes& task_attributes);
    ~SchedulePanel() override = default;

    // Loads a task into the slot. Whatever was staged (and however far
    // along it was) is simply replaced — no confirmation, at least for now.
    void stage_task(int task_id);
};
#endif
