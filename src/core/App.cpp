#include "core/App.hpp"

App::App(const std::string& db_path)
    : m_db(std::make_shared<Database>(db_path)),
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
    m_priority.load();
    m_day.load();

    m_priority.normalize();
    m_life.connect_changed([this]() { m_priority.normalize(); });
}
