<h1 align=center>Matuwall</h1>

<div align=center>

![GitHub last commit](https://img.shields.io/github/last-commit/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=a6e3a1)
![GitHub repo size](https://img.shields.io/github/repo-size/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=d3bfe6)
![AUR Version](https://img.shields.io/aur/version/matuwall?style=for-the-badge&labelColor=181825&color=b4befe)
![GitHub Repo stars](https://img.shields.io/github/stars/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=f9e2af)

Simple, fast and lightweight wallpaper picker for Wayland

</div>


> [!IMPORTANT]
> Matuwall is a C rewrite of my original GTK/Python application

---

## 🔥 Features
- Nativelly supports `sweetbg` and `awww` daemons
- Opens before image decoding starts
- Loads thumbnails asynchronously
- Uses a fast raw thumbnail cache
- Supports regular grid and carousel layouts
- Handles fractional scaling and multiple outputs
- Full-screen wallpaper previews without applying
- Hooks for tools such as `Matugen` and `Pywal`

## Install

### Arch Linux

On Arch Linux, install Matuwall from the AUR:

```bash
# prebuilt release package (recommended)
yay -S matuwall-bin

# or latest git build
yay -S matuwall-git
```

## Build from source
```sh
meson setup build --buildtype=release
ninja -C build
sudo ninja -C build install
```

Check the [installation wiki](https://github.com/naurissteins/Matuwall/wiki/Installation) for more information

## CLI Commands

```kdl
matuwall                                         // use the config or built-in defaults
matuwall --config ~/.config/matuwall/work.toml   // use another config once
matuwall --no-config -d ~/Pictures/Wallpapers    // defaults plus CLI overrides
matuwall --print-config                          // show the effective config and exit
matuwall -d ~/Pictures/Photography               // use another directory
matuwall -b awww                                 // use awww once
matuwall --backend auto                          // detect a running backend
matuwall --backend-arg --persist                 // replace backend flags for one run
matuwall --no-backend-args                       // apply without configured flags
matuwall --carousel                              // enable the centered carousel
matuwall --no-carousel                           // use the regular grid
matuwall --edge fade                             // fade tiles at carousel edges
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

### Few examples

```bash
# Preview with carousel, bottom position
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b auto -c 5 -w 280 --height 150 -p bottom --background "#181825cc"    

# Preview with carousel, left position
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b auto -c 3 -w 480 --height 250 -p left  --background "#181825cc"  

# Preview with no carousel, center position
matuwall -d ~/Pictures/Wallpapers --preview --no-carousel -b auto -c 3 -r 5 -w 280 --height 150 -p center  --background "#181825cc"
```

See full [CLI reference](https://github.com/naurissteins/Matuwall/wiki/CLI-Reference)

Run `matuwall --help` for every option

> [!IMPORTANT]
> Command-line options do not change the config file

## Configure

If you don't want to use CLI commands, you can simply configure matuwall via a config file: `~/.config/matuwall/config.toml` or `$XDG_CONFIG_HOME/matuwall/config.toml`
You can also use both CLI and config file together. Set fundamental configs via config file and overrides via CLI

See example config [`config/example.toml`](config/example.toml)

## Wallpaper daemons

The built-in default is `awww`. Set `backend = "sweetbg"` or use `-b sweetbg`
to select sweetbg instead.

Pass extra flags to a backend with `args`. Each item is one argument, placed
before the wallpaper path

```toml
[backend.sweetbg]
args = ["--persist"]            # keep the wallpaper after a sweetbg restart

[backend.awww]
args = ["--transition-type", "grow", "--transition-duration", "1.5"]
```

CLI override example using `--backend-arg` flags

```sh
matuwall -b awww --backend-arg --transition-type --backend-arg grow --backend-arg --transition-duration --backend-arg 2.5
```

## Hooks

Hooks run after a successful apply. `{path}` becomes the absolute wallpaper
path, with symlinks resolved

```toml
[hooks]
on_apply = ["matugen image {path} --source-color-index 1"]
```

Hooks run detached and never through a shell. Pipes, globs, variables and
shell quoting are not supported

```sh
matuwall --hook 'matugen image {path} --source-color-index 1' --hook 'wal -i {path} -n'
```

## Maintenance

```sh
matuwall --diagnose      # check config, Wayland, backends, hooks and paths
matuwall --clear-cache   # remove thumbnails only
```

Last selection `~/.local/state/matuwall/last-selection`, logs `~/.local/state/matuwall/matuwall.log`, and cached thumbnails `~/.cache/matuwall/thumbs/`

Logs are limited to 256 KiB each, the current log and two rotations are kept
