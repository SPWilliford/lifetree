#ifndef APP_HPP
#define APP_HPP

#include <memory>
#include "core/Database.hpp"
#include "core/TreeController.hpp"
#include "core/Work.hpp"
#include "core/Priority.hpp"
#include "core/TaskAttributes.hpp"

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

public:
    App();
    ~App() = default;

    int run(int argc, char* argv[]);

    // Panels depend on these, not on App itself.
    TreeController& life() { return m_life; }
    TreeController& projects() { return m_projects; }
    Work& work() { return m_work; }
    TaskAttributes& task_attributes() { return m_task_attributes; }
    Priority& priority() { return m_priority; }
};

#endif
