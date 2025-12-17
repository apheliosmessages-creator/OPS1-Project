#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/inotify.h>
#include <poll.h>
#include <wordexp.h>
#include <openssl/sha.h>
#include <time.h>



#define MAX_BACKUPS 32

#define MAX_WATCHES 1024

typedef struct {
    int wd;
    char path[PATH_MAX];
} Watch;

static Watch watches[MAX_WATCHES];
static int watch_count = 0;

char *real_path(const char *path, char *out);
void copy_recursive(const char *src, const char *dst);

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

int parse_command(const char *line, wordexp_t *p) {
    return wordexp(line, p, WRDE_NOCMD);
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

void map_path(const char *src, const char *source,
              const char *target, char *out) {
    snprintf(out, PATH_MAX, "%s%s", target, src + strlen(source));
}

int file_hash(const char *path, unsigned char out[SHA256_DIGEST_LENGTH]) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        SHA256_Update(&ctx, buf, n);

    fclose(f);
    SHA256_Final(out, &ctx);
    return 0;
}

int files_differ(const char *a, const char *b) {
    unsigned char ha[SHA256_DIGEST_LENGTH];
    unsigned char hb[SHA256_DIGEST_LENGTH];

    if (file_hash(a, ha) < 0 || file_hash(b, hb) < 0)
        return 1;

    return memcmp(ha, hb, SHA256_DIGEST_LENGTH) != 0;
}

void restore_copy(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) < 0) return;

    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, 0755);

        DIR *d = opendir(src);
        if (!d) return;

        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;

            char s[PATH_MAX], t[PATH_MAX];
            snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
            snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);

            restore_copy(s, t);
        }
        closedir(d);
    }
    else if (S_ISREG(st.st_mode)) {
        if (access(dst, F_OK) == 0 && !files_differ(src, dst))
            return; // unchanged

        FILE *in = fopen(src, "rb");
        FILE *out = fopen(dst, "wb");
        if (!in || !out) return;

        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
            fwrite(buf, 1, n, out);

        fclose(in);
        fclose(out);
    }
    else if (S_ISLNK(st.st_mode)) {
        char buf[PATH_MAX];
        ssize_t len = readlink(src, buf, sizeof(buf)-1);
        if (len < 0) return;
        buf[len] = 0;

        unlink(dst);
        symlink(buf, dst);
    }
}
void restore_cleanup(const char *src, const char *ref) {
    DIR *d = opendir(src);
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        char s[PATH_MAX], r[PATH_MAX];
        snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
        snprintf(r, sizeof(r), "%s/%s", ref, e->d_name);

        if (access(r, F_OK) != 0) {
            struct stat st;
            lstat(s, &st);
            if (S_ISDIR(st.st_mode)) {
                restore_cleanup(s, r);
                rmdir(s);
            } else {
                unlink(s);
            }
        } else {
            struct stat st;
            lstat(s, &st);
            if (S_ISDIR(st.st_mode))
                restore_cleanup(s, r);
        }
    }
    closedir(d);
}
void cmd_restore(const char *source, const char *target) {
    char rs[PATH_MAX], rt[PATH_MAX];

    if (!real_path(source, rs) || !real_path(target, rt))
        return;

    printf("Restoring backup...\n");

    restore_copy(rt, rs);
    restore_cleanup(rs, rt);

    printf("Restore complete\n");
}

void add_watches_recursive(int fd, const char *root);

void handle_event(int fd, struct inotify_event *ev,
                  const char *source, const char *target) {

    char src_path[PATH_MAX], dst_path[PATH_MAX];

    for (int i = 0; i < watch_count; i++) {
        if (watches[i].wd == ev->wd) {
            snprintf(src_path, sizeof(src_path),
                     "%s/%s", watches[i].path, ev->name);
            map_path(src_path, source, target, dst_path);
            break;
        }
    }

    if (ev->mask & IN_CREATE || ev->mask & IN_MOVED_TO) {
        struct stat st;
        lstat(src_path, &st);

        if (S_ISDIR(st.st_mode)) {
            mkdir(dst_path, 0755);
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
        for (int i = 0; i < watch_count; i++) {
            if (watches[i].wd == ev->wd) {
                // Move last element to this position
                watches[i] = watches[watch_count - 1];
                watch_count--;
                break;
            }
        }
    }
}


typedef struct {
    char source[PATH_MAX];
    char target[PATH_MAX];
    pid_t pid;
} BackupTarget;

static BackupTarget backups[MAX_BACKUPS];
static int backup_count = 0;


char *real_path(const char *path, char *out) {
    if (!realpath(path, out)) {
        perror("realpath");
        return NULL;
    }
    return out;
}
int dir_empty(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -1;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return 1;
}
int is_subpath(const char *parent, const char *child) {
    size_t len = strlen(parent);
    return strncmp(parent, child, len) == 0 &&
           (child[len] == '/' || child[len] == '\0');
}

void copy_recursive(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) < 0) {
        perror("lstat");
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, st.st_mode & 0777);

        DIR *dir = opendir(src);
        if (!dir) return;

        struct dirent *e;
        while ((e = readdir(dir))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;

            char s[PATH_MAX], t[PATH_MAX];
            snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
            snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);
            copy_recursive(s, t);
        }
        closedir(dir);
    }
    else if (S_ISREG(st.st_mode)) {
        FILE *in = fopen(src, "rb");
        FILE *out = fopen(dst, "wb");
        if (!in || !out) return;

        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
            fwrite(buf, 1, n, out);

        fclose(in);
        fclose(out);
    }
    else if (S_ISLNK(st.st_mode)) {
        char linkbuf[PATH_MAX];
        ssize_t len = readlink(src, linkbuf, sizeof(linkbuf) - 1);
        if (len < 0) return;
        linkbuf[len] = '\0';
        symlink(linkbuf, dst);
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


int backup_exists(const char *src, const char *dst) {
    for (int i = 0; i < backup_count; i++) {
        if (!strcmp(backups[i].source, src) &&
            !strcmp(backups[i].target, dst))
            return 1;
    }
    return 0;
}


void cmd_add(char *src, char *dst) {
    if (backup_count >= MAX_BACKUPS) {
        printf("Too many backups\n");
        return;
    }

    char rs[PATH_MAX], rt[PATH_MAX];

    if (!real_path(src, rs)) return;
    if (is_subpath(rs, rt) || is_subpath(rt, rs)) {
    printf("Error: recursive backup not allowed\n");
    return;
}

if (backup_exists(rs, rt)) {
    printf("Error: backup already exists\n");
    return;
}



    if (access(dst, F_OK) == 0) {
        if (!dir_empty(dst)) {
            printf("Target not empty\n");
            return;
        }
    } else {
        mkdir(dst, 0755);
    }

    if (!real_path(dst, rt)) return;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        run_worker(rs, rt);
        exit(0);
    }

    strcpy(backups[backup_count].source, rs);
    strcpy(backups[backup_count].target, rt);
    backups[backup_count].pid = pid;
    backup_count++;

    printf("Backup started\n");
}
void cmd_list(void) {
    for (int i = 0; i < backup_count; i++) {
        printf("Source: %s\n", backups[i].source);
        for (int j = i; j < backup_count; j++) {
            if (!strcmp(backups[i].source, backups[j].source))
                printf("  -> %s (pid %d)\n",
                       backups[j].target, backups[j].pid);
        }
        while (i + 1 < backup_count &&
               !strcmp(backups[i].source, backups[i + 1].source))
            i++;
    }
}

void cmd_end(char *src, char *dst) {
    char rs[PATH_MAX], rt[PATH_MAX];
    if (!real_path(src, rs) || !real_path(dst, rt)) return;

    for (int i = 0; i < backup_count; i++) {
        if (!strcmp(backups[i].source, rs) &&
            !strcmp(backups[i].target, rt)) {

            kill(backups[i].pid, SIGTERM);
            waitpid(backups[i].pid, NULL, 0);

            backups[i] = backups[backup_count - 1];
            backup_count--;

            printf("Backup ended\n");
            return;
        }
    }
    printf("Backup not found\n");
}

int main(void) {
    char line[1024];

    printf("Commands: add <src> <dst>, end <src> <dst>, list, exit\n");

    while (1) {
    printf("> ");
    fflush(stdout);
   
    if (!fgets(line, sizeof(line), stdin))
        break;

    line[strcspn(line, "\n")] = 0;

    wordexp_t p;
    int ret = parse_command(line, &p);
    if (ret != 0) {
        printf("Parse error: %d on line: %s\n", ret, line);
        continue;
    }

    if (p.we_wordc == 0) {
        wordfree(&p);
        continue;
    }

    char **argv = p.we_wordv;
    int argc = p.we_wordc;

   if (!strcmp(argv[0], "add")) {
    if (argc >= 3) {
        for (int i = 2; i < argc; i++)
            cmd_add(argv[1], argv[i]);
    } else {
        printf("Usage: add <src> <target...>\n");
    }
}
    else if (!strcmp(argv[0], "end")) {
    if (argc >= 3) {
        for (int i = 2; i < argc; i++)
            cmd_end(argv[1], argv[i]);
    } else {
        printf("Usage: end <src> <target...>\n");
    }
}
else if (!strcmp(argv[0], "restore")) {
    if (argc == 3)
        cmd_restore(argv[1], argv[2]);
    else
        printf("Usage: restore <src> <target>\n");
}


    else if (!strcmp(argv[0], "list")) {
        cmd_list();
    }
    else if (!strcmp(argv[0], "exit")) {
        wordfree(&p);
        break;
    }
    else {
        printf("Unknown command\n");
    }

    wordfree(&p);
}


    for (int i = 0; i < backup_count; i++) {
        kill(backups[i].pid, SIGTERM);
        waitpid(backups[i].pid, NULL, 0);
    }

    return 0;
}
