#include "chromeos_menu.h"
#include "gtk-layer-shell.h"
#include "ipc.h"
#include <gdk/gdkkeysyms.h>
#include <librsvg/rsvg.h>
#include <libupower-glib/upower.h>
#include <stdio.h>
#include <time.h>

const char *SETTINGS_SVG =
	"<svg width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
	"<path fill-rule=\"evenodd\" clip-rule=\"evenodd\" d=\"M12 8.00002C9.79085 8.00002 7.99999 9.79088 7.99999 12C7.99999 14.2092 9.79085 "
	"16 12 16C14.2091 16 16 14.2092 16 12C16 9.79088 14.2091 8.00002 12 8.00002ZM9.99999 12C9.99999 10.8955 10.8954 10 12 10C13.1046 10 14 "
	"10.8955 14 12C14 13.1046 13.1046 14 12 14C10.8954 14 9.99999 13.1046 9.99999 12Z\" fill=\"#e8eaed\"/>"
	"<path fill-rule=\"evenodd\" clip-rule=\"evenodd\" d=\"M12 8.00002C9.79085 8.00002 7.99999 9.79088 7.99999 12C7.99999 14.2092 9.79085 "
	"16 12 16C14.2091 16 16 14.2092 16 12C16 9.79088 14.2091 8.00002 12 8.00002ZM9.99999 12C9.99999 10.8955 10.8954 10 12 10C13.1046 10 14 "
	"10.8955 14 12C14 13.1046 13.1046 14 12 14C10.8954 14 9.99999 13.1046 9.99999 12Z\" fill=\"#e8eaed\"/>"
	"<path fill-rule=\"evenodd\" clip-rule=\"evenodd\" d=\"M10.7673 1.01709C10.9925 0.999829 11.2454 0.99993 11.4516 1.00001L12.5484 "
	"1.00001C12.7546 0.99993 13.0075 0.999829 13.2327 1.01709C13.4989 1.03749 13.8678 1.08936 14.2634 1.26937C14.7635 1.49689 15.1915 "
	"1.85736 15.5007 2.31147C15.7454 2.67075 15.8592 3.0255 15.9246 3.2843C15.9799 3.50334 16.0228 3.75249 16.0577 3.9557L16.1993 "
	"4.77635L16.2021 4.77788C16.2369 4.79712 16.2715 4.81659 16.306 4.8363L16.3086 4.83774L17.2455 4.49865C17.4356 4.42978 17.6693 4.34509 "
	"17.8835 4.28543C18.1371 4.2148 18.4954 4.13889 18.9216 4.17026C19.4614 4.20998 19.9803 4.39497 20.4235 4.70563C20.7734 4.95095 "
	"21.0029 5.23636 21.1546 5.4515C21.2829 5.63326 21.4103 5.84671 21.514 6.02029L22.0158 6.86003C22.1256 7.04345 22.2594 7.26713 22.3627 "
	"7.47527C22.4843 7.7203 22.6328 8.07474 22.6777 8.52067C22.7341 9.08222 22.6311 9.64831 22.3803 10.1539C22.1811 10.5554 21.9171 "
	"10.8347 21.7169 11.0212C21.5469 11.1795 21.3428 11.3417 21.1755 11.4746L20.5 12L21.1755 12.5254C21.3428 12.6584 21.5469 12.8205 "
	"21.7169 12.9789C21.9171 13.1653 22.1811 13.4446 22.3802 13.8461C22.631 14.3517 22.7341 14.9178 22.6776 15.4794C22.6328 15.9253 "
	"22.4842 16.2797 22.3626 16.5248C22.2593 16.7329 22.1255 16.9566 22.0158 17.14L21.5138 17.9799C21.4102 18.1535 21.2828 18.3668 21.1546 "
	"18.5485C21.0028 18.7637 20.7734 19.0491 20.4234 19.2944C19.9803 19.6051 19.4613 19.7901 18.9216 19.8298C18.4954 19.8612 18.1371 "
	"19.7852 17.8835 19.7146C17.6692 19.6549 17.4355 19.5703 17.2454 19.5014L16.3085 19.1623L16.306 19.1638C16.2715 19.1835 16.2369 "
	"19.2029 16.2021 19.2222L16.1993 19.2237L16.0577 20.0443C16.0228 20.2475 15.9799 20.4967 15.9246 20.7157C15.8592 20.9745 15.7454 "
	"21.3293 15.5007 21.6886C15.1915 22.1427 14.7635 22.5032 14.2634 22.7307C13.8678 22.9107 13.4989 22.9626 13.2327 22.983C13.0074 "
	"23.0002 12.7546 23.0001 12.5484 23H11.4516C11.2454 23.0001 10.9925 23.0002 10.7673 22.983C10.5011 22.9626 10.1322 22.9107 9.73655 "
	"22.7307C9.23648 22.5032 8.80849 22.1427 8.49926 21.6886C8.25461 21.3293 8.14077 20.9745 8.07542 20.7157C8.02011 20.4967 7.97723 "
	"20.2475 7.94225 20.0443L7.80068 19.2237L7.79791 19.2222C7.7631 19.2029 7.72845 19.1835 7.69396 19.1637L7.69142 19.1623L6.75458 "
	"19.5014C6.5645 19.5702 6.33078 19.6549 6.11651 19.7146C5.86288 19.7852 5.50463 19.8611 5.07841 19.8298C4.53866 19.7901 4.01971 "
	"19.6051 3.57654 19.2944C3.2266 19.0491 2.99714 18.7637 2.84539 18.5485C2.71718 18.3668 2.58974 18.1534 2.4861 17.9798L1.98418 "
	"17.14C1.87447 16.9566 1.74067 16.7329 1.63737 16.5248C1.51575 16.2797 1.36719 15.9253 1.32235 15.4794C1.26588 14.9178 1.36897 14.3517 "
	"1.61976 13.8461C1.81892 13.4446 2.08289 13.1653 2.28308 12.9789C2.45312 12.8205 2.65717 12.6584 2.82449 12.5254L3.47844 "
	"12.0054V11.9947L2.82445 11.4746C2.65712 11.3417 2.45308 11.1795 2.28304 11.0212C2.08285 10.8347 1.81888 10.5554 1.61972 "
	"10.1539C1.36893 9.64832 1.26584 9.08224 1.3223 8.52069C1.36714 8.07476 1.51571 7.72032 1.63732 7.47528C1.74062 7.26715 1.87443 "
	"7.04347 1.98414 6.86005L2.48605 6.02026C2.58969 5.84669 2.71714 5.63326 2.84534 5.45151C2.9971 5.23637 3.22655 4.95096 3.5765 "
	"4.70565C4.01966 4.39498 4.53862 4.20999 5.07837 4.17027C5.50458 4.1389 5.86284 4.21481 6.11646 4.28544C6.33072 4.34511 6.56444 4.4298 "
	"6.75451 4.49867L7.69141 4.83775L7.69394 4.8363C7.72844 4.8166 7.7631 4.79712 7.79791 4.77788L7.80068 4.77635L7.94225 3.95571C7.97723 "
	"3.7525 8.02011 3.50334 8.07542 3.2843C8.14077 3.0255 8.25461 2.67075 8.49926 2.31147C8.80849 1.85736 9.23648 1.49689 9.73655 "
	"1.26937C10.1322 1.08936 10.5011 1.03749 10.7673 1.01709ZM14.0938 4.3363C14.011 3.85634 13.9696 3.61637 13.8476 3.43717C13.7445 3.2858 "
	"13.6019 3.16564 13.4352 3.0898C13.2378 3.00002 12.9943 3.00002 12.5073 3.00002H11.4927C11.0057 3.00002 10.7621 3.00002 10.5648 "
	"3.0898C10.3981 3.16564 10.2555 3.2858 10.1524 3.43717C10.0304 3.61637 9.98895 3.85634 9.90615 4.3363L9.75012 5.24064C9.69445 5.56333 "
	"9.66662 5.72467 9.60765 5.84869C9.54975 5.97047 9.50241 6.03703 9.40636 6.13166C9.30853 6.22804 9.12753 6.3281 8.76554 "
	"6.52822C8.73884 6.54298 8.71227 6.55791 8.68582 6.57302C8.33956 6.77078 8.16643 6.86966 8.03785 6.90314C7.91158 6.93602 7.83293 "
	"6.94279 7.70289 6.93196C7.57049 6.92094 7.42216 6.86726 7.12551 6.7599L6.11194 6.39308C5.66271 6.2305 5.43809 6.14921 5.22515 "
	"6.16488C5.04524 6.17811 4.87225 6.23978 4.72453 6.34333C4.5497 6.46589 4.42715 6.67094 4.18206 7.08103L3.72269 7.84965C3.46394 8.2826 "
	"3.33456 8.49907 3.31227 8.72078C3.29345 8.90796 3.32781 9.09665 3.41141 9.26519C3.51042 9.4648 3.7078 9.62177 4.10256 9.9357L4.82745 "
	"10.5122C5.07927 10.7124 5.20518 10.8126 5.28411 10.9199C5.36944 11.036 5.40583 11.1114 5.44354 11.2504C5.47844 11.379 5.47844 11.586 "
	"5.47844 12C5.47844 12.414 5.47844 12.621 5.44354 12.7497C5.40582 12.8887 5.36944 12.9641 5.28413 13.0801C5.20518 13.1875 5.07927 "
	"13.2876 4.82743 13.4879L4.10261 14.0643C3.70785 14.3783 3.51047 14.5352 3.41145 14.7349C3.32785 14.9034 3.29349 15.0921 3.31231 "
	"15.2793C3.33461 15.501 3.46398 15.7174 3.72273 16.1504L4.1821 16.919C4.4272 17.3291 4.54974 17.5342 4.72457 17.6567C4.8723 17.7603 "
	"5.04528 17.8219 5.2252 17.8352C5.43813 17.8508 5.66275 17.7695 6.11199 17.607L7.12553 17.2402C7.42216 17.1328 7.5705 17.0791 7.7029 "
	"17.0681C7.83294 17.0573 7.91159 17.064 8.03786 17.0969C8.16644 17.1304 8.33956 17.2293 8.68582 17.427C8.71228 17.4421 8.73885 17.4571 "
	"8.76554 17.4718C9.12753 17.6719 9.30853 17.772 9.40635 17.8684C9.50241 17.963 9.54975 18.0296 9.60765 18.1514C9.66662 18.2754 9.69445 "
	"18.4367 9.75012 18.7594L9.90615 19.6637C9.98895 20.1437 10.0304 20.3837 10.1524 20.5629C10.2555 20.7142 10.3981 20.8344 10.5648 "
	"20.9102C10.7621 21 11.0057 21 11.4927 21H12.5073C12.9943 21 13.2378 21 13.4352 20.9102C13.6019 20.8344 13.7445 20.7142 13.8476 "
	"20.5629C13.9696 20.3837 14.011 20.1437 14.0938 19.6637L14.2499 18.7594C14.3055 18.4367 14.3334 18.2754 14.3923 18.1514C14.4502 "
	"18.0296 14.4976 17.963 14.5936 17.8684C14.6915 17.772 14.8725 17.6719 15.2344 17.4718C15.2611 17.4571 15.2877 17.4421 15.3141 "
	"17.427C15.6604 17.2293 15.8335 17.1304 15.9621 17.0969C16.0884 17.064 16.167 17.0573 16.2971 17.0681C16.4295 17.0791 16.5778 17.1328 "
	"16.8744 17.2402L17.888 17.607C18.3372 17.7696 18.5619 17.8509 18.7748 17.8352C18.9547 17.8219 19.1277 17.7603 19.2754 17.6567C19.4502 "
	"17.5342 19.5728 17.3291 19.8179 16.919L20.2773 16.1504C20.536 15.7175 20.6654 15.501 20.6877 15.2793C20.7065 15.0921 20.6721 14.9034 "
	"20.5885 14.7349C20.4895 14.5353 20.2921 14.3783 19.8974 14.0643L19.1726 13.4879C18.9207 13.2876 18.7948 13.1875 18.7159 "
	"13.0801C18.6306 12.9641 18.5942 12.8887 18.5564 12.7497C18.5215 12.6211 18.5215 12.414 18.5215 12C18.5215 11.586 18.5215 11.379 "
	"18.5564 11.2504C18.5942 11.1114 18.6306 11.036 18.7159 10.9199C18.7948 10.8126 18.9207 10.7124 19.1725 10.5122L19.8974 9.9357C20.2922 "
	"9.62176 20.4896 9.46479 20.5886 9.26517C20.6722 9.09664 20.7065 8.90795 20.6877 8.72076C20.6654 8.49906 20.5361 8.28259 20.2773 "
	"7.84964L19.8179 7.08102C19.5728 6.67093 19.4503 6.46588 19.2755 6.34332C19.1277 6.23977 18.9548 6.1781 18.7748 6.16486C18.5619 "
	"6.14919 18.3373 6.23048 17.888 6.39307L16.8745 6.75989C16.5778 6.86725 16.4295 6.92093 16.2971 6.93195C16.167 6.94278 16.0884 6.93601 "
	"15.9621 6.90313C15.8335 6.86965 15.6604 6.77077 15.3142 6.57302C15.2877 6.55791 15.2611 6.54298 15.2345 6.52822C14.8725 6.3281 "
	"14.6915 6.22804 14.5936 6.13166C14.4976 6.03703 14.4502 5.97047 14.3923 5.84869C14.3334 5.72467 14.3055 5.56332 14.2499 "
	"5.24064L14.0938 4.3363Z\" fill=\"#e8eaed\"/>"
	"</svg>";

/* Global timestamp to handle focus-out vs click race conditions */
static long long last_destroy_time = 0;

static long long get_time_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static gboolean on_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
	(void)event;
	(void)data;
	last_destroy_time = get_time_ms();
	gtk_widget_destroy(widget);
	return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
	(void)data;
	if (event->keyval == GDK_KEY_Escape) {
		last_destroy_time = get_time_ms();
		gtk_widget_destroy(widget);
		return TRUE;
	}
	return FALSE;
}

void apply_menu_css(void) {
	/* NUCLEAR: No more 'loaded' guard. Load and apply freshly every time. */
	const char *css = ".ebar-menu-window { "
					  "  background-color: transparent; "
					  "  background: none; "
					  "  box-shadow: none; "
					  "  border: none; "
					  "  outline: none; "
					  "} "
					  "#menu-bg { "
					  "  background-color: #2b2b2b; "
					  "  border-radius: 24px; "
					  "  border: 1px solid rgba(255,255,255,0.1); "
					  "} "
					  ".cb-menu { "
					  "  padding: 16px; "
					  "} "
					  ".cb-menu-grid { "
					  "  margin-bottom: 12px; "
					  "} "
					  ".cb-menu-pill { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 16px; "
					  "  padding: 12px; "
					  "  margin: 0; "
					  "  border: none; "
					  "  box-shadow: none; "
					  "  min-height: 52px; "
					  "} "
					  ".cb-menu-pill:hover { "
					  "  background-color: #4c4c4c; "
					  "} "
					  ".cb-menu-pill-active { "
					  "  background-color: #cbbef9; "
					  "  color: #202124; "
					  "} "
					  ".cb-menu-pill-active:hover { "
					  "  background-color: #d8cefa; "
					  "} "
					  ".cb-menu-pill label { "
					  "  font-weight: 600; "
					  "  margin: 0; "
					  "} "
					  ".cb-menu-pill .icon { "
					  "  font-size: 20px; "
					  "  margin-right: 12px; "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} "
					  ".cb-menu-pill .subtitle { "
					  "  font-size: 11px; "
					  "  opacity: 0.8; "
					  "} "
					  ".cb-menu-slider-box { "
					  "  margin: 6px 0; "
					  "} "
					  ".cb-menu-slider-icon { "
					  "  font-size: 18px; "
					  "  color: #202124; "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  margin-left: 24px; "
					  "} "
					  ".cb-menu-slider trough { "
					  "  background-color: #3c3c3c; "
					  "  border-radius: 18px; "
					  "  min-height: 36px; "
					  "} "
					  ".cb-menu-slider trough highlight { "
					  "  background-color: #cbbef9; "
					  "  border-radius: 18px; "
					  "} "
					  ".cb-menu-slider slider { "
					  "  all: unset; "
					  "} "
					  ".cb-menu-bottom { "
					  "  margin-top: 16px; "
					  "} "
					  ".cb-menu-power { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 20px; "
					  "  padding: 8px 16px; "
					  "  border: none; "
					  "} "
					  ".cb-menu-power label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 24px; "
					  "} "
					  ".cb-menu-settings { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 22px; "
					  "  min-width: 44px; "
					  "  min-height: 44px; "
					  "  border: none; "
					  "  padding: 0; "
					  "  display: flex; "
					  "  align-items: center; "
					  "  justify-content: center; "
					  "} "
					  ".cb-menu-settings label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "  font-size: 22px; "
					  "  margin: 0; "
					  "  padding: 0; "
					  "} "
					  ".cb-menu-battery { "
					  "  color: #e8eaed; "
					  "  font-size: 13px; "
					  "  margin: 0; "
					  "} "
					  ".cb-menu-slider-arrow { "
					  "  color: #e8eaed; "
					  "  font-size: 16px; "
					  "  margin-left: 12px; "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} "
					  ".cb-menu-slider-btn { "
					  "  background-color: #3c3c3c; "
					  "  color: #e8eaed; "
					  "  border-radius: 18px; "
					  "  width: 36px; "
					  "  height: 36px; "
					  "  min-width: 36px; "
					  "  min-height: 36px; "
					  "  font-size: 18px; "
					  "  border: none; "
					  "  padding: 0; "
					  "  margin-left: 8px; "
					  "} "
					  ".cb-menu-slider-btn label { "
					  "  font-family: \"JetBrainsMonoNerdFont\"; "
					  "} ";

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
											  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
}

static GtkWidget *create_menu_pill(const char *icon, const char *title, const char *subtitle, gboolean active, GtkWidget **subtitle_out) {
	GtkWidget *btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-pill");

	if (active)
		gtk_style_context_add_class(gtk_widget_get_style_context(btn), "cb-menu-pill-active");

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	GtkWidget *icon_lbl = gtk_label_new(icon);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "icon");
	gtk_box_pack_start(GTK_BOX(box), icon_lbl, FALSE, FALSE, 0);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

	GtkWidget *title_lbl = gtk_label_new(title);
	gtk_widget_set_halign(title_lbl, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), title_lbl, FALSE, FALSE, 0);

	if (subtitle) {
		GtkWidget *sub_lbl = gtk_label_new(subtitle);
		gtk_style_context_add_class(gtk_widget_get_style_context(sub_lbl), "subtitle");
		gtk_widget_set_halign(sub_lbl, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(vbox), sub_lbl, FALSE, FALSE, 0);
		if (subtitle_out)
			*subtitle_out = sub_lbl;
	}

	gtk_box_pack_start(GTK_BOX(box), vbox, TRUE, TRUE, 0);

	GtkWidget *arrow = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(arrow), "subtitle");
	gtk_box_pack_end(GTK_BOX(box), arrow, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(btn), box);

	return btn;
}

static GtkWidget *create_menu_slider(const char *icon, const char *right_icon) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "cb-menu-slider-box");

	GtkWidget *overlay = gtk_overlay_new();

	GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_range_set_value(GTK_RANGE(scale), 80);
	gtk_style_context_add_class(gtk_widget_get_style_context(scale), "cb-menu-slider");

	GtkWidget *icon_lbl = gtk_label_new(icon);
	gtk_style_context_add_class(gtk_widget_get_style_context(icon_lbl), "cb-menu-slider-icon");
	gtk_widget_set_halign(icon_lbl, GTK_ALIGN_START);
	gtk_widget_set_valign(icon_lbl, GTK_ALIGN_CENTER);

	gtk_container_add(GTK_CONTAINER(overlay), scale);
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), icon_lbl);

	gtk_box_pack_start(GTK_BOX(box), overlay, TRUE, TRUE, 0);

	if (right_icon) {
		GtkWidget *right_btn = gtk_button_new_with_label(right_icon);
		gtk_widget_set_valign(right_btn, GTK_ALIGN_CENTER);
		gtk_style_context_add_class(gtk_widget_get_style_context(right_btn), "cb-menu-slider-btn");
		gtk_box_pack_start(GTK_BOX(box), right_btn, FALSE, FALSE, 0);
	}

	GtkWidget *arrow_lbl = gtk_label_new("");
	gtk_widget_set_valign(arrow_lbl, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(arrow_lbl), "cb-menu-slider-arrow");
	gtk_box_pack_start(GTK_BOX(box), arrow_lbl, FALSE, FALSE, 0);

	return box;
}

static GdkPixbuf *create_pixbuf_from_svg(const char *svg_data, int size) {
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new_with_type("svg", NULL);
	if (!loader)
		return NULL;
	gdk_pixbuf_loader_write(loader, (const guchar *)svg_data, strlen(svg_data), NULL);
	gdk_pixbuf_loader_close(loader, NULL);
	GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
	if (pixbuf) {
		GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, size, size, GDK_INTERP_BILINEAR);
		g_object_ref(scaled);
		g_object_unref(loader);
		return scaled;
	}
	g_object_unref(loader);
	return NULL;
}

static char *get_battery_info(void) {
	UpClient *client = up_client_new();
	if (!client)
		return g_strdup("Battery: N/A");

	GPtrArray *devices = up_client_get_devices2(client);
	if (!devices) {
		g_object_unref(client);
		return g_strdup("Battery: N/A");
	}

	char *info = NULL;
	for (guint i = 0; i < devices->len; i++) {
		UpDevice *device = g_ptr_array_index(devices, i);
		UpDeviceKind kind;
		g_object_get(device, "kind", &kind, NULL);

		if (kind == UP_DEVICE_KIND_BATTERY) {
			double percentage;
			gint64 time_to_empty, time_to_full;
			UpDeviceState state;

			g_object_get(device, "percentage", &percentage, "state", &state, "time-to-empty", &time_to_empty, "time-to-full", &time_to_full,
						 NULL);

			int hours = 0, mins = 0;
			const char *status_str = "";

			if (state == UP_DEVICE_STATE_CHARGING) {
				hours = (int)(time_to_full / 3600);
				mins = (int)((time_to_full % 3600) / 60);
				status_str = "Time to full: ";
			} else if (state == UP_DEVICE_STATE_DISCHARGING) {
				hours = (int)(time_to_empty / 3600);
				mins = (int)((time_to_empty % 3600) / 60);
				status_str = "Remaining: ";
			} else if (state == UP_DEVICE_STATE_FULLY_CHARGED) {
				info = g_strdup_printf("%.0f%% - Charged", percentage);
				break;
			}

			if (hours > 0 || mins > 0) {
				info = g_strdup_printf("%.0f%% - %s%d:%02d", percentage, status_str, hours, mins);
			} else {
				info = g_strdup_printf("%.0f%%", percentage);
			}
			break;
		}
	}

	g_ptr_array_unref(devices);
	g_object_unref(client);

	if (!info)
		return g_strdup("Battery: N/A");
	return info;
}

static void on_keyboard_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	(void)data;
	char *res = hyprctl_request("switchxkblayout all next");
	if (res)
		free(res);
}

static GtkWidget *create_chromeos_menu(BarWindow *bw, AppState *state) {
	apply_menu_css();

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_name(win, "ebar-menu-window");
	gtk_style_context_add_class(gtk_widget_get_style_context(win), "ebar-menu-window");

	GdkScreen *screen = gdk_screen_get_default();
	GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
	if (visual && gdk_screen_is_composited(screen))
		gtk_widget_set_visual(win, visual);

	gtk_widget_set_app_paintable(win, TRUE);
	g_signal_connect(win, "focus-out-event", G_CALLBACK(on_focus_out), NULL);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key_press), NULL);

	gtk_layer_init_for_window(GTK_WINDOW(win));
	gtk_layer_set_monitor(GTK_WINDOW(win), bw->monitor);
	gtk_layer_set_namespace(GTK_WINDOW(win), "ebar-menu");

	/* Explicitly set all anchors to prevent the window from stretching to fullscreen */
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP, FALSE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);

	const int SPACING_FROM_BAR = 6;
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, 48 + SPACING_FROM_BAR); // 48 is the height of the bar
	gtk_layer_set_margin(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT, SPACING_FROM_BAR);

	gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_TOP);
	/* Use ON_DEMAND to allow Esc key to work without grabbing all input */
	gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
	gtk_layer_set_exclusive_zone(GTK_WINDOW(win), -1);

	gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
	gtk_window_set_resizable(GTK_WINDOW(win), FALSE);

	GtkWidget *bg = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_name(bg, "menu-bg");
	/* Ensure the background is centered/aligned to avoid blocking if the window is slightly larger */
	gtk_widget_set_halign(bg, GTK_ALIGN_END);
	gtk_widget_set_valign(bg, GTK_ALIGN_END);

	GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(main_box), "cb-menu");
	gtk_widget_set_size_request(main_box, 420, -1);
	gtk_widget_set_halign(main_box, GTK_ALIGN_END);
	gtk_widget_set_valign(main_box, GTK_ALIGN_END);

	GtkWidget *grid = gtk_grid_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(grid), "cb-menu-grid");
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

	GtkWidget *wifi = create_menu_pill("󰤨", "Not connected", "No networks", TRUE, NULL);
	gtk_grid_attach(GTK_GRID(grid), wifi, 0, 0, 2, 1);

	GtkWidget *screenshot = create_menu_pill("󰄀", "Screen capture", NULL, FALSE, NULL);
	gtk_grid_attach(GTK_GRID(grid), screenshot, 2, 0, 2, 1);

	GtkWidget *bluetooth = create_menu_pill("󰂯", "Bluetooth", "On", TRUE, NULL);
	gtk_grid_attach(GTK_GRID(grid), bluetooth, 0, 1, 2, 1);

	GtkWidget *keyboard =
		create_menu_pill("󰌌", "Keyboard", state->sys_data.kb_layout[0] ? state->sys_data.kb_layout : "US", FALSE, &bw->cb_menu_kb_label);
	g_signal_connect(keyboard, "clicked", G_CALLBACK(on_keyboard_clicked), NULL);
	gtk_grid_attach(GTK_GRID(grid), keyboard, 2, 1, 2, 1);

	gtk_box_pack_start(GTK_BOX(main_box), grid, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(main_box), create_menu_slider("󰕾", "󰝟"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(main_box), create_menu_slider("󰃟", ""), FALSE, FALSE, 0);

	GtkWidget *bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(bottom_box), "cb-menu-bottom");

	GtkWidget *power_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(power_btn), "cb-menu-power");
	gtk_container_add(GTK_CONTAINER(power_btn), gtk_label_new("󰐥 "));
	gtk_box_pack_start(GTK_BOX(bottom_box), power_btn, FALSE, FALSE, 0);

	char *bat_info = get_battery_info();
	GtkWidget *battery_lbl = gtk_label_new(bat_info);
	g_free(bat_info);
	bw->cb_menu_bat_label = battery_lbl;
	gtk_style_context_add_class(gtk_widget_get_style_context(battery_lbl), "cb-menu-battery");
	gtk_widget_set_halign(battery_lbl, GTK_ALIGN_END);
	gtk_box_pack_start(GTK_BOX(bottom_box), battery_lbl, TRUE, TRUE, 12);

	GtkWidget *settings_btn = gtk_button_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(settings_btn), "cb-menu-settings");
	GdkPixbuf *settings_pix = create_pixbuf_from_svg(SETTINGS_SVG, 22);
	if (settings_pix) {
		gtk_container_add(GTK_CONTAINER(settings_btn), gtk_image_new_from_pixbuf(settings_pix));
		g_object_unref(settings_pix);
	} else {
		gtk_container_add(GTK_CONTAINER(settings_btn), gtk_label_new("󰒓"));
	}
	gtk_box_pack_end(GTK_BOX(bottom_box), settings_btn, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(main_box), bottom_box, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(bg), main_box);
	gtk_container_add(GTK_CONTAINER(win), bg);

	return win;
}

static void on_menu_destroy(GtkWidget *widget, gpointer data) {
	(void)widget;
	BarWindow *bw = (BarWindow *)data;
	bw->menu_window = NULL;
	bw->cb_menu_kb_label = NULL;
	bw->cb_menu_bat_label = NULL;
}

void toggle_chromeos_menu(BarWindow *bw, AppState *state) {
	long long now = get_time_ms();

	if (bw->menu_window) {
		last_destroy_time = get_time_ms();
		gtk_widget_destroy(bw->menu_window);
		return;
	}

	if (now - last_destroy_time < 500) {
		return;
	}

	bw->menu_window = create_chromeos_menu(bw, state);
	g_signal_connect(bw->menu_window, "destroy", G_CALLBACK(on_menu_destroy), bw);

	gtk_widget_show_all(bw->menu_window);
	gtk_widget_grab_focus(bw->menu_window);
}

void close_all_chromeos_menus(AppState *state) {
	pthread_mutex_lock(&state->mutex);
	for (GList *l = state->bar_windows; l != NULL; l = l->next) {
		BarWindow *bw = (BarWindow *)l->data;
		if (bw->menu_window) {
			last_destroy_time = get_time_ms();
			gtk_widget_destroy(bw->menu_window);
			bw->menu_window = NULL;
		}
	}
	pthread_mutex_unlock(&state->mutex);
}

static void on_sys_btn_clicked(GtkWidget *widget, gpointer data) {
	(void)widget;
	struct {
		BarWindow *bw;
		AppState *state;
	} *ctx = data;
	toggle_chromeos_menu(ctx->bw, ctx->state);
}

void setup_chromeos_menu_toggle(BarWindow *bw, AppState *state) {
	if (!bw->cb_sys_label)
		return;

	GtkWidget *btn = gtk_widget_get_parent(bw->cb_sys_label);
	if (GTK_IS_BUTTON(btn)) {
		typedef struct {
			BarWindow *bw;
			AppState *state;
		} CallbackCtx;
		CallbackCtx *ctx = g_new0(CallbackCtx, 1);
		ctx->bw = bw;
		ctx->state = state;

		g_signal_connect(btn, "clicked", G_CALLBACK(on_sys_btn_clicked), ctx);
	}
}
