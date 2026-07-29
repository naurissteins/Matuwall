sweetwall(5)

# NAME

sweetwall - configuration file for the sweetwall wallpaper picker

# DESCRIPTION

*sweetwall*(1) reads _$XDG_CONFIG_HOME/sweetwall/config.toml_, falling back to
_~/.config/sweetwall/config.toml_. The file is read once at startup. There is
no hot reload.

A missing file runs with built-in defaults, so sweetwall works with no config
at all. A syntax error is reported with its path and line number, and sweetwall
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

*backend*
	Wallpaper daemon used to apply the selection. A backend name, currently
	only _"sweetbg"_, or _"auto"_ to probe the known backends in order and use
	the first one found. Defaults to _"sweetbg"_. An unknown name, or _"auto"_
	with nothing installed, is reported when you press *Enter* and the picker
	exits non-zero without changing the wallpaper.

## [window]

*position*
	Where the picker sits on screen: _"center"_, _"left"_, _"right"_,
	_"top"_, or _"bottom"_. Defaults to _"center"_.

*background*
	Surface color, as _"#rrggbb"_ (opaque) or _"#rrggbbaa"_ (with an alpha
	byte). Defaults to _"#1e1e2ecc"_.

*margin*
	Pixels between the tiles and the surface edge. Defaults to _24_.

## [grid]

*columns*
	Number of tile columns. Defaults to _5_.

*spacing*
	Pixels between tiles. Defaults to _16_.

*radius*
	Tile corner radius in pixels. Defaults to _8_.

*visible_rows*
	Rows kept visible before the grid scrolls. Defaults to _2_.

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
position   = "center"
background = "#1e1e2ecc"
margin     = 24

[grid]
columns = 5
spacing = 16
radius  = 8

[thumbnail]
width  = 240
height = 400

[hooks]
on_apply = [
	"matugen image {path}",
	"wal -i {path} -n",
]
```

# SEE ALSO

*sweetwall*(1)
