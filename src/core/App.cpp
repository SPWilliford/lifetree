#include "core/App.hpp"

App::App()
    : m_db(std::make_shared<Database>("lifetree.db")),
      m_life(m_db, TreeType::LIFE),
      m_projects(m_db, TreeType::PROJECTS),
      m_task_attributes(m_db, m_projects),
      m_priority(m_db, m_life, m_projects),
      m_work(m_db, m_projects, m_task_attributes),
      m_day(m_db) {
    m_db->insert_root(TreeType::LIFE, "Live a Good Life");
    m_db->insert_root(TreeType::PROJECTS, "Master Project Root");

    m_life.load();
    m_projects.load();

    m_task_attributes.load();
    m_priority.load();  // after m_life, whose nodes the weights refer to
    m_day.load();

    // On a database from before the every-child-has-a-weight rule this is
    // the migration; on one already in line it does nothing.
    m_priority.normalize();

    // A new life node arrives with no weight, leaving its siblings' set
    // short. Renormalizing on the tree's signal is what keeps the invariant
    // true between edits rather than only at startup.
    m_life.connect_changed([this]() { m_priority.normalize(); });
}
