<h1 align=center>Matuwall</h1>

<div align=center>

![GitHub last commit](https://img.shields.io/github/last-commit/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=a6e3a1)
![GitHub repo size](https://img.shields.io/github/repo-size/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=d3bfe6)
![AUR Version](https://img.shields.io/aur/version/matuwall?style=for-the-badge&labelColor=181825&color=b4befe)
![GitHub Repo stars](https://img.shields.io/github/stars/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=f9e2af)

Simple, fast and lightweight wallpaper picker for Wayland

</div>


> [!IMPORTANT]
> Matuwall is a C rewrite of the original GTK and Python application

---

## 🔥 Features

- Asynchronous JPEG, PNG, and WebP thumbnails with a fast on-disk cache
- Full-screen wallpaper previews without applying the selection
- Configurable grid layout, colors, thumbnail edge shadows, placement, output and fractional scaling
- Optional centered carousel layout for horizontal or vertical scrolling
- Native `sweetbg` and `awww` backends with automatic detection
- Keyboard-only by default, with optional mouse controls
- Safe, detached post-apply hooks for tools such as Matugen and Pywal

## Install

### Arch Linux

On Arch Linux, install Matuwall from the AUR:

```bash
yay -S matuwall
```

## Build from source
```sh
meson setup build --buildtype=release
ninja -C build
sudo ninja -C build install
```

Build from source requires a C11 compiler, Meson and Ninja. `scdoc` is optional and only needed
to build the man page

## Run

```kdl
matuwall                                         // use the config or built-in defaults
matuwall --config ~/.config/matuwall/work.toml   // use another config once
matuwall --no-config -d ~/Pictures/Wallpapers    // defaults plus CLI overrides
matuwall --print-config                          // show the effective config and exit
matuwall -d ~/Pictures/Photography               // use another directory
matuwall -b awww                                 // use awww once
matuwall --backend auto                          // detect a running backend
matuwall --carousel                              // enable the centered carousel
matuwall --no-carousel                           // use the regular grid
matuwall -c 4                                    // use at most four columns
matuwall -r 3                                    // use at most three visible rows
matuwall -s 12                                   // set tile spacing to 12
matuwall -m 24                                   // set panel padding to 24
matuwall --edge-margin 32                        // set the monitor edge gap to 32
matuwall --radius 10                             // set the tile corner radius to 10
matuwall --panel-radius 16                       // set the panel corner radius to 16
matuwall -w 320                                  // set thumbnail width to 320
matuwall --height 480                            // set thumbnail height to 480
matuwall --background "#181825cc"                // set the panel background color
matuwall --tile "#313244"                        // set the tile color
matuwall --border "#585b70"                      // set the thumbnail border color
matuwall --border-width 1                        // set the thumbnail border width
matuwall --shadow "#00000066"                    // set the thumbnail shadow color
matuwall --shadow-width 10                       // set the shadow fade distance
matuwall --ring "#f2cdcd"                        // set the selection ring color
matuwall --ring-width 0                          // disable the selection ring
matuwall --spinner "#cdd0e6"                     // set the loading spinner color
matuwall -o DP-1                                 // open explicitly on DP-1
matuwall -p left                                 // move the panel
matuwall --preview                               // enable the full-screen backdrop
matuwall --close-on-focus-loss                   // close when focus moves away
matuwall --no-hooks                              // apply without running hooks
matuwall --hook 'matugen image {path}'           // replace hooks for one run
```

Run `matuwall --help` for every option.

> [!IMPORTANT]
> Command-line options do not change the config file

Navigation transition:

```toml
[animation]
navigation_ms = 410
zoom_percent = 10
```

Centered carousel:

```toml
[grid]
carousel = true
edge_peek = false
```

At `top`, `bottom`, or `center`, the strip is horizontal and uses only
Left/Right. At `left` or `right`, it is vertical and uses only Up/Down. The
selected thumbnail stays centered while the strip moves. The carousel is
circular: moving past the last wallpaper continues at the first, and repeated
items fill the strip when necessary. `columns` limits the number of visible
thumbnails; `visible_rows` is ignored in this mode.
Set `edge_peek = true` to let neighboring thumbnails show in the panel margin.
It is disabled by default in both carousel and grid layouts.

## Controls
- Arrows or `h` `j` `k` `l`
- `Page Up`, `Page Down` | Move one page |
- `Home`, `g` | First wallpaper |
- `End`, `G` | Last wallpaper |
- `Enter` | Apply and exit |
- `Escape` | Cancel and exit |

In carousel mode, orthogonal arrows and page movement are disabled.

## Configure

Config file is optional. It is read from `~/.config/matuwall/config.toml` or `$XDG_CONFIG_HOME/matuwall/config.toml`.

See [`config/example.toml`](config/example.toml) for comments and `matuwall(5)`
for the full reference.

If something is not working as expected, run `matuwall --diagnose` to see the
diagnostic log and any error messages.

## Backends

The built-in default is `awww`. Set `backend = "sweetbg"` or use `-b sweetbg`
to select sweetbg instead.

| Value | Command |
| --- | --- |
| `sweetbg` | `sweetbg img <path>` |
| `awww` | `awww img -- <path>` |
| `auto` | First running backend: sweetbg, then awww |

Wallpaper paths are passed as arguments, never through a shell.

## Hooks

Hooks run after a successful apply. `{path}` becomes the absolute wallpaper
path

```toml
[hooks]
on_apply = ["matugen image {path}"]
```

Hooks run detached and never through a shell. Pipes, globs, variables, and
shell quoting are not supported

```sh
matuwall --hook 'matugen image {path} --source-color-index 1' --hook 'wal -i {path} -n'
```

The first `--hook` replaces the configured list and later occurrences append.
Hook options are processed left to right, so `--no-hooks` clears any `--hook`
options before it while a later `--hook` starts a new list.

## Maintenance

```sh
matuwall --diagnose      # check config, Wayland, backends, hooks and paths
matuwall --clear-cache   # remove thumbnails only
```

| Data | Default path |
| --- | --- |
| Last selection | `~/.local/state/matuwall/last-selection` |
| Logs | `~/.local/state/matuwall/matuwall.log` |
| Thumbnails | `~/.cache/matuwall/thumbs/` |

Logs are limited to 256 KiB each, the current log and two rotations are kept
