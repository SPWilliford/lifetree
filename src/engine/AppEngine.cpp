#include "engine/AppEngine.hpp"
#include <gtkmm/application.h>
#include <view/MainWindow.hpp>

AppEngine::AppEngine()
    : m_db(std::make_shared<Database>("lifetree.db")),
      m_life(m_db, TreeType::LIFE, [](const Row& r) { return LifeNode{ r.title }; }),
      m_projects(m_db, TreeType::PROJECTS, [](const Row& r) { return TaskNode{ r.title }; }),
      m_work_log(m_db),
      m_task_attributes(m_db, m_projects)
{
    m_db->insert_root(TreeType::LIFE, "Live a Good Life");
    m_db->insert_root(TreeType::PROJECTS, "Master Project Root");

    m_life.load();
    m_projects.load();

    m_task_attributes.load();
    m_task_attributes.run_spawn_scan(); // catches up on anything due since the app was last open
}

int AppEngine::run(int argc, char* argv[]) {
    auto app = Gtk::Application::create("org.lifetree.app");

    app->signal_activate().connect([this, app]() {
        auto* window = Gtk::make_managed<MainWindow>(*this);
        app->add_window(*window);
        window->set_visible(true);
    });

    return app->run(argc, argv);
}
