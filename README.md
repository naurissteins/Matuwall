# sweetwall

A minimal, instant-open wallpaper picker for Wayland.

Sweetwall opens before decoding thumbnails. Pick a wallpaper, apply it, and the
picker exits. It uses `wlr-layer-shell` and software rendering—no GTK, Qt, or GPU
context.

## Build

```sh
meson setup build
ninja -C build
./build/sweetwall
```

## Run

```sh
sweetwall                              # use the config or built-in defaults
sweetwall -d ~/Pictures/Photography   # use another directory
sweetwall -b awww                     # use awww once
sweetwall --backend auto              # detect a running backend
sweetwall -c 4                        # use at most four columns
sweetwall -r 3                        # use at most three visible rows
sweetwall -s 12                       # set tile spacing to 12
sweetwall -m 24                       # set the window margin to 24
sweetwall -p left                     # move the panel
sweetwall --no-preview                # open without the backdrop
```

Run `sweetwall --help` for every option. Command-line options do not change the
config file. `--columns` and `--rows` accept decimal values from 1 to 1024;
smaller outputs may still use fewer columns or rows. `--spacing` accepts decimal
values from 0 to 4096, as does `--margin`.

## Controls

| Key | Action |
| --- | --- |
| Arrows, `h` `j` `k` `l` | Move |
| `Page Up`, `Page Down` | Move one page |
| `Home`, `g` | First wallpaper |
| `End`, `G` | Last wallpaper |
| `Enter` | Apply and exit |
| `Escape` | Cancel and exit |

Mouse: hover to select, click to apply, scroll to move. Click outside the panel
to cancel when preview is enabled.

## Configure

Config: `$XDG_CONFIG_HOME/sweetwall/config.toml`, or
`~/.config/sweetwall/config.toml`. The file is optional.

```toml
[general]
directory = "~/Pictures/Wallpapers"
backend = "sweetbg"              # sweetbg | awww | auto

[window]
preview = true
position = "center"              # center | left | right | top | bottom
background = "#1e1e2ecc"         # #rrggbb | #rrggbbaa
margin = 24

[grid]
columns = 5                       # maximum
visible_rows = 2                  # maximum
spacing = 16
radius = 8

[thumbnail]
width = 240
height = 400

[colors]
tile = "#313244ff"
ring = "#f2cdcdff"
spinner = "#cdd0e6ff"

[hooks]
on_apply = [
  "matugen image {path}",
  "wal -i {path} -n",
]
```

See [`config/example.toml`](config/example.toml) for comments and `sweetwall(5)`
for the full reference.

Bad values produce a warning and use the default. Syntax errors include a line
number. Unknown keys or sections reject the file.

`columns` and `visible_rows` are maximums. Smaller outputs automatically use
fewer tiles.

## Preview

Preview shows the selection behind the grid without applying it. `Enter`
applies it. `Escape` exits and leaves the current wallpaper unchanged.

```toml
[window]
preview = false   # panel only, no full-screen backdrop
```

The backdrop uses about 15 MB at 2560×1440 or 33 MB at 3840×2160.

## Backends

| Value | Command |
| --- | --- |
| `sweetbg` | `sweetbg img <path>` |
| `awww` | `awww img -- <path>` |
| `auto` | First running backend: sweetbg, then awww |

`auto` requires both the client on `PATH` and a live daemon socket. An explicit
backend skips detection. For a named awww namespace, use `backend = "awww"`.

Wallpaper paths are passed as arguments, never through a shell.

## Hooks

Hooks run after a successful apply. `{path}` becomes the absolute wallpaper
path.

```toml
[hooks]
on_apply = ["matugen image {path}"]
```

Hooks run detached and never through a shell. Pipes, globs, variables, and
shell quoting are not supported.

## Maintenance

```sh
sweetwall --diagnose      # check config, Wayland, backends, hooks, and paths
sweetwall --clear-cache   # remove thumbnails only
```

| Data | Default path |
| --- | --- |
| Last selection | `~/.local/state/sweetwall/last-selection` |
| Logs | `~/.local/state/sweetwall/sweetwall.log` |
| Thumbnails | `~/.cache/sweetwall/thumbs/` |

`XDG_STATE_HOME` and `XDG_CACHE_HOME` override these base directories. Logs are
limited to 256 KiB each; the current log and two rotations are kept.

## Acknowledgments

- [sweetbg](https://github.com/sweetwm/sweetbg) — first backend and reference for
  the Wayland, TOML, and quality-check structure
- [awww](https://codeberg.org/LGFae/awww) — second backend and socket reference
