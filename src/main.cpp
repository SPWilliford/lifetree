#include <exception>
#include <iostream>

#include <gtkmm/application.h>

#include "core/App.hpp"
#include "view/MainWindow.hpp"

// The only place the domain and a user interface meet. App knows nothing
// about GTK. Swapping the frontend means rewriting this file and nothing
// else.
int main(int argc, char* argv[]) {
    try {
        App app;

        auto gtk_app = Gtk::Application::create("org.lifetree.app");

        // app outlives run(), so by reference is safe; gtk_app by value
        // because the signal can outlive this scope's local.
        gtk_app->signal_activate().connect([&app, gtk_app]() {
            auto* window = Gtk::make_managed<MainWindow>(app);
            gtk_app->add_window(*window);
            window->set_visible(true);
        });

        return gtk_app->run(argc, argv);
    } catch (const std::exception& e) {
        // Almost always the database: unopenable, or a failed migration.
        std::cerr << "LifeTree failed to start: " << e.what() << std::endl;
        return 1;
    }
}
