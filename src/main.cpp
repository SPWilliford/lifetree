#include <exception>
#include <filesystem>
#include <iostream>

#include <glibmm/miscutils.h>
#include <gtkmm/application.h>

#include "core/App.hpp"
#include "view/MainWindow.hpp"

// The only place the domain and the user interface meet.
int main(int argc, char* argv[]) {
    try {
        // ~/.local/share/lifetree/lifetree.db, not the working directory:
        // a launcher's cwd is wherever the desktop put it.
        const std::string dir = Glib::build_filename(Glib::get_user_data_dir(), "lifetree");
        std::filesystem::create_directories(dir);
        App app(Glib::build_filename(dir, "lifetree.db"));

        auto gtk_app = Gtk::Application::create("org.lifetree.app");
        gtk_app->signal_activate().connect([&app, gtk_app]() {
            auto* window = Gtk::make_managed<MainWindow>(app);
            gtk_app->add_window(*window);
            window->set_visible(true);
        });
        return gtk_app->run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "LifeTree failed to start: " << e.what() << std::endl;
        return 1;
    }
}
