#include "view/Style.hpp"

#include <gdkmm/display.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/styleprovider.h>
#include <gtkmm/widget.h>

namespace {

// The single definition; the stylesheet interpolates it rather than
// repeating the literal.
constexpr const char* ACCENT_HEX = "#f2bf33";

// One value for both themes, like the project swatches and for the same
// reason: it's handed to CSS and to Pango markup, neither of which can be
// re-derived when the theme flips. Picked as the green with the least
// difference between the two — roughly 3.9:1 against either background,
// which is under the 4.5:1 wanted for text carrying meaning on its own.
// Acceptable because this never does: whether a goal is served is always
// also readable from the projects listed beside it.
constexpr const char* SERVED_HEX = "#388e3c";

// The one approximation. GTK 4.10 deprecated the only API that could
// resolve a named theme color into RGB for Cairo; get_color() survives and
// gives the foreground, which everything else derives from. A background
// can't be derived from a foreground, so these two stand in — nudge them if
// the timeline reads as a different surface from the panels.
const Gdk::RGBA DARK_BACKGROUND("#212126");
const Gdk::RGBA LIGHT_BACKGROUND("#fafafa");

// A named color that doesn't resolve makes GTK discard the whole
// declaration, silently — so this uses only the two every theme is
// guaranteed to define and builds the rest from them.
const char* STYLESHEET = R"css(
@define-color lt_border alpha(@theme_fg_color, 0.18);
@define-color lt_surface alpha(@theme_fg_color, 0.04);

.panel-left, .panel-right {
  padding: 8px;
  border: 2px solid @lt_border;
}

/* A node's detail: the title labels it, the seed says what it means.
   Weighted accordingly — the title is the handle you scan for in a list,
   and this panel isn't a list. */

/* Small, spaced and dimmed, sitting over the seed like a caption over the
   thing it names rather than a heading the seed hangs beneath. */
.node-title {
  font-size: 0.85em;
  font-weight: normal;
  letter-spacing: 0.09em;
  opacity: 0.62;
}

/* The substance. Larger and italic: a seed is a statement of intent someone
   wrote deliberately, and italic is how a quoted line has been set apart
   from surrounding text for about five hundred years.

   Transparent, so the panel reads as one surface — a TextView draws its own
   background by default, which against the panel's tint looks like a pane
   nested inside a pane. */
.seed-view, .seed-view text {
  background: none;
  color: @theme_fg_color;
  font-size: 1.22em;
  font-style: italic;
}

.app-footer {
  padding: 2px 4px;
}

/* Bordered like the other panels rather than in the accent, which earns
   its weight inside the timeline marking the present moment. */
.staged-dock {
  background-color: @lt_surface;
  border: 2px solid @lt_border;
  padding: 10px 14px;
}

/* The date, doubling as the way into the day's settings.

   No frame and no background at rest, so an untouched dock reads as a label
   over the timeline rather than as a control sitting on it. The hover state
   is therefore the entire affordance — without it there is nothing on screen
   saying this can be clicked.

   Padding pulled back to nothing horizontally: the dock already pads its
   contents, and a button's own padding would indent the date away from the
   left edge the staged task's path lines up with. */
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

/* Where the staged task came from. Small and dim: it's context for the
   title under it, not a second thing to read. Splitting it off the title is
   what lets colour mean "which project" and dimming mean "this is context"
   — inline, one string in one colour had to carry both. */
.dock-path {
  font-size: 0.85em;
  opacity: 0.55;
}

/* The staged task itself — the one thing this dock exists to name. Sized
   up rather than merely coloured, because a project colour caps at about
   3.9:1 against either background, which is thin for small text and
   comfortable at this size. */
.dock-title {
  font-size: 1.2em;
  font-weight: bold;
}

/* Elapsed. Tabular figures so the digits sit still as the seconds run
   rather than shuffling the label's width every tick. */
.dock-elapsed {
  font-feature-settings: "tnum";
  opacity: 0.65;
}

/* The dock's one primary action, and the only control in the app drawn as
   one. Round and larger than Complete beside it: there is never more than
   one main move here, and the size is what says which it is. */
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

/* Tree disclosure arrows.

   GTK's node structure is `treeexpander > [indent]* > [expander] > child`,
   so this reaches the arrow without touching the row.

   Faded and shrunk because there is one per branch and they are structure,
   not content: present when looked for, absent when scanning titles. Full
   strength on hover, which is when you're aiming at one. Opacity rather
   than colour so it works however GTK draws the glyph. */
treeexpander > expander {
  opacity: 0.45;
  -gtk-icon-size: 14px;
}

listview > row:hover treeexpander > expander {
  opacity: 1;
}

/* A leaf's arrow: invisible, but still there.

   Transparent rather than hidden, because hiding it takes its width too and
   a leaf's title slides left until it sits under its parent's. This way GTK
   lays out exactly what it always did and the column of titles holds, with
   no measured padding to re-tune when the icon size changes.

   The second rule beats the hover rule above on specificity — a leaf's
   arrow must stay invisible when the pointer crosses it, or the whole thing
   is undone at exactly the moment you'd notice. */
treeexpander.leaf-row > expander,
listview > row:hover treeexpander.leaf-row > expander {
  opacity: 0;
}

/* The whole row, not the text. Tracking depth across the width is what this
   is for, so colouring the title alone would defeat it. Kept very soft —
   this fires on every row the pointer crosses. */
listview > row:hover {
  background-color: alpha(@theme_fg_color, 0.05);
}

/* The weight spin is the only control living inside a tree row, and at its
   default size it makes that row half again as tall as a plain one — so
   toggling weights on would visibly stretch the whole tree. Stripped back
   to the row's own text height. GTK4's spin is a `text` node flanked by two
   `button`s; all three carry padding worth removing. */
.card-row spinbutton,
.card-row spinbutton text,
.card-row spinbutton button {
  min-height: 0;
  padding-top: 0;
  padding-bottom: 0;
}

.card-row spinbutton button {
  min-width: 22px;
  padding-left: 0;
  padding-right: 0;
}

/* A row that acts rather than holds data — the trailing "+". Dimmed so it
   reads as an affordance instead of a project actually named "New project",
   and brightening on hover is what says it's clickable. */
.card-row-action {
  opacity: 0.55;
}

.card-row-action:hover {
  opacity: 1.0;
}

/* A goal some project is aimed at. On the widget rather than its label:
   GTK inherits color down, and the leaf lists are labels inside buttons. */
.leaf-served {
  color: @lt_served;
}

/* Toggled by CardRow::set_active(), which nothing calls yet. */
.card-row-active {
  background-color: alpha(@lt_accent, 0.18);
  border-radius: 4px;
}
)css";

// Opaque interpolation, used instead of alpha wherever the result gets
// layered on: a 50%-alpha tick over a band tints it; a tick blended to the
// same tone against the background doesn't.
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
    // Every one of these sits at the same luminance, ~3.9:1 against both the
    // light and the dark background. That isn't a rounded-off choice: a
    // single fixed color readable on both surfaces can do no better than
    // 3.97:1, and the value that achieves it is one specific luminance. So
    // hue and chroma are the only things left to vary — which is exactly why
    // the list can't be much longer than this.
    //
    // Ordered greedily by perceptual distance, each one as far as possible
    // from all the ones above it. Measured in OKLab, the gap to the nearest
    // earlier color runs 35, 21, 20, 13, 11, 8, 6 — so the first four are
    // unmistakable, six are workable, and past eight a new entry would only
    // be a shade of one already here.
    //
    // The consequence worth knowing: color can group a handful of projects
    // at a glance, but it cannot identify a dozen. The title beside it is
    // what actually names the row.
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

    // Spliced in rather than written twice — see ACCENT_HEX.
    provider->load_from_data(std::string("@define-color lt_accent ") + ACCENT_HEX + ";\n" +
                             "@define-color lt_served " + SERVED_HEX + ";\n" + STYLESHEET);

    // Above the theme, below anything the user sets themselves.
    Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), provider,
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

Palette palette_for(const Gtk::Widget& widget) {
    Palette palette;

    // The theme's resolved foreground for this widget, which is the only
    // theme color still readable without the deprecated lookup API.
    palette.text = widget.get_color();

    // Light text means a dark theme is in force. Rec. 601 luma — plenty for
    // a binary question, and it needs no color-science machinery.
    const double luma = 0.299 * palette.text.get_red() + 0.587 * palette.text.get_green() +
                        0.114 * palette.text.get_blue();
    palette.dark = luma > 0.5;

    palette.background = palette.dark ? DARK_BACKGROUND : LIGHT_BACKGROUND;
    palette.accent = Gdk::RGBA(ACCENT_HEX);

    // Three strengths of the theme's text color. Derived rather than fixed,
    // so ticks stay legible against whatever background the theme hands us.
    palette.tick_major = blend(palette.background, palette.text, 0.85);
    palette.tick_minor = blend(palette.background, palette.text, 0.55);
    palette.tick_faint = blend(palette.background, palette.text, 0.35);

    palette.band_fallback = blend(palette.background, palette.text, 0.65);

    return palette;
}

Gdk::RGBA parse_hex(const std::string& hex, const Gdk::RGBA& fallback) {
    if (hex.empty()) return fallback;

    // set() reports failure rather than throwing, so malformed stored text
    // falls back instead of drawing something arbitrary. (set_parse in
    // gtkmm 3.)
    Gdk::RGBA parsed;
    return parsed.set(hex) ? parsed : fallback;
}

void set_source(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& color, double alpha) {
    cr->set_source_rgba(color.get_red(), color.get_green(), color.get_blue(),
                        color.get_alpha() * alpha);
}

}  // namespace style
