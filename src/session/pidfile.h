#ifndef LIVEPAPER_PIDFILE_H
#define LIVEPAPER_PIDFILE_H

#include <sys/types.h>

pid_t read_pid(void);
void write_pid(pid_t pid);

#endif
