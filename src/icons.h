#ifndef EBAR_ICONS_H
#define EBAR_ICONS_H

/*
 * Nerd Font glyphs as explicit byte escapes.
 *
 * The private-use-area characters these refer to are fragile in editors and
 * tooling; keeping them here as \xNN sequences means no source file ever has
 * to contain the raw glyphs, and they can never be silently mangled again.
 * Each macro is a string literal, usable anywhere a const char * is expected
 * and composable via adjacent-literal concatenation ("x" ICON_CHECK "y").
 */

#define ICON_CHEVRON_RIGHT "\xef\x91\xa0"	   /* right chevron: pill arrow, slider +/- */
#define ICON_CHEVRON_LEFT "\xef\x91\xbd"	   /* left chevron: desk switcher prev */
#define ICON_ARROW_LEFT "\xf3\xb0\x81\x8d"	   /* menu back button */
#define ICON_LAUNCHER "\xee\xaa\xbc"		   /* launcher circle button */
#define ICON_CLIPBOARD "\xef\x83\x86"		   /* clipboard bar button */
#define ICON_PEN "\xf3\xb0\x8f\xab"			   /* stylus bar button */
#define ICON_VOLUME_HIGH "\xf3\xb0\x95\xbe"	   /* volume high */
#define ICON_VOLUME_MEDIUM "\xf3\xb0\x96\x80"  /* volume medium */
#define ICON_VOLUME_LOW "\xf3\xb0\x95\xbf"	   /* volume low */
#define ICON_VOLUME_MUTE "\xf3\xb0\x9d\x9f"	   /* volume muted */
#define ICON_BRIGHTNESS "\xf3\xb0\x83\x9f"	   /* brightness sun */
#define ICON_NIGHTLIGHT "\xef\x93\xae"		   /* nightlight toggle on brightness row */
#define ICON_NIGHTLIGHT_ON "\xf3\xb0\x96\x94"  /* nightlight active moon */
#define ICON_NIGHTLIGHT_OFF "\xf3\xb0\x96\x99" /* nightlight inactive */
#define ICON_LIGHTBULB "\xf3\xb0\x8c\xb5"	   /* LED slider on */
#define ICON_LIGHTBULB_OFF "\xf3\xb0\xb9\x8f"  /* LED slider off */
#define ICON_KEYBOARD "\xf3\xb0\x8c\x8c"	   /* keyboard pill + bt keyboard */
#define ICON_CHECK "\xf3\xb0\x84\xac"		   /* list checkmark */
#define ICON_BLUETOOTH "\xf3\xb0\x82\xaf"	   /* bluetooth pill / default device */
#define ICON_BLUETOOTH_ON "\xf3\xb0\x82\xaf"   /* bluetooth enabled */
#define ICON_BLUETOOTH_OFF "\xf3\xb0\x82\xb2"  /* bluetooth disabled */
#define ICON_WIFI_OFF "\xf3\xb0\xa4\xae"	   /* wifi off / no adapter */
#define ICON_CAMERA "\xf3\xb0\x84\x80"		   /* screen capture pill + bt camera */
#define ICON_POWER "\xf3\xb0\x90\xa5"		   /* power button + menu entry */
#define ICON_POWER_PLUG "\xef\x91\xbb"		   /* trailing glyph in power button label */
#define ICON_SLEEP "\xf3\xb0\xa4\x84"		   /* suspend entry */
#define ICON_RESTART "\xf3\xb0\x9c\x89"		   /* restart entry */
#define ICON_SETTINGS "\xf3\xb0\x92\x93"	   /* settings gear */
#define ICON_WIFI_0 "\xf3\xb0\xa4\xaf"		   /* wifi open, disconnected */
#define ICON_WIFI_1 "\xf3\xb0\xa4\x9f"		   /* wifi open, 1 bar */
#define ICON_WIFI_2 "\xf3\xb0\xa4\xa2"		   /* wifi open, 2 bars */
#define ICON_WIFI_3 "\xf3\xb0\xa4\xa5"		   /* wifi open, 3 bars */
#define ICON_WIFI_4 "\xf3\xb0\xa4\xa8"		   /* wifi open, full */
#define ICON_WIFI_SEC_0 "\xf3\xb0\xa4\xac"	   /* wifi secured, disconnected */
#define ICON_WIFI_SEC_1 "\xf3\xb0\xa4\xa1"	   /* wifi secured, 1 bar */
#define ICON_WIFI_SEC_2 "\xf3\xb0\xa4\xa4"	   /* wifi secured, 2 bars */
#define ICON_WIFI_SEC_3 "\xf3\xb0\xa4\xa7"	   /* wifi secured, 3 bars */
#define ICON_WIFI_SEC_4 "\xf3\xb0\xa4\xaa"	   /* wifi secured, full */
#define ICON_EYE "\xf3\xb0\x88\x88"			   /* password visible */
#define ICON_EYE_OFF "\xf3\xb0\x88\x89"		   /* password hidden */
#define ICON_BATTERY_10 "\xf3\xb0\x81\xba"	   /* battery discharge levels */
#define ICON_BATTERY_20 "\xf3\xb0\x81\xbb"
#define ICON_BATTERY_30 "\xf3\xb0\x81\xbc"
#define ICON_BATTERY_40 "\xf3\xb0\x81\xbd"
#define ICON_BATTERY_50 "\xf3\xb0\x81\xbe"
#define ICON_BATTERY_60 "\xf3\xb0\x81\xbf"
#define ICON_BATTERY_70 "\xf3\xb0\x82\x80"
#define ICON_BATTERY_80 "\xf3\xb0\x82\x81"
#define ICON_BATTERY_90 "\xf3\xb0\x82\x82"
#define ICON_BATTERY_100 "\xf3\xb0\x81\xb9"	   /* battery full */
#define ICON_BATTERY_CHG_10 "\xf3\xb0\xa2\x9c" /* battery charging levels */
#define ICON_BATTERY_CHG_20 "\xf3\xb0\x82\x86"
#define ICON_BATTERY_CHG_30 "\xf3\xb0\x82\x87"
#define ICON_BATTERY_CHG_40 "\xf3\xb0\x82\x88"
#define ICON_BATTERY_CHG_50 "\xf3\xb0\xa2\x9d"
#define ICON_BATTERY_CHG_60 "\xf3\xb0\x82\x89"
#define ICON_BATTERY_CHG_70 "\xf3\xb0\xa2\x9e"
#define ICON_BATTERY_CHG_80 "\xf3\xb0\x82\x8a"
#define ICON_BATTERY_CHG_90 "\xf3\xb0\x82\x8b"
#define ICON_BATTERY_CHG_100 "\xf3\xb0\x82\x85"
#define ICON_BATTERY_UNKNOWN "\xf3\xb1\x9f\xa9" /* no battery present */
#define ICON_METRIC_RAM "\xee\xbf\x85"			/* RAM metric box */
#define ICON_METRIC_CPU "\xef\x92\xbc"			/* CPU metric box */
#define ICON_METRIC_GPU "\xf3\xb0\xa2\xae"		/* GPU metric box */
#define ICON_METRIC_DISK "\xf3\xb0\x8b\x8a"		/* disk metric box */
#define ICON_METRIC_TEMP "\xf3\xb0\x94\x8f"		/* temp metric boxes */
#define ICON_MEDIA_PLAY "\xf3\xb0\x90\x8a"		/* media play */
#define ICON_MEDIA_PAUSE "\xf3\xb0\x8f\xa4"		/* media pause */
#define ICON_MEDIA_PREV "\xf3\xb0\x92\xae"		/* media previous */
#define ICON_MEDIA_NEXT "\xf3\xb0\x92\xad"		/* media next */
#define ICON_BT_PHONE "\xf3\xb0\x84\x8b"		/* bt phone */
#define ICON_BT_COMPUTER "\xf3\xb0\x8c\xa2"		/* bt computer/display */
#define ICON_BT_HEADSET "\xf3\xb0\x8b\x8e"		/* bt headset */
#define ICON_BT_HEADPHONES "\xf3\xb0\x8b\x8b"	/* bt headphones/audio card */
#define ICON_BT_MOUSE "\xf3\xb0\x8d\xbd"		/* bt mouse */
#define ICON_BT_GAMEPAD "\xf3\xb0\xae\x82"		/* bt gaming input */
#define ICON_BT_PRINTER "\xf3\xb0\x90\xaa"		/* bt printer */
#define ICON_BT_MEDIA_PLAYER "\xf3\xb0\xa6\x9a" /* bt multimedia player */
#define ICON_BT_NETWORK "\xf3\xb0\x91\xa9"		/* bt modem/wireless */
#define ICON_BT_WATCH "\xf3\xb0\x96\x89"		/* bt watch/wearable */
#define ICON_BT_DEVICE "\xf3\xb0\x97\xb6"		/* bt other peripheral */
#define ICON_WS_EMPTY "\xef\x84\x8c"			/* config default workspace empty dot */
#define ICON_WS_OCCUPIED "\xef\x84\x91"			/* config default workspace occupied dot */

#endif /* EBAR_ICONS_H */
