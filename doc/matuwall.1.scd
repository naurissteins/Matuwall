matuwall(1)

# NAME

matuwall - Minimal wallpaper picker for Wayland

# SYNOPSIS

*matuwall* [options]

# DESCRIPTION

*matuwall* is a keyboard driven wallpaper picker for Wayland. Select a
thumbnail and press *Enter* to apply it.

By default it opens as a bottom-aligned carousel. *--preview* shows the selected
wallpaper behind the panel without applying it. Only one picker can be open per
Wayland display; a new invocation cancels and replaces the existing one.

# OPTIONS

Options override the loaded configuration for one invocation and do not change
the configuration file. When opposite options are given, the last one wins.
Colors use _"#rrggbb"_ or _"#rrggbbaa"_; quote them in the shell.

*--background* _COLOR_
	Set _window.background_.

*-b* _BACKEND_, *--backend* _BACKEND_
	Use _sweetbg_, _awww_, or _auto_. Named backends are not probed; _auto_
	probes when applying the wallpaper.

*--border* _COLOR_
	Set _colors.border_.

*--border-width* _WIDTH_
	Set _grid.border_width_ to an integer from 0 to 4096. Zero disables borders.

*--carousel*
	Enable the centered carousel.

*--shadow* _COLOR_
	Set _colors.shadow_.

*--shadow-width* _WIDTH_
	Set _grid.shadow_width_ to an integer from 0 to 4096. Zero disables shadows.

*-c* _COLUMNS_, *--columns* _COLUMNS_
	Set the maximum columns to an integer from 1 to 1024. Fewer may be used to
	fit the output.

*--close-on-focus-loss*
	Close when keyboard focus leaves Matuwall.

*--config* _PATH_
	Load _PATH_ instead of the standard configuration file. _~_ expands to
	_$HOME_; relative paths use the current directory. The file must exist and
	parse successfully.

*-d* _DIRECTORY_, *--directory* _DIRECTORY_
	Scan _DIRECTORY_ for wallpapers. _~_ expands to _$HOME_; relative paths use
	the current directory.

*--height* _HEIGHT_
	Set _thumbnail.height_ to an integer from 1 to 16384.

*--hook* _COMMAND_
	Replace the configured _hooks.on_apply_ list. Later occurrences append in
	command-line order. Up to 16 non-empty commands of up to 511 bytes are
	accepted.

*--edge* _EDGE_
	Set _grid.edge_ to *auto*, *clip*, *peek*, or *fade*.

*--edge-margin* _MARGIN_
	Set _window.edge_margin_ to an integer from 0 to 4096.

*-m* _MARGIN_, *--margin* _MARGIN_
	Set _window.margin_ to an integer from 0 to 4096.

*--no-config*
	Use built-in defaults without loading the standard configuration file.

*--no-close-on-focus-loss*
	Keep Matuwall open when keyboard focus leaves.

*--no-carousel*
	Disable the carousel and use the regular grid.

*--no-hooks*
	Clear the runtime hook list. A later *--hook* starts a new list.

*-o* _NAME_, *--output* _NAME_
	Open on the named Wayland output. An unknown name is an error; available
	outputs are reported.

*-p* _POSITION_, *--position* _POSITION_
	Use _center_, _left_, _right_, _top_, or _bottom_.

*--panel-radius* _RADIUS_
	Set _window.radius_ to an integer from 0 to 4096.

*--print-config*
	Print the resolved configuration as TOML and exit without opening the picker.

*--radius* _RADIUS_
	Set _grid.radius_ to an integer from 0 to 4096.

*--ring* _COLOR_
	Set _colors.ring_.

*--ring-width* _WIDTH_
	Set _grid.ring_width_ to an integer from 0 to 4096. Zero disables the ring.

*-r* _ROWS_, *--rows* _ROWS_
	Set the maximum visible rows to an integer from 1 to 1024. Fewer may be used
	to fit the output.

*-s* _SPACING_, *--spacing* _SPACING_
	Set _grid.spacing_ to an integer from 0 to 4096.

*--spinner* _COLOR_
	Set _colors.spinner_.

*--tile* _COLOR_
	Set _colors.tile_.

*-w* _WIDTH_, *--width* _WIDTH_
	Set _thumbnail.width_ to an integer from 1 to 16384.

*--preview*
	Enable the full-screen live preview.

*--no-preview*
	Disable the full-screen live preview.

*-h*, *--help*
	Show help and exit.

*-V*, *--version*
	Show version information and exit.

*--clear-cache*
	Remove cached thumbnails and exit. Wallpapers and configuration are not
	changed.

*--diagnose*
	Check the current setup and exit. Blocking problems return status 1; warnings
	alone return status 0.

# KEY BINDINGS

*Arrow keys*, *h* *j* *k* *l*
	Move the selection. A horizontal carousel uses Left/Right; a vertical one
	uses Up/Down. Carousel movement wraps.

*Page Up*, *Page Down*
	Move by one visible page. Disabled in carousel mode.

*Home*, *g*
	Jump to the first tile.

*End*, *G*
	Jump to the last tile.

*Enter*
	Apply the selected wallpaper and exit.

*Escape*
	Cancel and exit.

# MOUSE

Set _input.mouse_ to _true_ to enable mouse input. Hover selects a tile, the
scroll wheel moves the selection, and a left click applies. With preview on,
clicking outside the panel cancels.

# BACKENDS

The default backend is *awww*. Set _general.backend_ to _sweetbg_ or _auto_ to
change it. Backend commands receive the path directly, never through a shell.
Failed applies leave the wallpaper unchanged and exit non-zero.

After a successful apply, _hooks.on_apply_ commands are started without waiting
for them. See *matuwall*(5).

# REMEMBERED SELECTION

After a successful apply, Matuwall remembers the path and selects it on the next
launch if it is still in the wallpaper directory.

# DIAGNOSTICS

*matuwall --diagnose* checks configuration, the wallpaper directory, Wayland,
backends, hooks, remembered selection, and data paths without opening the picker
or changing state. Invalid command-line usage exits with status 2.

If the wallpaper directory cannot be opened, the picker shows a warning glyph
and exits with status 1 when dismissed. The error is also written to standard
error and the log.

# LOGGING

Diagnostics are written to _$XDG_STATE_HOME/matuwall/matuwall.log_, or
_~/.local/state/matuwall/matuwall.log_. The log is limited to 256 KiB and keeps
two rotated copies. Paths may appear in it, but hook arguments are not logged.

# CONFIGURATION

Matuwall reads _$XDG_CONFIG_HOME/matuwall/config.toml_, or
_~/.config/matuwall/config.toml_. A missing file uses built-in defaults. See
*matuwall*(5).

# FILES

_$XDG_CONFIG_HOME/matuwall/config.toml_, _~/.config/matuwall/config.toml_
	Per-user configuration.

_$XDG_STATE_HOME/matuwall/last-selection_, _~/.local/state/matuwall/last-selection_
	Last successfully applied wallpaper.

_$XDG_STATE_HOME/matuwall/matuwall.log_, _~/.local/state/matuwall/matuwall.log_
	Diagnostic log.

# SEE ALSO

*matuwall*(5), *sweetbg*(1)
