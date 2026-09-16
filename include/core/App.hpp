#ifndef APP_HPP
#define APP_HPP

#include <memory>

#include "core/Database.hpp"
#include "core/Day.hpp"
#include "core/Priority.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"

// The assembled domain: the database, the two trees, and everything hanging
// off them, wired in dependency order and loaded in the right sequence.
//
// Deliberately knows nothing about GTK, or about any user interface at all.
// A frontend attaches to this by taking a reference and reading the
// accessors below; it is never the other way round. That's the whole
// property — swapping the GUI means writing a different main(), and this
// file doesn't change.
class App {
private:
    std::shared_ptr<Database> m_db;

    TreeController m_life;
    TreeController m_projects;

    // Declared before m_work because Work takes a reference to it
    // and subscribes to the tree in its own constructor — member init
    // order follows declaration order, so this has to be the real one.
    TaskAttributes m_task_attributes;

    // Depends on m_life only — the cascade is pure life tree arithmetic.
    Priority m_priority;

    Work m_work;

    // Depends on the database only: a day's hours are a fact about a date
    // and touch neither tree.
    Day m_day;

public:
    // Opens the database, migrating it if needed, and brings every
    // component up. Throws if the database can't be opened or migrated —
    // see main(), which is where that's reported.
    App();
    ~App() = default;

    // Panels depend on these, not on App itself.
    TreeController& life() { return m_life; }
    TreeController& projects() { return m_projects; }
    Work& work() { return m_work; }
    TaskAttributes& task_attributes() { return m_task_attributes; }
    Priority& priority() { return m_priority; }
    Day& day() { return m_day; }
};

#endif
