sweetwall(1)

# NAME

sweetwall - Minimal wallpaper picker for Wayland

# SYNOPSIS

*sweetwall* [options]

# DESCRIPTION

*sweetwall* is a keyboard-driven grid of wallpaper thumbnails for Wayland. It
opens instantly, you move around the grid, press *Enter* to apply, and it exits.

# OPTIONS

*-h*, *--help*
	Show a help message and exit.

*-V*, *--version*
	Show version information and exit.

# KEY BINDINGS

*Arrow keys*, *h* *j* *k* *l*
	Move the selection.

*Home*, *g*
	Jump to the first tile.

*End*, *G*
	Jump to the last tile.

*Enter*
	Apply the selected wallpaper through the backend, then exit.

*Escape*
	Cancel without applying and exit.

# MOUSE

The keyboard is the primary interface, but the pointer is supported. Moving the
cursor over a tile selects it, a left click applies that tile (like *Enter*), and
the scroll wheel moves the selection through the grid.

# BACKENDS

On *Enter*, the selected wallpaper is handed to a wallpaper daemon. The default
backend is *sweetbg*(1), invoked as _sweetbg img <path>_. The _backend_ config
key selects a backend by name or _"auto"_ to probe. The path is passed as an
argument, never through a shell. A backend that is unknown or fails is reported
and sweetwall exits non-zero without changing the wallpaper.

After a successful apply, the _hooks.on_apply_ commands run (for example a
colorscheme generator), spawned detached. See *sweetwall*(5).

# CONFIGURATION

Sweetwall reads _$XDG_CONFIG_HOME/sweetwall/config.toml_, falling back to
_~/.config/sweetwall/config.toml_. It runs with defaults when no file is
present. See *sweetwall*(5) for the keys.

# FILES

_$XDG_CONFIG_HOME/sweetwall/config.toml_, _~/.config/sweetwall/config.toml_
	Per-user configuration. See *sweetwall*(5).

# SEE ALSO

*sweetwall*(5), *sweetbg*(1)
