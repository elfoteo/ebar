#ifndef CONSTANTS_H
#define CONSTANTS_H

#define EXTRA_EVENTS_SOCK_PATH "/tmp/hypr-events-extras.sock"

#define NIGHTLIGHT_STATE_FILE_PATH "/tmp/ebar_nightlight"
#define NIGHTLIGHT_DEFAULT_LEVEL 15

#define BRIGHTNESSCTL_SET_FMT "brightnessctl set %.0f%%"
#define COORD_FILE_PATH "/tmp/ebar-brightness.coord"
#define BRIGHTNESS_LAST_FILE_PATH "/tmp/ebar-brightness.last"

#define ANIM_BRIGHTNESS_LERP 0.25f
#define ANIM_VOLUME_LERP 0.15f
#define ANIM_DEADZONE 0.05f
#define ANIM_TIMER_INTERVAL_MS 16

#define BRIGHTNESS_TRANSITION_STEP_NS 8000000

#define IPC_RECONNECT_DELAY_US 100000
#define BT_DBUS_SHORT_TIMEOUT_MS 2000
#define BT_DBUS_LONG_TIMEOUT_MS 100000

#define MEDIA_RESTART_DELAY_S 5
#define MEDIA_IDLE_DELAY_S 2

#define POPUP_AUTO_HIDE_TIMEOUT_MS 3000
#define POPUP_FADE_STEP 0.12

#define HYPR_SOCKET_BUFFER_SIZE 131072

#define DEFAULT_TEMP_PATH "/sys/class/thermal/thermal_zone1/temp"

#endif
