#ifndef STYLE_HPP
#define STYLE_HPP

#include <string>
#include <vector>

#include <cairomm/context.h>
#include <gdkmm/rgba.h>

namespace Gtk {
class Widget;
}

// The rule this file exists to enforce: no color literal anywhere else in
// the codebase. A widget adds a CSS class, or a draw function asks for a
// palette. Nothing is cached — caching is how a panel ends up still dark
// after the theme switches to light.
namespace style {

// Every color the Cairo-drawn panels need, against the theme in force now.
struct Palette {
    // The one value approximated rather than read from the theme — see
    // Style.cpp.
    Gdk::RGBA background;

    // Read straight off the widget. Everything below except accent derives
    // from this, which is what makes the panels track the theme.
    Gdk::RGBA text;

    // LifeTree's own, not the theme's. Fixed: an accent that followed the
    // system accent would stop meaning "the present moment" and start
    // meaning "selected".
    Gdk::RGBA accent;

    // Brightest on the hour. Opaque blends toward the background rather
    // than alpha, so a tick crossing a band doesn't tint it.
    Gdk::RGBA tick_major;
    Gdk::RGBA tick_minor;
    Gdk::RGBA tick_faint;

    // For a band whose project has no color. Neutral so it never competes
    // with accent.
    Gdk::RGBA band_fallback;

    // True when the theme's text is light, i.e. a dark variant is in force.
    bool dark = false;
};

// The fixed palette a project's color is chosen from, as "#RRGGBB". Literal
// while everything else here is derived, because a project color is user
// data written to the database — it can't shift when the theme does.
//
// Ordered by how distinct each one is from the ones before it, so picking
// down the list is picking the best-separated set of that size. The
// separation falls off a cliff — see Style.cpp.
std::vector<const char*> project_swatches();

// A life tree leaf some project is aimed at. The app's one color that means
// a STATE rather than an identity — everything else colored is colored to
// say which thing it is. Nothing marks an unserved leaf: plain text already
// reads as the absence, and a second color would make not having got to
// something yet look like an error.
//
// The same value the .leaf-served CSS class carries; this is for CardRow,
// which colors through Pango markup rather than a class.
const char* served_hex();

// Installs the application stylesheet display-wide. Call once, before the
// first window is shown.
void install();

// Call INSIDE a draw function, every time, and don't store the result. It
// costs nothing next to a repaint, and per-draw means a theme change needs
// no invalidation: the next frame is simply correct.
Palette palette_for(const Gtk::Widget& widget);

// "#RRGGBB" -> a color, or fallback if empty or malformed. The boundary
// where stored project colors become drawable ones.
Gdk::RGBA parse_hex(const std::string& hex, const Gdk::RGBA& fallback);

// set_source_rgba with a Gdk::RGBA. alpha multiplies the color's own, so a
// translucent source stays translucent rather than being overridden.
void set_source(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& color,
                double alpha = 1.0);

}  // namespace style

#endif
