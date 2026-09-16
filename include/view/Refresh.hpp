#ifndef REFRESH_HPP
#define REFRESH_HPP

#include <glibmm/main.h>
#include <sigc++/sigc++.h>

// Runs an action at most once per idle cycle however often it's requested.
// Deferring keeps a panel from rebuilding its model inside the GTK event
// that triggered the change; coalescing absorbs bursts of core signals.
//
// sigc::trackable: an owner destroyed with a run pending empties the slot.
class Refresh : public sigc::trackable {
public:
    explicit Refresh(sigc::slot<void()> action) : m_action(std::move(action)) {}

    void request() {
        if (m_pending) return;
        m_pending = true;
        Glib::signal_idle().connect_once(sigc::mem_fun(*this, &Refresh::fire));
    }

private:
    void fire() {
        // Cleared before the action, so a request made by the action itself
        // schedules a fresh run.
        m_pending = false;
        m_action();
    }

    sigc::slot<void()> m_action;
    bool m_pending = false;
};

#endif
