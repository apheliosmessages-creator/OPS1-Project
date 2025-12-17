#ifndef WATCHER_H
#define WATCHER_H

#include <sys/inotify.h>
#include <limits.h>

void add_watch(int fd, const char *path);
void add_watches_recursive(int fd, const char *root);
void remove_watch_by_wd(int wd);
const char *get_watch_path(int wd);

#endif
