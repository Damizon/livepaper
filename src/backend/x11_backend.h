#ifndef LIVEPAPER_X11_BACKEND_H
#define LIVEPAPER_X11_BACKEND_H

#include <X11/Xlib.h>
#include <sys/types.h>

void x11_backend_set_window(Display *display, Window window);
void x11_backend_set_mpv_pid(pid_t pid);
pid_t x11_backend_get_mpv_pid(void);
void cleanup(int sig);
void list_monitors(void);

#endif
