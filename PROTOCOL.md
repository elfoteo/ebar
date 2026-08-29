# ebar IPC Protocol

ebar listens for events on two Unix domain sockets:
1. Hyprland's `socket2` (for workspace and window events).
2. `/tmp/hypr-events-extras.sock` (for custom events).

All commands are passed through `handle_ipc_line` and should be terminated with a newline `\n`.

## Supported Commands

### Window & Workspace Management
- `workspace>>ID`: Updates the active workspace ID.
- `activewindowv2>>ADDR,CLASS`: Updates the active window focus.
- `openwindow>>ADDR,WS,CLASS,TITLE`: Tracks a new window opening.
- `closewindow>>ADDR`: Tracks a window closing.
- `movewindow>>ADDR,WS`: Tracks a window moving between workspaces.
- `togglefloating`: Forces a refresh of workspace and fullscreen state.
- `fullscreen>>1|0`: Updates the fullscreen state (1 for entered, 0 for left).

### System State
- `brightness>>VALUE`: Updates the bar's brightness display (0.0 to 100.0).
- `activelayout>>KEYBOARD,LAYOUT`: Notifies of a keyboard layout change.

## CLI Usage

The `ebar` binary itself can be used to send these events:

- `ebar --togglefloat`: Dispatches `hl.dsp.window.float({ action = "toggle" })` to Hyprland and notifies ebar.
- `ebar --brightness raise|lower`: Calculates the next brightness level based on `ebar.conf`, sets it, and notifies ebar.
