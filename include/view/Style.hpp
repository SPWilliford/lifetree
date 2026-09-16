#ifndef STYLE_HPP
#define STYLE_HPP

#include <string>
#include <vector>

#include <cairomm/context.h>
#include <gdkmm/rgba.h>

namespace Gtk {
class Label;
class Widget;
}  // namespace Gtk

// Every color in the app comes through here: widgets add a CSS class, draw
// functions ask for a palette. Nothing is cached, so a theme switch takes
// effect on the next frame.
namespace style {

// Colors for the Cairo-drawn panels, resolved against the current theme.
struct Palette {
    Gdk::RGBA background;  // approximated, not read from the theme
    Gdk::RGBA text;        // read from the widget; everything else derives
    Gdk::RGBA accent;      // LifeTree's own, fixed: marks "now"
    Gdk::RGBA tick_major;
    Gdk::RGBA tick_minor;
    Gdk::RGBA tick_faint;
    Gdk::RGBA band_fallback;  // for a band whose project has no color
    bool dark = false;
};

// Project colors as "#RRGGBB", ordered by perceptual distance from the ones
// before, so the first N are the best-separated set of that size. Fixed
// values: they're stored in the database and can't follow the theme.
std::vector<const char*> project_swatches();

// Marks a life leaf some project serves. Same value as the .leaf-served
// CSS class, for code that colors through Pango markup.
const char* served_hex();

// Installs the stylesheet display-wide. Call once before the first window.
void install();

// Call inside a draw function every time; don't store the result.
Palette palette_for(const Gtk::Widget& widget);

// "#RRGGBB" -> color, or fallback if empty or malformed.
Gdk::RGBA parse_hex(const std::string& hex, const Gdk::RGBA& fallback);

// set_source_rgba with a Gdk::RGBA; alpha multiplies the color's own.
void set_source(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& color,
                double alpha = 1.0);

// Sets the label's text tinted with hex_color; plain text if the color is
// empty. Escapes the text.
void set_colored_text(Gtk::Label& label, const std::string& text, const std::string& hex_color);

}  // namespace style

#endif
