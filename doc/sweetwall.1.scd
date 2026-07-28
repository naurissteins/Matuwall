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

# CONFIGURATION

Sweetwall reads _$XDG_CONFIG_HOME/sweetwall/config.toml_, falling back to
_~/.config/sweetwall/config.toml_. It runs with defaults when no file is
present. See *sweetwall*(5) for the keys.

# FILES

_$XDG_CONFIG_HOME/sweetwall/config.toml_, _~/.config/sweetwall/config.toml_
	Per-user configuration. See *sweetwall*(5).

# SEE ALSO

*sweetwall*(5)
