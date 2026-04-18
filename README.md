# ebar
Simple Hyprland bar written in C with minimal dependencies

## Build
 - Clone the repository: `git clone https://github.com/elfoteo/ebar.git`
 - Run `make`
 - The binary will be created under `build/ebar`

## Requirements

### Libraries (build-time)
 - `gtk3` — GTK 3 toolkit
 - `gtk-layer-shell` — Wayland layer-shell support for GTK windows

On Arch Linux:
```
sudo pacman -S gtk3 gtk-layer-shell
```

### Fonts (runtime)
 - `JetBrains Mono Nerd Font` — used for icons and text in the bar

On Arch Linux:
```
sudo pacman -S ttf-jetbrains-mono-nerd
```

### CLI tools (runtime)
 - `pactl` — volume queries and control (part of `pipewire-pulse` or `pulseaudio`)
 - `playerctl` — media player control and metadata follow mode
 - `hyprctl` — Hyprland workspace/client state (bundled with Hyprland, no separate install needed)

### Optional CLI tools
 - `pavucontrol` — GUI volume mixer, opened on left-click of the volume button (configurable via `VOLUME_APP`)
 - `nvidia-smi` — required **only** if `M_GPU` or `M_GPU_TEMP` are present in `bar_config`; skipped automatically otherwise

## Hyprland
Add to the `hyprland.conf` file a line to auto start the bar:
```
exec = /path/to/ebar/launch.sh
```
**Change the path to your cloned repo path.**
