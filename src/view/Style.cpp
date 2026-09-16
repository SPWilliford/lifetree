#include "view/Style.hpp"

#include <gdkmm/display.h>
#include <glibmm/markup.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/label.h>
#include <gtkmm/styleprovider.h>
#include <gtkmm/widget.h>

namespace {

constexpr const char* ACCENT_HEX = "#f2bf33";

// One value for both themes (it goes into CSS and Pango markup). About
// 3.9:1 against either background, so it never carries meaning alone.
constexpr const char* SERVED_HEX = "#388e3c";

// GTK 4.10 deprecated the API that resolved theme colors for Cairo;
// get_color() still gives the foreground. These stand in for the surface.
const Gdk::RGBA DARK_BACKGROUND("#212126");
const Gdk::RGBA LIGHT_BACKGROUND("#fafafa");

// Only @theme_fg_color and @theme_bg_color are guaranteed by every theme; a
// named color that doesn't resolve makes GTK drop the whole declaration.
const char* STYLESHEET = R"css(
@define-color lt_border alpha(@theme_fg_color, 0.18);
@define-color lt_surface alpha(@theme_fg_color, 0.04);

.panel-left, .panel-right {
  padding: 8px;
  border: 2px solid @lt_border;
}

/* The node detail panel: caption-style title over the seed. */
.node-title {
  font-size: 0.85em;
  font-weight: normal;
  letter-spacing: 0.09em;
  opacity: 0.62;
}

/* Transparent: a TextView draws its own background by default. */
.seed-view, .seed-view text {
  background: none;
  color: @theme_fg_color;
  font-size: 1.22em;
  font-style: italic;
}

.staged-dock {
  background-color: @lt_surface;
  border: 2px solid @lt_border;
  padding: 10px 14px;
}

/* The date label doubles as the day-settings button; the hover state is
   its only affordance. */
.day-button {
  padding: 2px 0;
  background: none;
  border: none;
  box-shadow: none;
  min-height: 0;
}

.day-button:hover {
  background-color: alpha(@theme_fg_color, 0.08);
}

.dock-path {
  font-size: 0.85em;
  opacity: 0.55;
}

.dock-title {
  font-size: 1.2em;
  font-weight: bold;
}

/* Tabular figures so the width holds still as the seconds run. */
.dock-elapsed {
  font-feature-settings: "tnum";
  opacity: 0.65;
}

.dock-activate {
  min-width: 34px;
  min-height: 34px;
  padding: 0;
  border-radius: 50%;
  font-size: 1.05em;
}

.dock-complete {
  min-width: 26px;
  min-height: 26px;
  padding: 0 8px;
}

/* Disclosure arrows. GTK's structure is
   `treeexpander > [indent]* > [expander] > child`. */
treeexpander > expander {
  opacity: 0.45;
  -gtk-icon-size: 14px;
}

listview > row:hover treeexpander > expander {
  opacity: 1;
}

/* A leaf's arrow: transparent, not hidden, so it keeps its width and the
   title column holds. The second selector outranks the hover rule. */
treeexpander.leaf-row > expander,
listview > row:hover treeexpander.leaf-row > expander {
  opacity: 0;
}

listview > row:hover {
  background-color: alpha(@theme_fg_color, 0.05);
}

/* The trailing "+" row: an affordance, not data. */
.card-row-action {
  opacity: 0.55;
}

.card-row-action:hover {
  opacity: 1.0;
}

/* A goal some project serves. On the widget: color inherits down. */
.leaf-served {
  color: @lt_served;
}

)css";

// Opaque interpolation: a blended tick over a band doesn't tint it the way
// an alpha one would.
Gdk::RGBA blend(const Gdk::RGBA& from, const Gdk::RGBA& to, double amount) {
    Gdk::RGBA out;
    out.set_rgba(from.get_red() + (to.get_red() - from.get_red()) * amount,
                 from.get_green() + (to.get_green() - from.get_green()) * amount,
                 from.get_blue() + (to.get_blue() - from.get_blue()) * amount, 1.0);
    return out;
}

}  // namespace

namespace style {

std::vector<const char*> project_swatches() {
    // All at one luminance, ~3.9:1 against both backgrounds — the best a
    // single fixed color can do. Separation from the nearest earlier entry
    // falls 35, 21, 20, 13, 11, 8, 6 in OKLab, so past eight a new entry is
    // only a shade of one already here.
    return {
        "#398f4b",  // green
        "#bf4dd1",  // magenta
        "#dd4b49",  // red
        "#3a82c7",  // blue
        "#9c7938",  // ochre
        "#816ce3",  // violet
        "#3b8997",  // teal
        "#b76b3a",  // brown
    };
}

const char* served_hex() {
    return SERVED_HEX;
}

void install() {
    auto provider = Gtk::CssProvider::create();

    provider->load_from_data(std::string("@define-color lt_accent ") + ACCENT_HEX + ";\n" +
                             "@define-color lt_served " + SERVED_HEX + ";\n" + STYLESHEET);

    Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), provider,
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

Palette palette_for(const Gtk::Widget& widget) {
    Palette palette;

    palette.text = widget.get_color();

    // Light text means a dark theme. Rec. 601 luma.
    const double luma = 0.299 * palette.text.get_red() + 0.587 * palette.text.get_green() +
                        0.114 * palette.text.get_blue();
    palette.dark = luma > 0.5;

    palette.background = palette.dark ? DARK_BACKGROUND : LIGHT_BACKGROUND;
    palette.accent = Gdk::RGBA(ACCENT_HEX);

    palette.tick_major = blend(palette.background, palette.text, 0.85);
    palette.tick_minor = blend(palette.background, palette.text, 0.55);
    palette.tick_faint = blend(palette.background, palette.text, 0.35);

    palette.band_fallback = blend(palette.background, palette.text, 0.65);

    return palette;
}

Gdk::RGBA parse_hex(const std::string& hex, const Gdk::RGBA& fallback) {
    if (hex.empty()) return fallback;

    Gdk::RGBA parsed;
    return parsed.set(hex) ? parsed : fallback;
}

void set_source(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& color, double alpha) {
    cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(),
                        color.get_alpha() * alpha);
}

void set_colored_text(Gtk::Label& label, const std::string& text, const std::string& hex_color) {
    if (hex_color.empty() || text.empty()) {
        label.set_text(text);
        return;
    }
    label.set_markup("<span foreground='" + hex_color + "'>" + Glib::Markup::escape_text(text) +
                     "</span>");
}

}  // namespace style
