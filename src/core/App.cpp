#include "core/App.hpp"

// Temporary. Prints the life tree with each node's weight and the priority
// that falls out of it, so the cascade can be checked against a real tree
// before anything depends on its numbers. Set to 0 or delete once you trust
// it — nothing else refers to this.
#define PRIORITY_DEBUG 1
#if PRIORITY_DEBUG
#include <iostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>

namespace {
    void dump_life(TreeController& life, const Priority& priority,
                   const std::unordered_map<int, double>& values, int id, int depth) {
        auto it = values.find(id);
        const double value = (it != values.end()) ? it->second : 0.0;
        std::string title = life.get_title(id);
        if (title.empty()) title = "(untitled)";

        std::cout << std::fixed << std::setprecision(2) << std::setw(9) << value
                  << std::setw(6) << id << "  "
                  << std::string(depth * 2, ' ') << title;
        if (priority.has_weight(id)) std::cout << "   [weight " << priority.weight_of(id) << "]";
        std::cout << "\n";

        for (int child : life.children_of(id)) dump_life(life, priority, values, child, depth + 1);
    }

    void dump_report(TreeController& life, TreeController& projects, const Priority& priority) {
        std::cout << "\n================ priority report ================\n";

        std::cout << "\nLIFE TREE      (id is what you INSERT against)\n";
        std::cout << " priority    id  title\n";
        dump_life(life, priority, priority.priorities(), 0, 0);

        const auto project_values = priority.project_priorities();
        std::cout << "\nPROJECTS       ranked, highest first\n";
        std::cout << " priority    id  title\n";
        for (int id : priority.ranked_projects()) {
            std::string title = projects.get_title(id);
            if (title.empty()) title = "(untitled)";
            std::cout << std::fixed << std::setprecision(2) << std::setw(9) << project_values.at(id)
                      << std::setw(6) << id << "  " << title;

            std::vector<int> leaves = priority.leaves_for(id);
            if (leaves.empty()) {
                std::cout << "   <- no associations";
            } else {
                std::cout << "   <- ";
                for (std::size_t i = 0; i < leaves.size(); ++i) {
                    if (i) std::cout << ", ";
                    std::cout << life.get_title(leaves[i])
                              << " (w" << priority.link_weight(id, leaves[i]) << ")";
                }
            }
            std::cout << "\n";
        }

        const auto unserved = priority.unserved_leaves();
        std::cout << "\nUNSERVED LEAVES   priority nothing is working toward\n";
        if (life.leaves().empty()) {
            std::cout << "  (the life tree has no leaves yet - not the same as full coverage)\n";
        } else if (unserved.empty()) {
            std::cout << "  none - every leaf has at least one project\n";
        } else {
            for (const auto& [id, value] : unserved) {
                std::cout << std::fixed << std::setprecision(2) << std::setw(9) << value
                          << std::setw(6) << id << "  " << life.get_title(id) << "\n";
            }
        }

        double served = 0.0;
        for (const auto& [id, value] : project_values) served += value;
        double missing = 0.0;
        for (const auto& [id, value] : unserved) missing += value;
        std::cout << "\n  served " << served << " + unserved " << missing
                  << " = " << (served + missing) << "\n";
        std::cout << "\nTo experiment:\n"
                  << "  INSERT OR REPLACE INTO life_weights VALUES (<life_id>, <0-100>);\n"
                  << "  INSERT OR REPLACE INTO project_links VALUES (<project_id>, <leaf_id>, <weight>);\n"
                  << "================================================\n\n";
    }
}
#endif
#include <gtkmm/application.h>
#include "view/MainWindow.hpp"

App::App()
    : m_db(std::make_shared<Database>("lifetree.db")),
      m_life(m_db, TreeType::LIFE),
      m_projects(m_db, TreeType::PROJECTS),
      m_task_attributes(m_db, m_projects),
      m_priority(m_db, m_life, m_projects),
      m_work(m_db, m_projects, m_task_attributes)
{
    m_db->insert_root(TreeType::LIFE, "Live a Good Life");
    m_db->insert_root(TreeType::PROJECTS, "Master Project Root");

    m_life.load();
    m_projects.load();

    m_task_attributes.load();
    m_priority.load(); // after m_life, whose nodes the weights refer to

#if PRIORITY_DEBUG
    dump_report(m_life, m_projects, m_priority);
#endif
    m_task_attributes.run_spawn_scan(); // catches up on anything due since the app was last open
}

int App::run(int argc, char* argv[]) {
    auto app = Gtk::Application::create("org.lifetree.app");

    app->signal_activate().connect([this, app]() {
        auto* window = Gtk::make_managed<MainWindow>(*this);
        app->add_window(*window);
        window->set_visible(true);
    });

    return app->run(argc, argv);
}
