#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>

#include "commands.h"
#include "worker.h"
#include "utils.h"

#define MAX_BACKUPS 32

typedef struct {
    char source[PATH_MAX];
    char target[PATH_MAX];
    pid_t pid;
} BackupTarget;

static BackupTarget backups[MAX_BACKUPS];
static int backup_count = 0;

int parse_command(const char *line, wordexp_t *p) {
    return wordexp(line, p, WRDE_NOCMD);
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

void cmd_restore(const char *source, const char *target) {
    char rs[PATH_MAX], rt[PATH_MAX];

    if (!real_path(source, rs) || !real_path(target, rt))
        return;

    printf("Restoring backup...\n");

    restore_copy(rt, rs);
    restore_cleanup(rs, rt);

    printf("Restore complete\n");
}

void cleanup_backups(void) {
    for (int i = 0; i < backup_count; i++) {
        kill(backups[i].pid, SIGTERM);
        waitpid(backups[i].pid, NULL, 0);
    }
}
