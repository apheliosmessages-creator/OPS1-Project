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
#include <openssl/sha.h>
#include "utils.h"

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

void map_path(const char *src, const char *source,
              const char *target, char *out) {
    snprintf(out, PATH_MAX, "%s%s", target, src + strlen(source));
}
