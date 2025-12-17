#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <limits.h>
#include <time.h>
#include <errno.h>

#include "worker.h"
#include "watcher.h"
#include "utils.h"

void handle_event(int fd, struct inotify_event *ev,
                  const char *source, const char *target) {

    char src_path[PATH_MAX], dst_path[PATH_MAX];
    const char *watch_path = get_watch_path(ev->wd);
    
    if (!watch_path) return;

    snprintf(src_path, sizeof(src_path),
                "%s/%s", watch_path, ev->name);
    map_path(src_path, source, target, dst_path);

    if (ev->mask & IN_CREATE || ev->mask & IN_MOVED_TO) {
        struct stat st;
        lstat(src_path, &st);

        if (S_ISDIR(st.st_mode)) {
            copy_recursive(src_path, dst_path);
            add_watches_recursive(fd, src_path);
        } else if (S_ISREG(st.st_mode)) {
            copy_recursive(src_path, dst_path);
        } else if (S_ISLNK(st.st_mode)) {
            char buf[PATH_MAX];
            ssize_t len = readlink(src_path, buf, sizeof(buf)-1);
            if (len >= 0) {
                buf[len] = 0;
                symlink(buf, dst_path);
            }
        }
    }

    if (ev->mask & IN_DELETE || ev->mask & IN_MOVED_FROM) {
        unlink(dst_path);
    }

    if (ev->mask & IN_MODIFY) {
        copy_recursive(src_path, dst_path);
    }

    if (ev->mask & IN_IGNORED) {
        remove_watch_by_wd(ev->wd);
    }
}

void run_worker(const char *source, const char *target) {
    copy_recursive(source, target);

    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) exit(1);

    add_watches_recursive(fd, source);

    char buf[4096]
        __attribute__((aligned(__alignof__(struct inotify_event))));

    while (1) {
        int len = read(fd, buf, sizeof(buf));
        if (len < 0) {
            struct timespec ts = {0, 100000000};
            nanosleep(&ts, NULL);
            continue;
        }

        for (char *ptr = buf; ptr < buf + len; ) {
            struct inotify_event *ev = (struct inotify_event *)ptr;
            handle_event(fd, ev, source, target);
            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }
}
