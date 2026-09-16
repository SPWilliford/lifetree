#ifndef REFRESH_HPP
#define REFRESH_HPP

#include <glibmm/main.h>
#include <sigc++/sigc++.h>

// A re-read of whatever a panel shows, run at most once per idle cycle
// however many times it's asked for. Two problems:
//
// Deferring: a panel must not rebuild its list store while GTK is still
// delivering the event that triggered the change — that mutates a model out
// from under the widget currently dispatching.
//
// Coalescing: the core signals are coarse and arrive in bursts. A spawn
// scan adding six instances emits six tree-changed signals.
//
// Derives from sigc::trackable and binds through mem_fun, so an owner
// destroyed with a run pending empties the slot instead of leaving a
// callback into freed memory.
class Refresh : public sigc::trackable {
public:
    explicit Refresh(sigc::slot<void()> action) : m_action(std::move(action)) {}

    // Safe to call repeatedly: the first schedules, the rest are absorbed.
    void request() {
        if (m_pending) return;
        m_pending = true;
        Glib::signal_idle().connect_once(sigc::mem_fun(*this, &Refresh::fire));
    }

private:
    void fire() {
        // Cleared before the action, so anything the action itself requests
        // schedules a fresh run instead of being swallowed by this one.
        m_pending = false;
        m_action();
    }

    sigc::slot<void()> m_action;
    bool m_pending = false;
};

#endif
