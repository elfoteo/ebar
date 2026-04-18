# ebar

A beautiful, modular, and customizable Hyprland bar written in C with GTK3 and Layer Shell.

## Features

- **Modular Architecture**: Clean C codebase split into logical components.
- **INI Configuration**: Easily customized via `~/.config/ebar/ebar.conf`.
- **Layout Flexibility**: Custom widget placement across `left`, `center`, and `right` sections.
- **Visual Modes**:
    - **Normal**: Classic full-width bar.
    - **Floating**: Detached bar with margins and rounded corners.
    - **Island**: Each widget group is wrapped in its own "bubble" with a unique "melting" effect when anchored to the screen edge.
- **Custom Aesthetics**:
    - Accent color support for metric bars and highlights.
    - Full control over transparency, margins, padding, and spacing.
    - Nerd Font integration for premium iconography.
- **Dynamic Metrics**: Real-time polling for CPU, RAM, GPU, Disk, and Temperature.
- **Media Player**: Integrated support for `playerctl` with configurable metadata visibility.

## Requirements

### Libraries (build-time)
- `gtk3`
- `gtk-layer-shell`
- `pthread`, `math`, `cairo`

On Arch Linux:
```bash
sudo pacman -S gtk3 gtk-layer-shell
```

### Fonts (runtime)
- **JetBrains Mono Nerd Font** (or any Nerd Font set in configuration)

On Arch Linux:
```bash
sudo pacman -S ttf-jetbrains-mono-nerd
```

### CLI tools (runtime)
- `pactl`: Volume control.
- `playerctl`: Media metadata and controls.
- `nvidia-smi`: Optional, for GPU metrics.

## Installation and Usage

1. **Build**:
   ```bash
   make
   ```
2. **Launch**:
   ```bash
   ./build/ebar
   ```
   *Note: On first run, a default config will be generated at `~/.config/ebar/ebar.conf`.*

3. **Hyprland Integration**:
   Add this to your `hyprland.conf`:
   ```hyprlang
   exec-once = /path/to/ebar/launch.sh
   ```

## Configuration

The configuration file allows you to tweak every aspect of the bar. 

### How to Blur
If you want to add blur to the bar on Hyprland, add the following to your `hyprland.conf`:

```hyprlang
layerrule = blur on, ignore_alpha 0.01, match:namespace ebar
```
*Note: Make sure your `namespace` in the bar is set to `ebar` (default).*

### Example Layout
```ini
[bar]
position        = bottom          # top | bottom
mode            = island          # normal | floating | island
margin          = 8               # outer gap in px (used when floating)
border_radius   = 12              # corner radius px (floating / island)
padding_h       = 12              # horizontal inner padding px
padding_v       = 5               # vertical inner padding px
spacing         = 12              # spacing between widgets px

[colors]
# Use any valid CSS colour: #RRGGBB, rgba(r,g,b,a), etc.
background      = rgba(0,0,0,0.2)
accent          = #D35D6E
foreground      = #ffffff
dim_foreground  = rgba(255,255,255,0.6)

[font]
family          = JetBrainsMonoNerdFont
size            = 13

[workspaces]
count           = 10
icon_empty      = 
icon_occupied   = 
show_empty      = true

[left]
# Options: workspaces, clock, media, volume, metrics
widgets         = workspaces

[center]
widgets         = media

[right]
widgets         = metrics, volume, clock

[clock]
time_format     = %H:%M
date_format     = %d/%m/%Y

[media]
show_title      = true
show_artist     = true
background      = false
max_title_width = 400

[volume]
app             = pavucontrol
show_percent    = false

[metrics]
# Rows separated by ; columns by spaces. Options: ram cpu gpu disk temp gputemp
layout          = ram cpu ; disk temp
use_bars        = true
temp_path       = auto
```

## License
MIT
