<h1 align=center>sweetwall</h1>

<div align=center>

![GitHub last commit](https://img.shields.io/github/last-commit/naurissteins/sweetwall?style=for-the-badge&labelColor=181825&color=a6e3a1)
![GitHub repo size](https://img.shields.io/github/repo-size/naurissteins/sweetwall?style=for-the-badge&labelColor=181825&color=d3bfe6)
![AUR Version](https://img.shields.io/aur/version/sweetwall-bin?style=for-the-badge&labelColor=181825&color=b4befe)
![GitHub Repo stars](https://img.shields.io/github/stars/naurissteins/sweetwall?style=for-the-badge&labelColor=181825&color=f9e2af)

Simple, fast and lightweight wallpaper picker for Wayland

</div>


> [!IMPORTANT]
> sweetwall

---

## 🔥 Features

- feature 1
- feature 1

## Install

### Arch Linux

On Arch Linux, install sweetwall from the AUR:

```bash
# prebuilt release package (recommended)
yay -S sweetwall-bin

# or latest git build
yay -S sweetwall-git

## Build from source

```sh
meson setup build --buildtype=release
ninja -C build
sudo ninja -C build install
```

Requires a C11 compiler, Meson, and Ninja. `scdoc` is optional and only needed
to build the man page.

## Run

```sh
sweetwall                             # use the config or built-in defaults
sweetwall --config ~/.config/sweetwall/work.toml # use another config once
sweetwall --no-config -d ~/Pictures/Wallpapers # defaults plus CLI overrides
sweetwall -d ~/Pictures/Photography   # use another directory
sweetwall -b awww                     # use awww once
sweetwall --backend auto              # detect a running backend
sweetwall -c 4                        # use at most four columns
sweetwall -r 3                        # use at most three visible rows
sweetwall -s 12                       # set tile spacing to 12
sweetwall -m 24                       # set the window margin to 24
sweetwall --radius 10                 # set the tile corner radius to 10
sweetwall -w 320                      # set thumbnail width to 320
sweetwall --height 480                # set thumbnail height to 480
sweetwall --background "#181825cc"    # set the panel background color
sweetwall --tile "#313244"            # set the tile color
sweetwall --ring "#f2cdcd"            # set the selection ring color
sweetwall --spinner "#cdd0e6"         # set the loading spinner color
sweetwall -p left                     # move the panel
sweetwall --no-preview                # open without the backdrop
sweetwall --no-hooks                  # apply without running hooks
sweetwall --hook 'matugen image {path}' # replace hooks for one run
```

Run `sweetwall --help` for every option. Command-line options do not change the
config file. `--width` accepts decimal values from 1 to 16384.
The same range applies to `--height`.
`--background`, `--tile`, `--ring`, and `--spinner` accept `#rrggbb` or
`#rrggbbaa` colors.

`--config PATH` loads an alternate configuration file for one invocation. A
leading `~` expands to `$HOME`, relative paths use the current directory, and
other command-line overrides are applied afterward. An explicitly selected
file must exist and parse successfully.

`--no-config` skips the standard configuration file and starts from built-in
defaults before applying other CLI overrides. `--config` and `--no-config` are
processed left to right, so the last one selects the configuration source.

## Controls

| Key | Action |
| --- | --- |
| Arrows, `h` `j` `k` `l` | Move |
| `Page Up`, `Page Down` | Move one page |
| `Home`, `g` | First wallpaper |
| `End`, `G` | Last wallpaper |
| `Enter` | Apply and exit |
| `Escape` | Cancel and exit |

Mouse users: hover to select, click to apply, scroll to move. Click outside the panel
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

Use `sweetwall --no-hooks` to skip configured hooks for one invocation.
Repeat `--hook COMMAND` to replace them temporarily with one or more commands:

```sh
sweetwall --hook 'matugen image {path}' --hook 'wal -i {path} -n'
```

The first `--hook` replaces the configured list and later occurrences append.
Hook options are processed left to right, so `--no-hooks` clears any `--hook`
options before it while a later `--hook` starts a new list.

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
