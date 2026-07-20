#ifndef APPENGINE_HPP
#define APPENGINE_HPP

#include <memory>
#include "model/Entities.hpp"
#include "engine/Database.hpp"
#include "engine/TreeController.hpp"
#include "engine/WorkLog.hpp"
#include "engine/TaskAttributes.hpp"

class AppEngine {
private:
    std::shared_ptr<Database> m_db;

    TreeController<LifeNode> m_life;
    TreeController<TaskNode> m_projects;
    WorkLog m_work_log;
    TaskAttributes m_task_attributes;

public:
    AppEngine();
    ~AppEngine() = default;

    int run(int argc, char* argv[]);

    // Panels depend on these, not on AppEngine itself.
    ITreeController& life() { return m_life; }
    ITreeController& projects() { return m_projects; }
    WorkLog& work_log() { return m_work_log; }
    TaskAttributes& task_attributes() { return m_task_attributes; }
};

#endif
