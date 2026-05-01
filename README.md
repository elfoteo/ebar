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
- **Volume Widget**: Circular progress ring around the volume icon; scroll anywhere on it to adjust level. Smooth-scroll (trackpad) supported.
- **Nightlight Widget**: Toggle-based night light control via `hyprsunset`; scroll to adjust intensity along a configurable curve.

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

### Optional (nightlight widget)
- **`hyprsunset`**: Must be installed and running. Add to your `hyprland.conf`:
  ```hyprlang
  exec-once = hyprsunset
  ```
  ebar communicates with it directly via the hyprsunset IPC socket — no `hyprctl` process is spawned.
  If the socket is unreachable the nightlight icon turns **red** as a visual cue.

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
ring_color      = rgba(255,255,255,0.9) # colour of circular progress rings (volume + nightlight)

[font]
family          = JetBrainsMonoNerdFont
size            = 13

[workspaces]
count           = 10
icon_empty      = 
icon_occupied   = 
show_empty      = true

[left]
# Options: workspaces, clock, media, volume, metrics, nightlight
widgets         = workspaces

[center]
widgets         = media

[right]
widgets         = metrics, volume, nightlight, clock

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

[nightlight]
# Requires hyprsunset running (exec-once = hyprsunset in hyprland.conf)
temp_max        = 6500   # white-point temperature (K) — identity value when off
temp_min        = 5400   # warm night temperature (K) — applied at level 100
gamma_max       = 100    # full brightness gamma
gamma_min       = 75     # reduced gamma at maximum nightlight level
step            = 5      # level change per scroll tick (range 0–100)
curve           = ease   # ease (smoothstep) | linear
```

### Nightlight Widget

| State | Icon | Ring |
|---|---|---|
| Off | `` (sun, dim) | Hidden |
| On | `` (moon, full brightness) | Visible — reflects current level |
| IPC error | `` (sun, **red**) | — |

**Interaction:**
- **Left-click**: Toggle on/off. First toggle starts at **15%** of the curve. Turning off resets hyprsunset to identity (`temperature 6500`, `gamma 100`).
- **Scroll**: Adjusts the nightlight *level* (0–100) by `step` per tick. Both temperature and gamma are derived from the same level value via the curve, so a single scroll adjusts both simultaneously. Smooth-scroll (trackpad) is supported.

**Curve functions** (`curve = ease` recommended):

| Mode | Behaviour |
|---|---|
| `ease` | Smoothstep — subtle at low levels, stronger near the top |
| `linear` | Straight-line interpolation between min and max |

## License
MIT
