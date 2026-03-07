# ebar
Simple hyprland bar written in C with minimal dependencies

## Build
 - Clone the repository
 - Run `make`
 - The binary will be created under `build/bar`

## Requirements
 - `gtk-layer-shell`, install using `sudo pacman -S gtk-layer-shell` on Arch Linux

## Hyprland
Add to the `hyprland.conf` file a line to auto start the bar:
`exec = /path/to/ebar/launch.sh`
**Change the path to your cloned repo path**
