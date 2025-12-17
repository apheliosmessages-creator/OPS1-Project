#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <limits.h>
#include "watcher.h"

#define MAX_WATCHES 1024

typedef struct {
    int wd;
    char path[PATH_MAX];
} Watch;

static Watch watches[MAX_WATCHES];
static int watch_count = 0;

void add_watch(int fd, const char *path) {
    if (watch_count >= MAX_WATCHES) {
        fprintf(stderr, "Error: Max watches reached\n");
        return;
    }

    int wd = inotify_add_watch(fd, path,
        IN_CREATE | IN_DELETE | IN_MODIFY |
        IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF);

    if (wd < 0) return;

    watches[watch_count].wd = wd;
    strncpy(watches[watch_count].path, path, PATH_MAX);
    watch_count++;
}

void add_watches_recursive(int fd, const char *root) {
    struct stat st;
    if (lstat(root, &st) < 0 || !S_ISDIR(st.st_mode))
        return;

    add_watch(fd, root);

    DIR *d = opendir(root);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char sub[PATH_MAX];
        snprintf(sub, sizeof(sub), "%s/%s", root, e->d_name);

        if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
            add_watches_recursive(fd, sub);
        }
    }
    closedir(d);
}

void remove_watch_by_wd(int wd) {
    for (int i = 0; i < watch_count; i++) {
        if (watches[i].wd == wd) {
            // Move last element to this position
            watches[i] = watches[watch_count - 1];
            watch_count--;
            break;
        }
    }
}

const char *get_watch_path(int wd) {
    for (int i = 0; i < watch_count; i++) {
        if (watches[i].wd == wd) {
            return watches[i].path;
        }
    }
    return NULL;
}
