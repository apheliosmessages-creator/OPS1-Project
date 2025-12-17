#ifndef UTILS_H
#define UTILS_H

#include <openssl/sha.h>
#include <limits.h>

char *real_path(const char *path, char *out);
int dir_empty(const char *path);
int is_subpath(const char *parent, const char *child);
void copy_recursive(const char *src, const char *dst);
int file_hash(const char *path, unsigned char out[SHA256_DIGEST_LENGTH]);
int files_differ(const char *a, const char *b);
void restore_copy(const char *src, const char *dst);
void restore_cleanup(const char *src, const char *ref);
void map_path(const char *src, const char *source, const char *target, char *out);

#endif
