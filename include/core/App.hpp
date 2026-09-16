#ifndef APP_HPP
#define APP_HPP

#include <memory>
#include <string>

#include "core/Database.hpp"
#include "core/Day.hpp"
#include "core/Priority.hpp"
#include "core/TaskAttributes.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"

// The assembled domain, wired in dependency order. Knows nothing about any
// user interface; a frontend takes a reference and reads the accessors.
class App {
public:
    // Opens or creates the database at db_path, migrating it if needed.
    // Throws if that fails.
    explicit App(const std::string& db_path);
    ~App() = default;

    TreeController& life() { return m_life; }
    TreeController& projects() { return m_projects; }
    Work& work() { return m_work; }
    TaskAttributes& task_attributes() { return m_task_attributes; }
    Priority& priority() { return m_priority; }
    Day& day() { return m_day; }

private:
    // Declaration order is construction order, and each member takes
    // references to the ones above it.
    std::shared_ptr<Database> m_db;
    TreeController m_life;
    TreeController m_projects;
    TaskAttributes m_task_attributes;
    Priority m_priority;
    Work m_work;
    Day m_day;
};

#endif
