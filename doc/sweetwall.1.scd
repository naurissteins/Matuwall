sweetwall(1)

# NAME

sweetwall - Minimal wallpaper picker for Wayland

# SYNOPSIS

*sweetwall* [options]

# DESCRIPTION

*sweetwall* is a keyboard-driven grid of wallpaper thumbnails for Wayland. It
opens instantly, you move around the grid, press *Enter* to apply, and it exits.

By default the selected wallpaper is previewed full-screen behind the grid, so
moving the selection looks like the wallpaper changed. The preview is drawn by
sweetwall alone: the wallpaper daemon is not involved until *Enter*, and
*Escape* leaves the real wallpaper untouched. See _preview_ in *sweetwall*(5).

# OPTIONS

*--background* _COLOR_
	Temporarily set _window.background_. _COLOR_ must be _"#rrggbb"_ or
	_"#rrggbbaa"_. Quote the value in a shell because it starts with _#_. The
	configuration file is not changed.

*-b* _BACKEND_, *--backend* _BACKEND_
	Temporarily use _BACKEND_ instead of the configured wallpaper backend.
	_BACKEND_ must be _sweetbg_, _awww_, or _auto_. An explicitly named backend
	is used without probing; _auto_ probes when a wallpaper is applied. The
	configuration file is not changed.

*-c* _COLUMNS_, *--columns* _COLUMNS_
	Temporarily set the maximum number of grid columns. _COLUMNS_ must be a
	decimal integer from 1 to 1024. The grid may use fewer columns to fit the
	selected output. The configuration file is not changed.

*-d* _DIRECTORY_, *--directory* _DIRECTORY_
	Temporarily scan _DIRECTORY_ instead of the configured wallpaper directory.
	A leading _~_ expands to _$HOME_. The configuration file is not changed.

*--height* _HEIGHT_
	Temporarily set _thumbnail.height_. _HEIGHT_ must be a decimal integer from 1
	to 16384. The configuration file is not changed.

*-m* _MARGIN_, *--margin* _MARGIN_
	Temporarily set _window.margin_. _MARGIN_ must be a decimal integer from 0 to
	4096. The configuration file is not changed.

*-p* _POSITION_, *--position* _POSITION_
	Temporarily override the configured window position for this invocation.
	_POSITION_ must be _center_, _left_, _right_, _top_, or _bottom_. The
	configuration file is not changed.

*--radius* _RADIUS_
	Temporarily set _grid.radius_. _RADIUS_ must be a decimal integer from 0 to
	4096. The configuration file is not changed.

*--ring* _COLOR_
	Temporarily set _colors.ring_. _COLOR_ must be _"#rrggbb"_ or
	_"#rrggbbaa"_. Quote the value in a shell because it starts with _#_. The
	configuration file is not changed.

*-r* _ROWS_, *--rows* _ROWS_
	Temporarily set the _grid.visible_rows_ maximum. _ROWS_ must be a decimal
	integer from 1 to 1024. The grid may use fewer rows to fit the selected
	output. The configuration file is not changed.

*-s* _SPACING_, *--spacing* _SPACING_
	Temporarily set _grid.spacing_. _SPACING_ must be a decimal integer from 0
	to 4096. The configuration file is not changed.

*--tile* _COLOR_
	Temporarily set _colors.tile_. _COLOR_ must be _"#rrggbb"_ or
	_"#rrggbbaa"_. Quote the value in a shell because it starts with _#_. The
	configuration file is not changed.

*-w* _WIDTH_, *--width* _WIDTH_
	Temporarily set _thumbnail.width_. _WIDTH_ must be a decimal integer from 1
	to 16384. The configuration file is not changed.

*--preview*
	Enable the full-screen live preview for this invocation, overriding the
	configured _preview_ value.

*--no-preview*
	Disable the full-screen live preview for this invocation, overriding the
	configured _preview_ value.

*-h*, *--help*
	Show a help message and exit.

*-V*, *--version*
	Show version information and exit.

*--clear-cache*
	Remove cached thumbnails and exit without opening the picker. Wallpapers and
	configuration are not touched.

*--diagnose*
	Check the current Sweetwall setup and print a compact report without opening
	the picker. Exit with status 1 for a blocking problem, or 0 when only warnings
	remain.

# KEY BINDINGS

*Arrow keys*, *h* *j* *k* *l*
	Move the selection.

*Page Up*, *Page Down*
	Move the selection by one visible page.

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
the scroll wheel moves the selection through the grid. When the preview backdrop
is on, a left click outside the panel cancels (like *Escape*).

# BACKENDS

On *Enter*, the selected wallpaper is handed to a wallpaper daemon. The default
backend is *sweetbg*(1), invoked as _sweetbg img <path>_. The _backend_ config
key selects a backend by name or _"auto"_ to probe. The path is passed as an
argument, never through a shell. A backend that is unknown or fails is reported
and sweetwall exits non-zero without changing the wallpaper.

After a successful apply, the _hooks.on_apply_ commands run (for example a
colorscheme generator), spawned detached. See *sweetwall*(5).

# REMEMBERED SELECTION

After a wallpaper is applied successfully, sweetwall remembers its path. The
next launch selects it and scrolls it into view when it is still present in the
configured directory. Missing, moved, corrupt, and unreadable state is ignored.

The state is read only after the first placeholder frame is committed, keeping
persistent file I/O off the instant-open path.

# DIAGNOSTICS

*sweetwall --diagnose* reports the effective config, wallpaper-directory access,
Wayland reachability, backend clients and daemon detection, configured hook
executables, remembered selection, and cache, state, and log paths. It may connect
to the compositor, but does not create a surface, apply a wallpaper, run hooks, or
create persistent files. Hook arguments are not printed.

An *ERROR* is a problem that prevents normal use and makes the command exit with
status 1. A *WARN* identifies a non-blocking problem and does not change the
successful exit status. Invalid command-line usage exits with status 2.

# LOGGING

Sweetwall writes human-readable diagnostics to
_$XDG_STATE_HOME/sweetwall/sweetwall.log_, falling back to
_~/.local/state/sweetwall/sweetwall.log_. Warnings and errors are also written
to standard error. The log includes config warnings, wallpaper scan results,
output and grid geometry, thumbnail failures and summary counts, backend
detection and exit status, hook launch failures, remembered selection state,
and the exit reason.

Routine navigation and individual cache hits are not logged. Wallpaper and
config paths may appear in the log. Hook arguments are never recorded; only the
executable name is included.

Log files are capped at 256 KiB. The current file and two rotated files are kept:
_sweetwall.log_, _sweetwall.log.1_, and _sweetwall.log.2_. Logging starts after
the first frame so it adds no file I/O to normal startup. Diagnostics buffered
before that point are still persisted when startup fails.

# CONFIGURATION

Sweetwall reads _$XDG_CONFIG_HOME/sweetwall/config.toml_, falling back to
_~/.config/sweetwall/config.toml_. It runs with defaults when no file is
present. Command-line overrides take precedence over the loaded configuration.
When conflicting overrides are supplied, the last one takes effect.
See *sweetwall*(5) for the keys.

# FILES

_$XDG_CONFIG_HOME/sweetwall/config.toml_, _~/.config/sweetwall/config.toml_
	Per-user configuration. See *sweetwall*(5).

_$XDG_STATE_HOME/sweetwall/last-selection_, _~/.local/state/sweetwall/last-selection_
	Last successfully applied wallpaper.

_$XDG_STATE_HOME/sweetwall/sweetwall.log_, _~/.local/state/sweetwall/sweetwall.log_
	Current diagnostic log. The _.1_ and _.2_ suffixes are older rotations.

# SEE ALSO

*sweetwall*(5), *sweetbg*(1)
