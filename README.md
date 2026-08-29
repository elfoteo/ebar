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
    - **ChromeOS**: A prototype shelf bar mimicking ChromeOS aesthetics closely.
- **Custom Aesthetics**:
    - Accent color support for metric bars and highlights.
    - Full control over transparency, margins, padding, and spacing.
    - Nerd Font integration for premium iconography.
- **Dynamic Metrics**: Real-time polling for CPU, RAM, GPU, Disk, and Temperature.
- **Media Player**: Integrated support for `playerctl` with configurable metadata visibility.
- **Volume Widget**: Circular progress ring around the volume icon; scroll anywhere on it to adjust level. Smooth-scroll (trackpad) supported.
- **Nightlight Widget**: Toggle-based night light control via `hyprsunset`; scroll to adjust intensity along a configurable curve.
- **Bluetooth Widget**: Toggle bluetooth on/off with a click; ring glows accent color when enabled.

## Requirements

### Libraries (build-time)
- `gtk3`
- `gtk-layer-shell`
- `upower-glib`
- `librsvg`
- `pthread`, `math`, `cairo`

On Arch Linux:
```bash
sudo pacman -S gtk3 gtk-layer-shell upower librsvg
```

### Fonts (runtime)
- **Noto Sans** (recommended for ChromeOS mode)
- **JetBrains Mono Nerd Font** (required for iconography)

On Arch Linux:
```bash
sudo pacman -S noto-fonts ttf-jetbrains-mono-nerd
```

### CLI tools (runtime)
- `pactl`: Volume control.
- `playerctl`: Media metadata and controls.
- `nvidia-smi`: Optional, for GPU metrics.
- BlueZ/`bluetoothd`: Optional, for Bluetooth quick settings.

### LED permissions (ChromeOS mode)
The quick-settings LEDs menu reads and writes to sysfs files under `/sys/class/leds/`.
By default only root can write to `brightness` and `multi_intensity`. To allow your
user to control LEDs without sudo, create a udev rule:

```bash
sudo cp contrib/90-ebar-leds.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The rule grants the `video` group write access to `brightness` and `multi_intensity`.
Make sure your user is in the `video` group:
```bash
sudo usermod -aG video $USER
```
Then log out and back in for the group change to take effect.

### Optional (nightlight widget)
- **`hyprsunset`**: Must be installed and running. Add to your `hyprland.conf`:
  ```hyprlang
  exec-once = hyprsunset
  ```
  ebar communicates with it directly via the hyprsunset IPC socket.
  If the socket is unreachable the nightlight icon turns **red**.

## Installation and Usage

1. **Build**:
   ```bash
   make
   ```
2. **Launch**:
   ```bash
   ~/coding/c/ebar/launch.sh
   ```
   *Note: On first run, a default config will be generated at `~/.config/ebar/ebar.conf`.*

3. **Hyprland Integration**:
   Add this to your `hyprland.conf`:
   ```hyprlang
   exec-once = ~/coding/c/ebar/launch.sh
   ```

## Configuration

The configuration file allows you to tweak every aspect of the bar.

### How to Blur
If you want to add blur to the bar on Hyprland, add the following to your `hyprland.conf`:

```hyprlang
layerrule = blur on, ignore_alpha 0.01, match:namespace ebar
```

### Custom Event Integration
ebar supports listening to custom events via a Unix domain socket at `/tmp/hypr-events-extras.sock`.
For details on the supported commands, see [PROTOCOL.md](PROTOCOL.md).

The `ebar` binary (via its launch script) can be used to send these events and control system state:

```lua
-- Brightness control
hl.bind(", XF86MonBrightnessDown", hl.dsp.exec_cmd("~/coding/c/ebar/launch.sh --brightness lower"))
hl.bind(", XF86MonBrightnessUp", hl.dsp.exec_cmd("~/coding/c/ebar/launch.sh --brightness raise"))

-- Toggle floating with bar refresh
hl.bind(mainMod .. "+F", hl.dsp.exec_cmd("~/coding/c/ebar/launch.sh --togglefloat"))
```

### Example Layout
```ini
[bar]
position        = bottom          # top | bottom
mode            = island          # normal | floating | island | chromeos
margin          = 8               # outer gap in px (used when floating)
border_radius   = 12              # corner radius px (floating / island)
padding_h       = 12              # horizontal inner padding px
padding_v       = 5               # vertical inner padding px
spacing         = 12              # spacing between widgets px

[colors]
# Use any valid CSS colour: #RRGGBB, rgba(r,g,b,a), etc.
background      = rgba(0,0,0,0.2)
accent          = #0179d4
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
# Options: workspaces, clock, media, volume, metrics, nightlight, bluetooth, launcher
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

[brightness]
levels          = 100 82 68 54 42 31 21 12 4 1 0
transition_ms   = 200    # transition time in milliseconds

[launcher]
# Format: app = action:icon_path
# Icon path can be absolute or relative to ~/.config/ebar/
# If the path is just an icon name (e.g. firefox), it will use the system theme icon
app             = firefox:firefox.svg
app             = alacritty:utilities-terminal

[chromeos]
accent_color    = #0179d4
screenshot_app  = ~/coding/c/escreen/launch.sh
```

## License
MIT
