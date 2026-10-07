<h1 align=center>Matuwall</h1>

<div align=center>

![GitHub last commit](https://img.shields.io/github/last-commit/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=a6e3a1)
![GitHub repo size](https://img.shields.io/github/repo-size/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=d3bfe6)
![AUR Version](https://img.shields.io/aur/version/matuwall?style=for-the-badge&labelColor=181825&color=b4befe)
![GitHub Repo stars](https://img.shields.io/github/stars/naurissteins/Matuwall?style=for-the-badge&labelColor=181825&color=f9e2af)

Simple, fast and lightweight wallpaper picker for Wayland

[< Wiki >](https://github.com/naurissteins/Matuwall/wiki)


</div>

https://github.com/user-attachments/assets/be785321-e27e-48fd-b33d-14a18dd0fb1f


> [!IMPORTANT]
> Matuwall is a C rewrite of my original GTK/Python application.

---

## 🔥 Features
- Nativelly supports [sweetbg](https://github.com/sweetwm/sweetbg), [awww](https://codeberg.org/LGFae/awww) (default) and KDE Plasma
- Custom backend command for any other setter (hyprpaper, swaybg, ...)
- Fast raw thumbnail cache
- Grid and carousel layouts
- Fractional scaling and multiple outputs
- Full-screen wallpaper previews
- Supports `Matugen` and `Pywal`

Btw, Matuwall is just a picker. You need a running `awww` or `sweetbg` wallpaper daemon to apply a wallpaper.
User [sweetbg](https://github.com/sweetwm/sweetbg) if you want something minimal, use [awww](https://codeberg.org/LGFae/awww)
to have transition animation

## Install

### Arch Linux

On Arch Linux, install Matuwall from the AUR:

```bash
# prebuilt release package
yay -S matuwall-bin
```

### NixOS

Add the flake input and overlay, then install `pkgs.matuwall`:

```nix
# flake.nix
inputs.matuwall = {
  url = "github:naurissteins/Matuwall";
  inputs.nixpkgs.follows = "nixpkgs";
};

# in your nixosSystem modules
{ nixpkgs.overlays = [ matuwall.overlays.default ]; }

# then, e.g. in home.packages or environment.systemPackages
pkgs.matuwall
```

Or try it without installing: `nix run github:naurissteins/Matuwall`

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
matuwall -b plasma                               // KDE Plasma
matuwall --backend auto                          // detect a running backend
matuwall --backend-arg --persist                 // replace backend flags for one run
matuwall --no-backend-args                       // apply without configured flags
matuwall --backend-command "swww img {path}"     // use your own setter once
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
matuwall --background none                       // draw no panel background
matuwall --tile "#313244"                        // set the tile color
matuwall --border "#585b70"                      // set the thumbnail border color
matuwall --border-width 1                        // set the thumbnail border width
matuwall --shadow "#00000066"                    // set the thumbnail shadow color
matuwall --shadow-width 10                       // set the shadow fade distance
matuwall --ring "#f2cdcd"                        // set the selection ring color
matuwall --ring-width 0                          // disable the selection ring
matuwall --spinner "#cdd0e6"                     // set the loading spinner color
matuwall --navigation-ms 0                       // disable the navigation transition
matuwall --zoom-percent 5                        // grow the selected tile by 5%
matuwall -o DP-1                                 // open explicitly on DP-1
matuwall -p left                                 // move the panel
matuwall --preview                               // enable the full-screen backdrop
matuwall --close-on-focus-loss                   // close when focus moves away
matuwall --no-hooks                              // apply without running hooks
matuwall --hook 'matugen image {path}'           // replace hooks for one run
```

### Examples

```bash
# Examples from a video above
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 5 -w 280 --height 150 -p bottom --background none
matuwall -d ~/Pictures/Wallpapers --preview --no-carousel -b sweetbg -c 3 -r 3 -w 380 --height 250 -p center --background none
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 3 -w 380 --height 250 -p left
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 3 -w 380 --height 250 -p right --background none
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 5 -w 280 --height 150 -p top
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 5 -w 380 --height 450 -p center --background none
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 2 -w 980 --height 520 -p center --background none
matuwall -d ~/Pictures/Wallpapers --preview --carousel -b sweetbg -c 4 -w 380 --height 520 -p center --background none --radius 0 --shadow-width 0 --border "#ffffff" --border-width 4 --panel-radius 0
```

See full [CLI reference](https://github.com/naurissteins/Matuwall/wiki/CLI-Reference)

Run `matuwall --help` for every option

> [!IMPORTANT]
> Command-line options do not change the config file

## Configure

If you don't want to use CLI commands, you can simply configure matuwall via a config file: `~/.config/matuwall/config.toml` or `$XDG_CONFIG_HOME/matuwall/config.toml`
You can also use both CLI and config file together. Set fundamental configs via config file and overrides via CLI

See example config [`config/example.toml`](config/example.toml)

---

## Wallpaper daemons

The built-in default is `awww`. Set `backend = "sweetbg"` or use `-b sweetbg`
to select sweetbg instead. On KDE Plasma, use `backend = "plasma"`

Pass extra flags to a backend with `args`. Each item is one argument, placed
before the wallpaper path

```toml
[backend.sweetbg]
args = ["--persist"]            # keep the wallpaper after a sweetbg restart

[backend.awww]
args = ["--transition-type", "grow", "--transition-duration", "1.5"]

[backend.plasma]
args = ["--fill-mode", "preserveAspectCrop"]
```

CLI override example using `--backend-arg` flags

```sh
matuwall -b awww --backend-arg --transition-type --backend-arg grow --backend-arg --transition-duration --backend-arg 2.5
```

### Custom command

Use any other wallpaper setter with `backend = "command"`. `{path}` becomes the
absolute wallpaper path

```toml
[general]
backend = "command"

[backend.command]
apply = "swww img {path}"
# apply = "hyprctl hyprpaper wallpaper ,{path}"
```

For one run, use `--backend-command "swww img {path}"`. It selects the command
backend unless `-b` names another one

Tools that keep running, like `swaybg` need a small wrapper script. See the
[wiki](https://github.com/naurissteins/Matuwall/wiki/Backends#custom-command)

---

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

---

## Maintenance

```sh
matuwall --diagnose      # check config, Wayland, backends, hooks and paths
matuwall --clear-cache   # remove thumbnails only
```

Last selection `~/.local/state/matuwall/last-selection`, logs `~/.local/state/matuwall/matuwall.log`, and cached thumbnails `~/.cache/matuwall/thumbs/`

Logs are limited to 256 KiB each, the current log and two rotations are kept
