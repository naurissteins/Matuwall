matuwall(5)

# NAME

matuwall - configuration file for the matuwall wallpaper picker

# DESCRIPTION

*matuwall*(1) reads _$XDG_CONFIG_HOME/matuwall/config.toml_, falling back to
_~/.config/matuwall/config.toml_. The file is read once at startup. There is
no hot reload.

A missing file runs with built-in defaults, so matuwall works with no config
at all. A syntax error is reported with its path and line number, and matuwall
continues with defaults rather than failing to start. A recognized key with an
out-of-range or wrong-typed value is warned about and the default for that key
is kept; the rest of the file still applies. An unknown key or unknown section
is an error that rejects the whole file.

The format is a small subset of TOML: _[section]_ tables and _key = value_
pairs, where a value is a double-quoted string, an integer, a boolean, or an
array of double-quoted strings. _#_ begins a comment, including at the end of a
line. Comments are not allowed inside an array.

# SECTIONS

## [general]

*directory*
	Directory scanned for wallpapers. A leading _~_ expands to _$HOME_.
	Defaults to _"~/Pictures/Wallpapers"_.

	When the directory is missing, unreadable, or not a directory, the picker
	shows a folder warning glyph. The exact path and error are written to
	standard error and the diagnostic log and reported by *--diagnose*.

*backend*
	Wallpaper daemon used to apply the selection. One of _"sweetbg"_
	(*sweetbg img <path>*), _"awww"_ (*awww img -- <path>*), or _"auto"_.
	Defaults to _"sweetbg"_.

	_"auto"_ probes sweetbg first, then awww, and picks the first one that is
	both installed and *running*: its client on *PATH* and its daemon socket
	live. A daemon that is installed but stopped is skipped rather than
	selected and failed on.

	Naming a backend explicitly never probes; it is used exactly as given.

	For awww, only the default daemon namespace is probed. With
	*awww-daemon --namespace <name>*, set _"awww"_ explicitly instead of
	_"auto"_.

	An unknown name, or _"auto"_ with no daemon running, is reported when you
	press *Enter*; the picker exits non-zero without changing the wallpaper.

## [window]

*preview*
	Show the selected wallpaper full-screen behind the grid, so moving the
	selection looks like the wallpaper changed. _true_ by default. Nothing is
	applied until you press *Enter*; *Escape* leaves the real wallpaper
	untouched.

	With preview on, the picker's surface covers the whole output and
	*position* places the grid panel inside it. Until the first wallpaper
	finishes decoding the surface stays transparent, so the real desktop shows
	through.

	The backdrop costs one output-sized image in memory (about 33 MB at 3840x2160,
	15 MB at 2560x1440) plus a larger drawing surface, and adds a few
	milliseconds to the first frame. Set it to _false_ for the older
	panel-sized window with no backdrop.

*close_on_focus_loss*
	Close Matuwall when keyboard focus leaves its surface. Defaults to _true_.
	Set this to _false_ to keep the picker open while focus moves to another
	output; moving focus back restores keyboard control. Starting another
	Matuwall invocation still replaces the open picker.

*position*
	Where the picker sits on screen: _"center"_, _"left"_, _"right"_,
	_"top"_, or _"bottom"_. Defaults to _"center"_. With *preview* on this
	places the panel within the full-screen surface.

*background*
	Panel color, as _"#rrggbb"_ (opaque) or _"#rrggbbaa"_ (with an alpha
	byte). Defaults to _"#1e1e2ecc"_. With *preview* on this is the color of
	the panel that floats over the wallpaper, so its alpha decides how much of
	the backdrop shows through behind the tiles.

*margin*
	Pixels between the tiles and the panel edge. Defaults to _24_.

*edge_margin*
	Pixels between the panel and the screen edge for non-centered *position*
	values. Defaults to _24_. Has no effect when *position* is _"center"_.

*radius*
	Panel corner radius in pixels. Defaults to _16_. This is independent of
	_grid.radius_, which controls the tile corners.

## [input]

*mouse*
	Enable mouse interaction. When enabled, moving over a tile selects it, the
	scroll wheel navigates the grid, and a left click applies the selected tile.
	With preview enabled, clicking outside the panel cancels. Defaults to _false_,
	leaving navigation and selection keyboard-only.

## [animation]

*navigation_ms*
	Duration of navigation transitions in milliseconds. Adjacent selections and
	one-row scrolling glide smoothly; wrapped row changes and longer jumps
	crossfade the selection ring. Defaults to _110_. Set to _0_ to disable
	navigation animation. Must be an integer from 0 to 1000.

*zoom_percent*
	Amount the selected thumbnail grows, as a percentage of its configured size.
	The previous thumbnail shrinks while the new selection grows on the navigation
	timeline. Defaults to _3_. Set to _0_ to disable focus scaling. Must be an
	integer from 0 to 10.

## [grid]

*columns*
	Maximum number of tile columns. Matuwall uses fewer when the selected
	output is too narrow for the configured grid. In carousel mode this
	is the maximum number of visible tiles along either axis. Defaults to _5_.

*spacing*
	Pixels between tiles. Defaults to _16_.

*radius*
	Tile corner radius in pixels. Defaults to _8_.

*border_width*
	Border width drawn inside every thumbnail in logical pixels. Defaults to _0_,
	which disables thumbnail borders. Must be an integer from 0 to 4096.

*shadow_width*
	Distance in logical pixels over which the thumbnail shadow fades outward.
	Only the exterior rounded edges are drawn; no shadow fill is rendered beneath
	the thumbnail. Defaults to _0_, which disables shadows. Must be an integer
	from 0 to 4096.

*ring_width*
	Selection ring width in logical pixels. Defaults to _2_. Set to _0_ to disable
	the ring. Must be an integer from 0 to 4096.

*visible_rows*
	Maximum rows kept visible before the grid scrolls. Matuwall uses fewer when
	the selected output is too short for the configured grid. Ignored when
	*carousel* is enabled. Defaults to _2_.

*carousel*
	Turn the grid into a centered, single-axis strip. Defaults to _false_. At
	_window.position_ _"top"_, _"bottom"_, or _"center"_, the strip is one
	horizontal row navigated with Left/Right. At _"left"_ or _"right"_, it is
	one vertical column navigated with Up/Down. The selected tile remains
	centered while the strip moves. The carousel wraps continuously in both
	directions, and repeats thumbnails when needed to keep the panel full.
	Orthogonal arrows and Page Up/Page Down do nothing; Home/End still jump to
	the ends.

Matuwall keeps the configured tile size, spacing, and margin while fitting the
grid, and always displays at least one tile.

## [thumbnail]

*width*, *height*
	Tile size in logical pixels. Default to _240_ and _400_. The thumbnail
	cache key includes these, so changing them regenerates thumbnails at the
	new size.

## [colors]

Each value is a color, as _"#rrggbb"_ or _"#rrggbbaa"_.

*tile*
	Placeholder shown on a tile until its thumbnail is ready. Defaults to
	_"#313244ff"_.

*border*
	Border drawn inside every thumbnail when _grid.border_width_ is nonzero.
	Defaults to _"#585b70ff"_.

*shadow*
	Shadow fading outward from every thumbnail when _grid.shadow_width_ is
	nonzero. Defaults to _"#00000066"_. Its alpha controls the shadow strength.

*ring*
	Selection ring around the focused tile. Defaults to _"#f2cdcdff"_.

*spinner*
	Loading dot pulsing on tiles that are still decoding. Defaults to
	_"#cdd0e6ff"_.

## [hooks]

*on_apply*
	An array of command strings run after the wallpaper is applied, for
	example to regenerate a colorscheme. Empty by default.

	Each command is split on whitespace into an argument vector and run
	directly with _execvp_(3); there is *no shell*, so pipes, globs, variable
	expansion, and quoting have no effect. The token _{path}_ is replaced with
	the absolute wallpaper path wherever it appears, always within a single
	argument, so a filename containing spaces or shell metacharacters is passed
	safely and never interpreted as code.

	Hooks are spawned detached and are not waited on, so a slow generator never
	delays the picker's exit. A command that fails to start logs a warning; its
	exit status is otherwise ignored. Up to 16 hooks are run.

# EXAMPLE

```
[general]
directory = "~/Pictures/Wallpapers"

[window]
preview    = true
close_on_focus_loss = true
position   = "center"
background = "#1e1e2ecc"
margin     = 24
edge_margin = 24
radius     = 16

[input]
mouse = false

[animation]
navigation_ms = 110
zoom_percent = 3

[grid]
columns = 5
spacing = 16
radius  = 8
border_width = 0
shadow_width = 0
ring_width = 2
visible_rows = 2
carousel = false

[thumbnail]
width  = 240
height = 400

[colors]
border = "#585b70ff"
shadow = "#00000066"

[hooks]
on_apply = [
	"matugen image {path}",
	"wal -i {path} -n",
]
```

# SEE ALSO

*matuwall*(1)
