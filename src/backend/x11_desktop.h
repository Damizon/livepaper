#ifndef LIVEPAPER_X11_DESKTOP_H
#define LIVEPAPER_X11_DESKTOP_H

#include <X11/Xlib.h>

typedef struct DesktopWindowSelection
{
    Window root;
    Window desktop;
    Window window;
    int swm_vroot_found;
    int nemo_found;
} DesktopWindowSelection;

DesktopWindowSelection xwinwrap_find_desktop_window(Display *d);
int enable_input_passthrough(Display *d, Window win);
int wait_for_desktop_ready(Display *d, int timeout_seconds);
void set_window_type_desktop(Display *d, Window win);
void set_window_states(Display *d, Window win);
void keep_layer_order(Display *d, Window livepaper);
void refresh_muffin_stacking_once(Display *d, Window livepaper);

#endif
