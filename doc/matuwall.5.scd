matuwall(5)

# NAME

matuwall - configuration file for the matuwall wallpaper picker

# DESCRIPTION

*matuwall*(1) reads _$XDG_CONFIG_HOME/matuwall/config.toml_, or
_~/.config/matuwall/config.toml_. A missing file uses built-in defaults.

Syntax errors include a line number and cause the whole file to be ignored.
Invalid known values produce a warning and use that key's default. Unknown keys
or sections cause the whole file to be ignored.

The supported TOML subset has _[section]_ tables and _key = value_ pairs. Values
may be double-quoted strings, integers, booleans, or arrays of double-quoted
strings. _#_ starts a comment. Comments are not allowed inside arrays.

# SECTIONS

## [general]

*directory*
	Wallpaper directory. _~_ expands to _$HOME_; relative paths use the current
	directory. Defaults to _"~/Pictures/Wallpapers"_.

*backend*
	Wallpaper backend: _"awww"_, _"sweetbg"_, or _"auto"_. Defaults to
	_"awww"_.

	_"auto"_ selects the first running backend, checking sweetbg before awww.
	Explicit backends are used without probing. Apply failures leave the
	wallpaper unchanged and exit non-zero.

## [backend.sweetbg], [backend.awww]

*args*
	Extra flags for that backend's _img_ command. Defaults to an empty array.
	Each item is one argument, placed before the wallpaper path. For awww they
	come before _--_. Only the selected backend's args are used.

	Arguments are passed as-is, without a shell. The daemon rejects flags it
	does not know. At most 32 arguments of up to 255 bytes each are accepted.

## [window]

*preview*
	Show the selected wallpaper full-screen behind the panel. Defaults to
	_false_. Nothing is applied until *Enter*; *Escape* leaves the wallpaper
	unchanged.

*close_on_focus_loss*
	Close when keyboard focus leaves Matuwall. Defaults to _false_.

*position*
	Panel position: _"center"_, _"left"_, _"right"_, _"top"_, or _"bottom"_.
	Defaults to _"bottom"_.

*background*
	Panel color as _"#rrggbb"_ or _"#rrggbbaa"_. Defaults to _"#11111bff"_.

*margin*
	Space between the tiles and panel edge, in pixels. Defaults to _34_.

*edge_margin*
	Space between the panel and screen edge, in pixels. Defaults to _24_. Ignored
	when *position* is _"center"_.

*radius*
	Panel corner radius in pixels. Defaults to _40_.

## [input]

*mouse*
	Enable hover, scrolling, and click-to-apply. With preview on, clicking
	outside the panel cancels. Defaults to _false_.

## [animation]

*navigation_ms*
	Navigation transition duration in milliseconds, from 0 to 1000. Defaults to
	_410_; zero disables the transition.

*zoom_percent*
	Selected-thumbnail growth, from 0 to 10 percent. Defaults to _10_; zero
	disables scaling.

## [grid]

*columns*
	Maximum tile columns. Fewer are used when needed to fit the output. Defaults
	to _5_. In carousel mode this limits visible tiles on either axis.

*spacing*
	Space between tiles in pixels. Defaults to _16_.

*radius*
	Tile corner radius in pixels. Defaults to _20_.

*border_width*
	Thumbnail border width in pixels, from 0 to 4096. Defaults to _0_; zero
	disables borders.

*shadow_width*
	Thumbnail shadow fade width in pixels, from 0 to 4096. Defaults to _12_; zero
	disables shadows.

*ring_width*
	Selection ring width in pixels, from 0 to 4096. Defaults to _0_; zero disables
	the ring.

*visible_rows*
	Maximum visible rows. Fewer are used when needed to fit the output. Defaults
	to _1_ and is ignored in carousel mode.

*carousel*
	Use a centered, wrapping strip. Defaults to _true_. Top, bottom, and center
	positions are horizontal; left and right positions are vertical. The selected
	tile stays centered.

*edge*
	Carousel edge style: _"auto"_, _"clip"_, _"peek"_, or _"fade"_. Defaults to
	_"auto"_. _clip_ cuts tiles at the panel margin. _peek_ lets them extend into
	it. _fade_ shrinks and fades tiles as they leave or enter. _auto_ fades when
	*background* is fully transparent and clips otherwise. The regular grid
	treats _fade_ as _clip_. The old *edge_peek* key still works but warns.

## [thumbnail]

*width*, *height*
	Tile size in logical pixels. Defaults to _300_ by _169_. Changing either
	value regenerates thumbnails at the new size. A tile larger than the
	output is clipped; the panel never grows past it.

## [colors]

All colors use _"#rrggbb"_ or _"#rrggbbaa"_.

*tile*
	Placeholder color. Defaults to _"#313244ff"_.

*border*
	Thumbnail border color. Defaults to _"#585b70ff"_.

*shadow*
	Thumbnail shadow color. Defaults to _"#00000066"_.

*ring*
	Selection ring color. Defaults to _"#f2cdcdff"_.

*spinner*
	Loading indicator color. Defaults to _"#cdd0e6ff"_.

## [hooks]

*on_apply*
	Commands run after a successful apply. Defaults to an empty array. _{path}_
	is replaced with the absolute wallpaper path, with symlinks resolved.

	Commands are split on whitespace and run directly, without a shell. Pipes,
	globs, variables, and shell quoting are not supported. Hooks run detached and
	do not delay exit. At most 16 commands of up to 511 bytes each are accepted.

# EXAMPLE

```
[general]
directory = "~/Pictures/Wallpapers"
backend = "awww"

[backend.sweetbg]
args = ["--persist"]

[backend.awww]
args = ["--transition-type", "grow", "--transition-duration", "1.5"]

[window]
preview = false
position = "bottom"
background = "#11111bff"

[grid]
columns = 5
carousel = true

[hooks]
on_apply = [
	"matugen image {path}",
]
```

# SEE ALSO

*matuwall*(1)
